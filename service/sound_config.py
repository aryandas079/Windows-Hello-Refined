import json
import os
import sys
import winsound

CONFIG_FILE = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "data",
    "presence_config.json",
)

SOUND_PRESETS = [
    ("Default (Windows Hello Soft Chime)", "default"),
    ("Windows Unlock", r"C:\Windows\Media\Windows Unlock.wav"),
    ("Windows Notify", r"C:\Windows\Media\Windows Notify.wav"),
    ("Chimes", r"C:\Windows\Media\chimes.wav"),
    ("Chord", r"C:\Windows\Media\chord.wav"),
    ("Ding", r"C:\Windows\Media\ding.wav"),
    ("Tada", r"C:\Windows\Media\tada.wav"),
    ("None (Silent)", "none"),
]


def load_sound():
    try:
        with open(CONFIG_FILE, "r", encoding="utf-8") as handle:
            return json.load(handle).get("sound", "default")
    except (OSError, ValueError, TypeError, json.JSONDecodeError):
        return "default"


def save_sound(sound_choice):
    os.makedirs(os.path.dirname(CONFIG_FILE), exist_ok=True)
    data = {}
    try:
        if os.path.exists(CONFIG_FILE):
            with open(CONFIG_FILE, "r", encoding="utf-8") as handle:
                data = json.load(handle)
    except Exception:
        data = {}

    data["sound"] = sound_choice
    with open(CONFIG_FILE, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2)


def play_preview(sound_val):
    if sound_val == "none":
        return
    if sound_val == "default":
        asset = os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            "assets",
            "hello_verified.wav",
        )
        if os.path.exists(asset):
            winsound.PlaySound(asset, winsound.SND_FILENAME | winsound.SND_ASYNC)
        else:
            winsound.PlaySound(r"C:\Windows\Media\Windows Notify.wav", winsound.SND_FILENAME | winsound.SND_ASYNC)
    elif os.path.exists(sound_val):
        winsound.PlaySound(sound_val, winsound.SND_FILENAME | winsound.SND_ASYNC)


def prompt_sound():
    print("\nSelect unlock feedback sound:")
    for index, (name, _) in enumerate(SOUND_PRESETS, 1):
        print(f"  {index}. {name}")
    print("  P<number> to preview sound (e.g. P1, P2)")
    print("  Custom path (e.g. C:\\path\\to\\custom.wav)")

    while True:
        choice = input("\nSelect sound [1-8] (default: 1): ").strip()
        if not choice:
            return "default"

        if choice.upper().startswith("P") and choice[1:].isdigit():
            idx = int(choice[1:])
            if 1 <= idx <= len(SOUND_PRESETS):
                val = SOUND_PRESETS[idx - 1][1]
                print(f"Previewing: {SOUND_PRESETS[idx - 1][0]}")
                play_preview(val)
                continue
            else:
                print("Invalid preview number.")
                continue

        if choice.isdigit() and 1 <= int(choice) <= len(SOUND_PRESETS):
            val = SOUND_PRESETS[int(choice) - 1][1]
            play_preview(val)
            return val

        if os.path.isfile(choice) and choice.lower().endswith(".wav"):
            play_preview(choice)
            return choice

        print("Invalid choice. Enter 1-8 or a valid .wav file path.")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--interactive", action="store_true")
    parser.add_argument("--set", type=str, help="Set sound directly ('default', 'none', or path)")
    args = parser.parse_args()

    if args.set:
        save_sound(args.set)
        print(f"Unlock sound updated to: {args.set}")
    elif args.interactive or len(sys.argv) == 1:
        chosen = prompt_sound()
        save_sound(chosen)
        for name, val in SOUND_PRESETS:
            if val == chosen:
                print(f"\n[SUCCESS] Unlock sound set to: {name}")
                break
        else:
            print(f"\n[SUCCESS] Unlock sound set to custom: {chosen}")
