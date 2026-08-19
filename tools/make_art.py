"""Draws the app's icon and CIA banner.

The **banner** is the scene: a sunlit gap between two forested ridges with the
track winding up through it. That is what "Verdant Pass" is, and at 256 px there
is room to say it that way.

The **icon** is not the same picture shrunk. It follows the house style the
other apps on this console already use - Hotswap, Model Kit, Blocksmith - which
is one centred, flat-shaded object on a dark rounded tile, not a small
landscape. A landscape at 48 px turns into three smudges of green; an object
keeps its silhouette. The object here is an isometric slab of the course:
grass on top, earth beneath, the road curving across it and a few conifers on
the verge - a piece of track you could pick up.

Everything is drawn from primitives here rather than exported from anywhere, so
the art is ours the same way the track data is.

    python tools/make_art.py
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent

# The palette. Warm sky against cool greens, so the gap between the ridges is
# the brightest thing in the frame and the eye goes there first.
SKY_TOP = (86, 170, 214)
SKY_MID = (168, 214, 226)
SKY_LOW = (250, 224, 168)
SUN = (255, 246, 214)

RIDGE_FAR = (108, 158, 122)
RIDGE_MID = (58, 116, 78)
RIDGE_NEAR = (30, 78, 50)
RIDGE_DARK = (18, 52, 34)

ROAD = (206, 208, 198)
ROAD_EDGE = (238, 240, 232)
GRASS = (74, 142, 88)


def lerp(a, b, t):
    return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))


def sky(size, horizon):
    """Vertical gradient, warmest just above the horizon."""
    w, h = size
    img = Image.new("RGB", size)
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, horizon)
        if t < 0.55:
            c = lerp(SKY_TOP, SKY_MID, t / 0.55)
        else:
            c = lerp(SKY_MID, SKY_LOW, min(1.0, (t - 0.55) / 0.45))
        d.line([(0, y), (w, y)], fill=c)
    return img


def ridge(draw, size, horizon, peaks, colour, base_extra=0):
    """One layer of hills: a smooth silhouette filled down to the bottom."""
    w, h = size
    points = [(-2, h + base_extra)]
    for x in range(-2, w + 3):
        y = horizon
        for cx, amp, width in peaks:
            y -= amp * math.exp(-((x - cx) ** 2) / (2.0 * width * width))
        points.append((x, y))
    points.append((w + 2, h + base_extra))
    draw.polygon(points, fill=colour)


def road(draw, size, horizon, width_bottom, width_top):
    """The track: a ribbon that bends, widening as it comes toward the viewer."""
    w, h = size
    left, right = [], []
    steps = 96
    for i in range(steps + 1):
        t = i / steps                    # 0 at the horizon, 1 at the bottom
        y = horizon + t * (h - horizon)
        # Ease so the far end pinches fast. That taper is the whole illusion of
        # depth in a flat drawing.
        k = t ** 1.7
        half = (width_top + (width_bottom - width_top) * k) / 2.0
        # A single S, largest in the middle distance, so it reads as a pass
        # winding between the hills rather than a straight run.
        cx = w * 0.50 + math.sin(t * math.pi * 0.9) * w * 0.10 * (1.0 - k * 0.4)
        left.append((cx - half, y))
        right.append((cx + half, y))

    draw.polygon([(x - w * 0.012, y) for x, y in left] +
                 [(x + w * 0.012, y) for x, y in reversed(right)], fill=ROAD_EDGE)
    draw.polygon(left + list(reversed(right)), fill=ROAD)
    return left, right


def tree(draw, x, base_y, height, colour):
    """A conifer silhouette: two stacked triangles, which is all that survives
    the reduction to 48 pixels."""
    half = height * 0.30
    draw.polygon([(x, base_y - height),
                  (x - half, base_y - height * 0.38),
                  (x + half, base_y - height * 0.38)], fill=colour)
    draw.polygon([(x, base_y - height * 0.66),
                  (x - half * 1.2, base_y),
                  (x + half * 1.2, base_y)], fill=colour)


def edge_mass(draw, size, side, reach, top_y, colour):
    """A forested shoulder anchored to one edge of frame.

    Drawn as its own shape rather than as another full-width ridge: a ridge fills
    flat to the bottom of the picture, which paints over the ground and the road
    and was exactly what made the first attempt read as a grey wedge on green.
    Returns points down its inner slope, for planting trees on.
    """
    w, h = size
    slope = []
    steps = 40
    for i in range(steps + 1):
        t = i / steps
        x = reach * t
        y = top_y + (h - top_y) * (t ** 2.1)
        slope.append((x if side < 0 else w - x, y))

    draw.polygon([(0 if side < 0 else w, h), (0 if side < 0 else w, top_y)] +
                 slope + [(slope[-1][0], h)], fill=colour)
    return slope


def scene(size, horizon_frac, detail):
    """The shared picture. `detail` scales how much is worth drawing."""
    w, h = size
    horizon = h * horizon_frac

    img = sky(size, horizon).convert("RGBA")
    d = ImageDraw.Draw(img)

    # Sun, sitting in the gap between the ridges - the brightest thing in frame,
    # so the eye goes to the gap first and reads the picture as a pass.
    r = w * 0.085
    cx, cy = w * 0.50, horizon - h * 0.10
    glow = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([cx - r * 2.8, cy - r * 2.8, cx + r * 2.8, cy + r * 2.8],
                                 fill=SUN + (78,))
    glow = glow.filter(ImageFilter.GaussianBlur(radius=max(2, w * 0.035)))
    img = Image.alpha_composite(img, glow)
    d = ImageDraw.Draw(img)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=SUN)

    # Two full-width ridges behind everything, dipping in the middle.
    ridge(d, size, horizon,
          [(w * 0.14, h * 0.13, w * 0.17), (w * 0.86, h * 0.12, w * 0.16)], RIDGE_FAR)
    ridge(d, size, horizon + h * 0.05,
          [(w * 0.02, h * 0.24, w * 0.16), (w * 0.98, h * 0.22, w * 0.16)], RIDGE_MID)

    # The valley floor. Drawn as a rolling ridge rather than a rectangle: a flat
    # rule straight across the frame reads as a seam between two flat colours
    # rather than as ground meeting hills.
    # The bumps have to be narrower than they are far apart, or the gaussians
    # overlap into a constant and the "rolling" floor comes out as a dead
    # straight rule - which is the seam this was written to avoid.
    ground = horizon + h * 0.07
    ridge(d, size, ground,
          [(w * 0.20, h * 0.045, w * 0.085), (w * 0.44, h * 0.018, w * 0.06),
           (w * 0.70, h * 0.036, w * 0.075), (w * 0.93, h * 0.050, w * 0.09)], GRASS)

    road(d, size, ground, w * 0.46, w * 0.035)

    # Forested shoulders last, so they overlap the road's near end and frame it.
    for side in (-1, 1):
        slope = edge_mass(d, size, side, w * 0.30, ground + h * 0.04, RIDGE_NEAR)
        if not detail:
            continue
        for i in range(1, len(slope), max(1, len(slope) // (detail + 2))):
            x, y = slope[i]
            size_t = h * (0.07 + 0.20 * (i / len(slope)))
            tree(d, x + side * size_t * 0.30, y + size_t * 0.10, size_t, RIDGE_DARK)

    return img


# ---------------------------------------------------------------------------
# The icon: an isometric slab of the course
# ---------------------------------------------------------------------------

TILE_BG = (14, 20, 18)          # near-black behind the rounded tile
TILE = (26, 44, 36)             # the tile itself
SLAB_TOP = (104, 176, 108)      # grass
SLAB_LEFT = (52, 106, 64)       # earth, lit side
SLAB_RIGHT = (26, 62, 40)       # earth, shaded side
SLAB_EDGE = (22, 54, 36)        # the line where grass meets earth
ICON_ROAD = (226, 230, 216)
ICON_ROAD_EDGE = (246, 249, 240)
ICON_TREE = (20, 56, 36)
ICON_TREE_LIT = (38, 88, 54)

ISO_A = 0.355                   # half-width of the top face, as a fraction of frame
ISO_B = 0.180                   # half-height of it - 2:1, the usual isometric ratio
ISO_D = 0.135                   # how deep the slab is


def iso(size, u, v):
    """Project a point on the slab's top face to the frame.

    (u, v) are 0..1 across the two edges of the top face. The whole icon is
    built in this space so the road and the trees agree with the slab without
    anything being positioned by hand.
    """
    w, h = size
    cx, cy = w * 0.5, h * 0.455
    return (cx + (u - v) * w * ISO_A,
            cy + (u + v - 1.0) * h * ISO_B)


def road_path(size, steps=72):
    """The course, as a curve across the top face. A single S, corner to corner."""
    # Runs from the near corner to the far one, and the bend is put on u-v.
    #
    # That choice is the whole readability of this icon. u+v is the screen
    # *vertical*, which the 2:1 projection squashes by more than half, so a
    # generous S written there comes out about two pixels tall at 48 and the
    # road looks like a painted stripe. u-v is the screen *horizontal* and is
    # not foreshortened at all, so the same amplitude actually shows.
    pts = []
    for i in range(steps + 1):
        t = i / steps
        s = 0.26 + 1.48 * t                      # near corner to far corner
        q = 0.42 * math.sin(t * math.pi * 1.6)   # the winding, side to side
        pts.append(iso(size, (s + q) / 2.0, (s - q) / 2.0))
    return pts


def screen_ribbon(draw, pts, half, colour, half_end=None):
    """A constant-width ribbon along an already-projected path.

    Width is held constant on screen rather than on the face: the face is
    foreshortened 2:1, so a constant width in face space renders as a road that
    visibly fattens as it turns, which reads as a mistake.
    """
    left, right = [], []
    n = len(pts) - 1
    for i, (x, y) in enumerate(pts):
        ax, ay = pts[max(0, i - 1)]
        bx, by = pts[min(n, i + 1)]
        dx, dy = bx - ax, by - ay
        L = math.hypot(dx, dy) or 1.0
        # Widen toward the near end. A dead-constant width makes the slab look
        # flat, because nothing else in the drawing says which end is closer.
        k = half if half_end is None else half + (half_end - half) * (i / n)
        ox, oy = -dy / L * k, dx / L * k
        left.append((x + ox, y + oy))
        right.append((x - ox, y - oy))
    draw.polygon(left + list(reversed(right)), fill=colour)


def conifer(draw, x, base_y, height, colour, lit):
    """Same two-triangle tree as the banner, but with a lit face so it does not
    read as a flat spike sitting on top of the grass."""
    half = height * 0.30
    draw.polygon([(x, base_y - height), (x - half, base_y - height * 0.36),
                  (x + half, base_y - height * 0.36)], fill=colour)
    draw.polygon([(x, base_y - height * 0.64), (x - half * 1.18, base_y),
                  (x + half * 1.18, base_y)], fill=colour)
    # A sliver down the left of each cone, so the silhouette has a direction.
    draw.polygon([(x, base_y - height), (x - half, base_y - height * 0.36),
                  (x - half * 0.30, base_y - height * 0.42)], fill=lit)


def make_icon(path):
    """48x48, drawn at 8x and reduced so the isometric edges are not jagged."""
    w = h = 384
    size = (w, h)
    img = Image.new("RGB", size, TILE_BG)
    d = ImageDraw.Draw(img)

    # The tile. Rounded like every other app's icon on this console, and inset
    # so the corners of the 48x48 stay dark and the shape reads as a tile.
    d.rounded_rectangle([w * 0.045, h * 0.045, w * 0.955, h * 0.955],
                        radius=w * 0.20, fill=TILE)

    top = [iso(size, 0, 0), iso(size, 1, 0), iso(size, 1, 1), iso(size, 0, 1)]
    depth = h * ISO_D

    # The two visible sides, drawn before the top so the top's edge is crisp.
    d.polygon([top[3], top[2], (top[2][0], top[2][1] + depth),
               (top[3][0], top[3][1] + depth)], fill=SLAB_LEFT)
    d.polygon([top[2], top[1], (top[1][0], top[1][1] + depth),
               (top[2][0], top[2][1] + depth)], fill=SLAB_RIGHT)
    d.polygon(top, fill=SLAB_TOP)
    d.line([top[3], top[2], top[1]], fill=SLAB_EDGE, width=max(1, int(w * 0.008)))

    # The course. Drawn on its own layer and composited through a mask of the
    # top face: a constant-width ribbon near a corner otherwise hangs off the
    # side of the slab, which reads as the road floating in mid-air.
    road_layer = Image.new("RGB", size, SLAB_TOP)
    rd = ImageDraw.Draw(road_layer)
    path_pts = road_path(size)
    screen_ribbon(rd, path_pts, w * 0.022, ICON_ROAD_EDGE, half_end=w * 0.036)
    screen_ribbon(rd, path_pts, w * 0.015, ICON_ROAD, half_end=w * 0.028)

    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).polygon(top, fill=255)
    img.paste(road_layer, (0, 0), mask)
    d = ImageDraw.Draw(img)
    d.line([top[3], top[2], top[1]], fill=SLAB_EDGE, width=max(1, int(w * 0.008)))

    # Conifers on the verge, back to front so the near ones overlap the far.
    # Positions are in face space and kept clear of the road's own corridor.
    # The road now runs up the middle, so the verges are the left and right
    # corners - that is |u - v| large. It peaks at 0.42, so 0.48 and beyond is
    # clear of the carriageway.
    trees = [(0.06, 0.54), (0.16, 0.72), (0.54, 0.06), (0.72, 0.16)]
    for u, v in sorted(trees, key=lambda q: q[0] + q[1]):
        x, y = iso(size, u, v)
        size_t = h * (0.150 + 0.060 * (u + v) / 2.0)
        conifer(d, x, y, size_t, ICON_TREE, ICON_TREE_LIT)

    out = img.resize((48, 48), Image.LANCZOS).convert("RGB")
    out.save(path)
    return out


def find_font(size):
    for name in ("arialbd.ttf", "seguisb.ttf", "segoeuib.ttf", "arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def make_banner(path):
    """256x128, the size the CIA banner is shown at on the HOME menu."""
    img = scene((1024, 512), 0.44, detail=4).resize((256, 128), Image.LANCZOS)
    img = img.convert("RGBA")

    # A scrim across the bottom third. Without it the title fights the road,
    # which is the brightest thing at the near end of the picture.
    scrim = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sd = ImageDraw.Draw(scrim)
    for y in range(64, 128):
        a = int(210 * ((y - 64) / 64.0) ** 1.6)
        sd.line([(0, y), (img.width, y)], fill=(8, 26, 18, a))
    img = Image.alpha_composite(img, scrim)
    d = ImageDraw.Draw(img)

    # The name, low and left, over the darkest part of the picture so it stays
    # legible without a panel behind it.
    title = "VERDANT PASS"
    font = find_font(24)
    x, y = 13, 84

    # A soft drop shadow rather than an outline: an outline at this size closes
    # up the counters of the letters and turns the word into a smear.
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).text((x, y), title, font=font, fill=(0, 20, 10, 190))
    shadow = shadow.filter(ImageFilter.GaussianBlur(radius=2))
    img = Image.alpha_composite(img.convert("RGBA"), shadow)

    d = ImageDraw.Draw(img)
    d.text((x, y), title, font=font, fill=(246, 253, 247))
    d.text((x + 2, y + 25), "a custom track for Mario Kart 7",
           font=find_font(11), fill=(176, 214, 186))

    img.convert("RGB").save(path)
    return img


if __name__ == "__main__":
    icon = ROOT / "icon.png"
    banner = ROOT / "cia" / "banner.png"
    make_icon(icon)
    make_banner(banner)
    print(f"{icon}  48x48")
    print(f"{banner}  256x128")
