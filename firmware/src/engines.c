/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table: the eight machines OB-Z, WAVE, ACID, DRIFT, JP-Z, CZ-Z, GW-Z, MICRO (V2; order = PRESETS browsing order and the engine numbers
 * of the editor protocol), the factory patterns (SAVE > PHRASES) and the parts' sounds at power-on. */
#include "dsp.c"
#include "category.c"           /* the CAT_* numbers, and (with four_presets.h) every machine's 32 presets and their categories */
_Static_assert(NELEM(OBX_PRESETS) == NPRESET && NELEM(OBX_CAT) == NPRESET, "OBX: 50 presets");
_Static_assert(NELEM(WAVE_PRESETS) == NPRESET && NELEM(WAVE_CAT) == NPRESET, "WAVE: 50 presets");
_Static_assert(NELEM(ACID_PRESETS) == NPRESET && NELEM(ACID_CAT) == NPRESET, "ACID: 50 presets");
_Static_assert(NELEM(DRIFT_PRESETS) == NPRESET && NELEM(DRIFT_CAT) == NPRESET, "DRIFT: 50 presets");
_Static_assert(NELEM(WAVE_PX) == NPRESET, "WAVE: 50 presets (WAVE_PX)");
_Static_assert(NELEM(DRIFT_PX) == NPRESET && NELEM(NANO_PX) == NPRESET, "DRIFT_PX, NANO_PX: 50 presets");
_Static_assert(NELEM(JPZ_PRESETS) == NPRESET && NELEM(JPZ_CAT) == NPRESET, "JPZ: 50 presets");
_Static_assert(NELEM(CZZ_PRESETS) == NPRESET && NELEM(CZZ_CAT) == NPRESET, "CZZ: 50 presets");
_Static_assert(NELEM(GWZ_PRESETS) == NPRESET && NELEM(GWZ_CAT) == NPRESET, "GWZ: 50 presets");
_Static_assert(NELEM(MICRO_PRESETS) == NPRESET && NELEM(MICRO_CAT) == NPRESET, "MICRO: 50 presets");

/* The parts' engine memory (1.5): a part plays one engine at a time, so the engines with per-part state
 * (engine_t.mem: WAVE's voices) keep it in one region per part instead of an array each. The region holds the
 * state of the engine that last claimed it (eng_mem_owner); a switch to another such engine zeroes its
 * engine_t.mem bytes, which is the state each had at power-on (tests/union_test.c). Sized by the largest of the
 * four (after their sources: each defines its <NAME>_MEM); the engines reach it through eng_mem_of (defined
 * below them). Zero-initialised pool section: main.c fm1_cstart clears it. */
static inline void *eng_mem_of(uint32_t p);
#define ENG_MEM(p) eng_mem_of(p)           /* part p's region (p < NPART) */
#define ENG_MEM_PART (VOID_MEM > ACID_MEM ? (VOID_MEM > DRIFT_MEM ? VOID_MEM : DRIFT_MEM) \
                                          : (ACID_MEM > DRIFT_MEM ? ACID_MEM : DRIFT_MEM))
#define ENG_MEM_FITS(name, size) _Static_assert((size) <= ENG_MEM_PART, name " state over ENG_MEM_PART")

#include "eng_obx.c"            /* OB-Z: two oscillators, noise, drive, the filter */
#include "eng_acid.c"           /* ACID: the TB-303 voice (bass303.c, after Open303) */
#include "eng_drift.c"          /* DRIFT: the Juno voice (dsp_va.c, after the Ultramaster KR-106) */
#include "eng_wave.c"           /* WAVE: the VOID WAVE plugin's voice (wavetables: tools/gen_void_tables.py) */
#include "eng_stub.c"           /* V2: JP-Z, CZ-Z, GW-Z, MICRO on a placeholder voice until their phases */
ENG_MEM_FITS("DRIFT", DRIFT_MEM);
ENG_MEM_FITS("ACID", ACID_MEM);    /* (against the region, not against WAVE: ACID owes the pool nothing but its own size; the test pins that the pool did not grow) */

static union {
    uint64_t align;
    uint8_t b[ENG_MEM_PART];
} eng_mem[NPART] __attribute__((section(".pool")));
static uint8_t eng_mem_owner[NPART];       /* engine index + 1 whose state is in the region, 0 = none (zeros) */
static inline void *eng_mem_of(uint32_t p) { return (void *)eng_mem[p].b; }

/* the editor protocol, user presets and projects store these indices */
#ifndef ENGINES_QUAL
#define ENGINES_QUAL const      /* (a host test defines it empty before the include, to swap in a stub engine) */
#endif
static const engine_t *ENGINES_QUAL ENGINES[NENGINES] = {
    &ENG_OBX,                    /* 0 (ENGI_OBX) */
    &ENG_WAVE,                   /* 1 (ENGI_WAVE) */
    &ENG_ACID,                   /* 2 (ENGI_ACID) */
    &ENG_DRIFT,                  /* 3 (ENGI_DRIFT) */
    &ENG_JPZ,                    /* 4 (ENGI_JPZ) */
    &ENG_CZZ,                    /* 5 (ENGI_CZZ) */
    &ENG_GWZ,                    /* 6 (ENGI_GWZ) */
    &ENG_MICRO,                  /* 7 (ENGI_MICRO) */
};

/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* part t's engine memory goes to its engine (t->engine; voice.c engine_block, once a block: on a switch, after
 * the old engine's fade, before any note on the new one): zeroed (its engine_t.mem bytes) when another
 * engine's state is in it. An engine without memory leaves it as it is: a switch back finds its state. */
static void eng_mem_claim(const track_t *t)
{
    uint32_t p = (uint32_t)(t - trk), e, k, n;
    uint32_t *w;
    if (p >= NPART)
        return;
    e = eng_idx(t->engine);
    n = ENGINES[e]->mem;
    if (!n || eng_mem_owner[p] == e + 1u)
        return;
    w = (uint32_t *)ENG_MEM(p);
    for (k = 0; k < (n + 3u) / 4u; k++)
        w[k] = 0;
    eng_mem_owner[p] = (uint8_t)(e + 1u);
}

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the EDIT layer's keys, the editor's list) */
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {0, 1, 2, 3, 4, 5, 6, 7};

/* the engines one can pick, in ENGINE_ORDER: eng_ok(e), the n-th of them eng_vis(n), e's place among them
 * eng_rank(e), the next / previous one eng_step(e, dir) (wraps) */
static int eng_ok(uint32_t e) { return e < NENGINES; }
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % NENG_SHOWN]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    for (n = 0; n < NENG_SHOWN && ENGINE_ORDER[n] != e; n++)
        ;
    return n < NENG_SHOWN ? n : 0u;
}
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    return eng_vis((eng_rank(e % NENGINES) + (dir > 0 ? 1u : NENG_SHOWN - 1u)) % NENG_SHOWN);
}

/* factory sequence patterns: SAVE > PHRASES loads one into the selected track (ui.c pat_load); a preset
 * only suggests one with PAT(n), loading a sound never touches the steps. Absolute notes, loaded as they
 * are (SCL TRANS and OCT transpose): 0 = rest;
 * flags 1 = accent, 2 = slide, 4 = tie (holds the previous note). Names: at most 8 characters */
#define T_ 4
static const struct {
    const char *name;
    uint8_t note[16], flags[16];
} PATTERNS[] = {
    {"ACID", {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50},          /* 1 */
     {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1}},
    {"OFFBEAT", {0, 36, 0, 36, 0, 36, 0, 48, 0, 36, 0, 36, 0, 39, 0, 43},          /* 2 bass */
     {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0}},
    {"MELODY", {60, 0, 67, 0, 72, 67, 0, 64, 62, 0, 69, 0, 74, 69, 0, 67},         /* 3 pluck */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    {"LEAD", {72, 0, 0, 74, 0, 0, 76, 0, 79, 0, 76, 0, 74, 0, 0, 0},               /* 4 */
     {1, T_, 0, 2, T_, 0, 0, 0, 1, 0, 2, 0, 0, T_, T_, 0}},
    {"PAD", {60, 0, 0, 0, 0, 0, 0, 0, 57, 0, 0, 0, 55, 0, 0, 0},                   /* 5 long notes */
     {0, T_, T_, T_, T_, T_, T_, 0, 0, T_, T_, 0, 0, T_, T_, 0}},
    {"KEYS", {0, 0, 60, 0, 0, 63, 0, 0, 0, 0, 60, 0, 0, 65, 0, 63},                /* 6 offbeat stabs */
     {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}},
    {"BELL", {72, 0, 0, 79, 0, 0, 84, 0, 0, 0, 76, 0, 0, 0, 0, 0},                 /* 7 sparse */
     {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"SUB", {36, 0, 0, 0, 0, 0, 0, 36, 0, 0, 34, 0, 0, 0, 0, 0},                   /* 8 low, held */
     {1, T_, T_, T_, 0, 0, 0, 0, 0, 0, 0, T_, T_, T_, 0, 0}},
    /* 16ths up a C minor arpeggio, twice: the line the ARP preset (OB-Z RAVE) suggests
     * now that the arpeggiator is the track's and a preset no longer switches it on */
    {"ARP", {48, 51, 55, 60, 63, 67, 72, 75, 48, 51, 55, 60, 63, 67, 72, 75},       /* 9 */
     {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}},
};
#undef T_
#define NPATTERNS (sizeof PATTERNS / sizeof PATTERNS[0])

/* the parts at power-on (engine, preset, PATTERNS[n - 1] in the sequencer, 0 = empty): the four machines, ACID with
 * the ACID pattern */
static const uint8_t TRK_DEF[NPART][3] = {{ENGI_OBX, 0, 0}, {ENGI_WAVE, 0, 0}, {ENGI_ACID, 0, 1}, {ENGI_DRIFT, 0, 0}};
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
