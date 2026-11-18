#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""MICRO's wavetables (firmware/src/eng_micro.c) -> build/gen/mw_tables.h. Standard library only.

PPG / Microwave-STYLE tables of our own (the plan's §7 option (a)): the originals are PPG's and Waldorf's data and are not
used. Each table is 8 keyframes of one 128-sample cycle, signed 8 bits (the Microwave's waves are 8-bit); the machine
steps through 64 waves between them (eng_micro.c: no crossfade between waves, the PPG's stepped sweep). Built from
additive, sync, pulse, formant, fold and FM formulas, each keyframe normalised to a peak of 127.
"""
import math
import random
import sys

N = 128                                       # samples a cycle
K = 8                                         # keyframes a table
TAU = 2.0 * math.pi


def add(amps, phases=None):
    phases = phases or [0.0] * len(amps)
    return [sum(a * math.sin(TAU * (h + 1) * i / N + phases[h]) for h, a in enumerate(amps) if a) for i in range(N)]


def norm(w):
    m = max(abs(x) for x in w) or 1.0
    return [max(-127, min(127, int(round(127 * x / m)))) for x in w]


def saw_h(nh, odd=False):
    return [((1.0 / (h + 1)) if (not odd or h % 2 == 0) else 0.0) for h in range(nh)]


def table_harm_sweep(k):                      # 0 PPG UPPER: the harmonics come in, up to 2^k
    return add(saw_h(max(1, 2 ** k // 2 + k)))


def table_sync(k):                            # 1 SYNC: a saw hard-synced at ratio 1 .. 8
    r = 1.0 + k
    return [2.0 * ((r * i / N) % 1.0) - 1.0 for i in range(N)]


def table_pulse(k):                           # 2 PWM: duty 50 % down to 6 %
    d = 0.5 - k * 0.0625
    return [1.0 if (i / N) < d else -1.0 for i in range(N)]


def table_formant(k):                         # 3 FORMANT: a resonance walking up the harmonics
    c = 2 + k * 3
    return add([math.exp(-((h + 1 - c) ** 2) / 6.0) + (0.3 if h == 0 else 0) for h in range(40)])


def table_basic(k):                           # 4 BASIC: sine, triangle, square, saw and back between them
    shapes = [
        lambda x: math.sin(TAU * x),
        lambda x: 1 - 4 * abs((x + 0.25) % 1 - 0.5),
        lambda x: 1.0 if x < 0.5 else -1.0,
        lambda x: 2 * x - 1,
    ]
    a, f = divmod(k * 3 / 7.0, 1.0)
    s0, s1 = shapes[int(a)], shapes[min(3, int(a) + 1)]
    return [(1 - f) * s0(i / N) + f * s1(i / N) for i in range(N)]


def table_odd(k):                             # 5 HOLLOW: odd harmonics, brighter down the table
    return add([(1.0 / (h + 1)) ** (1.6 - k * 0.15) if h % 2 == 0 else 0.0 for h in range(31)])


def table_organ(k):                           # 6 DRAWBAR: organ registrations morphing
    regs = ["80800000", "88800000", "80880000", "88880000", "80808080", "88808088", "88888888", "80000888"]
    bars = [int(c) for c in regs[k]]
    foot = [1, 2, 3, 4, 6, 8, 10, 12]
    a = [0.0] * 12
    for b, h in zip(bars, foot):
        a[h - 1] += b / 8.0
    return add(a)


def table_digital(k):                         # 7 DIGITAL: random-phase harmonic clusters (seeded)
    rnd = random.Random(1000 + k)
    return add([rnd.random() * (1.0 / (h + 1)) ** 0.5 if rnd.random() < 0.6 else 0.0 for h in range(24 + k * 4)],
               [rnd.random() * TAU for _ in range(24 + k * 4)])


def table_reso(k):                            # 8 RESO: a windowed resonant partial sweeping up (the CZ / PPG resonance)
    r = 2 + k * 2
    return [math.sin(TAU * r * i / N) * (1 - i / N) for i in range(N)]


def table_vocal(k):                           # 9 VOCAL: aah -> eeh -> ooh formant pairs
    f = [(7, 11), (5, 18), (3, 22), (5, 9), (4, 8), (6, 14), (3, 25), (8, 12)][k]
    return add([math.exp(-((h + 1 - f[0]) ** 2) / 3) + 0.6 * math.exp(-((h + 1 - f[1]) ** 2) / 5) for h in range(32)])


def table_fm(k):                              # 10 FM: sin(x + m sin(3x)), the index up the table
    m = k * 0.7
    return [math.sin(TAU * i / N + m * math.sin(3 * TAU * i / N)) for i in range(N)]


def table_fold(k):                            # 11 FOLD: a sine folded harder down the table
    g = 1 + k * 0.8
    return [math.sin(g * math.sin(TAU * i / N) * math.pi / 2) for i in range(N)]


def table_single(k):                          # 12 PARTIALS: one harmonic a keyframe, 1 .. 8 (the PPG's upper waves)
    return add([1.0 if h == k else (0.25 if h == 0 else 0.0) for h in range(8)])


def table_bell(k):                            # 13 BELL: odd, stretched partials coming in
    return add([(0.9 ** h) if h in (0, 2, 4, 6, 9, 12, 16, 21)[:k + 1] else 0.0 for h in range(24)])


def table_xor(k):                             # 14 BITS: integer patterns of the phase (the digital grit of early wave synths)
    return [(((i * (k + 1)) ^ (i >> 1)) & 127) / 63.5 - 1.0 for i in range(N)]


def table_pad(k):                             # 15 STRINGS: saw ensemble-ish spectra, warm to bright
    return add([(1.0 / (h + 1)) * math.exp(-h / (2 + k * 2)) for h in range(40)])


TABLES = [table_harm_sweep, table_sync, table_pulse, table_formant, table_basic, table_odd, table_organ, table_digital,
          table_reso, table_vocal, table_fm, table_fold, table_single, table_bell, table_xor, table_pad]
NAMES = ["UPPER", "SYNC", "PWM", "FRMNT", "BASIC", "HOLLW", "ORGAN", "DIGIT", "RESO", "VOCAL", "FM", "FOLD",
         "PARTL", "BELL", "BITS", "STRNG"]


def main(out):
    h = ["/* generated by tools/gen_mw_tables.py: do not edit (MICRO, firmware/src/eng_micro.c). PPG-style tables of our",
         " * own: 16 tables x 8 keyframes x 128 signed 8-bit samples */",
         f"#define MW_NT {len(TABLES)}", f"#define MW_NK {K}", f"#define MW_N {N}",
         "static const int8_t MW_WT[MW_NT][MW_NK][MW_N] = {"]
    for t, fn in enumerate(TABLES):
        h.append(f"    {{   /* {t} {NAMES[t]} */")
        for k in range(K):
            w = norm(fn(k))
            h.append("        {" + ", ".join(str(x) for x in w) + "},")
        h.append("    },")
    h += ["};", "static const char *const MW_TNAME[MW_NT] = {" + ", ".join(f'"{n}"' for n in NAMES) + "};"]
    open(out, "w", encoding="utf-8").write("\n".join(h) + "\n")
    print(f"mw tables: {len(TABLES)} x {K} x {N} = {len(TABLES) * K * N} B -> {out}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build/gen/mw_tables.h")
