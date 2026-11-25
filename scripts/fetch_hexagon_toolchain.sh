#!/usr/bin/env bash
# Copyright 2026 Timothy Place and the catenae contributors
# Carried from SampleRateTap's scripts/fetch_hexagon_toolchain.sh (NOTICE.md): the destination
# is an argument, and the root it prints is what cmake/hexagon-linux-musl.cmake reads.
#
# Downloads and verifies the pinned Hexagon cross toolchain (CodeLinaro, BSD-3-Clause), the
# compiler of the Hexagon test leg and the ratchet's hexagon key. Both CI jobs write the same
# digest-keyed cache entry, so both verify the same pin before anything lands under it.
#
#   scripts/fetch_hexagon_toolchain.sh DIR
#
#   HEXAGON_TOOLCHAIN_URL     release artifact (CodeLinaro)
#   HEXAGON_TOOLCHAIN_SHA256  hard pin; the download must match exactly
#
# Prints HEXAGON_TOOLCHAIN_ROOT=<dir> on its last line.
set -euo pipefail

: "${HEXAGON_TOOLCHAIN_URL:?set by the CI workflow}"
: "${HEXAGON_TOOLCHAIN_SHA256:?set by the CI workflow}"
dest="${1:?usage: $0 DIR}"

mkdir -p "$dest"
cd "$dest"
if [ ! -f .verified ]; then
    curl -sfLo toolchain.tar.zst "$HEXAGON_TOOLCHAIN_URL"
    actual=$(sha256sum toolchain.tar.zst | cut -d' ' -f1)
    # The published SHA256SUMS catches corruption and cache poisoning; only the pin catches an
    # origin compromise.
    curl -sfLo SHA256SUMS "$(dirname "$HEXAGON_TOOLCHAIN_URL")/SHA256SUMS"
    expected=$(grep "$(basename "$HEXAGON_TOOLCHAIN_URL")" SHA256SUMS | awk '{print $1}' | head -1)
    if [ -z "$expected" ] || [ "$actual" != "$expected" ]; then
        echo "::error::toolchain does not match the published SHA256SUMS"
        exit 1
    fi
    if [ "$actual" != "$HEXAGON_TOOLCHAIN_SHA256" ]; then
        echo "::error::toolchain checksum $actual does not match the pinned value"
        exit 1
    fi
    tar --zstd -xf toolchain.tar.zst
    rm toolchain.tar.zst SHA256SUMS
    touch .verified
fi
# No -type f: the compiler may be a symlink in a restored cache.
cxx=$(find "$PWD" -maxdepth 6 -name 'hexagon-unknown-linux-musl-clang++' | sort | head -1)
if [ -z "$cxx" ]; then
    echo "::error::no hexagon-unknown-linux-musl-clang++ under $dest"
    exit 1
fi
echo "HEXAGON_TOOLCHAIN_ROOT=$(dirname "$(dirname "$cxx")")"
