import os
import sys
import math
import time
import tkinter as tk

class WindowsHelloVectorHUD:
    def __init__(self, username="Aryan", on_close=None):
        self.username = username
        self.on_close = on_close

        self.root = tk.Tk()
        self.root.overrideredirect(True)
        self.root.attributes("-topmost", True)

        # True transparent background key
        self.bg_key = "#010101"
        self.root.attributes("-transparentcolor", self.bg_key)
        self.root.config(bg=self.bg_key)

        # Window geometry: Top center of primary monitor
        sw = self.root.winfo_screenwidth()
        self.w, self.h = 440, 170
        x = (sw - self.w) // 2
        y = 45
        self.root.geometry(f"{self.w}x{self.h}+{x}+{y}")

        # Vector Canvas
        self.cv = tk.Canvas(
            self.root,
            width=self.w,
            height=self.h,
            bg=self.bg_key,
            highlightthickness=0
        )
        self.cv.pack()

        # States: "scanning", "recognizing", "verified", "failed"
        self.state = "scanning"
        self.sweep_angle = 0.0
        self.gaze_t = 0.0
        self.wink_progress = 0.0  # 0.0 = open, 1.0 = fully winking
        self.smile_width = 34
        self.is_running = True

        # Center coordinates for face glyph
        self.cx = self.w // 2
        self.cy = 48

        # Bind Esc to exit preview
        self.root.bind("<Escape>", lambda e: self.close())

        # Start 60 FPS animation loop
        self.last_tick = time.perf_counter()
        self._animate()

    def set_state(self, state, msg=None):
        self.state = state
        self.custom_msg = msg
        if state == "verified":
            self.root.after(1200, self.close)
        elif state == "failed":
            self.root.after(2000, self.close)

    def close(self):
        if not self.is_running:
            return
        self.is_running = False
        if self.on_close:
            self.on_close()
        try:
            self.root.destroy()
        except Exception:
            pass

    def _draw_frame(self):
        self.cv.delete("all")
        cx, cy = self.cx, self.cy

        # 1. SCANNING STATE: Sweeping radar arc & gazing eyes
        if self.state == "scanning":
            # Orbital sweep arc (Image 2 style)
            r_arc = 36
            arc_extent = 120
            self.cv.create_arc(
                cx - r_arc, cy - r_arc,
                cx + r_arc, cy + r_arc,
                start=int(self.sweep_angle),
                extent=arc_extent,
                style="arc",
                width=5,
                outline="#FFFFFF"
            )

            # Gazing eyes (gaze slightly left & right)
            gaze_x = math.sin(self.gaze_t * 2.8) * 5.0
            r_eye = 5
            # Left eye
            self.cv.create_oval(
                cx - 18 + gaze_x - r_eye, cy - 6 - r_eye,
                cx - 18 + gaze_x + r_eye, cy - 6 + r_eye,
                fill="#FFFFFF", outline="#FFFFFF"
            )
            # Right eye
            self.cv.create_oval(
                cx + 18 + gaze_x - r_eye, cy - 6 - r_eye,
                cx + 18 + gaze_x + r_eye, cy - 6 + r_eye,
                fill="#FFFFFF", outline="#FFFFFF"
            )

            # Relaxed subtle smile arc
            self.cv.create_arc(
                cx - 28, cy - 18,
                cx + 28, cy + 22,
                start=220, extent=100,
                style="arc", width=5, outline="#FFFFFF"
            )

            # Text
            self.cv.create_text(
                cx, 115,
                text="Looking for you...",
                font=("Segoe UI", 13),
                fill="#FFFFFF"
            )

        # 2. RECOGNIZING STATE: Centered eyes, locking smile
        elif self.state == "recognizing":
            r_eye = 6
            # Centered Left eye
            self.cv.create_oval(
                cx - 18 - r_eye, cy - 6 - r_eye,
                cx - 18 + r_eye, cy - 6 + r_eye,
                fill="#FFFFFF", outline="#FFFFFF"
            )
            # Centered Right eye
            self.cv.create_oval(
                cx + 18 - r_eye, cy - 6 - r_eye,
                cx + 18 + r_eye, cy - 6 + r_eye,
                fill="#FFFFFF", outline="#FFFFFF"
            )

            # Full Smile Arc
            self.cv.create_arc(
                cx - 32, cy - 16,
                cx + 32, cy + 26,
                start=215, extent=110,
                style="arc", width=6, outline="#FFFFFF"
            )

            # Text
            self.cv.create_text(
                cx, 115,
                text="Making sure it's you...",
                font=("Segoe UI", 13),
                fill="#FFFFFF"
            )

        # 3. VERIFIED STATE: Iconic Winking Face (Image 3 style)
        elif self.state == "verified":
            r_eye = 6
            # Left Eye: Wide open and smiling
            self.cv.create_oval(
                cx - 18 - r_eye, cy - 7 - r_eye,
                cx - 18 + r_eye, cy - 7 + r_eye,
                fill="#FFFFFF", outline="#FFFFFF"
            )

            # Right Eye: Smooth horizontal wink line / curve
            wink_y = cy - 7
            wink_len = 10
            self.cv.create_line(
                cx + 18 - wink_len, wink_y,
                cx + 18 + wink_len, wink_y,
                width=5, fill="#FFFFFF", capstyle="round"
            )

            # Cheerful Broad Smile Arc
            self.cv.create_arc(
                cx - 35, cy - 18,
                cx + 35, cy + 28,
                start=210, extent=120,
                style="arc", width=6, outline="#FFFFFF"
            )

            # Welcome Text
            self.cv.create_text(
                cx, 115,
                text=f"Welcome back, {self.username}!",
                font=("Segoe UI", 14, "bold"),
                fill="#FFFFFF"
            )

        # 4. FAILED STATE
        elif self.state == "failed":
            r_eye = 5
            self.cv.create_oval(
                cx - 18 - r_eye, cy - 6 - r_eye,
                cx - 18 + r_eye, cy - 6 + r_eye,
                fill="#DDDDDD", outline="#DDDDDD"
            )
            self.cv.create_oval(
                cx + 18 - r_eye, cy - 6 - r_eye,
                cx + 18 + r_eye, cy - 6 + r_eye,
                fill="#DDDDDD", outline="#DDDDDD"
            )
            # Straight neutral mouth
            self.cv.create_line(
                cx - 20, cy + 18,
                cx + 20, cy + 18,
                width=4, fill="#DDDDDD", capstyle="round"
            )
            self.cv.create_text(
                cx, 110,
                text="Couldn't recognize you",
                font=("Segoe UI", 12),
                fill="#FFFFFF"
            )
            self.cv.create_text(
                cx, 134,
                text="Swipe up to sign in with PIN or Password",
                font=("Segoe UI", 10),
                fill="#AAAAAA"
            )

    def _animate(self):
        if not self.is_running:
            return

        now = time.perf_counter()
        dt = now - self.last_tick
        self.last_tick = now

        # Update angle for scanning arc (approx 360 deg/sec)
        self.sweep_angle = (self.sweep_angle + 360.0 * dt * 0.9) % 360.0
        self.gaze_t += dt

        self._draw_frame()

        # Target ~60 FPS (16ms)
        self.root.after(16, self._animate)

    def run(self):
        self.root.mainloop()

    def run_demo(self):
        """Simulates full Windows Hello unlock cycle."""
        print("[*] Running smooth vector Windows Hello demonstration...")
        self.root.after(1600, lambda: self.set_state("recognizing"))
        self.root.after(2800, lambda: self.set_state("verified"))
        self.root.mainloop()

if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else "Aryan"
    hud = WindowsHelloVectorHUD(name)
    hud.run_demo()
