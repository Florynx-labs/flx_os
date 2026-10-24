# FLX OS security review

Review date: 2026-10-23. This is a focused source review and build check, not a formal penetration test.

## Implemented account isolation

- The Rust `flx-authd` service keeps a root-only account database and one salted PBKDF2-SHA256 credential record per user. It rejects malformed identity databases and missing/unsafe credentials instead of silently resetting identities.
- Each account gets a distinct Linux UID/GID and a mode-0700 `/home/users/<name>` directory. The root-owned `/home/user` compatibility link is switched only after the selected account authenticates. At boot it is reset to the default `flx` profile.
- Applications launch under the active UID/GID, with supplementary groups cleared and `no_new_privs` set. The compositor checks `SO_PEERCRED` against the active UID before accepting a window. Its socket is mode 0660 and restricted to the active account's primary group.
- Switching accounts terminates the previous session's application process groups before changing the active home. Settings can create users with Standard or Administrator labels; the service requires the current administrator's password and creates private homes and credentials.
- The lock screen lists accounts. Switching to another user requires that user's password. Empty first-run setup still opens the desktop directly; a password can be added later in Settings → Comptes. Passwords accept 1–256 bytes as requested. Password change and one-time recovery codes are per-account.
- Settings and recent-file writes run in a short-lived child after dropping to the active UID, so a compromised app cannot use the root compositor to write through a symlink in its home.

## Remaining findings and limits

| Severity | Finding | State |
| --- | --- | --- |
| High | PID 1, the compositor, and `flx-authd` remain trusted root processes. An exploit in those components can bypass home permissions. | Open; reduce privileged code and fuzz IPC. |
| High | Home directories use Unix DAC, not per-user encryption. A process with root access or offline access to the disk image can read them. A logged-in Administrator role runs as an ordinary UID and cannot read another user's mode-0700 home through normal filesystem access. | Root/physical-access protection requires encrypted homes and key lifecycle work. |
| High | Applications of the same user still share that user's files, clipboard, and IPC authority. There is no per-app sandbox, seccomp policy, or namespace isolation. | Open. |
| High | The requested one-character minimum is weak. PBKDF2 protects stored credentials but cannot make guessable passwords strong. Failure throttling is in-memory and resets when the service restarts. | Partly mitigated; durable throttling and configurable policy remain. |
| Medium | Recovery codes are bearer secrets. They are stored as salted verifiers and consumed on reset, but there is no hardware-backed recovery or administrator-mediated recovery flow. | Open. |
| Medium | Account and filesystem integration has only build and focused auth-unit coverage. Multi-session GUI flows, process teardown races, and hostile IPC need automated QEMU integration tests and fuzzing. | Open. |
| Medium | Package installation is not production-ready until signed repository metadata, key rotation, rollback protection, and atomic installs are implemented. | Open. |

## Checks

- `scripts/stress-auth.sh` exercises password bounds, malformed hex, constant-time comparison, and repeated parser input.
- `scripts/security-check.sh` checks formatting, compilation, auth unit tests, selected security invariants, shell syntax, and whitespace.

These checks are useful regression coverage; they are not proof of exploit resistance. The system still trusts the kernel, boot image, root services, and physical integrity of the machine.
