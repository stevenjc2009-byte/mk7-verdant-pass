"""Builds the reference files the patch-engine test compares itself against.

The C engine on the console has to produce the same archive the PC toolchain
produced when the track was built - that archive is the one that was actually
looked at and driven around. So the references here are decompressed with the
project's own yaz0.py, which was verified against 3dstool, and the C output is
held against them byte for byte.

Nothing is written outside the reference directory, and mk7-mod is only read.

    python test/prep_reference.py <mk7-mod-root> <ref-dir>
"""

from __future__ import annotations

import sys
from pathlib import Path

UI_FILES = ["ed", "ee", "ef", "ei", "en", "ep", "er", "es"]


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 2

    root = Path(argv[1])
    ref = Path(argv[2])
    sys.path.insert(0, str(root / "tools"))
    import yaz0  # noqa: E402  - the path has to be set up first

    ref.mkdir(parents=True, exist_ok=True)

    jobs = [
        (root / "extract/romfs/Course/Gn64_KalimariDesert.szs", "stock_course.sarc"),
        (root / "build/Gn64_KalimariDesert.szs", "built_course.sarc"),
    ]
    for i, lang in enumerate(UI_FILES):
        jobs.append((root / f"extract/romfs/UI/common-{lang}.szs", f"stock_ui_{i}.sarc"))
        jobs.append((root / f"build/UI/common-{lang}.szs", f"built_ui_{i}.sarc"))

    for src, name in jobs:
        if not src.is_file():
            print(f"missing: {src}")
            return 1
        raw = yaz0.decompress(src.read_bytes())
        (ref / name).write_bytes(raw)

    print(f"reference: {len(jobs)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
