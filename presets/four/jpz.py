# JP-Z: JP-8000 Super Saw. V2 Phase 0.3: 50 INIT placeholders on the stub voice (eng_stub.c), in the plan's categories
# (LEAD 16, ATMOS 14, KEYS 12, BASS 8); its own phase replaces them with the real presets, voiced by ear (Phase 5).
# e = the engine's edit[] defaults (eng_stub.c ENG_JPZ).
_CATS = [("LEAD", 16), ("ATMOS", 14), ("KEYS", 12), ("BASS", 8)]
PRESETS = []
for _cat, _n in _CATS:
    for _i in range(_n):
        PRESETS.append(dict(name=f"JP-Z INIT {len(PRESETS) + 1}", cat=_cat, e=[64, 64, -64, 0, 100, 20, 64, 0],
                            env=[0, 64, 100, 30], fenv=0, mono=1 if _cat in ("BASS", "LEAD") else 0, fx=(0, 0, 0, 10)))
