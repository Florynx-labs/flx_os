# FLX OS lock screen and account sessions

The compositor draws and handles input for the lock screen with FLX's own renderer. PID 1 starts the Rust `flx-authd` service. The auth socket and credential database are root-only; passwords are stored as PBKDF2-HMAC-SHA256 verifiers with random salts. Empty first-run credentials boot straight to the desktop. A password can be created later in Settings → Comptes; the requested policy accepts 1–256 bytes.

After a normal lock, the slider opens the account picker and password dialog. Selecting a different account requires that account's password. The service returns its UID/GID/role only on successful authentication. Before switching, the compositor kills the previous session's app process groups, atomically changes the root-owned `/home/user` compatibility link, reloads that profile's settings, and launches subsequent applications under the selected UID/GID with supplementary groups cleared and `no_new_privs` enabled. The compositor verifies each app's kernel-reported peer UID. Account creation is brokered only from Settings; `flx-authd` verifies the acting account is an Administrator and checks its current password before creating another account.

Each account has a separate root-managed `/home/users/<name>` directory with mode 0700 and its own auth record. Standard and Administrator accounts run as ordinary, unprivileged UIDs; the Administrator label does not grant root. Profile settings and recent-file writes are performed after dropping to the active UID, preventing the compositor from following a user-planted symlink with root write privileges. Password changes require the current account's password. Recovery codes are one-time bearer secrets stored as salted verifiers.

Suspend/resume may use the slider alone when the existing session was already authenticated. If suspend is unavailable, FLX keeps the session locked. Battery information is shown only if a supported power-supply capacity file is available.

## Security boundary

Unix permissions isolate ordinary account processes, including a logged-in Administrator, from another account's mode-0700 home. PID 1, the compositor, and the auth service remain trusted root code and can bypass DAC if compromised. Home files are not encrypted, so root access or offline access to the disk image can read them. Protecting against root or physical disk access requires encrypted homes with per-account keys. Apps within one account also remain mutually unsandboxed. See [SECURITY_AUDIT.md](SECURITY_AUDIT.md) for the current findings and required follow-up.
