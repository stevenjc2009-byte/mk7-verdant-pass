# Verdant Pass

A custom Mario Kart 7 track, shipped as a 3DS app that installs it.

Run the app once and it writes the track onto your SD card as a mod that
[Hotswap](https://github.com/stevenjc2009-byte/Hotswap) can switch on and off.
Stock Mario Kart 7 keeps working exactly as it did; you swap to Verdant Pass
when you want it and back to Stock when you don't.

The track replaces **Kalimari Desert** in the Lightning Cup, and renames it in
all eight European languages.

## Install

<img src="docs/install-qr.png" width="220" alt="Install QR code">

Scan that with FBI's *Remote Install → Scan QR Code*, or download
[`verdantpass1.0.0.cia`](https://github.com/stevenjc2009-byte/mk7-verdant-pass/releases/download/v1.0.0/verdantpass1.0.0.cia)
and install it with FBI by hand.

## Using it

1. **Open Verdant Pass** from the HOME menu and press **A**. It writes the
   track to `sdmc:/hotswap/mk7/Verdant Pass/`.
2. **Open Hotswap**, pick Mario Kart 7, and choose **Verdant Pass**.
3. **Launch Mario Kart 7** and go to the Lightning Cup.

To go back to normal, open Hotswap and switch Mario Kart 7 to **Stock**.

### You need

- A 3DS with **Luma3DS** and custom firmware.
- **Luma3DS game patching turned on** — hold SELECT on boot, and enable
  *Enable game patching*. Without it Luma ignores the swapped-in files and you
  get stock Kalimari Desert with nothing to say why.
- **Hotswap**, which is what actually swaps the track in and out.
- A legitimately installed copy of Mario Kart 7.

### Updates

Press **Y** in the app to check GitHub for a newer version. If there is one it
downloads and installs it, then relaunches.

The track lives inside the app, so a new track version *is* a new app version:
after updating, press **A** again to write the newer track to the card.

The check needs an internet connection and only works on the installed CIA —
the `.3dsx` build has no way to install a title, so the button is greyed out
there.

## If it refuses to install

> *Verdant Pass is swapped in right now. Open Hotswap, switch Mario Kart 7 back
> to Stock, then run this again.*

That is the app declining to fight Hotswap for the files. While the mod is
swapped in, Hotswap has renamed its folder away into `/luma/titles/`, and
writing a fresh copy underneath would leave two of them and a state file that
disagrees with the disk. Swap back to Stock first.

## Building it

Needs [devkitPro](https://devkitpro.org/) with `3ds-dev`, plus `3ds-curl` and
`3ds-mbedtls` for the updater.

```bash
make        # verdantpass.3dsx
make cia    # verdantpass.cia
```

Build in the devkitPro MSYS2 shell. `-Werror` is on, so a warning is a failed
build.

### Tests

The install engine is plain POSIX file IO, so it compiles and runs on the host
against a throwaway directory:

```bash
sh test/run.sh
```

That covers the recursive walk, `mkdir -p`, the copy, the size read-back, and
reading Hotswap's state file. It does not cover the console front end or the
updater — those need a 3DS.

## What is in the app

- `romfs/mod/Course/Gn64_KalimariDesert.szs` — the track: model, collision and
  course layout.
- `romfs/mod/UI/common-e?.szs` — the eight language files, each with the course
  name changed to *Verdant Pass*.
- `romfs/cacert.pem` — the CA bundle the update check needs. The console's own
  root store predates every certificate authority in use today.

## Legal

This is a fan-made modification. It contains modified data derived from
Mario Kart 7, which is © Nintendo. It is not affiliated with or endorsed by
Nintendo. You need your own legitimate copy of the game for any of it to do
anything.
