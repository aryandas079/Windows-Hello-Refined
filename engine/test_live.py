import os
import sys
import time
import cv2

# Ensure root dir is in path
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from engine.face_engine import FaceEngine
from engine.storage import load_profile

def run_live_test(camera_index=0):
    print("=" * 68)
    print("       WINDOWS HELLO REFINED - LIVE BIOMETRIC TEST HUD")
    print("                        Made by Aryan Das")
    print("          GitHub:   https://github.com/aryandas079 (@aryandas079)")
    print("          LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)")
    print("=" * 68)

    profile = load_profile()
    if not profile:
        print("[!] No enrolled face profile found.")
        print("    Please run enrollment first: python enrollment/enroll.py")
        return

    username = profile["username"]
    enrolled_embeddings = profile["embeddings"]
    print(f"Loaded {len(enrolled_embeddings)} enrolled embeddings for user: [{username}]")
    print("Starting camera feed... Press 'q' or ESC to exit.")

    engine = FaceEngine()
    cap = cv2.VideoCapture(camera_index, cv2.CAP_DSHOW)
    if not cap.isOpened():
        cap = cv2.VideoCapture(camera_index, cv2.CAP_MSMF)
    if not cap.isOpened():
        print(f"Error: Could not open camera {camera_index}")
        return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    cv2.namedWindow("Windows Hello Refined - Live Test", cv2.WINDOW_AUTOSIZE)

    prev_time = time.time()
    fps = 0.0
    engine.reset_blink_history()

    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                continue

            start_proc = time.perf_counter()

            # Preprocess lighting if needed
            enhanced = engine.preprocess_lighting(frame)
            face, status_msg = engine.detect_face(enhanced)

            status_text = "Looking for face..."
            status_color = (180, 180, 180)
            score_text = ""

            if face is not None:
                is_live, live_score, live_msg = engine.check_liveness(frame, face)
                x, y, fw, fh = int(face[0]), int(face[1]), int(face[2]), int(face[3])

                if not is_live:
                    status_text = f"SPOOF DETECTED: {live_msg}"
                    status_color = (0, 0, 255)  # Red
                    score_text = f"Liveness Score: {live_score * 100:.1f}% | Anti-Spoof: REJECTED"
                    cv2.rectangle(frame, (x, y), (x + fw, y + fh), status_color, 2)
                else:
                    query_feature = engine.extract_features(enhanced, face)
                    is_match, similarity = engine.match_face(query_feature, enrolled_embeddings)

                    if is_match:
                        status_text = f"WELCOME, {username.upper()}!"
                        status_color = (0, 255, 0)  # Green
                        score_text = f"Match Confidence: {similarity * 100:.1f}% | Liveness: {live_score * 100:.1f}%"
                        cv2.rectangle(frame, (x, y), (x + fw, y + fh), status_color, 3)
                    else:
                        status_text = "UNKNOWN FACE - ACCESS DENIED"
                        status_color = (0, 140, 255)  # Orange
                        score_text = f"Score: {similarity * 100:.1f}% | Liveness: {live_score * 100:.1f}%"
                        cv2.rectangle(frame, (x, y), (x + fw, y + fh), status_color, 2)

                # Draw 478 MediaPipe dense landmarks with anatomically color-coded regions
                engine.draw_landmarks_478(frame, face)

            proc_ms = (time.perf_counter() - start_proc) * 1000

            # Calculate FPS
            now = time.time()
            fps = 0.9 * fps + 0.1 * (1.0 / max(1e-5, now - prev_time))
            prev_time = now

            # HUD Display
            h, w = frame.shape[:2]
            overlay = frame.copy()
            cv2.rectangle(overlay, (0, 0), (w, 65), (25, 25, 25), -1)
            cv2.addWeighted(overlay, 0.7, frame, 0.3, 0, frame)

            cv2.putText(frame, status_text, (20, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.75, status_color, 2)
            if score_text:
                cv2.putText(frame, score_text, (20, 55), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (220, 220, 220), 1)

            perf_text = f"{proc_ms:.1f}ms | {fps:.1f} FPS"
            cv2.putText(frame, perf_text, (w - 170, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 1)

            cv2.imshow("Windows Hello Refined - Live Test", frame)

            key = cv2.waitKey(1) & 0xFF
            if key == 27 or key == ord('q'):
                break

    finally:
        cap.release()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    idx = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    run_live_test(idx)
