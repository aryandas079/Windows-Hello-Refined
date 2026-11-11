import sys
import winreg

def show_banner():
    print("=" * 68)
    print("        WINDOWS HELLO REFINED - LOCK SCREEN DE-CLUTTER")
    print("                        Made by Aryan Das")
    print("          GitHub:   https://github.com/aryandas079 (@aryandas079)")
    print("          LinkedIn: https://linkedin.com/in/iam-aryandas (@iam-aryandas)")
    print("=" * 68)

def set_reg_value(hive, subkey, name, value, value_type=winreg.REG_DWORD):
    try:
        key = winreg.CreateKeyEx(hive, subkey, 0, winreg.KEY_SET_VALUE | winreg.KEY_WRITE)
        winreg.SetValueEx(key, name, 0, value_type, value)
        winreg.CloseKey(key)
        return True
    except Exception as e:
        return False

def clean_lock_screen():
    """
    Disables Windows Spotlight fun facts, tips, tricks, news suggestions,
    and MSN lock screen widgets (Weather, Stocks, Sports, Traffic).
    Leaves the lock screen completely minimal, clean, and distraction-free.
    """
    print("\n[*] Cleaning Windows Lock Screen:")

    # 1. ContentDeliveryManager: Fun facts, tips, and Spotlight overlays
    cdm_key = r"Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager"
    tweaks_cdm = [
        ("RotatingLockScreenOverlayEnabled", 0, "Windows Spotlight Fun Facts & Overlays"),
        ("SubscribedContent-338387Enabled", 0, "Tips, Tricks, and Suggestions"),
        ("SubscribedContent-338388Enabled", 0, "Spotlight Recommendations"),
        ("SubscribedContent-338389Enabled", 0, "Edge / Lock Screen Prompts"),
        ("SoftLandingEnabled", 0, "Welcome Experience & Tips"),
        ("SystemPaneSuggestionsEnabled", 0, "Lock Screen System Suggestions")
    ]

    for name, val, desc in tweaks_cdm:
        ok = set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, name, val)
        status = "[DONE]" if ok else "[FAIL]"
        print(f"  {status} Disable {desc}")

    # 2. Lock Screen Widgets (Weather, Stocks, Sports, Traffic on Windows 11)
    ls_key = r"Software\Microsoft\Windows\CurrentVersion\Lock Screen"
    tweaks_ls = [
        ("LockScreenWidgetsEnabled", 0, "Lock Screen MSN Widgets (Weather/News/Stocks)"),
        ("LockScreenWidgetsSystemCurationEnabled", 0, "Widget System Curation")
    ]
    for name, val, desc in tweaks_ls:
        ok = set_reg_value(winreg.HKEY_CURRENT_USER, ls_key, name, val)
        status = "[DONE]" if ok else "[FAIL]"
        print(f"  {status} Disable {desc}")

    # 3. Lock Screen Feeds
    feed_key = r"Software\Microsoft\Windows\CurrentVersion\Feeds"
    set_reg_value(winreg.HKEY_CURRENT_USER, feed_key, "LockScreenFeedEnabled", 0)

    # 4. HKLM CloudContent Policies (Optional, applies if running as Administrator)
    policy_key = r"SOFTWARE\Policies\Microsoft\Windows\CloudContent"
    set_reg_value(winreg.HKEY_LOCAL_MACHINE, policy_key, "DisableWindowsSpotlightFeatures", 1)
    set_reg_value(winreg.HKEY_LOCAL_MACHINE, policy_key, "DisableSoftLanding", 1)

    print("\n[SUCCESS] Lock screen suggestions, fun facts, and widgets disabled!")
    print("Your lock screen will now be completely clean and distraction-free.")

def restore_lock_screen():
    """Restores default Windows lock screen suggestions and widgets."""
    print("\n[*] Restoring Default Lock Screen Settings:")
    cdm_key = r"Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager"
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "RotatingLockScreenOverlayEnabled", 1)
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "SubscribedContent-338387Enabled", 1)
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "SubscribedContent-338388Enabled", 1)
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "SubscribedContent-338389Enabled", 1)
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "SoftLandingEnabled", 1)

    ls_key = r"Software\Microsoft\Windows\CurrentVersion\Lock Screen"
    set_reg_value(winreg.HKEY_CURRENT_USER, ls_key, "LockScreenWidgetsEnabled", 1)

    print("\n[SUCCESS] Windows default lock screen settings restored.")

def main():
    show_banner()
    print("\nOptions:")
    print("  1. Remove all lock screen suggestions, news, fun facts, and widgets")
    print("  2. Restore default Windows lock screen suggestions")
    print("  0. Cancel")

    choice = input("\nSelect an option [0-2]: ").strip()
    if choice == "1":
        clean_lock_screen()
    elif choice == "2":
        restore_lock_screen()
    else:
        print("[*] Cancelled.")

if __name__ == "__main__":
    main()
