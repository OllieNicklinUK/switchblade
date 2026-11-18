# GW-Z: ORGAN / VOX / ADD (eng_gwz.c, V2 Phase 2). Most are Quantum GammaWave's own WHEEL, VOICE and ADDTV presets
# (GPL-3.0-only, as Switch Blade), moved onto GW-Z's slots by the converters below; QG's extra modulation (LFO, matrix,
# CHARACTER) is not carried. Phase 5 voices them by ear with the user.
# ORGAN e = [REG, SUB, BODY, TOP, CUT, RES, PERC, ROTR]; VOX e = [BUZZ, VOWL2, TALK, RAND, SHIFT, Q, BREATH, VOWEL];
# ADD e = [SUB, TILT, DECAY, STRETCH, CUT, RES, DRIVE, SPECTR]. Every slot 0..127 (eng_gwz.c).

PERC = {"OFF": 0, "2ND": 1, "3RD": 2, "2SOFT": 3, "3SOFT": 4, "2SLOW": 5, "3SLOW": 6}
ROTR = (0, 8, 127)                                            # OFF, SLOW, FAST (GW-Z: a continuous speed, OFF below 8)


def organ(name, cat, reg, sub, body, top, perc, rot, env, fx, click=40, cut=110, res=0, mono=0):
    """QG WHEEL: REG 0..15, SUB / BODY / TOP -8..8 bars, PERC 0..6, ROTR 0..2"""
    off = lambda d: max(0, min(127, 64 + d * 8))
    return dict(name=name, cat=cat, model="ORGAN", click=click, mono=mono, fenv=0, env=env, fx=fx,
                e=[reg * 8 + 4, off(sub), off(body), off(top), cut, res, perc * 16 + 8, ROTR[rot]])


def vox(name, cat, vowel, vowel2, talk, shift, buzz, breath, q, rand, env, fx, mono=0, fenv=0):
    """QG VOICE: VOWEL VOWEL2 TALK SHIFT(-12..12) BUZZ BREATH Q RAND"""
    sh = max(0, min(127, 64 + shift * 64 // 12))
    return dict(name=name, cat=cat, model="VOX", mono=mono, fenv=fenv, env=env, fx=fx,
                e=[buzz, vowel2, talk, rand, sh, q, breath, vowel])


def add(name, cat, spectr, tilt, decay, stretch, cut, res, drive, sub, env, fx, mono=0, fenv=0):
    """QG ADDTV: SPECTRUM TILT DECAY STRETCH CUT RES DRIVE SUB"""
    return dict(name=name, cat=cat, model="ADD", mono=mono, fenv=fenv, env=env, fx=fx,
                e=[sub, tilt, decay, stretch, cut, res, drive, spectr])


PRESETS = [
    # KEYS (22): ORGAN 14, ADD 8
    organ("FULL ORGAN", "KEYS", 15, 0, 0, 0, 0, 1, [0, 64, 127, 45], (0, 28, 8, 30), click=30),
    organ("JAZZ PERC", "KEYS", 4, 0, 0, 0, 2, 1, [0, 64, 127, 40], (0, 20, 4, 22), click=50),
    organ("GOSPEL", "KEYS", 6, 0, 0, 0, 1, 2, [0, 64, 127, 45], (6, 32, 4, 38), click=60),
    organ("SOFT FLUTE", "KEYS", 1, 0, -2, 0, 0, 1, [0, 64, 127, 55], (0, 22, 16, 54), click=10),
    organ("ROCK DRIVE", "KEYS", 7, 0, 0, 0, 0, 2, [0, 64, 127, 40], (38, 0, 10, 22), click=70),
    organ("BAKELITE", "KEYS", 11, 4, 2, -2, 3, 2, [2, 70, 120, 50], (44, 18, 30, 42), click=65),
    organ("QUANTUM DRAW", "KEYS", 9, 2, 1, 1, 2, 2, [0, 64, 127, 45], (12, 20, 16, 38), click=40),
    organ("NORDIC ORGAN", "KEYS", 7, 3, 0, 1, 1, 1, [15, 64, 127, 65], (0, 34, 28, 68), click=30),
    organ("TONE CABINET", "KEYS", 6, 1, 2, 1, 1, 1, [0, 72, 100, 40], (0, 40, 10, 34), click=35),
    organ("SALT REED", "KEYS", 3, 2, -1, 2, 2, 1, [8, 78, 92, 56], (0, 30, 18, 52), click=25),
    organ("CLARINET ORG", "KEYS", 9, 0, 0, 0, 0, 0, [0, 64, 127, 40], (0, 20, 10, 30), click=20, cut=90),
    organ("BLUES BARS", "KEYS", 5, 0, 0, 0, 2, 2, [0, 64, 127, 40], (10, 16, 8, 26), click=55),
    organ("CHAPEL KEYS", "KEYS", 12, 0, 0, 0, 0, 1, [0, 64, 127, 60], (0, 30, 20, 60), click=10, cut=96),
    organ("BRIGHT BARS", "KEYS", 13, 0, 0, 2, 1, 1, [0, 64, 127, 45], (0, 24, 12, 34), click=45),
    add("ADDTV ORGAN", "KEYS", 0, 64, 0, 0, 108, 10, 0, 0, [0, 64, 127, 45], (0, 26, 8, 28)),
    add("GLASS BELL", "KEYS", 55, 78, 78, 42, 118, 14, 0, 0, [0, 72, 24, 66], (0, 30, 26, 62), fenv=8),
    add("CRYSTAL PLK", "KEYS", 44, 90, 70, 20, 110, 26, 6, 0, [0, 76, 0, 40], (8, 20, 40, 34), fenv=18),
    add("TINE EP", "KEYS", 24, 66, 58, 0, 100, 8, 2, 0, [0, 66, 40, 44], (0, 16, 22, 32), fenv=20),
    add("GONG DEEP", "KEYS", 62, 40, 48, 96, 92, 18, 10, 24, [0, 84, 12, 96], (6, 34, 30, 88), fenv=6),
    add("METAL CLUST", "KEYS", 127, 96, 64, 74, 114, 40, 30, 0, [2, 70, 30, 58], (24, 12, 28, 44), fenv=22),
    add("MUSIC BOX", "KEYS", 50, 84, 84, 10, 120, 6, 0, 0, [0, 60, 0, 50], (0, 20, 30, 50)),
    add("CELESTA", "KEYS", 40, 70, 66, 30, 112, 10, 0, 0, [0, 70, 10, 60], (0, 24, 24, 56)),
    # ATMOS (12): VOX and ADD pads
    vox("CHOIR AAH", "ATMOS", 0, 0, 0, 0, 40, 22, 60, 0, [85, 90, 115, 95], (0, 66, 18, 88)),
    vox("WHISPER", "ATMOS", 0, 95, 88, 3, 50, 120, 50, 0, [50, 90, 110, 90], (0, 42, 32, 68)),
    vox("BLACK CHOIR", "ATMOS", 18, 78, 42, -5, 76, 35, 74, 24, [70, 95, 118, 100], (14, 62, 30, 90)),
    vox("SAINT GLOW", "ATMOS", 12, 48, 28, 3, 45, 50, 62, 12, [82, 92, 115, 98], (0, 72, 40, 92)),
    vox("SOFT CHOIR", "ATMOS", 10, 40, 0, 0, 30, 40, 50, 20, [90, 90, 120, 100], (0, 80, 25, 95)),
    vox("ANGEL AIR", "ATMOS", 60, 90, 20, 12, 20, 80, 60, 30, [95, 90, 120, 105], (0, 85, 35, 100)),
    vox("MONK DRONE", "ATMOS", 5, 10, 10, -5, 60, 20, 70, 10, [90, 90, 125, 110], (0, 58, 20, 92)),
    vox("VOCODE PAD", "ATMOS", 30, 70, 40, 0, 70, 30, 65, 20, [70, 90, 115, 95], (0, 75, 30, 90)),
    vox("TIBETAN VOX", "ATMOS", 5, 25, 15, -6, 75, 15, 80, 20, [95, 90, 125, 110], (0, 48, 30, 92)),
    add("SPECTRAL PAD", "ATMOS", 14, 54, 8, 12, 82, 24, 4, 12, [52, 88, 120, 92], (0, 52, 32, 84)),
    add("AIR CHOIR", "ATMOS", 26, 30, 10, 74, 96, 12, 0, 0, [70, 92, 116, 96], (0, 58, 36, 80)),
    organ("DRONE ORGAN", "ATMOS", 0, 4, 2, -4, 0, 1, [60, 64, 127, 90], (0, 38, 20, 86), click=0),
    # LEAD (10): VOX, ORGAN
    vox("VOX LEAD", "LEAD", 32, 0, 0, 2, 90, 8, 72, 0, [6, 70, 105, 55], (6, 22, 48, 38), mono=1),
    vox("ROBO TALK", "LEAD", 40, 100, 90, 0, 110, 10, 80, 0, [0, 70, 100, 35], (20, 0, 38, 22), mono=1),
    vox("GHOUL RIOT", "LEAD", 64, 120, 96, -4, 112, 40, 88, 55, [4, 72, 90, 44], (52, 24, 34, 32), mono=1),
    vox("ETHNIC CALL", "LEAD", 45, 85, 60, 4, 90, 25, 70, 15, [20, 75, 105, 60], (0, 34, 36, 66), mono=1),
    vox("TALK BOX", "LEAD", 40, 96, 72, 4, 96, 8, 80, 20, [0, 78, 54, 40], (22, 0, 36, 22), mono=1),
    vox("GURDY WHEEL", "LEAD", 15, 45, 30, -2, 85, 30, 75, 10, [10, 70, 120, 50], (18, 14, 22, 32), mono=1),
    organ("HURDY GURDY", "LEAD", 10, 4, 3, -2, 1, 0, [5, 60, 127, 40], (16, 10, 16, 28), click=85, mono=1),
    organ("VIELLE", "LEAD", 6, 3, 2, -1, 0, 0, [10, 70, 120, 50], (10, 14, 16, 44), click=40, mono=1),
    organ("ORGAN SOLO", "LEAD", 10, 0, 0, 2, 2, 2, [0, 64, 127, 40], (14, 10, 24, 30), click=50, mono=1),
    add("REED HORN", "LEAD", 8, 92, 20, 30, 76, 44, 18, 20, [18, 80, 100, 50], (0, 18, 24, 30), mono=1, fenv=35),
    # BASS (6): ORGAN
    organ("ORGAN BASS", "BASS", 14, 0, 0, 0, 0, 0, [0, 64, 127, 30], (0, 0, 0, 8), click=40, cut=80, mono=1),
    organ("PEDAL 16", "BASS", 2, 2, 0, -8, 0, 0, [0, 64, 127, 30], (0, 0, 0, 10), click=20, cut=70, mono=1),
    organ("PERC BASS", "BASS", 14, 0, 0, 0, 1, 0, [0, 60, 100, 25], (0, 0, 0, 6), click=60, cut=90, mono=1),
    organ("ROTOR BASS", "BASS", 14, 2, 0, -4, 0, 1, [0, 64, 127, 30], (0, 0, 0, 8), click=30, cut=80, mono=1),
    organ("DARK PEDAL", "BASS", 0, 4, -2, -8, 0, 0, [0, 64, 127, 30], (0, 0, 0, 8), click=10, cut=60, mono=1),
    organ("GROWL BASS", "BASS", 4, 2, 2, 0, 0, 0, [0, 64, 127, 30], (30, 0, 0, 6), click=50, cut=100, mono=1),
]
