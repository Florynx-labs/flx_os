#!/bin/sh
# Scans a staged rootfs for third-party UI stacks / inits / shells. Exit 1 on any hit.
# Checks file names and strings inside every binary.
R="$1"
FAIL=0
NAMES='x11|xorg|wayland|weston|qt[0-9]|libqt|gtk|gdk|sdl2?|cairo|skia|freetype|lvgl|imgui|webkit|blink|chrom|gecko|servo|electron|busybox|systemd|toybox'
hits=$(cd "$R" && find . | grep -Ei "($NAMES)" | grep -v '^\./share/proof' || true)
if [ -n "$hits" ]; then echo "FAIL file names: $hits"; FAIL=1; fi
PAT='libX11|XOpenDisplay|wl_display_connect|libwayland|QApplication|libQt|gtk_init|libgtk|SDL_Init|libSDL|cairo_create|FT_Init_FreeType|SkCanvas|lv_init|ImGui::|WebKit|Chromium|Gecko/|BusyBox v|systemd|Electron/'
N=0
for f in $(cd "$R" && find . -type f | grep -v '^\./share/proof'); do
  N=$((N + 1))
  s=$(strings -n 5 "$R/$f" | grep -E "$PAT" | head -3)
  if [ -n "$s" ]; then echo "FAIL $f: $s"; FAIL=1; fi
done
if [ $FAIL = 0 ]; then
  echo "PASS $(date '+%Y-%m-%d %H:%M') scanned $N files (names + strings)"
  echo "checked: X11, Wayland, Qt, GTK, SDL, Cairo, Skia, FreeType, LVGL, ImGui, WebKit/Blink, Chromium, Gecko, Electron, busybox, systemd"
fi
exit $FAIL
