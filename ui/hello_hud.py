import os
import sys
import tkinter as tk

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS_DIR = os.path.join(BASE_DIR, "assets")

class WindowsHelloHUD:
    def __init__(self, username="Aryan"):
        self.username = username
        self.root = tk.Tk()
        self.root.overrideredirect(True)
        self.root.attributes("-topmost", True)
        self.root.attributes("-transparentcolor", "#000001")
        self.root.config(bg="#000001")

        # Top center geometry
        sw = self.root.winfo_screenwidth()
        w, h = 420, 160
        x = (sw - w) // 2
        y = 55
        self.root.geometry(f"{w}x{h}+{x}+{y}")

        # Load PNGs
        self.img_normal = tk.PhotoImage(file=os.path.join(ASSETS_DIR, "hello_normal_hud.png"))
        self.img_scan = tk.PhotoImage(file=os.path.join(ASSETS_DIR, "hello_scan_hud.png"))
        self.img_wink = tk.PhotoImage(file=os.path.join(ASSETS_DIR, "hello_wink_hud.png"))

        # Icon
        self.icon_label = tk.Label(self.root, bg="#000001", image=self.img_scan)
        self.icon_label.pack(pady=(10, 5))

        # Title / Status Text
        self.text_label = tk.Label(
            self.root,
            text="Looking for you...",
            font=("Segoe UI", 13),
            fg="#FFFFFF",
            bg="#000001"
        )
        self.text_label.pack()

        # Secondary subtitle
        self.sub_label = tk.Label(
            self.root,
            text="",
            font=("Segoe UI", 10),
            fg="#BBBBBB",
            bg="#000001"
        )
        self.sub_label.pack()

    def set_state(self, state, custom_msg=None):
        if state == "scanning":
            self.icon_label.config(image=self.img_scan)
            self.text_label.config(text=custom_msg or "Looking for you...")
            self.sub_label.config(text="")
        elif state == "recognizing":
            self.icon_label.config(image=self.img_normal)
            self.text_label.config(text=custom_msg or "Making sure it's you...")
            self.sub_label.config(text="")
        elif state == "verified":
            self.icon_label.config(image=self.img_wink)
            self.text_label.config(text=custom_msg or f"Welcome back, {self.username}!")
            self.sub_label.config(text="")
            self.root.after(1100, self.close)
        elif state == "failed":
            self.icon_label.config(image=self.img_normal)
            self.text_label.config(text="Couldn't recognize you")
            self.sub_label.config(text="Swipe up or use PIN/Password")
            self.root.after(2200, self.close)

    def close(self):
        try:
            self.root.destroy()
        except Exception:
            pass

    def run_preview(self):
        def to_recognizing():
            self.set_state("recognizing")
            self.root.after(900, to_verified)

        def to_verified():
            self.set_state("verified")

        self.set_state("scanning")
        self.root.after(1000, to_recognizing)
        self.root.mainloop()

if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else "Aryan"
    hud = WindowsHelloHUD(name)
    hud.run_preview()
