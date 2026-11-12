# Changelog

All notable changes to Windows Hello Refined are documented here.

## Unreleased

- Added standard contribution, security, conduct, issue-template, and pull-request documentation.
- Added configurable walk-away auto-lock presets.
- Presence monitoring now samples every 5 seconds and releases the camera between samples so the privacy indicator turns off between checks.
- Improved lock-screen face verification, animation feedback, and PIN-entry handling.

## Existing behavior

- Supports Windows 10/11 64-bit systems with a standard RGB camera.
- Stores enrolled credentials using Windows DPAPI.
- Integrates with the Windows lock screen through a native credential provider.
