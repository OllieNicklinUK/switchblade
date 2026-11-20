#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""JP-Z's 50 factory presets (2026-11-20, the user's choice): 21 of Roland's own JP-8000 factory patches, read from the
JP-8000 v1.05 firmware ROM, and 29 of Alan Marcero's JP-8000 / JP-8080 bank (jp80x0_rev_2, his permission 2026-11-19:
CREDITS.md). All are ones whose oscillators JP-Z plays (OSC1 SUPER SAW or SAW, OSC2 SAW / PULSE / TRI, no sync or ring).
Names: at most 10 characters (the home screen's large type).

  tools/jpz_presets.py [ROM.bin [BANK.syx]]
      ROM   default ../ROMS/jp8000_v1.05.bin (beside the repo; not in it: Roland's firmware)
      BANK  default presets/jp/alan_marcero_jp80x0_rev_2.syx

Writes presets/four/jpz.py (generated: tools/four_presets.py turns it into JPZ_PRESETS / JPZ_PX) and docs/jpz_presets.md,
what each keeps and loses.

Alan's bank: one DT1 message a patch (address 02 0p pp 00, 240 bytes: 0x00..0x7F, then 0x100..0x16F), the layout of
gearmulator's jeLib/jemiditypes.h (enum Patch).
The ROM: 128 single patches from 0x562B6, 160 bytes each; then the 69 performances from 0x5B2B6, 0x180 bytes each (a
64-byte header, the Upper patch at +0x40, the Lower at +0xE0). A ROM patch's bytes 0x00..0x49 (name, LFO .. OSC SHIFT) are
the SysEx layout's own (the enumerated parameters' ranges match it exactly); 0x4A.. are the firmware's internal format (its
velocity and CONTROL assignments), not read: a ROM patch converts with VELOCITY off.

The mapping (eng_jpz.c):
  OSC1        SUPER SAW: DTN = CTRL1 (the detune, JP-Z's JPZ_DTN is the JP's knob curve), MIX = CTRL2 (the side saws);
              SAW: MIX 0 (the centre saw alone)
  OSC2        its waveform and the pulse's width (CTRL1); RANGE -> OSC2 semitones (+-WIDE: +-24); FINE (cents); BALANCE -> BAL
  filter      TYPE and SLOPE -> P_FTYPE (LP 24 / 12, BP, HP 12 / 24); CUTOFF, RESONANCE 1:1; KEY FOLLOW (+ only) x 2 -> KTR;
              the filter envelope -> FLT ENV (A D S R 1:1; its velocity sensitivity 64 with the patch's VELOCITY on), its
              DEPTH -> ENV DEST FLT
  amp         its envelope -> A D S R; AMP LEVEL; MONO / LEGATO / PORTAMENTO -> mono, GLIDE; OSC SHIFT
  pitch env   DEPTH (63 = +12 semitones: by ear), A, D, to OSC1+2 or OSC2
  LFO         LFO1 (wave, rate, fade; its OSC / FILTER / AMP depths -> LFO DEST PIT FLT AMP, the pitch's 63 = +-2 semitones:
              PIT_K) when it is used; else LFO2 (the
              JP's mod-lever LFO: its PITCH depth on LFO DEST PIT, the matrix's MODW -> DEPTH so the wheel brings it in)
  FX          MULTI-FX: SUPER CHORUS -> WID (JP-Z's part chorus I / II / I+II), FLANGER / PHASER -> the CHORUS send,
              DISTORTION -> DIST; DELAY LEVEL -> the DELAY send (its type and time are the FM-1's global delay); REVERB: the
              category's send (the JP has none)
  lost        LFO2 where LFO1 is used, LFO1 / the pitch envelope on OSC2 alone or on X-MOD, the velocity and morph (CONTROL)
              routings, TONE CONTROL, the bend ranges, OSC2 CTRL2, the delay's type and time
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_PY = ROOT / "presets" / "four" / "jpz.py"
OUT_MD = ROOT / "docs" / "jpz_presets.md"
DEF_ROM = ROOT.parent / "ROMS" / "jp8000_v1.05.bin"
DEF_SYX = ROOT / "presets" / "jp" / "alan_marcero_jp80x0_rev_2.syx"

ROM_SINGLES, ROM_PERFS, ROM_PATCH, ROM_PERF = 0x562B6, 0x5B2B6, 160, 0x180

# the 50 (2026-11-20), listed LEAD, ATMOS, KEYS, BASS: (source, slot, WAVE category, FM-1 name of at most 10)
#   source "A": Alan's bank, slot 1..128; "R": the ROM, "S<n>" its single patch n (1..128) or "P<n>U" / "P<n>L" a part of
#   performance n (1..69)
PICK = [
    ("A", 34, "LEAD", "CLASSIC"), ("A", 73, "LEAD", "SAW LEGND1"), ("A", 78, "LEAD", "SAW LEGND6"),
    ("A", 80, "LEAD", "SAW LEGND8"), ("A", 90, "LEAD", "TRANCY ARP"), ("A", 24, "LEAD", "TRANCE 99"),
    ("A", 25, "LEAD", "70S SPRSAW"), ("A", 31, "LEAD", "BPF SAW"), ("A", 37, "LEAD", "DOUBLETREE"),
    ("A", 43, "LEAD", "FUNNY FACE"), ("A", 44, "LEAD", "GAS BRASS"), ("A", 52, "LEAD", "JP8 BRASS"),
    ("A", 54, "LEAD", "JUST 2 OSC"), ("A", 60, "LEAD", "NO SPR REQ"), ("A", 62, "LEAD", "PHRYGIAN"),
    ("A", 67, "LEAD", "PUNCHY 7TH"), ("A", 72, "LEAD", "RESY BETSY"), ("A", 94, "LEAD", "WHATS THAT"),
    ("R", "S073", "ATMOS", "STR MACHIN"), ("R", "S074", "ATMOS", "TRON VLNS"), ("R", "S075", "ATMOS", "LUX SYMPH"),
    ("R", "S076", "ATMOS", "DEBUSSY"), ("R", "S077", "ATMOS", "BPF STRING"), ("R", "S078", "ATMOS", "DETUNE STR"),
    ("R", "S079", "ATMOS", "JUNO B81"), ("R", "S080", "ATMOS", "RICHLAND"), ("R", "P28L", "ATMOS", "EMPEROR"),
    ("A", 104, "ATMOS", "INTO SUN"), ("A", 118, "ATMOS", "BLANKET"), ("A", 111, "ATMOS", "PHASED"),
    ("A", 119, "ATMOS", "WHISP"),
    ("R", "S028", "KEYS", "SEP WAYS"), ("R", "S029", "KEYS", "FOR RPS"), ("R", "S030", "KEYS", "BREAD+BUTR"),
    ("R", "S031", "KEYS", "SILK 5THS"), ("R", "P01U", "KEYS", "CHARIOTS"), ("R", "P12U", "KEYS", "1979"),
    ("R", "S096", "KEYS", "REPLICANT"), ("A", 128, "KEYS", "VAN DYK"), ("A", 123, "KEYS", "DETUNED"),
    ("A", 125, "KEYS", "RUSH"),
    ("R", "S006", "BASS", "BIG+DARK"), ("R", "S007", "BASS", "BASS FLOW"), ("R", "S008", "BASS", "JUNO BASS"),
    ("R", "S010", "BASS", "JUICE BASS"), ("R", "S011", "BASS", "DREAMS"), ("A", 12, "BASS", "MOOGTASTIC"),
    ("A", 19, "BASS", "SUB PUNCH"), ("A", 22, "BASS", "TYAS BASS"), ("A", 63, "BASS", "PORTA SAW"),
]
NAME_MAX = 10
REV = dict(LEAD=26, ATMOS=60, KEYS=30, BASS=8)        # the reverb send by category (the JP has no reverb)
PAT = dict(LEAD=4, ATMOS=5, KEYS=3, BASS=2)
OSC1 = ["SUPER SAW", "TRI MOD", "NOISE", "FEEDBACK", "SQUARE", "SAW", "TRI"]
OSC2 = ["PULSE", "TRI", "SAW", "NOISE"]
LWAVE = {0: 1, 1: 2, 2: 3, 3: 4}                       # the JP's LFO1 TRI SAW SQR S/H -> Felucca's TRI SAW SQR S&H
MS_MODW, MD_DEPTH = 6, 20                              # mod.c MS_MODW, MD_DEPTH
# the JP's LFO pitch depth (+-63) as LFO DEST PIT (63 = 11.8 semitones there): 63 = +-2 semitones (by ear: 1:1 made the vibratos of
# the device test a pitch wobble of up to +-2.8 semitones; the JP's own scale is not measured)
PIT_K = 2.0 / 11.8


def clampi(v, lo, hi):
    return int(max(lo, min(hi, round(v))))


def alan_patches(path):
    b = Path(path).read_bytes()
    out, i = [], 0
    while (s := b.find(b"\xf0", i)) >= 0:
        e = b.find(b"\xf7", s)
        m = b[s:e + 1]
        if len(m) == 252 and m[1] == 0x41 and m[5] == 0x12 and m[6] == 0x02:
            out.append(m[10:-2])
        i = e + 1
    if len(out) != 128:
        raise SystemExit(f"jpz_presets: {len(out)} patches in {path}, want 128")
    return out


def rom_patch(rom, tag):
    """a ROM patch as a 240-byte SysEx-layout patch (0x00..0x49 its own, the rest 0: VELOCITY off)"""
    if tag[0] == "S":
        n = int(tag[1:])
        o = ROM_SINGLES + (n - 1) * ROM_PATCH
    else:
        n = int(tag[1:-1])
        o = ROM_PERFS + (n - 1) * ROM_PERF + (0x40 if tag[-1] == "U" else 0xE0)
    p = rom[o:o + ROM_PATCH]
    if len(p) != ROM_PATCH or not all(32 <= c < 127 for c in p[:16]):
        raise SystemExit(f"jpz_presets: no patch at ROM {tag} (0x{o:X}): is it the JP-8000 v1.05 ROM?")
    return bytes(p[:0x4A]) + bytes(240 - 0x4A)


def convert(d, cat, name):
    def g(a):
        return d[a] if a < 0x80 else d[a - 0x100 + 0x80]

    def sg(a):
        return g(a) - 64

    lost, notes = [], []
    o1, o2 = g(0x1E), g(0x21)
    if OSC1[o1] not in ("SUPER SAW", "SAW") or OSC2[o2] == "NOISE" or g(0x22) or g(0x15):
        raise SystemExit(f"{name}: OSC1 {OSC1[o1]} / OSC2 {OSC2[o2]} / sync / ring: not one JP-Z plays")
    if g(0x16):
        lost.append(f"cross modulation {g(0x16)}")
    dtn, mix = (g(0x1F), g(0x20)) if o1 == 0 else (0, 0)
    bal = sg(0x17)
    rng = g(0x23)
    semis = clampi(rng - 25, -24, 24)
    if rng in (0, 50):
        lost.append("OSC2 RANGE WIDE (as +-24)")
    fine = g(0x24) - 50
    w2 = {2: 0, 0: 1, 1: 2}[o2]
    pw = g(0x25) if w2 == 1 else 0
    ft, slope = g(0x27), g(0x28)
    ftype = {(2, 1): 0, (2, 0): 3, (1, 0): 1, (1, 1): 1, (0, 0): 2, (0, 1): 4}[(ft, slope)]
    if (ft, slope) == (1, 1):
        lost.append("BPF 24 dB (as 12)")
    ktr = clampi(2 * sg(0x2B), 0, 127)
    if sg(0x2B) < 0:
        lost.append(f"negative KEY FOLLOW {sg(0x2B)}")
    vel = g(0x11B)
    fe = [64 if vel else 1, g(0x2F), g(0x30), g(0x31), g(0x32)]
    fenv = clampi(sg(0x2E), -64, 63)
    env = [g(0x36), g(0x37), g(0x38), g(0x39)]
    lvl = 0 if g(0x33) >= 127 else max(1, g(0x33))
    mono = int(bool(g(0x47) or g(0x48)))
    glide = g(0x46) if g(0x45) else 0
    shift = g(0x49) - 2
    # pitch envelope
    pdest = g(0x18)
    penv = [0, 0, 0, 127]
    if abs(sg(0x1B)) >= 1:
        if pdest == 2:
            lost.append("pitch envelope on X-MOD")
        else:
            penv = [g(0x1B), g(0x1C), g(0x1D), 127 if pdest == 0 else 0]
            notes.append(f"pitch env {sg(0x1B):+d}" + (" (OSC2)" if pdest == 1 else ""))
    # the LFO
    l1 = [sg(0x19), sg(0x2C), sg(0x34)]
    l2 = [sg(0x1A), sg(0x2D), sg(0x35)]
    lfo, ld, mx = [0, 0, 0, 0, 0], [0, 0, 0], []
    if any(abs(x) >= 2 for x in l1):
        lfo = [1, g(0x11), LWAVE.get(g(0x10), 1), 1, g(0x12)]
        ld = [clampi(l1[0] * PIT_K, -64, 63), clampi(l1[1], -64, 63), clampi(l1[2], -64, 63)]
        if pdest == 1 and abs(l1[0]) >= 2:
            lost.append("LFO1 pitch on OSC2 alone (here both)")
        if pdest == 2:
            lost.append("LFO1 on X-MOD")
        if any(abs(x) >= 2 for x in l2):
            lost.append("LFO2, the mod lever's (JP-Z: one LFO, LFO1 kept)")
    elif any(abs(x) >= 2 for x in l2):
        lfo = [1, g(0x13), 0, 1, 0]
        ld = [clampi(l2[0] * PIT_K, -64, 63), clampi(l2[1], -64, 63), clampi(l2[2], -64, 63)]
        mx = [MS_MODW, MD_DEPTH, 63]
        notes.append("LFO2 on the mod wheel")
    # FX
    mt, ml, dl = g(0x3D), g(0x3E), g(0x42)
    wid, cho, dist = 0, 0, 0
    if ml:
        if mt <= 3:
            wid = {0: 20, 1: 60, 2: 100, 3: 60}[mt]
            notes.append("SUPER CHORUS as WID")
        elif mt <= 11:
            cho = clampi(ml * 0.6, 0, 126)
            notes.append("flanger / phaser as the CHORUS send")
        else:
            dist = clampi(ml * 0.5, 0, 126)
    fx = (dist, cho, clampi(dl * 0.7, 0, 126), REV[cat])
    lost.append("velocity / CONTROL (morph) routings") if vel else None
    px = dict(ftype=ftype, fe=fe, penv=penv, osc2=[w2, pw], fine=(fine + 64) if fine else 0,
              shift=(shift + 3) if shift else 0, lvl=lvl, lfo=lfo, ld=ld, glide=glide, mx=mx + [0] * (12 - len(mx)))
    e = [dtn, mix, bal, semis, g(0x29), g(0x2A), ktr, wid]
    return dict(name=name, cat=cat, e=e, env=env, fenv=fenv, mono=mono, fx=fx, px=px), lost, notes


def main():
    rom_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEF_ROM
    syx_path = Path(sys.argv[2]) if len(sys.argv) > 2 else DEF_SYX
    if not rom_path.exists():
        raise SystemExit(f"jpz_presets: no ROM at {rom_path} (the JP-8000 v1.05 firmware, 524,288 bytes)")
    rom = rom_path.read_bytes()
    A = alan_patches(syx_path)
    out, rep, names = [], [], set()
    for src, slot, cat, name in PICK:
        if len(name) > NAME_MAX or name in names:
            raise SystemExit(f"jpz_presets: {name!r}: names must be unique, at most {NAME_MAX} characters")
        names.add(name)
        if src == "A":
            d, who = A[slot - 1], f"Alan {slot}"
        else:
            d, who = rom_patch(rom, slot), f"Roland {slot}"
        p, lost, notes = convert(d, cat, name)
        p["pat"] = PAT[cat]
        out.append(p)
        rep.append((who, d[:16].decode("ascii", "replace").strip(), p, lost, notes))
    L = ["# JP-Z: 50 factory presets, 21 of Roland's JP-8000 factory patches (the v1.05 firmware ROM) and 29 of Alan Marcero's",
         "# JP-8000 / JP-8080 bank (jp80x0_rev_2, his permission 2026-11-19, CREDITS.md). GENERATED by tools/jpz_presets.py: do not",
         "# edit by hand; change the converter and run it again. docs/jpz_presets.md: what each keeps and loses.",
         "# e = [DTN, MIX, BAL, OSC2, CUT, RES, KTR, WID]; px = JPZ_PX (eng_jpz.c jpz_track_loaded): ftype, fe = FLT ENV",
         "# {LVL A D S R}, penv {depth+64 (0 off), A, D, dest (< 64 OSC2)}, osc2 {wave 0 SAW 1 PULSE 2 TRI, PW}, fine (cents+64,",
         "# 0 none), shift (octaves+3, 0 none), lvl (AMP LEVEL, 0 = 127), lfo {set, RATE, WAVE, TRIG, FADE}, ld = LFO DEST PIT FLT",
         "# AMP, glide, mx = the matrix (SRC DST AMT x 4). The banks this replaces are in git.",
         "", "PRESETS = ["]
    L += ["    " + repr(p) + "," for p in out]
    L.append("]")
    OUT_PY.write_text("\n".join(L) + "\n", newline="\n")
    M = ["# JP-Z presets: Roland's JP-8000 factory patches and Alan Marcero's bank on JP-Z", "",
         "Generated by `tools/jpz_presets.py`: 21 of Roland's JP-8000 factory patches (read from the v1.05 firmware ROM; S = a",
         "single patch, P = a performance's Upper / Lower part; converted with VELOCITY off) and 29 of Alan Marcero's",
         "`jp80x0_rev_2` bank (used with his permission, 2026-11-19). All are ones whose oscillators JP-Z plays. Per preset:",
         "what the conversion approximates (notes) and what it could not carry (lost).", "",
         "| # | JP-Z | Source patch | Category | Notes | Lost |", "|---|---|---|---|---|---|"]
    for k, (who, an, p, lost, notes) in enumerate(rep):
        M.append(f"| {k + 1} | {p['name']} | {an} ({who}) | {p['cat']} | {'; '.join(notes) or '-'} | {'; '.join(x for x in lost if x) or '-'} |")
    OUT_MD.write_text("\n".join(M) + "\n", newline="\n")
    print(f"jpz_presets: {len(out)} presets -> {OUT_PY.relative_to(ROOT)}, report {OUT_MD.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
