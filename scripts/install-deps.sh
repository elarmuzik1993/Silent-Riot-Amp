#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Boris Miscenco
set -euo pipefail
echo "Installing Ubuntu build deps for SilentRiot..."
sudo apt-get update && sudo apt-get install -y \
  build-essential cmake ninja-build ccache git pkg-config \
  libasound2-dev libjack-jackd2-dev libfreetype6-dev \
  libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libgl1-mesa-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev ladspa-sdk
echo "Done!"
