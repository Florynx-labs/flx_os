#!/bin/sh
# Windows 12: build (if needed), start the Copilot bridge on the Mac, boot QEMU.
# QEMU flags start from Lumen OS 2's working set (virt + hvf + virtio-gpu/tablet/keyboard/net/blk).
#   W12_DISPLAY=cocoa (default) | vnc | none     W12_NO_BRIDGE=1     W12_DISK=path.img
set -e
cd "$(dirname "$0")"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
mkdir -p build data

# 1. build if needed
if [ ! -f build/Image ] || [ ! -f build/initramfs.cpio ] || [ ! -f build/home.img ] || \
   [ -n "$(find src kernel docker third_party tools scripts -type f -newer build/initramfs.cpio | head -1)" ]; then
  ./build.sh
fi

# 2. persistent disk (ext2 /home), created from the template on first run
DISK="${W12_DISK:-data/disk.img}"
[ -f "$DISK" ] || { echo "Creating $DISK"; cp build/home.img "$DISK"; }

# 3. Copilot bridge (127.0.0.1 only; the guest reaches it as 10.0.2.2:8766)
BRIDGE=""
if [ -z "$W12_NO_BRIDGE" ]; then
  python3 bridge/copilot_bridge > build/bridge.log 2>&1 &
  BRIDGE=$!
fi
QMP=/tmp/w12-qmp.sock
cleanup() { [ -n "$BRIDGE" ] && kill "$BRIDGE" 2>/dev/null; rm -f "$QMP"; }
trap cleanup EXIT INT TERM

if [ "$(uname -s)" = "Darwin" ]; then
  ACCEL="-accel hvf -cpu host"
  DEFAULT_DISP="cocoa"
else
  if [ "$(uname -m)" = "aarch64" ]; then
    ACCEL="-accel kvm -cpu host"
  else
    ACCEL="-accel tcg -cpu cortex-a72"
  fi
  DEFAULT_DISP="gtk"
fi

case "${W12_DISPLAY:-$DEFAULT_DISP}" in
  cocoa) DISP="-display cocoa,zoom-to-fit=on,show-cursor=off" ;;
  gtk)   DISP="-display gtk,zoom-to-fit=on,show-cursor=off" ;;
  sdl)   DISP="-display sdl" ;;
  vnc)   DISP="-display none -vnc localhost:12"; echo "VNC on localhost:5912" ;;
  *)     DISP="-display none" ;;
esac

rm -f "$QMP"
qemu-system-aarch64 -M virt $ACCEL -smp 4 -m 2048 \
  -kernel build/Image -initrd build/initramfs.cpio \
  -append "console=ttyAMA0 loglevel=4 quiet" \
  -device virtio-gpu-pci,xres=1920,yres=1080 \
  -device virtio-keyboard-pci -device virtio-tablet-pci \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -drive file="$DISK",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0 \
  -device virtio-rng-pci \
  -rtc base=localtime \
  $DISP \
  -serial file:build/serial.log \
  -qmp unix:$QMP,server,nowait
