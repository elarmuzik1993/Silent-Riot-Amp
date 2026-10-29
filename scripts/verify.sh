#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Boris Miscenco
# The one verification command for this repo. Same checks locally, in CI and in cloud sessions.
#
#   bash scripts/verify.sh           full run: static checks + the CI `test` job
#                                    (Release configure with tests, build, ctest)
#   bash scripts/verify.sh --quick   SessionStart subset: static checks only, no build,
#                                    no network, silent when clean
#
# Exit 0 when everything passes, non-zero otherwise.
# The full run builds into build-verify/ so it never clobbers build/ (scripts/build.sh).

set -u
cd "$(git rev-parse --show-toplevel)"

QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1

fail=0
say()  { [ "$QUICK" -eq 1 ] || echo "$@"; }
bad()  { echo "FAIL: $*"; fail=1; }

# ── Static checks (both modes) ──────────────────────────────────────────

# 1. Every .cpp under src/ is compiled into the plugin. An unlisted file is
#    dead code at best and an undefined symbol at link time at worst.
plugin_srcs=$(grep -oE 'src/[A-Za-z0-9_/]+\.cpp' CMakeLists.txt | sort -u)
for f in $(find src -name '*.cpp' | sort); do
    grep -qx "$f" <<<"$plugin_srcs" || bad "$f is not listed in CMakeLists.txt"
done

# 2. sr_render's source list is a hand-copy of the plugin's (tools/README.md:
#    "if you add a plugin .cpp to the root CMakeLists.txt, add it here too or
#    the link fails"). Both lists are read from the files on every run.
# sr_editor_snapshot links the same full plugin list, so it is held to it too.
for tool in sr_render sr_editor_snapshot; do
    tool_srcs=$(sed -n "/^target_sources(${tool} /,/^)/p" tools/CMakeLists.txt \
                | grep -oE 'src/[A-Za-z0-9_/]+\.cpp' | sort -u)
    if [ -z "$tool_srcs" ]; then
        bad "could not find ${tool}'s source list in tools/CMakeLists.txt"
        continue
    fi
    for f in $plugin_srcs; do
        grep -qx "$f" <<<"$tool_srcs" || bad "$f is in CMakeLists.txt but missing from ${tool} in tools/CMakeLists.txt"
    done
done

# 3. Shell scripts parse.
for f in scripts/*.sh .agents/*.sh; do
    [ -f "$f" ] || continue
    bash -n "$f" 2>/dev/null || bad "$f has a shell syntax error"
done

# 4. The sign-off gate table is valid JSON (tools/signoff.py reads it).
if command -v python3 >/dev/null; then
    python3 -c 'import json,sys; json.load(open(sys.argv[1]))' tools/signoff_gates.json 2>/dev/null \
        || bad "tools/signoff_gates.json is not valid JSON"
fi

# 5. Every hand-written source file carries its licence tag (CREDITS.md,
#    GPL-3.0-or-later) in its first 3 lines, after any shebang.
while IFS= read -r f; do
    head -n 3 "$f" | grep -q 'SPDX-License-Identifier: GPL-3.0-or-later' \
        || bad "$f has no 'SPDX-License-Identifier: GPL-3.0-or-later' in its first 3 lines"
done < <(git ls-files 'src/*.h' 'src/*.cpp' 'tests/*.h' 'tests/*.cpp' 'tools/*.cpp' 'tools/*.py' \
                      'scripts/*.sh' 'scripts/*.py' 'docs/*.cpp' '.agents/*.sh')

# 6. Every repo path the docs and code comments name exists: Markdown links and
#    backticked paths in *.md, docs/... mentions in src/, tests/, tools/.
#    Known non-file mentions: scripts/linkcheck-allow.txt.
if command -v python3 >/dev/null; then
    while IFS= read -r line; do
        bad "$line"
    done < <(python3 scripts/check_links.py)
fi

# 7. Every non-ASCII character in a src/ string literal can render: spelled as
#    UTF-8 inside juce::CharPointer_UTF8 (a raw literal turns to mojibake) and
#    present in the embedded fonts (a missing glyph draws as an empty box).
if command -v python3 >/dev/null; then
    while IFS= read -r line; do
        bad "$line"
    done < <(python3 scripts/check_text.py)
fi

say "static checks: $([ $fail -eq 0 ] && echo pass || echo FAIL)"
[ "$QUICK" -eq 1 ] && exit $fail

# ── Full: the CI `test` job (.github/workflows/build.yml) ───────────────
BUILD_DIR=build-verify
LOG=$BUILD_DIR/verify
mkdir -p "$BUILD_DIR"
say "configure ($BUILD_DIR, Release, tests on)"
cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSR_BUILD_TESTS=ON >"$LOG-configure.log" 2>&1 \
    || { bad "configure failed; see $LOG-configure.log"; exit 1; }
say "build"
cmake --build "$BUILD_DIR" -j"$(nproc)" >"$LOG-build.log" 2>&1 \
    || { bad "build failed; see $LOG-build.log"; tail -n 30 "$LOG-build.log"; exit 1; }
say "test"
ctest --test-dir "$BUILD_DIR" --output-on-failure >"$LOG-test.log" 2>&1 \
    || { bad "ctest failed; see $LOG-test.log"; tail -n 40 "$LOG-test.log"; exit 1; }
tail -n 3 "$LOG-test.log"

[ $fail -eq 0 ] && echo "verify: all checks passed" || echo "verify: FAILED"
exit $fail
