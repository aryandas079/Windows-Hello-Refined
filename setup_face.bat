@echo off
setlocal
cd /d "%~dp0"
echo Starting Windows Hello Face Setup...
.venv\Scripts\python.exe setup_face.py
pause
