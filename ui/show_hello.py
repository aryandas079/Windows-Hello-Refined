import os
import sys
import webview

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HTML_FILE = os.path.join(BASE_DIR, "ui", "hello_animation.html")

def show_transparent_hello(username="Aryan"):
    with open(HTML_FILE, "r", encoding="utf-8") as f:
        html_content = f.read()

    html_content = html_content.replace("Hello, Aryan", f"Hello, {username}")

    w, h = 380, 280
    window = webview.create_window(
        title="Windows Hello",
        html=html_content,
        width=w,
        height=h,
        frameless=True,
        transparent=True,
        on_top=True,
        easy_drag=False
    )

    webview.start()

if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else "Aryan"
    show_transparent_hello(name)
