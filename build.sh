#!/bin/sh
# FLX OS — build script.
# Builds kernel + userland + rootfs inside the flx-toolchain container.
# Supports: x86_64 (native KVM) and arm64 (emulated via qemu-user or native on Apple/ARM servers).
#
#   FLX_ARCH=x86_64   force x86_64 build
#   FLX_ARCH=arm64    force arm64 build  (default when host is aarch64)
#   (auto-detected when unset)
set -e
cd "$(dirname "$0")"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

HOST_ARCH="$(uname -m)"
case "${FLX_ARCH:-$HOST_ARCH}" in
  x86_64|amd64)   TARGET="x86_64"; PLATFORM="linux/amd64" ;;
  aarch64|arm64)  TARGET="arm64";  PLATFORM="linux/arm64" ;;
  *) echo "Unsupported arch: ${FLX_ARCH:-$HOST_ARCH}"; exit 1 ;;
esac
export FLX_ARCH="$TARGET"

if ! docker ps >/dev/null 2>&1 && sg docker -c "docker ps" >/dev/null 2>&1; then
  exec sg docker -c "$0 $*"
fi

IMAGE="flx-toolchain-$TARGET"
docker image inspect "$IMAGE" >/dev/null 2>&1 || \
  docker build --platform "$PLATFORM" -t "$IMAGE" docker/

mkdir -p build
docker run --rm --platform "$PLATFORM" \
  -e FLX_ARCH="$TARGET" \
  -e FLX_BUILD_TIME="$(date '+%Y-%m-%d %H:%M')" \
  -v "$PWD":/src \
  -v "flx-kbuild-$TARGET":/kbuild \
  "$IMAGE" sh /src/docker/build-inside.sh
