# Windows 12 — plan

Unofficial fan project, not affiliated with Microsoft. Linux kernel below; everything above
`/dev/dri` and `/dev/input` is written in this repo (C + musl, static).

## Architecture

```
 apps (separate processes, libw12ui)          w12sh (own shell, runs on a pty)
 explorer settings terminal notepad calc browser taskmgr minesweeper proof
        |  SOCK_SEQPACKET /run/w12.sock  +  memfd ARGB double buffers (explicit release)
 w12-compositor  — DRM/KMS dumb buffers + page flip, evdev, WM, shell, glass, Copilot client
 w12-netd        — own DHCP client (AF_PACKET) + rtnetlink -> /run/net.conf
 /sbin/init      — PID 1: mounts, ext2 /home on virtio-blk, respawn, shutdown/restart
 ----------------------------------------------------------------------------------
 Linux 6.12 (arm64, minimal config, no fbcon)            QEMU virt + hvf, virtio-*
```

- libw12ui: gfx (SDF rounded rects, shadows, glass blur, polygons), Selawik text via
  stb_truetype, icons drawn in code, immediate-mode widgets, JSON parser, client protocol.
- net: DNS, HTTP/1.1 (+chunked, redirects, keep-alive), TLS via BearSSL, inflate (from Lumen OS 2).
- web: HTML tokenizer/tree builder, CSS cascade, block/inline/table layout (from Lumen OS 2).
- Copilot: compositor -> HTTP POST 10.0.2.2:8766/ask -> `bridge/copilot_bridge` (Mac) ->
  `claude -p` with all tools disabled, streamed back as NDJSON.

## Milestones
- [x] M0 repo, toolchain, kernel, own init on serial
- [x] M1 DRM 1920x1080, boot animation, input, cursor, QMP harness, first screenshot
- [ ] M2 compositor, window protocol, WM, libw12ui, lock screen
- [ ] M3 taskbar, top bar, Start, glass, themes
- [ ] M4 Terminal + shell, Notepad, Calculator, Explorer, Settings/About
- [ ] M5 net stack, Browser, Task Manager, network info
- [ ] M6 Copilot + bridge + offline fallback
- [ ] M7 Widgets, snap layouts, Alt+Tab, Minesweeper, polish
- [ ] M8 proof mode, toolkit scan, e2e, README
