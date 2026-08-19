# The icon and the banner

Both are drawn from primitives by `tools/make_art.py`, using only Pillow's
polygon, ellipse, line and text calls. Nothing is traced, sampled, exported or
copied from anywhere — the art is ours the same way the track data is, which
is the point.

```bash
python tools/make_art.py
```

writes `icon.png` (48×48, the HOME menu icon, picked up by the Makefile's
`APP_ICON`) and `cia/banner.png` (256×128, the wide banner shown under the icon
when the title is selected, fed to `bannertool makebanner`).

## What the picture is

One scene, drawn twice at different sizes: **a sunlit gap between two forested
ridges, with the track's road winding up through it.** That is literally what
the name means, so the icon says what the app is without a word on it.

Reading it from the back forward:

- **Sky** — a vertical gradient, cool blue at the top through pale cyan to a
  warm cream at the horizon. Warmest where the sun is, so the gap is the
  brightest part of the frame and the eye lands there first.
- **Sun** — a disc sitting just below the ridge line, dead centre, with a
  blurred glow composited under it. It is the only round shape in the picture.
- **Three ridge layers** — pale sage far, mid green, dark green near, each one
  dipping in the middle to form the pass. Layering them light-to-dark front to
  back is the whole depth cue; there is no perspective drawing here.
- **The valley floor** — a gently rolling line rather than a rule across the
  frame. (An earlier version drew it flat and it read as a seam between two
  colours instead of ground meeting hills.)
- **The road** — a pale ribbon with brighter edges, pinching hard toward the
  horizon and bending in a single S. The taper is the only thing selling depth,
  so it eases as `t^1.7` rather than linearly.
- **Forested shoulders** — a dark mass anchored to each edge of frame with
  conifer silhouettes planted down its inner slope. Drawn last so they overlap
  the near end of the road and frame it.

## Why it is built the way it is

**Big shapes, four greens.** The icon is 48 pixels. Anything with detail
smaller than about two pixels turns to mush in the reduction, so every element
had to survive being three or four pixels across. The conifers are two stacked
triangles for exactly this reason; a real tree shape disappears.

**Drawn large, reduced with LANCZOS.** The icon is drawn at 192×192 and the
banner at 1024×512, then reduced. Drawing at final size gives stair-stepped
diagonals — and this picture is nothing but diagonals.

**The shoulders are their own shapes, not more ridges.** A ridge in this script
fills flat down to the bottom of the picture. A full-width one in the
foreground therefore paints over the ground *and* the road, which is what made
the first attempt read as a grey wedge on green. `edge_mass()` anchors to one
edge and stops.

**A drop shadow behind the title, not an outline.** At 24 px an outline closes
up the counters of the letters and "VERDANT PASS" becomes a smear. A blurred
shadow keeps the word legible against the picture.

**A soft scrim across the bottom third of the banner**, easing in over 64 rows
rather than starting at a hard edge, so the title has something dark to sit on
without a visible band across the art.

**No chequered start line.** There was one. At 256 px wide it landed behind the
word "PASS" and read as a smudge, and it never survived the reduction as
anything recognisable. Dropped rather than fought.

## Palette

| | RGB | Where |
|---|---|---|
| `SKY_TOP` | 86, 170, 214 | top of frame |
| `SKY_MID` | 168, 214, 226 | mid sky |
| `SKY_LOW` | 250, 224, 168 | at the horizon |
| `SUN` | 255, 246, 214 | disc and glow |
| `RIDGE_FAR` | 108, 158, 122 | furthest hills |
| `RIDGE_MID` | 58, 116, 78 | middle hills |
| `RIDGE_NEAR` | 30, 78, 50 | forested shoulders |
| `RIDGE_DARK` | 18, 52, 34 | conifers |
| `GRASS` | 74, 142, 88 | valley floor |
| `ROAD` | 206, 208, 198 | the track |
| `ROAD_EDGE` | 238, 240, 232 | its kerbs |

## The text on the banner

- **VERDANT PASS** — bold, low and left, over the darkest part of the picture.
- **a custom track for Mario Kart 7** — under it, in a muted green.

The font is whichever of Arial Bold / Segoe UI Semibold / Segoe UI Bold / Arial
is installed, falling back to Pillow's default. It is baked into the PNG at
build time, so nothing about the font matters after that.

## The rest of the metadata

The title, description and author shown on the HOME menu come from the SMDH,
set in the `Makefile`:

```
APP_TITLE       := Verdant Pass
APP_DESCRIPTION := A custom track for Mario Kart 7
APP_AUTHOR      := steve
```

`cia/banner.wav` is silence — the banner jingle. `bannertool` requires the file
to exist; it does not require it to make a noise.
