#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""VOID FOUR's factory presets: presets/four/{obx,wave,acid,drift}.py -> build/gen/four_presets.h.

  tools/four_presets.py [OUT.h] [VOID_TABLES.h]    (default build/gen/four_presets.h, build/gen/void_tables.h)

Each source holds PRESETS, a list of dicts (see presets/four/obx.py for the fields; wave.py adds the void_px_t
extras ftype voice detune spread glide lfo[5] fe[5] mx[4][3]). The generator enforces the contract and exits non-zero
on any violation, so a build with a bad preset set fails: exactly 50 presets per machine (V2; 0..31 the shipped ones), the categories and their
counts of the machine (CATS), unique names of at most 12 characters in the large font's glyphs (GLYPHS), every e[i]
inside the engine's edit[i] range (RANGES: a copy of the engines' tables; WAVE's from void_tables.h).
Writes <M>_PRESETS[50], <M>_CAT[50] (CAT_*, category.c) for OBX WAVE ACID DRIFT JPZ CZZ GWZ MICRO, WAVE_PX[50], DRIFT_PX[50] (DRIFT's chor) and NANO_PX[50] (model, ftype).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "presets" / "four"
N = 50                                                           # V2: 50 per machine (0..31 as in 0.2-beta, 32..49 new)
N_OLD = 32                                                       # the presets 0.2-beta shipped: their numbers never move
MACHINES = ["OBX", "WAVE", "ACID", "DRIFT", "JPZ", "CZZ", "GWZ", "MICRO"]   # engine numbers 0..7 (core.h ENGI_*)
CAT_ORDER = ["BASS", "LEAD", "KEYS", "BRASS", "STRINGS", "ATMOS", "ACID"]     # category.c: CAT_NONE = 0, then these
CATS = {                                                         # per machine: category -> count (in this order)
    "OBX": {"BRASS": 8, "STRINGS": 8, "KEYS": 8, "LEAD": 8},
    "WAVE": {"ATMOS": 8, "LEAD": 10, "BASS": 14},
    "ACID": {"ACID": 2, "BASS": 12, "LEAD": 5, "KEYS": 7, "ATMOS": 6},      # NANO (2026-10-21): the 303's 2, then the SUB model's 30 (fm1_subsynth_presets.h)
    "DRIFT": {"BASS": 12, "LEAD": 10, "KEYS": 10},
    # the V2 machines (2026-11-17-eight-machines.md, section 8): all 50 count
    "JPZ": {"LEAD": 16, "ATMOS": 14, "KEYS": 12, "BASS": 8},
    "CZZ": {"KEYS": 14, "BASS": 12, "LEAD": 12, "ATMOS": 12},
    "GWZ": {"KEYS": 22, "ATMOS": 12, "LEAD": 10, "BASS": 6},
    "MICRO": {"ATMOS": 18, "KEYS": 12, "LEAD": 10, "BASS": 10},
}
# the four 0.2-beta machines: CATS counts presets 0..31; 32..49 are placeholders until Phase 5 (placeholder() below)
GLYPHS = set(" #%+-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ")      # the AF_L face (gen_aa_font.py L_CHARS)
PCT = (0, 127)
RANGES = {                                                       # edit[0..7] min..max (eng_obx.c, eng_acid.c, eng_drift.c)
    "OBX": [(0, 6)] + [PCT] * 7,
    "ACID": [(0, 1), (-64, 63)] + [PCT] * 6,
    "DRIFT": [PCT] * 8,
    "JPZ": [PCT, PCT, (-64, 63), (-24, 24), PCT, PCT, PCT, PCT],      # eng_stub.c ENG_JPZ (DTN MIX BAL OSC2 CUT RES KTR WID)
    "CZZ": [(0, 7), (0, 7), (0, 3), PCT, PCT, PCT, (0, 2), PCT],   # eng_cz.c ENG_CZZ (WAV1 WAV2 LINE DTN CUT KEYF MOD DCWE)
    "GWZ": [PCT] * 8,                                              # eng_gwz.c ENG_GWZ: every slot 0..127 on its three models
    "MICRO": [(0, 31), PCT, (-64, 63)] + [PCT] * 5,                 # ENG_MICRO (WT POS EPOS OSC2 CUT RES GRIT KTR)
}
WAVE_PX = dict(ftype=(0, 6), voice=(0, 3), detune=(0, 128), spread=PCT, glide=PCT, ldflt=(-64, 63), lsync=(0, 10))
NANO_MODEL = (0, 1)                                              # NANO's model: 0 the 303, 1 SUB (eng_sub.c); the ACID category is the 303's
NANO_FTYPE = (0, 3)                                              # SUB's filter TYPE (P_FTYPE): 0 LP24 1 BP 2 HP 3 LP12
NANO_KEYS = ["model", "ftype", "mono1", "glide", "fa", "fd", "fs", "fr", "ktrk", "o1", "w2", "semi", "pwm", "vel", "lrate", "lpit", "lcut", "vol"]   # eng_sub.c NPX_*
# CZ-Z (eng_cz.c cz_track_loaded, V2 Phase 1): dcw = the 8-step DCW envelope, dict(r=[8 rates 0..99], l=[8 levels 0..99],
# sus=step 0..7 or None, end=step 0..7), absent = CZ_DCW_DEF; oct2 = line 2's octave -2..2 (absent 0)
CZ_DCW_DEF = dict(r=[92, 55, 45, 0, 0, 0, 0, 0], l=[99, 60, 0, 0, 0, 0, 0, 0], sus=1, end=2)


def cz_px(p):
    d = p.get("dcw", CZ_DCW_DEF)
    sus = d.get("sus")
    return list(d["r"]) + list(d["l"]) + [8 if sus is None else sus, d["end"], p.get("oct2", 0) + 2]


GW_MODELS = {"ORGAN": 0, "VOX": 1, "ADD": 2}                      # GW-Z (eng_gwz.c gw_track_loaded): model, then ORGAN's click
DRIFT_CHOR = (0, 3)                                              # DRIFT's chor: the Juno chorus, 0 OFF, 1 I, 2 II, 3 I+II (absent = 0)


def placeholder(src, first):
    """V2 Phase 0.4: the presets after a 0.2-beta machine's 32, until Phase 5 voices them: copies of src (with every
    extra a machine keeps), named SLOT <n> (n = first + 1 ..), flagged ph; they keep their source's category but are left
    out of the category counts and of WAVE's distinctness rule"""
    out = []
    for i, p in enumerate(src):
        q = dict(p, name=f"SLOT {first + i + 1}", ph=True)
        q.pop("pat", None)
        out.append(q)
    return out


def load(m):
    """PRESETS of presets/four/<m>.py, read as source (no .pyc: an edit within the same second and size is seen)"""
    path = SRC / f"{m.lower()}.py"
    ns = {"placeholder": placeholder}
    exec(compile(path.read_text(), str(path), "exec"), ns)
    return ns["PRESETS"]


def wave_ranges(tables):
    t = Path(tables).read_text()
    ntbl = int(re.search(r"#define VOID_NTBL (\d+)", t).group(1))
    hid = (0, 0)                                                 # MIX (E3), OSC2 (E6): hidden since Task 4.4 (eng_wave.c), range 0..0
    warp = (9, 127)                                              # WARP (E2, was TBL2): 64 = off; 0..8 are the old TBL2 values (eng_wave.c VOID_WARP_MIN)
    return [(0, ntbl - 1), PCT, warp, hid, PCT, PCT, hid, PCT]


WAVE_TABLES = ["SINE", "SAW", "SQUARE", "SPECT", "FORMA", "FORME", "BASS1", "BASS2", "NOISE"]   # eng_wave.c N_VOID_TBL
WAVE_TYPES = ["LP", "BP", "HP", "LP12", "HP24", "NOTCH", "VOWEL"]                                # params.c N_FTYPE
MX_SRC = dict(LFO=1, ENV=2, VEL=3, KEY=4, RAND=5, MODW=6, AT=7, EXPR=8, SH=9, SLEW=10)           # mod.c MS_*
MX_DST = dict(PITCH=1, CUT=2, POS=(3, 13), AMP=4, VIB=11, TBL=12, WARP=14)                       # mod.c MD_* (POS: SHP or E2)


def route_holds(p, term):
    """does the preset really have the routing a signature term names? UNISON, GLIDE, FENV->CUT (the filter envelope),
    ENV->CUT also by the ENV -> FLT amount, or SRC->DST in a matrix slot"""
    if term == "UNISON":
        return p["voice"] == 3
    if term == "GLIDE":
        return p["glide"] > 0 and p["mono"] == 1
    if "->" not in term:
        return False
    src, dst = term.split("->")
    if src == "FENV":
        return dst == "CUT" and p["fe"][0] > 0 and p["fenv"] != 0
    if src == "ENV" and dst == "CUT" and p["fe"][0] == 0 and p["fenv"] != 0:
        return True
    if src == "LFO" and dst == "CUT" and p.get("ldflt", 0) != 0:    # the LFO's own depth on the filter (LFO DEST FLT)
        return True
    if src not in MX_SRC or dst not in MX_DST:
        return False
    ds = MX_DST[dst] if isinstance(MX_DST[dst], tuple) else (MX_DST[dst],)
    return any(r[0] == MX_SRC[src] and r[1] in ds and r[2] != 0 for r in p["mx"])


def check_distinct(errs, pr):
    """WAVE's distinctness rule: every preset declares a signature route (sig), which must hold in its routing, and no
    two presets share (table, TYPE, sig)"""
    seen = {}
    for p in pr:
        who = f"WAVE {p.get('name')!r}"
        sig = p.get("sig")
        if not isinstance(sig, str) or not sig:
            errs.append(f"{who}: no signature route (sig)")
            continue
        src = ""
        for term in sig.split("+"):
            if "->" in term:
                src = term.split("->")[0]
            elif term not in ("UNISON", "GLIDE"):
                term = f"{src}->{term}"               # "VEL->CUT+WARP": the second destination of the same source
            if not route_holds(p, term):
                errs.append(f"{who}: signature term {term!r} is not in the preset's routing")
        e0, ft = p["e"][0], p["ftype"]
        if not 0 <= e0 < len(WAVE_TABLES) or not 0 <= ft < len(WAVE_TYPES):
            continue
        key = (WAVE_TABLES[e0], WAVE_TYPES[ft], sig)
        if key in seen:
            errs.append(f"WAVE: {p['name']!r} and {seen[key]!r} share the signature {key}")
        seen[key] = p["name"]


def check_range(errs, who, what, v, lo, hi):
    if not isinstance(v, int) or isinstance(v, bool) or not lo <= v <= hi:
        errs.append(f"{who}: {what} = {v!r} outside {lo}..{hi}")


def validate(m, pr, rng, errs):
    allowed = CATS[m]
    if len(pr) != N:
        errs.append(f"{m}: {len(pr)} presets, need {N}")
    count = {}
    names = set()
    if m in ("OBX", "WAVE", "ACID", "DRIFT") and (any(p.get("ph") for p in pr[:N_OLD]) or
                                                   not all(p.get("ph") for p in pr[N_OLD:])):
        errs.append(f"{m}: presets 0..{N_OLD - 1} must be the shipped ones and {N_OLD}..{N - 1} placeholders "
                    "(Phase 5 replaces them: then drop this check)")
    for k, p in enumerate(pr):
        who = f"{m}[{k}] {p.get('name')!r}"
        cat = p.get("cat")
        if cat not in allowed:
            errs.append(f"{who}: category {cat!r} not allowed for {m} ({', '.join(allowed)})")
        if not p.get("ph"):
            count[cat] = count.get(cat, 0) + 1
        nm = p.get("name", "")
        if not nm or len(nm) > 12:
            errs.append(f"{who}: name must be 1..12 characters")
        bad = sorted(set(nm) - GLYPHS)
        if bad:
            errs.append(f"{who}: characters {''.join(bad)!r} not in the large font")
        if nm in names:
            errs.append(f"{who}: duplicate name")
        names.add(nm)
        e = p.get("e", [])
        if len(e) != 8:
            errs.append(f"{who}: e needs 8 values")
        for i, v in enumerate(e[:8]):
            check_range(errs, who, f"e[{i}]", v, *rng[i])
        env = p.get("env", [])
        if len(env) != 4:
            errs.append(f"{who}: env needs 4 values")
        for i, v in enumerate(env[:4]):
            check_range(errs, who, f"env[{i}]", v, 0, 127)
        check_range(errs, who, "fenv", p.get("fenv"), -64, 63)
        check_range(errs, who, "mono", p.get("mono"), 0, 1)
        check_range(errs, who, "pat", p.get("pat", 0), 0, 9)
        fx = p.get("fx", ())
        if len(fx) != 4:
            errs.append(f"{who}: fx needs 4 sends")
        for i, v in enumerate(fx[:4]):
            check_range(errs, who, f"fx[{i}]", v, 0, 126)
        if m == "DRIFT":
            check_range(errs, who, "chor", p.get("chor", 0), *DRIFT_CHOR)
        if m == "GWZ":
            if p.get("model", "ORGAN") not in GW_MODELS:
                errs.append(f"{who}: model is one of {', '.join(GW_MODELS)}")
            check_range(errs, who, "click", p.get("click", 0), 0, 127)
        if m == "CZZ":
            d = p.get("dcw", CZ_DCW_DEF)
            if len(d.get("r", ())) != 8 or len(d.get("l", ())) != 8:
                errs.append(f"{who}: dcw needs 8 rates and 8 levels")
            for i, v in enumerate(list(d.get("r", ()))[:8] + list(d.get("l", ()))[:8]):
                check_range(errs, who, f"dcw[{i}]", v, 0, 99)
            check_range(errs, who, "dcw end", d.get("end"), 0, 7)
            if d.get("sus") is not None:
                check_range(errs, who, "dcw sus", d.get("sus"), 0, 7)
                if d["sus"] > d.get("end", 0):
                    errs.append(f"{who}: dcw sus after end")
            check_range(errs, who, "oct2", p.get("oct2", 0), -2, 2)
        if m == "ACID":
            check_range(errs, who, "model", p.get("model", 0), *NANO_MODEL)
            check_range(errs, who, "ftype", p.get("ftype", 0), *NANO_FTYPE)
            for k in NANO_KEYS[2:]:
                check_range(errs, who, k, p.get(k, 0), 0, 1 if k == "mono1" else 127)
            if (p.get("model", 0) == 0) != (cat == "ACID"):
                errs.append(f"{who}: the ACID category is the 303's (model 0) and only it")
        if m == "WAVE":
            for key, (lo, hi) in WAVE_PX.items():
                check_range(errs, who, key, p.get(key), lo, hi)
            for key, n_ in (("lfo", 5), ("fe", 5)):
                v = p.get(key, [])
                if len(v) != n_:
                    errs.append(f"{who}: {key} needs {n_} values")
                for i, x in enumerate(v[:n_]):
                    check_range(errs, who, f"{key}[{i}]", x, 0, 127)
            mx = p.get("mx", [])
            if len(mx) != 4 or any(len(r) != 3 for r in mx):
                errs.append(f"{who}: mx needs 4 x 3 values")
            else:
                for j, r in enumerate(mx):
                    for i, x in enumerate(r):
                        check_range(errs, who, f"mx[{j}][{i}]", x, -64 if i == 2 else 0, 63 if i == 2 else 127)
    if m == "WAVE":
        check_distinct(errs, [p for p in pr if not p.get("ph")])
    for cat, want in allowed.items():
        if count.get(cat, 0) != want:
            errs.append(f"{m}: {count.get(cat, 0)} {cat} presets, need {want}")


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "gen" / "four_presets.h"
    tables = sys.argv[2] if len(sys.argv) > 2 else ROOT / "build" / "gen" / "void_tables.h"
    rng = dict(RANGES, WAVE=wave_ranges(tables))
    errs, sets = [], {}
    for m in MACHINES:
        sets[m] = load(m)
        validate(m, sets[m], rng[m], errs)
    if errs:
        sys.stderr.write("four_presets: " + str(len(errs)) + " problem(s)\n  " + "\n  ".join(errs) + "\n")
        return 1
    H = ["/* generated by tools/four_presets.py from presets/four/<m>.py: do not edit.",
         " * <M>_PRESETS: name, {E0..E7}, {ATK DEC SUS REL}, ENV -> FLT, mono, the sends, the pattern;",
         " * <M>_CAT: the category of each (category.c CAT_*); WAVE_PX: what a preset_t cannot hold (core.h void_px_t);",
         " * DRIFT_PX: DRIFT's chorus mode of each (P_RSV_LN, 0 OFF 1 I 2 II 3 I+II; eng_drift.c drift_track_loaded);",
         " * NANO_PX: NANO's model and SUB filter TYPE of each (eng_acid.c acid_track_loaded) */"]
    for m in MACHINES:
        H.append(f"static const preset_t {m}_PRESETS[] = {{")
        for p in sets[m]:
            pat = f", PAT({p['pat']})" if p.get("pat") else ""
            H.append(f'    {{"{p["name"]}", {{{", ".join(map(str, p["e"]))}}}, {{{", ".join(map(str, p["env"]))}}}, '
                     f'{p["fenv"]}, {p["mono"]}, FX({", ".join(map(str, p["fx"]))}){pat}}},')
        H += ["};", f"static const uint8_t {m}_CAT[] = {{"]
        H += [f"    CAT_{p['cat']}," for p in sets[m]]
        H.append("};")
    H += [f"static const void_px_t WAVE_PX[] = {{",
          "    /* ftype, voice (3: UNISON), detune + 1, spread, glide, lfo {set, RATE, WAVE, TRIG, FADE}, fe {LVL, A, D, S, R}, "
          "mx {SRC, DST, AMT} x 4, LFO DEST FLT, LFO SYNC */"]
    for p in sets["WAVE"]:
        mx = ", ".join("{" + ", ".join(map(str, r)) + "}" for r in p["mx"])
        H.append(f'    {{{p["ftype"]}, {p["voice"]}, {p["detune"]}, {p["spread"]}, {p["glide"]}, '
                 f'{{{", ".join(map(str, p["lfo"]))}}}, {{{", ".join(map(str, p["fe"]))}}}, {{{mx}}}, {p["ldflt"]}, {p["lsync"]}}},   /* {p["name"]} */')
    H.append("};")
    H += ["static const uint8_t DRIFT_PX[] = {"]
    H += [f'    {p.get("chor", 0)},   /* {p["name"]} */' for p in sets["DRIFT"]]
    H.append("};")
    H += [f"static const uint8_t NANO_PX[][{len(NANO_KEYS)}] = {{   /* NANO (eng_acid.c acid_track_loaded; eng_sub.c NPX_*): model (0 the 303, 1 SUB), then SUB's",
          "    * TYPE, mono, glide, filter ADSR, key track, osc 1 level, osc 2 wave, semitones, PWM, velocity, LFO rate / pitch / cutoff, volume */"]
    H += [f'    {{{", ".join(str(p.get(k, 0)) for k in NANO_KEYS)}}},   /* {p["name"]} */' for p in sets["ACID"]]
    H.append("};")
    H += ["static const uint8_t GW_PX[][2] = {   /* GW-Z (eng_gwz.c gw_track_loaded): model (0 ORGAN 1 VOX 2 ADD), ORGAN's click */"]
    H += [f'    {{{GW_MODELS[p.get("model", "ORGAN")]}, {p.get("click", 0)}}},   /* {p["name"]} */' for p in sets["GWZ"]]
    H.append("};")
    H += ["static const uint8_t CZ_PX[][19] = {   /* CZ-Z (eng_cz.c cz_track_loaded): the 8-step DCW envelope: 8 rates, 8 levels,",
          "    * SUS step (8 none), END step; then line 2's octave + 2 */"]
    H += [f'    {{{", ".join(str(v) for v in cz_px(p))}}},   /* {p["name"]} */' for p in sets["CZZ"]]
    H.append("};")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(H) + "\n")
    print(f"four_presets: {N * len(MACHINES)} presets -> {out.relative_to(ROOT) if out.is_relative_to(ROOT) else out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
