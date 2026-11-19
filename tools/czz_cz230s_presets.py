#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CZ-Z's factory presets from the Casio CZ-230S's 99 tones as CZ-101 SysEx (github.com/alphacharlie/CZ-Presets; Casio's
factory data, used at the user's decision, 2026-11-19). 50 of the 80 instrument tones (0..79: the drums and effects 80..99
left out), chosen by what CZ-Z carries: the fewest losses within each category (the four basses all in).

  tools/czz_cz230s_presets.py [CZ230S_DIR]     (default ../CZ-Presets-master/CZ-Presets-master/CZ-230S)

Writes presets/four/czz.py (generated) and docs/czz_presets.md (what each keeps and loses). The tone layout: the CZ-101's 128
bytes as 256 nibbles, low first (youngmonkey's CZ MIDI notes, archived at github.com/ajwills72/cz101 docs/sysex.md):
PFLAG PDS PDL PDH PVK PVDLD(3) PVSD(3) PVDD(3) MFW(2) MAMD MAMV MWMD MWMV PMAL PMA(16) PMWL PMW(16) PMPL PMP(16) SFW(2)
SAMD SAMV SWMD SWMV PSAL PSA(16) PSWL PSW(16) PSPL PSP(16).

The mapping (eng_cz.c):
  LINE        PFLAG's line select (1, 2, 1+1', 1+2'); its octave: lost (TRANS is the track's)
  WAV1, WAV2  line 1's and line 2's first waveform (SAW SQR PULSE DSIN SAWP RES1..3); MOD: MFW's ring / noise
  DTN, iv2    line 2's detune: its octaves and notes -> the line 2 interval (iv2, semitones; eng_cz.c slot 18 = 64 + it), its
              fine (60 a semitone, the byte's code shifted left by 2) -> DTN cents
  DCW         line 1's 8-step DCW envelope as it is (rates, levels 0..99, sustain and end steps: the CZ's own units), CUT 0 and
              DCWE 127 so it alone sets the DCW (CZ-Z's DCW = CUT + DCWE x the envelope, as the CZ's), KEYF = its key follow
  amp         line 1's 8-step DCA envelope as an ADSR: the first step the attack, the steps to the sustain step the decay
              (its level the sustain), the rest the release (CZ-Z's rate curve, 20 s x 2^(-rate / 7) a full sweep)
  vibrato     PVK / delay / rate / depth -> the track LFO (TRI SQR SAW), its fade, LFO DEST PIT (CZ_LX: by ear)
  lost        line 2's own envelopes (CZ-Z's lines share line 1's), each line's second waveform, the DCO (pitch) envelopes,
              the DCA key follow
"""
import glob
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_PY = ROOT / "presets" / "four" / "czz.py"
OUT_MD = ROOT / "docs" / "czz_presets.md"
DEF_SRC = ROOT.parent / "CZ-Presets-master" / "CZ-Presets-master" / "CZ-230S"
QUOTA = {"KEYS": 22, "LEAD": 13, "ATMOS": 11, "BASS": 4}
FX = dict(KEYS=(0, 10, 16, 30), LEAD=(0, 10, 30, 26), ATMOS=(0, 20, 24, 60), BASS=(0, 0, 0, 8))
PAT = dict(KEYS=3, LEAD=4, ATMOS=5, BASS=2)
CAT = {}
for n in ("Symphonic Ens. 1", "Symphonic Ens. 2", "Symphonic Ens. 3", "String Ens. 1", "String Ens. 2", "Synth Ens. 1",
          "Synth Ens. 2", "Sweet Strings", "Female Chorus", "Male Chorus", "Space Voice 1", "Space Voice 2", "Magical Wind",
          "Southern Wing", "Mars Sound", "Fantasy 1", "Fantasy 2", "Soft Attack", "Light Attack"):
    CAT[n] = "ATMOS"
for n in ("Brass Ens. 1", "Brass Ens. 2", "Brass Ens. 3", "Funky Horn", "Slap Horn", "Trumpet", "Flute", "Whistle", "Violin",
          "Cello", "Blues Harmonica", "Shakuhachi", "Synth Reed", "Double Reed", "Slash Reed", "Accordion", "Wah Voice",
          "Meow Attack"):
    CAT[n] = "LEAD"
for n in ("Elec. Bass 1", "Elec. Bass 2", "Slap Bass", "Metallic Bass"):
    CAT[n] = "BASS"
SHORT = {"SYMPHONIC ENS. 1": "SYMPHONIC 1", "SYMPHONIC ENS. 2": "SYMPHONIC 2", "SYMPHONIC ENS. 3": "SYMPHONIC 3",
         "STRING ENS. 1": "STRINGS 1", "STRING ENS. 2": "STRINGS 2", "SYNTH ENS. 1": "SYNTH ENS 1", "SYNTH ENS. 2": "SYNTH ENS 2",
         "SWEET STRINGS": "SWEET STRNGS", "FEMALE CHORUS": "FEMALE CHOIR", "BRASS ENS. 1": "BRASS ENS 1",
         "BRASS ENS. 2": "BRASS ENS 2", "BRASS ENS. 3": "BRASS ENS 3", "BLUES HARMONICA": "BLUES HARP",
         "HONKY-TONK PIANO": "HONKY TONK", "METAL HONKY-TONK": "METAL HONKY", "SYNTH CELESTA": "SYNTH CELSTA",
         "ELEC. PIANO": "ELEC PIANO", "SYNTH CLAVI.": "SYNTH CLAVI", "SOFT XYLOPHONE": "SOFT XYLO", "ACO. GUITAR 1": "ACO GUITAR 1",
         "ACO. GUITAR 2": "ACO GUITAR 2", "SEMIACO. GUITAR": "SEMIACO GTR", "ELEC. GUITAR 1": "ELEC GTR 1",
         "ELEC. GUITAR 2": "ELEC GTR 2", "ELEC. BASS 1": "ELEC BASS 1", "ELEC. BASS 2": "ELEC BASS 2",
         "METALLIC BASS": "METAL BASS", "METALLIC SOUND": "METAL SOUND", "PLUNK EXTEND": "PLUNK EXTND",
         "HARPSICHORD 1": "HARPSICHRD 1", "HARPSICHORD 2": "HARPSICHRD 2", "DOUBLE ATTACK": "DOUBLE ATTCK",
         "SYNTH VIB. 1": "SYNTH VIB 1", "SYNTH VIB. 2": "SYNTH VIB 2", "SYNTH VIB. 3": "SYNTH VIB 3", "SPACE VOICE 1": "SPACE VOX 1",
         "SPACE VOICE 2": "SPACE VOX 2", "SOUTHERN WING": "SOUTHRN WING", "MAGICAL WIND": "MAGICAL WIND"}
WAVE = {0: 0, 1: 1, 2: 2, 4: 3, 5: 4}                   # 3-bit codes -> CZ-Z's SAW SQR PULS DSIN SAWP (6: RES1..3 by suffix)


def tone(path):
    b = Path(path).read_bytes()
    body = b[6:6 + 256]
    if len(b) != 263 or b[0] != 0xF0 or b[1] != 0x44:
        raise SystemExit(f"{path}: not a CZ-101 tone dump")
    return bytes((body[2 * j] & 15) | ((body[2 * j + 1] & 15) << 4) for j in range(128))


def wave_of(b0, b1):
    w = b0 >> 5
    if w == 6:
        return 4 + max(1, b1 >> 6)                       # RES1..3: the suffix in the second byte's top bits
    return WAVE.get(w, 0)


def env8(d, at, rate):
    """an 8-step envelope at offset at (its end step one before): rates and levels as the panel's 0..99, sustain step, end"""
    end = d[at - 1] & 7
    r, l, sus = [], [], None
    for s in range(8):
        rb, lb = d[at + 2 * s] & 0x7F, d[at + 2 * s + 1]
        r.append(rate(rb))
        l.append(0 if (lb & 0x7F) == 0 else min(99, 99 * (lb & 0x7F) // 127 + 1))
        if lb & 0x80 and sus is None and s <= end:
            sus = s
    return r, l, sus, end


def dca_rate(b):
    return 0 if b == 0 else min(99, 99 * b // 119 + 1)


def dcw_rate(b):
    return 0 if b <= 8 else min(99, 99 * (b - 8) // 119 + 1)


def step_ms(a, b, r):
    return abs(b - a) / 99.0 * 20000.0 * 2.0 ** (-r / 7.0)


def time_knob(ms):
    return max(0, min(127, round(127 * math.log10(max(ms, 1.0)) / 4.0)))


def adsr(r, l, sus, end):
    """the CZ's 8-step DCA as A D S R"""
    peak = max(l[:end + 1]) or 1
    a = step_ms(0, l[0], r[0])
    last = sus if sus is not None else end
    d = sum(step_ms(l[s - 1], l[s], r[s]) for s in range(1, last + 1))
    s_lv = l[sus] / peak if sus is not None else 0.0
    rel = sum(step_ms(l[s - 1], l[s], r[s]) for s in range(last + 1, end + 1)) if sus is not None else step_ms(l[end], 0, r[end])
    return [time_knob(a), time_knob(d), round(127 * s_lv), time_knob(rel or 20)]


def convert(d, name):
    lost, notes, score = [], [], 0.0
    line = d[0] & 3
    octv = (d[0] >> 2) & 3
    if octv:
        lost.append(f"the tone's octave {'+1' if octv == 1 else '-1'}")
        score += 1
    sign = -1 if d[1] else 1
    enc = d[2] >> 2                                      # (the fine byte holds the panel's code shifted left by 2)
    fine = 15 * (enc >> 4) + (enc & 15)
    iv2 = max(-48, min(48, sign * d[3]))                 # PDH = 12 x octave + note: line 2's interval in semitones
    dtn = min(127, round(fine * 100 / 60))               # the fine detune (60 a semitone) as DTN cents
    if iv2 or fine:
        notes.append(f"line 2 {iv2:+d} st {fine}/60")
    m0, m1, s0, s1 = d[14], d[15], d[71], d[72]
    w1, w2 = wave_of(m0, m1), wave_of(s0, s1)
    if m0 & 2:
        lost.append("line 1's second waveform")
        score += 1.5
    if s0 & 2 and line:
        lost.append("line 2's second waveform")
        score += 1
    mod = {4: 1, 3: 2}.get((m1 >> 3) & 7, 0)
    # envelopes: line 1's DCA, DCW, DCO
    ar, al, asus, aend = env8(d, 21, dca_rate)
    wr, wl, wsus, wend = env8(d, 38, dcw_rate)
    pr, pl, _, pend = env8(d, 55, lambda b: b * 99 // 127)
    if aend > 3:
        notes.append(f"DCA {aend + 1} steps as an ADSR")
        score += 0.5
    if line in (2, 3) and (d[77:111] != d[20:54]):
        lost.append("line 2's own DCA / DCW envelopes")
        score += 1 if line == 3 else 0.5
    pdepth = max(max(pl[:pend + 1]), max(env8(d, 112, lambda b: b)[1][:(d[111] & 7) + 1]))
    if pdepth:
        lost.append(f"the pitch envelope (level {pdepth})")
        score += pdepth / 20
    env = adsr(ar, al, asus, aend)
    keyf = min(127, (d[18] & 15) * 14)
    # vibrato
    lx = [0, 0, 0, 0, 0]
    if d[11]:
        hz = 5.5 * 2 ** ((d[8] - 53) / 14)
        lx = [1, max(0, min(127, round(127 * math.log(max(hz, 0.05) / 0.05) / math.log(800)))),
              {0x08: 1, 0x04: 3, 0x20: 2, 0x02: 2}.get(d[4], 1),
              time_knob(d[5] * 25) if d[5] else 0, max(1, min(63, round(d[11] / 40 * 63 / 11.8)))]
        notes.append("vibrato as the LFO")
    dcw = dict(r=wr, l=wl, sus=wsus, end=wend)
    p = dict(name=name, e=[w1, w2, line, dtn, 0, keyf, mod, 127], env=env, fenv=0, dcw=dcw, iv2=iv2, lx=lx)
    return p, lost, notes, score


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else DEF_SRC
    names = [l.split(" - ", 1)[1].rsplit(" (Pre", 1)[0] for l in (src / "CZ230Sindex.txt").read_text().splitlines()
             if " - " in l and "(Pre" in l]
    files = sorted(glob.glob(str(src / "presets" / "*.syx")))[:80]
    cand = []
    for k, f in enumerate(files):
        n = names[k]
        nm = SHORT.get(n.upper(), n.upper().replace(".", ""))[:12].rstrip()
        p, lost, notes, score = convert(tone(f), nm)
        p["cat"] = CAT.get(n, "KEYS")
        cand.append((score, k, n, p, lost, notes))
    pick = []
    for cat in ("LEAD", "ATMOS", "KEYS", "BASS"):
        c = sorted([x for x in cand if x[3]["cat"] == cat], key=lambda x: (x[0], x[1]))[:QUOTA[cat]]
        if len(c) != QUOTA[cat]:
            raise SystemExit(f"czz_cz230s_presets: {len(c)} {cat} tones, want {QUOTA[cat]}")
        pick += sorted(c, key=lambda x: x[1])
    out = []
    for score, k, n, p, lost, notes in pick:
        p["mono"] = 1 if p["cat"] in ("LEAD", "BASS") else 0
        p["fx"] = FX[p["cat"]]
        p["pat"] = PAT[p["cat"]]
        out.append(p)
    L = ["# CZ-Z: 50 factory presets from the Casio CZ-230S's tones (CZ-101 SysEx, github.com/alphacharlie/CZ-Presets; Casio's",
         "# factory data, the user's decision 2026-11-19). GENERATED by tools/czz_cz230s_presets.py: do not edit by hand.",
         "# e = [WAV1, WAV2, LINE, DTN, CUT, KEYF, MOD, DCWE]; dcw = line 1's 8-step DCW envelope (the CZ's own rates and levels);",
         "# oct2 = line 2's octave; lx = CZ_LX (eng_cz.c cz_track_loaded): the vibrato as the LFO {set, RATE, WAVE, FADE, DEST PIT}.",
         "# docs/czz_presets.md: what each keeps and loses. The first voicing this replaces (2026-11-18) is in git.", "", "PRESETS = ["]
    L += ["    " + repr(p) + "," for p in out]
    L.append("]")
    OUT_PY.write_text("\n".join(L) + "\n", newline="\n")
    M = ["# CZ-Z presets: the Casio CZ-230S's tones on CZ-Z", "",
         "Generated by `tools/czz_cz230s_presets.py` from the CZ-230S's 99 tones as CZ-101 SysEx. The 50 of its 80 instrument",
         "tones that lose least on CZ-Z, by category. CZ-Z plays the uPD933 chip itself; the CZ's envelopes beyond line 1's DCW",
         "are approximated or lost, per preset below.", "",
         "| # | CZ-Z | CZ-230S tone | Category | Notes | Lost |", "|---|---|---|---|---|---|"]
    for i, (score, k, n, p, lost, notes) in enumerate(pick):
        M.append(f"| {i + 1} | {p['name']} | {n} (Pre {k:02d}) | {p['cat']} | {'; '.join(notes) or '-'} | {'; '.join(lost) or '-'} |")
    rest = sorted((x for x in cand if x not in pick), key=lambda x: x[1])
    M += ["", "Left out (more lost): " + ", ".join(f"{x[2]} (Pre {x[1]:02d})" for x in rest) + "."]
    OUT_MD.write_text("\n".join(M) + "\n", newline="\n")
    print(f"czz_cz230s_presets: {len(out)} presets -> {OUT_PY.relative_to(ROOT)}, report {OUT_MD.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
