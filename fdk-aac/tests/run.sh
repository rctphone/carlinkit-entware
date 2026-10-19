#!/usr/bin/env bash
# Guarded-buffer tests for the fdk-aac decoder patches (see guard_test.c).
#
# Builds fdk-aac $PKG_VERSION natively in docker with this package's patches applied in order,
# encodes AAC-ELD / AAC-LC test streams with the library's own encoder, decodes them the way the
# CarPlay SDK converter does (TT_MP4_RAW + ConfigRaw + Fill + DecodeFrame) into output buffers
# that end at a PROT_NONE guard page, and checks results and written byte ranges.
#
#   bash fdk-aac/tests/run.sh
#
# Environment:
#   FDK_AAC_TARBALL  local fdk-aac-$PKG_VERSION.tar.gz (else cached in docker volume
#                    carlinkit-fdk-aac-test-dl, downloaded once from PKG_SOURCE_URL)
#   FDK_AAC_PATCHES  directory with the patches to apply (default: ../patches)
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
PKG=$(dirname "$HERE")
PATCHES=$(cd "${FDK_AAC_PATCHES:-$PKG/patches}" && pwd)
IMAGE=carlinkit-fdk-aac-test
VOLUME=carlinkit-fdk-aac-test-dl

mkvar() { sed -n "s/^$1:=//p" "$PKG/Makefile"; }
VERSION=$(mkvar PKG_VERSION)
HASH=$(mkvar PKG_HASH)
URL=$(mkvar PKG_SOURCE_URL | sed "s/\$(PKG_VERSION)/$VERSION/")
TARNAME=fdk-aac-$VERSION.tar.gz

docker build -q -t "$IMAGE" - >/dev/null <<'EOF'
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential autoconf automake libtool \
      curl ca-certificates \
 && rm -rf /var/lib/apt/lists/*
EOF

mounts=(-v "$HERE:/tests:ro" -v "$PATCHES:/patches:ro" -v "$VOLUME:/dl")
if [ -n "${FDK_AAC_TARBALL:-}" ]; then
  mounts+=(-v "$(cd "$(dirname "$FDK_AAC_TARBALL")" && pwd)/$(basename "$FDK_AAC_TARBALL"):/src.tar.gz:ro")
fi

docker run --rm "${mounts[@]}" -e TARNAME="$TARNAME" -e HASH="$HASH" -e URL="$URL" "$IMAGE" \
  bash -euo pipefail -c '
    tarball=/src.tar.gz
    if [ ! -f "$tarball" ]; then
      tarball=/dl/$TARNAME
      if [ ! -f "$tarball" ]; then
        curl -fsSL -o "$tarball.part" "$URL"
        mv "$tarball.part" "$tarball"
      fi
    fi
    echo "$HASH  $tarball" | sha256sum -c --quiet
    mkdir /build && tar -xzf "$tarball" -C /build --strip-components=1 && cd /build
    for p in /patches/*.patch; do
      echo "applying $(basename "$p")"
      patch -p1 -s --no-backup-if-mismatch < "$p"
    done
    { autoreconf -fi && ./configure --prefix=/opt/fdk --disable-static --disable-example \
        && make -j"$(nproc)" && make install; } > /build.log 2>&1 || { tail -50 /build.log; exit 1; }
    gcc -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -o /guard_test /tests/guard_test.c \
        -I/opt/fdk/include -L/opt/fdk/lib -lfdk-aac -lm -Wl,-rpath,/opt/fdk/lib
    /guard_test
  '
