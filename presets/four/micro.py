# MICRO: after the Waldorf Microwave I (eng_micro.c, V2 Phase 4). A first voicing, our own sounds, for Phase 5's listening.
# e = [WT, POS, EPOS, OSC2, CUT, RES, GRIT, KTR]
#   WT the table (0 UPPER 1 SYNC 2 PWM 3 FRMNT 4 BASIC 5 HOLLW 6 ORGAN 7 DIGIT 8 RESO 9 VOCAL 10 FM 11 FOLD 12 PARTL
#   13 BELL 14 BITS 15 STRNG), POS the start wave, EPOS the envelope's sweep (-64..63), OSC2 the second oscillator's
#   detune in cents (0 off), CUT / RES the 24 dB low-pass, GRIT bits and hold, KTR key tracking.

ATMOS_FX, KEYS_FX, LEAD_FX, BASS_FX = (0, 30, 24, 64), (0, 16, 16, 34), (0, 10, 28, 24), (0, 0, 0, 8)

P = [
    # ATMOS (18)
    ("PPG SWEEP", "ATMOS", [0, 0, 63, 8, 100, 20, 20, 40], [60, 100, 100, 90]),
    ("GLASS TIDE", "ATMOS", [13, 20, 50, 6, 110, 10, 10, 40], [70, 100, 110, 100]),
    ("VOCAL MIST", "ATMOS", [9, 0, 60, 10, 96, 20, 16, 40], [80, 90, 110, 100]),
    ("DIGI DRIFT", "ATMOS", [7, 40, 30, 12, 90, 24, 40, 40], [70, 100, 110, 100]),
    ("STRING WAVE", "ATMOS", [15, 30, 30, 9, 92, 10, 8, 50], [60, 90, 110, 90]),
    ("FORMANT AIR", "ATMOS", [3, 10, 50, 7, 100, 16, 12, 40], [80, 90, 110, 110]),
    ("HOLLOW PAD", "ATMOS", [5, 20, 40, 6, 88, 20, 12, 40], [60, 90, 110, 90]),
    ("ORGAN HAZE", "ATMOS", [6, 0, 60, 5, 96, 10, 16, 40], [50, 90, 110, 90]),
    ("BELL FIELD", "ATMOS", [13, 0, 63, 4, 116, 8, 8, 40], [40, 100, 80, 110]),
    ("FM CLOUD", "ATMOS", [10, 10, 50, 8, 100, 14, 16, 40], [70, 100, 110, 100]),
    ("RESO NIGHT", "ATMOS", [8, 20, 40, 10, 84, 30, 20, 40], [80, 100, 110, 110]),
    ("BITSTREAM", "ATMOS", [14, 20, 30, 12, 90, 20, 70, 40], [60, 90, 110, 90]),
    ("SLOW UPPER", "ATMOS", [0, 30, -40, 10, 96, 16, 16, 40], [100, 100, 110, 110]),
    ("WARM WAVES", "ATMOS", [4, 40, 20, 8, 70, 20, 8, 40], [70, 90, 110, 90]),
    ("FOLD SPACE", "ATMOS", [11, 0, 63, 6, 90, 20, 20, 40], [80, 100, 110, 110]),
    ("PARTIAL SEA", "ATMOS", [12, 0, 63, 9, 110, 10, 12, 40], [70, 100, 110, 110]),
    ("COLD CHOIR", "ATMOS", [9, 40, -30, 6, 100, 16, 24, 40], [70, 90, 110, 100]),
    ("PWM HALO", "ATMOS", [2, 10, 50, 10, 96, 14, 8, 40], [60, 90, 110, 100]),
    # KEYS (12)
    ("WAVE KEYS", "KEYS", [0, 20, 40, 0, 90, 20, 24, 64], [0, 70, 50, 50]),
    ("DIGI PIANO", "KEYS", [13, 30, 40, 4, 100, 10, 20, 64], [0, 70, 30, 50]),
    ("SYNC KEYS", "KEYS", [1, 10, 50, 0, 96, 20, 24, 64], [0, 60, 40, 40]),
    ("PPG CLAV", "KEYS", [0, 50, 40, 0, 84, 30, 40, 64], [0, 40, 10, 30]),
    ("BELL KEYS", "KEYS", [13, 10, 50, 0, 110, 10, 16, 64], [0, 80, 20, 70]),
    ("ORGAN BITS", "KEYS", [6, 60, 0, 3, 100, 10, 50, 64], [0, 0, 127, 30]),
    ("FM TINE", "KEYS", [10, 30, 30, 0, 100, 10, 16, 64], [0, 70, 30, 50]),
    ("HARP WAVE", "KEYS", [12, 20, 40, 0, 104, 10, 20, 64], [0, 60, 0, 50]),
    ("PLUCK STEP", "KEYS", [3, 40, 63, 0, 112, 30, 30, 64], [0, 56, 12, 44]),
    ("VOX KEYS", "KEYS", [9, 20, 40, 4, 96, 16, 20, 64], [0, 70, 50, 50]),
    ("HOLLOW KEYS", "KEYS", [5, 30, 30, 4, 90, 20, 20, 64], [0, 70, 40, 50]),
    ("GRIT POLY", "KEYS", [7, 40, 30, 6, 90, 20, 90, 64], [0, 60, 60, 40]),
    # LEAD (10, mono)
    ("WAVE LEAD", "LEAD", [0, 50, 30, 6, 100, 30, 24, 64], [0, 60, 110, 30]),
    ("SYNC SCREAM", "LEAD", [1, 20, 63, 8, 110, 30, 30, 64], [0, 60, 110, 30]),
    ("PWM SOLO", "LEAD", [2, 30, 40, 5, 96, 20, 16, 64], [0, 60, 110, 30]),
    ("FORMANT LEAD", "LEAD", [3, 40, 30, 4, 100, 20, 20, 64], [0, 60, 110, 30]),
    ("BIT LEAD", "LEAD", [14, 30, 30, 4, 100, 20, 80, 64], [0, 60, 110, 30]),
    ("FOLD LEAD", "LEAD", [11, 30, 40, 6, 96, 30, 20, 64], [0, 60, 110, 30]),
    ("FM LEAD", "LEAD", [10, 50, 20, 6, 100, 20, 20, 64], [0, 60, 110, 30]),
    ("RESO LEAD", "LEAD", [8, 40, 30, 0, 100, 40, 24, 64], [0, 60, 110, 30]),
    ("UPPER LEAD", "LEAD", [12, 60, -30, 6, 104, 20, 16, 64], [0, 60, 110, 30]),
    ("DIRTY LEAD", "LEAD", [7, 60, 20, 10, 100, 30, 110, 64], [0, 60, 110, 30]),
    # BASS (10, mono)
    ("WAVE BASS", "BASS", [4, 90, 30, 0, 60, 30, 24, 32], [0, 50, 80, 20]),
    ("SYNC BASS", "BASS", [1, 0, 50, 0, 70, 30, 24, 32], [0, 50, 80, 20]),
    ("PWM BASS", "BASS", [2, 20, 40, 4, 60, 30, 16, 32], [0, 50, 80, 20]),
    ("DIGI BASS", "BASS", [7, 30, 30, 0, 64, 30, 40, 32], [0, 50, 80, 20]),
    ("BIT BASS", "BASS", [14, 20, 20, 0, 70, 30, 90, 32], [0, 50, 80, 20]),
    ("FOLD BASS", "BASS", [11, 10, 50, 0, 66, 30, 20, 32], [0, 50, 80, 20]),
    ("FM BASS", "BASS", [10, 30, 40, 0, 64, 20, 20, 32], [0, 50, 80, 20]),
    ("RESO BASS", "BASS", [8, 0, 63, 0, 88, 40, 24, 32], [0, 50, 70, 20]),
    ("ORGAN BASS", "BASS", [6, 0, 0, 0, 70, 10, 16, 32], [0, 0, 127, 20]),
    ("DEEP STEP", "BASS", [0, 0, 40, 4, 50, 20, 20, 32], [0, 50, 80, 20]),
]

FX = dict(ATMOS=ATMOS_FX, KEYS=KEYS_FX, LEAD=LEAD_FX, BASS=BASS_FX)
PRESETS = [dict(name=n, cat=c, e=e, env=env, fenv=0, mono=1 if c in ("LEAD", "BASS") else 0, fx=FX[c])
           for n, c, e, env in P]
