# JP-Z: the JP-8000 Super Saw (eng_jpz.c, V2 Phase 3). A first voicing for Phase 5's listening; our own names and
# patches, not the JP factory bank. e = [DTN, MIX, BAL, OSC2, CUT, RES, KTR, WID]
#   DTN the Super Saw detune (the JP's knob curve), MIX the six side saws, BAL Super Saw (-64) .. OSC2 (+63),
#   OSC2 its interval in semitones, CUT / RES the filter (LP 24 dB), KTR key tracking, WID the stereo chorus (0 off,
#   1..42 I, 43..85 II, 86..127 I+II).
# env = voice.c's ADSR; fenv = ENV -> cutoff; fx = DIST CHORUS DELAY REVERB sends.

LEAD_FX, ATMOS_FX, KEYS_FX, BASS_FX = (0, 0, 30, 26), (0, 0, 24, 60), (0, 0, 16, 30), (0, 0, 0, 8)
WIDE, MID, OFF = 100, 60, 0

P = [
    # LEAD (16, mono)
    ("TRANCE LEAD", "LEAD", [70, 100, -64, 0, 96, 20, 64, WIDE], [0, 70, 110, 40], 10),
    ("HOOVER LEAD", "LEAD", [110, 110, -30, -12, 80, 30, 64, MID], [0, 70, 110, 30], 20),
    ("SAW SOLO", "LEAD", [40, 70, -64, 0, 90, 30, 64, OFF], [0, 70, 110, 30], 12),
    ("OCTAVE LEAD", "LEAD", [60, 90, 0, 12, 88, 20, 64, MID], [0, 70, 110, 30], 10),
    ("FIFTH LEAD", "LEAD", [60, 90, 0, 7, 88, 20, 64, MID], [0, 70, 110, 30], 10),
    ("ACID SUPER", "LEAD", [50, 80, -64, 0, 50, 90, 64, OFF], [0, 50, 60, 20], 40),
    ("SCREAMER", "LEAD", [90, 120, -64, 0, 110, 60, 64, OFF], [0, 70, 110, 30], 0),
    ("THIN LEAD", "LEAD", [20, 40, -64, 0, 80, 40, 64, OFF], [0, 70, 110, 30], 8),
    ("RAVE LEAD", "LEAD", [100, 127, -64, 0, 100, 10, 64, WIDE], [0, 70, 110, 30], 6),
    ("SOFT LEAD", "LEAD", [50, 60, -64, 0, 60, 10, 64, MID], [10, 70, 110, 40], 10),
    ("SYNC LEAD", "LEAD", [30, 50, 40, 19, 90, 30, 64, OFF], [0, 70, 110, 30], 14),
    ("EUPHORIA", "LEAD", [80, 110, -64, 0, 92, 24, 64, WIDE], [4, 70, 110, 50], 12),
    ("PORTA LEAD", "LEAD", [64, 96, -64, 0, 88, 20, 64, MID], [0, 70, 110, 30], 10),
    ("SQUELCH", "LEAD", [40, 70, -64, 0, 40, 100, 64, OFF], [0, 40, 50, 20], 50),
    ("DETUNE SOLO", "LEAD", [120, 90, -64, 0, 84, 20, 64, OFF], [0, 70, 110, 30], 8),
    ("ANTHEM LEAD", "LEAD", [76, 104, -40, 12, 96, 16, 64, WIDE], [0, 70, 110, 50], 10),
    # ATMOS (14)
    ("SUPER PAD", "ATMOS", [70, 100, -64, 0, 70, 10, 32, WIDE], [60, 80, 110, 90], 10),
    ("TRANCE PAD", "ATMOS", [80, 110, -64, 0, 64, 14, 32, WIDE], [50, 80, 110, 90], 14),
    ("DARK SWELL", "ATMOS", [60, 90, -64, 0, 40, 30, 32, MID], [80, 80, 110, 100], 20),
    ("SHIMMER", "ATMOS", [90, 100, 0, 12, 80, 10, 32, WIDE], [60, 80, 110, 100], 6),
    ("STRINGS JP", "ATMOS", [50, 80, -64, 0, 76, 6, 40, WIDE], [40, 80, 110, 70], 4),
    ("FILTER SWEEP", "ATMOS", [70, 100, -64, 0, 30, 60, 32, MID], [70, 100, 100, 90], 40),
    ("CHOIR SAW", "ATMOS", [40, 60, -64, 0, 56, 30, 32, WIDE], [60, 80, 110, 90], 10),
    ("SLOW TIDE", "ATMOS", [100, 120, -64, 0, 50, 20, 32, WIDE], [90, 90, 110, 110], 24),
    ("GLASS PAD", "ATMOS", [30, 50, 20, 24, 90, 20, 32, WIDE], [60, 80, 110, 100], 4),
    ("WIDE DRONE", "ATMOS", [110, 127, -64, 0, 60, 10, 0, WIDE], [80, 0, 127, 110], 0),
    ("WARM PAD JP", "ATMOS", [60, 80, -64, 0, 48, 10, 32, MID], [60, 80, 110, 90], 10),
    ("OCT PAD", "ATMOS", [70, 90, 0, -12, 60, 10, 32, WIDE], [60, 80, 110, 90], 10),
    ("RISER", "ATMOS", [127, 127, -64, 0, 40, 40, 32, WIDE], [110, 0, 127, 100], 50),
    ("NIGHT AIR", "ATMOS", [50, 70, -64, 0, 66, 24, 32, WIDE], [70, 80, 110, 110], 8),
    # KEYS (12)
    ("SUPER STAB", "KEYS", [80, 110, -64, 0, 70, 20, 64, MID], [0, 50, 30, 30], 40),
    ("HOUSE CHORD", "KEYS", [60, 90, -64, 0, 60, 30, 64, MID], [0, 45, 20, 30], 46),
    ("PLUCK JP", "KEYS", [40, 70, -64, 0, 64, 30, 64, OFF], [0, 50, 0, 36], 50),
    ("ORGAN SAW", "KEYS", [20, 30, 0, 12, 90, 0, 64, OFF], [0, 0, 127, 20], 0),
    ("BRASS JP", "KEYS", [50, 80, -64, 0, 50, 10, 64, MID], [8, 60, 90, 30], 36),
    ("HARD STAB", "KEYS", [100, 127, -64, 0, 80, 40, 64, OFF], [0, 40, 20, 20], 30),
    ("POLY SWEEP", "KEYS", [60, 90, -64, 0, 44, 50, 64, MID], [0, 60, 60, 40], 40),
    ("FIFTH STAB", "KEYS", [60, 90, 0, 7, 64, 20, 64, MID], [0, 50, 30, 30], 36),
    ("BELL SAW", "KEYS", [20, 40, 30, 24, 100, 10, 64, WIDE], [0, 70, 20, 60], 10),
    ("SOFT KEYS JP", "KEYS", [40, 60, -64, 0, 56, 10, 64, MID], [4, 60, 60, 50], 20),
    ("CLUB STAB", "KEYS", [90, 120, -64, 0, 66, 24, 64, WIDE], [0, 40, 10, 30], 44),
    ("PIANO SAW", "KEYS", [30, 50, -64, 0, 60, 10, 64, OFF], [0, 70, 30, 40], 30),
    # BASS (8, mono)
    ("SUPER BASS", "BASS", [40, 70, -64, 0, 40, 20, 32, OFF], [0, 50, 80, 20], 30),
    ("REESE JP", "BASS", [90, 100, -64, 0, 36, 10, 32, OFF], [0, 0, 127, 20], 10),
    ("SUB SAW", "BASS", [10, 20, 20, -12, 40, 10, 32, OFF], [0, 50, 90, 20], 20),
    ("ACID BASS JP", "BASS", [20, 40, -64, 0, 24, 100, 32, OFF], [0, 40, 40, 10], 50),
    ("PLUCK BASS", "BASS", [30, 60, -64, 0, 28, 20, 32, OFF], [0, 40, 20, 20], 44),
    ("OCT BASS", "BASS", [40, 60, 0, -12, 40, 20, 32, OFF], [0, 50, 80, 20], 24),
    ("GROWL BASS", "BASS", [70, 90, -64, 0, 30, 60, 32, OFF], [0, 50, 80, 20], 40),
    ("DEEP JP", "BASS", [20, 30, -64, 0, 20, 10, 32, OFF], [0, 50, 100, 20], 20),
]

FX = dict(LEAD=LEAD_FX, ATMOS=ATMOS_FX, KEYS=KEYS_FX, BASS=BASS_FX)
PRESETS = [dict(name=n, cat=c, e=e, env=env, fenv=fe, mono=1 if c in ("LEAD", "BASS") else 0, fx=FX[c])
           for n, c, e, env, fe in P]
