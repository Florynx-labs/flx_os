"""Shared test helpers: boot the OS headless, wait on serial markers, drive it via QMP."""
import os, re, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from qmp import QMP

SERIAL = os.path.join(ROOT, "build", "serial.log")


def serial():
    try:
        return open(SERIAL, errors="replace").read()
    except FileNotFoundError:
        return ""


def mark():
    return len(serial())


def wait(pattern, timeout=20, since=0):
    t = time.time()
    while time.time() - t < timeout:
        m = re.search(pattern, serial()[since:])
        if m:
            return m
        time.sleep(0.1)
    return None


def boot(bridge=True, display="none", disk=None):
    """Start run.sh in the background; returns (proc, qmp)."""
    subprocess.run(["pkill", "-f", "qemu-system-aarch64.*w12-qmp"], capture_output=True)
    time.sleep(0.5)
    env = dict(os.environ, W12_DISPLAY=display)
    if not bridge:
        env["W12_NO_BRIDGE"] = "1"
    if disk:
        env["W12_DISK"] = disk
    try:
        os.remove(SERIAL)
    except FileNotFoundError:
        pass
    p = subprocess.Popen([os.path.join(ROOT, "run.sh")], env=env, stdout=open(os.path.join(ROOT, "build", "run.out"), "w"), stderr=subprocess.STDOUT)
    for _ in range(600):
        if os.path.exists("/tmp/w12-qmp.sock"):
            break
        time.sleep(0.2)
    return p, QMP()


def start_items():
    """Start menu item centres announced by the compositor (W12:START_ITEM exe x y)."""
    return {m[0]: (int(m[1]), int(m[2])) for m in re.findall(r"W12:START_ITEM (\S+) (\d+) (\d+)", serial())}
