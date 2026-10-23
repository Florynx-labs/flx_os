# Native Rust application migration status

FLX OS now stages the File Explorer and Task Manager as Rust applications linked to the existing FLX C UI library. The C implementations remain available as source fallbacks. The Rust applications use the same compositor, drawing primitives, event loop and filesystem services as the rest of the desktop.

- File Explorer: see [FLX_FILE_EXPLORER_RUST.md](FLX_FILE_EXPLORER_RUST.md).
- Task Manager: see [FLX_TASK_MANAGER_RUST.md](FLX_TASK_MANAGER_RUST.md).
- Lock screen and current authentication/power limitations: see [FLX_LOCKSCREEN_RUST.md](FLX_LOCKSCREEN_RUST.md).

Build with `FLX_ARCH=x86_64 ./build.sh`. The desktop shell and lock screen remain implemented by the native C compositor; this migration does not replace the compositor.
