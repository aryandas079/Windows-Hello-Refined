import sys
import os
import subprocess

def show_banner():
    print("=" * 65)
    print("        WINDOWS HELLO REFINED - FACE ID SYSTEM")
    print("   High-Speed Biometric Authentication on Any Standard Camera")
    print("=" * 65)

def main():
    show_banner()
    python_exe = os.path.join(os.path.dirname(__file__), ".venv", "Scripts", "python.exe")

    print("\nOptions:")
    print("  1. Enroll Face Profile (Run this first!)")
    print("  2. Save Windows Password for Instant Auto-Unlock")
    print("  3. Live Camera & Face Recognition Test (Visual HUD)")
    print("  4. Start Background Face ID Daemon (Named Pipe Service)")
    print("  5. Start Walk-Away Auto-Lock (Presence Guard)")
    print("  6. Register Native Windows Lock Screen Credential Provider")
    print("  7. Unregister Credential Provider")
    print("  8. Preview Transparent Windows Hello HTML/SVG Animation")
    print("  0. Exit")

    choice = input("\nSelect an option [0-8]: ").strip()

    if choice == "1":
        subprocess.run([python_exe, "setup_face.py"])
    elif choice == "2":
        subprocess.run([python_exe, "save_password.py"])
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
        subprocess.run([python_exe, "ui/show_hello.py"])
    elif choice == "0":
        print("Exiting.")
    else:
        print("Invalid choice.")

if __name__ == "__main__":
    main()
