import os
import sys
import time
import ctypes
import subprocess
import cv2
import numpy as np

os.environ["OPENCV_VIDEOIO_ENABLE_OBSENSOR"] = "0"
os.environ["OPENCV_VIDEOIO_PRIORITY_OBSENSOR"] = "0"
os.environ["OPENCV_VIDEOIO_PRIORITY_MSMF"] = "0"
os.environ["OPENCV_LOG_LEVEL"] = "ERROR"

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from engine.face_engine import FaceEngine
from service.presence_config import CHECK_INTERVAL, label, load_timeout

LOG_FILE = r"C:\Users\Public\WindowsHelloRefined.log"
MUTEX_NAME = "Global\\WindowsHelloRefinedPresence"


def log_service(message):
    line = f"[{time.strftime('%H:%M:%S')}] [PRESENCE] {message}\n"
    print(line.strip(), flush=True)
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


def is_user_active(idle_threshold_sec=3.0):
    """Return True if user has interacted with keyboard or mouse recently."""
    class LASTINPUTINFO(ctypes.Structure):
        _fields_ = [("cbSize", ctypes.c_uint), ("dwTime", ctypes.c_uint)]
    lii = LASTINPUTINFO()
    lii.cbSize = ctypes.sizeof(LASTINPUTINFO)
    if ctypes.windll.user32.GetLastInputInfo(ctypes.byref(lii)):
        tick = ctypes.windll.kernel32.GetTickCount()
        idle_sec = (tick - lii.dwTime) / 1000.0
        return idle_sec < idle_threshold_sec
    return False


def is_locked():
    """Return True when workstation is locked (Winlogon desktop is active)."""
    user32 = ctypes.windll.user32
    desktop = user32.OpenInputDesktop(0, False, 0x0001)
    if not desktop:
        err = ctypes.GetLastError()
        # When desktop switched to Winlogon, standard user tokens get ERROR_ACCESS_DENIED (5)
        return (err == 5)
    try:
        name = ctypes.create_unicode_buffer(64)
        needed = ctypes.c_ulong()
        ok = user32.GetUserObjectInformationW(
            desktop, 2, name, ctypes.sizeof(name), ctypes.byref(needed)
        )
        return bool(ok and name.value.lower() == "winlogon")
    finally:
        user32.CloseDesktop(desktop)


def lock_workstation():
    """Immediately lock Windows session."""
    try:
        ctypes.windll.user32.LockWorkStation()
    except Exception:
        pass
    try:
        subprocess.run(["rundll32.exe", "user32.dll,LockWorkStation"], check=False)
    except Exception:
        pass


def is_valid_face_presence(frame, face):
    """
    Validate whether a detected face bounding box is a genuine human face.
    Rejects phantom false-positive detections caused by webcam sensor noise
    or background objects in empty rooms.
    """
    if face is None:
        return False
    x, y, w, h = [int(v) for v in face[:4]]
    if w < 50 or h < 50:
        return False
    conf = float(face[-1]) if len(face) > 14 else 1.0
    if conf < 0.65:
        return False
    H, W = frame.shape[:2]
    x1, y1 = max(0, x), max(0, y)
    x2, y2 = min(W, x + w), min(H, y + h)
    crop = frame[y1:y2, x1:x2]
    if crop.size == 0:
        return False
    gray = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY)
    mean_lum = float(np.mean(gray))
    lap_var = float(cv2.Laplacian(gray, cv2.CV_64F).var())
    if mean_lum < 12.0 or lap_var < 0.8:
        return False
    return True


def open_camera(index):
    camera = cv2.VideoCapture(index, cv2.CAP_DSHOW)
    if not camera.isOpened():
        camera.release()
        camera = cv2.VideoCapture(index, cv2.CAP_MSMF)
    if camera.isOpened():
        camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        camera.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        return camera
    camera.release()
    return None


def run():
    mutex = acquire_single_instance()
    if mutex is None:
        return

    engine = FaceEngine()
    next_check = time.monotonic()
    absence_start = None
    camera = None
    log_service(f"Standalone presence watcher active: camera pulses every {int(CHECK_INTERVAL)}s.")

    try:
        while True:
            timeout = load_timeout()
            next_check += CHECK_INTERVAL
            delay = next_check - time.monotonic()
            if delay > 0:
                time.sleep(delay)

            try:
                if timeout is None:
                    absence_start = None
                    continue
                if is_locked():
                    absence_start = None
                    continue
                if is_user_active(CHECK_INTERVAL):
                    if absence_start is not None:
                        log_service("User activity detected (typing/mouse); resetting absence timer.")
                    absence_start = None
                    continue

                if camera is None or not camera.isOpened():
                    camera = open_camera(0)
                if camera is None:
                    log_service("Camera unavailable; presence check skipped.")
                    continue

                ok, frame = camera.read()
                if not ok:
                    log_service("Camera read failed; presence check skipped.")
                    camera.release()
                    camera = None
                    continue

                face, _ = engine.detect_face(frame)
                camera.release()
                camera = None

                present = is_valid_face_presence(frame, face)
                if present:
                    if absence_start is not None:
                        log_service("Face detected in front of screen; user presence confirmed.")
                    absence_start = None
                    continue

                # User is NOT active and NOT in front of camera
                now = time.monotonic()
                if absence_start is None:
                    absence_start = now

                elapsed = now - absence_start
                log_service(f"No face detected; absence duration: {elapsed:.1f}s / {timeout}s.")

                if elapsed >= float(timeout):
                    log_service(
                        f"User absent for {elapsed:.1f}s (configured timeout: {label(timeout)} reached). "
                        "Locking device immediately!"
                    )
                    lock_workstation()
                    absence_start = None
                    time.sleep(4.0)

            except Exception as exc:
                absence_start = None
                log_service(f"Presence sample skipped: {exc}")
    finally:
        if camera is not None:
            camera.release()
        ctypes.windll.kernel32.CloseHandle(mutex)


if __name__ == "__main__":
    run()
