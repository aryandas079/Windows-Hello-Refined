import os
import sys
import time
import getpass
import subprocess
import cv2
import numpy as np

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from engine.face_engine import FaceEngine
from engine.storage import save_profile, load_profile, save_credentials, load_credentials

STEPS = [
    {"name": "LOOK STRAIGHT", "target": 5, "instruction": "Look directly at the camera", "color": (0, 255, 120)},
    {"name": "TURN SLIGHTLY LEFT", "target": 3, "instruction": "Turn your head slightly to your LEFT", "color": (0, 220, 255)},
    {"name": "TURN SLIGHTLY RIGHT", "target": 3, "instruction": "Turn your head slightly to your RIGHT", "color": (0, 220, 255)},
    {"name": "TILT / SMILE", "target": 3, "instruction": "Tilt head slightly or smile naturally", "color": (255, 200, 0)}
]

def print_header():
    print("=" * 68)
    print("        WINDOWS HELLO REFINED - ONE-TIME BIOMETRIC ENROLLMENT")
    print("                        Made by Aryan Das")
    print("          GitHub:   https://github.com/aryandas079 (@aryandas079)")
    print("          LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)")
    print("   Enrolls your face once; works automatically on the lock screen!")
    print("=" * 68)

def draw_hud(frame, face, step_info, current_in_step, total_samples, total_needed=14):
    h, w = frame.shape[:2]

    # Semi-transparent dark header banner
    overlay = frame.copy()
    cv2.rectangle(overlay, (0, 0), (w, 85), (20, 20, 20), -1)
    cv2.rectangle(overlay, (0, h - 45), (w, h), (20, 20, 20), -1)
    cv2.addWeighted(overlay, 0.7, frame, 0.3, 0, frame)

    # Title & Step
    step_name = step_info["name"]
    instruction = step_info["instruction"]
    color = step_info["color"]

    cv2.putText(frame, f"STEP: {step_name}", (20, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.75, color, 2)
    cv2.putText(frame, instruction, (20, 65), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1)

    # Progress counter
    progress_text = f"Samples: {total_samples}/{total_needed}"
    cv2.putText(frame, progress_text, (w - 180, 45), cv2.FONT_HERSHEY_SIMPLEX, 0.68, (0, 255, 255), 2)

    # Face box & 5 landmarks
    if face is not None:
        x, y, fw, fh = int(face[0]), int(face[1]), int(face[2]), int(face[3])
        cv2.rectangle(frame, (x, y), (x + fw, y + fh), (0, 255, 120), 2)

        landmarks = [
            (int(face[4]), int(face[5])),
            (int(face[6]), int(face[7])),
            (int(face[8]), int(face[9])),
            (int(face[10]), int(face[11])),
            (int(face[12]), int(face[13]))
        ]
        for pt in landmarks:
            cv2.circle(frame, pt, 3, (0, 255, 255), -1)

        cv2.putText(frame, "Face aligned. Hold still...", (20, h - 16),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 120), 1)
    else:
        cv2.putText(frame, "Align your face in the camera view...", (20, h - 16),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 165, 255), 1)

def main():
    print_header()
    username = getpass.getuser()
    print(f"[*] Target Windows User: [{username}]")
    print("[*] Initializing neural models (YuNet + SFace)...")

    engine = FaceEngine()

    print("[*] Opening camera...")
    cap = cv2.VideoCapture(0, cv2.CAP_DSHOW)
    if not cap.isOpened():
        cap = cv2.VideoCapture(0, cv2.CAP_MSMF)
    if not cap.isOpened():
        print("[!] ERROR: Could not access webcam. Make sure no other app is using it.")
        return 1

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    # Discard initial dark sensor frames
    for _ in range(5):
        cap.read()

    window_title = "Windows Hello - Face Setup"
    cv2.namedWindow(window_title, cv2.WINDOW_AUTOSIZE)

    step_idx = 0
    step_samples = 0
    collected_embeddings = []
    last_sample_time = 0

    print("\n[+] Camera preview opened! Please look at the window:")
    print("    Follow the on-screen instructions across 4 angles (takes ~8 seconds).")

    try:
        while step_idx < len(STEPS):
            ret, frame = cap.read()
            if not ret:
                time.sleep(0.01)
                continue

            current_step = STEPS[step_idx]
            face, _ = engine.detect_face(frame)

            now = time.time()
            if face is not None:
                is_live, _, _ = engine.check_liveness(frame, face)
                if is_live:
                    if now - last_sample_time > 0.35:
                        feature = engine.extract_features(frame, face)
                        collected_embeddings.append(feature)
                        step_samples += 1
                        last_sample_time = now

                        if step_samples >= current_step["target"]:
                            step_idx += 1
                            step_samples = 0
                            time.sleep(0.2)

            draw_hud(frame, face, current_step, step_samples, len(collected_embeddings))
            cv2.imshow(window_title, frame)

            key = cv2.waitKey(1) & 0xFF
            if key == 27 or key == ord('q'):
                print("\n[!] Setup cancelled by user.")
                return 1

        # Completed all steps!
        print("\n[+] All 14 multi-angle biometric samples collected successfully!")
        print("[*] Encrypting profile with Windows DPAPI...")
        saved_path = save_profile(username, collected_embeddings)
        print(f"[+] Biometric profile saved to: {saved_path}")

        # Show success screen
        for _ in range(30):
            ret, frame = cap.read()
            if ret:
                h, w = frame.shape[:2]
                cv2.rectangle(frame, (40, h//2 - 50), (w - 40, h//2 + 50), (0, 180, 0), -1)
                cv2.putText(frame, "SETUP COMPLETE!", (w//2 - 140, h//2 + 8),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 255, 255), 2)
                cv2.imshow(window_title, frame)
                if cv2.waitKey(30) & 0xFF == 27:
                    break

    finally:
        cap.release()
        cv2.destroyAllWindows()

    # ── Optional: Save Windows password for automatic lock screen unlock ───
    print("\n" + "-" * 65)
    print("  WINDOWS AUTO-UNLOCK (OPTIONAL)")
    print("  To automatically unlock your screen when your face is recognized,")
    print("  enter your Windows password once (encrypted with Windows DPAPI).")
    print("  If you press Enter without typing a password, Face ID will verify")
    print("  and allow you to click 'Unlock' or enter your PIN as usual.")
    print("-" * 65)

    try:
        from save_password import main as save_password_main
        save_password_main()
    except Exception as e:
        print(f"[*] Note: {e}")

    # ── Self-test validation ──────────────────────────────────────────
    print("\n[*] Running instant self-test validation...")
    test_profile = load_profile()
    if not test_profile or len(test_profile.get("embeddings", [])) == 0:
        print("[!] ERROR: Profile verification failed.")
        return 1

    print(f"[+] Loaded verified profile with {len(test_profile['embeddings'])} embeddings.")

    # ── Configure Windows auto-start for background service ───────────
    print("\n[*] Configuring background face service auto-start...")
    pythonw_exe = os.path.join(BASE_DIR, ".venv", "Scripts", "pythonw.exe")
    service_py = os.path.join(BASE_DIR, "service", "face_service.py")

    try:
        import winreg
        key = winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            r"Software\Microsoft\Windows\CurrentVersion\Run",
            0,
            winreg.KEY_SET_VALUE
        )
        cmd_val = f'"{pythonw_exe}" "{service_py}"'
        winreg.SetValueEx(key, "WindowsHelloRefinedService", 0, winreg.REG_SZ, cmd_val)
        winreg.CloseKey(key)
        print("[+] Registered in Windows Startup (HKCU Run).")
    except Exception as e:
        print(f"[!] Warning registering startup: {e}")

    # ── Start background service now ──────────────────────────────────
    print("[*] Starting background service...")
    try:
        DETACHED_PROCESS = 0x00000008
        CREATE_NEW_PROCESS_GROUP = 0x00000200
        subprocess.Popen(
            [pythonw_exe, "-u", service_py],
            cwd=BASE_DIR,
            creationflags=DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
            close_fds=True
        )
        print("[+] Background service is active and listening.")
    except Exception as e:
        print(f"[!] Error starting service: {e}")

    # ── Optional: Clean lock screen suggestions & widgets ────────────
    print("\n" + "=" * 68)
    clean_prompt = input("Do you want to remove lock screen news, fun facts, tips, and widgets for a clean look? [Y/n]: ").strip().lower()
    if clean_prompt != 'n':
        try:
            from clean_lockscreen import clean_lock_screen
            clean_lock_screen()
        except Exception as e:
            print(f"[!] Note: {e}")

    print("\n" + "=" * 65)
    print("   WINDOWS HELLO REFINED IS READY!")
    print("=" * 65)
    print(" - Your face is enrolled.")
    print(" - You NEVER need to run setup again.")
    print(" - Press [Win + L] to lock your screen.")
    print(" - Simply look at your camera to unlock instantly!")
    print(" - Windows PIN remains available at all times.")
    print("=" * 65 + "\n")
    return 0

if __name__ == "__main__":
    sys.exit(main())
