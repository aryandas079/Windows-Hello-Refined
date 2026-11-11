import os
import cv2
import numpy as np

class FaceEngine:
    def __init__(self,
                 detector_path="models/face_detection_yunet_2023mar.onnx",
                 recognizer_path="models/face_recognition_sface_2021dec.onnx",
                 score_threshold=0.35,
                 nms_threshold=0.3,
                 cosine_threshold=0.33):
        """
        FaceEngine provides sub-second face detection, alignment,
        liveness detection, and embedding matching.
        """
        if not os.path.isabs(detector_path):
            base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            detector_path = os.path.join(base_dir, detector_path)
            recognizer_path = os.path.join(base_dir, recognizer_path)

        self.score_threshold = score_threshold
        self.nms_threshold = nms_threshold
        self.cosine_threshold = cosine_threshold

        # Initialize YuNet Detector
        self.detector = cv2.FaceDetectorYN.create(
            model=detector_path,
            config="",
            input_size=(320, 240),
            score_threshold=self.score_threshold,
            nms_threshold=self.nms_threshold,
            top_k=5
        )

        # Initialize SFace Recognizer
        self.recognizer = cv2.FaceRecognizerSF.create(
            model=recognizer_path,
            config=""
        )

        # CLAHE for low-light enhancement
        self.clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))

    def preprocess_lighting(self, frame):
        """Enhances contrast adaptively for dim or uneven lighting."""
        lab = cv2.cvtColor(frame, cv2.COLOR_BGR2LAB)
        l, a, b = cv2.split(lab)
        l2 = self.clahe.apply(l)
        lab_enhanced = cv2.merge((l2, a, b))
        return cv2.cvtColor(lab_enhanced, cv2.COLOR_LAB2BGR)

    def detect_face(self, frame):
        """
        Detects primary face in frame.
        Returns: (face_box_and_landmarks, status)
        """
        h, w = frame.shape[:2]
        self.detector.setInputSize((w, h))

        # Detect faces
        _, faces = self.detector.detect(frame)
        if faces is None or len(faces) == 0:
            return None, "No face detected"

        # Sort by confidence or area to find the closest/primary face
        primary_face = max(faces, key=lambda f: f[2] * f[3])  # area w * h
        return primary_face, "Face detected"

    def check_liveness(self, frame, face):
        """
        Passive anti-spoofing:
        - Texture frequency check (Laplacian variance)
        - Color spectrum variance (YCrCb color space distribution)
        Tuned for standard 720p/1080p webcams with auto-exposure.
        """
        x, y, w, h = int(face[0]), int(face[1]), int(face[2]), int(face[3])
        H, W = frame.shape[:2]
        x1, y1 = max(0, x), max(0, y)
        x2, y2 = min(W, x + w), min(H, y + h)

        if x2 - x1 < 30 or y2 - y1 < 30:
            return False, 0.0, "Face too small / far"

        face_roi = frame[y1:y2, x1:x2]

        # 1. High frequency texture analysis (Laplacian)
        gray = cv2.cvtColor(face_roi, cv2.COLOR_BGR2GRAY)
        lap_var = cv2.Laplacian(gray, cv2.CV_64F).var()
        if lap_var < 7.0:
            # Extreme blur or solid block (typical of fake/printed paper)
            return False, float(lap_var), "Extreme blur detected"

        # 2. Color reflection check in YCrCb
        ycrcb = cv2.cvtColor(face_roi, cv2.COLOR_BGR2YCrCb)
        _, cr, cb = cv2.split(ycrcb)
        cr_std = np.std(cr)
        cb_std = np.std(cb)

        if cr_std < 0.6 or cb_std < 0.6:
            return False, float(cr_std + cb_std), "Abnormal color compression"

        liveness_score = min(1.0, (lap_var / 150.0) * 0.5 + ((cr_std + cb_std) / 20.0) * 0.5)
        return True, liveness_score, "Live human verified"

    def extract_features(self, frame, face):
        """Aligns the face and extracts a 128-d biometric feature vector."""
        aligned_face = self.recognizer.alignCrop(frame, face)
        feature = self.recognizer.feature(aligned_face)
        return feature

    def match_face(self, query_feature, enrolled_features):
        """
        Matches query feature vector against a list of enrolled template features.
        Returns: (is_match, max_similarity_score)
        """
        if enrolled_features is None or len(enrolled_features) == 0:
            return False, 0.0

        max_score = -1.0
        for enrolled in enrolled_features:
            score = self.recognizer.match(query_feature, enrolled, cv2.FaceRecognizerSF_FR_COSINE)
            if score > max_score:
                max_score = score

        is_match = (max_score >= self.cosine_threshold)
        return is_match, float(max_score)
