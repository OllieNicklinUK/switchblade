# CZ-Z: Casio CZ phase distortion (eng_cz.c, V2 Phase 1). A first voicing, written in the CZ's own terms; Phase 5 voices
# them by ear with the user, and the preset audit (tests/preset_audit_test.c) measures their levels.
# e = [WAV1, WAV2, LINE, DTN, CUT, KEYF, MOD, DCWE]
#   WAV 0 SAW 1 SQR 2 PULS 3 DSIN 4 SAWP 5 RES1 6 RES2 7 RES3; LINE 0 "1", 1 "2", 2 "1+1'", 3 "1+2'"; DTN cents (line 2);
#   CUT the DCW; KEYF DCW key follow; MOD 0 OFF 1 RING 2 NOISE; DCWE the 8-step DCW envelope's depth
# dcw = the 8-step DCW envelope (rates 0..99, levels 0..99, sus step or None, end step); oct2 = line 2's octave -2..2.
# env = voice.c's ADSR (the amplitude); fenv = ENV -> DCW; fx = DIST CHORUS DELAY REVERB sends.


def dcw(r, l, sus, end):
    r, l = list(r) + [0] * (8 - len(r)), list(l) + [0] * (8 - len(l))
    return dict(r=r, l=l, sus=sus, end=end)


KEYS_FX, BASS_FX, LEAD_FX, ATMOS_FX = (0, 20, 10, 30), (0, 0, 0, 6), (0, 10, 25, 20), (0, 30, 20, 60)
DECAY = dcw([99, 40], [90, 10], None, 1)           # a struck sound: up at once, then down to a little
PLUCKD = dcw([99, 60], [99, 0], None, 1)
HELD = dcw([92, 55, 45], [99, 60, 0], 1, 2)        # the default shape: up, settle, hold, close on release
SLOW = dcw([30, 20, 25], [80, 50, 0], 1, 2)

P = [
    # KEYS (14)
    ("CZ EPIANO", "KEYS", [3, 0, 3, 6, 20, 40, 0, 90], [0, 70, 40, 60], DECAY, 0),
    ("TINE PIANO", "KEYS", [3, 3, 2, 4, 10, 30, 0, 100], [0, 75, 30, 60], dcw([99, 35], [99, 0], None, 1), 0),
    ("CLAVI PD", "KEYS", [4, 1, 3, 0, 30, 50, 0, 80], [0, 50, 0, 30], dcw([99, 60], [80, 0], None, 1), 0),
    ("HARPSI PD", "KEYS", [1, 0, 3, 8, 40, 40, 0, 70], [0, 60, 20, 40], dcw([99, 55], [90, 20], None, 1), 0),
    ("VIBES PD", "KEYS", [3, 3, 2, 3, 0, 20, 0, 60], [0, 90, 30, 80], DECAY, 0),
    ("MARIMBA PD", "KEYS", [3, 0, 0, 0, 5, 30, 0, 90], [0, 40, 0, 30], PLUCKD, 0),
    ("BELL PD", "KEYS", [3, 3, 3, 40, 30, 10, 1, 60], [0, 100, 0, 100], DECAY, 1),
    ("TOY PIANO", "KEYS", [2, 3, 3, 10, 30, 40, 0, 70], [0, 55, 10, 40], DECAY, 1),
    ("ORGAN PD", "KEYS", [3, 1, 3, 0, 40, 0, 0, 0], [0, 0, 127, 20], HELD, 0),
    ("PERC ORGAN", "KEYS", [1, 3, 3, 2, 25, 10, 0, 70], [0, 0, 127, 20], dcw([99, 70], [99, 30], 1, 1), 0),
    ("GLASS KEYS", "KEYS", [6, 3, 3, 5, 20, 30, 0, 80], [0, 80, 40, 70], DECAY, 0),
    ("DIGI KEYS", "KEYS", [5, 0, 3, 7, 10, 20, 0, 100], [0, 70, 30, 60], DECAY, 0),
    ("PLUCK PD", "KEYS", [0, 2, 3, 6, 0, 40, 0, 110], [0, 45, 0, 30], PLUCKD, 0),
    ("SOFT KEYS", "KEYS", [3, 0, 2, 9, 30, 30, 0, 40], [10, 80, 60, 60], HELD, 0),
    # BASS (12)
    ("CZ BASS", "BASS", [0, 0, 0, 0, 10, 0, 0, 110], [0, 60, 80, 20], dcw([99, 55], [99, 30], 1, 1), 0),
    ("RESO BASS", "BASS", [5, 0, 0, 0, 5, 0, 0, 120], [0, 60, 80, 20], dcw([99, 50], [99, 20], 1, 1), 0),
    ("SQR BASS", "BASS", [1, 0, 0, 0, 15, 0, 0, 90], [0, 60, 90, 20], dcw([99, 60], [90, 30], 1, 1), 0),
    ("SYNC BASS", "BASS", [6, 0, 0, 0, 20, 0, 0, 110], [0, 60, 80, 20], dcw([99, 45], [99, 25], 1, 1), 0),
    ("FAT BASS", "BASS", [0, 1, 3, 8, 20, 0, 0, 80], [0, 60, 90, 20], HELD, -1),
    ("PULSE BASS", "BASS", [2, 0, 0, 0, 25, 0, 0, 70], [0, 60, 90, 20], HELD, 0),
    ("WOOD BASS", "BASS", [3, 0, 0, 0, 0, 0, 0, 90], [0, 50, 30, 20], PLUCKD, 0),
    ("ACID PD", "BASS", [7, 0, 0, 0, 10, 0, 0, 127], [0, 40, 60, 10], dcw([99, 65], [99, 0], None, 1), 0),
    ("SUB PD", "BASS", [3, 3, 2, 3, 0, 0, 0, 30], [0, 0, 127, 20], HELD, 0),
    ("SAWP BASS", "BASS", [4, 0, 0, 0, 20, 0, 0, 90], [0, 60, 80, 20], dcw([99, 55], [99, 30], 1, 1), 0),
    ("RING BASS", "BASS", [0, 3, 3, 0, 20, 0, 1, 60], [0, 60, 80, 20], HELD, 1),
    ("NOISE BASS", "BASS", [1, 0, 3, 0, 30, 0, 2, 60], [0, 50, 60, 20], HELD, 0),
    # LEAD (12)
    ("CZ BRASS", "LEAD", [0, 0, 2, 8, 10, 20, 0, 100], [8, 60, 90, 30], dcw([70, 40, 50], [99, 60, 0], 1, 2), 0),
    ("SYNC LEAD", "LEAD", [5, 0, 0, 0, 30, 20, 0, 100], [0, 60, 100, 30], HELD, 0),
    ("SQR LEAD", "LEAD", [1, 1, 2, 6, 40, 10, 0, 60], [0, 60, 100, 30], HELD, 0),
    ("SOLO SAW", "LEAD", [0, 0, 2, 12, 50, 20, 0, 40], [0, 60, 110, 30], HELD, 0),
    ("WHISTLE PD", "LEAD", [3, 0, 0, 0, 0, 20, 0, 20], [12, 60, 110, 30], SLOW, 0),
    ("RESO LEAD", "LEAD", [7, 6, 3, 5, 30, 20, 0, 90], [0, 60, 100, 30], HELD, 0),
    ("OCT LEAD", "LEAD", [0, 1, 3, 4, 40, 20, 0, 60], [0, 60, 100, 30], HELD, 1),
    ("RING LEAD", "LEAD", [3, 0, 3, 20, 40, 20, 1, 70], [0, 60, 100, 30], HELD, 1),
    ("PULSE SOLO", "LEAD", [2, 2, 2, 8, 35, 20, 0, 70], [0, 60, 100, 30], HELD, 0),
    ("SAWP LEAD", "LEAD", [4, 0, 0, 0, 40, 20, 0, 60], [0, 60, 100, 30], HELD, 0),
    ("HIGH LEAD", "LEAD", [0, 0, 3, 7, 40, 20, 0, 50], [0, 60, 100, 30], HELD, 1),
    ("SCREAM PD", "LEAD", [6, 7, 3, 10, 60, 10, 0, 100], [0, 60, 100, 30], HELD, 0),
    # ATMOS (12)
    ("CZ STRINGS", "ATMOS", [0, 0, 2, 10, 20, 30, 0, 60], [60, 80, 110, 80], SLOW, 0),
    ("CZ CHOIR", "ATMOS", [6, 3, 3, 6, 10, 30, 0, 70], [70, 80, 110, 90], SLOW, 0),
    ("GLASS PAD", "ATMOS", [3, 3, 2, 7, 20, 20, 0, 80], [50, 90, 100, 100], SLOW, 0),
    ("SWEEP PAD", "ATMOS", [5, 6, 3, 8, 0, 10, 0, 127], [50, 90, 110, 90], dcw([15, 10], [99, 20], None, 1), 0),
    ("NOISE WIND", "ATMOS", [3, 0, 3, 0, 30, 0, 2, 60], [80, 80, 110, 110], SLOW, 0),
    ("RING PAD", "ATMOS", [3, 3, 3, 30, 20, 20, 1, 50], [60, 90, 100, 100], SLOW, 1),
    ("WARM PAD", "ATMOS", [3, 0, 3, 5, 40, 20, 0, 30], [70, 80, 110, 90], SLOW, 0),
    ("DARK DRONE", "ATMOS", [0, 1, 3, 3, 10, 0, 0, 40], [90, 0, 127, 110], SLOW, -1),
    ("SPACE PD", "ATMOS", [7, 5, 3, 9, 10, 10, 0, 90], [60, 90, 100, 110], SLOW, 0),
    ("HOLLOW PAD", "ATMOS", [1, 3, 3, 6, 30, 20, 0, 50], [60, 80, 110, 90], SLOW, 0),
    ("SHIMMER PD", "ATMOS", [6, 6, 2, 11, 20, 10, 0, 80], [60, 90, 100, 100], SLOW, 1),
    ("MOTION PAD", "ATMOS", [5, 7, 3, 4, 15, 10, 0, 110], [60, 90, 110, 100],
     dcw([20, 20, 20, 20, 20], [90, 30, 80, 40, 0], 3, 4), 0),
]

FX = dict(KEYS=KEYS_FX, BASS=BASS_FX, LEAD=LEAD_FX, ATMOS=ATMOS_FX)
PRESETS = [dict(name=n, cat=c, e=e, env=env, fenv=0, mono=1 if c in ("BASS", "LEAD") else 0, fx=FX[c], dcw=d, oct2=o)
           for n, c, e, env, d, o in P]
