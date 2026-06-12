#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Complain about non-ASCII punctuation on staged added lines, so that
typographic dashes, quotes and suchlike don't land in the sources (see
CONTRIBUTING.md "Coding style"). Non-ASCII letters and symbols are fine,
and so are Markdown files.

Use it as a pre-commit hook:
    ln -sf "$(git rev-parse --show-toplevel)/scripts/check-ascii.py" \
           "$(git rev-parse --git-path hooks)/pre-commit"
or call it from an existing one.
"""

import subprocess
import sys
import unicodedata

diff = subprocess.run(
    ["git", "diff", "--cached", "--unified=0", "--diff-filter=ACMR",
     "--", ".", ":(exclude)*.md"],
    capture_output=True, check=True,
).stdout.decode("utf-8", "replace")

bad = False
path = None
line = 0

for l in diff.splitlines():
    if l.startswith("+++ b/"):
        path = l[6:]
    elif l.startswith("@@"):
        line = int(l.split()[2].split(",")[0][1:])
    elif l.startswith("+") and not l.startswith("+++"):
        text = l[1:]
        hits = [ch for ch in text
                if ord(ch) > 127 and unicodedata.category(ch).startswith("P")]
        if hits:
            if not bad:
                print("non-ASCII punctuation in staged changes:", file=sys.stderr)
            names = ", ".join(f"U+{ord(ch):04X} {unicodedata.name(ch, '?')}"
                              for ch in dict.fromkeys(hits))
            print(f"{path}:{line}: {text} ({names})", file=sys.stderr)
            bad = True
        line += 1

sys.exit(1 if bad else 0)
