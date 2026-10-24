#!/bin/sh
# Offline parser and password-policy stress checks for FLX authd.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cargo test --offline --manifest-path "$ROOT/src/rust/authd/Cargo.toml"
