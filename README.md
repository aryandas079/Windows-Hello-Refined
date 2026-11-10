# Windows Hello Refined (Face ID on Any Camera)

High-speed, anti-spoofing biometric Face ID that works on standard RGB webcams and integrates directly with the **Windows Lock Screen (`LogonUI.exe`)**.

---

## Key Highlights

- **Any Camera Supported**: Works with standard laptop webcams, USB cameras, and external cams (no IR or depth hardware required).
- **Native Lock Screen Integration**: Uses a native 64-bit C++ **Windows Credential Provider** loaded by Windows `LogonUI`—no standalone app windows or overlays during unlock.
- **Sub-Second Performance**:
  - Face Detection (YuNet): ~15–20ms
  - Face Alignment & 128-d Feature Extraction (SFace): ~30–50ms
  - Pre-warmed background service prevents cold-start camera delay.
- **Anti-Spoofing (Liveness Check)**: Rejects smartphone screens, 2D printed photos, and static mockups via micro-texture Laplacian analysis and YCrCb color space reflection tests.
- **DPAPI Biometric Security**: Face embeddings are encrypted using Windows Data Protection API (DPAPI) and tied to your Windows user account.
- **Walk-Away Auto-Lock (Presence Guard)**: Automatically locks your workstation (`Win + L`) when you step away.

---

## Project Structure

```
Windows Hello Refined/
├── engine/
│   ├── face_engine.py       # YuNet detector + SFace recognition + Anti-spoofing
│   ├── storage.py           # Windows DPAPI encrypted profile management
│   └── test_live.py         # Visual real-time verification & latency HUD
├── enrollment/
│   └── enroll.py            # 4-stage multi-angle biometric enrollment tool
├── service/
│   ├── face_service.py      # Background IPC Named Pipe daemon
│   └── session_guard.py     # Walk-away presence & auto-lock guard
├── credential_provider/
│   ├── FaceCredentialProvider.dll # Compiled 64-bit native Windows Credential Provider
│   ├── FaceCredential.cpp   # Handles lock screen UI & pipe verification
│   ├── FaceCredentialProvider.cpp # Implements ICredentialProvider for LogonUI
│   └── build.bat            # MSVC compilation script
├── models/
│   ├── face_detection_yunet_2023mar.onnx
│   └── face_recognition_sface_2021dec.onnx
└── manage.py                # Interactive CLI manager
```

---

## Quick Start Guide

### Step 1: Enroll Your Face
Run the enrollment tool to capture your multi-angle profile (front, left, right, tilt/smile):
```powershell
.\.venv\Scripts\python.exe enrollment\enroll.py
```
*(Takes ~10 seconds. Embeddings are automatically encrypted and saved.)*

### Step 2: Test Verification & Anti-Spoofing Live
Run the visual verification HUD to see real-time recognition speed and test anti-spoofing against a phone photo:
```powershell
.\.venv\Scripts\python.exe engine\test_live.py
```
- Green box: Verified User (with match percentage)
- Orange box: Unknown Face
- Red box: Spoof / Screen / Photo Detected

### Step 3: Run the Background Face ID Service
Start the pre-warmed background daemon:
```powershell
.\.venv\Scripts\python.exe service\face_service.py
```

### Step 4: Register with Windows Lock Screen
To have Windows display the Face ID tile on your lock screen (`Win + L`):
Open PowerShell **as Administrator** and run:
```powershell
regsvr32 "D:\Projects_New\Windows Hello Refined\credential_provider\FaceCredentialProvider.dll"
```
To unregister at any time:
```powershell
regsvr32 /u "D:\Projects_New\Windows Hello Refined\credential_provider\FaceCredentialProvider.dll"
```

### Step 5 (Optional): Enable Walk-Away Auto-Lock
To automatically lock your PC when you leave your desk:
```powershell
.\.venv\Scripts\python.exe service\session_guard.py
```
