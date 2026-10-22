#!/bin/sh
# Builds kernel + userland + rootfs inside the arm64 Alpine container (pattern from Lumen OS 2).
set -e
cd "$(dirname "$0")"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
if ! docker ps >/dev/null 2>&1 && sg docker -c "docker ps" >/dev/null 2>&1; then
  exec sg docker -c "$0 $*"
fi
docker image inspect flx-toolchain >/dev/null 2>&1 || docker build --platform linux/arm64 -t flx-toolchain docker
mkdir -p build
docker run --rm --platform linux/arm64 -e W12_BUILD_TIME="$(date '+%Y-%m-%d %H:%M')" -v "$PWD":/src -v flx-kbuild:/kbuild flx-toolchain sh /src/docker/build-inside.sh
