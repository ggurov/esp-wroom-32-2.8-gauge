#!/usr/bin/env python3
"""
Render host-side mock-ups of every gauge preset on the 2.8" panel.

Mirrors the geometry the firmware builds, so the design can be reviewed - and
the arithmetic sanity-checked - without flashing the board.  Keep these
constants in step with:

  firmware/components/gauge/gauge_render.c     (layout maths, dial centring)
  firmware/components/gauge/gauge_theme.c      (palette, tick geometry)
  firmware/components/gauge/gauge_presets.c    (ranges, captions)

Run:  python tools/render_preview.py
"""

from __future__ import annotations

import math
import os

from PIL import Image, ImageDraw, ImageFont

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO, "tools", "preview")

SS = 3                      # supersample factor
PANEL_W = 320               # framebuffer
PANEL_H = 240
DIAL = 240                  # the dial itself is a 240 px circle, centred

# ---- geometry, mirrors gauge_render.c / gauge_theme_greddy ------------------
BEZEL_W = 4
BAND_GAP = 3
BAND_W = 5
ALARM_GAP = 1
ALARM_W = 4
TICK_MAJOR_LEN = 15
TICK_MINOR_LEN = 7
TICK_MAJOR_W = 4          # half-width of the major tick wedge
TICK_MINOR_W = 1
LABEL_PAD_RADIAL = 3
LABEL_ROTATE = 0            # numerals stay upright

HUB_R = 18
Y_TAGLINE = -(HUB_R + 8)
Y_WORDMARK = -(HUB_R + 16)
Y_CAPTION = HUB_R + 14
Y_VALUE = HUB_R + 36
Y_UNIT = HUB_R + 58

F_LABEL = 22       # gfx_font_label
F_VALUE = 32       # gfx_font_value
F_SMALL = 13       # gfx_font_small, used for every label except those two

# LVGL's lv_scale places the numeral centre at:
#   radius - major_len - (pad_radial + LV_SCALE_DEFAULT_LABEL_GAP)
LV_SCALE_DEFAULT_LABEL_GAP = 15

RAIL_R = DIAL // 2 - (BEZEL_W + BAND_GAP + BAND_W // 2)
SCALE_D = 2 * RAIL_R
LABEL_R = RAIL_R - BAND_W - TICK_MAJOR_LEN - ALARM_GAP - ALARM_W - 3 - 8
NEEDLE_LEN = RAIL_R - BAND_W - TICK_MAJOR_LEN - ALARM_GAP - ALARM_W - 2

# ---- palettes, mirrors gauge_theme.c -------------------------------------
GREDDY = dict(
    face=(0x00, 0x00, 0x00), bezel=(0xE8, 0xE8, 0xE8),
    tick_major=(0x2B, 0xE0, 0x6A), tick_minor=(0x27, 0xC0, 0x5C),
    label=(0x46, 0xF0, 0x8A), band=(0x2B, 0xE0, 0x6A),
    band_glow=(0x0E, 0x8B, 0x3C), alarm=(0xFF, 0x1A, 0x1A),
    needle=(0xFF, 0x3B, 0x0A), hub=(0x0A, 0x0A, 0x0A), hub_ring=(0x33, 0x33, 0x33),
    value=(0x5C, 0xFF, 0x9E), caption=(0x3B, 0xE8, 0x7C), unit=(0x27, 0xB8, 0x5E),
    wordmark=(0x46, 0xF0, 0x8A), tagline=(0x1E, 0x9C, 0x4E),
)

AMBER = dict(
    face=(0x00, 0x00, 0x00), bezel=(0xD0, 0xD0, 0xD0),
    tick_major=(0xFF, 0xA0, 0x00), tick_minor=(0xB8, 0x74, 0x00),
    label=(0xFF, 0xB7, 0x33), band=(0xFF, 0xA0, 0x00),
    band_glow=(0x8B, 0x4A, 0x00), alarm=(0xFF, 0x2D, 0x2D),
    needle=(0xFF, 0x3B, 0x0A), hub=(0x0A, 0x0A, 0x0A), hub_ring=(0x33, 0x33, 0x33),
    value=(0xFF, 0xC2, 0x4D), caption=(0xFF, 0xA0, 0x00), unit=(0xB8, 0x74, 0x00),
    wordmark=(0xFF, 0xB7, 0x33), tagline=(0x8B, 0x5A, 0x00),
)

# ---- presets, mirrors gauge_presets.c ------------------------------------
PRESETS = [
    dict(id="rpm", caption="RPM", unit="", wordmark="epicEFI",
 lo=0, hi=8000, major=1000, minor=4,
         alarm=6500, decimals=0, theme=GREDDY, show=4200,
         labels=["0", "1", "2", "3", "4", "5", "6", "7", "8"]),
    dict(id="temp", caption="CL TEMP", unit="", wordmark="epicEFI",
 lo=50, hi=150, major=10, minor=2,
         alarm=115, decimals=0, theme=GREDDY, show=92),
    dict(id="boost", caption="BOOST", unit="", wordmark="epicEFI",
 lo=-1.0, hi=2.0, major=0.5, minor=5,
         alarm=1.75, decimals=1, theme=GREDDY, show=0.9),
    dict(id="volts", caption="VOLTS", unit="", wordmark="epicEFI",
 lo=8, hi=16, major=1, minor=2,
         alarm=None, decimals=1, theme=AMBER, show=13.8),
]

ROT = 135.0     # first tick, clockwise from 3 o'clock
SWEEP = 270.0


def pick_font(size: int, bold: bool = True):
    names = (["seguisb.ttf", "segoeuib.ttf", "arialbd.ttf", "verdanab.ttf", "calibrib.ttf"]
             if bold else
             ["segoeui.ttf", "arial.ttf", "verdana.ttf", "calibri.ttf"])
    for n in names:
        p = os.path.join(r"C:\Windows\Fonts", n)
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def polar(cx, cy, r, deg):
    a = math.radians(deg)
    return cx + r * math.cos(a), cy + r * math.sin(a)


def text_centered(d, xy, text, font, fill, letter_space=0):
    cx, cy = xy
    if letter_space:
        widths = [d.textlength(c, font=font) for c in text]
        total = sum(widths) + letter_space * (len(text) - 1)
        x = cx - total / 2
        for ch, w in zip(text, widths):
            d.text((x, cy), ch, font=font, fill=fill, anchor="lm")
            x += w + letter_space
    else:
        d.text((cx, cy), text, font=font, fill=fill, anchor="mm")


def fmt(value: float, decimals: int) -> str:
    return f"{value:.{decimals}f}"


def angle_of(value: float, p: dict) -> float:
    frac = (value - p["lo"]) / (p["hi"] - p["lo"])
    frac = min(1.0, max(0.0, frac))
    return ROT + SWEEP * frac


def render(p: dict) -> Image.Image:
    th = p["theme"]
    W, H = PANEL_W * SS, PANEL_H * SS
    img = Image.new("RGB", (W, H), th["face"])
    d = ImageDraw.Draw(img)

    # the rail spans [RAIL_R - BAND_W, RAIL_R]; PIL strokes about a centre line
    cx = W / 2
    cy = H / 2
    end = ROT + SWEEP
    rail_mid = (RAIL_R - BAND_W / 2) * SS
    band_w = BAND_W * SS
    tick_base = (RAIL_R - BAND_W) * SS
    alarm_out = (RAIL_R - BAND_W - TICK_MAJOR_LEN - ALARM_GAP) * SS

    r_outer = DIAL / 2 * SS
    d.ellipse([cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer],
              fill=th["face"], outline=th["bezel"], width=BEZEL_W * SS)

    # glow: stacked translucent arcs behind the rail
    glow_w = BAND_W * 4 * SS
    for i in range(6, 0, -1):
        w = int(glow_w * i / 6)
        overlay = Image.new("RGB", (W, H), (0, 0, 0))
        od = ImageDraw.Draw(overlay)
        od.arc([cx - rail_mid, cy - rail_mid, cx + rail_mid, cy + rail_mid], ROT, end,
               fill=th["band_glow"], width=w)
        mask = Image.new("L", (W, H), 0)
        ImageDraw.Draw(mask).arc([cx - rail_mid, cy - rail_mid, cx + rail_mid, cy + rail_mid],
                                 ROT, end, fill=int(90 / i), width=w)
        img = Image.composite(overlay, img, mask)
    d = ImageDraw.Draw(img)

    # the rail runs uninterrupted - the warning sector never paints over it
    d.arc([cx - rail_mid, cy - rail_mid, cx + rail_mid, cy + rail_mid], ROT, end,
          fill=th["band"], width=band_w)

    majors = int(round((p["hi"] - p["lo"]) / p["major"]))
    total_ticks = majors * p["minor"] + 1

    # warning sector: a separate arc inboard of the ticks
    if p["alarm"] is not None and p["alarm"] <= p["hi"]:
        alarm_deg = angle_of(p["alarm"], p)
        d.arc([cx - alarm_out, cy - alarm_out, cx + alarm_out, cy + alarm_out],
              alarm_deg, end, fill=th["alarm"], width=ALARM_W * SS)

    for i in range(total_ticks):
        deg = ROT + SWEEP * i / (total_ticks - 1)
        is_major = (i % p["minor"]) == 0
        rad = math.radians(deg)
        ux, uy = math.cos(rad), math.sin(rad)      # outward
        vx, vy = -uy, ux                           # across

        if not is_major:
            # minor ticks are plain radial lines
            p0 = (cx + tick_base * ux, cy + tick_base * uy)
            p1 = (cx + (tick_base - TICK_MINOR_LEN * SS) * ux,
                  cy + (tick_base - TICK_MINOR_LEN * SS) * uy)
            d.line([p0, p1], fill=th["tick_minor"], width=max(1, TICK_MINOR_W * SS))
            continue

        # major ticks are wedges pointing at the centre
        w = TICK_MAJOR_W * SS
        r_tip = tick_base - TICK_MAJOR_LEN * SS
        d.polygon([
            (cx + tick_base * ux + w * vx, cy + tick_base * uy + w * vy),
            (cx + tick_base * ux - w * vx, cy + tick_base * uy - w * vy),
            (cx + r_tip * ux, cy + r_tip * uy),
        ], fill=th["tick_major"])

    f_label = pick_font(int(F_LABEL * SS))
    labels = p.get("labels")
    for i in range(majors + 1):
        deg = ROT + SWEEP * i / majors
        txt = labels[i] if labels else fmt(p["lo"] + p["major"] * i, p["decimals"])
        text_centered(d, polar(cx, cy, LABEL_R * SS, deg), txt, f_label, th["label"])

    # needle: the same tapered blade the firmware builds as a polygon in
    # gauge_render.c.  angle_of() is already in the gfx convention (degrees
    # clockwise from 3 o'clock), so no further offset.
    theta = math.radians(angle_of(p["show"], p))
    ux, uy = math.cos(theta), math.sin(theta)
    vx, vy = -uy, ux
    blade = [(-5.0, 0.0), (-1.2, float(NEEDLE_LEN)), (1.2, float(NEEDLE_LEN)),
             (5.0, 0.0), (4.0, -13.0), (-4.0, -13.0)]
    # cx/cy and the canvas are in supersampled units, so the blade has to be
    # scaled up too or the needle comes out a third of its real length
    pts = [(cx + (a * vx + b * ux) * SS, cy + (a * vy + b * uy) * SS)
           for a, b in blade]
    d.polygon(pts, fill=th["needle"])

    hr = HUB_R * SS
    d.ellipse([cx - hr, cy - hr, cx + hr, cy + hr], fill=th["hub"],
              outline=th["hub_ring"], width=2 * SS)

    f_word = pick_font(int(F_SMALL * SS))
    f_tag = pick_font(int(F_SMALL * SS), bold=False)
    f_cap = pick_font(int(F_SMALL * SS))
    f_val = pick_font(int(F_VALUE * SS))
    f_unit = pick_font(int(F_SMALL * SS), bold=False)

    text_centered(d, (cx, cy + Y_WORDMARK * SS), p["wordmark"], f_word, th["wordmark"], 2 * SS)
    if p.get("tagline"):
        text_centered(d, (cx, cy + Y_TAGLINE * SS), p["tagline"], f_tag, th["tagline"], SS)
    text_centered(d, (cx, cy + Y_CAPTION * SS), p["caption"], f_cap, th["caption"], SS)
    text_centered(d, (cx, cy + Y_VALUE * SS), fmt(p["show"], p["decimals"]), f_val, th["value"])
    if p["unit"]:
        text_centered(d, (cx, cy + Y_UNIT * SS), p["unit"], f_unit, th["unit"], SS)

    return img.resize((PANEL_W, PANEL_H), Image.LANCZOS)


def main() -> int:
    os.makedirs(OUT_DIR, exist_ok=True)
    sheet = Image.new("RGB", (PANEL_W * len(PRESETS), PANEL_H), (0, 0, 0))

    for i, p in enumerate(PRESETS):
        img = render(p)
        path = os.path.join(OUT_DIR, f"dial_{p['id']}.png")
        img.save(path)
        img.resize((PANEL_W * 2, PANEL_H * 2), Image.NEAREST).save(
            os.path.join(OUT_DIR, f"dial_{p['id']}_2x.png"))
        sheet.paste(img, (PANEL_W * i, 0))
        print(f"preview : {path}")

    sheet_path = os.path.join(OUT_DIR, "dial_all.png")
    sheet.resize((PANEL_W * len(PRESETS) * 2, PANEL_H * 2), Image.NEAREST).save(sheet_path)
    print(f"preview : {sheet_path}")

    print()
    print(f"  panel          : {PANEL_W}x{PANEL_H}, dial centred at "
          f"({PANEL_W // 2}, {PANEL_H // 2})")
    print(f"  rail radius    : {RAIL_R} px   (lv_scale widget {SCALE_D}x{SCALE_D})")
    print(f"  needle tip     : {NEEDLE_LEN} px")
    print(f"  numeral centre : {LABEL_R} px   "
          f"(band {LABEL_R - 12} .. {LABEL_R + 12})")
    print(f"  major ticks    : {RAIL_R - TICK_MAJOR_LEN} .. {RAIL_R} px")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
