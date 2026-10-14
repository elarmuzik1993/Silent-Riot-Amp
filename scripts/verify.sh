#!/usr/bin/env bash
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
render_srcs=$(sed -n '/^target_sources(sr_render/,/^)/p' tools/CMakeLists.txt \
              | grep -oE 'src/[A-Za-z0-9_/]+\.cpp' | sort -u)
if [ -z "$render_srcs" ]; then
    bad "could not find sr_render's source list in tools/CMakeLists.txt"
else
    for f in $plugin_srcs; do
        grep -qx "$f" <<<"$render_srcs" || bad "$f is in CMakeLists.txt but missing from sr_render in tools/CMakeLists.txt"
    done
fi

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
