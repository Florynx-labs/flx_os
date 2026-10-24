# FLX hardware service

`flx-deviced` is started and supervised by PID 1. It serves one request per local `SOCK_SEQPACKET` connection at `/run/flx-device.sock`. The socket is writable by desktop users because every request is validated against a small command allowlist; callers cannot submit paths, ioctl numbers, or shell commands.

Supported requests:

- `GET`: enumerate actual `/sys` devices (DRM connectors, backlights, ALSA cards, Bluetooth rfkill, network links, power supplies, block devices, and input devices).
- `VOLUME GET` / `VOLUME 0..100`: read or update a writable ALSA playback control selected from a fixed whitelist (`Master`, `PCM`, `Speaker`, or `Headphone` Playback Volume).
- `BRIGHTNESS 5..100`: set the first detected kernel backlight using its reported maximum.
- `BLUETOOTH on|off`: change the software rfkill state for a detected Bluetooth adapter.

Settings reads the live inventory and only exposes controls for devices reported by the kernel. A missing driver or device is reported as unavailable. QEMU currently attaches no audio controller, Bluetooth adapter, or physical backlight, so those controls cannot change hardware in the default VM. The network page continues to read state from `flx-netd`.

## Browser JavaScript

The browser links QuickJS statically and executes page scripts with a fresh runtime per document. It limits each page to 24 MiB, a 128 KiB stack and 1.5 seconds of script execution. Page code receives no filesystem, process, native-module, or direct network APIs. External classic scripts are fetched by the browser's existing HTTP client. The exposed DOM is a small subset (`document.write`, `getElementById`, simple `querySelector`, `createElement`, `appendChild`, attributes, and element `textContent` / `innerText`); the renderer does not yet implement the full DOM, CSSOM, event loop, CORS, `fetch`, XHR, or a module graph. Complex modern sites may still fail to render.
