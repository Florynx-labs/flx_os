#!/usr/bin/env python3
"""
FLX OS — Asset Preparation Pipeline
Converts source images from assets/ into rootfs-ready formats:
  - Wallpapers  → build/wallpapers/*.raw   (FLXW magic + ARGB32 pixels, 1920×1080)
  - Lock screen → build/wallpapers/lock.raw
  - App icons   → build/icons/*.png        (128×128 RGBA PNG)
  - Lucide SVGs → build/icons/lucide/<name>-<size>.png (white RGBA masks)

Called from docker/build-inside.sh (inside the Alpine container).
Run standalone on the host for quick verification.
"""
import os, sys, struct
from pathlib import Path

SCR_W, SCR_H = 1920, 1080
ICON_SIZE = 128
SRC = Path(__file__).parent.parent  # project root
ASSETS = SRC / "assets"
OUT_ROOT = Path(os.environ.get("FLX_ASSET_OUT", SRC / "build"))
OUT_WALL = OUT_ROOT / "wallpapers"
OUT_ICONS = OUT_ROOT / "icons"
LUCIDE_SRC = SRC / "third_party" / "icons" / "lucide" / "svg"
LUCIDE_OUT = OUT_ICONS / "lucide"
ICON_SIZES = (16, 20, 24, 32, 48, 64)
LUCIDE_NAMES = (
    "search wifi wifi-off volume-2 battery power x minimize maximize copy "
    "chevron-right chevron-left chevron-down chevron-up arrow-left arrow-right "
    "arrow-up rotate-cw plus trash-2 pencil folder file settings user-round "
    "panels-top-left house list grid-2x2 sun moon cloud cloud-rain star send flag "
    "clock lock-keyhole info palette network layout-grid monitor shield-check "
    "check ellipsis clipboard save bluetooth cpu memory-stick hard-drive refresh-cw "
    "calculator chart-no-axes-column-increasing bomb globe file-pen-line terminal "
    "image music video archive file-code-2 file-text file-json braces box"
).split()
OUT_WALL.mkdir(parents=True, exist_ok=True)
OUT_ICONS.mkdir(parents=True, exist_ok=True)
LUCIDE_OUT.mkdir(parents=True, exist_ok=True)

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False
    print("[assets] Pillow not available — skipping PNG conversion; raw fallback only.", flush=True)

try:
    import cairosvg
    HAS_CAIROSVG = True
except ImportError:
    HAS_CAIROSVG = False


def argb32(r, g, b, a=255):
    return (a << 24) | (r << 16) | (g << 8) | b


def save_raw(img, out_path, source_path=None):
    """Save a PIL image as FLXW raw file (magic + width + height + ARGB32 pixels)."""
    if source_path and out_path.exists() and out_path.stat().st_mtime_ns >= source_path.stat().st_mtime_ns:
        print(f"[assets] cached wallpaper → {out_path.name}", flush=True)
        return
    img = img.convert("RGBA").resize((SCR_W, SCR_H), Image.LANCZOS)
    data = bytearray()
    data += struct.pack("<III", 0x574C5846, SCR_W, SCR_H)  # "FLXW" little-endian
    for r, g, b, a in img.getdata():
        data += struct.pack("<I", argb32(r, g, b, a))
    out_path.write_bytes(bytes(data))
    print(f"[assets] wallpaper  → {out_path.name} ({SCR_W}×{SCR_H})", flush=True)


def save_icon(img, out_path, size=ICON_SIZE, source_path=None):
    """Save a PIL image as PNG icon (default 128×128, or custom size)."""
    if source_path and out_path.exists() and out_path.stat().st_mtime_ns >= source_path.stat().st_mtime_ns:
        print(f"[assets] cached icon → {out_path.name}", flush=True)
        return
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

# Full FLX wallpaper pack. Selection IDs are kept in sync with WALL_NAMES in
# src/lib/icons.c; the same six designs have separate light and dark renders.
PACK_WALLPAPERS = {
    **{f"dark_{name.lower()}": f"FLX_{name}_Dark_1920x1080.png" for name in ("Aurora", "Eclipse", "Flow", "Glass", "Horizon")},
    "dark_obsidian": "FLX_Obsidian_Dark_source.png",
    **{f"light_{name.lower()}": f"FLX_{name}_Light_1920x1080.png" for name in ("Aurora", "Eclipse", "Flow", "Glass", "Horizon")},
    "light_obsidian": "FLX_Obsidian_Light_1920x1080.png",
    "world_alpine": "FLX_Worlds_Alpine_1920x1080.png",
    "world_coast": "FLX_Worlds_Coast_1920x1080.png",
    "world_sahara": "FLX_Worlds_Sahara_1920x1080.png",
}

if HAS_PIL:
    pack = ASSETS / "FLX_OS_Wallpaper_Pack"
    for slug, filename in PACK_WALLPAPERS.items():
        src = next(pack.rglob(filename), None)
        if src:
            save_raw(Image.open(src), OUT_WALL / f"{slug}.raw", src)
        else:
            print(f"[assets] MISSING wallpaper source: {filename}", flush=True)
    for slug, src_name in WALLPAPERS.items():
        src = ASSETS / src_name
        if not src.exists():
            candidate = next((p for p in (ASSETS / "FLX_OS_Wallpaper_Pack").rglob(WALLPAPER_FALLBACKS[slug])), None)
            if candidate:
                src = candidate
        if not src.exists():
            print(f"[assets] MISSING wallpaper source: {src_name} → skipping {slug}.raw", flush=True)
            continue
        save_raw(Image.open(src), OUT_WALL / f"{slug}.raw", src)
else:
    print("[assets] No PIL — wallpapers will be generated procedurally by gen_wallpaper", flush=True)

# ---------------------------------------------------------------------------
# Brand assets retained as raster images; application and system UI glyphs use Lucide.
# ---------------------------------------------------------------------------
ICON_MAP = {
    "flx_os_logo":   "flx-os-logo.png",
    "flx_logo_mark": "flx_logo_pour_le_ctart_et_lock_screen.png",
    "user_avatar":   "flx_user_pfp.png",
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
        save_icon(Image.open(src), OUT_ICONS / f"{slug}.png", size=sz, source_path=src)

    # Clock flyout card landscape image (320×140)
    card_src = ASSETS / "widget_landscape.png"
    if not card_src.exists():
        card_src = ASSETS / "lock_screen.png"
    if card_src.exists():
        card_out = OUT_ICONS / "card_landscape.png"
        if not card_out.exists() or card_out.stat().st_mtime_ns < card_src.stat().st_mtime_ns:
            im_card = Image.open(card_src).convert("RGBA").resize((320, 140), Image.LANCZOS)
            im_card.save(str(card_out))
            print("[assets] card landscape → card_landscape.png (320×140)", flush=True)

    # Lucide assets are monochrome masks: all visible pixels are white and their
    # alpha comes from SVG coverage. libflxui can therefore apply theme colors.
    if LUCIDE_SRC.is_dir():
        if not HAS_CAIROSVG:
            raise SystemExit("[assets] Lucide SVGs found but CairoSVG is unavailable")
        missing = [name for name in LUCIDE_NAMES if not (LUCIDE_SRC / f"{name}.svg").is_file()]
        if missing:
            raise SystemExit(f"[assets] Missing vendored Lucide SVGs: {', '.join(missing)}")
        for name in LUCIDE_NAMES:
            svg = LUCIDE_SRC / f"{name}.svg"
            for size in ICON_SIZES:
                png = LUCIDE_OUT / f"{svg.stem}-{size}.png"
                if png.exists() and png.stat().st_mtime_ns >= svg.stat().st_mtime_ns:
                    continue
                rendered = cairosvg.svg2png(url=str(svg), output_width=size, output_height=size)
                from io import BytesIO
                im = Image.open(BytesIO(rendered)).convert("RGBA")
                # Normalize source color while retaining rasterized alpha.
                alpha = im.getchannel("A")
                mask = Image.new("RGBA", (size, size), (255, 255, 255, 0))
                mask.putalpha(alpha)
                mask.save(str(png), optimize=True)
        print(f"[assets] Lucide SVGs → {len(LUCIDE_NAMES)} names × {len(ICON_SIZES)} sizes", flush=True)
else:
    print("[assets] No PIL — icons will use procedurally drawn fallbacks in icons.c", flush=True)

print(f"[assets] Done. wallpapers={len(list(OUT_WALL.glob('*.raw')))} icons={len(list(OUT_ICONS.glob('*.png')))}", flush=True)
