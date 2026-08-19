"""Reads the C engine's output back with the project's Python tools.

This crosses the implementation boundary on purpose. The C test can only tell
you that C agrees with C: it decompresses its own compressed stream with its own
decompressor, so a matched pair of bugs would pass. Here the .szs files the C
engine wrote are opened by yaz0.py, sarc.py and msbt.py - none of which have
ever seen the C code - and the course name is read out of the result.

    python test/check_output.py <mk7-mod-root> <out-dir> <ref-dir>
"""

from __future__ import annotations

import sys
from pathlib import Path

NAME_INDEX = 129
COURSE_NAME = "Verdant Pass"
HASH_MSBT = 0x1DAA5659
UI_COUNT = 8

failures = 0


def check(what: str, ok: bool) -> None:
    global failures
    if not ok:
        failures += 1
        print(f"  FAIL  {what}")


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(__doc__)
        return 2

    root, out, ref = Path(argv[1]), Path(argv[2]), Path(argv[3])
    sys.path.insert(0, str(root / "tools"))
    import msbt  # noqa: E402
    import sarc  # noqa: E402
    import yaz0  # noqa: E402

    print("cross-check (python reading C's output)")

    # The course. C compressed it; Python has to get the archive back out.
    szs = (out / "course.szs").read_bytes()
    raw = yaz0.decompress(szs)
    check("python decompresses C's course stream to C's own archive",
          raw == (out / "course.sarc").read_bytes())
    check("and it matches the archive the PC toolchain built",
          raw == (ref / "built_course.sarc").read_bytes())

    entries = sarc.read_sarc(raw)
    check("course archive has 15 entries", len(entries) == 15)

    # The UI files, one per language. The name is what the player reads on the
    # cup screen, so it is checked in every language rather than in English.
    for i in range(UI_COUNT):
        szs = (out / f"ui_{i}.szs").read_bytes()
        raw = yaz0.decompress(szs)
        check(f"ui {i}: python decompresses C's stream",
              raw == (out / f"ui_{i}.sarc").read_bytes())
        check(f"ui {i}: matches the PC toolchain's archive",
              raw == (ref / f"built_ui_{i}.sarc").read_bytes())

        table = None
        for name, data in sarc.read_sarc(raw):
            if int(name.split(".")[0], 16) == HASH_MSBT:
                table = data
                break
        if table is None:
            check(f"ui {i}: name table present", False)
            continue

        strings = msbt.parse(table)["strings"]
        check(f"ui {i}: name reads back as {COURSE_NAME!r}",
              strings[NAME_INDEX] == COURSE_NAME)

        stock = msbt.parse(
            next(d for n, d in sarc.read_sarc((ref / f"stock_ui_{i}.sarc").read_bytes())
                 if int(n.split(".")[0], 16) == HASH_MSBT))["strings"]
        check(f"ui {i}: string count unchanged", len(strings) == len(stock))
        others = sum(1 for k in range(len(stock))
                     if k != NAME_INDEX and strings[k] != stock[k])
        check(f"ui {i}: no other string touched", others == 0)

    print(f"{'0 failed' if failures == 0 else str(failures) + ' failed'}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
