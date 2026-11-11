import os
import sys
import getpass

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from engine.storage import save_credentials, load_credentials

def main():
    print("=" * 68)
    print("             WINDOWS HELLO REFINED - PIN AUTO-UNLOCK")
    print("                        Made by Aryan Das")
    print("          GitHub:   https://github.com/aryandas079 (@aryandas079)")
    print("          LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)")
    print("=" * 68)
    print("On Windows 11 with a Microsoft Account, Windows signs in using your")
    print("Windows Hello PIN (passwords are disabled by Windows security policy).")
    print("\nTo automatically unlock your screen when your face is recognized,")
    print("enter your Windows Hello PIN once (encrypted securely with Windows DPAPI).")
    print("=" * 68)

    user = getpass.getuser()
    print(f"\n[*] Target Windows User: [{user}]")

    pin1 = getpass.getpass("\nEnter your Windows Hello PIN: ").strip()
    if not pin1:
        print("[*] Cancelled. No PIN entered.")
        return 0

    pin2 = getpass.getpass("Confirm your Windows Hello PIN: ").strip()
    if pin1 != pin2:
        print("[!] ERROR: PINs do not match. Please run again and re-enter.")
        return 1

    save_credentials(user, pin=pin1)

    print("\n" + "=" * 65)
    print("[SUCCESS] Windows Hello PIN encrypted and saved with Windows DPAPI!")
    print("=" * 65)
    print("\nHow It Works Now:")
    print(" 1. Press [Win + L] to lock your screen.")
    print(" 2. Look at your camera (it recognizes your face in ~1 second).")
    print(" 3. Windows Hello Refined verifies your face and unlocks your PC instantly!")
    print(" 4. If face is not recognized, your PIN remains available to enter manually.")
    print("=" * 65 + "\n")
    return 0

if __name__ == "__main__":
    sys.exit(main())
