import os
import sys
import time
import ctypes
import cv2

os.environ["OPENCV_VIDEOIO_ENABLE_OBSENSOR"] = "0"
os.environ["OPENCV_VIDEOIO_PRIORITY_OBSENSOR"] = "0"
os.environ["OPENCV_VIDEOIO_PRIORITY_MSMF"] = "0"
os.environ["OPENCV_LOG_LEVEL"] = "ERROR"

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from engine.face_engine import FaceEngine

LOG_FILE = r"C:\Users\Public\WindowsHelloRefined.log"
CHECK_INTERVAL = 3.0
MAX_MISSED_CHECKS = 10
MUTEX_NAME = "Global\\WindowsHelloRefinedPresence"


def log_service(message):
    line = f"[{time.strftime('%H:%M:%S')}] [PRESENCE] {message}\n"
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as handle:
            handle.write(line)
    except Exception:
        pass


def acquire_single_instance():
    kernel32 = ctypes.windll.kernel32
    mutex = kernel32.CreateMutexW(None, False, MUTEX_NAME)
    if not mutex:
        log_service(f"Unable to create single-instance mutex (error {kernel32.GetLastError()}).")
        return None
    if kernel32.GetLastError() == 183:
        kernel32.CloseHandle(mutex)
        log_service("Another presence watcher is already running; exiting duplicate instance.")
        return None
    return mutex


def is_locked():
    user32 = ctypes.windll.user32
    desktop = user32.OpenInputDesktop(0, False, 0x0001)
    if not desktop:
        return False
    try:
        name = ctypes.create_unicode_buffer(64)
        needed = ctypes.c_ulong()
        ok = user32.GetUserObjectInformationW(
            desktop, 2, name, ctypes.sizeof(name), ctypes.byref(needed)
        )
        return bool(ok and name.value.lower() == "winlogon")
    finally:
        user32.CloseDesktop(desktop)


def open_camera(index):
    camera = cv2.VideoCapture(index, cv2.CAP_DSHOW)
    if not camera.isOpened():
        camera.release()
        camera = cv2.VideoCapture(index, cv2.CAP_MSMF)
    if camera.isOpened():
        camera.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        return camera
    camera.release()
    return None


def run():
    mutex = acquire_single_instance()
    if mutex is None:
        return

    engine = FaceEngine()
    missed = 0
    next_check = time.monotonic()
    log_service("Standalone presence watcher active: every 3s, lock after 10 misses.")

    try:
        while True:
            next_check += CHECK_INTERVAL
            delay = next_check - time.monotonic()
            if delay > 0:
                time.sleep(delay)

            try:
                if is_locked():
                    missed = 0
                    continue

                camera = open_camera(0)
                if camera is None:
                    log_service("Camera unavailable; sample ignored.")
                    continue

                try:
                    ok, frame = camera.read()
                finally:
                    camera.release()

                if not ok:
                    log_service("Camera read failed; sample ignored.")
                    continue

                face, _ = engine.detect_face(frame)
                if face is not None:
                    if missed:
                        log_service("Face detected; resetting missed count.")
                    missed = 0
                    continue

                missed += 1
                log_service(f"No face detected; missed check {missed}/{MAX_MISSED_CHECKS}.")
                if missed >= MAX_MISSED_CHECKS:
                    log_service("10 missed checks in 30s; locking workstation.")
                    ctypes.windll.user32.LockWorkStation()
                    missed = 0
                    time.sleep(5.0)
            except Exception as exc:
                missed = 0
                log_service(f"Presence sample skipped: {exc}")
    finally:
        ctypes.windll.kernel32.CloseHandle(mutex)


if __name__ == "__main__":
    run()
