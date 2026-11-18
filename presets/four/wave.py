# WAVE: 32 original factory presets for the single-oscillator engine: ATMOS 8, LEAD 10, BASS 14.
# 2026-10-20 bank: dark basses, haunting / lush pads, characterful leads (replaces the Task 4.6 bank).
# The bank is no longer converted from the VOID WAVE plugin (docs/void_presets.md). Each preset is designed around one signature
# route that really defines it, declared in `sig` and checked by tools/four_presets.py: the route must hold in the preset's
# routing, and no two presets may share (table, TYPE, sig).
# e = TBL1 POS WARP (MIX) CUT RES (OSC2) DRV: MIX and OSC2 are hidden (one oscillator, eng_wave.c) and 0; WARP 64 = off.
# px = the void_px_t extras (ftype, voice, detune+1, spread, glide, lfo[5], fe[5], mx[4][3], ldflt, lsync).
# ldflt = LFO DEST FLT (the LFO's own depth on the filter, -64..63; counts as LFO->CUT for sig); lsync = LFO SYNC
# (0 OFF 1 4BAR 2 2BAR 3 1/1 4 1/2 5 1/4 6 1/8 7 8T 8 1/16 9 16T 10 1/32): with SYNC on, RATE is not used.
# Matrix codes: SRC 1 LFO 2 ENV 3 VEL 4 KEY 5 RAND 6 MODW 7 AT 9 S&H; DST 1 PITCH 2 CUT 3 SHP(POS) 11 VIB 14 WARP (E3).
# LFO RATE at 120 BPM: 44 = 1 bar-half (0.5 Hz), 57 = 1/2, 70 = 1/4, 83 = 1/8, 96 = 1/16 ("synced" = that rate, for presets with lsync 0). LFO WAVE 0 SIN 1 TRI 2 SAW 3 SQR 4 S&H.
# ENV -> CUT is the amp envelope's ENV -> FLT amount (fenv, up to 63 steps) when the filter envelope is off (fe[0] = 0), the
# filter envelope (fe) itself reaches one octave at most.
# Tables: 0 SINE 1 SAW 2 SQUARE 3 SPECT 4 FORMA 5 FORME 6 BASS1 7 BASS2 8 NOISE. TYPE: 0 LP(24) 1 BP 2 HP 3 LP12 4 HP24 5 NOTCH 6 VOWEL.
NOFE = [0, 0, 64, 127, 64]
NOMX = [[0, 0, 0]] * 4
FXD = dict(ATMOS=(0, 30, 30, 60), LEAD=(0, 10, 35, 25), BASS=(0, 0, 0, 0))
PATD = dict(ATMOS=5, LEAD=4, BASS=2)


def P(name, cat, sig, tbl, pos, cut, res, drv, env, ftype, warp=64, fenv=0, mono=0, fx=None, pat=None, voice=0, detune=0,
      spread=0, glide=0, lfo=None, fe=None, mx=None, ldflt=0, lsync=0):
    return dict(name=name, cat=cat, sig=sig, e=[tbl, pos, warp, 0, cut, res, 0, drv], env=env, fenv=fenv, mono=mono,
                fx=fx if fx is not None else FXD[cat], pat=pat or PATD[cat], ftype=ftype, voice=voice, detune=detune,
                spread=spread, glide=glide, lfo=lfo or [0, 0, 0, 0, 0], fe=fe or NOFE,
                mx=(mx or []) + [[0, 0, 0]] * (4 - len(mx or [])), ldflt=ldflt, lsync=lsync)


PRESETS = [
    # ------------------------------------------------------------------------------------------------ ATMOS (haunting, lush)
    # (preset 0 is the power-on sound of part 2 (engines.c TRK_DEF): POLY, and the tests that record into that part need it)
    P("HAUNTED HALL", "ATMOS", "LFO->CUT+POS", 4, 50, 52, 60, 0, [96, 80, 120, 110], 6, lfo=[1, 22, 1, 0, 0],
      mx=[[1, 2, 30], [1, 3, 20]], fx=(0, 60, 35, 90)),
    P("VELVET VOID", "ATMOS", "UNISON", 1, 0, 58, 20, 0, [100, 80, 127, 110], 0, voice=3, detune=41, spread=120,
      lfo=[1, 18, 0, 0, 0], mx=[[1, 2, 18]], fx=(0, 70, 25, 85)),
    P("GHOST CHOIR", "ATMOS", "LFO->POS", 5, 40, 60, 45, 0, [105, 80, 120, 115], 6, lfo=[1, 26, 0, 0, 60],
      mx=[[1, 3, 40]], fx=(0, 50, 30, 100)),
    P("BLACK GLASS", "ATMOS", "ENV->POS", 3, 90, 88, 25, 10, [80, 100, 90, 120], 3, mx=[[2, 3, -40]], fx=(0, 40, 50, 90)),
                                    P("FROZEN TIDE", "ATMOS", "UNISON+LFO->POS", 4, 40, 92, 20, 10, [96, 80, 127, 120], 0, voice=3, detune=31, spread=127,
      lfo=[1, 14, 0, 0, 0], mx=[[1, 3, 50], [1, 2, 15]], fx=(0, 60, 20, 100)),
    P("CRYPT DRONE", "ATMOS", "LFO->CUT", 7, 40, 70, 50, 20, [110, 80, 127, 120], 5, lfo=[1, 12, 1, 0, 0],
      mx=[[1, 2, 45]], fx=(10, 30, 30, 90)),
    P("LOST SIGNAL", "ATMOS", "SH->CUT", 8, 20, 66, 100, 0, [90, 80, 110, 120], 1, lfo=[1, 40, 4, 0, 0],
      mx=[[9, 2, 25]], fx=(0, 20, 60, 90)),
    P("MOURNING", "ATMOS", "LFO->WARP", 0, 0, 80, 10, 0, [85, 80, 120, 110], 3, warp=80, lfo=[1, 24, 1, 0, 0],
      mx=[[1, 14, 10]], fx=(0, 70, 30, 80)),
    # ------------------------------------------------------------------------------------------------ LEAD (characterful)
    P("THEREMIN", "LEAD", "LFO->PITCH", 0, 0, 100, 0, 0, [40, 64, 120, 80], 3, mono=1, glide=70,
      lfo=[1, 86, 0, 0, 80], mx=[[1, 1, 2]], fx=(0, 0, 40, 60)),
    P("WRAITH", "LEAD", "ENV->POS", 4, 20, 60, 50, 20, [30, 90, 100, 70], 6, mono=1, glide=40, lfo=[1, 84, 0, 0, 90],
      mx=[[2, 3, 50], [1, 1, 1]], fx=(0, 10, 45, 60)),
    P("NEON RAIN", "LEAD", "FENV->CUT", 1, 0, 70, 30, 10, [20, 80, 110, 80], 0, fenv=40, mono=1, glide=20, voice=3,
      detune=61, spread=90, fe=[90, 10, 70, 60, 70], fx=(0, 20, 50, 60)),
    P("GLITCH MONK", "LEAD", "SH->POS", 3, 60, 80, 40, 30, [10, 64, 120, 60], 1, mono=1, glide=10,
      lfo=[1, 96, 4, 1, 0], mx=[[9, 3, 50]], fx=(0, 10, 55, 40)),
    P("SERPENT", "LEAD", "LFO->WARP", 6, 30, 82, 30, 50, [20, 64, 120, 60], 0, warp=90, mono=1, glide=30,
      lfo=[1, 70, 1, 1, 0], mx=[[1, 14, 20]], fx=(15, 0, 40, 30)),
    P("IRON SIREN", "LEAD", "KEY->POS", 2, 20, 78, 35, 60, [15, 64, 120, 60], 0, mono=1, glide=25,
      mx=[[4, 3, 50]], fx=(20, 0, 35, 30)),
    P("NIGHT CALL", "LEAD", "VEL->CUT", 5, 30, 58, 55, 20, [25, 70, 110, 75], 6, mono=1, glide=25, lfo=[1, 85, 0, 0, 85],
      mx=[[3, 2, 40], [1, 1, 1]], fx=(0, 10, 50, 50)),
    P("DUST HORN", "LEAD", "ENV->CUT", 1, 0, 48, 25, 40, [45, 80, 105, 65], 0, fenv=50, mono=1, glide=20,
      lfo=[1, 85, 0, 0, 70], mx=[[7, 11, 12]], fx=(10, 0, 30, 40)),
    P("CHROME SYNC", "LEAD", "ENV->WARP", 2, 40, 88, 20, 30, [5, 70, 100, 60], 0, mono=1, glide=15,
      mx=[[2, 14, 45]], fx=(0, 5, 40, 30)),
    P("VOID FLUTE", "LEAD", "RAND->CUT", 0, 10, 84, 40, 0, [45, 64, 118, 70], 1, mono=1, glide=50,
      lfo=[1, 86, 0, 0, 85], mx=[[5, 2, 20], [1, 1, 1]], fx=(0, 0, 45, 65)),
    # ------------------------------------------------------------------------------------------------ BASS (dark)
    # The first two are the user's own WAVE 01 / WAVE 02 (user preset slots 1, 2 on the FM-1, 2026-10-20) as they were saved;
    # the ten after the two kept presets share their recipe: BASS1 / BASS2 through LP24, LEGATO, attack 0, a little
    # ENV -> FLT, and a tempo-synced LFO moving the filter directly (ldflt / lsync).
    P("SHADOW GRIND", "BASS", "LFO->CUT", 7, 30, 48, 40, 80, [0, 70, 100, 40], 0, fenv=12, mono=1,
      fx=(82, 0, 0, 0), lfo=[1, 62, 0, 0, 0], ldflt=23, lsync=4),                     # user WAVE 01
    P("NIGHT PULSE", "BASS", "LFO->CUT+VEL->CUT", 6, 24, 37, 30, 40, [0, 80, 120, 40], 0, fenv=6, mono=1,
      lfo=[1, 62, 0, 0, 0], ldflt=21, lsync=6, mx=[[3, 2, 35]]),                      # user WAVE 02
    P("FORMANT GRWL", "BASS", "LFO->CUT", 1, 0, 64, 90, 60, [0, 80, 127, 30], 6, mono=1,
      lfo=[1, 77, 0, 1, 0], mx=[[1, 2, 50]]),                                       # kept from the Task 4.6 bank
    P("NEURO WOBBLE", "BASS", "LFO->POS+CUT", 3, 20, 56, 64, 60, [0, 80, 127, 30], 0, mono=1,
      lfo=[1, 83, 2, 1, 0], mx=[[1, 3, 40], [1, 2, 30]]),                           # kept from the Task 4.6 bank
    P("IRON PULSE", "BASS", "LFO->CUT", 6, 60, 42, 35, 70, [0, 75, 105, 40], 0, fenv=10, mono=1,
      fx=(40, 0, 0, 0), lfo=[1, 62, 0, 0, 0], ldflt=20, lsync=5),
    P("GRAVE PUMP", "BASS", "LFO->CUT", 7, 10, 40, 45, 60, [0, 80, 110, 40], 3, fenv=8, mono=1,
      lfo=[1, 62, 2, 0, 0], ldflt=18, lsync=8),
    P("DEEP CURRENT", "BASS", "LFO->CUT+GLIDE", 6, 40, 38, 30, 40, [0, 80, 120, 45], 0, fenv=10, mono=1, glide=20,
      lfo=[1, 62, 0, 0, 0], ldflt=26, lsync=3),
    P("TRIPLET DOOM", "BASS", "LFO->CUT+POS", 7, 50, 44, 40, 70, [0, 75, 100, 40], 0, fenv=8, mono=1,
      fx=(30, 0, 0, 0), lfo=[1, 62, 0, 0, 0], ldflt=22, lsync=7, mx=[[1, 3, 15]]),
    P("SAW SHADOW", "BASS", "LFO->CUT+VEL->CUT", 1, 0, 36, 35, 60, [0, 80, 115, 40], 0, fenv=8, mono=1,
      lfo=[1, 62, 0, 0, 0], ldflt=20, lsync=6, mx=[[3, 2, 30]]),
    P("PULSE BLACK", "BASS", "LFO->CUT", 2, 10, 40, 30, 60, [0, 80, 110, 40], 0, fenv=10, mono=1,
      fx=(20, 0, 0, 0), lfo=[1, 62, 3, 0, 0], ldflt=22, lsync=5),
    P("DUSK RUMBLE", "BASS", "LFO->CUT", 6, 20, 46, 50, 50, [0, 80, 110, 40], 6, fenv=8, mono=1,
      lfo=[1, 62, 0, 0, 0], ldflt=20, lsync=4),
    P("STEEL RAIN", "BASS", "SH->CUT", 7, 30, 42, 40, 60, [0, 75, 105, 40], 0, fenv=10, mono=1,
      fx=(25, 0, 0, 0), lfo=[1, 62, 4, 0, 0], lsync=9, mx=[[9, 2, 20]]),
    P("BLACK SPECTR", "BASS", "LFO->CUT+POS", 3, 30, 50, 35, 60, [0, 80, 110, 40], 3, fenv=8, mono=1,
      lfo=[1, 62, 0, 0, 0], ldflt=18, lsync=4, mx=[[1, 3, 20]]),
    P("UNDERWORLD", "BASS", "UNISON+LFO->CUT", 6, 30, 40, 30, 50, [0, 80, 115, 40], 0, fenv=8, voice=3, detune=31,
      spread=30, lfo=[1, 62, 0, 0, 0], ldflt=15, lsync=3),
]

# V2 Phase 0.4: presets 32..49 are placeholders (copies of 0..17, named SLOT 33..50) until Phase 5 voices them;
# 0..31 above keep their numbers (saved projects and Program Changes load the same sound)
PRESETS += placeholder(PRESETS[:18], 32)
