# Windows 12 — handover for recording

## Start / stop
```bash
cd ~/Documents/Windows12 && ./run.sh                 # QEMU window (cocoa), bridge started automatically
W12_DISPLAY=none ./run.sh                            # headless, drive it via QMP only
```
- QMP: `/tmp/w12-qmp.sock` (stable path). Screen is 1920x1080, absolute tablet coordinates.
- Serial log with `W12:` markers: `build/serial.log`. Bridge log: `build/bridge.log`.
- Fresh state: delete `data/disk.img` (recreated from the template on the next run; light theme, sample files).
- Stop: close the QEMU window, Ctrl-C run.sh, `python3 tools/qmp.py quit`, or Start > power > Shut down.
- Helper: `python3 tools/qmp.py shot PATH | click X Y [right] | dblclick X Y | drag X0 Y0 X1 Y1 [ms] | type TEXT | key COMBO | wheel X Y N`
  (key combos are QEMU qcodes joined by `-`, e.g. `meta_l`, `meta_l-left`, `alt-tab`, `ctrl-s`, `alt-f4`).

## Timeline after QEMU starts
- ~0.1 s boot animation (logo + dots) · ~3.0 s lock screen (`W12:READY lock`) · weather arrives ~1 s after DHCP.
- Unlock: click anywhere, Enter, or drag up. `W12:READY desktop` after the 0.42 s slide.

## Fixed screen positions (guest pixels)
- Top bar (y 0..36): Widgets/weather (100, 18) · search (960, 18) opens Start · network/volume/battery (1690, 18) opens Quick Settings · clock (1800, 18) opens Widgets · Copilot (1893, 18)
- Taskbar (y 1012..1068, centred): Start (717, 1040) · Search (769, 1040) · pinned apps from x=829 every 52 px:
  Explorer 829 · Browser 881 · Terminal 933 · Notepad 985 · Settings 1037 · Task Manager 1089 · Calculator 1141 · Minesweeper 1193 (running unpinned apps get added after, the bar re-centres)
- Start menu (x 620..1300, y 300..1000): pinned grid centres are printed as `W12:START_ITEM <exe> X Y` when it opens:
  row 1 y=471: Explorer 700 · Settings 804 · Terminal 908 · Notepad 1012 · Calculator 1116 · Browser 1220; row 2 y=567: Task Manager 700 · Minesweeper 804 · Proof 908.
  Type to search, Enter opens the first match. Power (1250, 966) → Lock / Restart / Shut down at y 817 / 857 / 897, x≈1140.
- Every window open prints `W12:WINDOW_MAPPED <exe> <id> X Y W H` (content rect; titlebar is the 40 px above Y).
  Caption buttons in the titlebar from the right edge: close (-23), maximize (-69, hover 0.45 s = snap flyout), minimize (-115).

## Keyboard
Win = Start · Win+←/→/↑/↓ snap/maximize/restore · Win+C Copilot · Win+W Widgets · Win+A Quick Settings · Win+E Explorer · Win+L lock · Alt+Tab · Alt+F4 · Esc closes panels.

## Good demo beats
- Terminal: `fetch`, `ps`, `ifconfig`, `ping -c 3 10.0.2.2`, `curl https://example.com`, `proof` (scroll back with the wheel).
- Settings > Personalization: dark/light, accents, wallpapers (crossfade); Settings > About > Open Proof.
- Snap: Win+Left on one window, Win+Right on another → snap assist shows the rest; or hover maximize for the layout flyout.
- Browser: quick links (Wikipedia mobile renders best), or type `en.m.wikipedia.org/wiki/Linux_kernel`.
- Task Manager: open Calculator, select it in Task Manager, End task → the window closes (it was a real process).
- Copilot: answers stream in 2-5 s via `claude -p` on the Mac; kill `bridge/copilot_bridge` to show the offline message.

## Markers worth waiting on
`W12:READY boot|lock|desktop`, `W12:WINDOW_OPEN/MAPPED/CLOSE <exe>`, `W12:START open`, `W12:START_ITEM`, `W12:SNAP <exe> <how>`,
`W12:SNAP_ASSIST`, `W12:ALTTAB <exe>`, `W12:THEME dark=..`, `W12:COPILOT_DONE ok|offline`, `W12:BROWSER loaded <status> <url>`,
`W12:SAVED <path>`, `W12:CALC ...`, `W12:TASKMGR kill <pid> <name>`, `W12:PROOF ...`, `W12:POWER shutdown|restart`.
