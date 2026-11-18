#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Target-side cost estimate of the hot DSP code, from the pi32v2 objdump listing tools/build.py writes
(build/felucca.dis). For each engine's render function and the audio ISR: the instructions
inside loops (every address range closed by a backward branch: the per-sample loops and what they
contain), and the hardware divides and calls in them. cost = the loop instructions, each weighted 4 per
level of nesting (an inner loop runs several times per sample) and 1 + 8 for a divide (many cycles). A static count, not a cycle count: it changes only when the compiled code changes,
so it is exact run to run, and it catches a render loop that grew (more work per sample, a new divide,
something not inlined any more). Compared with tests/target_budget.txt at +10 %. The host
CPU check (regress.c) is the primary one; this one sees the target compiler's code.
  tests/target_budget.py [DIS [BUDGET]]      BUDGET_UPDATE=1 rewrites BUDGET"""
import os
import re
import sys

FUNCS = ["obx_render", "obx_render_f", "obx_render_p4", "obx_render_x",   # OB-Z (eng_obx.c): MODE 0's low-pass
                                                    # loop, the MODE morph's, the 4-pole's, WAVE SYNC / SUB's
         "acid_render", "bass303_render",            # ACID (eng_acid.c, bass303.c): the voice wrapper, and the 303's per-sample loop
         "sub_render",                              # NANO's SUB model (eng_sub.c, 2026-10-21): the module's voice
         "drift_render",                            # DRIFT (eng_drift.c): the Juno voice (its ladder is dsp_va.c's, inlined; FPU opcodes are
                                                    # invisible to this count, so it guards the structure -- the loops and the divides -- only)
         "void_render", "void_block_slow",            # WAVE (eng_wave.c): the four specialised fast loops (LP24 / SVF x DRV), and the one
                                                    # out-of-line copy for a note's first 5 ms and a closing cutoff (Task 4.4)
         "micro_render",                            # MICRO (eng_micro.c, V2 Phase 4): two stepped 8-bit wavetable oscillators, GRIT, two SVFs
         "gw_render", "gw_block",                   # GW-Z (eng_gwz.c, V2 Phase 2): ORGAN / VOX / ADD (inlined), the per-part block
         "jpz_render",                              # JP-Z (eng_jpz.c, V2 Phase 3): 7 naive saws, OSC2, the HPF, two SVFs
         "cz_render", "cz_wave",                    # CZ-Z (eng_cz.c, V2 Phase 1): the two lines, and the uPD933 sample (cz_core.h: out of line, its divides)
         "slicer_track",
         "fm1_alnk0_irq", "fm1_timer5_irq",               # the audio ISR; TIMER5: the key / LED scan (hal/fm1_input.h)
         "uac_tap48",                                       # the USB audio input at 48 kHz: the 44.1 -> 48 resampler (usb.c)
         "click_render",                                    # the metronome's click (click.c), while it sounds
         "mod_begin", "mod_voice", "mod_end",                 # the modulation matrix (mod.c), called when active
         "perf_begin", "perf_mute", "perf_pre", "perf_block", "perf_master",   # the FX layer (perform.c), when busy
         "perf_flanger", "perf_phaser",                     # .. FLANGER, PHASER (1.2), while held or fading
         "perf_throw",                                      # .. REV / ECHO THROW (1.5.1): per part, while held or gliding
         "rev_room", "rev_spring", "rev_hall", "rev_side_mix",   # the reverb bus (fx.c): REVERB TYPE ROOM / SPRING / HALL (and its stereo)
         "track_insert", "ins_drive", "ins_swept",          # the per-track INSERT (fx.c, 1.5), while a TYPE plays
         "track_dist"]                                      # the per-track DIST (fx.c, FX.1), while P_DIST is up
OPTIONAL = {"uac_tap48": 504}  # (absent from a FELUCCA_UAC_48K=0 or FELUCCA_UAC=0 build)
TOL = 0.10                      # exact (no noise): small edits pass, a grown render loop does not
DIV_W = 8                       # a divide weighs 1 + 8 instructions
NEST = 4                        # an instruction in a loop inside a loop weighs 4, two deep 16, ...
MAXD = 4

dis = sys.argv[1] if len(sys.argv) > 1 else "build/felucca.dis"
budget = sys.argv[2] if len(sys.argv) > 2 else "tests/target_budget.txt"
LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9.]*):$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
TARGET = re.compile(r"goto -?\d+ <[^>]*: ([0-9a-f]+) >")


def functions(path):
    """{name: [(addr, text)]}: a function runs to the next label not starting with '.' (the compiler's
    jump-table labels are inside functions); jump-table data ('< n : 0x.. >') is left out"""
    out, cur = {}, None
    with open(path) as f:
        for line in f:
            m = LABEL.match(line.strip())
            if m:
                name = m.group(1)
                if not name.startswith("."):
                    cur = out.setdefault(name, []) if name in FUNCS else None
                continue
            m = INSN.match(line)
            if cur is not None and m and not m.group(3).lstrip().startswith("<"):
                cur.append((int(m.group(1), 16), m.group(3).strip()))
    return out


def cost(insns):
    """loops: one span per loop head (the farthest backward branch to it); an instruction inside d
    spans weighs NEST ** (d - 1) (an inner loop runs several times per pass of the outer one)"""
    lo = insns[0][0]
    heads = {}
    for a, t in insns:
        m = TARGET.search(t)
        if m and lo <= int(m.group(1), 16) < a:
            h = int(m.group(1), 16)
            heads[h] = max(heads.get(h, a), a)
    n = divs = calls = 0
    w = 0
    for a, t in insns:
        d = min(sum(1 for h, e in heads.items() if h <= a <= e), MAXD)
        if not d:
            continue
        k = NEST ** (d - 1)
        n += 1
        if re.search(r"= r\d+ / r\d+", t):
            divs += 1
            k *= 1 + DIV_W
        calls += t.startswith("call")
        w += k
    return {"insns": len(insns), "loop": n, "div": divs, "call": calls, "cost": w}


def main():
    if not os.path.exists(dis):
        print(f"target: skip ({dis} missing: run ./build.sh)")
        return 0
    fns = functions(dis)
    res = {n: cost(fns[n]) for n in FUNCS if fns.get(n)}
    missing = [n for n in FUNCS if n not in res and n not in OPTIONAL]
    base = {}
    if os.path.exists(budget):
        for line in open(budget):
            p = line.split()
            if len(p) >= 2 and not line.startswith("#"):
                base[p[0]] = int(p[1])
    if os.environ.get("BUDGET_UPDATE"):
        with open(budget, "w") as f:
            f.write("# FELUCCA target cost budget (tests/target_budget.py): instructions in the loops of each\n"
                    f"# function in build/felucca.dis, x{NEST} per nesting level, divides x{1 + DIV_W}. The check allows "
                    f"+{TOL * 100:.0f} %.\n# Rewritten by BUDGET_UPDATE=1.\n")
            for n, r in res.items():
                if n not in OPTIONAL:
                    f.write(f"{n} {r['cost']}\n")
        print(f"target: budget {budget} rewritten ({len(res)} functions)")
    fail = 0
    for n in missing:
        print(f"target: FAIL {n} not found in {dis} (renamed? inlined? update FUNCS)")
        fail += 1
    for n, r in res.items():
        b = base.get(n, OPTIONAL.get(n))
        state = "no budget (BUDGET_UPDATE=1 adds it)" if b is None else "ok"
        if b is not None and not os.environ.get("BUDGET_UPDATE"):
            if r["cost"] > b * (1 + TOL):
                state = f"OVER BUDGET (+{(r['cost'] / b - 1) * 100:.0f} %, limit +{TOL * 100:.0f} %)"
                fail += 1
            elif r["cost"] < b * (1 - TOL):
                state = f"note: {(r['cost'] / b - 1) * 100:.0f} % (BUDGET_UPDATE=1 to keep it)"
        print(f"target: {n:15s} {r['insns']:5d} instructions, {r['loop']:4d} in loops, {r['div']} divides, "
              f"{r['call']:2d} calls there: cost {r['cost']:5d} (budget {b if b is not None else '-'}) {state}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
