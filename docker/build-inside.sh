#!/bin/sh
# FLX OS build, runs inside the flx-toolchain container (Alpine musl native).
# Supports dual target architecture: x86_64 and arm64.
# Produces build/bzImage (or Image), build/initramfs.cpio, build/home.img
set -eu

TARGET_ARCH="${FLX_ARCH:-$(uname -m)}"
case "$TARGET_ARCH" in
  x86_64|amd64)
    KARCH="x86_64"
    KCONFIG="/src/kernel/flx-x86_64.config"
    ;;
  aarch64|arm64)
    KARCH="arm64"
    KCONFIG="/src/kernel/flx-arm64.config"
    ;;
  *)
    echo "[build] Unsupported architecture: $TARGET_ARCH"
    exit 1
    ;;
esac

KVER=6.12.113
K=/kbuild/linux-$KVER
OUT=/src/build
GEN=/kbuild/gen
JOBS=$(nproc)
mkdir -p "$OUT" "$GEN"

# ---- kernel -------------------------------------------------------------
if [ ! -d "$K" ]; then
  echo "[build] downloading linux-$KVER"
  curl -sSfL "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$KVER.tar.xz" | tar xJ -C /kbuild
fi
FRAG_SUM=$(md5sum "$KCONFIG" | cut -d' ' -f1)
if [ "$(cat $K/.flx-frag-$KARCH 2>/dev/null)" != "$FRAG_SUM" ]; then
  echo "[build] configuring kernel ($KARCH)"
  make -C "$K" ARCH="$KARCH" allnoconfig >/dev/null
  (cd "$K" && ARCH="$KARCH" scripts/kconfig/merge_config.sh -m .config "$KCONFIG" >/dev/null)
  make -C "$K" ARCH="$KARCH" olddefconfig >/dev/null
  grep -E '^CONFIG_.*=y' "$KCONFIG" | while read -r l; do
    grep -qx "$l" "$K/.config" || echo "[build] WARNING: $l not set"
  done
  echo "$FRAG_SUM" > "$K/.flx-frag-$KARCH"
fi
echo "[build] kernel ($KARCH)"
if [ "$KARCH" = "x86_64" ]; then
  make -C "$K" ARCH="$KARCH" -j"$JOBS" bzImage >/kbuild/kernel-build.log 2>&1 || { tail -40 /kbuild/kernel-build.log; exit 1; }
  cp "$K/arch/x86/boot/bzImage" "$OUT/bzImage"
  cp "$K/arch/x86/boot/bzImage" "$OUT/Image"  # compatibility copy
  OUT_IMAGE="$OUT/bzImage"
else
  make -C "$K" ARCH="$KARCH" -j"$JOBS" Image >/kbuild/kernel-build.log 2>&1 || { tail -40 /kbuild/kernel-build.log; exit 1; }
  cp "$K/arch/arm64/boot/Image" "$OUT/Image"
  OUT_IMAGE="$OUT/Image"
fi
[ -f "$K/usr/include/drm/drm.h" ] || make -C "$K" ARCH="$KARCH" headers >/dev/null

# ---- BearSSL ------------------------------------------------------------
if [ ! -f /kbuild/bearssl/build/libbearssl.a ]; then
  echo "[build] building BearSSL"
  rm -rf /kbuild/bearssl && cp -r /src/third_party/bearssl /kbuild/bearssl
  make -C /kbuild/bearssl -j"$JOBS" lib tools >/dev/null
fi
CA_SUM=$(md5sum /src/third_party/cacert.pem | cut -d' ' -f1)
if [ "$(cat $GEN/.ca 2>/dev/null)" != "$CA_SUM" ]; then
  echo "[build] converting Mozilla CA bundle"
  { echo '#include "bearssl.h"'; /kbuild/bearssl/build/brssl ta -q /src/third_party/cacert.pem;
    echo 'const br_x509_trust_anchor *flx_tas = TAs; const size_t flx_tas_num = TAs_NUM;'; } > "$GEN/ta.c"
  echo "$CA_SUM" > "$GEN/.ca"
fi

# ---- userland -----------------------------------------------------------
echo "[build] userland ($KARCH)"
make -C /src/src -j"$JOBS" O=/kbuild/obj KINC=$K/usr/include GEN=$GEN BEAR=/kbuild/bearssl >/kbuild/user-build.log 2>&1 || { grep -E 'error|Error' -A3 /kbuild/user-build.log | head -60; exit 1; }
grep -E 'warning' /kbuild/user-build.log | head -20 || true

# ---- rootfs staging -----------------------------------------------------
R=/kbuild/rootfs
rm -rf "$R"; mkdir -p "$R"/bin "$R"/sbin "$R"/share/fonts "$R"/share/wallpapers "$R"/share/icons "$R"/share/proof "$R"/etc
for b in /kbuild/obj/bin/*; do cp "$b" "$R/bin/"; done
mv "$R/bin/init" "$R/sbin/init"
strip "$R"/bin/* "$R"/sbin/init
cp /src/third_party/fonts/*.ttf "$R/share/fonts/"
cp /src/third_party/fonts/LICENSE.txt "$R/share/fonts/OFL.txt"
cp /src/third_party/fonts/LIBERATION-LICENSE.txt "$R/share/fonts/LIBERATION-LICENSE.txt"

# Real Wallpapers and Real Icons from assets:
python3 /src/scripts/prepare_assets.py
if [ -d /src/build/wallpapers ]; then
  cp -r /src/build/wallpapers/*.raw "$R/share/wallpapers/" 2>/dev/null || true
fi
if [ -d /src/build/icons ]; then
  cp -r /src/build/icons/*.png "$R/share/icons/" 2>/dev/null || true
fi
[ -f "$R/share/wallpapers/bloom.raw" ] || /kbuild/obj/tools/gen_wallpaper "$R/share/wallpapers"

cat > "$R/etc/os-release" <<OS
NAME="FLX OS"
PRETTY_NAME="FLX OS 0.1.0 ($KARCH)"
VERSION="0.1.0 build $(date +%y%m%d)"
ID=flx-os
ID_LIKE=linux
HOME_URL="https://github.com/Florynx-labs"
OS
echo "flx-os" > "$R/etc/hostname"
cat > "$R/etc/passwd" <<'PASSWD'
root:x:0:0:root:/root:/bin/sh
flx:x:1000:1000:FLX Desktop:/home/user:/bin/sh
PASSWD
cat > "$R/etc/group" <<'GROUP'
root:x:0:
flx:x:1000:
GROUP
sh /src/scripts/loc.sh /src > "$R/share/proof/loc.txt"
echo "${FLX_BUILD_TIME:-$(date '+%Y-%m-%d %H:%M')}" > "$R/share/proof/build.txt"
FLX_BUILD_TIME="${FLX_BUILD_TIME:-}" sh /src/scripts/verify_no_toolkits.sh "$R" > "$R/share/proof/scan.txt" || { cat "$R/share/proof/scan.txt"; echo "[build] FLX OS TOOLKIT SCAN FAILED"; exit 1; }
cat "$R/share/proof/scan.txt" | head -3

# ---- initramfs ----------------------------------------------------------
[ -x /kbuild/gen_init_cpio ] || gcc -O2 -o /kbuild/gen_init_cpio "$K/usr/gen_init_cpio.c"
{
  echo "dir /dev 0755 0 0"; echo "nod /dev/console 0600 0 0 c 5 1"
  for d in proc sys run home; do echo "dir /$d 0755 0 0"; done
  echo "dir /tmp 1777 0 0"
  (cd "$R" && find . -mindepth 1 -type d | sort | sed 's|^\.||' | while read -r d; do echo "dir $d 0755 0 0"; done)
  (cd "$R" && find . -type f | sort | sed 's|^\.||' | while read -r f; do
     m=0644; [ -x "$R$f" ] && m=0755; echo "file $f $R$f $m 0 0"; done)
  echo "slink /init /sbin/init 0777 0 0"
} > /kbuild/initramfs.list
/kbuild/gen_init_cpio /kbuild/initramfs.list > "$OUT/initramfs.cpio"
(cd "$R" && find . -type f -exec ls -l {} \; | awk '{print $5, $9}' | sed 's| \./| /|' | sort -k2) > "$OUT/rootfs-files.txt"

# ---- persistent home disk template (ext2) --------------------------------
H=/kbuild/home
rm -rf "$H"; mkdir -p "$H/user/.config" "$H/user/Documents" "$H/user/Pictures" "$H/user/Desktop" "$H/user/Downloads" "$H/user/Music" "$H/.config" "$H/.flx-system"
cp -r /src/home-skel/. "$H/user/" 2>/dev/null || true
chown -R 1000:1000 "$H/user"
chmod 0700 "$H/user"
chmod 0700 "$H/user/.config"
chmod 0700 "$H/.flx-system"
rm -f "$OUT/home.img"
mke2fs -q -t ext2 -L flxhome -d "$H" "$OUT/home.img" 64M
echo "[build] FLX OS ($KARCH) done: $(ls -la $OUT_IMAGE $OUT/initramfs.cpio | awk '{print $5, $9}' | tr '\n' ' ')"
