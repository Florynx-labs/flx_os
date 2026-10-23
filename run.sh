#!/bin/sh
# FLX OS — boot script.
# Builds if needed, then launches QEMU.
# Supports x86_64 (native KVM) and arm64 (emulated TCG or native KVM on ARM hosts).
#
# Environment variables:
#   FLX_ARCH=x86_64|arm64   force target architecture (auto-detected by default)
#   FLX_DISPLAY=gtk|sdl|vnc|none|cocoa  (default: gtk on Linux, cocoa on macOS)
#   FLX_NO_BRIDGE=1          disable the Copilot bridge
#   FLX_DISK=path.img        use a custom persistent disk image
#   FLX_QMP=/tmp/flx-qmp.sock  (fixed path used by test harness)
set -e
cd "$(dirname "$0")"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
mkdir -p build data

HOST_ARCH="$(uname -m)"
case "${FLX_ARCH:-$HOST_ARCH}" in
  x86_64|amd64)  TARGET="x86_64" ;;
  aarch64|arm64) TARGET="arm64" ;;
  *) echo "Unsupported arch: ${FLX_ARCH:-$HOST_ARCH}"; exit 1 ;;
esac
export FLX_ARCH="$TARGET"

# 1. Build if needed (check by image presence + source freshness)
KERNEL_IMG="build/Image"
[ "$TARGET" = "x86_64" ] && KERNEL_IMG="build/bzImage"
if [ ! -f "$KERNEL_IMG" ] || [ ! -f build/initramfs.cpio ] || [ ! -f build/home.img ] || \
   [ -n "$(find src kernel docker third_party tools scripts assets -type f -newer build/initramfs.cpio | head -1)" ]; then
  ./build.sh
fi

# 2. Persistent ext2 /home disk (created from template on first run)
DISK="${FLX_DISK:-data/disk.img}"
[ -f "$DISK" ] || { echo "Creating $DISK"; cp build/home.img "$DISK"; }

# 3. Copilot/AI bridge (127.0.0.1; guest reaches it as 10.0.2.2:8766)
BRIDGE=""
if [ -z "$FLX_NO_BRIDGE" ]; then
  python3 bridge/copilot_bridge > build/bridge.log 2>&1 &
  BRIDGE=$!
fi
QMP="${FLX_QMP:-/tmp/flx-qmp.sock}"
cleanup() { [ -n "$BRIDGE" ] && kill "$BRIDGE" 2>/dev/null; rm -f "$QMP"; }
trap cleanup EXIT INT TERM

# 4. QEMU flags — choose acceleration, machine and display by OS + arch
OS="$(uname -s)"
if [ "$OS" = "Darwin" ]; then
  # macOS: HVF for both arm64 and x86_64
  if [ "$TARGET" = "arm64" ]; then
    MACHINE="virt"; ACCEL="-accel hvf -cpu host"; CONSOLE="ttyAMA0"
  else
    MACHINE="q35"; ACCEL="-accel hvf -cpu host"; CONSOLE="ttyS0"
  fi
  DEFAULT_DISP="cocoa"
else
  # Linux
  if [ "$TARGET" = "$HOST_ARCH" ]; then
    # Native: use KVM for maximum speed
    if [ "$TARGET" = "arm64" ]; then
      MACHINE="virt"; ACCEL="-accel kvm -cpu host"; CONSOLE="ttyAMA0"
    else
      MACHINE="q35"; ACCEL="-accel kvm -cpu host"; CONSOLE="ttyS0"
    fi
  else
    # Cross-arch: software emulation (slow, testing only)
    if [ "$TARGET" = "arm64" ]; then
      MACHINE="virt"; ACCEL="-accel tcg -cpu cortex-a72"; CONSOLE="ttyAMA0"
    else
      MACHINE="q35"; ACCEL="-accel tcg -cpu qemu64"; CONSOLE="ttyS0"
    fi
  fi
  DEFAULT_DISP="gtk"
fi

case "${FLX_DISPLAY:-$DEFAULT_DISP}" in
  cocoa) DISP="-display cocoa,zoom-to-fit=on,show-cursor=off" ;;
  gtk)   DISP="-display gtk,zoom-to-fit=on,show-cursor=off" ;;
  sdl)   DISP="-display sdl" ;;
  vnc)   DISP="-display none -vnc localhost:12"; echo "VNC on localhost:5912" ;;
  *)     DISP="-display none" ;;
esac

# 5. Select QEMU binary and kernel/console params
if [ "$TARGET" = "arm64" ]; then
  QEMU="qemu-system-aarch64"
  KERNEL_FLAG="-kernel build/Image"
  CONSOLE_FLAG="console=ttyAMA0"
  MACHINE_FLAGS="-M $MACHINE $ACCEL -smp 4 -m 2048"
else
  QEMU="qemu-system-x86_64"
  KERNEL_FLAG="-kernel build/bzImage"
  CONSOLE_FLAG="console=ttyS0"
  MACHINE_FLAGS="-M $MACHINE $ACCEL -smp 4 -m 2048"
fi

rm -f "$QMP"
$QEMU $MACHINE_FLAGS \
  -vga none \
  $KERNEL_FLAG -initrd build/initramfs.cpio \
  -append "$CONSOLE_FLAG loglevel=4 quiet" \
  -device virtio-gpu-pci,xres=1920,yres=1080 \
  -device virtio-keyboard-pci -device virtio-tablet-pci \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -drive file="$DISK",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0 \
  -device virtio-rng-pci \
  -rtc base=localtime \
  $DISP \
  -serial file:build/serial.log \
  -qmp unix:"$QMP",server,nowait
