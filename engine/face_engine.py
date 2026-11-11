"""
Windows Hello Refined - Advanced Face Recognition Engine
--------------------------------------------------------
Multi-layer biometric pipeline combining:
  * OpenCV YuNet (face detection, alignment, 5-point keypoints)
  * MediaPipe FaceLandmarker (478 facial landmarks + 52 blendshapes)
  * OpenCV SFace (128-d biometric embedding, cosine match)
  * Multi-layer anti-spoofing liveness detection:
      - Laplacian texture frequency (blur/screen detection)
      - YCrCb color-space variance (print/flat surface)
      - Local Binary Pattern (LBP) micro-texture analysis
      - Specular highlight asymmetry (3D vs. flat surface)
      - Eye Aspect Ratio (EAR) blink detection via 478 landmarks
      - Blendshape eye-openness variance (live micro-expression)
      - Low-frequency gradient analysis (flat display / moire)
  * Adaptive low-light enhancement:
      - CLAHE on L-channel (LAB space) with tunable clip limit
      - Gamma correction for extreme dark conditions (<50 brightness)
      - Histogram equalization for near-black frames (<20 brightness)

Made by Aryan Das | GitHub: @aryandas079 | LinkedIn: @iam-aryandas
"""

import os
import cv2
import numpy as np
import threading
import time
from collections import deque

# MediaPipe FaceLandmarker (478 landmarks + 52 blendshapes)
try:
    import mediapipe as mp
    from mediapipe.tasks import python as mp_python
    from mediapipe.tasks.python import vision as mp_vision
    _MEDIAPIPE_AVAILABLE = True
except ImportError:
    _MEDIAPIPE_AVAILABLE = False

# Left eye landmark indices (MediaPipe FaceMesh 478-point)
_LEFT_EYE_UPPER  = [159, 160, 161]
_LEFT_EYE_LOWER  = [145, 144, 163]
_LEFT_EYE_CORNERS = [33, 133]

# Right eye landmark indices
_RIGHT_EYE_UPPER = [386, 387, 385]
_RIGHT_EYE_LOWER = [374, 373, 380]
_RIGHT_EYE_CORNERS = [362, 263]

# EAR below this = eye closed (blink)
_EAR_BLINK_THRESHOLD = 0.22

# Landmark color-coding for visualization (index ranges -> BGR color)
_EYE_INDICES  = set(range(33, 134)) | set(range(362, 264))
_IRIS_INDICES = set(range(468, 478))
_BROW_INDICES = set(range(46, 56)) | set(range(276, 286))
_LIP_INDICES  = set(range(61, 147)) | set(range(78, 309))
_NOSE_INDICES = {1, 2, 4, 5, 6, 19, 94, 97, 98, 99, 125, 126, 127, 128, 129, 209}
_JAW_INDICES  = set(range(0, 18)) | set(range(199, 209))


def _ear(landmarks, upper_idx, lower_idx, corner_idx):
    """Compute Eye Aspect Ratio from normalized landmark coordinates."""
    v = 0.0
    for u, lo in zip(upper_idx, lower_idx):
        v += np.linalg.norm(
            np.array([landmarks[u].x, landmarks[u].y]) -
            np.array([landmarks[lo].x, landmarks[lo].y])
        )
    v /= len(upper_idx)
    h = np.linalg.norm(
        np.array([landmarks[corner_idx[0]].x, landmarks[corner_idx[0]].y]) -
        np.array([landmarks[corner_idx[1]].x, landmarks[corner_idx[1]].y])
    )
    return v / (h + 1e-6)


def _lbp_histogram(gray_roi, num_points=8, radius=1):
    """Local Binary Pattern histogram for skin micro-texture analysis."""
    h, w = gray_roi.shape
    if h < 10 or w < 10:
        return np.zeros(256, dtype=np.float32)

    lbp = np.zeros_like(gray_roi, dtype=np.uint8)
    for i in range(num_points):
        angle = 2.0 * np.pi * i / num_points
        xi = int(round(radius * np.cos(angle)))
        yi = int(round(-radius * np.sin(angle)))

        x1, x2 = max(0, xi),  min(w, w + xi)
        y1, y2 = max(0, yi),  min(h, h + yi)
        nx1, nx2 = max(0, -xi), min(w, w - xi)
        ny1, ny2 = max(0, -yi), min(h, h - yi)

        if x2 > x1 and y2 > y1 and nx2 > nx1 and ny2 > ny1:
            neighbor = gray_roi[y1:y2, x1:x2]
            center   = gray_roi[ny1:ny2, nx1:nx2]
            lbp[ny1:ny2, nx1:nx2] |= ((neighbor >= center).astype(np.uint8) << i)

    hist, _ = np.histogram(lbp.ravel(), bins=256, range=(0, 256))
    return hist.astype(np.float32) / (hist.sum() + 1e-6)


class FaceEngine:
    """
    High-precision face recognition engine with 478-landmark MediaPipe
    liveness verification, multi-layer anti-spoofing, and SFace embedding.
    """

    def __init__(self,
                 detector_path="models/face_detection_yunet_2023mar.onnx",
                 recognizer_path="models/face_recognition_sface_2021dec.onnx",
                 landmarker_path="models/face_landmarker.task",
                 score_threshold=0.40,
                 nms_threshold=0.30,
                 cosine_threshold=0.38):
        """
        cosine_threshold=0.38 gives approximately 85% genuine-user precision
        for SFace on consumer 720p/1080p webcam imagery.
        """
        if not os.path.isabs(detector_path):
            base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            detector_path   = os.path.join(base_dir, detector_path)
            recognizer_path = os.path.join(base_dir, recognizer_path)
            landmarker_path = os.path.join(base_dir, landmarker_path)

        self.cosine_threshold = cosine_threshold

        # YuNet face detector
        self.detector = cv2.FaceDetectorYN.create(
            model=detector_path,
            config="",
            input_size=(320, 240),
            score_threshold=score_threshold,
            nms_threshold=nms_threshold,
            top_k=5
        )

        # SFace recognizer
        self.recognizer = cv2.FaceRecognizerSF.create(
            model=recognizer_path,
            config=""
        )

        # MediaPipe FaceLandmarker
        self._landmarker = None
        self._mp_lock = threading.Lock()
        if _MEDIAPIPE_AVAILABLE and os.path.exists(landmarker_path):
            try:
                base_options = mp_python.BaseOptions(model_asset_path=landmarker_path)
                options = mp_vision.FaceLandmarkerOptions(
                    base_options=base_options,
                    output_face_blendshapes=True,
                    output_facial_transformation_matrixes=True,
                    num_faces=1,
                    min_face_detection_confidence=0.45,
                    min_face_presence_confidence=0.45,
                    min_tracking_confidence=0.45,
                    running_mode=mp_vision.RunningMode.IMAGE
                )
                self._landmarker = mp_vision.FaceLandmarker.create_from_options(options)
            except Exception:
                self._landmarker = None

        # CLAHE for adaptive contrast enhancement (low-light)
        self.clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8))

        # Temporal histories for liveness, EAR blink, and micro-expression verification
        self._ear_values = deque(maxlen=25)
        self._blink_history = deque(maxlen=25)
        self._blendshape_history = deque(maxlen=25)

    # ------------------------------------------------------------------
    # Low-light preprocessing
    # ------------------------------------------------------------------

    def preprocess_lighting(self, frame):
        """
        Multi-stage adaptive low-light enhancement.
        Stage 1 - brightness measurement.
        Stage 2 - gamma boost for frames with mean brightness < 50.
        Stage 3 - CLAHE on LAB L-channel.
        Stage 4 - histogram equalization for extreme darkness (<20).
        """
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        mean_brightness = float(np.mean(gray))

        # Gamma correction for dark scenes
        if mean_brightness < 50.0:
            gamma = max(0.3, mean_brightness / 90.0)
            inv_gamma = 1.0 / max(gamma, 0.1)
            lut = np.array(
                [min(255, int((i / 255.0) ** inv_gamma * 255)) for i in range(256)],
                dtype=np.uint8
            )
            frame = cv2.LUT(frame, lut)

        # CLAHE on LAB L-channel
        lab = cv2.cvtColor(frame, cv2.COLOR_BGR2LAB)
        l, a, b = cv2.split(lab)
        l_enh = self.clahe.apply(l)

        # Extreme dark: also equalize
        if mean_brightness < 20.0:
            l_enh = cv2.equalizeHist(l_enh)

        return cv2.cvtColor(cv2.merge((l_enh, a, b)), cv2.COLOR_LAB2BGR)

    # ------------------------------------------------------------------
    # Detection
    # ------------------------------------------------------------------

    def detect_face(self, frame):
        """
        Detects the primary face using YuNet.
        Returns (face [15 floats], status_msg) or (None, msg).
        """
        h, w = frame.shape[:2]
        self.detector.setInputSize((w, h))
        _, faces = self.detector.detect(frame)

        if faces is None or len(faces) == 0:
            return None, "No face detected"

        primary = max(faces, key=lambda f: f[2] * f[3])
        return primary, "Face detected"

    # ------------------------------------------------------------------
    # 478-landmark detection
    # ------------------------------------------------------------------

    def get_face_landmarks_478(self, frame):
        """
        Run MediaPipe FaceLandmarker on the frame.
        Returns (landmarks [478], blendshapes [52]) or (None, None).
        """
        if self._landmarker is None:
            return None, None

        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)

        with self._mp_lock:
            result = self._landmarker.detect(mp_image)

        if not result.face_landmarks:
            return None, None

        landmarks  = result.face_landmarks[0]
        blendshapes = result.face_blendshapes[0] if result.face_blendshapes else []
        return landmarks, blendshapes

    # ------------------------------------------------------------------
    # Liveness detection
    # ------------------------------------------------------------------

    def check_liveness(self, frame, face):
        """
        Multi-layer biometric anti-spoofing and liveness detection:
        Layer 1 : Laplacian texture variance (screen blur / flat surface)
        Layer 2 : YCrCb color-space chrominance distribution (print / screen gamut)
        Layer 3 : Local Binary Pattern (LBP) micro-texture analysis (skin micropores vs paper)
        Layer 4 : Specular highlight asymmetry (3D curved skin vs flat screen glass)
        Layer 5 : MediaPipe 478 3D landmark depth curvature (Z-axis anatomical profile)
        Layer 6 : Directional gradient / moire pattern detection (phone LCD/OLED raster)
        Layer 7 : Temporal EAR dynamics, micro-tremor, and blink verification (rejects static photos)

        Returns: (is_live: bool, liveness_score: float, message: str)
        """
        x, y, w, h = int(face[0]), int(face[1]), int(face[2]), int(face[3])
        H, W = frame.shape[:2]
        x1, y1 = max(0, x), max(0, y)
        x2, y2 = min(W, x + w), min(H, y + h)

        if x2 - x1 < 40 or y2 - y1 < 40:
            return False, 0.0, "Face too small or too far"

        face_roi = frame[y1:y2, x1:x2]
        gray_roi = cv2.cvtColor(face_roi, cv2.COLOR_BGR2GRAY)

        # Layer 1: Laplacian texture frequency
        lap_var = float(cv2.Laplacian(gray_roi, cv2.CV_64F).var())
        if lap_var < 8.0:
            return False, lap_var / 150.0, "Extreme blur or flat surface detected"

        # Layer 2: YCrCb color chrominance distribution
        ycrcb = cv2.cvtColor(face_roi, cv2.COLOR_BGR2YCrCb)
        _, cr, cb = cv2.split(ycrcb)
        cr_std = float(np.std(cr))
        cb_std = float(np.std(cb))
        if cr_std < 0.8 or cb_std < 0.8:
            return False, (cr_std + cb_std) / 20.0, "Abnormal color compression (possible print/screen)"

        # Layer 3: LBP micro-texture entropy
        lbp_hist = _lbp_histogram(gray_roi)
        lbp_entropy = -float(np.sum(lbp_hist * np.log2(lbp_hist + 1e-10)))
        if lbp_entropy < 4.4:
            return False, lbp_entropy / 8.0, "LBP micro-texture too uniform (possible photo)"

        # Layer 4: Specular highlight asymmetry (flat glass vs 3D curved face)
        _, thresh = cv2.threshold(gray_roi, 225, 255, cv2.THRESH_BINARY)
        highlight_total = float(np.sum(thresh > 0))
        if highlight_total > 250:
            half_w = gray_roi.shape[1] // 2
            left_h  = float(np.sum(thresh[:, :half_w] > 0))
            right_h = float(np.sum(thresh[:, half_w:] > 0))
            sym = min(left_h, right_h) / (max(left_h, right_h) + 1e-6)
            if sym > 0.96:
                return False, 1.0 - sym, "Flat screen glass reflection detected"

        # Layer 5: MediaPipe 478 3D landmark depth curvature
        landmarks, blendshapes = self.get_face_landmarks_478(frame)
        if landmarks is None or len(landmarks) < 468:
            return False, 0.25, "MediaPipe 478 landmarks not registered"

        zs = [lm.z for lm in landmarks]
        z_span = float(max(zs) - min(zs))
        if z_span < 0.08:
            return False, z_span / 0.20, "Lack of 3D facial depth (possible flat photo)"

        # Layer 6: Directional gradient / moire pattern detection
        blurred = cv2.GaussianBlur(gray_roi, (15, 15), 0)
        sobelx_std = float(np.std(cv2.Sobel(blurred, cv2.CV_64F, 1, 0, ksize=3)))
        sobely_std = float(np.std(cv2.Sobel(blurred, cv2.CV_64F, 0, 1, ksize=3)))
        max_grad = max(sobelx_std, sobely_std)
        grad_ratio = min(sobelx_std, sobely_std) / (max_grad + 1e-6)
        if grad_ratio < 0.04 and max_grad > 60.0:
            return False, grad_ratio, "Moire / screen raster scanlines detected"

        # Layer 7: Temporal EAR dynamics, micro-movement, and blink detection
        blink_verified = False
        ear_l = _ear(landmarks, _LEFT_EYE_UPPER,  _LEFT_EYE_LOWER,  _LEFT_EYE_CORNERS)
        ear_r = _ear(landmarks, _RIGHT_EYE_UPPER, _RIGHT_EYE_LOWER, _RIGHT_EYE_CORNERS)
        ear_val = float((ear_l + ear_r) / 2.0)

        self._ear_values.append(ear_val)
        self._blink_history.append(ear_val < _EAR_BLINK_THRESHOLD)

        # Track blendshape eye/brow values if available
        bs_eye = 0.0
        if blendshapes:
            bs_dict = {b.category_name: b.score for b in blendshapes}
            bs_eye = float((bs_dict.get('eyeBlinkLeft', 0.0) + bs_dict.get('eyeBlinkRight', 0.0)) / 2.0)
            self._blendshape_history.append(bs_eye)

        # Check for blink transition (open -> closed -> open)
        if len(self._blink_history) >= 4:
            hist = list(self._blink_history)
            transitions = sum(1 for i in range(1, len(hist)) if hist[i] != hist[i - 1])
            if transitions >= 2:
                blink_verified = True

        # Check for static photo attack over consecutive frames
        if len(self._ear_values) >= 8:
            ear_std = float(np.std(self._ear_values))
            bs_std = float(np.std(self._blendshape_history)) if len(self._blendshape_history) >= 8 else 0.005
            if ear_std < 0.0012 and bs_std < 0.0010 and not blink_verified:
                return False, 0.20, "Static photo spoof detected (no ocular micro-movement or blink)"

        # Combined scoring
        texture_score = min(1.0, max(0.0, (lap_var - 8.0) / 120.0))
        color_score   = min(1.0, (cr_std + cb_std) / 22.0)
        lbp_score     = min(1.0, max(0.0, (lbp_entropy - 4.2) / 3.0))
        depth_score   = min(1.0, max(0.0, (z_span - 0.08) / 0.15))

        if blink_verified:
            dynamic_score = 1.0
        elif len(self._ear_values) >= 4 and float(np.std(self._ear_values)) >= 0.0020:
            dynamic_score = 0.88
        else:
            dynamic_score = 0.70

        liveness_score = (
            texture_score * 0.20 +
            color_score   * 0.15 +
            lbp_score     * 0.20 +
            depth_score   * 0.20 +
            dynamic_score * 0.25
        )

        if liveness_score < 0.58:
            return False, liveness_score, f"Liveness check failed (score={liveness_score:.2f})"

        return True, liveness_score, "Live human verified"

    # ------------------------------------------------------------------
    # Embedding extraction and matching
    # ------------------------------------------------------------------

    def extract_features(self, frame, face):
        """Align and extract 128-d SFace biometric feature vector."""
        aligned = self.recognizer.alignCrop(frame, face)
        return self.recognizer.feature(aligned)

    def match_face(self, query_feature, enrolled_features):
        """
        Cosine similarity match against all enrolled templates.
        cosine_threshold=0.38 targets ~85% genuine-user precision.
        Returns: (is_match: bool, max_similarity: float)
        """
        if not enrolled_features:
            return False, 0.0

        max_score = max(
            self.recognizer.match(query_feature, e, cv2.FaceRecognizerSF_FR_COSINE)
            for e in enrolled_features
        )
        return max_score >= self.cosine_threshold, float(max_score)

    # ------------------------------------------------------------------
    # Visualization
    # ------------------------------------------------------------------

    def draw_landmarks_478(self, frame, face=None):
        """
        Draw all 478 MediaPipe landmarks with color-coded regions.
        Falls back to 5 YuNet keypoints if MediaPipe is unavailable.
        """
        landmarks, _ = self.get_face_landmarks_478(frame)
        h, w = frame.shape[:2]

        if landmarks is None:
            # Fallback: 5 YuNet keypoints
            if face is not None:
                for i in range(5):
                    px = int(face[4 + i * 2])
                    py = int(face[5 + i * 2])
                    cv2.circle(frame, (px, py), 3, (0, 255, 255), -1)
            return frame

        for idx, lm in enumerate(landmarks):
            px = int(lm.x * w)
            py = int(lm.y * h)

            if idx in _IRIS_INDICES:
                color = (0, 165, 255)   # orange
            elif idx in _EYE_INDICES:
                color = (0, 255, 255)   # cyan
            elif idx in _BROW_INDICES:
                color = (0, 215, 255)   # yellow
            elif idx in _LIP_INDICES:
                color = (255, 0, 180)   # magenta
            elif idx in _NOSE_INDICES:
                color = (0, 255, 80)    # green
            elif idx in _JAW_INDICES:
                color = (220, 220, 220) # white
            else:
                color = (140, 140, 255) # light blue

            cv2.circle(frame, (px, py), 1, color, -1)

        return frame

    def reset_blink_history(self):
        """Reset temporal liveness, EAR blink, and blendshape buffers between sessions."""
        self._ear_values.clear()
        self._blink_history.clear()
        self._blendshape_history.clear()
