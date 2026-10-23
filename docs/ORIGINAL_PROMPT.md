# Build "Windows 12" — a real, bootable fan OS

You are building a complete desktop OS called **Windows 12** (unofficial, non-commercial fan project for a YouTube video). It must boot in QEMU on this machine (M4 MacBook Air, macOS, aarch64, `-accel hvf`). You have ~3–4 hours. Work autonomously: don't ask me questions, make reasonable decisions, log them, and keep going.

The whole point of the video: viewers say "AI OSes are just a Linux skin or a web page." This one must be provably neither. The Linux kernel is allowed. **Everything above the kernel is written by you in this repo.**

---

## 1. Hard rules

- **Allowed below/beside your code:** Linux kernel (arm64), musl libc (static linking), stb_truetype, stb_image, BearSSL. Nothing else third-party in the guest.
- **Forbidden in the guest:** X11, Wayland, Qt, GTK, SDL, Cairo, Skia, FreeType, LVGL, Dear ImGui, any browser engine (WebKit/Blink/Gecko/Servo), Electron, any HTML/JS UI, busybox/systemd/any third-party init or shell in the release image (busybox only in a separate debug build if you need it).
- Your own PID 1 init. Your own compositor, window manager, widget toolkit, shell, and apps. All C + musl.
- Draw directly to **DRM/KMS dumb buffers** (`/dev/dri/card0`, virtio-gpu). Input from **evdev**.
- Resolution **1920×1080**.
- **Branding:** name "Windows 12" everywhere. **Original logo** drawn in code (do NOT reproduce Microsoft's four-pane window logo; design your own simple mark, e.g. a single glass pane in perspective with a soft gradient). **Selawik** font (OFL, github.com/microsoft/Selawik). All icons original, drawn in code. Wallpaper: original, procedurally generated at build time. About screen must say: **"Unofficial fan project, not affiliated with Microsoft."**
- Never leave `main` broken. If something blocks you for >20 min, simplify, log it in PROGRESS.log, move on.

## 2. Reuse Lumen OS 2 (my own MIT code)

`~/Documents/PhoneOS2` is a phone OS you built earlier: C + musl, DRM/KMS dumb buffers, evdev, stb_truetype, BearSSL, own DHCP/DNS/HTTP/TLS, a tiny HTML browser, and a Mac-side bridge (YouTube) reached from the guest at 10.0.2.2.

**First step:** read it thoroughly. Then reuse/port (don't rewrite):
- kernel build + config, initramfs/rootfs build, cross toolchain setup, QEMU flags that already work on this Mac with hvf
- DRM/KMS + evdev + font rendering code
- the full net stack (DHCP/DNS/HTTP/TLS) and the HTML browser
- the bridge pattern (Mac-side script, guest talks plain HTTP to 10.0.2.2)

Copy code into the new repo; keep MIT license headers and add attribution to Lumen OS 2 in README and About.

## 3. Repo & logging

- New repo: `~/Documents/Windows12`, git initialized.
- **Git commit at every milestone** (message: `M<n>: <name>`), plus smaller commits in between.
- **`PROGRESS.log`**: append one line per meaningful step, format `HH:MM <what happened>` (local time, from `date +%H:%M`). Log decisions, problems, workarounds. I use this for a time-lapse, so log often (every 5–15 min of work).
- **Screenshots per milestone**: `screenshots/M<n>-<name>.png` via QMP (see §8). Several per milestone is fine.
- `PLAN.md` at the start: architecture + milestone checklist, updated as you go.

## 4. Run & boot

- `./build.sh` → builds everything (kernel if needed, userland, rootfs image).
- `./run.sh` → builds if needed, starts the Copilot bridge on the Mac (§7), starts QEMU, stops the bridge on exit.
- QEMU: `-machine virt -accel hvf -cpu host`, virtio-gpu-pci at 1920×1080, virtio-keyboard + **virtio-tablet** (absolute pointer, needed for QMP clicks), virtio-net (user mode, so 10.0.2.2 = Mac), virtio-blk disk with a persistent writable filesystem (for files the user saves), serial console to a log file, **QMP socket** at `/tmp/w12-qmp.sock`. Start from Lumen's working flags.
- Boot flow: kernel → own init → **boot animation** (logo + Windows-style loading dots) → **lock screen** (big clock, date, wallpaper with blur; click/Enter/swipe up to unlock, no password) → desktop.
- Target: lock screen visible within a few seconds of kernel start.

## 5. Architecture (suggested, adjust if Lumen suggests better)

- `w12-compositor`: single process owning DRM + evdev. Composites windows, draws the shell (taskbar, top bar, Start, panels), handles focus, drag, resize, snap, animations, software cursor (or DRM cursor plane).
- **Apps are separate processes.** Minimal own window protocol: Unix socket for messages + `memfd` shared ARGB buffers. This makes Task Manager show real processes, which matters for the proof.
- Own small UI toolkit lib (`libw12ui`): text (stb_truetype + Selawik, glyph cache), buttons, lists, text fields, scroll views, menus, icons, rounded rects, shadows, layout helpers.
- Rendering is software. Performance tricks: dirty rectangles, page flip or damage-only copy, **pre-blurred wallpaper cached** for glass surfaces over the desktop, cheap box-blur approximations for glass over windows, NEON where it helps. Cursor movement must feel smooth (target 60 fps).

## 6. Look & feel ("leaked Windows 12" style, Fluent)

- **Floating centered taskbar**: pill-shaped, detached from the screen bottom, glass (blur + tint + subtle border + shadow). Start button, search, pinned apps, running app indicators, hover/press animations.
- **Top status bar**: left: Widgets button + weather; center: search; right: Wi-Fi/network state (real), volume icon, battery icon, date/time, Copilot button. Glass.
- **Start menu**: centered, glass, search field that filters apps, pinned grid, "Recommended" list (recent files), user + power (restart/shutdown actually work via reboot syscalls).
- **Copilot panel**: slides in from the right, glass, chat bubbles, streaming text (§7).
- **Widgets board**: slides in from the left: clock, calendar, weather (real, via HTTPS e.g. open-meteo, with cached fallback), system stats (CPU/RAM from /proc), a notes widget, a "photo" widget with procedural art.
- **Windows**: rounded corners, soft shadows, Mica-like tinted titlebars, min/max/close, drag, resize from edges, double-click titlebar to maximize, focus animations, open/close/minimize animations.
- **Snap layouts**: hovering the maximize button shows the layout flyout (halves, thirds, quarters, 2/3+1/3…), plus Win+Arrow keys and drag-to-edge snapping with preview.
- **Alt+Tab** switcher, Win key opens Start.
- Light/dark theme + accent color, switchable in Settings, applied live everywhere.

## 7. Copilot (real AI via Mac bridge)

- **No API key in the image.** `run.sh` starts `bridge/copilot_bridge` on the Mac (same language/pattern as Lumen's YouTube bridge), bound to `127.0.0.1:<port>` only; the guest reaches it at `10.0.2.2:<port>`.
- Endpoint: `POST /ask` with JSON `{"prompt": "...", "history": [...]}`. Bridge pipes it into **`claude -p`** on the Mac (works headless here) and **streams** the answer back (chunked transfer or SSE). Check `claude --help` for the right streaming output flags.
- Safety: pass the prompt via stdin/argv, never through a shell string. **Disable all tools** for this `claude -p` call (find the right flag in `claude --help`) and run it in an empty temp dir, so the panel can only chat. Add a short system prompt: "You are Copilot inside Windows 12, an unofficial fan OS. Answer briefly in plain text." Timeout + max length.
- Guest side: your own HTTP client + your own JSON parser (no libs), streaming tokens into the panel as they arrive, history kept for the session.
- If the bridge is unreachable: friendly offline message in the panel ("Copilot is offline. The bridge on the host isn't running."), no hang, no crash.

## 8. Apps (all separate processes)

1. **File Explorer**: real filesystem, navigation pane, breadcrumb, list/grid view, open files in Notepad, create folder, rename, delete.
2. **Settings**: System (display info, about), Personalization (theme, accent, wallpaper variants), Network (real IP/DNS/gateway from your stack), Apps list. **About**: version, kernel version, CPU/RAM, logo, disclaimer, Lumen attribution, **Proof** button (§10).
3. **Terminal**: own terminal emulator + own shell. Commands at least: `ls cd pwd cat echo mkdir rm touch ps kill uname free df ifconfig ping curl clear help proof`. `curl` uses your HTTP/TLS stack.
4. **Notepad**: open/save, cursor, selection, copy/paste (own clipboard via compositor), word wrap, unsaved-changes prompt.
5. **Calculator**: standard mode, keyboard input.
6. **Browser**: ported Lumen HTML browser in a window, address bar, back/forward/reload, HTTPS works.
7. **Task Manager**: real processes from /proc, CPU/RAM per process, live graphs, End task actually kills.
8. **Minesweeper**: beginner/intermediate/expert, flags (right-click), timer, original art.
9. Plus the shell surfaces: lock screen, boot animation, Start, Widgets, Copilot, snap layouts.

## 9. Agent testability (you test yourself continuously)

- `tools/qmp.py` (Python on the Mac, stdlib only): `screenshot(path)` (QMP `screendump`, PNG), `click(x,y,button)`, `move(x,y)`, `drag(...)`, `type(text)`, `key(combo)` via `input-send-event` with absolute axes.
- **Serial markers**: compositor and apps print machine-readable lines to the serial log, e.g. `W12:READY lock`, `W12:READY desktop`, `W12:WINDOW_OPEN notepad <id>`, `W12:WINDOW_CLOSE ...`, `W12:COPILOT_DONE`, `W12:ERROR ...`. Tests wait on these instead of fixed sleeps.
- `tests/e2e.sh`: boots the OS, then: screenshot boot + lock screen → unlock → desktop → open **every app** via Start menu clicks, interact a bit (type in Notepad and save, compute in Calculator, click Minesweeper cells, run `proof` in Terminal, load a page in Browser, end a process in Task Manager), screenshot each, close → Widgets board → snap layout (two windows side by side) → Alt+Tab → theme switch → Copilot with bridge up (real answer) **and** bridge down (offline message) → shutdown. Screenshots to `tests/out/`, clear PASS/FAIL per step, nonzero exit on failure.
- **Actually look at the screenshots** (open the PNGs) after each milestone to catch visual bugs that markers can't: clipped text, wrong colors, missing blur, misaligned taskbar.

## 10. Proof mode

Reachable from Terminal (`proof`) and Settings → About → Proof. Shows:
- every running process from /proc with its exe path (all should be your binaries + kernel threads)
- lines of code per component (computed at build time from this repo, embedded in the image)
- full file listing of the root filesystem with sizes
- result of the toolkit scan: `scripts/verify_no_toolkits.sh` runs at build time, scans the rootfs for X11/Wayland/Qt/GTK/SDL/WebKit/Chromium/Gecko/busybox/systemd (file names + strings in binaries) and **fails the build** if anything is found; proof mode shows the PASS result and timestamp
- kernel version + a one-line explanation: "Linux kernel below. Everything above /dev/dri and /dev/input is written from scratch in this repo."

## 11. Milestones (rough time budget)

- **M0 (0:00–0:20)** Study Lumen, PLAN.md, repo, toolchain, `run.sh` boots kernel + own init, prints to serial.
- **M1 (–0:45)** DRM at 1920×1080, boot animation, evdev + tablet input, cursor, QMP harness + first screenshot.
- **M2 (–1:20)** Compositor, window protocol, WM (drag/resize/min/max/close, shadows, rounded corners), `libw12ui` basics, lock screen.
- **M3 (–1:50)** Taskbar, top bar, Start menu, glass, theme system.
- **M4 (–2:20)** Terminal + shell, Notepad, Calculator, File Explorer, Settings/About.
- **M5 (–2:40)** Net stack port, Browser, Task Manager, real network info.
- **M6 (–3:00)** Copilot panel + Mac bridge + offline fallback.
- **M7 (–3:25)** Widgets board, snap layouts, Alt+Tab, Minesweeper, animations polish.
- **M8 (–3:45)** Proof mode + toolkit scan, full e2e pass, README (how to run, architecture, disclaimer, attribution), final screenshot set.

If you're behind schedule, keep the order but cut depth (fewer Settings pages, simpler widgets), never cut: boot + lock screen, taskbar/Start/glass, working windows, Copilot, proof mode, e2e.

## 12. Done means

- `./run.sh` on a clean checkout boots to the lock screen at 1920×1080.
- `tests/e2e.sh` passes end to end, with screenshots of every app in `tests/out/`.
- Proof mode shows no third-party UI stack.
- PROGRESS.log, commits per milestone, `screenshots/` per milestone, README all present.
- Final message to me: what works, what's rough, known bugs, and how long each milestone actually took.
## Notes from me
- Docker runs through colima (already started). `~/Documents/PhoneOS2` is read-only for you: copy from it, never edit it.
- The `claude` CLI inherits env vars from whatever starts it, which can break its login. In the bridge, start it with a clean env: `env -i HOME=$HOME USER=$USER PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin claude -p ...` (tested, works).
- I record the screen later with my own QMP screendump tool, so keep the QMP socket path stable and leave the e2e harness in the repo.
