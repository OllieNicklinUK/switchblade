# MICRO: Waldorf Microwave I. V2 Phase 0.3: 50 INIT placeholders on the stub voice (eng_stub.c), in the plan's categories
# (ATMOS 18, KEYS 12, LEAD 10, BASS 10); its own phase replaces them with the real presets, voiced by ear (Phase 5).
# e = the engine's edit[] defaults (eng_stub.c ENG_MICRO).
_CATS = [("ATMOS", 18), ("KEYS", 12), ("LEAD", 10), ("BASS", 10)]
PRESETS = []
for _cat, _n in _CATS:
    for _i in range(_n):
        PRESETS.append(dict(name=f"MW INIT {len(PRESETS) + 1}", cat=_cat, e=[0, 0, 32, 0, 90, 20, 32, 64],
                            env=[0, 64, 100, 30], fenv=0, mono=1 if _cat in ("BASS", "LEAD") else 0, fx=(0, 0, 0, 10)))
