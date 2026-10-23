#!/usr/bin/env python3
"""QMP harness for FLX OS (stdlib only). Pattern from Lumen OS 2's test/q.py.
Coordinates are guest pixels (1920x1080), sent as absolute virtio-tablet events.
CLI:  qmp.py shot PATH | click X Y [left|right] | move X Y | drag X0 Y0 X1 Y1 [MS]
      qmp.py type TEXT | key COMBO (e.g. meta_l, alt-tab, ctrl-s, ret) | wheel X Y N | quit"""
import json, os, socket, sys, time

SOCK = os.environ.get("FLX_QMP", os.environ.get("W12_QMP", "/tmp/flx-qmp.sock"))
W, H = 1920, 1080


class QMP:
    def __init__(self, path=SOCK, tries=150):
        for _ in range(tries):
            try:
                self.s = socket.socket(socket.AF_UNIX); self.s.connect(path); break
            except OSError:
                time.sleep(0.1)
        else:
            raise RuntimeError("no QMP socket at " + path)
        self.f = self.s.makefile("rw")
        self.f.readline()
        self.cmd("qmp_capabilities")

    def close(self):
        try: self.s.close()
        except OSError: pass

    def cmd(self, name, **args):
        self.f.write(json.dumps({"execute": name, "arguments": args}) + "\n"); self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if "return" in r or "error" in r:
                if "error" in r: raise RuntimeError(r["error"])
                return r["return"]

    def _abs(self, x, y):
        return [{"type": "abs", "data": {"axis": "x", "value": int(max(0, min(W - 1, x)) * 32767 / (W - 1))}},
                {"type": "abs", "data": {"axis": "y", "value": int(max(0, min(H - 1, y)) * 32767 / (H - 1))}}]

    def move(self, x, y): self.cmd("input-send-event", events=self._abs(x, y))

    def btn(self, down, button="left"):
        self.cmd("input-send-event", events=[{"type": "btn", "data": {"down": down, "button": button}}])

    def click(self, x, y, button="left", hold=0.06):
        self.move(x, y); time.sleep(0.04); self.btn(True, button); time.sleep(hold); self.btn(False, button); time.sleep(0.06)

    def dblclick(self, x, y):
        self.click(x, y, hold=0.03); time.sleep(0.05); self.click(x, y, hold=0.03)

    def glide(self, x0, y0, x1, y1, ms=300):
        steps = max(6, ms // 16)
        for i in range(1, steps + 1):
            t = i / steps
            self.move(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t); time.sleep(ms / 1000 / steps)

    def drag(self, x0, y0, x1, y1, ms=400, hold_end=0.05):
        self.move(x0, y0); time.sleep(0.05); self.btn(True); time.sleep(0.05)
        self.glide(x0, y0, x1, y1, ms)
        time.sleep(hold_end); self.btn(False); time.sleep(0.08)

    def wheel(self, x, y, n):
        self.move(x, y)
        b = "wheel-up" if n > 0 else "wheel-down"
        for _ in range(abs(n)):
            self.btn(True, b); self.btn(False, b); time.sleep(0.03)

    def key(self, combo):
        keys = combo.split("-")
        ev = lambda d, ks: [{"type": "key", "data": {"down": d, "key": {"type": "qcode", "data": k}}} for k in ks]
        self.cmd("input-send-event", events=ev(True, keys)); time.sleep(0.03)
        self.cmd("input-send-event", events=ev(False, list(reversed(keys)))); time.sleep(0.03)

    def hold(self, key, down):
        self.cmd("input-send-event", events=[{"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": key}}}])

    SYM = {" ": ("spc", 0), ".": ("dot", 0), "/": ("slash", 0), ":": ("semicolon", 1), ";": ("semicolon", 0),
           "-": ("minus", 0), "=": ("equal", 0), ",": ("comma", 0), "?": ("slash", 1), "\n": ("ret", 0),
           "_": ("minus", 1), "&": ("7", 1), "'": ("apostrophe", 0), "\"": ("apostrophe", 1), "!": ("1", 1),
           "*": ("8", 1), "+": ("equal", 1), "(": ("9", 1), ")": ("0", 1), "<": ("comma", 1), ">": ("dot", 1),
           "|": ("backslash", 1), "\\": ("backslash", 0), "@": ("2", 1), "#": ("3", 1), "$": ("4", 1), "%": ("5", 1)}

    def type(self, text, delay=0.0):
        for ch in text:
            if ch in self.SYM:
                q, sh = self.SYM[ch]
                self.key(("shift-" if sh else "") + q)
            elif ch.isupper(): self.key("shift-" + ch.lower())
            else: self.key(ch)
            if delay: time.sleep(delay)

    def screenshot(self, path):
        path = os.path.abspath(path)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.cmd("screendump", filename=path, format="png")
        return path


def screenshot(path): q = QMP(); r = q.screenshot(path); q.close(); return r
def click(x, y, button="left"): q = QMP(); q.click(x, y, button); q.close()
def move(x, y): q = QMP(); q.move(x, y); q.close()
def drag(x0, y0, x1, y1, ms=400): q = QMP(); q.drag(x0, y0, x1, y1, ms); q.close()
def type(text): q = QMP(); q.type(text); q.close()
def key(combo): q = QMP(); q.key(combo); q.close()


if __name__ == "__main__":
    a = sys.argv[1:]
    q = QMP()
    if a[0] == "shot": print(q.screenshot(a[1]))
    elif a[0] == "click": q.click(float(a[1]), float(a[2]), a[3] if len(a) > 3 else "left")
    elif a[0] == "dblclick": q.dblclick(float(a[1]), float(a[2]))
    elif a[0] == "move": q.move(float(a[1]), float(a[2]))
    elif a[0] == "drag": q.drag(*map(float, a[1:5]), ms=int(a[5]) if len(a) > 5 else 400)
    elif a[0] == "type": q.type(a[1])
    elif a[0] == "key": q.key(a[1])
    elif a[0] == "wheel": q.wheel(float(a[1]), float(a[2]), int(a[3]))
    elif a[0] == "quit": q.cmd("quit")
    q.close()
