#!/usr/bin/env bash
# Copyright 2026 Timothy Place and the catenae contributors
#
# Builds the published site (D46): the guide (mdBook) at its root and the reference (Doxygen)
# under api/, into build-docs/site, with every warning an error and every internal link
# checked. CI's docs job runs this and publishes build-docs/site from main.
#
#   scripts/build_site.sh        # needs mdbook (v0.4.40, as CI pins it) and doxygen on PATH
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rm -rf build-docs
mkdir -p build-docs

# The reference. Doxygen's own WARN_AS_ERROR fails the run on any warning.
doxygen docs/Doxyfile

# The guide. mdBook reports a missing include or anchor as a warning and still exits 0, so
# its log is read: a warning or an error is a failure.
mdbook build guide 2>&1 | tee build-docs/mdbook.log
if grep -qiE '\b(warn|warning|error)\b' build-docs/mdbook.log; then
    echo "::error::mdbook reported a warning or an error (a stale include or anchor?)"
    exit 1
fi

# One site: the guide at the root, the reference at /api/.
mkdir -p build-docs/site
cp -r build-docs/guide/. build-docs/site/
cp -r build-docs/html/. build-docs/site/api/ 2>/dev/null || { mkdir -p build-docs/site/api && cp -r build-docs/html/. build-docs/site/api/; }
python3 scripts/check_links.py build-docs/site
echo "site: build-docs/site ($(find build-docs/site -name '*.html' | wc -l) pages)"
