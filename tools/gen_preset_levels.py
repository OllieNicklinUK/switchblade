#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""V2 (Phase 0.7): the measured level of every factory preset, and how far each sits from the library's.

After Quantum GammaWave's tools/gen_preset_levels.py (GPL-3.0-only). The measurement is the preset audit's: one held
C4 at LEVEL 100 through the whole mix, from power-on state, the level the loudest ~93 ms of the note
(tests/preset_audit_test.c --levels). This reads it and reports, per machine, the spread and every preset more than
TOL dB from the target (the median of the voiced presets of every machine), with the correction it needs.

    sh tests/run_tests.sh                       # (builds build/host/preset_audit_test)
    build/host/preset_audit_test --levels > build/preset_levels.csv
    python3 tools/gen_preset_levels.py build/preset_levels.csv [--md docs/PRESET_LEVELS.md]

It writes no firmware. On Switch Blade LEVEL is the part's own fader (param_kept: a preset load keeps it), so the
correction is not a LEVEL a preset loads with, as QG's preset_level.c is. Phase 5 (the presets) decides where it goes:
in the voicing of each preset (presets/four/<machine>.py), or a per-preset gain column. The placeholders of Phase 0
(SLOT nn, the INIT presets of the four new machines) are listed apart and left out of the target.
"""
import csv
import re
import statistics
import sys

TOL = 3.0                                   # dB either side of the target: left alone
PLACEHOLDER = re.compile(r"^(SLOT \d+|.* INIT \d+)$")


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    md = argv[argv.index("--md") + 1] if "--md" in argv else None
    if md in args:
        args.remove(md)
    if not args:
        print(__doc__)
        return 2
    rows = []
    with open(args[0], encoding="utf-8") as f:
        for r in csv.reader(f):
            if len(r) < 6 or not r[0].isdigit():
                continue
            rows.append(dict(e=int(r[0]), i=int(r[1]), machine=r[2], name=r[3], db=float(r[4]), peak=int(r[5]),
                             ph=bool(PLACEHOLDER.match(r[3]))))
    if not rows:
        print("gen_preset_levels: no rows (run build/host/preset_audit_test --levels)")
        return 2
    voiced = [r for r in rows if not r["ph"]]
    target = statistics.median(r["db"] for r in voiced) if voiced else statistics.median(r["db"] for r in rows)
    out = [f"# Preset levels (measured)", "",
           f"Target: **{target:+.1f} dBFS**, the median of the {len(voiced)} voiced presets (one held C4 at LEVEL 100, "
           f"the loudest ~93 ms; `tests/preset_audit_test.c --levels`). Presets within ±{TOL:.0f} dB are left alone.", "",
           "| Machine | Voiced | Spread (dB) | Min | Median | Max | Off by more than 3 dB | Placeholders |",
           "|---|---|---|---|---|---|---|---|"]
    detail = []
    for e in sorted({r["e"] for r in rows}):
        m = [r for r in rows if r["e"] == e]
        v = [r for r in m if not r["ph"]]
        name = m[0]["machine"]
        if v:
            dbs = [r["db"] for r in v]
            off = [r for r in v if abs(r["db"] - target) > TOL]
            out.append(f"| {name} | {len(v)} | {max(dbs) - min(dbs):.1f} | {min(dbs):+.1f} | {statistics.median(dbs):+.1f} | "
                       f"{max(dbs):+.1f} | {len(off)} | {len(m) - len(v)} |")
            for r in sorted(off, key=lambda r: r["db"]):
                detail.append(f"| {name} | {r['i']} | {r['name']} | {r['db']:+.1f} | {target - r['db']:+.1f} |")
        else:
            out.append(f"| {name} | 0 | | | | | | {len(m)} |")
    if detail:
        out += ["", "## Corrections", "", "| Machine | # | Preset | Level (dBFS) | Needs (dB) |", "|---|---|---|---|---|"] + detail
    text = "\n".join(out) + "\n"
    print(text)
    if md:
        open(md, "w", encoding="utf-8").write(text)
        print(f"gen_preset_levels: wrote {md}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
