import os
import sys
import time
import json
import win32pipe
import win32file
import win32event
import pywintypes
import cv2

# Ensure root dir is in path
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from engine.face_engine import FaceEngine
from engine.storage import load_profile

PIPE_NAME = r"\\.\pipe\WindowsHelloRefinedPipe"

class FaceDaemon:
    def __init__(self, camera_index=0):
        self.camera_index = camera_index
        print("[*] Initializing FaceDaemon engine...")
        self.engine = FaceEngine()
        self.profile = load_profile()
        self.camera = None
        if self.profile:
            print(f"[*] Loaded profile for user: {self.profile['username']} ({len(self.profile['embeddings'])} embeddings)")
        else:
            print("[!] Warning: No enrolled profile found yet.")

    def ensure_camera(self):
        if self.camera is None or not self.camera.isOpened():
            self.camera = cv2.VideoCapture(self.camera_index, cv2.CAP_DSHOW)
            if not self.camera.isOpened():
                self.camera = cv2.VideoCapture(self.camera_index, cv2.CAP_MSMF)
            if self.camera.isOpened():
                self.camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
                self.camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        return self.camera.isOpened()

    def release_camera(self):
        if self.camera is not None and self.camera.isOpened():
            self.camera.release()
            self.camera = None

    def verify_current_face(self, timeout_sec=3.0):
        """
        Attempts to verify face from camera within timeout_sec.
        Returns dict with success status, username, confidence, message.
        """
        if not self.profile:
            # Try reloading profile in case it was just enrolled
            self.profile = load_profile()
            if not self.profile:
                return {"status": "error", "message": "No enrolled profile"}

        if not self.ensure_camera():
            return {"status": "error", "message": "Could not access camera"}

        start_time = time.time()
        enrolled = self.profile["embeddings"]
        username = self.profile["username"]

        while time.time() - start_time < timeout_sec:
            ret, frame = self.camera.read()
            if not ret:
                time.sleep(0.02)
                continue

            enhanced = self.engine.preprocess_lighting(frame)
            face, _ = self.engine.detect_face(enhanced)

            if face is not None:
                is_live, score, live_msg = self.engine.check_liveness(frame, face)
                if not is_live:
                    continue  # Reject spoof

                feature = self.engine.extract_features(enhanced, face)
                is_match, similarity = self.engine.match_face(feature, enrolled)

                if is_match:
                    return {
                        "status": "verified",
                        "username": username,
                        "confidence": round(similarity * 100, 1),
                        "message": f"Welcome, {username}!"
                    }

            time.sleep(0.01)

        return {"status": "failed", "message": "Face verification timed out or unrecognized"}

    def handle_client(self, h_pipe):
        """Processes request from Credential Provider client."""
        try:
            # Read command from pipe
            result, data = win32file.ReadFile(h_pipe, 4096)
            cmd_str = data.decode('utf-8').strip()
            print(f"[*] Received command: {cmd_str}")

            response = {}
            if cmd_str == "PING":
                response = {"status": "ok", "message": "PONG"}
            elif cmd_str == "STATUS":
                has_prof = self.profile is not None
                response = {
                    "status": "ready" if has_prof else "unconfigured",
                    "user": self.profile["username"] if has_prof else None
                }
            elif cmd_str == "VERIFY":
                print("[*] Performing face verification...")
                response = self.verify_current_face(timeout_sec=4.0)
                print(f"[*] Verification result: {response}")
            else:
                response = {"status": "unknown_command"}

            res_bytes = (json.dumps(response) + "\n").encode('utf-8')
            win32file.WriteFile(h_pipe, res_bytes)
        except Exception as e:
            print(f"[!] Error handling pipe client: {e}")
        finally:
            win32pipe.DisconnectNamedPipe(h_pipe)

    def run_server(self):
        print(f"[*] Windows Hello Refined Service active on {PIPE_NAME}")
        # Warm up camera once at startup so driver is initialized
        self.ensure_camera()
        time.sleep(0.5)
        print("[*] Camera warmed up and ready.")

        while True:
            # Create Named Pipe for IPC
            h_pipe = win32pipe.CreateNamedPipe(
                PIPE_NAME,
                win32pipe.PIPE_ACCESS_DUPLEX,
                win32pipe.PIPE_TYPE_MESSAGE | win32pipe.PIPE_READMODE_MESSAGE | win32pipe.PIPE_WAIT,
                win32pipe.PIPE_UNLIMITED_INSTANCES,
                65536,
                65536,
                0,
                None
            )

            try:
                # Wait for LogonUI or client to connect
                win32pipe.ConnectNamedPipe(h_pipe, None)
                self.handle_client(h_pipe)
            except Exception as e:
                time.sleep(0.05)
            finally:
                try:
                    win32file.CloseHandle(h_pipe)
                except Exception:
                    pass

if __name__ == "__main__":
    idx = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    daemon = FaceDaemon(idx)
    daemon.run_server()
