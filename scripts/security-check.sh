#!/bin/sh
# Repeatable local security review checks. This is not a penetration test.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
sh -n build.sh run.sh scripts/stress-auth.sh
cargo fmt --manifest-path src/rust/Cargo.toml --all -- --check
cargo check --offline --manifest-path src/rust/Cargo.toml --workspace
cargo test --offline --manifest-path src/rust/authd/Cargo.toml
# Assert the credential service's declared storage/socket protections and that
# the compositor only brokers auth commands for Settings.
rg -q 'PermissionsExt|mode\(0o600\)|0o700' src/rust/authd/src/main.rs
rg -q 'w->peer_uid == session_uid && w->peer_pid == w->pid && !strcmp\(w->exe, "settings"\)' src/comp/wm.c
rg -q 'MIN_PASSWORD: usize = 1' src/rust/authd/src/main.rs
rg -q 'FLX Overview' src/comp/shell.c
rg -q 'Liberation Sans' src/lib/font.c
rg -q 'font_version=2' src/comp/shell.c
git diff --check
printf '%s\n' 'Static security checks passed. Review docs/SECURITY_AUDIT.md for findings and limits.'
