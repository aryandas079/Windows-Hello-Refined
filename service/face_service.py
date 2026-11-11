import os
import sys
import time
import json
import threading
import win32pipe
import win32file
import win32security
import pywintypes
import cv2

# Ensure root dir is in path
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from engine.face_engine import FaceEngine
from engine.storage import load_profile, load_credentials

PIPE_NAME = r"\\.\pipe\WindowsHelloRefinedPipe"
LOG_FILE = r"C:\Users\Public\WindowsHelloRefined.log"

def log_service(msg):
    timestamp = time.strftime("[%H:%M:%S]")
    line = f"{timestamp} [SERVICE] {msg}\n"
    print(line.strip(), flush=True)
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(line)
    except Exception:
        pass

def get_pipe_security_attributes():
    """Create Security Attributes with NULL DACL so SYSTEM (LogonUI) and all users can access the pipe."""
    sa = win32security.SECURITY_ATTRIBUTES()
    sa.bInheritHandle = True
    sd = win32security.SECURITY_DESCRIPTOR()
    sd.Initialize()
    # NULL DACL = Full access to Everyone and SYSTEM
    sd.SetSecurityDescriptorDacl(True, None, False)
    sa.SECURITY_DESCRIPTOR = sd
    return sa

class FaceDaemon:
    def __init__(self, camera_index=0):
        self.camera_index = camera_index
        self.camera_lock = threading.Lock()
        log_service("Initializing FaceDaemon engine...")
        self.engine = FaceEngine()
        self.profile = load_profile()
        self.camera = None
        if self.profile:
            log_service(f"Loaded profile for user: {self.profile['username']} ({len(self.profile['embeddings'])} embeddings)")
        else:
            log_service("Warning: No enrolled profile found yet.")

    def ensure_camera(self):
        """Open the camera ONLY when needed."""
        if self.camera is None or not self.camera.isOpened():
            self.camera = cv2.VideoCapture(self.camera_index, cv2.CAP_DSHOW)
            if not self.camera.isOpened():
                self.camera = cv2.VideoCapture(self.camera_index, cv2.CAP_MSMF)
            if self.camera.isOpened():
                self.camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
                self.camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        return self.camera.isOpened() if self.camera else False

    def release_camera(self):
        """Release the camera hardware IMMEDIATELY so the camera light turns off."""
        if self.camera is not None:
            try:
                if self.camera.isOpened():
                    self.camera.release()
            except Exception:
                pass
            self.camera = None
            log_service("Camera hardware released (LED off).")

    def verify_current_face(self, timeout_sec=8.0):
        """
        Runs face recognition for up to timeout_sec seconds.
        GUARANTEES camera is opened only during verification and released immediately after.
        """
        if not self.profile:
            self.profile = load_profile()
            if not self.profile:
                return {"status": "error", "message": "No enrolled profile"}

        username = self.profile["username"]
        enrolled = self.profile["embeddings"]

        with self.camera_lock:
            log_service("Opening camera for lock screen verification...")
            try:
                if not self.ensure_camera():
                    log_service("ERROR: Could not access camera.")
                    return {"status": "error", "message": "Could not access camera"}

                # Discard 2 frames for auto-exposure stabilization
                for _ in range(2):
                    self.camera.read()

                start_time = time.time()
                frames_tested = 0
                while time.time() - start_time < timeout_sec:
                    ret, frame = self.camera.read()
                    if not ret:
                        time.sleep(0.015)
                        continue

                    frames_tested += 1
                    face, status = self.engine.detect_face(frame)

                    if face is not None:
                        is_live, lscore, live_msg = self.engine.check_liveness(frame, face)
                        feature = self.engine.extract_features(frame, face)
                        is_match, similarity = self.engine.match_face(feature, enrolled)

                        log_service(f"Frame {frames_tested}: live={is_live} ({live_msg}), match={is_match} (sim={similarity:.3f})")

                        # Verified match: either full liveness + match, OR very high biometric match
                        if is_match and (is_live or similarity >= 0.42):
                            log_service(f"Match verified! User: {username}, Similarity: {similarity:.2f}")
                            creds = load_credentials()
                            resp = {
                                "status": "verified",
                                "username": username,
                                "confidence": round(similarity * 100, 1),
                                "message": f"Welcome back, {username}!"
                            }
                            if creds:
                                if creds.get("pin"):
                                    resp["pin"] = creds["pin"]
                                if creds.get("password"):
                                    resp["password"] = creds["password"]
                                if creds.get("domain"):
                                    resp["domain"] = creds["domain"]
                            return resp

                    time.sleep(0.01)

                log_service(f"Face verification timed out after {frames_tested} frames ({timeout_sec:.1f}s).")
                return {"status": "failed", "message": "Face verification timed out"}
            finally:
                # ALWAYS release camera hardware immediately so LED turns off!
                self.release_camera()

    def handle_client(self, h_pipe):
        try:
            result, data = win32file.ReadFile(h_pipe, 4096)
            cmd_str = data.decode('utf-8').strip()
            log_service(f"Pipe received command: {cmd_str}")

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
                log_service("Lock screen requested face verification...")
                response = self.verify_current_face(timeout_sec=8.0)
                log_service(f"Verification outcome: status={response.get('status')}")
            else:
                response = {"status": "unknown_command"}

            res_bytes = (json.dumps(response) + "\n").encode('utf-8')
            win32file.WriteFile(h_pipe, res_bytes)
        except Exception as e:
            log_service(f"Error handling pipe client: {e}")
        finally:
            try:
                win32pipe.DisconnectNamedPipe(h_pipe)
            except Exception:
                pass
            try:
                win32file.CloseHandle(h_pipe)
            except Exception:
                pass

    def run_server(self):
        log_service(f"Windows Hello Refined Service active on {PIPE_NAME}")
        log_service("Camera is IDLE (LED is OFF). Waiting for lock-screen requests...")

        sa = get_pipe_security_attributes()

        while True:
            try:
                h_pipe = win32pipe.CreateNamedPipe(
                    PIPE_NAME,
                    win32pipe.PIPE_ACCESS_DUPLEX,
                    win32pipe.PIPE_TYPE_MESSAGE | win32pipe.PIPE_READMODE_MESSAGE | win32pipe.PIPE_WAIT,
                    win32pipe.PIPE_UNLIMITED_INSTANCES,
                    65536,
                    65536,
                    0,
                    sa
                )

                try:
                    win32pipe.ConnectNamedPipe(h_pipe, None)
                except pywintypes.error as e:
                    if e.winerror == 535:  # ERROR_PIPE_CONNECTED (client already connected)
                        pass
                    else:
                        raise

                # Handle client in a thread so server never blocks listening
                client_thread = threading.Thread(target=self.handle_client, args=(h_pipe,))
                client_thread.daemon = True
                client_thread.start()

            except Exception as e:
                log_service(f"Server loop exception: {e}")
                time.sleep(0.05)

if __name__ == "__main__":
    idx = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    daemon = FaceDaemon(idx)
    daemon.run_server()
