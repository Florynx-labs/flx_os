#!/usr/bin/env python3
"""
FLX OS — Asset Preparation Pipeline
Converts source images from assets/ into rootfs-ready formats:
  - Wallpapers  → build/wallpapers/*.raw   (FLXW magic + ARGB32 pixels, 1920×1080)
  - Lock screen → build/wallpapers/lock.raw
  - App icons   → build/icons/*.png        (128×128 RGBA PNG)

Called from docker/build-inside.sh (inside the Alpine container).
Run standalone on the host for quick verification.
"""
import os, sys, struct
from pathlib import Path

SCR_W, SCR_H = 1920, 1080
ICON_SIZE = 128
SRC = Path(__file__).parent.parent  # project root
ASSETS = SRC / "assets"
OUT_WALL = SRC / "build" / "wallpapers"
OUT_ICONS = SRC / "build" / "icons"
OUT_WALL.mkdir(parents=True, exist_ok=True)
OUT_ICONS.mkdir(parents=True, exist_ok=True)

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False
    print("[assets] Pillow not available — skipping PNG conversion; raw fallback only.", flush=True)


def argb32(r, g, b, a=255):
    return (a << 24) | (r << 16) | (g << 8) | b


def save_raw(img, out_path):
    """Save a PIL image as FLXW raw file (magic + width + height + ARGB32 pixels)."""
    img = img.convert("RGBA").resize((SCR_W, SCR_H), Image.LANCZOS)
    data = bytearray()
    data += struct.pack("<III", 0x574C5846, SCR_W, SCR_H)  # "FLXW" little-endian
    for r, g, b, a in img.getdata():
        data += struct.pack("<I", argb32(r, g, b, a))
    out_path.write_bytes(bytes(data))
    print(f"[assets] wallpaper  → {out_path.name} ({SCR_W}×{SCR_H})", flush=True)


def save_icon(img, out_path, size=ICON_SIZE):
    """Save a PIL image as PNG icon (default 128×128, or custom size)."""
    img = img.convert("RGBA").resize((size, size), Image.LANCZOS)
    img.save(str(out_path))
    print(f"[assets] icon       → {out_path.name} ({size}×{size})", flush=True)


# ---------------------------------------------------------------------------
# Wallpaper mapping: slug → source filename in assets/
# If a source file is missing, a gradient fallback is generated in gen_wallpaper.c
# ---------------------------------------------------------------------------
WALLPAPERS = {
    # shell.c loads "bloom", "aurora", "dune" — map them to available assets
    "bloom":  "lock_screen.png",                     # vibrant sunrise landscape → wallpaper 0
    "aurora": "flx_dark_font.png",                   # dark dusk city landscape → wallpaper 1
    "dune":   "flx_font.png",                        # warm mountain landscape → wallpaper 2
    "lock":   "flx_ft.png",                          # high-detail dusk landscape for lock screen
}

# Prefer the supplied scenic FLX wallpaper pack when the older loose assets have
# been removed. These files are already 1920x1080 and preserve the widescreen crop.
WALLPAPER_FALLBACKS = {
    "bloom": "FLX_Worlds_Coast_1920x1080.png",
    "aurora": "FLX_Aurora_Dark_1920x1080.png",
    "dune": "FLX_Worlds_Sahara_1920x1080.png",
    "lock": "FLX_Worlds_Coast_1920x1080.png",
}

if HAS_PIL:
    for slug, src_name in WALLPAPERS.items():
        src = ASSETS / src_name
        if not src.exists():
            candidate = next((p for p in (ASSETS / "FLX_OS_Wallpaper_Pack").rglob(WALLPAPER_FALLBACKS[slug])), None)
            if candidate:
                src = candidate
        if not src.exists():
            print(f"[assets] MISSING wallpaper source: {src_name} → skipping {slug}.raw", flush=True)
            continue
        save_raw(Image.open(src), OUT_WALL / f"{slug}.raw")
else:
    print("[assets] No PIL — wallpapers will be generated procedurally by gen_wallpaper", flush=True)

# ---------------------------------------------------------------------------
# Icon mapping: filename stem → source icon in assets/icones/ or assets/
# ---------------------------------------------------------------------------
ICON_MAP = {
    # Core apps (match APPS[].exe names in icons.c)
    "explorer":    "file-explorer.png",
    "settings":    "settings.png",
    "terminal":    "terminal.png",
    "notepad":     "note_app_icone.png",
    "calc":        "calculator_icones.png",
    "browser":     "flw-browser.png",
    "taskmgr":     "task-manager.png",
    "minesweeper": "applications-grid.png",
    "proof":       "system-info.png",
    "store":       "flx-store.png",
    # Extra apps & system icons from assets/icones/
    "media":       "media_palyer_icone.png",
    "weather":     "meteo_icone.png",
    "gallery":     "galerie_images_icone.png",
    "clock":       "clock.png",
    "calendar":    "calandar.png",
    "trash":       "trash.png",
    "power":       "power.png",
    "wifi":        "wifi.png",
    "battery":     "battery.png",
    "notifications": "notifications.png",
    "app_launcher":  "app-launcher.png",
    "flx_os_logo":   "flx-os-logo.png",
    "browser_lg":    "brower_icone.png",
    # Official FLX OS branding and user profile (from assets/ root)
    "flx_logo_mark":        "flx_logo_pour_le_ctart_et_lock_screen.png",
    "flx_logo":             "flx_logo.png",
    "flx_logo_transparent": "flx_logo_transparent.png",
    "user_avatar":          "flx_user_pfp.png",
}

if HAS_PIL:
    ICONS_SRC = ASSETS / "icones"
    for slug, src_name in ICON_MAP.items():
        src = ICONS_SRC / src_name
        # Fallback to assets root for logo/avatar assets
        if not src.exists():
            src = ASSETS / src_name
        if not src.exists():
            print(f"[assets] MISSING icon source: {src_name} → skipping {slug}.png", flush=True)
            continue
        sz = 256 if slug in ("flx_logo_mark", "user_avatar") else ICON_SIZE
        save_icon(Image.open(src), OUT_ICONS / f"{slug}.png", size=sz)

    # Clock flyout card landscape image (320×140)
    card_src = ASSETS / "widget_landscape.png"
    if not card_src.exists():
        card_src = ASSETS / "lock_screen.png"
    if card_src.exists():
        im_card = Image.open(card_src).convert("RGBA").resize((320, 140), Image.LANCZOS)
        im_card.save(str(OUT_ICONS / "card_landscape.png"))
        print("[assets] card landscape → card_landscape.png (320×140)", flush=True)
else:
    print("[assets] No PIL — icons will use procedurally drawn fallbacks in icons.c", flush=True)

print(f"[assets] Done. wallpapers={len(list(OUT_WALL.glob('*.raw')))} icons={len(list(OUT_ICONS.glob('*.png')))}", flush=True)
