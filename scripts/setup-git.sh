#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Boris Miscenco
set -euo pipefail
[ ! -d ".git" ] && git init
git branch -M main 2>/dev/null || true
git show-ref --verify --quiet refs/heads/develop 2>/dev/null || git checkout -b develop && git checkout main
if ! git log --oneline -1 &>/dev/null; then
    git add -A
    git commit -m "feat: Silent Riot amp sim scaffold

Spec v2.6 — full normalized voltage domain, 5-stage signal chain,
multi-node PSU, LTP nodal solver, EL84 pentode, speaker coupling.
Phase 1 (core preamp) ready for implementation."
fi
echo "Git ready. Workflow: main → develop → feature/*"
