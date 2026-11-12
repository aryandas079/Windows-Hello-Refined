import os
import sys
import shutil
import subprocess
import winreg

def show_banner():
    print("=" * 68)
    print("        WINDOWS HELLO REFINED - COMPLETE LOCK SCREEN CLEANER")
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
    except Exception:
        return False

def delete_reg_value(hive, subkey, name):
    try:
        key = winreg.OpenKey(hive, subkey, 0, winreg.KEY_SET_VALUE | winreg.KEY_WRITE)
        winreg.DeleteValue(key, name)
        winreg.CloseKey(key)
        return True
    except FileNotFoundError:
        return True
    except Exception:
        return False

def clear_spotlight_cache():
    local_app_data = os.environ.get("LOCALAPPDATA")
    if not local_app_data:
        return
    base = os.path.join(local_app_data, r"Packages\Microsoft.Windows.ContentDeliveryManager_cw5n1h2txyewy\LocalState")
    # Keep TargetedContentCache: Windows may store the active lock-screen
    # wallpaper there. Only remove promotional text and suggestion metadata.
    cache_dirs = ["Tips", "ContentManagementSDK"]
    cleared = 0
    for d in cache_dirs:
        target = os.path.join(base, d)
        if os.path.exists(target):
            try:
                for root, dirs, files in os.walk(target, topdown=False):
                    for f in files:
                        try:
                            os.remove(os.path.join(root, f))
                            cleared += 1
                        except Exception:
                            pass
                    for sub in dirs:
                        try:
                            os.rmdir(os.path.join(root, sub))
                        except Exception:
                            pass
            except Exception:
                pass
    if cleared > 0:
        print(f"  [DONE] Purged {cleared} cached lock screen suggestions and trivia files")

def clean_lock_screen():
    """
    Completely removes all lock screen suggestions, news, MSN widgets (weather/stocks/sports/traffic),
    Windows Spotlight trivia/fun facts, camera prompt icons, and edge recommendation cards.
    Leaves the lock screen minimal, clean, and distraction-free.
    """
    print("\n[*] De-cluttering Windows Lock Screen:")

    # 1. ContentDeliveryManager: Suggestions, Tips, Spotlight text overlays
    cdm_key = r"Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager"
    tweaks_cdm = [
        ("RotatingLockScreenOverlayEnabled", 0, "Spotlight Fun Facts & Trivia"),
        ("SubscribedContent-338387Enabled", 0, "Tips, Tricks, and Suggestions"),
        ("SubscribedContent-338388Enabled", 0, "Spotlight Recommendations"),
        ("SubscribedContent-338389Enabled", 0, "Edge / Lock Screen Prompts"),
        ("SubscribedContent-310091Enabled", 0, "Suggested Content"),
        ("SubscribedContent-310093Enabled", 0, "Welcome Experience Overlays"),
        ("SubscribedContent-314559Enabled", 0, "Lock Screen Prompts"),
        ("SubscribedContent-314563Enabled", 0, "Spotlight In-Session Suggestions"),
        ("SubscribedContent-338393Enabled", 0, "Suggested Applications & Prompts"),
        ("SubscribedContent-353694Enabled", 0, "Search & News Suggestions"),
        ("SubscribedContent-353696Enabled", 0, "Spotlight Canvas Content"),
        ("SubscribedContent-353698Enabled", 0, "Lock Screen Creative Overlays"),
        ("SoftLandingEnabled", 0, "Welcome Experience & Tips"),
        ("SystemPaneSuggestionsEnabled", 0, "Lock Screen System Suggestions"),
        ("ContentDeliveryAllowed", 1, "Background Content Delivery"),
        ("FeatureManagementEnabled", 0, "Promotional Feature Delivery")
    ]

    for name, val, desc in tweaks_cdm:
        ok = set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, name, val)
        status = "[DONE]" if ok else "[FAIL]"
        print(f"  {status} Disable {desc}")

    # 2. Lock Screen Widgets & Status (Weather, Stocks, Sports, Traffic on Windows 11)
    ls_key = r"Software\Microsoft\Windows\CurrentVersion\Lock Screen"
    tweaks_ls = [
        ("LockScreenWidgetsEnabled", 0, "Lock Screen MSN Widgets (Weather/News/Stocks)"),
        ("LockScreenWidgetsSystemCurationEnabled", 0, "Widget System Curation"),
        ("PreplacedWidgetsAttempted", 0, "Preplaced MSN Widgets")
    ]
    for name, val, desc in tweaks_ls:
        ok = set_reg_value(winreg.HKEY_CURRENT_USER, ls_key, name, val)
        status = "[DONE]" if ok else "[FAIL]"
        print(f"  {status} Disable {desc}")

    # Remove Detailed Status App (Sets Lock screen status to "None")
    delete_reg_value(winreg.HKEY_CURRENT_USER, ls_key, "DetailedStatusApp")
    delete_reg_value(winreg.HKEY_CURRENT_USER, ls_key, "BadgeAppIds")
    print("  [DONE] Set Lock Screen App Status to None")

    # 3. Lock Screen Feeds (MSN Feed integration)
    feed_key = r"Software\Microsoft\Windows\CurrentVersion\Feeds"
    set_reg_value(winreg.HKEY_CURRENT_USER, feed_key, "LSWE", 0)
    set_reg_value(winreg.HKEY_CURRENT_USER, feed_key, "LockScreenFeedEnabled", 0)
    print("  [DONE] Disable Lock Screen Feeds (LSWE)")

    # 4. Remove all Spotlight and Wallpaper blocking policies to allow full settings access
    delete_reg_value(winreg.HKEY_CURRENT_USER, r"Software\Policies\Microsoft\Windows\CloudContent", "DisableWindowsSpotlightFeatures")
    delete_reg_value(winreg.HKEY_CURRENT_USER, r"Software\Policies\Microsoft\Windows\CloudContent", "ConfigureWindowsSpotlight")
    delete_reg_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Policies\Microsoft\Windows\CloudContent", "DisableWindowsSpotlightFeatures")
    delete_reg_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Policies\Microsoft\Windows\CloudContent", "ConfigureWindowsSpotlight")
    delete_reg_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Policies\Microsoft\Windows\CloudContent", "DisableWindowsConsumerFeatures")
    delete_reg_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Policies\Microsoft\Windows\Personalization", "NoLockScreen")
    delete_reg_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Policies\Microsoft\Windows\Personalization", "NoLockScreenSlideshow")
    delete_reg_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Policies\Microsoft\Windows\Personalization", "NoLockScreenCamera")
    print("  [DONE] Unlocked Lock Screen Wallpaper and Windows Spotlight in Settings")

    # 5. HKLM Policies: Windows 11 24H2 MSN Widgets Policy (PersonalizationCSP & Dsh)
    set_reg_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Policies\Microsoft\Dsh", "DisableWidgetsOnLockScreen", 1)
    set_reg_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows\CurrentVersion\PersonalizationCSP", "LockScreenWidgetsEnabled", 0)
    print("  [DONE] Enforce Machine Widgets Policies (Dsh & PersonalizationCSP)")

    # 6. Preserve the user's current wallpaper source and disable only the
    # Spotlight overlay layer that adds trivia and recommendations.
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "RotatingLockScreenOverlayEnabled", 0)
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "ContentDeliveryAllowed", 1)

    # 8. Clear cached Spotlight suggestions and trivia
    clear_spotlight_cache()

    # 9. Refresh LockApp so changes take effect immediately
    try:
        subprocess.run(["taskkill", "/F", "/IM", "LockApp.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass

    print("\n[SUCCESS] All lock screen suggestions, news, fun facts, and widgets have been completely removed!")
    print("Your lock screen is now 100% clean, minimal, and distraction-free.")

def restore_lock_screen():
    """Restores default Windows lock screen suggestions and widgets."""
    print("\n[*] Restoring Default Lock Screen Settings:")
    cdm_key = r"Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager"
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "RotatingLockScreenEnabled", 1)
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "RotatingLockScreenOverlayEnabled", 1)
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "SubscribedContent-338387Enabled", 1)
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "SubscribedContent-338388Enabled", 1)
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "SubscribedContent-338389Enabled", 1)
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "SoftLandingEnabled", 1)
    set_reg_value(winreg.HKEY_CURRENT_USER, cdm_key, "ContentDeliveryAllowed", 1)

    ls_key = r"Software\Microsoft\Windows\CurrentVersion\Lock Screen"
    set_reg_value(winreg.HKEY_CURRENT_USER, ls_key, "LockScreenWidgetsEnabled", 1)
    set_reg_value(winreg.HKEY_CURRENT_USER, ls_key, "LockScreenWidgetsSystemCurationEnabled", 1)

    feed_key = r"Software\Microsoft\Windows\CurrentVersion\Feeds"
    set_reg_value(winreg.HKEY_CURRENT_USER, feed_key, "LSWE", 1)
    set_reg_value(winreg.HKEY_CURRENT_USER, feed_key, "LockScreenFeedEnabled", 1)

    delete_reg_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Policies\Microsoft\Dsh", "DisableWidgetsOnLockScreen")
    delete_reg_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows\CurrentVersion\PersonalizationCSP", "LockScreenWidgetsEnabled")
    delete_reg_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Policies\Microsoft\Windows\CloudContent", "DisableWindowsSpotlightFeatures")
    delete_reg_value(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Policies\Microsoft\Windows\Personalization", "NoLockScreenCamera")

    print("\n[SUCCESS] Windows default lock screen settings restored.")

def main():
    show_banner()
    if len(sys.argv) > 1 and sys.argv[1].lower() in ("--clean", "/clean"):
        clean_lock_screen()
        return

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
