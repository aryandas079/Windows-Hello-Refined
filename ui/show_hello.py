import os
import sys
import webview

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HTML_FILE = os.path.join(BASE_DIR, "ui", "hello_animation.html")

def get_screen_center():
    """Get screen dimensions to position the window top-center."""
    try:
        import ctypes
        user32 = ctypes.windll.user32
        sw = user32.GetSystemMetrics(0)
        return sw
    except Exception:
        return 1920

def show_transparent_hello(username="Aryan"):
    """
    Launch the Windows Hello animation in a transparent, frameless, always-on-top window.
    Uses file:// URL so relative assets (gsap.min.js) load correctly.
    """
    screen_width = get_screen_center()
    win_w, win_h = 380, 300
    win_x = (screen_width - win_w) // 2
    win_y = 40  # near top of screen, like real Windows Hello

    # Build file:// URL — this lets the WebView resolve gsap.min.js relatively
    file_url = "file:///" + HTML_FILE.replace("\\", "/")

    window = webview.create_window(
        title="Windows Hello",
        url=file_url,
        width=win_w,
        height=win_h,
        x=win_x,
        y=win_y,
        frameless=True,
        transparent=True,
        on_top=True,
        easy_drag=False
    )

    # Inject username into the page once it loads
    def on_loaded():
        safe_name = username.replace("'", "\\'")
        window.evaluate_js(f"setUsername('{safe_name}')")

    window.events.loaded += on_loaded

    webview.start()

if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else "Aryan"
    show_transparent_hello(name)
