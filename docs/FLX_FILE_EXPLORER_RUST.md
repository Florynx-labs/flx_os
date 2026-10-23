# FLX File Explorer — Rust audit and implementation

## Existing FLX integration

- Applications run as separate processes through `app_main(AppDef, ...)`; the C ABI and input/UI state are declared in `src/lib/w12.h` and implemented in `src/lib/client.c` / `src/lib/ui.c`.
- The Rust Explorer calls that actual ABI and links the existing static `libw12.a`. It uses the FLX rasterizer, `ui_field`, `ui_icon_btn`, `ui_scroll`, `icon_folder`, `icon_file`, `icon_glyph`, `icon_logo`, `app_launch`, `app_recent`, and `flx_fs_stats`. The C compositor continues to own desktop wallpaper, dock, decorations and window management.
- `src/Makefile` stages the Rust binary as `/bin/explorer`; app ID `explorer` is already registered with the launcher. The former `src/apps/explorer.c` remains as a source fallback, not the staged binary.
- The default FLX account currently uses `/home/user` (set by PID 1). Rust reads `$HOME` and falls back to `/home/user`; conventional subfolders that do not exist show the real navigation error rather than silently opening an unrelated path.
- The system has no network-share browser or trash service. The Explorer labels network shares as unconfigured and makes permanent delete explicit with confirmation.

## Implemented behavior

The Rust window now renders the reference hierarchy: app header, left navigation, path and search controls, central file table, right item details and actions. Folder and file icons are drawn by FLX; images show a landscape mark, audio a note, video a play symbol, and PDF/archive/code/text/config files have distinct colors and marks. The shared C icon helper keeps a generic fallback. Directory tabs are selectable and closable; opening a selected entry in a new tab opens its directory or the current containing directory for files.

The Explorer supports real directory listing, directory-first sorting by name/type/size/modified time, filename substring search in the current directory, back/forward/parent/home navigation, direct path entry, hidden-file toggle, list/grid toggle, single selection, keyboard up/down/enter/backspace/F5, opening folders, opening supported text/source files in Notepad, launching registered `/bin` applications only after explicit Open, and real metadata display. It discovers filesystem capacity through `statvfs` on `/`.

Actions include create folder, rename, confirm and permanently delete, file copy/cut/paste, explicit name-conflict rejection, recursive regular-file/directory copy, and error reporting. Symlinks are navigable when they target a directory; deleting a symlink removes the link itself, while copy currently refuses symlinks. Multi-file selection, tabs, recursive search, trash restore, external file associations, rich previews, and asynchronous large-directory operations are not implemented. File operations and directory scans still run synchronously in the UI process, so very large folders may pause redraws.

## Verification

The Explorer and Task Manager Rust binaries build offline as static x86_64 PIE executables against the native FLX library. `flx-core` tests cover directory listing, hidden files, Unicode names, missing paths, and directory symlinks. Explorer tests cover filename validation, common file classification, nested copy/conflict rejection, and safe symlink deletion. The full x86_64 image build succeeds; QEMU reached the desktop and Explorer was opened. Visual inspection confirmed real directory icons, a populated list, selection highlighting, and filesystem-backed details. x86_64 image/runtime verified; arm64 is still unverified.
