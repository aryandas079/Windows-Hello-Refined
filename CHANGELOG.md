# Changelog

All notable changes to Windows Hello Refined are documented here.

## Unreleased

- Added standard contribution, security, conduct, issue-template, and pull-request documentation.
- Added configurable walk-away auto-lock presets.
- Presence monitoring now samples every 5 seconds and releases the camera between samples so the privacy indicator turns off between checks.
- Ultra-fast camera detection (<0.5s) using native Windows Media Foundation (MSMF) and lock-screen pre-warming.
- 12,500+ dense 3D depth point cloud projection with wireframe Delaunay mesh lines for real-time anatomical curvature and planarity verification.
- High-precision anti-spoofing against mobile phones, tablets, photos, and video replays via screen bezel detection, 2D FFT subpixel moiré analysis, glass glare rejection, and biological hemoglobin subsurface scattering.
- Improved lock-screen face verification, animation feedback, and PIN-entry handling.

## Existing behavior

- Supports Windows 10/11 64-bit systems with a standard RGB camera.
- Stores enrolled credentials using Windows DPAPI.
- Integrates with the Windows lock screen through a native credential provider.
