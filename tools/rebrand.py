#!/usr/bin/env python3
"""Rebrand the forked Muse Gadget SDK firmware to AICoworker Gadgets (Dou / AICW).

Idempotent: run it again after merging upstream changes.

  python3 tools/rebrand.py [--dry] [root]

Rules, in order:
  1. Lines that carry a licence notice (Copyright / SPDX / "Licensed under") are
     kept as they are: Apache-2.0 requires the original notices to stay.
  2. Upstream cloud addresses (api.muse.ai, gadgets.muse.ai, *.meta.ai,
     *.metaaivm.com, muse.ai) are kept: they name real services, not our brand.
  3. Phrases about the app or the server behind the device become AICW:
     "Muse app" -> "AICW app", "app Muse" -> "app AICW", "Muse server" -> "AICW server" ...
  4. Meta / META -> AICW (whole word only: metadata, <meta>, MetaModifier stay).
  5. muse / Muse / MUSE -> dou / Dou / DOU everywhere else, including inside
     identifiers (muse_ui_start -> dou_ui_start, CONFIG_MUSE_X -> CONFIG_DOU_X).
  6. File and directory names get rule 5 too.
"""
import os
import re
import sys

SKIP_DIRS = {".git", "build", "managed_components", "node_modules", "__pycache__"}
BINARY_EXT = {".gif", ".png", ".jpg", ".jpeg", ".bin", ".pte", ".mp3", ".wav", ".ttf", ".otf", ".woff",
              ".woff2", ".ico", ".pdf", ".zip", ".a", ".o", ".webp", ".tflite", ".der", ".pem"}
KEEP_LINE = re.compile(r"Copyright|SPDX-License|Licensed under|licenses/LICENSE|Meta Platforms")
# Placeholders survive the case-mapping below untouched.
PROTECT = [
    re.compile(r"[A-Za-z0-9.-]*\b(?:muse\.ai|meta\.ai|metaaivm\.com)\b"),
    re.compile(r"facebookincubator/muse-gadget-sdk"),
]
AICW_PHRASES = [
    (r"\b(?:Muse|Dou)'s server\b", "AICW server"),
    (r"\b(?:the )?(?:Muse|Dou) app\b", lambda m: ("the " if m.group(0).startswith("the ") else "") + "AICW app"),
    (r"\b(?:Muse|Dou) server\b", "AICW server"),
    (r"\b(?:MUSE|DOU) APP\b", "AICW APP"),
    (r"\bapp (?:Muse|Dou)\b", "app AICW"), (r"\bApp (?:Muse|Dou)\b", "App AICW"),
    (r"\bapp do Muse\b", "app do AICW"),
    (r"\bServidor do (?:Muse|Dou)\b", "Servidor do AICW"),
    (r"\b(?:Muse|Dou) Gadgets Test\b", "AICW Gadgets Test"),
    (r"\bMuse VM\b", "AICW VM"),
    (r"\bMuse account\b", "AICW account"),
]
META = [(re.compile(r"\bMeta\b"), "AICW"), (re.compile(r"\bMETA\b"), "AICW")]
CASES = [(re.compile(r"MUSE"), "DOU"), (re.compile(r"Muse"), "Dou"), (re.compile(r"muse"), "dou")]


def map_line(line):
    if KEEP_LINE.search(line):
        return line
    keep = []

    def stash(m):
        keep.append(m.group(0))
        return f"\x00{len(keep) - 1}\x00"

    for rx in PROTECT:
        line = rx.sub(stash, line)
    for pat, rep in AICW_PHRASES:
        line = re.sub(pat, rep, line)
    for rx, rep in META:
        line = rx.sub(rep, line)
    for rx, rep in CASES:
        line = rx.sub(rep, line)
    return re.sub(r"\x00(\d+)\x00", lambda m: keep[int(m.group(1))], line)


def rename(name):
    for rx, rep in CASES:
        name = rx.sub(rep, name)
    return name


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry = "--dry" in sys.argv
    root = args[0] if args else "."
    changed = renamed = 0
    for d, dirs, files in os.walk(root, topdown=True):
        dirs[:] = [x for x in dirs if x not in SKIP_DIRS and not x.startswith("build-")]
        for f in files:
            p = os.path.join(d, f)
            if os.path.splitext(f)[1].lower() in BINARY_EXT or os.path.islink(p):
                continue
            try:
                text = open(p, encoding="utf-8").read()
            except (UnicodeDecodeError, OSError):
                continue
            new = "".join(map_line(l) for l in text.splitlines(keepends=True))
            if new != text:
                changed += 1
                if not dry:
                    open(p, "w", encoding="utf-8").write(new)
    # rename deepest first
    for d, dirs, files in os.walk(root, topdown=False):
        if any(part in SKIP_DIRS or part.startswith("build-") for part in d.split(os.sep)):
            continue
        for n in files + dirs:
            nn = rename(n)
            if nn != n:
                renamed += 1
                if not dry:
                    os.rename(os.path.join(d, n), os.path.join(d, nn))
    print(f"files changed: {changed}, names changed: {renamed}{' (dry run)' if dry else ''}")


if __name__ == "__main__":
    main()
