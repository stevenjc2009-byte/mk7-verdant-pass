"""Draws the install QR code for a release, and reads it back.

FBI's Remote Install scans this off a screen or a phone, through the 3DS's
low-resolution camera. Two things follow from that:

  * Error correction stays at L. Higher levels add modules, and a denser code
    is harder for that camera than a less redundant one.
  * Each module is drawn as a block of whole pixels with a wide quiet zone,
    rather than scaled afterwards, so no module lands on a half pixel.

The read-back at the end is the point of the script. A QR code that encodes the
wrong URL looks exactly like one that encodes the right one, and the only way to
know is to decode it - which is how a release once shipped a code pointing at a
404.

    python tools/make_qr.py 1.1.0
"""

from __future__ import annotations

import sys
from pathlib import Path

import cv2
import qrcode
from qrcode.constants import ERROR_CORRECT_L

ROOT = Path(__file__).resolve().parent.parent
REPO = "https://github.com/stevenjc2009-byte/mk7-verdant-pass"


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    version = sys.argv[1].lstrip("v")
    url = f"{REPO}/releases/download/v{version}/verdantpass{version}.cia"
    out = ROOT / "docs" / "install-qr.png"

    qr = qrcode.QRCode(version=None, error_correction=ERROR_CORRECT_L,
                       box_size=8, border=4)
    qr.add_data(url)
    qr.make(fit=True)
    qr.make_image(fill_color="black", back_color="white").save(out)

    # Read it back with a different library than the one that wrote it.
    img = cv2.imread(str(out))
    decoded, _, _ = cv2.QRCodeDetector().detectAndDecode(img)

    print(f"{out}  {img.shape[1]}x{img.shape[0]}")
    print(f"  encoded: {url}")
    print(f"  decoded: {decoded or '(nothing)'}")

    if decoded != url:
        print("  FAIL - the code does not read back as the URL")
        return 1

    print("  OK - reads back exactly")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
