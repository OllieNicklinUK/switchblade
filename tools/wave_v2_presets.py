#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""WAVE's factory presets from the VOID WAVE V2 plugin's (D:/VOID SYNTH/VoidWave, git e280794, 2026-07-13).

  tools/wave_v2_presets.py [FACTORY_DIR]      (default ../../VOID SYNTH/VoidWave/Resources/Presets/factory)

Writes presets/four/wave.py (checked in: the build does not need the plugin; tools/four_presets.py turns it into
WAVE_PRESETS / WAVE_PX) and docs/void_presets.md, what each preset keeps and loses.

The 50 (PICK below) are the V2 factory presets that port to the single-oscillator WAVE engine with the least loss
(2026-11-19, the "lean" upgrade): OSC2 silent (OSC MIX 0, OSC2 level 0 in Blend, or a Morph to its own table) or a
detuned copy of OSC1 (the same table and octave, a few cents off: UNISON / SPREAD here), filters WAVE has (LP12 LP24
HP12 HP24 BP NOTCH; FORMANT as VOWEL), tables 0-8, no second filter, no LFO3 / ENV3 modulation, no oscillator
distortion or spectral modes. The mapping, from the V2 plugin's DSP (Source/DSP/VoiceManager.cpp, Filter.cpp):
  oscillator   TBL1, POS (x 127); OLVL = the oscillator's weight (osc1_level, times 1 - mix in Blend; the two copies'
               sum for a detuned pair) against the plugin's default 0.8 (64)
  PENV         pitchenv_amount, clamped to V2's own +-24 semitones (the plugin clamps a stored -36 / -48 the same way)
  SUB, NOISE   sub_level, noise_level (127 = the plugin's 1.0); sub_octave 1 (two down) -> P_FM2_DEC 127; noise_color
  filter       TYPE: LP24 LP, LP12, HP12 HP, HP24, BP, NOTCH; FORMANT -> VOWEL with CUT on V2's vowel sweep
               ((cutoff - 200) / 7800: U .. I), RES 0 (V2's formants are fixed at k 0.6); CUT: the inverse of
               20 + 19980 x^4; RES, DRV: x 127; filter_veltrack: VEL -> CUT with the base cutoff lowered to match
  envelopes    ENV2 (amp) -> A D S R (Felucca's times: 1 ms * 10000^(v / 127)); ENV1 -> FLT ENV (P_FM1_*, its
               velocity sensitivity in P_FM1_LEVEL), env1_depth -> ENV DEST FLT (63 = V2's 4 octaves, eng_wave.c)
  LFO          the plugin's LFO1 (or LFO2 if LFO1 is idle) is the track LFO: rate (free or its sync division as a
               SYNC note value), shape, trigger, fade; its own target: cutoff -> LFO DEST FLT (2 octaves a unit),
               WT position -> a matrix slot LFO -> POS (half the table a unit)
  matrix       V2's 12 slots -> 4 (the most audible first): sources ENV1/ENV2 -> ENV, LFO1/2 -> LFO, VEL, NOTE -> KEY,
               AT, MODW; destinations cutoff (4 octaves a unit), WT position, pitch (1 semitone a unit, coarse 24), RES,
               DRV, SUB (their edit slots), the LFO's depth (a mod wheel / aftertouch / velocity slot on it -> DEPTH: the
               LFO's gain follows that source, its routes at the full depth); filter_keytrack -> KEY -> CUT
  voice        voice_poly 1 -> MONO; unison (or the detuned copy) on a mono preset or a bass -> MONO UNISON with
               DETUNE, on another poly one SPREAD (lost: the detune); glide -> GLIDE
  FX           V2's distortion / chorus / delay / reverb mix as the sends; a preset with all four off gets WAVE's
               category sends (the plugin's presets are dry for a DAW's own effects)
  lost         macros (they sit at 0: silent in the plugin too), LFO2 where LFO1 is used, the matrix past 4 slots,
               envelope-time and LFO rate / depth destinations, per-stage envelope curves, V2's 4x oversampled drive
"""
import math
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_PY = ROOT / "presets" / "four" / "wave.py"
OUT_MD = ROOT / "docs" / "void_presets.md"
DEF_SRC = ROOT.parents[1] / "VOID SYNTH" / "VoidWave" / "Resources" / "Presets" / "factory"

# (file under the factory folder, WAVE category, FM-1 name, suggested pattern): ATMOS first (preset 0 is part 2's
# power-on sound and must be POLY), then LEAD, KEYS (plucks, stabs), BASS
PICK = [
    ("PAD/Supersaw_Warm.vwpreset", "ATMOS", "SUPRSAW WARM", 5),
    ("PAD/Supersaw_Rave.vwpreset", "ATMOS", "SUPRSAW RAVE", 5),
    ("PAD/Supersaw_Smooth.vwpreset", "ATMOS", "SUPRSAW SMTH", 5),
    ("PAD/Supersaw_Breath.vwpreset", "ATMOS", "SUPRSAW BRTH", 5),
    ("PAD/Supersaw_Dark.vwpreset", "ATMOS", "SUPRSAW DARK", 5),
    ("PAD/Formant_Choir.vwpreset", "ATMOS", "FORMNT CHOIR", 5),
    ("PAD/Formant_Distant.vwpreset", "ATMOS", "FORMNT DIST", 5),
    ("PAD/Formant_Drone.vwpreset", "ATMOS", "FORMNT DRONE", 5),
    ("PAD/Formant_Space.vwpreset", "ATMOS", "FORMNT SPACE", 5),
    ("PAD/Formant_Vowel.vwpreset", "ATMOS", "FORMNT VOWEL", 5),
    ("LEAD/Acid_Rubber.vwpreset", "LEAD", "ACID RUBBER", 1),
    ("LEAD/Acid_Screech.vwpreset", "LEAD", "ACID SCREECH", 1),
    ("LEAD/Mono_Lead.vwpreset", "LEAD", "MONO LEAD", 4),
    ("LEAD/Good_Vibes.vwpreset", "LEAD", "GOOD VIBES", 4),
    ("LEAD/Hollow_Swell.vwpreset", "LEAD", "HOLLOW SWELL", 4),
    ("LEAD/Juno_Chaser.vwpreset", "LEAD", "JUNO CHASER", 4),
    ("LEAD/Soothing_Sine.vwpreset", "LEAD", "SOOTHNG SINE", 4),
    ("LEAD/Theramin_Air.vwpreset", "LEAD", "THERAMIN AIR", 4),
    ("LEAD/Theramin_Thick.vwpreset", "LEAD", "THERAMIN THK", 4),
    ("LEAD/Underwater_Glow.vwpreset", "LEAD", "UNDERWTR GLW", 4),
    ("LEAD/Uzi_Nerves.vwpreset", "LEAD", "UZI NERVES", 4),
    ("LEAD/Zen_Flute.vwpreset", "LEAD", "ZEN FLUTE", 4),
    ("PLUCK/Deep_House_Key.vwpreset", "KEYS", "DEEP HSE KEY", 6),
    ("PLUCK/Full_On_Snap.vwpreset", "KEYS", "FULL ON SNAP", 3),
    ("PLUCK/Garage_Skip.vwpreset", "KEYS", "GARAGE SKIP", 6),
    ("PLUCK/House_Snap.vwpreset", "KEYS", "HOUSE SNAP", 6),
    ("PLUCK/Jack_Pluck.vwpreset", "KEYS", "JACK PLUCK", 3),
    ("PLUCK/Lo_Fi_Pluck.vwpreset", "KEYS", "LO FI PLUCK", 3),
    ("PLUCK/Prog_Pluck.vwpreset", "KEYS", "PROG PLUCK", 3),
    ("PLUCK/Psy_Dart.vwpreset", "KEYS", "PSY DART", 3),
    ("PLUCK/Psy_Rubber.vwpreset", "KEYS", "PSY RUBBER", 3),
    ("PLUCK/Trance_138.vwpreset", "KEYS", "TRANCE 138", 3),
    ("STAB/Acid_Stab.vwpreset", "KEYS", "ACID STAB", 6),
    ("STAB/Rave_Hoover_Stab.vwpreset", "KEYS", "RAVE HOOVER", 6),
    ("STAB/Reese_Stab.vwpreset", "KEYS", "REESE STAB", 6),
    ("STAB/Supersaw_Stab.vwpreset", "KEYS", "SUPRSAW STAB", 6),
    ("BASS/Acid_Deep.vwpreset", "BASS", "ACID DEEP", 1),
    ("BASS/Acid_House.vwpreset", "BASS", "ACID HOUSE", 1),
    ("BASS/Acid_Slo.vwpreset", "BASS", "ACID SLO", 1),
    ("BASS/Acid_Spike.vwpreset", "BASS", "ACID SPIKE", 1),
    ("BASS/Acid_Squelch.vwpreset", "BASS", "ACID SQUELCH", 1),
    ("BASS/Sub_Kick.vwpreset", "BASS", "SUB KICK", 2),
    ("BASS/Sub_Noise_Punch.vwpreset", "BASS", "SUB NZ PUNCH", 2),
    ("BASS/Sub_Roll.vwpreset", "BASS", "SUB ROLL", 2),
    ("BASS/Pluck_Bass.vwpreset", "BASS", "PLUCK BASS", 2),
    ("BASS/Hoover_Classic.vwpreset", "BASS", "HOOVER CLSC", 2),
    ("BASS/Hoover_Hard.vwpreset", "BASS", "HOOVER HARD", 2),
    ("BASS/Reese_Darkside.vwpreset", "BASS", "REESE DRKSD", 2),
    ("BASS/Reese_Deep.vwpreset", "BASS", "REESE DEEP", 2),
    ("BASS/Reese_DnB.vwpreset", "BASS", "REESE DNB", 2),
]
FXD = dict(ATMOS=(0, 30, 30, 60), LEAD=(0, 10, 35, 25), KEYS=(0, 10, 25, 30), BASS=(0, 0, 0, 0))   # WAVE's category sends

PLUGIN_FT = ["LP12", "LP24", "HP12", "HP24", "BP", "NOTCH", "COMB", "FORMANT", "DIODE"]
VF = {0: 3, 1: 0, 2: 2, 3: 4, 4: 1, 5: 5, 7: 6}                         # plugin filter_type -> eng_wave.c VF_* (VOWEL 6)
STEPS_PER_OCT = 127 / math.log2(16000 / 30)                              # Felucca's cutoff index: ~14 per octave
MS_LFO, MS_ENV, MS_VEL, MS_KEY, MS_AT, MS_MODW = 1, 2, 3, 4, 7, 6
MD_PITCH, MD_CUT, MD_SHP = 1, 2, 3
MD_RES, MD_SUB, MD_DRV = 12 + 5, 12 + 6, 12 + 7                          # E6 RES, E7 SUB, E8 DRV (mod.c MD_E1 + k)
MD_DEPTH = 12 + 8                                                        # mod.c MD_DEPTH: the track LFO's gain
LWAVE = {0: 0, 1: 1, 2: 2, 3: 2, 4: 3, 5: 4, 6: 4}                     # plugin LFO shape -> SIN TRI SAW SQR S&H
SYNC_BARS = [(1, 4.0), (2, 2.0), (3, 1.0), (4, 0.5), (5, 0.25), (6, 0.125), (7, 1 / 12), (8, 0.0625), (9, 1 / 24),
             (10, 0.03125)]                                              # params.c N_LSYNC, in bars
MX_NAME = {MS_LFO: "LFO", MS_ENV: "ENV", MS_VEL: "VEL", MS_KEY: "KEY", MS_AT: "AT", MS_MODW: "MODW"}
DST_NAME = {MD_PITCH: "PITCH", MD_CUT: "CUT", MD_SHP: "POS"}


def f(p, k, d=0.0):
    try:
        return float(p.get(k, d))
    except (TypeError, ValueError):
        return d


def i(p, k, d=0):
    return int(round(f(p, k, d)))


def clampi(v, lo, hi):
    return int(max(lo, min(hi, round(v))))


def time_knob(sec):
    """Felucca's F_TIME: 1 ms * 10000^(v / 127)"""
    return clampi(127 * math.log10(max(sec * 1000.0, 1.0)) / 4.0, 0, 127)


def cut_knob(hz):
    """the inverse of the plugin's 20 + 19980 x^4 (filter_cutoff, skew 0.25)"""
    return clampi(127 * (max(hz - 20.0, 0.0) / 19980.0) ** 0.25, 0, 127)


def convert(p, cat, name):
    lost, notes = [], []
    mode, mix = i(p, "osc_mix_mode"), f(p, "osc_mix", 0.5)
    l1, l2 = f(p, "osc1_level", 0.8), f(p, "osc2_level", 0.8)
    t1 = i(p, "osc1_table")
    if not 0 <= t1 <= 8:
        raise SystemExit(f"{name}: table {t1} is not one WAVE has")
    pos = clampi(127 * f(p, "osc1_position"), 0, 127)
    # OSC2: silent, or a detuned copy of OSC1 (UNISON / SPREAD)
    osc2_on = mix > 1e-3 and not (mode == 1 and l2 <= 1e-3) and not (mode == 0 and i(p, "osc2_table") == t1)
    copy_ct = None
    if osc2_on:
        if mode != 1 or i(p, "osc2_table") != t1 or i(p, "osc2_coarse") != i(p, "osc1_coarse"):
            raise SystemExit(f"{name}: OSC2 sounds and is not a detuned copy of OSC1")
        copy_ct = abs(f(p, "osc2_fine") - f(p, "osc1_fine"))
        w = l1 * (1 - mix) + l2 * mix
        notes.append(f"OSC2 a copy {copy_ct:.0f} cents off: unison")
    elif mode == 1 and mix > 1e-3:
        w = l1 * (1 - mix)
    else:
        w = l1
    olvl = clampi(64 * w / 0.8, 1, 127)
    if i(p, "osc1_coarse"):
        lost.append(f"OSC1 {i(p, 'osc1_coarse'):+d} semitones (TRANS is the track's)")
    if abs(f(p, "osc1_fine")) > 0.5 and copy_ct is None:
        lost.append(f"OSC1 fine {f(p, 'osc1_fine'):+.0f} cents")
    penv = clampi(f(p, "pitchenv_amount"), -24, 24)
    if abs(f(p, "pitchenv_amount")) > 24:
        notes.append(f"PENV {f(p, 'pitchenv_amount'):+.0f} st stored, V2 plays +-24")
    sub = clampi(127 * f(p, "sub_level"), 0, 127)
    noise = clampi(127 * f(p, "noise_level"), 0, 127)
    ncol = clampi(127 * f(p, "noise_color", 0.5), 0, 127)
    suboct = 127 if i(p, "sub_octave") else 0
    # filter
    pft = i(p, "filter_type", 1)
    if pft not in VF:
        raise SystemExit(f"{name}: filter {PLUGIN_FT[pft]} is not one WAVE has")
    ft = VF[pft]
    base = max(20.0, min(20000.0, f(p, "filter_cutoff", 5000)))
    vt = f(p, "filter_veltrack")
    if vt > 0.01:                                                        # V2: the cutoff times 1 - vt + vt vel
        base = max(20.0, base * max(1 - vt, 0.05))
    res, drv = clampi(127 * f(p, "filter_res"), 0, 127), clampi(127 * f(p, "filter_drive"), 0, 127)
    if pft == 7:                                                         # FORMANT: V2 sweeps U .. I by the cutoff
        cut = clampi(127 * (base - 200.0) / 7800.0, 0, 127)
        res = 0
        notes.append("FORMANT as VOWEL")
    else:
        cut = cut_knob(base)
    if int(f(p, "filter2_type")):
        raise SystemExit(f"{name}: uses the second filter")
    # envelopes
    env = [time_knob(f(p, "env2_attack", 0.01)), time_knob(f(p, "env2_decay", 0.1)),
           clampi(127 * f(p, "env2_sustain", 0.7), 0, 127), time_knob(f(p, "env2_release", 0.3))]
    if f(p, "env2_hold") > 0.005:
        lost.append(f"ENV2 hold {f(p, 'env2_hold'):.2f} s")
    dep1, vs1 = f(p, "env1_depth", 0.5), f(p, "env1_vel_sens", 0.5)
    vdep = sum(f(p, f"mod{k}_depth") for k in range(1, 13) if i(p, f"mod{k}_enabled") and i(p, f"mod{k}_source") == 5
               and i(p, f"mod{k}_dest") == 17)
    if abs(vdep) > 0.005 and abs(dep1 + vdep) > 0.005:                   # VEL -> ENV1 depth: the depth at velocity 1, and the
        full = dep1 + vdep                                               # FLT ENV's velocity sensitivity that gives V2's at 0:
        vs1 = max(0.0, min(1.0, 1 - (1 - vs1) * dep1 / full))            # (1 - vs') full = (1 - vs) depth
        dep1 = full
        notes.append("VEL -> ENV1 depth as FLT ENV's velocity")
    fenv = clampi(63 * dep1, -64, 63)                                    # 63 = V2's 4 octaves (eng_wave.c)
    fe = [clampi(1 + 126 * vs1, 1, 127), time_knob(f(p, "env1_attack", 0.01)),
          time_knob(f(p, "env1_decay", 0.1)), clampi(127 * f(p, "env1_sustain", 0.7), 0, 127),
          time_knob(f(p, "env1_release", 0.3))]
    if f(p, "env1_hold") > 0.005:
        lost.append(f"ENV1 hold {f(p, 'env1_hold'):.2f} s")
    if not fenv:
        fe = [0, 0, 64, 127, 64]                                         # no filter envelope: FLT ENV off
    mono = i(p, "voice_poly", 8) == 1
    # voice
    voice, detune, spread = 0, 0, 0
    uni = [1, 2, 3, 4, 6, 8][clampi(i(p, "osc1_unison"), 0, 5)]
    if copy_ct is not None and uni == 1:
        uni, cents = 2, copy_ct / 2
    else:
        cents = 200 * f(p, "osc1_detune")
        if copy_ct is not None:
            cents = max(cents, copy_ct / 2)
    if uni > 1 and cat == "BASS" and not mono:                          # a bass's detuned stack: MONO UNISON keeps its beating
        mono = True                                                      # (WAVE's SPREAD only pans; UNISON is mono)
        notes.append("poly in the plugin: MONO for UNISON")
    if uni > 1:
        if mono:
            voice = 3
            detune = clampi(127 * cents / 40, 0, 127) + 1
            if cents > 40:
                lost.append(f"unison detune +-{cents:.0f} cents (WAVE's at most +-40)")
            notes.append(f"unison {uni} -> UNISON")
        else:
            spread = 64
            lost.append(f"unison {uni} / detune (poly: WAVE's SPREAD pans the voices, it does not detune them)")
    glide = time_knob(f(p, "voice_glide")) if f(p, "voice_glide") > 0.001 else 0
    # LFO: the one that is used, its own target and the matrix
    def depth_slots(n):                                                  # slots on LFOn's depth (V2: depth + the source, e.g. the mod wheel)
        return [(i(p, f"mod{k}_source"), f(p, f"mod{k}_depth")) for k in range(1, 13)
                if i(p, f"mod{k}_enabled") and i(p, f"mod{k}_dest") == 14 + n and abs(f(p, f"mod{k}_depth")) > 0.005
                and i(p, f"mod{k}_source") in (5, 7, 8)]
    def lfo_eff(n):                                                      # LFOn's depth with its depth sources full up
        return f(p, f"lfo{n}_depth") + sum(dd for _, dd in depth_slots(n))
    def lfo_used(n):
        if abs(lfo_eff(n)) < 0.005:
            return False
        if i(p, f"lfo{n}_target") in (1, 3, 7):
            return True
        return any(i(p, f"mod{k}_enabled") and i(p, f"mod{k}_source") == n + 2 and abs(f(p, f"mod{k}_depth")) > 0.005
                   for k in range(1, 13))
    lfo = 1 if lfo_used(1) else 2 if lfo_used(2) else 0
    if lfo == 1 and lfo_used(2):
        lost.append("LFO2 (WAVE has one LFO: LFO1's routings kept)")
    routes, ldflt = [], 0
    if abs(f(p, "filter_keytrack")) > 0.01:
        routes.append((5, MS_KEY, MD_CUT, clampi(64 * STEPS_PER_OCT * f(p, "filter_keytrack") / 12, -64, 63), "keytrack"))
    if vt > 0.01:
        routes.append((6, MS_VEL, MD_CUT, clampi(STEPS_PER_OCT * math.log2(1 / max(1 - vt, 0.05)), -64, 63), "veltrack"))
    if lfo:
        dep, tg = lfo_eff(lfo), i(p, f"lfo{lfo}_target")
        for s_, dd in depth_slots(lfo):                                  # the depth source as WAVE's DEPTH: the LFO's gain follows it
            fs_ = {5: MS_VEL, 7: MS_AT, 8: MS_MODW}[s_]
            routes.append((7, fs_, MD_DEPTH, clampi(64 * dd / dep, -64, 63) if dep else 0, "LFO depth"))
        sign = -1 if i(p, f"lfo{lfo}_shape") == 3 else 1                 # RAMP: a falling saw
        if tg == 3:
            ldflt = clampi(sign * 2 * STEPS_PER_OCT * dep, -64, 63)
        elif tg in (1, 7):
            routes.append((9, MS_LFO, MD_SHP, clampi(sign * 63.5 * dep, -64, 63), "LFO target WT"))
        elif tg:
            lost.append(f"LFO{lfo} target {tg}")
    for k in range(1, 13):
        if not i(p, f"mod{k}_enabled"):
            continue
        src, dst, dd = i(p, f"mod{k}_source"), i(p, f"mod{k}_dest"), f(p, f"mod{k}_depth")
        if abs(dd) < 0.005:
            continue
        if src in (9, 14) or 10 <= src <= 13:
            notes.append(f"macro / CC slot {k} (0 in the plugin)") if 10 <= src <= 13 else None
            continue
        sval = 1.0
        if src in (3, 4):
            if src - 2 != lfo:
                continue                                                  # the other LFO: lost above
            sval = lfo_eff(src - 2)
        fsrc = {0: MS_ENV, 1: MS_ENV, 3: MS_LFO, 4: MS_LFO, 5: MS_VEL, 6: MS_KEY, 7: MS_AT, 8: MS_MODW}.get(src)
        if fsrc is None:
            lost.append(f"matrix slot {k}: source {src}")
            continue
        x = dd * sval
        if src in (3, 4) and i(p, f"lfo{src - 2}_shape") == 3:
            x = -x
        if dst in (1, 3, 5, 7, 9, 20, 22, 24, 26, 28):
            continue                                                      # OSC2's: silent here, or a copy that follows OSC1's
        if dst == 17 and src == 5:
            continue                                                      # VEL -> ENV1 depth: in FLT ENV above
        if dst in (15, 16):
            if dst - 14 != lfo or src not in (5, 7, 8):
                lost.append(f"matrix slot {k}: source {src} -> LFO{dst - 14} depth")
            continue                                                      # (the LFO's DEPTH: above)
        if dst == 10:
            amt, imp, fd = 4 * STEPS_PER_OCT * x, 10, MD_CUT
        elif dst in (2, 3):
            amt, imp, fd = 127 * x, 8, MD_SHP
        elif dst in (0, 1, 19, 20, 21, 22):
            amt, imp, fd = 63 / 11.8 * x * (24 if dst in (19, 20) else 1), 6, MD_PITCH
        elif dst == 11:
            amt, imp, fd = 127 * x, 4, MD_RES
        elif dst == 12:
            amt, imp, fd = 127 * x, 4, MD_DRV
        elif dst == 34:
            amt, imp, fd = 127 * x, 3, MD_SUB
        elif dst in (4, 6, 8, 18):
            continue                                                      # level, pan, detune, AMP: WAVE's own
        else:
            lost.append(f"matrix slot {k}: source {src} -> destination {dst}")
            continue
        routes.append((imp, fsrc, fd, clampi(amt, -64, 63), f"slot {k}"))
    merged = {}
    for imp, s_, d_, a_, why in routes:                                   # same source and destination: one slot
        key = (s_, d_)
        if key in merged:
            merged[key] = (max(imp, merged[key][0]), s_, d_, clampi(merged[key][3] + a_, -64, 63), why)
        else:
            merged[key] = (imp, s_, d_, a_, why)
    routes = sorted((r for r in merged.values() if r[3]), key=lambda r: -r[0])
    for r in routes[4:]:
        lost.append(f"matrix ({r[4]}): more than WAVE's 4 slots")
    routes = routes[:4]
    lx, lsync = [0, 0, 0, 0, 0], 0
    if lfo:
        hz = max(f(p, f"lfo{lfo}_rate", 1.0), 0.05)
        lx = [1, clampi(127 * math.log(hz / 0.05) / math.log(800), 0, 127), LWAVE.get(i(p, f"lfo{lfo}_shape"), 0),
              0 if i(p, f"lfo{lfo}_trigger") else 1,
              time_knob(f(p, f"lfo{lfo}_fade")) if f(p, f"lfo{lfo}_fade") > 0.001 else 0]
        if i(p, f"lfo{lfo}_tempo_sync"):
            bars = f(p, f"lfo{lfo}_sync_div", 0.5)
            lsync = min(SYNC_BARS, key=lambda sb: abs(math.log2(sb[1] / max(bars, 1e-3))))[0]
    # FX: V2's mixes as the sends
    fx = [clampi(126 * f(p, "fx1_param3") * min(1.0, 0.5 + f(p, "fx1_param1")), 0, 126) if f(p, "fx1_param3") > 0.01 else 0,
          clampi(126 * f(p, "fx2_param3"), 0, 126), clampi(126 * f(p, "fx3_param3"), 0, 126),
          clampi(126 * f(p, "fx4_param4"), 0, 126)]
    if not any(fx):
        fx = list(FXD[cat])
        notes.append("dry in the plugin: WAVE's category sends")
    # the signature: every route the preset really has (tools/four_presets.py route_holds), in a fixed order
    sig = []
    if voice == 3:
        sig.append("UNISON")
    if glide and mono:
        sig.append("GLIDE")
    if fe[0] and fenv:
        sig.append("FENV->CUT")
    elif fenv:
        sig.append("ENV->CUT")
    if ldflt:
        sig.append("LFO->CUT")
    for r in sorted(routes, key=lambda r: (r[1], r[2])):
        t = f"{MX_NAME[r[1]]}->{DST_NAME.get(r[2], {MD_RES: 'RES', MD_SUB: 'SUBL', MD_DRV: 'DRV'}.get(r[2]))}"
        if r[2] in DST_NAME and t not in sig:
            sig.append(t)
    for flag, term in ((penv, "PENV"), (sub, "SUB"), (noise, "NOISE")):
        if flag:
            sig.append(term)
    if not sig:
        sig.append("STATIC")
    mx = [list(r[1:4]) for r in routes] + [[0, 0, 0]] * (4 - len(routes))
    return dict(name=name, cat=cat, sig="+".join(sig), e=[t1, pos, 64, penv, cut, res, sub, drv], env=env, fenv=fenv,
                mono=int(mono), fx=fx, ftype=ft, voice=voice, detune=detune, spread=spread, glide=glide, lfo=lx, fe=fe,
                mx=mx, ldflt=ldflt, lsync=lsync, nz=[noise, ncol, suboct], olvl=olvl), lost, notes


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else DEF_SRC
    if not src.is_dir():
        raise SystemExit(f"wave_v2_presets: no factory folder at {src}")
    out, rep = [], []
    for fn, cat, name, pat in PICK:
        root = ET.parse(src / fn).getroot()
        d, lost, notes = convert(root.attrib, cat, name)
        d["pat"] = pat
        out.append(d)
        rep.append((fn, root.attrib.get("_name", fn), d, lost, notes))
    seen = {}
    for d in out:                                                        # four_presets.py's distinctness rule, early
        key = (d["e"][0], d["ftype"], d["sig"])
        if key in seen:
            n = 2
            while (d["e"][0], d["ftype"], f"{d['sig']}+V{n}") in seen:
                n += 1
            d["sig"] = f"{d['sig']}+V{n}"
            key = (d["e"][0], d["ftype"], d["sig"])
        seen[key] = d["name"]
    L = ["# WAVE: 50 factory presets converted from the VOID WAVE V2 plugin's factory set (2026-11-19, the lean upgrade).",
         "# GENERATED by tools/wave_v2_presets.py from D:/VOID SYNTH/VoidWave/Resources/Presets/factory: do not edit by hand,",
         "# change the converter (its docstring has the mapping) and run it again. docs/void_presets.md: what each keeps and loses.",
         "# e = TBL1 POS WARP PENV CUT RES SUB DRV (WARP 64 = off); px = the void_px_t extras (ftype voice detune spread glide",
         "# lfo[5] fe[5] mx[4][3] ldflt lsync nz[3] olvl): nz = NOISE, COLOR, the sub's octave (>= 64: two down); olvl = the",
         "# oscillator's level (64 = the plugin's 0.8). sig: every route the preset has (tools/four_presets.py checks each holds;",
         "# a +V<n> tells apart two V2 presets whose routing is the same: the distinctness rule, kept for the converted bank).",
         "# The 32 presets of the 2026-10-20 bank this replaces are in git (presets/four/wave.py before this change).",
         "", "PRESETS = ["]
    for d in out:
        L.append("    " + repr(d) + ",")
    L.append("]")
    OUT_PY.write_text("\n".join(L) + "\n", newline="\n")
    M = ["# WAVE presets: what each keeps of the VOID WAVE V2 plugin's", "",
         "Generated by `tools/wave_v2_presets.py` from the V2 plugin's factory set (`D:/VOID SYNTH/VoidWave`, git e280794).",
         "WAVE plays the plugin's tables 0-8 at V2's level, its LP24 ladder and SVFs, V2's drive shaper (not its 4x",
         "oversampling), V2's pitch envelope, sub oscillator and noise. Per preset: what the conversion approximates",
         "(notes) and what it could not carry (lost).", "",
         "| # | WAVE | V2 preset | Category | Notes | Lost |", "|---|---|---|---|---|---|"]
    for k, (fn, vname, d, lost, notes) in enumerate(rep):
        M.append(f"| {k + 1} | {d['name']} | {vname} ({fn.split('/')[0]}) | {d['cat']} | {'; '.join(n for n in notes if n) or '-'} | "
                 f"{'; '.join(lost) or '-'} |")
    n_clean = sum(1 for r in rep if not r[3])
    M += ["", f"{len(rep)} presets, {n_clean} with nothing lost.",
          "", "The bank this replaces (2026-10-20: 32 originals, ATMOS 8 LEAD 10 BASS 14, and 18 SLOT placeholders) is in git history."]
    OUT_MD.write_text("\n".join(M) + "\n", newline="\n")
    print(f"wave_v2_presets: {len(out)} presets -> {OUT_PY.relative_to(ROOT)}, report {OUT_MD.relative_to(ROOT)} "
          f"({n_clean} with nothing lost)")


if __name__ == "__main__":
    main()
