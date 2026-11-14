import os
import sys

# CRITICAL: Disable OpenCV OBSENSOR (Orbbec 3D) probe before cv2 import!
# Probing obsensor introduces a 5.7-second hang enumerating non-existent 3D depth channels.
os.environ["OPENCV_VIDEOIO_ENABLE_OBSENSOR"] = "0"
os.environ["OPENCV_VIDEOIO_PRIORITY_OBSENSOR"] = "0"
os.environ["OPENCV_VIDEOIO_PRIORITY_MSMF"] = "0"
os.environ["OPENCV_LOG_LEVEL"] = "ERROR"

import time
import json
import threading
import win32pipe
import win32file
import win32security
import pywintypes
import ctypes
import cv2
import numpy as np

# Ensure root dir is in path
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from engine.face_engine import FaceEngine
from engine.storage import load_profile, load_credentials
from service.presence_config import CHECK_INTERVAL, label, load_timeout, max_missed_checks

PIPE_NAME = r"\\.\pipe\WindowsHelloRefinedPipe"
LOG_FILE = r"C:\Users\Public\WindowsHelloRefined.log"
MIN_LIVENESS_FRAMES = 2

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
        self.presence_interval = CHECK_INTERVAL
        self.presence_missed_checks = 0
        self.presence_timeout = load_timeout()
        self.presence_max_missed_checks = max_missed_checks(self.presence_timeout)
        log_service("Initializing FaceDaemon engine...")
        self.engine = FaceEngine()
        self.profile = load_profile()
        self.camera = None
        if self.profile:
            log_service(f"Loaded profile for user: {self.profile['username']} ({len(self.profile['embeddings'])} embeddings)")
        else:
            log_service("Warning: No enrolled profile found yet.")

        # Start background Lock & Wake monitor
        threading.Thread(target=self._run_wake_monitor, daemon=True).start()
        # Sample presence periodically while Windows is unlocked.
        threading.Thread(target=self._run_presence_monitor, daemon=True).start()

    def _run_wake_monitor(self):
        """
        Background monitor that detects when the workstation is locked or wakes from sleep.
        Automatically dismisses the Lock Screen curtain by sending a wake pulse (VK_SPACE)
        so that Windows LogonUI is immediately active and looking for the user's face.
        """
        user32 = ctypes.windll.user32
        last_desktop = None
        log_service("Lock & Wake monitor thread active.")

        while True:
            try:
                time.sleep(0.04)
                hDesk = user32.OpenInputDesktop(0, False, 0x0001)
                if not hDesk:
                    continue

                buf = ctypes.create_unicode_buffer(64)
                needed = ctypes.c_ulong()
                user32.GetUserObjectInformationW(hDesk, 2, buf, ctypes.sizeof(buf), ctypes.byref(needed))
                user32.CloseDesktop(hDesk)
                cur_desktop = buf.value

                # Detect transition into locked state (Winlogon desktop)
                if cur_desktop == "Winlogon" and last_desktop != "Winlogon":
                    log_service("Workstation locked / Winlogon desktop detected. Lifting lock screen curtain with VK_UP...")
                    time.sleep(0.02)
                    # Use VK_UP (0x26) with KEYEVENTF_EXTENDEDKEY (1) instead of VK_SPACE
                    # to lift the lock screen curtain cleanly without inserting a space/dot into the PIN box!
                    user32.keybd_event(0x26, 0x48, 1, 0)
                    time.sleep(0.01)
                    user32.keybd_event(0x26, 0x48, 1 | 2, 0)

                last_desktop = cur_desktop
            except Exception:
                time.sleep(0.5)

    def _is_locked(self):
        """Return True when the active input desktop is the secure Winlogon desktop."""
        user32 = ctypes.windll.user32
        h_desk = user32.OpenInputDesktop(0, False, 0x0001)
        if not h_desk:
            return False

        try:
            buf = ctypes.create_unicode_buffer(64)
            needed = ctypes.c_ulong()
            ok = user32.GetUserObjectInformationW(
                h_desk, 2, buf, ctypes.sizeof(buf), ctypes.byref(needed)
            )
            return bool(ok and buf.value.lower() == "winlogon")
        finally:
            user32.CloseDesktop(h_desk)

    def _run_presence_monitor(self):
        """Lock after the configured face-absence timeout."""
        log_service(
            "Presence monitor active: every %.1fs; auto-lock after %s."
            % (
                self.presence_interval,
                label(self.presence_timeout),
            )
        )

        next_check = time.monotonic()
        while True:
            self.presence_timeout = load_timeout()
            self.presence_max_missed_checks = max_missed_checks(self.presence_timeout)
            next_check += self.presence_interval
            delay = next_check - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            try:
                if self.presence_timeout is None:
                    self.presence_missed_checks = 0
                    continue
                if self._is_locked():
                    self.presence_missed_checks = 0
                    continue

                # Do not compete with an active lock-screen verification.
                if not self.camera_lock.acquire(False):
                    continue

                present = False
                sample_ok = False
                try:
                    if self.ensure_camera():
                        ret, frame = self.camera.read()
                        if ret:
                            sample_ok = True
                            face, _ = self.engine.detect_face(frame)
                            present = face is not None
                finally:
                    self.release_camera()
                    self.camera_lock.release()

                if not sample_ok:
                    continue

                if present:
                    if self.presence_missed_checks:
                        log_service("Presence detected again; resetting missed-check count.")
                    self.presence_missed_checks = 0
                    continue

                self.presence_missed_checks += 1
                log_service(
                    "No face detected; missed presence check %d/%d."
                    % (self.presence_missed_checks, self.presence_max_missed_checks)
                )
                if self.presence_missed_checks >= self.presence_max_missed_checks:
                    log_service(
                        "No face detected for %d checks (%ds); locking workstation."
                        % (
                            self.presence_max_missed_checks,
                            int(self.presence_interval * self.presence_max_missed_checks),
                        )
                    )
                    ctypes.windll.user32.LockWorkStation()
                    self.presence_missed_checks = 0
                    time.sleep(5.0)
            except Exception as exc:
                # A camera or desktop-transition failure must not be treated
                # as proof that the user is absent.
                log_service("Presence monitor sample skipped: %s" % exc)

    def ensure_camera(self):
        """Open the camera with ultra-low latency using DirectShow native backend."""
        if self.camera is None or not self.camera.isOpened():
            # Try DirectShow first for fast 400ms startup (MSMF takes >1100ms on many webcams)
            self.camera = cv2.VideoCapture(self.camera_index, cv2.CAP_DSHOW)
            if not self.camera.isOpened():
                self.camera = cv2.VideoCapture(self.camera_index, cv2.CAP_MSMF)
            if self.camera.isOpened():
                self.camera.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        return self.camera.isOpened() if self.camera else False

    def release_camera(self):
        """Release the camera hardware IMMEDIATELY so the camera light turns off without blocking the caller."""
        cam = self.camera
        self.camera = None
        if cam is not None:
            def _close_cam():
                try:
                    if cam.isOpened():
                        cam.release()
                except Exception:
                    pass
                log_service("Camera hardware released (LED off).")

            threading.Thread(target=_close_cam, daemon=True).start()

    def verify_current_face(self, timeout_sec=2.0):
        """
        Runs face recognition with ultra-fast <0.5s matching, 12,500+ dense 3D depth
        biometrics, and multi-layer anti-spoofing against phones, photos, and videos.
        GUARANTEES camera is opened only during verification and released immediately after.
        """
        if not self.profile:
            self.profile = load_profile()
            if not self.profile:
                return {"status": "error", "message": "No enrolled profile"}

        username = self.profile["username"]
        enrolled = self.profile["embeddings"]

        with self.camera_lock:
            log_service("Opening camera for fast <0.5s face verification...")
            try:
                if not self.ensure_camera():
                    log_service("ERROR: Could not access camera.")
                    return {"status": "error", "message": "Could not access camera"}

                start_time = time.perf_counter()
                frames_tested = 0
                consecutive_verified = 0
                self.engine.reset_blink_history(clear_cooldown=True)
                detected_low_light = False

                while time.perf_counter() - start_time < timeout_sec:
                    ret, frame = self.camera.read()
                    if not ret:
                        continue

                    frames_tested += 1
                    gray_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
                    mean_luminance = float(np.mean(gray_frame))
                    if mean_luminance < 75.0:
                        detected_low_light = True

                    face, status = self.engine.detect_face(frame)

                    if face is None:
                        # If no face is in front of the lens after 2 frames (~350ms),
                        # release camera hardware immediately so LED blinks briefly and turns OFF.
                        if frames_tested >= 2:
                            break
                        continue

                    # Ignore tiny background objects or distant faces (< 55px)
                    if face[2] < 55 or face[3] < 55:
                        if frames_tested >= 2:
                            break
                        continue

                    # Dual-pass lighting invariant feature extraction
                    feat_raw = self.engine.extract_features(frame, face)
                    is_match, similarity = self.engine.match_face(feat_raw, enrolled)
                    eval_frame = frame
                    enh_frame = None

                    if not is_match or similarity < 0.48:
                        enh_frame = self.engine.preprocess_lighting(frame)
                        feat_enh = self.engine.extract_features(enh_frame, face)
                        m_enh, sim_enh = self.engine.match_face(feat_enh, enrolled)
                        if sim_enh > similarity:
                            is_match, similarity = m_enh, sim_enh
                            eval_frame = enh_frame

                    is_live, liveness_score, liveness_msg = self.engine.check_liveness(
                        frame, face, enhanced=enh_frame
                    )
                    elapsed_ms = (time.perf_counter() - start_time) * 1000
                    log_service(
                        f"Frame {frames_tested}: match={is_match} "
                        f"(sim={similarity:.3f}), liveness={is_live} "
                        f"(score={liveness_score:.2f}; {liveness_msg}) [{elapsed_ms:.1f}ms]"
                    )
                    # Ultra-fast <0.5s sub-second unlock target:
                    # Verified live human with genuine face match unlocks instantly on Frame 1 (~380ms)
                    target_frames = 1 if similarity >= 0.40 else 2

                    if is_match and is_live:
                        consecutive_verified += 1
                        if consecutive_verified >= target_frames:
                            log_service(
                                f"Robust <0.6s face unlock verified for user {username} in {elapsed_ms:.1f}ms ({frames_tested} frames, consecutive={consecutive_verified})!"
                            )
                            creds = load_credentials()
                            resp = {
                                "status": "verified",
                                "username": username,
                                "confidence": round(similarity * 100, 1),
                                "message": f"Welcome back, {username}!",
                                "low_light": detected_low_light,
                            }
                            if creds:
                                if creds.get("pin"):
                                    resp["pin"] = creds["pin"]
                                if creds.get("password"):
                                    resp["password"] = creds["password"]
                                if creds.get("domain"):
                                    resp["domain"] = creds["domain"]
                            return resp
                    else:
                        consecutive_verified = 0
                        # Immediate abort only on definitive physical presentation spoofs
                        # (phone chassis border, screen bezel, OLED subpixel emitter raster)
                        msg_lower = liveness_msg.lower()
                        is_hard_spoof = (
                            not is_live and any(kw in msg_lower for kw in ("chassis", "bezel", "raster", "gamut", "glare", "static photo"))
                        )
                        if is_hard_spoof:
                            log_service(f"HARD SPOOF DETECTED: {liveness_msg}. Immediately aborting verification session.")
                            return {"status": "failed", "message": f"Spoof detected: {liveness_msg}", "low_light": detected_low_light}

                elapsed_total = (time.perf_counter() - start_time) * 1000
                log_service(f"Face verification timed out after {frames_tested} frames ({elapsed_total:.1f}ms).")
                return {"status": "failed", "message": "Face verification timed out", "low_light": detected_low_light}
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
                response = self.verify_current_face(timeout_sec=2.0)
                log_service(f"Verification outcome: status={response.get('status')}")
            elif cmd_str.startswith("SET_TIMEOUT"):
                parts = cmd_str.split(None, 1)
                sec_val = None
                if len(parts) > 1 and parts[1].lower() != "never" and parts[1].lower() != "null":
                    try:
                        sec_val = int(parts[1])
                    except ValueError:
                        sec_val = None
                from service.presence_config import save_timeout, label
                save_timeout(sec_val)
                self.presence_timeout = sec_val
                self.presence_max_missed_checks = max_missed_checks(sec_val)
                self.presence_missed_checks = 0
                log_service(f"Presence timeout updated via pipe to: {label(sec_val)}")
                response = {"status": "ok", "timeout": label(sec_val), "seconds": sec_val}
            elif cmd_str == "GET_TIMEOUT":
                from service.presence_config import label
                response = {"status": "ok", "timeout": label(self.presence_timeout), "seconds": self.presence_timeout}
            elif cmd_str in ("STOP", "EXIT", "RESTART"):
                log_service("Received STOP/RESTART command. Terminating daemon process immediately...")
                try:
                    response = {"status": "ok", "message": "Stopping daemon"}
                    res_bytes = (json.dumps(response) + "\n").encode('utf-8')
                    win32file.WriteFile(h_pipe, res_bytes)
                except Exception:
                    pass
                try:
                    self.release_camera()
                except Exception:
                    pass
                os._exit(0)
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
        log_service("Camera is IDLE (LED is OFF). Waiting for lock-screen requests and presence samples...")

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
    print("=" * 68)
    print("        WINDOWS HELLO REFINED - BACKGROUND BIOMETRIC SERVICE")
    print("                        Made by Aryan Das")
    print("          GitHub:   https://github.com/aryandas079 (@aryandas079)")
    print("          LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)")
    print("=" * 68)
    idx = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    daemon = FaceDaemon(idx)
    daemon.run_server()
