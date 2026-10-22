# Build toolchain for FLX OS (from Lumen OS 2) (arm64 Alpine = musl native)
FROM --platform=linux/arm64 alpine:3.22
RUN apk add --no-cache build-base linux-headers bc flex bison perl xz curl \
    python3 py3-fonttools elfutils-dev openssl-dev diffutils findutils cpio e2fsprogs
WORKDIR /src
