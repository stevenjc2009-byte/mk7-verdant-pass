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

They are **not** the same picture at two sizes. The banner is a scene; the icon
is an object. See "Why the icon is not the banner" below.

## The icon: a slab of the course

A **48×48 isometric block of the track** on a dark rounded tile — grass on top,
earth beneath, the road winding across it, conifers along both verges. A piece
of track you could pick up.

This is the house style the other apps on this console already use, and the
icon was made to match them deliberately:

| App | Object |
|---|---|
| Hotswap | a game cartridge over two coloured slots |
| Model Kit | a gunpla runner and a part, on a cutting mat |
| Blocksmith | one isometric grass block |
| **Verdant Pass** | **one isometric slab of the course** |

All of them are one centred, flat-shaded object on a dark tile. None of them is
a landscape, and that is the right call: a landscape at 48 px collapses into
three smudges of green, while an object keeps a silhouette.

### How it is put together

Everything is positioned in the top face's own `(u, v)` coordinates and pushed
through `iso()`, so the road and the trees agree with the slab without anything
being placed by hand. The projection is the usual 2:1 isometric.

- **The slab** — the two visible side faces are drawn before the top, so the
  grass edge stays crisp. Three distinct greens; an earlier pass had them
  within a few values of each other and the block read flat.
- **The road** — drawn on its own layer and composited through a mask of the
  top-face polygon. Without the mask, a constant-width ribbon near a corner
  hangs off the side of the slab and reads as road floating in mid-air.
- **The bend is on `u - v`, and that is the whole readability of the icon.**
  `u + v` is the screen *vertical*, which the 2:1 projection squashes by more
  than half — a generous S written there comes out about two pixels tall at 48
  and the road looks like a painted stripe. `u - v` is the screen *horizontal*
  and is not foreshortened at all. Two renders were wasted before this was
  worked out rather than guessed at.
- **The road widens toward the viewer.** A dead-constant width made the slab
  look flat, because nothing else in the drawing says which end is nearer.
- **The trees are on the verges, not in the road.** They sit where `|u - v|` is
  large; the road's own swing peaks at 0.42, so 0.48 and beyond is clear of the
  carriageway. An earlier arrangement put two of them in front of the road and
  they read as posts planted in the carriageway.

## The banner: the pass itself

**A sunlit gap between two forested ridges, with the track's road winding up
through it.** That is literally what the name means, and at 256 px there is
room to say it that way.

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

## Why the icon is not the banner

The first version of the icon *was* the banner, drawn small. It was rendered
four times and, for three of those, never actually looked at — which is its own
lesson. When it finally was, the honest reading is that it worked but it read
as a photograph of scenery, and it did not look like it belonged next to the
other apps on the HOME menu, which are all objects.

So the icon was redrawn as a track slab. The banner keeps the scene: at 256×128
there is room for a horizon, three ridge layers and a sun, and none of that
survives at 48.

## Why it is built the way it is

**Big shapes, four greens.** The icon is 48 pixels. Anything with detail
smaller than about two pixels turns to mush in the reduction, so every element
had to survive being three or four pixels across. The conifers are two stacked
triangles for exactly this reason; a real tree shape disappears.

**Drawn large, reduced with LANCZOS.** The icon is drawn at 384×384 and the
banner at 1024×512, then reduced. Drawing at final size gives stair-stepped
diagonals — and both pictures are nothing but diagonals.

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

### Icon

| | RGB | Where |
|---|---|---|
| `TILE_BG` | 14, 20, 18 | the dark corners behind the tile |
| `TILE` | 26, 44, 36 | the rounded tile |
| `SLAB_TOP` | 104, 176, 108 | grass, the top face |
| `SLAB_LEFT` | 52, 106, 64 | earth, lit side |
| `SLAB_RIGHT` | 26, 62, 40 | earth, shaded side |
| `SLAB_EDGE` | 22, 54, 36 | where grass meets earth |
| `ICON_ROAD` | 226, 230, 216 | the track |
| `ICON_ROAD_EDGE` | 246, 249, 240 | its kerbs |
| `ICON_TREE` | 20, 56, 36 | conifers |
| `ICON_TREE_LIT` | 38, 88, 54 | the lit sliver down each cone |

### Banner

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
