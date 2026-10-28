#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Boris Miscenco
"""Check that every non-ASCII character the plugin puts on screen can render.

Two ways text has gone out broken:
  * a raw non-ASCII literal ("Gain — drive") handed to juce::String reads as
    ASCII, so the UTF-8 bytes show up as mojibake ("Gain â□□ drive"). Spell it
    with \\x escapes inside juce::CharPointer_UTF8(...).
  * a character the embedded fonts (resources/fonts/*.ttf) lack draws as an
    empty box: JUCE does not fall back to a system font for an embedded
    typeface. The preset arrows and the tuner's flat/sharp did this.

Scans string literals (not comments) in src/. Fails on any raw non-ASCII byte,
on a \\x-escaped UTF-8 literal outside CharPointer_UTF8/fromUTF8, and on any
character not in every bundled font. Prints one line per problem; exit 1 if any.
"""
import glob
import os
import re
import struct
import subprocess
import sys

ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                      capture_output=True, text=True, check=True).stdout.strip()
os.chdir(ROOT)

UTF8_WRAPPERS = ("CharPointer_UTF8(", "fromUTF8(")


def font_codepoints(path):
    """The set of code points a TrueType font maps (cmap formats 4 and 12)."""
    data = open(path, "rb").read()
    num_tables = struct.unpack_from(">H", data, 4)[0]
    cmap = None
    for i in range(num_tables):
        tag, _, offset, _ = struct.unpack_from(">4sIII", data, 12 + 16 * i)
        if tag == b"cmap":
            cmap = offset
    if cmap is None:
        return set()
    points = set()
    for i in range(struct.unpack_from(">H", data, cmap + 2)[0]):
        _, _, sub = struct.unpack_from(">HHI", data, cmap + 4 + 8 * i)
        sub += cmap
        fmt = struct.unpack_from(">H", data, sub)[0]
        if fmt == 4:
            segs = struct.unpack_from(">H", data, sub + 6)[0] // 2
            ends = struct.unpack_from(f">{segs}H", data, sub + 14)
            starts = struct.unpack_from(f">{segs}H", data, sub + 16 + 2 * segs)
            for s, e in zip(starts, ends):
                if s != 0xFFFF:
                    points.update(range(s, e + 1))
        elif fmt == 12:
            for g in range(struct.unpack_from(">I", data, sub + 12)[0]):
                s, e, _ = struct.unpack_from(">III", data, sub + 16 + 12 * g)
                points.update(range(s, e + 1))
    return points


def string_literals(src):
    """Yield (line, start offset, raw body) for each "..." literal, skipping
    comments and char literals. Raw strings (R"(...)") are not used in src/."""
    i, line, n = 0, 1, len(src)
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
        elif src.startswith("//", i):
            i = src.find("\n", i)
            if i < 0:
                return
            continue
        elif src.startswith("/*", i):
            end = src.find("*/", i + 2)
            line += src.count("\n", i, end)
            i = end + 2
            continue
        elif c in "\"'":
            j = i + 1
            while src[j] != c:
                j += 2 if src[j] == "\\" else 1
            if c == '"':
                yield line, i, src[i + 1:j]
            i = j
        i += 1


def decode_escapes(body):
    """The bytes a C++ narrow literal body denotes (only \\x and simple escapes)."""
    out, i = bytearray(), 0
    while i < len(body):
        if body[i] == "\\":
            m = re.match(r"x([0-9A-Fa-f]+)", body[i + 1:])
            if m:
                out.append(int(m.group(1), 16) & 0xFF)
                i += 1 + len(m.group(0))
                continue
            out += body[i + 1].encode()
            i += 2
            continue
        out += body[i].encode()
        i += 1
    return bytes(out)


fonts = sorted(glob.glob("resources/fonts/*.ttf"))
covered = set.intersection(*(font_codepoints(f) for f in fonts)) if fonts else set()
problems = []

for path in sorted(glob.glob("src/**/*.[ch]*", recursive=True)):
    src = open(path, encoding="utf-8").read()
    for line, start, body in string_literals(src):
        where = f"{path}:{line}"
        if any(ord(ch) > 0x7F for ch in body):
            problems.append(f"{where}: raw non-ASCII in a string literal; juce::String reads it "
                            "as ASCII. Use \\x escapes inside juce::CharPointer_UTF8(...)")
            continue
        raw = decode_escapes(body)
        if all(b <= 0x7F for b in raw):
            continue
        if not src[:start].rstrip().endswith(UTF8_WRAPPERS):
            problems.append(f"{where}: UTF-8 escapes outside juce::CharPointer_UTF8(...)")
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            problems.append(f"{where}: escaped bytes are not valid UTF-8")
            continue
        for ch in sorted({ch for ch in text if ord(ch) > 0x7F}):
            if ord(ch) not in covered:
                problems.append(f"{where}: U+{ord(ch):04X} is missing from the embedded fonts "
                                "and would draw as an empty box")

for p in problems:
    print(p)
sys.exit(1 if problems else 0)
