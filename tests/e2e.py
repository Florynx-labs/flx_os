#!/usr/bin/env python3
"""FLX OS end-to-end test. Boots the OS headless through run.sh on a fresh copy of the
home disk, drives it only through QMP (absolute tablet + keyboard), waits on serial markers
(W12:...), and saves a screenshot for every step to tests/out/. Exit code != 0 on failure."""
import os, re, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from w12 import ROOT, boot, mark, wait, serial, start_items

OUT = os.path.join(HERE, "out")
DISK = os.path.join(ROOT, "build", "e2e-disk.img")
results, n_shot = [], [0]
q = None


def shot(name):
    n_shot[0] += 1
    path = os.path.join(OUT, f"{n_shot[0]:02d}_{name}.png")
    q.screenshot(path)
    return path


def check(name, ok, detail=""):
    results.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + (f"  ({detail})" if detail else ""), flush=True)
    return ok


def geom(exe, since=0):
    m = wait(r"WINDOW_MAPPED %s \d+ (\d+) (\d+) (\d+) (\d+)" % exe, 8, since)
    return tuple(map(int, m.groups())) if m else None


def open_from_start(exe):
    m = mark()
    q.key("meta_l")
    if not wait(r"START_ITEM proof", 4, m):
        return None
    time.sleep(0.15)
    items = start_items()
    q.click(*items[exe])
    g = geom(exe, m)
    time.sleep(0.45)
    return g


def close_focused(exe):
    m = mark()
    q.key("alt-f4")
    return wait(r"WINDOW_CLOSE %s" % exe, 6, m)


def main():
    global q
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    subprocess.run([os.path.join(ROOT, "build.sh")], check=True)
    shutil.copy(os.path.join(ROOT, "build", "home.img"), DISK)
    proc, q = boot(bridge=True, disk=DISK)
    t0 = time.time()

    # --- boot, lock, unlock -------------------------------------------------------------
    check("boot animation", wait(r"W12:READY boot", 30))
    time.sleep(1.3); shot("boot")
    check("lock screen", wait(r"W12:READY lock", 15), f"{time.time() - t0:.1f}s after QEMU start")
    time.sleep(0.6); shot("lock")
    check("network up (own DHCP)", wait(r"W12:NET up", 10))
    m = mark(); q.click(960, 700)
    check("unlock to desktop", wait(r"W12:READY desktop", 5, m))
    time.sleep(0.4); shot("desktop")

    # --- Start menu ------------------------------------------------------------------------
    m = mark(); q.key("meta_l")
    check("Start menu opens (Win key)", wait(r"START_ITEM proof", 4, m))
    time.sleep(0.2); shot("start_menu")
    q.type("calc"); time.sleep(0.3); shot("start_search")
    q.key("esc"); time.sleep(0.3)

    # --- Notepad: type, save, unsaved prompt -----------------------------------------------
    g = open_from_start("notepad")
    check("Notepad opens", g)
    q.type("Bonjour depuis le test e2e de FLX OS!\nSauvegardé par notre propre éditeur de texte sur un disque ext2.")
    q.key("ctrl-s"); time.sleep(0.4)
    q.key("ctrl-a"); q.type("e2e-note.txt\n")
    check("Notepad saves a file", wait(r"SAVED /home/user/Documents/e2e-note.txt", 5))
    time.sleep(0.3); shot("notepad_saved")
    q.type("\nUnsaved line.")
    m = mark(); q.key("alt-f4")
    check("Notepad asks about unsaved changes", wait(r"NOTEPAD_PROMPT", 4, m))
    time.sleep(0.3); shot("notepad_unsaved_prompt")
    m = mark(); q.key("n")
    check("Notepad closes after Don't save", wait(r"WINDOW_CLOSE notepad", 4, m))

    # --- Calculator --------------------------------------------------------------------------
    g = open_from_start("calc")
    check("Calculator opens", g)
    m = mark(); q.type("12*3"); q.key("ret")
    r = wait(r"CALC (.*)", 4, m)
    check("Calculator computes 12 x 3 = 36", r and r.group(1).strip().endswith("36"), r and r.group(1))
    shot("calculator")
    check("Calculator closes", close_focused("calc"))

    # --- Minesweeper --------------------------------------------------------------------------
    g = open_from_start("minesweeper")
    check("Minesweeper opens", g)
    if g:
        x, y, w, h = g
        cs = int(min((w - 32) / 9, (h - 100) / 9)); ox = (w - cs * 9) // 2; oy = 60 + (h - 90 - cs * 9) // 2
        m = mark(); q.click(x + ox + 4 * cs + cs // 2, y + oy + 4 * cs + cs // 2)
        check("Minesweeper reveals cells", wait(r"MINES reveal 4 4 open=\d+", 4, m))
        q.click(x + ox + 8 * cs + cs // 2, y + oy + 0 * cs + cs // 2, "right"); time.sleep(0.3)
        shot("minesweeper")
    check("Minesweeper closes", close_focused("minesweeper"))

    # --- Terminal + own shell + proof ----------------------------------------------------------
    g = open_from_start("terminal")
    check("Terminal opens", g)
    m = mark(); q.type("fetch\n")
    check("shell runs commands", wait(r"SHELL fetch", 4, m))
    q.type("ls -l Documents\n"); time.sleep(0.4)
    m = mark(); q.type("ping -c 2 10.0.2.2\n"); time.sleep(2.6)
    shot("terminal_fetch_ping")
    q.type("clear\n"); m = mark(); q.type("proof\n")
    check("proof runs in Terminal", wait(r"PROOF terminal \d+ processes", 5, m))
    time.sleep(0.6); shot("terminal_proof")
    check("Terminal closes", close_focused("terminal"))

    # --- Browser (own HTML/CSS engine over our HTTPS) --------------------------------------------
    g = open_from_start("browser")
    check("Browser opens", g)
    if g:
        x, y, w, h = g
        shot("browser_start")
        m = mark(); q.click(x + 500, y + 64); time.sleep(0.2); q.type("https://example.com\n")
        check("Browser loads https://example.com", wait(r"BROWSER loaded 200 https://example.com", 20, m))
        time.sleep(0.8); shot("browser_example")
        m = mark(); q.click(x + 500, y + 64); time.sleep(0.2); q.type("en.m.wikipedia.org/wiki/Linux_kernel\n")
        check("Browser loads Wikipedia", wait(r"BROWSER loaded 200", 30, m))
        time.sleep(2.5); shot("browser_wikipedia")
    check("Browser closes", close_focused("browser"))

    # --- Task Manager: end a real process -----------------------------------------------------
    open_from_start("calc")
    g = open_from_start("taskmgr")
    check("Task Manager opens", g)
    killed = False
    if g:
        x, y, w, h = g
        time.sleep(1.2); shot("taskmgr")
        for i in range(4):
            m = mark(); q.click(x + 300, y + 158 + i * 32)
            r = wait(r"TASKMGR select (\d+) (\S+)", 2, m)
            if r and r.group(2) == "calc":
                m = mark(); q.click(x + w - 95, y + 33)
                killed = wait(r"TASKMGR kill \d+ calc", 3, m) and wait(r"WINDOW_CLOSE calc", 4, m)
                break
        time.sleep(0.5); shot("taskmgr_ended_calc")
        q.click(x + 32, y + 84); time.sleep(1.2); shot("taskmgr_performance")
    check("Task Manager ends a process (calc)", killed)
    check("Task Manager closes", close_focused("taskmgr"))

    # --- File Explorer -----------------------------------------------------------------------
    g = open_from_start("explorer")
    check("File Explorer opens", g)
    if g:
        x, y, w, h = g
        m = mark(); q.click(x + 100, y + 193)
        check("Explorer navigates to Documents", wait(r"EXPLORER cd /home/user/Documents", 3, m))
        time.sleep(0.3); shot("explorer_documents")
        m = mark(); q.click(x + 230, y + 24)
        check("Explorer creates a folder", wait(r"EXPLORER mkdir", 3, m))
        time.sleep(0.2); q.key("ctrl-a"); q.type("E2E Folder\n")
        check("Explorer renames it", wait(r"EXPLORER rename .* -> /home/user/Documents/E2E Folder", 3, m))
        time.sleep(0.3); shot("explorer_renamed")
        m = mark(); q.key("delete"); time.sleep(0.3); shot("explorer_delete_confirm"); q.key("ret")
        check("Explorer deletes it", wait(r"EXPLORER delete /home/user/Documents/E2E Folder", 3, m))
    check("File Explorer closes", close_focused("explorer"))

    # --- Settings: personalization, theme switch, about, proof ----------------------------------
    g = open_from_start("settings")
    check("Settings opens", g)
    if g:
        x, y, w, h = g
        shot("settings_system")
        q.click(x + 150, y + 171); time.sleep(0.4); shot("settings_personalization")
        m = mark(); q.click(x + 310 + 236 + 110, y + 100 + 34 + 75)
        check("theme switches to dark (live, every window)", wait(r"THEME dark=1", 3, m))
        time.sleep(0.5); shot("settings_dark_theme")
        q.click(x + 150, y + 110 + 2 * 42 + 19); time.sleep(0.5); shot("settings_network")
        q.click(x + 150, y + 110 + 4 * 42 + 19); time.sleep(0.4); shot("settings_about")
        m = mark(); q.click(x + w - 40 - 87, y + 100 + 186 + 236 + 60)
        g2 = geom("proof", m)
        check("About > Proof opens the proof window", g2)
        time.sleep(0.6); shot("proof_window")
        check("Proof closes", close_focused("proof"))
    check("Settings closes", close_focused("settings"))

    # --- Widgets ------------------------------------------------------------------------------
    m = mark(); q.click(100, 18)
    check("Widgets board opens", wait(r"WIDGETS open", 3, m))
    time.sleep(0.5); shot("widgets")
    q.key("esc"); time.sleep(0.4)

    # --- Snap layouts ---------------------------------------------------------------------------
    gn = open_from_start("notepad")
    m = mark(); q.key("meta_l-left")
    check("Win+Left snaps a window to the left half", wait(r"SNAP notepad left", 3, m))
    gt = open_from_start("terminal")
    m = mark(); q.key("meta_l-right")
    check("Win+Right snaps a window to the right half", wait(r"SNAP terminal right", 3, m))
    check("snap assist offers the other windows", wait(r"SNAP_ASSIST \d+", 3, m))
    time.sleep(0.4); shot("snap_assist")
    q.key("esc"); time.sleep(0.4); shot("snap_side_by_side")
    ge = open_from_start("explorer")
    if ge:
        x, y, w, h = ge
        m = mark(); q.move(x + w - 69, y - 20); time.sleep(0.2); q.move(x + w - 68, y - 21)
        ok = wait(r"SNAP_FLYOUT explorer", 3, m)
        check("hovering maximize shows the snap layout flyout", ok)
        time.sleep(0.3); shot("snap_flyout")
        if ok:
            fx = x + w - 92 - 120
            q.move(fx + 12 + 3 * 72 + 48, y + 14 + 33); time.sleep(0.15)
            m = mark(); q.click(fx + 12 + 3 * 72 + 48, y + 14 + 33)
            check("flyout zone snaps the window (quarters)", wait(r"SNAP explorer layout3.zone3", 3, m))
            time.sleep(0.4); shot("snap_quarter")

    # --- Alt+Tab --------------------------------------------------------------------------------
    q.hold("alt", True); time.sleep(0.05); q.key("tab"); time.sleep(0.4)
    shot("alt_tab")
    m = mark(); q.hold("alt", False)
    check("Alt+Tab switches windows", wait(r"ALTTAB \S+", 3, m))
    time.sleep(0.3)
    for exe in ("explorer", "terminal", "notepad"):
        m = mark(); q.key("alt-f4"); wait(r"WINDOW_CLOSE", 3, m)
        if wait(r"NOTEPAD_PROMPT", 0.5, m): q.key("n")
    time.sleep(0.4)

    # --- Copilot: bridge up, then bridge down ---------------------------------------------------
    m = mark(); q.click(1893, 18); time.sleep(0.5)
    q.type("En une phrase, qu'est-ce que FLX OS?\n")
    r = wait(r"COPILOT_DONE (\S+)", 90, m)
    check("Copilot answers through the bridge (real claude -p)", r and r.group(1) == "ok", r and r.group(0))
    time.sleep(0.4); shot("copilot_answer")
    subprocess.run(["pkill", "-f", "bridge/copilot_bridge"])
    time.sleep(0.5)
    m = mark(); q.type("Are you there?\n")
    r = wait(r"COPILOT_DONE (\S+)", 20, m)
    check("Copilot shows the offline message when the bridge is down", r and r.group(1) == "offline", r and r.group(0))
    time.sleep(0.4); shot("copilot_offline")
    q.key("esc"); time.sleep(0.3)

    # --- lock + shutdown --------------------------------------------------------------------------
    m = mark(); q.key("meta_l-l")
    check("Win+L locks", wait(r"W12:READY lock", 3, m))
    time.sleep(0.5); shot("locked_again")
    q.key("ret"); wait(r"W12:READY desktop", 3, m); time.sleep(0.3)
    m = mark(); q.key("meta_l"); wait(r"START_ITEM proof", 3, m); time.sleep(0.2)
    q.click((1920 - 680) // 2 + 680 - 50, 1012 - 12 - 68 + 34); time.sleep(0.3)
    shot("power_menu")
    m = mark(); q.click((1920 - 680) // 2 + 680 - 220 + 60, 1012 - 12 - 68 - 140 + 6 + 2 * 40 + 19)
    check("Shut down from Start", wait(r"W12:POWER shutdown", 5, m))
    try:
        proc.wait(timeout=15)
        check("QEMU powered off", True)
    except subprocess.TimeoutExpired:
        check("QEMU powered off", False)
        subprocess.run(["pkill", "-f", "qemu.*flx-qmp"])

    errors = re.findall(r"W12:ERROR.*", serial())
    check("no W12:ERROR markers in the serial log", not errors, "; ".join(errors[:3]))
    passed = sum(ok for _, ok in results)
    summary = f"{passed}/{len(results)} checks passed in {time.time() - t0:.0f}s, {n_shot[0]} screenshots in tests/out/"
    print(("PASS: " if passed == len(results) else "FAIL: ") + summary)
    with open(os.path.join(OUT, "REPORT.txt"), "w") as f:
        for name, ok in results: f.write(("PASS " if ok else "FAIL ") + name + "\n")
        f.write(summary + "\n")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
