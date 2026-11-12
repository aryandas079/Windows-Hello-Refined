import sys
import os
import subprocess
from service.presence_config import label, prompt_timeout, save_timeout

def show_banner():
    print("=" * 68)
    print("             WINDOWS HELLO REFINED - FACE ID SYSTEM")
    print("                        Made by Aryan Das")
    print("          GitHub:   https://github.com/aryandas079 (@aryandas079)")
    print("          LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)")
    print("   High-Speed Biometric Authentication on Any Standard Camera")
    print("=" * 68)

def main():
    show_banner()
    python_exe = os.path.join(os.path.dirname(__file__), ".venv", "Scripts", "python.exe")

    print("\nOptions:")
    print("  1. Enroll Face Profile (Run this first!)")
    print("  2. Save Windows Hello PIN for Instant Auto-Unlock")
    print("  3. Live Camera & Face Recognition Test (Visual HUD)")
    print("  4. Start Background Face ID Daemon (Named Pipe Service)")
    print("  5. Start Walk-Away Auto-Lock (Presence Guard)")
    print("  6. Register Native Windows Lock Screen Credential Provider")
    print("  7. Unregister Credential Provider")
    print("  8. Run All-in-One Installer (install.bat)")
    print("  9. Run All-in-One Uninstaller (uninstall.bat)")
    print(" 10. Clean Lock Screen (Remove News, Fun Facts, Tips & Widgets)")
    print(" 11. Preview Transparent Windows Hello HTML/SVG Animation")
    print(" 12. Set Face-Absence Auto-Lock Timeout")
    print("  0. Exit")

    choice = input("\nSelect an option [0-12]: ").strip()

    if choice == "1":
        subprocess.run([python_exe, "setup_face.py"])
    elif choice == "2":
        subprocess.run([python_exe, "save_pin.py"])
    elif choice == "3":
        subprocess.run([python_exe, "engine/test_live.py"])
    elif choice == "4":
        subprocess.run([python_exe, "service/face_service.py"])
    elif choice == "5":
        subprocess.run([python_exe, "service/session_guard.py"])
    elif choice == "6":
        dll_path = os.path.abspath("credential_provider/FaceCredentialProvider.dll")
        print(f"Registering: {dll_path}")
        subprocess.run(["regsvr32", "/s", dll_path])
        print("[+] Credential Provider registered with Windows LogonUI!")
    elif choice == "7":
        dll_path = os.path.abspath("credential_provider/FaceCredentialProvider.dll")
        subprocess.run(["regsvr32", "/u", "/s", dll_path])
        print("[-] Credential Provider unregistered.")
    elif choice == "8":
        bat_path = os.path.abspath("install.bat")
        print(f"Launching installer: {bat_path}")
        subprocess.run(["cmd", "/c", bat_path])
    elif choice == "9":
        bat_path = os.path.abspath("uninstall.bat")
        print(f"Launching uninstaller: {bat_path}")
        subprocess.run(["cmd", "/c", bat_path])
    elif choice == "10":
        subprocess.run([python_exe, "clean_lockscreen.py"])
    elif choice == "11":
        subprocess.run([python_exe, "ui/show_hello.py"])
    elif choice == "12":
        selected = prompt_timeout()
        save_timeout(selected)
        print(f"Auto-lock timeout saved: {label(selected)}")
    elif choice == "0":
        print("Exiting.")
    else:
        print("Invalid choice.")

if __name__ == "__main__":
    main()
