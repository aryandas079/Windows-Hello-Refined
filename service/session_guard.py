import os
import sys
import time
import ctypes
import cv2

# Ensure root dir is in path
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from engine.face_engine import FaceEngine
from engine.storage import load_profile

class SessionGuard:
    def __init__(self, away_timeout=25, check_interval=3, camera_index=0):
        """
        Walk-Away Auto-Lock:
        Monitors presence. If user steps away for > away_timeout seconds,
        it automatically locks Windows (Win + L).
        """
        self.away_timeout = away_timeout
        self.check_interval = check_interval
        self.camera_index = camera_index
        self.engine = FaceEngine()
        self.last_seen_time = time.time()
        self.is_running = True

    def lock_workstation(self):
        print("\n[!] User walked away! Locking workstation...")
        ctypes.windll.user32.LockWorkStation()

    def start(self):
        print("=" * 60)
        print("   WINDOWS HELLO REFINED - PRESENCE & WALK-AWAY LOCK")
        print(f"   Auto-lock after {self.away_timeout}s of absence.")
        print("=" * 60)

        cap = cv2.VideoCapture(self.camera_index, cv2.CAP_DSHOW)
        if not cap.isOpened():
            cap = cv2.VideoCapture(self.camera_index, cv2.CAP_MSMF)
        if not cap.isOpened():
            print("[!] Could not open camera for presence guard.")
            return

        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 320)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 240)

        self.last_seen_time = time.time()

        try:
            while self.is_running:
                ret, frame = cap.read()
                if not ret:
                    time.sleep(self.check_interval)
                    continue

                face, _ = self.engine.detect_face(frame)
                now = time.time()

                if face is not None:
                    # User is present
                    self.last_seen_time = now
                    print(f"[*] User present. Resetting away timer. [{time.strftime('%H:%M:%S')}]", end="\r")
                else:
                    away_sec = int(now - self.last_seen_time)
                    print(f"[?] User away: {away_sec}/{self.away_timeout}s...            ", end="\r")
                    if away_sec >= self.away_timeout:
                        self.lock_workstation()
                        self.last_seen_time = time.time()  # Reset after locking
                        # Pause checking while workstation is locked
                        time.sleep(5)

                time.sleep(self.check_interval)

        except KeyboardInterrupt:
            print("\nPresence guard stopped.")
        finally:
            cap.release()

if __name__ == "__main__":
    guard = SessionGuard(away_timeout=25, check_interval=2)
    guard.start()
