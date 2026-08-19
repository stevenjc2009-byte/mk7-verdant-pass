# Verdant Pass

A custom Mario Kart 7 track, shipped as a 3DS app that builds it onto your SD
card.

Run the app once and it writes the track out as a mod that
[Hotswap](https://github.com/stevenjc2009-byte/Hotswap) can switch on and off.
Stock Mario Kart 7 keeps working exactly as it did; you swap to Verdant Pass
when you want it and back to Stock when you don't.

The track replaces **Kalimari Desert** in the Lightning Cup, and renames it in
all eight European languages.

## Install

<img src="docs/install-qr.png" width="220" alt="Install QR code">

Scan that with FBI's *Remote Install → Scan QR Code*, or download
[`verdantpass1.1.1.cia`](https://github.com/stevenjc2009-byte/mk7-verdant-pass/releases/download/v1.1.1/verdantpass1.1.1.cia)
and install it with FBI by hand.

Once it is on, later versions install themselves — see [Updates](#updates).

## Using it

1. **Open Verdant Pass** from the HOME menu and tap **Install track** (or press
   **A**). It reads your copy of Mario Kart 7, builds the nine modded files,
   and writes them to `sdmc:/hotswap/mk7/Verdant Pass/`.
2. **Open Hotswap**, pick Mario Kart 7, and choose **Verdant Pass**.
3. **Launch Mario Kart 7** and go to the Lightning Cup.

To go back to normal, open Hotswap and switch Mario Kart 7 to **Stock**.

### You need

- A 3DS with **Luma3DS** and custom firmware.
- **Luma3DS game patching turned on** — hold SELECT on boot, and enable
  *Enable game patching*. Without it Luma ignores the swapped-in files and you
  get stock Kalimari Desert with nothing to say why.
- **Hotswap**, which is what actually swaps the track in and out.
- **Mario Kart 7 readable on the console** — a cartridge in the slot or an
  installed copy. The install works by reading your own game; there is no copy
  of Nintendo's data inside this app to fall back on.

### Updates

Tap **Check for updates** (or press **Y**) to ask GitHub for a newer version.
If there is one it downloads and installs it, then relaunches. The bar on the
top screen moves the whole time, so a slow download looks slow rather than
looking hung.

The track lives inside the app, so a new track version *is* a new app version:
after updating, install again to write the newer track to the card.

The check needs an internet connection and only works on the installed CIA —
the `.3dsx` build has no way to install a title, so the button is greyed out
there.

## If it refuses

> *Verdant Pass is swapped in right now. Open Hotswap, switch Mario Kart 7 back
> to Stock, then run this again.*

That is the app declining to fight Hotswap for the files. While the mod is
swapped in, Hotswap has renamed its folder away into `/luma/titles/`, and
writing a fresh copy underneath would leave two of them and a state file that
disagrees with the disk. Swap back to Stock first.

> *Mario Kart 7 could not be opened.*

The app could not mount the game's RomFS. It tries the European, Japanese,
American and Korean title IDs against both the SD card and the game card. If
none of them answer, the game is not readable on this console.

## How it works

**The app ships none of Nintendo's data.** What it carries is the three files
this project generated — the track model, the collision mesh and the course
layout — and it reads everything else out of your own copy of the game,
assembling the finished files on the console.

The course archive has fifteen entries and only three of them are ours; the
eight UI archives are entirely Nintendo's with one string changed. Shipping
those finished archives would mean redistributing 943,342 bytes of Nintendo's
data. Building them on-device means shipping none.

So on install the app:

1. Mounts your game's RomFS (`gamefs.c`).
2. Reads `Course/Gn64_KalimariDesert.szs`, decompresses the Yaz0, swaps our
   three entries into the SARC, rebuilds and recompresses (`szs.c`, `sarc.c`,
   `patch.c`).
3. Does the same for each of the eight `UI/common-e?.szs`, editing message 129
   in the `MsgStdBn` table to read *Verdant Pass* (`msbt.c`).
4. Writes the nine files into Hotswap's layout, reading each one's size back
   off the card afterwards.

None of that touches a 3DS API, which is why the whole recipe is covered by
host tests that compare its output against the PC toolchain that built the
track in the first place.

## What is in the app

- `romfs/mod/course.bcmdl` — the track model. Ours.
- `romfs/mod/course.kcl` — the collision mesh. Ours.
- `romfs/mod/course.kmp` — the course layout: route, checkpoints, objects,
  the start line. Ours.
- `romfs/cacert.pem` — the CA bundle the update check needs. The console's own
  root store predates every certificate authority in use today.

## Building it

Needs [devkitPro](https://devkitpro.org/) with `3ds-dev`, plus `3ds-curl` and
`3ds-mbedtls` for the updater.

```bash
make        # verdantpass.3dsx
make cia    # verdantpass.cia
```

Build in the devkitPro MSYS2 shell. `-Werror` is on, so a warning is a failed
build.

The icon and the HOME menu banner are drawn from primitives by
`tools/make_art.py` — see [docs/art.md](docs/art.md). Nothing in them is
traced, sampled or exported from anywhere.

### Tests

The patch engine, the install engine and the front end's button logic are all
plain C with no 3DS API in them, so they compile and run on the host:

```bash
sh test/run.sh
```

That builds three suites, runs them against a real Mario Kart 7 extract, and
then has the project's Python toolchain read back what the C wrote:

- **ui model** — which buttons exist in each state, where they are, and what a
  tap on any given pixel does.
- **patch engine** — Yaz0 round-trips, the rebuilt course archive is
  byte-identical to the PC toolchain's, Nintendo's twelve entries come through
  untouched, and garbage or a wrong archive is refused.
- **install engine** — the real `vp_install` against the real extract: nine
  files, in order, each decompressing to the expected archive, and all four
  ways Hotswap's state file can say "don't".

The suite needs a ROM extract, which is not in this repository. Point
`MK7_MOD_ROOT` at one, or the run skips — loudly.

It does not cover the drawing, the updater, or `gamefs.c` — those need a 3DS.

## Legal

This is a fan-made modification and is not affiliated with or endorsed by
Nintendo. It contains no Nintendo data: the track, the icon and the banner are
original work, and everything else is read out of the copy of the game already
on your console. Mario Kart 7 is © Nintendo.
