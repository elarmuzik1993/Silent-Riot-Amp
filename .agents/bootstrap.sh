#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Boris Miscenco
# Installs what a fresh clone needs before `bash scripts/verify.sh` can build.
# Cloud sessions run this at start. Package list mirrors
# .github/actions/setup-linux-deps/action.yml, minus libwebkit2gtk-4.1-dev:
# the plugin sets JUCE_WEB_BROWSER=0 and builds and passes tests without it.
# Python packages are for the sign-off harness in tools/ (not part of verify).
set -eu

# Already provisioned: the header whose absence breaks juceaide's build.
if [ -f /usr/include/X11/extensions/Xrandr.h ] && command -v ninja >/dev/null; then
    exit 0
fi

SUDO=""
[ "$(id -u)" -eq 0 ] || SUDO="sudo"

$SUDO apt-get update -qq
DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y -qq --no-install-recommends \
    build-essential cmake ninja-build ccache pkg-config \
    libasound2-dev libjack-jackd2-dev libfreetype6-dev \
    libx11-dev libxext-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxi-dev libgl1-mesa-dev \
    libcurl4-openssl-dev

python3 -m pip install -q numpy scipy 2>/dev/null || true
