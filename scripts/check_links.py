#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Boris Miscenco
"""Check that repo-relative paths named in the docs and code comments exist.

Scans:
  * Markdown links [text](path) in tracked *.md files,
  * backticked repo paths in tracked *.md files (`src/...`, `docs/...`, `CREDITS.md`, ...),
  * docs/... paths mentioned in src/, tests/ and tools/.

A `file:line` suffix is stripped. Globs, build directories and URLs are ignored.
Known non-file mentions (branch names, build outputs, files a plan has yet to
create) live in scripts/linkcheck-allow.txt, one per line:
    <source path or fnmatch glob> <referenced path>
An entry whose path has since been created is reported as stale.
Prints one line per problem; exit 1 if any.
"""
import fnmatch
import os
import re
import subprocess
import sys

ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                      capture_output=True, text=True, check=True).stdout.strip()
os.chdir(ROOT)

TOP_DIRS = ("src/", "tests/", "tools/", "scripts/", "docs/", "resources/", ".github/", ".agents/")
MD_LINK = re.compile(r"\[[^\]]*\]\((<[^>]+>|[^)\s]+)(?:\s+\"[^\"]*\")?\)")
BACKTICK = re.compile(r"`([^`\n]+)`")
# A docs/ path in code; the DSP spec's file name has spaces, so name it explicitly.
CODE_DOC = re.compile(r"docs/(?:Silent Riot DSP Model Spec v[\d.]+\.md|[A-Za-z0-9_./-]+)")
LINE_SUFFIX = re.compile(r"(:\d+(?:[-,]\d+)*)+$")


def tracked(*patterns):
    out = subprocess.run(["git", "ls-files", "--", *patterns],
                         capture_output=True, text=True, check=True).stdout
    return [p for p in out.splitlines() if p]


def load_allow():
    allow = []
    path = "scripts/linkcheck-allow.txt"
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            line = line.split("#", 1)[0].strip()
            if line:
                src, ref = line.split(None, 1)
                allow.append((src, ref.strip()))
    return allow


def clean(ref):
    ref = ref.strip().rstrip(".,;)")
    ref = LINE_SUFFIX.sub("", ref)
    return ref


def ignorable(ref):
    return (not ref
            or any(c in ref for c in "*{}<>$")
            or re.match(r"^[a-z]+:", ref)          # http:, mailto:, ...
            or ref.startswith("#")
            or re.match(r"^(\./)?build[^/]*/", ref))


def exists(ref, base_dir):
    ref = ref.split("#", 1)[0]
    if not ref:
        return True
    # `src/gui/EffectsPanel.h/.cpp` names a header/source pair.
    pair = re.fullmatch(r"(.+)\.(\w+)/\.(\w+)", ref)
    if pair:
        stem, a, b = pair.groups()
        return exists(f"{stem}.{a}", base_dir) and exists(f"{stem}.{b}", base_dir)
    cands = [os.path.normpath(os.path.join(base_dir, ref)), os.path.normpath(ref)]
    return any(os.path.exists(c) for c in cands)


def looks_like_repo_path(s):
    if s.startswith(TOP_DIRS):
        return True
    return re.fullmatch(r"[A-Za-z_]+\.md", s) is not None


def main():
    allow = load_allow()
    missing = []

    def report(src, ref):
        if any(fnmatch.fnmatch(src, a_src) and ref == a_ref for a_src, a_ref in allow):
            return
        missing.append((src, ref))

    for md in tracked("*.md"):
        base = os.path.dirname(md)
        text = open(md, encoding="utf-8").read()
        for m in MD_LINK.finditer(text):
            ref = m.group(1).strip("<>")
            ref = LINE_SUFFIX.sub("", ref.split("#", 1)[0])
            if ignorable(ref) or not ref:
                continue
            ref = ref.replace("%20", " ")
            if not exists(ref, base):
                report(md, ref)
        for m in BACKTICK.finditer(text):
            raw = m.group(1).strip()
            if not looks_like_repo_path(raw):
                continue
            ref = clean(raw)
            if ignorable(ref) or exists(ref, ""):
                continue
            # A command line (`tools/signoff.py --render-bin ...`): check its first word.
            first = clean(ref.split()[0]) if " " in ref else ref
            if first != ref and (ignorable(first) or exists(first, "")):
                continue
            report(md, ref)

    for code in tracked("src/*", "tests/*", "tools/*"):
        if code.endswith((".md", ".wav", ".raw", ".ttf", ".png")):
            continue
        try:
            text = open(code, encoding="utf-8").read()
        except UnicodeDecodeError:
            continue
        for m in CODE_DOC.finditer(text):
            ref = clean(m.group(0))
            if ignorable(ref) or exists(ref, ""):
                continue
            report(code, ref)

    # An allowlisted path that now exists is stale: drop its line.
    stale = [(a_src, a_ref) for a_src, a_ref in allow if exists(a_ref, "")]

    for src, ref in sorted(set(missing)):
        print(f"{src}: missing path `{ref}`")
    for a_src, a_ref in stale:
        print(f"scripts/linkcheck-allow.txt: `{a_ref}` now exists; remove its entry ({a_src})")
    return 1 if missing or stale else 0


if __name__ == "__main__":
    sys.exit(main())
