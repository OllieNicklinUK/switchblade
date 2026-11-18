/* SPDX-License-Identifier: GPL-3.0-only */
/* V2 Phase 0.3: the four new machines' placeholders. JP-Z (4), CZ-Z (5), GW-Z (6) and MICRO (7) each have their final id,
 * name, EDIT contract (labels, ranges, defaults, HOME knobs) and presets, and they all play this one voice until their DSP
 * lands (Phases 1-4): a naive saw through a one-pole low-pass on CUT (+ the shared filter modulation), amplitude from
 * voice.c. RES and the other slots do nothing yet. So the UI, storage, MIDI, bank select and CPU checks can be built and
 * tested against all eight machines before any of the new DSP exists (`2026-11-17-eight-machines.md` §3.1).
 * Each machine's own source replaces its engine_t here when its phase starts; the ids and the edit[] contract stay. */

/* the cutoff (CUT << 8 + modulation, 0 .. 127 << 8) -> a one-pole coefficient, Q15: ~30 Hz .. ~15 kHz, exponential */
static inline int32_t stub_k(int32_t cut)
{
    int32_t c = clamp(cut, 0, 127 << 8);
    /* 2^(c / 16 / 256) steps of the semitone-ish table: 30 << (c / (16 << 8)) with a linear in-between, then clamp */
    int32_t oct = c >> 12, fr = c & 4095;
    int32_t k = (40 << oct) + (((40 << oct) * fr) >> 12);
    return clamp(k, 40, 30000);
}

static void stub_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[0] = 0;                                         /* the low-pass */
}

static void stub_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    int32_t k = stub_k((t->p[P_E4] << 8) + m->cutoff), lp = v->s[0];
    uint32_t i, ph = v->ph[0], inc = m->inc;
    for (i = 0; i < n; i++) {
        int32_t s = (int32_t)(ph >> 16) - 32768;         /* a naive saw, Q15 */
        ph += inc;
        lp += mulq15(s - lp, k);
        out[i] += voice_amp(lp, m, i);
    }
    v->ph[0] = ph;
    v->s[0] = lp;
}

/* (CZ-Z: its own voice since Phase 1, eng_cz.c; JP-Z since Phase 3, eng_jpz.c) */

static const engine_t ENG_GWZ = {                        /* ORGAN / VOX / ADD, after Quantum GammaWave (the plan §6); ORGAN's slots */
    .name = "GW-Z", .page_title = {"VOICE", "TONE"},
    .edit = {
        {"REG", F_INT, 0, 15, 0, 0, 0},             {"SUB", F_BIPCT, -64, 63, 0, 0, 0},
        {"BODY", F_BIPCT, -64, 63, 0, 0, 0},        {"TOP", F_BIPCT, -64, 63, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 110, 0, 0},       {"RES", F_PCT, 0, 127, 0, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},            {"ROTR", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = GWZ_PRESETS, .npresets = NELEM(GWZ_PRESETS),   /* presets/four/gwz.py */
    .knob = {P_E4, P_E5, P_ED_FLT, P_E7},
    .note_on = stub_note_on, .render = stub_render,
};

static const engine_t ENG_MICRO = {                      /* Waldorf Microwave I (the plan §7) */
    .name = "MICRO", .page_title = {"WAVE", "FLT"},
    .edit = {
        {"WT", F_INT, 0, 31, 0, 0, 0},              {"POS", F_PCT, 0, 127, 0, 0, 0},
        {"EPOS", F_BIPCT, -64, 63, 32, 0, 0},       {"OSC2", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"GRIT", F_PCT, 0, 127, 32, 0, 0},          {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = MICRO_PRESETS, .npresets = NELEM(MICRO_PRESETS),   /* presets/four/micro.py */
    .knob = {P_E4, P_E5, P_ED_FLT, P_E2},
    .note_on = stub_note_on, .render = stub_render,
};
