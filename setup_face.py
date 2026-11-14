import os
import sys

# Disable OpenCV OBSENSOR probe
os.environ["OPENCV_VIDEOIO_ENABLE_OBSENSOR"] = "0"
os.environ["OPENCV_VIDEOIO_PRIORITY_OBSENSOR"] = "0"
os.environ["OPENCV_VIDEOIO_PRIORITY_MSMF"] = "0"
os.environ["OPENCV_LOG_LEVEL"] = "ERROR"

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
    {"name": "LOOK STRAIGHT",      "target": 7, "instruction": "Look directly at the camera", "color": (0, 255, 120)},
    {"name": "TURN SLIGHTLY LEFT",  "target": 4, "instruction": "Turn your head slightly to your LEFT", "color": (0, 220, 255)},
    {"name": "TURN SLIGHTLY RIGHT", "target": 4, "instruction": "Turn your head slightly to your RIGHT", "color": (0, 220, 255)},
    {"name": "TILT HEAD UP",        "target": 3, "instruction": "Tilt your head slightly UP", "color": (255, 200, 0)},
    {"name": "TILT HEAD DOWN",      "target": 3, "instruction": "Tilt your head slightly DOWN", "color": (255, 200, 0)},
    {"name": "SMILE NATURALLY",     "target": 3, "instruction": "Smile naturally", "color": (0, 255, 180)},
    {"name": "NEUTRAL EXPRESSION",  "target": 2, "instruction": "Return to neutral face expression", "color": (180, 255, 100)}
]

def print_header():
    print("=" * 68)
    print("        WINDOWS HELLO REFINED - ONE-TIME BIOMETRIC ENROLLMENT")
    print("                        Made by Aryan Das")
    print("          GitHub:   https://github.com/aryandas079 (@aryandas079)")
    print("          LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)")
    print("   Enrolls your face once; works automatically on the lock screen!")
    print("=" * 68)

def draw_hud(frame, face, step_info, current_in_step, total_samples, total_needed=26, engine=None):
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

    # Draw 478 MediaPipe dense landmarks & face bounding box
    if face is not None:
        x, y, fw, fh = int(face[0]), int(face[1]), int(face[2]), int(face[3])
        cv2.rectangle(frame, (x, y), (x + fw, y + fh), (0, 255, 120), 2)

        if engine is not None:
            lm, _, _ = engine.get_face_landmarks_478(frame)
            if lm is not None and len(lm) >= 468:
                engine.draw_dense_3d_depth_grid(frame, lm, face)
            else:
                engine.draw_landmarks_478(frame, face)
        else:
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

    existing_profile = load_profile()
    enroll_needed = True
    if existing_profile and len(existing_profile.get("embeddings", [])) >= 10:
        print(f"\n[+] Verified face profile already exists for user [{existing_profile.get('username', username)}] with {len(existing_profile['embeddings'])} biometric samples.")
        re_enroll = input("    Do you want to re-enroll your face? [y/N]: ").strip().lower()
        if re_enroll != 'y':
            print("[+] Keeping existing biometric profile.")
            enroll_needed = False

    if enroll_needed:
        print("[*] Initializing neural models (YuNet + SFace)...")
        engine = FaceEngine()

        print("[*] Opening camera...")
        cap = cv2.VideoCapture(0, cv2.CAP_DSHOW)
        if not cap.isOpened():
            cap = cv2.VideoCapture(0, cv2.CAP_MSMF)
        if not cap.isOpened():
            print("[!] ERROR: Could not access webcam. Make sure no other app is using it.")
            return 1

        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

        # Discard initial dark sensor frames
        for _ in range(3):
            cap.read()

        window_title = "Windows Hello - Face Setup"
        cv2.namedWindow(window_title, cv2.WINDOW_AUTOSIZE)

        step_idx = 0
        step_samples = 0
        collected_embeddings = []
        last_sample_time = 0
        engine.reset_blink_history()

        print("\n[+] Camera preview opened! Please look at the window:")
        print("    Follow the on-screen instructions across 7 angles (takes ~12 seconds).")

        try:
            while step_idx < len(STEPS):
                ret, frame = cap.read()
                if not ret:
                    time.sleep(0.01)
                    continue

                current_step = STEPS[step_idx]

                # Adaptive low-light preprocessing
                enhanced = engine.preprocess_lighting(frame)
                face, _ = engine.detect_face(enhanced)

                now = time.time()
                if face is not None:
                    is_live, _, _ = engine.check_liveness(frame, face)
                    if is_live:
                        if now - last_sample_time > 0.35:
                            feature = engine.extract_features(enhanced, face)
                            collected_embeddings.append(feature)
                            step_samples += 1
                            last_sample_time = now

                            if step_samples >= current_step["target"]:
                                step_idx += 1
                                step_samples = 0
                                time.sleep(0.2)

                draw_hud(frame, face, current_step, step_samples, len(collected_embeddings), total_needed=26, engine=engine)
                cv2.imshow(window_title, frame)

                key = cv2.waitKey(1) & 0xFF
                if key == 27 or key == ord('q'):
                    print("\n[!] Setup cancelled by user.")
                    return 1

            # Completed all steps!
            print("\n[+] All 26 multi-angle biometric samples collected successfully!")
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

    # ── Windows Hello PIN for automatic lock screen unlock ───────────
    print("\n" + "=" * 68)
    print("            WINDOWS HELLO PIN CONFIGURATION (REQUIRED)")
    print("  To automatically enter your PIN and unlock your workstation when")
    print("  your face matches, please enter your Windows Hello PIN once.")
    print("  Your PIN is encrypted locally with Windows DPAPI (Local Machine).")
    print("=" * 68)

    creds = load_credentials()
    if creds and creds.get("pin"):
        print(f"\n[+] Saved Windows Hello PIN already configured for [{creds.get('username', username)}].")
        change_pin = input("    Do you want to re-enter/change your saved PIN? [y/N]: ").strip().lower()
        if change_pin == 'y':
            try:
                from save_pin import main as save_pin_main
                save_pin_main()
            except Exception as e:
                print(f"[*] Note: {e}")
        else:
            print("[+] Keeping existing encrypted Windows Hello PIN.")
    else:
        try:
            from save_pin import main as save_pin_main
            save_pin_main()
        except Exception as e:
            print(f"[*] Note: {e}")

    # ── Self-test validation ──────────────────────────────────────────
    print("\n[*] Running instant self-test validation...")
    test_profile = load_profile()
    if not test_profile or len(test_profile.get("embeddings", [])) == 0:
        print("[!] ERROR: Profile verification failed.")
        return 1

    print(f"[+] Loaded verified profile with {len(test_profile['embeddings'])} embeddings.")
    creds = load_credentials()
    if creds and creds.get("pin"):
        print(f"[+] Verified Windows Hello PIN protection for user [{creds.get('username', username)}].")

    # ── Optional: Clean lock screen suggestions & widgets ────────────
    print("\n" + "=" * 68)
    clean_prompt = input("Do you want to remove lock screen news, fun facts, tips, and widgets for a clean look? [Y/n]: ").strip().lower()
    if clean_prompt != 'n':
        try:
            from clean_lockscreen import clean_lock_screen
            clean_lock_screen()
        except Exception as e:
            print(f"[!] Note: {e}")

    return 0

if __name__ == "__main__":
    sys.exit(main())
