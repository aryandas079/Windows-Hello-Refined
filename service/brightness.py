"""Temporary panel illumination; WMI runs on its own COM-initialized thread."""
import queue
import threading
import time


class BrightnessController:
    def __init__(self, log=lambda message: None):
        self.log = log
        self.commands = queue.Queue()
        self.is_boosted = False
        threading.Thread(target=self._worker, daemon=True).start()

    def boost(self):
        """Boost display brightness to 100%."""
        done = threading.Event()
        res = [False]
        self.commands.put(("boost", done, res))
        done.wait(1.5)
        return res[0]

    def restore(self, wait=False):
        """Restore display brightness to original level."""
        done = threading.Event() if wait else None
        self.commands.put(("restore", done, None))
        if done:
            done.wait(1.0)

    def _worker(self):
        import pythoncom
        import win32com.client

        pythoncom.CoInitialize()
        saved = []
        deadline = 0
        wmi = None

        def set_brightness(method, val):
            in_params = method.Methods_('WmiSetBrightness').InParameters.SpawnInstance_()
            in_params.Timeout = 0
            in_params.Brightness = int(val)
            method.ExecMethod_('WmiSetBrightness', in_params)

        def restore():
            nonlocal saved, wmi
            pending, saved = saved, []
            self.is_boosted = False
            if not pending:
                return
            if wmi is None:
                try:
                    wmi = win32com.client.GetObject(r"winmgmts:\\.\root\wmi")
                except Exception:
                    pass
            for name, original, boosted, method in pending:
                try:
                    current = {item.InstanceName: int(item.CurrentBrightness)
                               for item in wmi.ExecQuery("SELECT * FROM WmiMonitorBrightness WHERE Active=TRUE")}
                    curr_val = current.get(name)
                    # Restore if brightness is at or near boosted target (>= 90%)
                    if curr_val is None or curr_val >= 90:
                        set_brightness(method, original)
                        self.log(f"Restored panel brightness: {boosted}% -> {original}%")
                except Exception as exc:
                    self.log(f"Brightness restore failed: {exc}")

        try:
            while True:
                try:
                    item = self.commands.get(timeout=max(0.01, deadline - time.monotonic()) if saved else None)
                    command, done, res = item
                except queue.Empty:
                    command = "restore"
                    done = None
                    res = None

                if command == "restore":
                    restore()
                    if done:
                        done.set()
                    continue

                if saved:
                    if done:
                        if res:
                            res[0] = True
                        done.set()
                    continue  # Already boosted, don't overwrite original baseline

                try:
                    if wmi is None:
                        wmi = win32com.client.GetObject(r"winmgmts:\\.\root\wmi")
                    levels = {item.InstanceName: (int(item.CurrentBrightness), list(item.Level))
                              for item in wmi.ExecQuery("SELECT * FROM WmiMonitorBrightness WHERE Active=TRUE")}
                    did_boost = False
                    for method in wmi.ExecQuery("SELECT * FROM WmiMonitorBrightnessMethods WHERE Active=TRUE"):
                        if method.InstanceName not in levels:
                            continue
                        original, supported = levels[method.InstanceName]
                        target = 100 if 100 in supported else (max(supported) if supported else 100)
                        if original >= target:
                            self.log(f"Panel brightness already at maximum ({original}%).")
                            continue
                        # Record before setting, so a partial failure can still be restored
                        saved.append((method.InstanceName, original, target, method))
                        set_brightness(method, target)
                        did_boost = True
                        self.log(f"Low-light panel illumination: {original}% -> {target}%")

                    self.is_boosted = did_boost
                    if res:
                        res[0] = did_boost
                    if done:
                        done.set()
                    deadline = time.monotonic() + 5.0
                except Exception as exc:
                    self.log(f"Panel brightness unavailable: {exc}")
                    restore()
                    wmi = None
                    if done:
                        done.set()
        finally:
            restore()
            pythoncom.CoUninitialize()
