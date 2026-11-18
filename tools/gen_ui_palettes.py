#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The UI palettes (run by tools/build.py generate()).

  gen_ui_palettes.py OUT.h [--report]

A palette is five colours: BG (background), SURF (surface: dialogs, menu rows, mixer strips),
TEXT, THEME (identity colour: values, curves, gauges, selection) and ACCENT (the one active
thing: hot knob, cursor, playhead, a sounding step). The firmware derives the rest with fixed
blends at palette_set() time (src/gfx.c, the same integer maths as mix() here):
  MID  = mix(BG, TEXT, 70 %)    labels, units, secondary text
  DIM  = mix(BG, TEXT, 42 %)    inactive, empty steps, disabled
  LINE = mix(BG, TEXT, 18 %)    1 px dividers, the faint gauge track
  SEL  = mix(BG, THEME, 80 %)   selection fill and gauge fill (the "mid" of the theme)
  TINT = mix(BG, THEME, 12 %)   a faint area (the selected drum lane)
  RAISE = mix(SURF, TEXT, 12 %) a raised area on a surface: empty step stubs, guides, the selected drum
                                lane, fader slots, button wells, chips
  KEY  = mix(BG, TEXT, 78 %)    a keycap's fill (a mid-light cushion; its label INK, unavailable: a DIM fill)
  INK  = BG: text on a selection or THEME fill (GREY, MONO: a light fill, dark ink)
Outside the palette: REC, a fixed red; the QR code's fixed black and white; the crash screen's fixed red.
Switch Blade has one colour scheme, the four machines' palettes (OB-Z WAVE ACID DRIFT, engine order); the ten fixed
palettes of earlier firmware (GREY GREEN AMBER ICE VIOLET ROSE PAPER HI-CON NIGHT MONO) and CUSTOM are gone. Saved settings
name a palette by a tagged id (PAL_TAG + 10 = MACHINE is written, so earlier firmware reads it as GREY); any stored id
loads as MACHINE.
--report prints the WCAG contrast of every pairing. The tool fails (exit 1) on any miss.
"""
import argparse
import sys
from pathlib import Path

# name, bg, surf, text, theme, accent[, key] (8-bit RGB; stored as RGB565)
# the four machines' palettes (the palette follows the selected track's machine; engine order
# OB-Z WAVE ACID DRIFT). Sampled from the
# machines' reference panels (assets/refs/, docs/plans/2026-10-19-switch-blade-ui.md), then held to the same contrast
# rules as every palette (docs/plans: the sampled hexes, lifted only where a rule failed: OB-Z surf and theme, DRIFT surf
# and theme, ACID bg / surf / accent; the report of the changes is in .superpowers/sdd/task-ui23-report.md). ACID is a light palette (the 303's silver, like PAPER: its derived tokens come out the light way)
MACHINE_PALETTES = [
    ("OB-Z",  (5, 8, 13),      (13, 23, 37),    (232, 235, 239), (96, 152, 212),  (232, 53, 44)),
    ("WAVE",  (19, 20, 24),    (42, 45, 54),    (242, 232, 220), (245, 166, 35),  (255, 208, 138)),
    ("ACID",  (208, 218, 225), (188, 202, 213), (18, 24, 32),    (37, 38, 45),     (138, 54, 0)),
    # DRIFT's keycaps are tan, as the Juno-106's buttons (the 6th colour: a keycap fill of its own, not derived)
    ("DRIFT", (11, 13, 17),    (24, 27, 34),    (228, 230, 232), (255, 96, 90),   (61, 134, 200), (224, 197, 144)),
    # V2 placeholders (the UI phase samples the real panels): dark, each with its theme colour
    ("JP-Z",  (12, 14, 20),    (30, 34, 44),    (230, 232, 236), (80, 170, 255),  (255, 140, 60)),
    ("CZ-Z",  (13, 13, 16),    (24, 24, 29),    (236, 236, 236), (255, 104, 98),  (96, 156, 236)),
    ("GW-Z",  (16, 12, 10),    (30, 24, 20),    (240, 228, 210), (230, 170, 80),  (246, 128, 88)),
    ("MICRO", (8, 12, 18),     (22, 32, 44),    (226, 236, 242), (70, 200, 220),  (240, 200, 80)),
]
PAL_TAG = 64                       # stored id = PAL_TAG + 10 (MACHINE's id in the earlier firmware's palette list)
PAL_STORED_MACHINE = 10
REC_DARK, REC_LIGHT = (255, 72, 72), (190, 24, 40)
QR_LIGHT, QR_DARK, CRASH_BG, CRASH_INK = (255, 255, 255), (0, 0, 0), (160, 0, 0), (255, 255, 255)
PCT = {"MID": 70, "DIM": 42, "LINE": 18, "SEL": 80, "TINT": 12, "KEY": 78}
RAISE_PCT = 12                     # SURF -> TEXT


def to565(c, exact=False):
    r, g, b = c
    if r == g == b and not exact:                     # a gray stays on the RGB565 gray axis (G = 2 R)
        return ((r >> 3) << 11) | ((r >> 3) << 6) | (r >> 3)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def from565(v):
    return ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


def cdiv(a, b):
    return a // b if a >= 0 else -((-a) // b)


def mix(a, b, pct, mono=False):
    """src/gfx.c ux_mix: a + (b - a) * pct / 100 per channel, rounded; GREY, MONO on the 5-bit red channel"""
    if mono:
        x, y = a >> 11, b >> 11
        d = (y - x) * pct
        v = x + cdiv(d + (50 if d >= 0 else -50), 100)
        return (v << 11) | (v << 6) | v
    out = 0
    for sh, mask in ((11, 31), (5, 63), (0, 31)):
        x, y = (a >> sh) & mask, (b >> sh) & mask
        d = (y - x) * pct
        out |= (x + cdiv(d + (50 if d >= 0 else -50), 100)) << sh
    return out


def luma(c):
    """src/gfx.c ux_luma: 0..255, integer"""
    r, g, b = c >> 11, (c >> 5) & 63, c & 31
    return (r * 255 // 31 * 54 + g * 255 // 63 * 183 + b * 255 // 31 * 19) >> 8


def lum(c565):
    def lin(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = from565(c565)
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def contrast(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def derive(p):
    name, *cols = p
    own_key = to565(cols[5]) if len(cols) > 5 else 0
    bg, surf, text, theme, accent = (to565(c) for c in cols[:5])
    mono = False
    d = dict(bg=bg, surf=surf, text=text, theme=theme, accent=accent,
             mid=mix(bg, text, PCT["MID"], mono), dim=mix(bg, text, PCT["DIM"], mono),
             line=mix(bg, text, PCT["LINE"], mono), sel=mix(bg, theme, PCT["SEL"], mono),
             tint=mix(bg, theme, PCT["TINT"], mono), raise_=mix(surf, text, RAISE_PCT, mono),
             key=mix(bg, text, PCT["KEY"], mono))
    if own_key:
        d["key"] = own_key
    d["ink"] = bg
    d["light"] = luma(bg) > 128
    d["rec"] = to565(REC_LIGHT if d["light"] else REC_DARK)
    return name, d


# (foreground, background, minimum contrast, why)
CHECKS = [("text", "bg", 12.0, "body text"), ("text", "surf", 9.0, "body text on a surface"),
          ("mid", "bg", 5.0, "labels"), ("mid", "surf", 4.0, "labels on a surface"),
          ("dim", "bg", 2.2, "inactive (not read)"), ("line", "bg", 1.25, "1 px dividers"),
          ("theme", "bg", 4.5, "values, curves"), ("theme", "surf", 4.5, "values on a surface"),
          ("accent", "bg", 4.5, "hot value, cursor, playhead"), ("accent", "surf", 4.5, "hot on a surface"),
          ("ink", "sel", 4.5, "text on a selection"), ("ink", "theme", 4.5, "text on a THEME fill"),
          ("sel", "bg", 1.8, "gauge fill vs track"), ("rec", "bg", 3.0, "REC (graphic)"),
          ("text", "raise_", 7.0, "text on a RAISE chip or button"), ("ink", "rec", 4.5, "text on the REC chip"),
          ("ink", "key", 4.5, "a keycap's label"), ("key", "bg", 3.0, "a keycap on the background"),
          ("key", "surf", 2.5, "a keycap on a surface"), ("ink", "dim", 1.8, "an unavailable keycap's label"),
          ("ink", "accent", 4.5, "text on the ARM badge")]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?")
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    fails = []
    rows = [derive(p) for p in MACHINE_PALETTES]
    for name, d in rows:
        for f, b, lim, why in CHECKS:
            c = contrast(d[f], d[b])
            if c < lim:
                fails.append(f"{name}: {f}/{b} {c:.2f} < {lim} ({why})")
    if a.report:
        print(f"{'palette':8s} " + " ".join(f"{f}/{b}".ljust(12) for f, b, _, _ in CHECKS))
        for name, d in rows:
            print(f"{name:8s} " + " ".join(f"{contrast(d[f], d[b]):5.1f}".ljust(12) for f, b, _, _ in CHECKS))
        print("minimum  " + " ".join(f"{lim:<12.2f}" for _, _, lim, _ in CHECKS))
    if a.out:
        out = ["/* generated by tools/gen_ui_palettes.py: the UI palettes (5 colours each, the rest derived) */",
               "#pragma once", "#include <stdint.h>", "",
               "/* ui_pal_t {name, bg, surf, text, theme, accent[, key]} (src/gfx.c): the machines' palettes, in engine order",
               " * (src/gfx.c palette_set: id = machine) */",
               "static const ui_pal_t UI_MACHINE_PALS[] = {"]
        for p in MACHINE_PALETTES:
            out.append(f'    {{"{p[0]}", ' + ", ".join(f"0x{to565(c):04x}" for c in p[1:]) + "},")   # (a 6th colour: the key)
        out += ["};", f"#define UI_NMACHINE {len(MACHINE_PALETTES)}u", "",
                f"#define UI_PAL_TAG {PAL_TAG}u          /* stored id = UI_PAL_TAG + index of the earlier firmware's list; below 20: an older id */",
                f"#define UI_PAL_STORED_MACHINE {PAL_STORED_MACHINE}u   /* MACHINE's id in that list: what is written */"]
        out += [f"#define UI_{k}_PCT {v}" for k, v in PCT.items()] + [f"#define UI_RAISE_PCT {RAISE_PCT}   /* SURF -> TEXT */"]
        out += [f"#define UI_REC_DARK 0x{to565(REC_DARK):04x}u", f"#define UI_REC_LIGHT 0x{to565(REC_LIGHT):04x}u",
                f"#define UI_QR_LIGHT 0x{to565(QR_LIGHT):04x}u", f"#define UI_QR_DARK 0x{to565(QR_DARK):04x}u",
                f"#define UI_CRASH_BG 0x{to565(CRASH_BG):04x}u", f"#define UI_CRASH_INK 0x{to565(CRASH_INK):04x}u", ""]
        Path(a.out).write_text("\n".join(out))
    for f in fails:
        print("FAIL", f)
    if fails:
        sys.exit(1)


if __name__ == "__main__":
    main()
