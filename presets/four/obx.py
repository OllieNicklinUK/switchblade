# OB-Z: 32 factory presets (BRASS 8, STRINGS 8, KEYS 8, LEAD 8).
# e = WAVE (0 SAW 1 SQR 2 TRI 3 SIN 4 PWM 5 SYNC 6 SUB), DTN, MIX, PW, CUT, RES, MODE, SLOP
# PW 64 / MODE 0 / SLOP 0 are the neutral settings of the three new slots (Task 4.1); Task 7.2 voices them.
# env = ATK DEC SUS REL; fenv = ENV -> FILTER (-64..63); fx = DIST CHORUS DELAY REVERB sends
PRESETS = [
    # --- LEAD ---
    dict(name="SAW LEAD", cat="LEAD", e=[0, 12, 64, 64, 90, 30, 0, 0], env=[4, 70, 100, 50], fenv=20, mono=0, fx=(0, 10, 45, 30), pat=4),
    dict(name="SQR LEAD", cat="LEAD", e=[1, 0, 0, 64, 80, 50, 0, 0], env=[0, 60, 90, 30], fenv=30, mono=0, fx=(5, 0, 35, 20), pat=4),
    dict(name="ACID LEAD", cat="LEAD", e=[0, 0, 0, 64, 50, 100, 0, 0], env=[0, 55, 20, 30], fenv=48, mono=0, fx=(20, 0, 45, 15), pat=1),
    dict(name="RAVE", cat="LEAD", e=[4, 30, 64, 64, 85, 20, 0, 0], env=[20, 80, 110, 60], fenv=10, mono=0, fx=(30, 40, 30, 30), pat=9),
    dict(name="SUB LEAD", cat="LEAD", e=[3, 0, 0, 64, 90, 0, 0, 0], env=[0, 60, 100, 25], fenv=0, mono=0, fx=(0, 0, 25, 20), pat=4),
    dict(name="SYNC LEAD", cat="LEAD", e=[5, 40, 100, 64, 95, 25, 0, 0], env=[2, 70, 100, 40], fenv=36, mono=0, fx=(8, 0, 40, 25), pat=4),
    dict(name="PWM LEAD", cat="LEAD", e=[4, 16, 70, 64, 88, 30, 0, 0], env=[6, 70, 100, 45], fenv=18, mono=0, fx=(0, 25, 40, 30), pat=4),
    dict(name="FAT LEAD", cat="LEAD", e=[0, 34, 100, 64, 76, 40, 0, 0], env=[3, 70, 105, 50], fenv=26, mono=0, fx=(12, 20, 45, 35), pat=4),
    # --- BRASS ---
    dict(name="BRASS", cat="BRASS", e=[0, 10, 64, 64, 45, 20, 0, 0], env=[35, 70, 90, 45], fenv=40, mono=0, fx=(0, 20, 20, 40), pat=6),
    dict(name="ROYAL HORNS", cat="BRASS", e=[0, 12, 64, 64, 78, 34, 0, 0], env=[18, 70, 96, 50], fenv=28, mono=0, fx=(0, 24, 20, 40), pat=6),
    dict(name="WIND", cat="BRASS", e=[0, 0, 0, 64, 30, 90, 0, 0], env=[60, 90, 60, 80], fenv=50, mono=0, fx=(0, 30, 30, 70), pat=5),
    dict(name="SOLO TRUMPET", cat="BRASS", e=[0, 6, 40, 64, 70, 25, 0, 0], env=[10, 60, 100, 35], fenv=45, mono=1, fx=(0, 8, 25, 30), pat=4),
    dict(name="MELLOW BONE", cat="BRASS", e=[0, 8, 64, 64, 36, 12, 0, 0], env=[30, 70, 95, 40], fenv=30, mono=0, fx=(0, 16, 20, 40), pat=6),
    dict(name="SYNTH BRASS", cat="BRASS", e=[0, 18, 90, 64, 60, 15, 0, 0], env=[14, 75, 100, 45], fenv=34, mono=0, fx=(0, 30, 15, 35), pat=6),
    dict(name="FANFARE", cat="BRASS", e=[4, 14, 80, 64, 66, 20, 0, 0], env=[8, 65, 100, 45], fenv=50, mono=0, fx=(0, 20, 20, 50), pat=6),
    dict(name="JUMP STAB", cat="BRASS", e=[0, 8, 64, 64, 85, 15, 0, 0], env=[0, 50, 40, 30], fenv=55, mono=0, fx=(5, 10, 30, 30), pat=6),
    # --- STRINGS ---
    dict(name="STRINGS", cat="STRINGS", e=[0, 25, 64, 64, 70, 10, 0, 0], env=[70, 90, 115, 90], fenv=5, mono=0, fx=(0, 60, 20, 70), pat=5),
    dict(name="PWM STR", cat="STRINGS", e=[4, 8, 40, 64, 75, 20, 0, 0], env=[60, 80, 110, 85], fenv=8, mono=0, fx=(0, 50, 20, 60), pat=5),
    dict(name="SOFT PAD", cat="STRINGS", e=[0, 20, 64, 64, 60, 10, 0, 0], env=[80, 90, 110, 95], fenv=10, mono=0, fx=(0, 60, 20, 70), pat=5),
    dict(name="SOLINA", cat="STRINGS", e=[0, 30, 90, 64, 82, 5, 0, 0], env=[50, 85, 118, 80], fenv=0, mono=0, fx=(0, 90, 15, 50), pat=5),
    dict(name="SLOW BOW", cat="STRINGS", e=[0, 14, 64, 64, 52, 15, 0, 0], env=[100, 90, 118, 100], fenv=22, mono=0, fx=(0, 40, 20, 80), pat=5),
    dict(name="CELLO", cat="STRINGS", e=[0, 6, 40, 64, 40, 20, 0, 0], env=[45, 80, 110, 70], fenv=26, mono=0, fx=(0, 20, 10, 50), pat=5),
    dict(name="VIOLINS", cat="STRINGS", e=[0, 22, 64, 64, 100, 8, 0, 0], env=[60, 85, 115, 75], fenv=4, mono=0, fx=(0, 70, 25, 70), pat=5),
    dict(name="TAPE STR", cat="STRINGS", e=[4, 20, 70, 64, 56, 14, 0, 0], env=[55, 85, 112, 85], fenv=12, mono=0, fx=(8, 80, 30, 60), pat=5),
    # --- KEYS ---
    dict(name="SINE KEY", cat="KEYS", e=[3, 6, 50, 64, 127, 0, 0, 0], env=[2, 80, 30, 70], fenv=0, mono=0, fx=(0, 30, 25, 40), pat=6),
    dict(name="PLUCK", cat="KEYS", e=[0, 8, 50, 64, 30, 40, 0, 0], env=[0, 88, 0, 60], fenv=55, mono=0, fx=(0, 20, 50, 30), pat=3),
    dict(name="E PIANO", cat="KEYS", e=[2, 4, 50, 64, 80, 5, 0, 0], env=[1, 90, 20, 65], fenv=20, mono=0, fx=(0, 50, 25, 45), pat=6),
    dict(name="CLAV", cat="KEYS", e=[1, 3, 30, 64, 62, 35, 0, 0], env=[0, 55, 0, 30], fenv=48, mono=0, fx=(0, 0, 20, 20), pat=6),
    dict(name="ORGAN", cat="KEYS", e=[6, 6, 100, 64, 110, 0, 0, 0], env=[1, 100, 120, 20], fenv=0, mono=0, fx=(0, 60, 0, 40), pat=6),
    dict(name="VIBES", cat="KEYS", e=[3, 12, 80, 64, 120, 0, 0, 0], env=[0, 96, 0, 85], fenv=0, mono=0, fx=(0, 40, 40, 55), pat=7),
    dict(name="SOFT BELL", cat="KEYS", e=[3, 25, 100, 64, 100, 10, 0, 0], env=[0, 100, 0, 95], fenv=10, mono=0, fx=(0, 30, 45, 70), pat=7),
    dict(name="WURLY", cat="KEYS", e=[2, 8, 70, 64, 66, 10, 0, 0], env=[1, 85, 40, 60], fenv=24, mono=0, fx=(5, 55, 20, 35), pat=6),
]

# V2 Phase 0.4: presets 32..49 are placeholders (copies of 0..17, named SLOT 33..50) until Phase 5 voices them;
# 0..31 above keep their numbers (saved projects and Program Changes load the same sound)
PRESETS += placeholder(PRESETS[:18], 32)
