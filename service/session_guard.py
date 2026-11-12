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
    def __init__(self, away_timeout=30, check_interval=3, camera_index=0):
        """
        Walk-Away Auto-Lock:
        Monitors presence. If user steps away for > away_timeout seconds,
        it automatically locks Windows (Win + L).
        """
        self.away_timeout = away_timeout
        self.check_interval = check_interval
        self.max_missed_checks = max(1, int(round(away_timeout / check_interval)))
        self.camera_index = camera_index
        self.engine = FaceEngine()
        self.missed_checks = 0
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

        try:
            while self.is_running:
                ret, frame = cap.read()
                if not ret:
                    time.sleep(self.check_interval)
                    continue

                face, _ = self.engine.detect_face(frame)
                if face is not None:
                    # User is present
                    if self.missed_checks:
                        print("[*] User detected. Resetting missed-check count.       ", end="\r")
                    self.missed_checks = 0
                else:
                    self.missed_checks += 1
                    elapsed = self.missed_checks * self.check_interval
                    print(
                        f"[?] User away: {self.missed_checks}/{self.max_missed_checks} checks "
                        f"({elapsed}/{self.away_timeout}s)...            ",
                        end="\r",
                    )
                    if self.missed_checks >= self.max_missed_checks:
                        self.lock_workstation()
                        self.missed_checks = 0
                        # Pause checking while workstation is locked
                        time.sleep(5)

                time.sleep(self.check_interval)

        except KeyboardInterrupt:
            print("\nPresence guard stopped.")
        finally:
            cap.release()

if __name__ == "__main__":
    print("=" * 68)
    print("          WINDOWS HELLO REFINED - PRESENCE AUTO-LOCK GUARD")
    print("                        Made by Aryan Das")
    print("          GitHub:   https://github.com/aryandas079 (@aryandas079)")
    print("          LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)")
    print("=" * 68)
    guard = SessionGuard(away_timeout=30, check_interval=3)
    guard.start()
