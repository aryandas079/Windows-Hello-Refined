import os
import sys
import time
import cv2
import getpass
import numpy as np

# Ensure root dir is in path
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from engine.face_engine import FaceEngine
from engine.storage import save_profile

STEPS = [
    {"name": "LOOK STRAIGHT", "target_count": 5, "instruction": "Look directly at the camera"},
    {"name": "TURN SLIGHTLY LEFT", "target_count": 3, "instruction": "Turn your head slightly to the LEFT"},
    {"name": "TURN SLIGHTLY RIGHT", "target_count": 3, "instruction": "Turn your head slightly to the RIGHT"},
    {"name": "TILT / SMILE", "target_count": 3, "instruction": "Tilt head slightly or smile naturally"}
]

def draw_hud(frame, face, step_info, current_samples, total_samples, liveness_ok, msg):
    h, w = frame.shape[:2]
    # Darken overlay background for HUD text at top
    overlay = frame.copy()
    cv2.rectangle(overlay, (0, 0), (w, 80), (20, 20, 20), -1)
    cv2.rectangle(overlay, (0, h - 40), (w, h), (20, 20, 20), -1)
    cv2.addWeighted(overlay, 0.65, frame, 0.35, 0, frame)

    # Title & Step
    step_title = f"WINDOWS HELLO ENROLLMENT: [{step_info['name']}]"
    cv2.putText(frame, step_title, (20, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 200), 2)
    cv2.putText(frame, step_info['instruction'], (20, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1)

    # Progress counter
    progress_text = f"Samples: {total_samples}/14"
    cv2.putText(frame, progress_text, (w - 180, 45), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)

    # Status / instruction at bottom
    status_color = (0, 255, 0) if liveness_ok else (0, 0, 255)
    cv2.putText(frame, msg, (20, h - 15), cv2.FONT_HERSHEY_SIMPLEX, 0.55, status_color, 1)

    # Face Box & landmarks
    if face is not None:
        x, y, fw, fh = int(face[0]), int(face[1]), int(face[2]), int(face[3])
        box_color = (0, 255, 0) if liveness_ok else (0, 165, 255)
        # Bounding box
        cv2.rectangle(frame, (x, y), (x + fw, y + fh), box_color, 2)

        # 5 facial landmarks (right eye, left eye, nose tip, mouth right, mouth left)
        landmarks = [(int(face[4]), int(face[5])),
                     (int(face[6]), int(face[7])),
                     (int(face[8]), int(face[9])),
                     (int(face[10]), int(face[11])),
                     (int(face[12]), int(face[13]))]
        for pt in landmarks:
            cv2.circle(frame, pt, 3, (0, 255, 255), -1)

def run_enrollment(camera_index=0):
    print("=" * 60)
    print("      WINDOWS HELLO REFINED - FACE ENROLLMENT")
    print("=" * 60)

    username = getpass.getuser()
    print(f"Enrolling biometric profile for Windows User: [{username}]")
    print("Initializing camera and neural models...")

    engine = FaceEngine()
    cap = cv2.VideoCapture(camera_index, cv2.CAP_DSHOW)
    if not cap.isOpened():
        cap = cv2.VideoCapture(camera_index, cv2.CAP_MSMF)
    if not cap.isOpened():
        print(f"Error: Could not open camera {camera_index}")
        return False

    # Set camera resolution
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    cv2.namedWindow("Windows Hello Refined - Enrollment", cv2.WINDOW_AUTOSIZE)

    step_idx = 0
    step_samples = 0
    collected_embeddings = []
    last_sample_time = 0
    msg = "Align your face inside the frame"

    try:
        while step_idx < len(STEPS):
            ret, frame = cap.read()
            if not ret:
                time.sleep(0.01)
                continue

            current_step = STEPS[step_idx]
            face, det_msg = engine.detect_face(frame)

            liveness_ok = False
            if face is not None:
                liveness_ok, score, live_msg = engine.check_liveness(frame, face)
                now = time.time()
                if liveness_ok:
                    msg = f"Good! Hold still ({live_msg})"
                    # Capture a sample every 0.4 seconds to ensure slight variation
                    if now - last_sample_time > 0.4:
                        feature = engine.extract_features(frame, face)
                        collected_embeddings.append(feature)
                        step_samples += 1
                        last_sample_time = now

                        if step_samples >= current_step["target_count"]:
                            step_idx += 1
                            step_samples = 0
                            time.sleep(0.3)
                else:
                    msg = live_msg
            else:
                msg = det_msg

            draw_hud(frame, face, current_step, step_samples, len(collected_embeddings), liveness_ok, msg)
            cv2.imshow("Windows Hello Refined - Enrollment", frame)

            key = cv2.waitKey(1) & 0xFF
            if key == 27 or key == ord('q'):  # ESC or q
                print("\nEnrollment cancelled by user.")
                return False

        # All steps completed successfully
        print("\nAll 14 multi-angle biometric samples collected successfully!")
        print("Encrypting with Windows DPAPI...")
        saved_path = save_profile(username, collected_embeddings)
        print(f"Profile securely saved to: {saved_path}")

        # Final success screen
        ret, frame = cap.read()
        if ret:
            h, w = frame.shape[:2]
            cv2.rectangle(frame, (50, h//2 - 50), (w - 50, h//2 + 50), (0, 180, 0), -1)
            cv2.putText(frame, "ENROLLMENT COMPLETE!", (w//2 - 160, h//2 + 10),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2)
            cv2.imshow("Windows Hello Refined - Enrollment", frame)
            cv2.waitKey(2000)

        return True

    finally:
        cap.release()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    idx = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    run_enrollment(idx)
