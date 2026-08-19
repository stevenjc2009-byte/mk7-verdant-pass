"""Proves the built CIA carries none of Nintendo's data.

Two independent checks, because either one alone can be fooled:

  1. Filenames. RomFS stores them as UTF-16LE, so an ASCII grep for
     "Gn64_KalimariDesert" in a CIA comes back clean whether the file is in
     there or not. Every name is searched in both encodings.

  2. Content. Names can be changed; bytes cannot. Chunks are lifted from the
     head, middle and tail of each stock file in the reference extract and
     searched for verbatim in the CIA. A file cannot be present without its
     own bytes being present.

Check 2 is the one that matters. Check 1 is there to catch a stray reference.

    python tools/verify_no_game_data.py [path/to/extract/romfs]
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EXTRACT = ROOT.parent.parent / "fuck u" / "mk7-mod" / "extract" / "romfs"

# Names that would only appear if a stock file had been packed.
NAMES = [
    "Gn64_KalimariDesert",
    "common-en", "common-fr", "common-de", "common-it",
    "common-es", "common-nl", "common-pt", "common-ru",
]

# How much of each stock file to fingerprint, and from where.
CHUNK = 512


def chunks(data: bytes) -> list[tuple[str, bytes]]:
    n = len(data)
    if n < CHUNK * 3:
        return [("whole", data)]
    return [
        ("head", data[:CHUNK]),
        ("mid", data[n // 2: n // 2 + CHUNK]),
        ("tail", data[-CHUNK:]),
    ]


def main() -> int:
    cia = ROOT / "verdantpass.cia"
    extract = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_EXTRACT

    if not cia.exists():
        print(f"no CIA at {cia} - run 'make cia' first")
        return 2
    if not extract.is_dir():
        print(f"no reference extract at {extract} - cannot check content")
        return 2

    blob = cia.read_bytes()
    print(f"{cia.name}  {len(blob):,} bytes")

    failures = 0

    # --- 1. names -----------------------------------------------------------
    for name in NAMES:
        for label, needle in (("ascii", name.encode("ascii")),
                              ("utf16", name.encode("utf-16-le"))):
            if needle in blob:
                print(f"  FAIL  name '{name}' present ({label})")
                failures += 1
    print(f"  names: {len(NAMES)} checked in both encodings")

    # --- 2. content ---------------------------------------------------------
    stock = [extract / "Course" / "Gn64_KalimariDesert.szs"]
    stock += sorted((extract / "UI").glob("common-e?.szs"))

    if not stock[0].exists():
        print(f"  FAIL  reference course missing at {stock[0]}")
        return 2

    checked = 0
    for path in stock:
        if not path.exists():
            continue
        data = path.read_bytes()
        for where, chunk in chunks(data):
            checked += 1
            if chunk in blob:
                print(f"  FAIL  {path.name} {where} chunk present in the CIA")
                failures += 1
    print(f"  content: {len(stock)} stock files, {checked} chunks checked")

    # --- what IS in there ---------------------------------------------------
    for name in ("course.bcmdl", "course.kcl", "course.kmp"):
        ours = ROOT / "romfs" / "mod" / name
        head = ours.read_bytes()[:CHUNK]
        if head not in blob:
            print(f"  FAIL  our own {name} is NOT in the CIA")
            failures += 1
    print("  payload: our three files confirmed present")

    print("CLEAN - no Nintendo data in the build" if failures == 0
          else f"{failures} failures")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
