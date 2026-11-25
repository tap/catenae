#!/bin/bash
# Copyright 2026 Timothy Place and the catenae contributors
# SessionStart hook for Claude Code on the web, carried from SampleRateTap (NOTICE.md).
#
# A fresh web-session container clones the repository with no pre-commit hook installed, so
# `git commit` would run unformatted and the clang-format CI job would fail on code a local
# clone formats at commit time. This installs pre-commit, registers the repository's hook
# (.pre-commit-config.yaml, the pinned clang-format) and warms its binary. Web sessions only;
# a local clone is untouched. catenae has no submodules, so the Tap copy's submodule step is
# gone.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
    exit 0
fi

cd "$CLAUDE_PROJECT_DIR"

echo "session-start: installing pre-commit ..."
if ! python3 -m pre_commit --version >/dev/null 2>&1; then
    python3 -m pip install --quiet pre-commit
fi
python3 -m pre_commit install
python3 -m pre_commit install-hooks

echo "session-start: done."
