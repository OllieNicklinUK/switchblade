# GW-Z: ORGAN / VOX / ADD. V2 Phase 0.3: 50 INIT placeholders on the stub voice (eng_stub.c), in the plan's categories
# (KEYS 22, ATMOS 12, LEAD 10, BASS 6); its own phase replaces them with the real presets, voiced by ear (Phase 5).
# e = the engine's edit[] defaults (eng_stub.c ENG_GWZ).
_CATS = [("KEYS", 22), ("ATMOS", 12), ("LEAD", 10), ("BASS", 6)]
PRESETS = []
for _cat, _n in _CATS:
    for _i in range(_n):
        PRESETS.append(dict(name=f"GW-Z INIT {len(PRESETS) + 1}", cat=_cat, e=[0, 0, 0, 0, 110, 0, 0, 0],
                            env=[0, 64, 100, 30], fenv=0, mono=1 if _cat in ("BASS", "LEAD") else 0, fx=(0, 0, 0, 10)))
