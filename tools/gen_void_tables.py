#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Wavetables of the VOID engine (eng_void.c): the VOID WAVE plugin's built-in tables 0-8.

The plugin (VoidWave/Source/Data/WavetableBank.cpp) builds each table as 256 frames of 2048 float
samples; position 0..1 picks the frame (frame = position * 255). Since the V2 plugin (D:/VOID SYNTH, 2026-07-12,
f5dead5) every frame is normalised as V2's normalize(): its DC removed, then scaled to an RMS of 0.35, the scale
cut where the peak would pass 0.999 (V1 peak-normalised to 1.0: the tables are 4-7 dB quieter, each by its own
amount). The formulas are V1's, unchanged in V2. Here:
  - the same frames, from the same formulas (the Noise table's phases from JUCE's Random, its LCG
    and nextFloat reproduced), but only at KEYS: the frames between are a linear crossfade of the
    two keyframes around them (KEYS chosen with --choose: within KEY_DB of the plugin's frames).
    Two exceptions: SQUARE (a pulse whose width moves: no crossfade of two widths is a width
    between) is one band-limited saw here, the engine's pulse the difference of two readings of
    it (any width, band-limited, DC-free; the plugin's pulse is naive), its V2 level a gain per
    position (VOID_SQ_GAIN: normalize() of the naive pulse of that width); NOISE (random phases per frame:
    neighbours are unrelated; no factory preset uses it) keeps 5 evenly spaced frames;
  - each keyframe band-limited per octave from its spectrum (DFT of the 2048 samples): level l has
    N0 >> l samples and the harmonics up to (N0 >> l) / 4, so the engine reads with linear
    interpolation at >= 4 samples per cycle of the top harmonic and picks the level by pitch.
    The plugin has no band-limiting (it aliases); the DC is left out (its master DC blocker
    removes it there);
  - int16, one scale for every table (VOID_WT_ONE = the plugin's 1.0).

  gen_void_tables.py OUT.h          the header (standard library only)
  gen_void_tables.py --choose [DB]  choose KEYS again (needs numpy): prints the new KEYS
"""
import cmath
import math
import sys
from pathlib import Path

FRAMES = 256                     # the plugin's frames per table
FSIZE = 2048                     # the plugin's samples per frame
NLEV = 6                         # mip levels per keyframe
KEY_DB = -30.0                   # --choose: the crossfade within this of the plugin's frame (RMS) ..
KEY_NEAR = 80                    # .. up to this frame (position 0.31: the factory presets sit at 0, their WT
KEY_DB_FAR = -20.0               # drift reaches 0.055, their LFO sweeps ~0.3), and within this above it

NAMES = ["SINE", "SAW", "SQUARE", "SPECTRL", "FORM A", "FORM E", "BASS 1", "BASS 2", "NOISE"]
# level-0 length: 4 samples per cycle of the top harmonic kept. 128 (32 harmonics) where the plugin's frames
# have nothing above the 32nd (measured: below -50 dB); SQUARE's saw 512 (128 harmonics: a 1/h series)
N0 = [128, 256, 512, 256, 128, 128, 128, 128, 256]
SQUARE, NOISE = 2, 8

# keyframes per table (frame numbers 0..255, the first 0 and the last 255): tools/gen_void_tables.py --choose
KEYS = [[0, 80, 131, 255], [0, 80, 255], [0],
        [0, 11, 21, 34, 45, 54, 63, 72, 80, 102, 124, 146, 167, 189, 211, 233, 255],   # SPECTRAL: chosen again for V2's frames (2026-11-19)
        [0, 21, 41, 60, 80, 127, 167, 211, 255], [0, 20, 40, 60, 80, 123, 166, 209, 255], [0, 80, 255],
        [0, 80, 184, 255], [0, 64, 128, 192, 255]]


# ------------------------------------------------------------------ the plugin's frames ---
def juce_random_floats(seed, n):
    """juce::Random(seed).nextFloat() x n (juce_Random.cpp)"""
    out = []
    for _ in range(n):
        seed = (seed * 0x5DEECE66D + 11) & 0xFFFFFFFFFFFF
        u = (seed >> 16) & 0xFFFFFFFF
        f = u / 4294967296.0
        out.append(min(f, 1.0 - 2.0 ** -23))
    return out


def terms(tbl, frame):
    """the frame as sum amp * sin(mult * a + phase), a = 2 pi s / 2048; None = the SQUARE"""
    t = frame / (FRAMES - 1)
    if tbl == 0:                                       # SINE: harmonics grow with t
        return [(1.0, 1.0, 0.0)] + [(t / h, float(h), 0.0) for h in range(2, 33)]
    if tbl == 1:                                       # SAW -> square: even harmonics fade
        return [((1.0 / h) * ((1.0 - t) if h % 2 == 0 else 1.0), float(h), 0.0) for h in range(1, 49)]
    if tbl == 2:
        return None
    if tbl == 3:                                       # SPECTRAL: a peak sweeping h 1 -> 31
        peak = 1.0 + t * 30.0
        return [(math.exp(-(h - peak) ** 2 * 0.06) + 0.25 / h, float(h), 0.0) for h in range(1, 65)]
    if tbl in (4, 5):                                  # FORMANT: two peaks on a 100 Hz reference
        f1s, f2s, f1e, f2e = (700.0, 1100.0, 270.0, 2300.0) if tbl == 4 else (530.0, 1840.0, 570.0, 840.0)
        f1, f2 = f1s + t * (f1e - f1s), f2s + t * (f2e - f2s)
        return [(math.exp(-((100.0 * h - f1) / 200.0) ** 2) + 0.6 * math.exp(-((100.0 * h - f2) / 300.0) ** 2),
                 float(h), 0.0) for h in range(1, 49)]
    if tbl == 6:                                       # BASS 01 (its 0.5 is a half-sine over the cycle)
        return [(1.0, 1.0, 0.0), (0.7 * t, 0.5, 0.0), (0.4 * t, 2.0, 0.0), (0.2 * t, 3.0, 0.0), (0.12 * t, 4.0, 0.0)]
    if tbl == 7:                                       # BASS 02 (its 1.5 is not periodic in the cycle)
        return [(1.0 - 0.2 * t, 1.0, 0.0), (1.0 - t, 1.5, 0.0)] + \
               [((t / h) * math.exp(-h * 0.06), float(h), 0.0) for h in range(2, 33)]
    if tbl == 8:                                       # NOISE: random phases, slope -1.5 -> -0.5
        ph = juce_random_floats(frame * 1234567 + 987654, 48)
        return [((h + 1) ** -(1.5 - t), float(h + 1), ph[h] * 2.0 * math.pi) for h in range(48)]
    raise ValueError(tbl)


V2_RMS, V2_PEAK = 0.35, 0.999     # WavetableBank.cpp normalize(): targetRMS, the peak cap


def v2_scale(x):
    """V2's normalize() of a frame: (its DC, the scale) -- the frame becomes (x - dc) * scale"""
    n = len(x)
    dc = sum(x) / n
    rms = math.sqrt(sum((v - dc) ** 2 for v in x) / n)
    peak = max(abs(v - dc) for v in x)
    if rms < 1e-6 or peak < 1e-6:
        return dc, 1.0
    sc = V2_RMS / rms
    return dc, (V2_PEAK / peak if sc * peak > V2_PEAK else sc)


def sq_gain(frame):
    """SQUARE at a frame: the V2 scale of the plugin's naive pulse (+-1, width 0.5 - 0.45 t), which is also the scale of
    the engine's DC-free pulse of two saw readings (that pulse is the naive one less its DC)"""
    pw = 0.5 - (frame / (FRAMES - 1)) * 0.45
    edge = int(pw * FSIZE)
    return v2_scale([1.0 if s < edge else -1.0 for s in range(FSIZE)])[1]


def frame_wave(tbl, frame):
    """the plugin's frame: 2048 samples, normalised as V2's normalize() (standard library). SQUARE here: the saw the
    engine makes its pulse of, (2 / pi) sum sin(h a) / h (a pulse of two readings spans +-1; VOID_SQ_GAIN levels it)"""
    if tbl == SQUARE:
        return [(2.0 / math.pi) * sum(math.sin(h * 2.0 * math.pi * s / FSIZE) / h for h in range(1, 129))
                for s in range(FSIZE)]
    if tbl == -1:                                       # SQUARE: naive pulse, 50 % -> 5 %
        pw = 0.5 - (frame / (FRAMES - 1)) * 0.45
        edge = int(pw * FSIZE)
        return [1.0 if s < edge else -1.0 for s in range(FSIZE)]
    tt = terms(tbl, frame)
    out = []
    for s in range(FSIZE):
        a = 2.0 * math.pi * s / FSIZE
        out.append(sum(amp * math.sin(m * a + p) for amp, m, p in tt))
    dc, sc = v2_scale(out)
    return [(v - dc) * sc for v in out]


def fft(x):
    """radix-2 DFT of a real list (len a power of 2): complex bins"""
    n = len(x)
    a = [complex(v) for v in x]
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    size = 2
    while size <= n:
        w = cmath.exp(-2j * math.pi / size)
        for st in range(0, n, size):
            wk = 1.0
            for k in range(size // 2):
                u, v = a[st + k], a[st + k + size // 2] * wk
                a[st + k], a[st + k + size // 2] = u + v, u - v
                wk *= w
        size <<= 1
    return a


def levels(x, n0):
    """band-limited copies of one frame: level l, n0 >> l samples, harmonics 1 .. (n0 >> l) / 4"""
    X = fft(x)
    out = []
    for lv in range(NLEV):
        n = n0 >> lv
        hmax = n // 4
        c = [(2.0 / FSIZE) * X[h] for h in range(hmax + 1)]
        out.append([sum(abs(c[h]) * math.cos(2.0 * math.pi * h * i / n + cmath.phase(c[h]))
                        for h in range(1, hmax + 1)) for i in range(n)])
    return out


# ------------------------------------------------------------------------- the header ---
def arr(name, ctype, vals, per=16):
    out = [f"static const {ctype} {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), per):
        out.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
    out.append("};")
    return out


def header(path):
    keys = KEYS
    lv = [[levels(frame_wave(tb, k), N0[tb]) for k in keys[tb]] for tb in range(len(NAMES))]
    peak = max(abs(v) for tb in lv for key in tb for lev in key for v in lev)
    one = int(32000 / peak)                            # int16 of the plugin's 1.0
    wave, ofs, kpos, kfirst = [], [], [], []
    for tb in range(len(NAMES)):
        kfirst.append(len(kpos))
        for k, key in zip(keys[tb], lv[tb]):
            kpos.append(k)
            ofs.append(len(wave))
            for lev in key:
                wave += [int(round(v * one)) for v in lev]
    kfirst.append(len(kpos))
    L = ["/* generated by tools/gen_void_tables.py: the VOID WAVE plugin's tables 0-8 (eng_void.c) */",
         "#pragma once", "#include <stdint.h>",
         f"#define VOID_NTBL {len(NAMES)}", f"#define VOID_NLEV {NLEV}", f"#define VOID_WT_ONE {one}",
         f"#define VOID_N0LOG2_MIN {min(n.bit_length() - 1 for n in N0)}   /* eng_wave.c void_osc_at: the level search shifts by n0l - 2 - lv, needs >= NLEV */",
         "/* table t: keyframes VOID_KFIRST[t] .. VOID_KFIRST[t + 1] - 1, at the plugin's frames VOID_KPOS[];",
         " * keyframe k: level l at VOID_WAVE + VOID_KOFS[k] + (2 n0 - (2 n0 >> l)), n0 >> l samples, n0 = 1 << VOID_N0LOG2[t] */"]
    L += arr("VOID_N0LOG2", "uint8_t", [n.bit_length() - 1 for n in N0])
    L += arr("VOID_KFIRST", "uint8_t", kfirst)
    L += arr("VOID_KPOS", "uint8_t", kpos)
    L += arr("VOID_KOFS", "uint32_t", ofs, 8)
    L += arr("VOID_WAVE", "int16_t", wave)
    L += ["/* SQUARE: V2's level of the pulse at position i (frame i), Q15 (the pulse of two saw readings x this) */"]
    L += arr("VOID_SQ_GAIN", "uint16_t", [int(round(32767 * sq_gain(f))) for f in range(FRAMES)])
    L += curves()
    Path(path).write_text("\n".join(L) + "\n")
    print(f"void tables: {len(NAMES)} tables, {len(kpos)} keyframes, {len(wave) * 2} B of samples, 1.0 = {one}")


# ------------------------------------------------------------------------- the curves ---
def curves():
    L = []
    # CUT 0..127 -> Felucca's cutoff index (CUTOFF_HZ: 30 Hz * (16000 / 30)^(i / 127)), Q8: the plugin's
    # knob law, 20 + 19980 x^4 Hz (filter_cutoff, skew 0.25)
    q = []
    for v in range(128):
        hz = 20.0 + 19980.0 * (v / 127) ** 4
        i = 127.0 * math.log(hz / 30.0) / math.log(16000.0 / 30.0)
        q.append(int(round(min(max(i, 0.0), 127.0) * 256)))
    L += ["/* CUT 0..127 -> the cutoff index (CUTOFF_HZ, Q8): the plugin's 20 + 19980 x^4 Hz */"]
    L += arr("VOID_CUT_Q8", "uint16_t", q)
    # VOWEL (eng_wave.c): the cutoff index (0..127, +1 for the interpolation) -> the two formants' cutoff indices (Q8, the SVF's
    # g is read at them). The vowels U O A E I sit at CUT 0 / 32 / 64 / 95 / 127 (the five equal steps of the knob, 4 / 127 apiece),
    # i.e. at the cutoff index those knob values give; between them the position is linear in the index, F1 / F2 linear in Hz.
    knob_idx = [min(max(127.0 * math.log((20.0 + 19980.0 * (k / 127) ** 4) / 30.0) / math.log(16000.0 / 30.0), 0.0), 127.0)
                for k in (0, 32, 64, 95, 127)]
    vowels = [(300, 870), (450, 800), (730, 1090), (530, 1840), (270, 2290)]            # U O A E I: F1, F2 Hz
    def vidx(hz):
        return 127.0 * math.log(hz / 30.0) / math.log(16000.0 / 30.0)
    vt = []
    for ci in range(129):
        x = min(float(ci), 127.0)
        j = max(jj for jj in range(4) if knob_idx[jj] <= x) if x < knob_idx[4] else 3
        t = min(max((x - knob_idx[j]) / (knob_idx[j + 1] - knob_idx[j]), 0.0), 1.0)
        for f in range(2):
            vt.append(int(round(256 * vidx(vowels[j][f] + t * (vowels[j + 1][f] - vowels[j][f])))))
    L += ["/* VOWEL: [cutoff index][F1, F2] = the formants' cutoff index, Q8 (U O A E I at CUT 0 / 32 / 64 / 95 / 127; 129 rows) */"]
    L += arr("VOID_VOWEL", "uint16_t", vt)
    # tanh(x) for x = 0 .. 4 in 512 steps, Q15 (TANH_Q15 stops at 2; the ladder's feedback node goes past it)
    L += ["/* tanh(i / 128), i = 0..512, Q15 */"]
    L += arr("VOID_TANH", "int16_t", [int(round(32767 * math.tanh(i / 128))) for i in range(513)])
    # the same on -1..1, signed (no abs, no clamp: the ladder's stages, whose inputs stay within +-1)
    L += ["/* tanh((i - 128) / 128), i = 0..256, Q15 */"]
    L += arr("VOID_TANH1", "int16_t", [int(round(32767 * math.tanh((i - 128) / 128))) for i in range(257)])
    # the voice's soft clipper above 0.72 (VoiceManager.cpp): o / (1 + o) for o = 0 .. 2 in 256 steps, Q15 (a voice
    # reaches o ~1.2 at most: eng_void.c clamps it)
    L += ["/* o / (1 + o), o = i / 128, i = 0..256, Q15 */"]
    L += arr("VOID_SOFT", "int16_t", [int(round(32767 * (i / 128) / (1 + i / 128))) for i in range(257)])
    # the filter envelope's reach (eng_void.c, the plugin's ENV1): the cutoff times 1 + x, x = -1 .. 1, as cutoff steps
    # (Felucca's index: 127 over 30 Hz .. 16 kHz), Q8; down to -64 steps (x -> -1: towards 20 Hz)
    spo = 127 / math.log2(16000 / 30)
    L += ["/* cutoff steps (Q8) of a factor 1 + x, x = (i - 256) / 256, i = 0..512 (at most 64 steps down) */"]
    L += arr("VOID_LOG2P1", "int16_t", [int(round(max(-64.0, spo * math.log2(max(1 + (k - 256) / 256, 1e-6))) * 256))
                                       for k in range(513)])
    # RES 0..127: (1 - r)^1.5, Q15 (r = RES / 127, clamped at 0.9999 as the plugin's): SVF k = 2 x, ladder re = 1 - x
    # NOISE's colour (eng_wave.c, the V2 plugin's noise: a one-pole low-pass on white noise, fc = 200 * 100^c Hz, c = COLOR / 127):
    # its coefficient 1 - exp(-2 pi fc / FS), Q15
    L += ["/* NOISE COLOR 0..127 -> the one-pole's 1 - exp(-2 pi fc / 44100), fc = 200 * 100^(COLOR / 127) Hz, Q15 */"]
    L += arr("VOID_NCOL", "uint16_t", [int(round(32767 * (1 - math.exp(-2 * math.pi * 200 * 100 ** (v / 127) / 44100))))
                                       for v in range(128)])
    L += ["/* (1 - RES / 127)^1.5, Q15 */"]
    L += arr("VOID_RES_P15", "uint16_t", [int(round(32767 * (1 - min(v / 127, 0.9999)) ** 1.5)) for v in range(128)])
    return L


# ------------------------------------------------------------------- choosing KEYS ---
def choose(db):
    import numpy as np
    a = 2.0 * np.pi * np.arange(FSIZE) / FSIZE
    keys = []
    for tb in range(len(NAMES)):
        if tb == SQUARE:
            keys.append([0])
            continue
        if tb == NOISE:
            keys.append([0, 64, 128, 192, 255])
            continue
        n0 = N0[tb]
        hmax = n0 // 4
        fr = []
        for f in range(FRAMES):
            if True:
                x = sum(amp * np.sin(m * a + p) for amp, m, p in terms(tb, f))
                dc, sc = v2_scale(list(x))
                x = (x - dc) * sc
            X = np.fft.rfft(x)
            X[0] = 0.0
            X[hmax + 1:] = 0.0
            fr.append(np.fft.irfft(X[:n0 // 2 + 1] * (n0 / FSIZE), n0))
        fr = np.array(fr)

        def worst(ks):
            w, at = -999.0, -1
            for a0, b0 in zip(ks, ks[1:]):
                for f in range(a0 + 1, b0):
                    u = (f - a0) / (b0 - a0)
                    e = (1 - u) * fr[a0] + u * fr[b0] - fr[f]
                    r = 20 * np.log10(max(np.sqrt(np.mean(e ** 2)), 1e-12) / max(np.sqrt(np.mean(fr[f] ** 2)), 1e-12))
                    r -= db if f <= KEY_NEAR else KEY_DB_FAR      # over its limit by r dB
                    if r > w:
                        w, at = r, f
            return w, at
        ks = [0, FRAMES - 1]
        while True:
            w, at = worst(ks)
            if w <= 0.0 or at < 0:
                break
            ks = sorted(ks + [at])
        print(f"  {NAMES[tb]:8s} {len(ks):2d} keys, worst {w:+5.1f} dB from its limit", file=sys.stderr)
        keys.append(ks)
    print("KEYS = " + repr(keys))


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "--choose":
        choose(float(sys.argv[2]) if len(sys.argv) > 2 else KEY_DB)
    elif len(sys.argv) == 2:
        if KEYS is None:
            raise SystemExit("gen_void_tables: no KEYS (run --choose)")
        header(sys.argv[1])
    else:
        raise SystemExit(__doc__)
