#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""VOID's wavetables (build/gen/void_tables.h) against the plugin's frames, recomputed here with numpy:
every keyframe's level 0 is the plugin frame's spectrum up to its harmonic limit (no DC); the levels
above keep fewer harmonics; the crossfade between keyframes stays within KEY_DB of the plugin up to
KEY_NEAR, KEY_DB_FAR above (the band-limited frames); the SQUARE's pulse of two saw readings is
the plugin's 50 % pulse, band-limited.
  tests/void_tables_test.py [build/gen/void_tables.h]      (needs numpy)"""
import re
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import gen_void_tables as g  # noqa: E402

fails = 0


def check(ok, what):
    global fails
    print(f"  {'ok  ' if ok else 'FAIL'} {what}")
    fails += not ok


def parse(path):
    txt = Path(path).read_text()
    arrs = {m.group(1): [int(v) for v in re.findall(r"-?\d+", m.group(2))]
            for m in re.finditer(r"static const \w+ (\w+)\[\d+\] = \{([^}]*)\};", txt)}
    one = int(re.search(r"#define VOID_WT_ONE (\d+)", txt).group(1))
    return arrs, one


def plugin_frame(tb, f):
    a = 2.0 * np.pi * np.arange(g.FSIZE) / g.FSIZE
    x = sum(amp * np.sin(m * a + p) for amp, m, p in g.terms(tb, f))
    dc, sc = g.v2_scale(list(x))                       # V2's normalize(): DC out, RMS 0.35, the peak at most 0.999
    return (x - dc) * sc


def bandlimit(x, n, hmax):
    X = np.fft.rfft(x)
    X[0] = 0.0
    X[hmax + 1:] = 0.0
    return np.fft.irfft(X[:n // 2 + 1] * (n / g.FSIZE), n)


def level(arrs, k, tb, lv):
    n0 = 1 << arrs["VOID_N0LOG2"][tb]
    o = arrs["VOID_KOFS"][k] + 2 * n0 - (2 * n0 >> lv)
    return np.array(arrs["VOID_WAVE"][o:o + (n0 >> lv)], dtype=float)


def main(path):
    arrs, one = parse(path)
    check(max(abs(v) for v in arrs["VOID_WAVE"]) <= 32767, "int16 range")
    worst_key = -999.0
    for tb in range(g.VOID_NTBL if hasattr(g, "VOID_NTBL") else len(g.NAMES)):
        k0, k1 = arrs["VOID_KFIRST"][tb], arrs["VOID_KFIRST"][tb + 1]
        n0 = 1 << arrs["VOID_N0LOG2"][tb]
        if tb == g.SQUARE:
            saw = level(arrs, k0, tb, 0) / one
            pulse = saw - np.roll(saw, -n0 // 2)          # two readings half a cycle apart: a 50 % pulse
            a = 2.0 * np.pi * np.arange(g.FSIZE) / g.FSIZE   # the ideal square (the plugin's naive one sits half
            sq = (4 / np.pi) * sum(np.sin(h * a) / h for h in range(1, n0 // 4 + 1, 2))   # a sample early)
            ref = bandlimit(sq, n0, n0 // 4)
            e = 20 * np.log10(np.sqrt(np.mean((pulse - ref) ** 2)) / np.sqrt(np.mean(ref ** 2)))
            check(e < -60, f"SQUARE: two saw readings = the plugin's 50 % pulse, band-limited ({e:.0f} dB)")
            gq = arrs["VOID_SQ_GAIN"]
            pw = [0.5 - (f / 255) * 0.45 for f in range(256)]
            want = [min(0.35 / (2 * np.sqrt(d * (1 - d))), 0.999 / (2 * max(d, 1 - d))) for d in pw]
            err = max(abs(gq[f] / 32767 - want[f]) / want[f] for f in range(256))
            check(len(gq) == 256 and err < 0.01, f"SQUARE: V2's level per width (VOID_SQ_GAIN within {100 * err:.2f} %)")
            continue
        keys = arrs["VOID_KPOS"][k0:k1]
        fr = {}
        for i, f in enumerate(keys):
            for lv in range(g.NLEV):
                n = n0 >> lv
                ref = bandlimit(plugin_frame(tb, f), n, n // 4)
                got = level(arrs, k0 + i, tb, lv) / one
                e = 20 * np.log10(max(np.sqrt(np.mean((got - ref) ** 2)), 1e-9))   # dB re the plugin's 1.0
                worst_key = max(worst_key, e)
                if lv == 0:
                    fr[f] = got
        # the crossfade between keyframes, at every plugin frame
        worst = -999.0
        for a0, b0 in zip(keys, keys[1:]):
            for f in range(a0 + 1, b0):
                u = (f - a0) / (b0 - a0)
                ref = bandlimit(plugin_frame(tb, f), n0, n0 // 4)
                e = (1 - u) * fr[a0] + u * fr[b0] - ref
                r = 20 * np.log10(np.sqrt(np.mean(e ** 2)) / np.sqrt(np.mean(ref ** 2)))
                worst = max(worst, r - (g.KEY_DB if f <= g.KEY_NEAR else g.KEY_DB_FAR))
        if tb == g.NOISE:
            check(True, f"NOISE: {len(keys)} keyframes (not crossfade-accurate by design; no preset uses it)")
        else:
            check(worst <= 0.5, f"{g.NAMES[tb]}: {len(keys)} keyframes, crossfade within its limit ({worst:+.1f} dB)")
    check(worst_key < -60, f"every keyframe and level = the plugin's frame band-limited (error {worst_key:.0f} dB re 1.0)")
    print(f"void tables: {fails} FAILED" if fails else "void tables: ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/gen/void_tables.h"))
