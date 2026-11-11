import os
import pickle
import win32crypt

PROFILE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data")
PROFILE_FILE = os.path.join(PROFILE_DIR, "face_profile.enc")
CREDS_FILE = os.path.join(PROFILE_DIR, "credentials.enc")

CRYPTPROTECT_LOCAL_MACHINE = 0x4

def save_profile(username, embeddings):
    """
    Encrypts and saves the enrolled biometric embeddings using Windows DPAPI.
    Uses CRYPTPROTECT_LOCAL_MACHINE (0x4) so both the user and system-level
    lock-screen components (LogonUI / reboot startup) can decrypt on this PC.
    """
    os.makedirs(PROFILE_DIR, exist_ok=True)
    payload = {
        "username": username,
        "embeddings": embeddings,
        "count": len(embeddings)
    }
    raw_data = pickle.dumps(payload)
    encrypted_blob = win32crypt.CryptProtectData(
        raw_data,
        "WindowsHelloRefinedBiometrics",
        None,
        None,
        None,
        CRYPTPROTECT_LOCAL_MACHINE
    )
    with open(PROFILE_FILE, "wb") as f:
        f.write(encrypted_blob)
    return PROFILE_FILE

def load_profile():
    """
    Decrypts and loads the enrolled biometric embeddings using Windows DPAPI.
    Returns: dict with 'username', 'embeddings', 'count' or None if not found.
    """
    if not os.path.exists(PROFILE_FILE):
        return None

    try:
        with open(PROFILE_FILE, "rb") as f:
            encrypted_blob = f.read()
        _, decrypted_data = win32crypt.CryptUnprotectData(
            encrypted_blob,
            None,
            None,
            None,
            0
        )
        return pickle.loads(decrypted_data)
    except Exception as e:
        print(f"Error loading face profile: {e}")
        return None

def save_credentials(username, password=None, domain=None, pin=None):
    """
    Encrypts user logon credentials (PIN or Password) using Windows DPAPI.
    Uses CRYPTPROTECT_LOCAL_MACHINE (0x4) so both the user and system-level
    lock-screen components (LogonUI / reboot startup) can decrypt on this PC.
    Retains existing credentials when only updating PIN or password.
    """
    os.makedirs(PROFILE_DIR, exist_ok=True)
    existing = load_credentials() or {}

    payload = {
        "username": username or existing.get("username", ""),
        "password": password if password is not None else existing.get("password", ""),
        "domain": domain if domain is not None else existing.get("domain", ""),
        "pin": pin if pin is not None else existing.get("pin", "")
    }
    raw_data = pickle.dumps(payload)
    encrypted_blob = win32crypt.CryptProtectData(
        raw_data,
        "WindowsHelloRefinedCredentials",
        None,
        None,
        None,
        CRYPTPROTECT_LOCAL_MACHINE
    )
    with open(CREDS_FILE, "wb") as f:
        f.write(encrypted_blob)
    return CREDS_FILE

def load_credentials():
    """Decrypts stored credentials using Windows DPAPI."""
    if not os.path.exists(CREDS_FILE):
        return None

    try:
        with open(CREDS_FILE, "rb") as f:
            encrypted_blob = f.read()
        _, decrypted_data = win32crypt.CryptUnprotectData(
            encrypted_blob,
            None,
            None,
            None,
            0
        )
        return pickle.loads(decrypted_data)
    except Exception as e:
        print(f"Error loading credentials: {e}")
        return None

def profile_exists():
    return os.path.exists(PROFILE_FILE)
