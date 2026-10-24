# Build toolchain for FLX OS (Alpine musl native)
# Usage:
#   docker build -t flx-toolchain docker/                                    (host arch)
#   docker build --platform linux/arm64 -t flx-toolchain-arm64 docker/      (arm64)
#   docker build --platform linux/amd64 -t flx-toolchain-amd64 docker/      (x86_64)
FROM alpine:3.22
RUN apk add --no-cache build-base linux-headers bc flex bison perl xz curl \
    python3 py3-pillow py3-cairosvg elfutils-dev openssl-dev diffutils findutils cpio e2fsprogs rust cargo
WORKDIR /src
