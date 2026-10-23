#!/bin/sh
# FLX OS end-to-end test: boots headless, drives every app through QMP, checks serial
# markers, writes screenshots to tests/out/. Exit code 0 = all checks passed.
cd "$(dirname "$0")/.." || exit 1
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
exec python3 tests/e2e.py "$@"
