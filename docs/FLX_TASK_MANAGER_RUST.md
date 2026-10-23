# FLX Task Manager — Rust implementation notes

## Repository and integration audit

- `src/apps/taskmgr.c` is the existing C process monitor. It creates a resizable native window through `AppDef` + `app_main`, declared in `src/lib/w12.h` and implemented in `src/lib/client.c`.
- The client connects to `/run/flx.sock` using the existing `SOCK_SEQPACKET` protocol, double-buffered memfd surfaces, and real `M_MOUSE`, `M_KEY`, `M_CONFIGURE`, `M_THEME`, `M_CLOSE_REQ`, and `M_RELEASE` events. `Canvas`, `UIState`, `g_*`, `text`, and `ui_*` functions are the existing graphics/input boundary. The C compositor, window manager, theme, font and icon libraries remain the FLX foundation.
- The shell, store, app registry and launcher identify Task Manager as `taskmgr`, launched from `/bin/taskmgr`. `src/Makefile` stages all application outputs into that rootfs path. The Rust binary reuses the actual static `libw12.a` ABI and has no GUI dependencies.
- Existing process data is available through mounted `/proc`; init mounts procfs and starts the compositor and `flx-netd`. Linux procfs exposes process stat/status/cmdline/exe and global CPU/memory/network/block-device counters. Some fields can disappear or be denied while read.
- FLX has no standard service manager, per-process network accounting service, GPU telemetry layer, or guaranteed hardware sensor interface. Startup is currently performed directly by PID 1. These pages must be informative/read-only and unavailable values must remain explicit.
- The FLX text/widget API offers raster drawing, text, buttons, fields, scrolling, clipping and existing icons, but no table/chart widget. The Rust UI draws using those real primitives. Window controls belong to the compositor decorations.
- The Alpine x86_64 image has Rust/Cargo 1.87.0 and successfully builds static musl apps. arm64 has not yet been verified.

## Design and implementation decisions

The reference is a dark FLX desktop image with a large 16:9-ish application surface: 7-item narrow navigation rail, title/subtitle header, compact resource cards, dense process table, and a right information/action column. Desktop wallpaper, top bar, launchers and window decorations remain owned by FLX shell/compositor. The application will keep the same overall information hierarchy while scaling down to the actual window size.

The native Rust app is implemented at `src/rust/taskmgr/src/main.rs` and stages as `/bin/taskmgr`. It uses the existing FLX app/window ABI, with no new GUI crates. A bounded worker channel samples procfs/sysfs/statvfs away from rendering; the UI owns snapshots and bounded history. The interface includes resource cards and history, searchable/sortable/filterable process rows, process details, application registry discovery, user aggregation, FLX service detection, and explicitly read-only startup information. Process actions show a confirmation, reject PID 1, re-read PID start time immediately before signaling, and report permission errors. Arbitrary restart is disabled because FLX has no safe restart manager.

The process table reports per-process CPU/RSS and procfs I/O where available. There is no reliable per-process network accounting source in this system, so its network column remains unavailable. Network cards show measured aggregate RX/TX rates without inventing a utilization percentage. Sensor and memory fields remain unavailable when the kernel does not expose them.

`flx-core` provides tested parsers and CPU accounting shared by the Rust apps. The x86_64 host-side static link check passed, and the core test suite currently passes (8 tests). The complete x86_64 image build succeeded and a fresh QEMU instance booted through unlock to the desktop. Visual interaction with the Task Manager remains to be checked in that running VM.

## Known limitations

There is no FLX privilege broker, service manager, or per-process network/I/O service. Signal operations therefore succeed only when Linux permissions permit. Filesystem capacity can be read through a tiny libc `statvfs` compatibility helper. GPU usage and thermal sensors may be unavailable. The supplied screenshot contains example data and device-specific values; none of those values are reused as runtime data.
