#!/bin/sh
# Builds kernel + userland + rootfs inside the arm64 Alpine container (pattern from Lumen OS 2).
set -e
cd "$(dirname "$0")"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
docker image inspect w12-toolchain >/dev/null 2>&1 || docker build -t w12-toolchain docker
mkdir -p build
docker run --rm -e W12_BUILD_TIME="$(date '+%Y-%m-%d %H:%M')" -v "$PWD":/src -v w12-kbuild:/kbuild w12-toolchain sh /src/docker/build-inside.sh
