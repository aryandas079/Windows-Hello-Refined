import os
import sys
import re
import getpass
import subprocess
import win32security
import win32con

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from engine.storage import save_credentials, load_credentials

def get_account_identities():
    local_user = getpass.getuser()
    identities = [local_user]

    # Detect Microsoft Account email if applicable
    try:
        output = subprocess.check_output("whoami /groups", shell=True).decode('utf-8', errors='ignore')
        msa_matches = re.findall(r"MicrosoftAccount\\([^\s]+)", output)
        for email in msa_matches:
            if email not in identities:
                identities.insert(0, email)  # put MSA email first
    except Exception:
        pass

    return identities

def main():
    print("=" * 65)
    print("      WINDOWS HELLO REFINED - AUTO-UNLOCK CREDENTIAL SETUP")
    print("=" * 65)
    print("To automatically unlock your Windows desktop when your face is recognized,")
    print("Windows requires your account password (stored securely using Windows DPAPI).")
    print("\nIMPORTANT:")
    print(" - If your Windows is signed into a Microsoft Account (e.g. Outlook/Hotmail/Gmail),")
    print("   please enter your Microsoft Account password (NOT your 4-digit PIN).")
    print(" - Your password is encrypted on-disk with hardware-backed DPAPI.")
    print("=" * 65)

    identities = get_account_identities()
    primary_id = identities[0]
    print(f"\n[*] Detected Account: [{primary_id}]")

    attempts = 0
    while attempts < 3:
        entered_pwd = getpass.getpass(f"\nEnter Windows / Microsoft password for [{primary_id}]: ").strip()
        if not entered_pwd:
            print("[*] No password entered. Auto-unlock will remain disabled.")
            print("[*] Face recognition will verify, but you will need to enter your PIN to unlock.")
            return 0

        print("[*] Validating password with Windows Local Security Authority (LSA)...")
        verified_identity = None

        for identity in identities:
            try:
                h = win32security.LogonUser(
                    identity,
                    None,
                    entered_pwd,
                    win32con.LOGON32_LOGON_INTERACTIVE,
                    win32con.LOGON32_PROVIDER_DEFAULT
                )
                verified_identity = identity
                break
            except Exception:
                continue

        if verified_identity:
            save_credentials(verified_identity, entered_pwd)
            print("\n" + "=" * 65)
            print(f"[SUCCESS] Password verified for [{verified_identity}]!")
            print("[SUCCESS] Credentials encrypted and saved with Windows DPAPI.")
            print("=" * 65)
            print("\nYour lock screen is now configured for INSTANT AUTO-UNLOCK:")
            print(" 1. Press [Win + L] to lock.")
            print(" 2. Look at your webcam (it recognizes your face in ~1s).")
            print(" 3. Windows will automatically unlock to your desktop!")
            print(" 4. Windows Hello PIN remains fully available in Sign-in options.")
            print("=" * 65 + "\n")
            return 0
        else:
            attempts += 1
            print(f"[!] ERROR: The password entered is incorrect (attempt {attempts}/3).")
            print("    Remember: enter your full Windows account / Microsoft password, not your PIN.")

    print("\n[!] Setup cancelled after 3 failed attempts to protect your account.")
    return 1

if __name__ == "__main__":
    sys.exit(main())
