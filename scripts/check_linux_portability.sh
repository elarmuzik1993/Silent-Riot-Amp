#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Boris Miscenco
# The Linux binaries must run where v0.4.0 runs: any x86-64 CPU, and glibc 2.39
# (Ubuntu 24.04). GCC 15 on Ubuntu 26.04 (GitHub's ubuntu-latest since
# 2026-11) defaults to -march=x86-64-v3, so a build there emits AVX2/FMA
# instructions and dies with SIGILL on older CPUs. CI's build-linux job and
# release.yml run this on what they built; it fails on any VEX-encoded (AVX or
# later) instruction, or a glibc symbol newer than MAX_GLIBC.
#
#   bash scripts/check_linux_portability.sh build/SilentRiot_artefacts/Release

set -u
dir=${1:?usage: check_linux_portability.sh <SilentRiot_artefacts/Release dir>}
MAX_GLIBC=2.39

fail=0
found=0
while IFS= read -r -d '' bin; do
    file -b "$bin" | grep -q '^ELF' || continue
    found=$((found + 1))
    # objdump -d lines look like "  1234:\tvmovss %xmm0,...": a mnemonic starting
    # with v is VEX-encoded, which baseline x86-64 (SSE2) never emits.
    avx=$(objdump -d --no-show-raw-insn "$bin" | grep -cE '^ *[0-9a-f]+:[[:space:]]+v[a-z0-9]+[[:space:]]')
    glibc=$(objdump -T "$bin" | grep -oE 'GLIBC_[0-9]+\.[0-9]+(\.[0-9]+)?' | sed 's/GLIBC_//' | sort -uV | tail -n 1)
    echo "$bin: ${avx} AVX+ instructions, needs glibc ${glibc:-none}"
    if [ "$avx" -ne 0 ]; then
        echo "FAIL: $bin has AVX or later instructions: build for baseline x86-64 (Ubuntu 24.04 runner)"
        fail=1
    fi
    if [ -n "$glibc" ] && [ "$(printf '%s\n%s\n' "$glibc" "$MAX_GLIBC" | sort -V | tail -n 1)" != "$MAX_GLIBC" ]; then
        echo "FAIL: $bin needs glibc $glibc, newer than $MAX_GLIBC"
        fail=1
    fi
done < <(find "$dir" -type f \( -name '*.so' -o -perm -u+x \) -print0)

if [ "$found" -eq 0 ]; then
    echo "FAIL: no ELF binaries under $dir"
    exit 1
fi
exit $fail
