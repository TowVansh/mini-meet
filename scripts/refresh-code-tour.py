"""Refresh the line numbers in docs/CODE_TOUR.md after code changes.

    python3 scripts/refresh-code-tour.py

Rewrites every "`path:N` `function`" pair so N is the line where that
function is defined now. Pairs it cannot match are left alone and listed.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOUR = ROOT / "docs" / "CODE_TOUR.md"
PAIR = re.compile(r"`(src/[\w/.]+):(\d+)` `(\w+)`")


def def_line(path, name):
    # a definition: the name followed by "(" on a line that does not end in ";" (not a prototype or call)
    pat = re.compile(rf"^[A-Za-z].*\b{name}\s*\(")
    for i, line in enumerate((ROOT / path).read_text(errors="replace").splitlines(), 1):
        if pat.match(line) and not line.rstrip().endswith(";"):
            return i
    return None


def main():
    text = TOUR.read_text()
    missed = []

    def fix(m):
        line = def_line(m[1], m[3])
        if line is None:
            missed.append(f"{m[1]} {m[3]}")
            return m[0]
        return f"`{m[1]}:{line}` `{m[3]}`"

    TOUR.write_text(PAIR.sub(fix, text))
    print("updated", TOUR, "| not found:", ", ".join(missed) or "none")


if __name__ == "__main__":
    main()
