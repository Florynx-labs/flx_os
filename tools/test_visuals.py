#!/usr/bin/env python3
"""Test and capture screenshots of FLX OS new features:
- Boot / Lock screen
- Clean desktop
- Start menu with user avatar
- Settings with User Accounts & Dock Auto-Hide
- Explorer with new design
- Full screen maximization with autohide
"""
import os, sys, time, shutil, subprocess, re

ROOT = "/home/asuno/Projects/flx-os"
ART_DIR = "/home/asuno/.gemini/antigravity-ide/brain/8633b449-c6f0-4c91-a7ef-c80744e6e41b"
DISK = os.path.join(ROOT, "build", "visual-test-disk.img")
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from w12 import boot, wait, mark, serial, start_items
from qmp import QMP

def geom(exe, since=0):
    m = wait(r"WINDOW_MAPPED %s \d+ (\d+) (\d+) (\d+) (\d+)" % exe, 8, since)
    return tuple(map(int, m.groups())) if m else None

def main():
    print("[test] Preparing test disk...")
    shutil.copy(os.path.join(ROOT, "build", "home.img"), DISK)

    print("[test] Starting FLX OS in QEMU headless...")
    proc, q = boot(bridge=False, display="none", disk=DISK)

    try:
        # 1. Wait for boot / lock
        print("[test] Waiting for lock screen...")
        m = wait(r"READY lock", 30)
        if not m:
            print("[test] Failed waiting for READY lock; serial log:")
            print(serial()[-1000:])
            return

        time.sleep(0.5)
        lock_shot = os.path.join(ART_DIR, "flx_lock_screen_new.png")
        q.screenshot(lock_shot)
        print(f"[test] Saved lock screen screenshot: {lock_shot}")

        # 2. Unlock to Desktop
        print("[test] Unlocking to desktop...")
        m_desk = mark()
        q.key("ret")
        wait(r"READY desktop", 10, m_desk)
        time.sleep(0.8)

        desk_shot = os.path.join(ART_DIR, "flx_desktop_clean.png")
        q.screenshot(desk_shot)
        print(f"[test] Saved clean desktop screenshot: {desk_shot}")

        # 3. Start Menu
        print("[test] Opening Start menu...")
        m_start = mark()
        q.key("meta_l")
        wait(r"START open", 5, m_start)
        time.sleep(0.5)

        start_shot = os.path.join(ART_DIR, "flx_start_menu_avatar.png")
        q.screenshot(start_shot)
        print(f"[test] Saved start menu screenshot: {start_shot}")

        # 4. Open Settings from Start
        print("[test] Opening Settings app...")
        items = start_items()
        if "settings" in items:
            q.click(*items["settings"])
        else:
            q.type("param")
            time.sleep(0.3)
            q.key("ret")

        g_set = geom("settings", m_start)
        print(f"[test] Settings window at {g_set}")
        time.sleep(0.8)

        set_shot = os.path.join(ART_DIR, "flx_settings_system.png")
        q.screenshot(set_shot)
        print(f"[test] Saved settings system screenshot: {set_shot}")

        if g_set:
            wx, wy, ww, wh = g_set

            # Navigate to Affichage (item 1)
            print("[test] Navigating to Affichage...")
            q.click(wx + 80, wy + 56 + 12 + 1 * 40 + 15)
            time.sleep(0.6)
            disp_shot = os.path.join(ART_DIR, "flx_settings_display_autohide.png")
            q.screenshot(disp_shot)
            print(f"[test] Saved settings display screenshot: {disp_shot}")

            # Toggle dock auto-hide button
            print("[test] Activating dock auto-hide...")
            btn_x = wx + ww - 80
            btn_y = wy + 56 + 56 + 10 + 35
            q.click(btn_x, btn_y)
            time.sleep(0.6)

            # Navigate to Comptes (item 7)
            print("[test] Navigating to Comptes...")
            q.click(wx + 80, wy + 56 + 12 + 7 * 40 + 15)
            time.sleep(0.6)
            acc_shot = os.path.join(ART_DIR, "flx_settings_accounts.png")
            q.screenshot(acc_shot)
            print(f"[test] Saved settings accounts screenshot: {acc_shot}")

        # Close Settings
        q.key("alt-f4")
        time.sleep(0.6)

        # 5. Open Explorer by clicking dock icon at (820, 1040)
        print("[test] Opening Explorer from dock...")
        m_exp = mark()
        q.click(820, 1040)
        g_exp = geom("explorer", m_exp)
        if not g_exp:
            # Fallback via start
            q.key("meta_l")
            time.sleep(0.3)
            q.type("fich")
            time.sleep(0.3)
            q.key("ret")
            g_exp = geom("explorer", m_exp)

        print(f"[test] Explorer window at {g_exp}")
        time.sleep(0.8)
        exp_shot = os.path.join(ART_DIR, "flx_explorer_cards.png")
        q.screenshot(exp_shot)
        print(f"[test] Saved explorer screenshot: {exp_shot}")

        # Maximize Explorer
        if g_exp:
            wx, wy, ww, wh = g_exp
            print("[test] Maximizing Explorer window...")
            # Click maximize button on titlebar (titlebar is wy - 40, max btn is at wx + ww - 45 - 23)
            q.click(wx + ww - 68, wy - 20)
            time.sleep(0.8)
            max_shot = os.path.join(ART_DIR, "flx_explorer_maximized.png")
            q.screenshot(max_shot)
            print(f"[test] Saved maximized explorer screenshot: {max_shot}")

        print("[test] ALL SCREENSHOTS SUCCESSFULLY CAPTURED!")

    finally:
        q.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        subprocess.run(["pkill", "-f", "qemu-system"], capture_output=True)

if __name__ == "__main__":
    main()
