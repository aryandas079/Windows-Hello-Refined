import json
import os

CONFIG_FILE = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "data",
    "presence_config.json",
)
CHECK_INTERVAL = 5.0
PRESETS = (
    ("30s", 30),
    ("1m", 60),
    ("2m", 120),
    ("5m", 300),
    ("10m", 600),
    ("30m", 1800),
    ("1hr", 3600),
    ("Never", None),
)


def load_timeout():
    try:
        with open(CONFIG_FILE, "r", encoding="utf-8") as handle:
            value = json.load(handle).get("timeout_seconds", 30)
        return None if value is None else max(1, int(value))
    except (OSError, ValueError, TypeError, json.JSONDecodeError):
        return 30


def save_timeout(seconds):
    os.makedirs(os.path.dirname(CONFIG_FILE), exist_ok=True)
    with open(CONFIG_FILE, "w", encoding="utf-8") as handle:
        json.dump({"timeout_seconds": seconds}, handle, indent=2)


def max_missed_checks(seconds):
    return 0 if seconds is None else max(1, int(round(seconds / CHECK_INTERVAL)))


def label(seconds):
    if seconds is None:
        return "Never"
    for name, value in PRESETS:
        if value == seconds:
            return name
    return f"{seconds}s"


def prompt_timeout():
    print("\nAuto-lock when no face is detected:")
    for index, (name, _) in enumerate(PRESETS, 1):
        print(f"  {index}. {name}")
    while True:
        choice = input("Select timeout [1-8] (default: 30s): ").strip() or "1"
        if choice.isdigit() and 1 <= int(choice) <= len(PRESETS):
            return PRESETS[int(choice) - 1][1]
        print("Invalid choice. Select a number from 1 to 8.")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--interactive", action="store_true")
    args = parser.parse_args()
    if args.interactive:
        selected = prompt_timeout()
        save_timeout(selected)
        print(f"Auto-lock timeout saved: {label(selected)}")
