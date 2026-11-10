import os
import pickle
import win32crypt

PROFILE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data")
PROFILE_FILE = os.path.join(PROFILE_DIR, "face_profile.enc")

def save_profile(username, embeddings):
    """
    Encrypts and saves the enrolled biometric embeddings using Windows DPAPI.
    Only the current Windows user / system can decrypt this data.
    """
    os.makedirs(PROFILE_DIR, exist_ok=True)
    payload = {
        "username": username,
        "embeddings": embeddings,
        "count": len(embeddings)
    }
    raw_data = pickle.dumps(payload)
    # Encrypt using Windows DPAPI
    encrypted_blob = win32crypt.CryptProtectData(
        raw_data,
        "WindowsHelloRefinedBiometrics",
        None,
        None,
        None,
        0
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

def profile_exists():
    return os.path.exists(PROFILE_FILE)
