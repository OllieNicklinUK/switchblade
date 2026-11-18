/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA core types: tracks, voices, engines, parameters.
 * Four tracks, each a synth part: its own engine, preset, parameters, voices and 64-step
 * pattern. The parts share one budget of NVOICE sounding voices (voice.c). Four machines: OB-Z, WAVE,
 * ACID, DRIFT (engines.c).
 * Sections: sizes, parameters, voices and engines, tracks and the song, system. */
#include <stdint.h>

/* ------------------------------------------------------------ sizes --- */
#define NVOICE 8                 /* voices per part, and the budget shared by all parts */
#define NPART 4                  /* synth parts: tracks 1..4 */
#define NTRK NPART               /* tracks (the formats and the protocol count these): every track is a part */
#define NSTEP 64
#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms at 44.1 kHz (a key waits 0..1 half, then plays 1 half later) */
#define NENGINES 8u              /* V2: the eight machines (engines.c ENGINES[]); ids 0..7 are stored: never reorder */
#define NENG_SHOWN 8u            /* the engines one can pick, in engines.c ENGINE_ORDER */
#define NPRESET 50u              /* V2: factory presets per machine (0..31 as 0.2-beta shipped them, 32..49 new) */
#define ENGI_OBX 0u              /* OB-Z (eng_obx.c) */
#define ENGI_WAVE 1u             /* WAVE (eng_wave.c, the VOID WAVE plugin's voice) */
#define ENGI_ACID 2u             /* ACID (eng_acid.c) */
#define ENGI_DRIFT 3u            /* DRIFT (eng_drift.c) */
#define ENGI_JPZ 4u              /* V2: JP-Z, the JP-8000 Super Saw (eng_jpz.c) */
#define ENGI_CZZ 5u              /* V2: CZ-Z, Casio phase distortion (eng_cz.c) */
#define ENGI_GWZ 6u              /* V2: GW-Z, ORGAN / VOX / ADD (eng_gwz.c) */
#define ENGI_MICRO 7u            /* V2: MICRO, the Waldorf Microwave I (eng_micro.c) */
#define UP_SLOTS 32u             /* user presets (upreset.c) */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: unused, kept for the formats / protocol */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* SLICER insert (slicer.c) */
    P_M1SRC, P_M1DST, P_M1AMT,                 /* modulation matrix (mod.c): 4 slots of SRC, DST, AMT; */
    P_M2SRC, P_M2DST, P_M2AMT,                 /* new common parameters go just before P_E0 (user presets */
    P_M3SRC, P_M3DST, P_M3AMT,                 /* and projects map by count) */
    P_M4SRC, P_M4DST, P_M4AMT,
    P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL, P_FM1_LEVEL,
    P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL, P_FM2_LEVEL,
    P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL, P_FM3_LEVEL,
    P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL, P_FM4_LEVEL,
    P_CHRD, P_VOIC,                            /* chord keys (chord.c): one key plays a chord; its voicing */
    P_RSV_LN, P_RSV_LN_END = P_RSV_LN + 7,     /* reserved: the retired DRUM lane levels' ids (kept for the formats until
                                                * Task 1.2 rewrites them; nothing reads them) */
    P_LSYNC, P_LTRIG, P_LPOL,                  /* 1.2 (#154): LFO SYNC (OFF, a note value), TRIG (NOTE, FREE), POL (BI, UNI) */
    P_SQNT,                                    /* 1.2 (#162): QUANTIZE, the steps' recorded / nudged timing (ON: none) */
    P_SPRD,                                    /* (#148): SPREAD, the voices of a POLY / UNISON track around PAN (fx.c) */
    P_ITYPE, P_IA, P_IB, P_IC, P_IMIX,         /* 1.5 (#78, #177): the INSERT, its TYPE, three values and MIX (fx.c) */
    P_FTYPE,                                   /* 1.5 (#104): the filter TYPE, LP (as before) / BP / HP (eng_obx.c, eng_wave.c) */
    P_ESYNC,                                   /* 1.5 (#175): ENV SYNC: ATK DEC REL as note values of the tempo (voice.c) */
    P_MOUT,                                    /* FM1-Four: MIDI OUT, MOUT_INT / EXT / BOTH (midi_control.c mout_*) */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_ROUTE, G_INFO,   /* G_ROUTE: MIDI IN, 0 CH1-4 (channels 1..4 -> parts 1..4, 5..16 ignored), 1 SEL;
                                        * 1.2: 2..4 CH5-8 CH9-12 CH13-16 (seq.c midi_base); 1.5.5: 5 TRACK (song.mch) */
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_ENGGO,          /* no page: the editor switches the engine with a SET of G_ENGSEL; G_ENGGO is
                                 * unused (ids are fixed by the formats and the protocol) */
    G_CLRSEQ, G_INITSND,
    G_RTYPE,                    /* REVERB TYPE: 0 ROOM, 1 SPRING, 2 HALL (1.2, the power-on default) (fx.c). Was
                                 * G_DRCH, the GM drum part's MIDI channel (inert since 1.0, never read); */
    G_DRLVL, G_DRREV,           /* inert (label "-", on no page): the GM drum part they set is gone; kept
                                 * because the ids and G_COUNT are fixed by the formats and the protocol (nothing
                                 * reads them) */
    G_CLKOUT,                   /* FM1-Four: MENU > MIDI OUT > CLOCK OUT, OFF / ON (midi_control.c mout_clock) */
    G_COUNT
};

/* stored parameters of an older layout -> today's P_* order. A store keeps np = the P_COUNT it was
 * written with; common parameters are only ever added just before P_E0, so the first np - 8 are
 * P_LEVEL.. in order and the last 8 are P_E0..P_E7; the parameters a store does not have take def[]
 * (user presets, projects of formats 1 and 2) */
static void params_by_count(int16_t *out, const int16_t *in, uint32_t np, const int16_t *def)
{
    uint32_t i, nc = np - 8u;
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? in[i] : def[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = in[nc + i];
}

/* ------------------------------------------------- voices, engines --- */
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
enum { MOUT_INT, MOUT_EXT, MOUT_BOTH };         /* P_MOUT: the machine only, external gear only, both */
typedef struct {
    uint8_t note, vel, gate, active;
    uint8_t stage;               /* env: 0 off, 1 attack, 2 decay/sustain, 3 release */
    uint8_t side;                /* SPREAD (fx.c mix_spread): 0 left of PAN, 1 right; set as the voice starts */
    int32_t env;                 /* Q24 */
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    int32_t gstep;               /* glide TIME mode: 1/16 st per control tick, 0 = RATE mode */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    uint32_t ph[3];
    int32_t s[8];                /* engine state (filters, envs) */
    uint32_t age;
    uint8_t mvel;                /* mod.c: the note-on velocity (before UNISON scaling) and RAND of the note */
    int16_t mrnd;
} voice_t;

typedef struct {                 /* per-voice control-rate modulation, computed in voice.c */
    uint32_t inc;                /* phase increment of the base pitch */
    int32_t pitch16;
    int32_t amp0, amp1;          /* Q15 ramp over the block */
    int32_t cutoff;              /* 0..127 << 8 */
    int32_t shape;               /* 0..127 << 8 */
    int32_t envq15;              /* env value (for engines that use it as a mod source) */
    int32_t fine;                /* the residual below 1/16 semitone in inc: unison detune, TUNE, bend (1/4096) */
} vmod_t;

typedef struct {
    const char *name;
    int8_t e[8];                 /* P_E0..P_E7 (signed: an interval below the note; every value fits) */
    uint8_t env[4];              /* ATK DEC SUS REL */
    int8_t fenv;                 /* ENV -> FILTER amount (-64..63) */
    uint8_t mono;                /* 1 = MONO (bass / lead), 0 = POLY */
    /* the rest of the patch; each value is stored + 1, 0 = the default */
    uint8_t fx[4];               /* DIST, CHORUS, DELAY, REVERB sends */
    uint8_t pat;                 /* suggested pattern (PATTERNS[pat - 1], 0 = none): only a hint. A sound load
                                  * never touches the steps; SEQ > PATTERNS loads a pattern (ui.c pat_load). The
                                  * arp is the track's too: a preset does not set it */
} preset_t;
typedef struct {                 /* WAVE's extras of a factory preset (WAVE_PX, four_presets.h; void_track_loaded) */
    uint8_t ftype;               /* P_FTYPE (VF_*) */
    uint8_t voice;               /* 3: UNISON (else the preset's mono flag: POLY / LEGATO) */
    uint8_t detune;              /* P_DETUNE + 1 (0: the default) */
    uint8_t spread;              /* P_SPRD */
    uint8_t glide;               /* P_GLIDE */
    uint8_t lfo[5];              /* 1: the LFO's RATE, WAVE, TRIG, FADE below (0: the defaults) */
    uint8_t fe[5];               /* FLT ENV: P_FM1_LEVEL (0 off, 1..127 on: its velocity sensitivity), A D S R */
    int8_t mx[4][3];             /* the matrix: SRC, DST, AMT per slot */
    int8_t ldflt;                /* P_LD_FLT: the LFO's own depth on the filter (LFO DEST FLT, -64..63) */
    uint8_t lsync;               /* P_LSYNC: LFO SYNC (0 OFF, else a note value of the tempo, params.c N_LSYNC) */
} void_px_t;
#define FX(d, c, dl, r) .fx = {(d) + 1, (c) + 1, (dl) + 1, (r) + 1}
#define PAT(n) .pat = (n)

struct track;
typedef struct {                 /* an engine (engines.c ENGINES[]; the eng_*.c files) */
    const char *name;            /* "OB-Z" (PRESETS, the editor) */
    const char *page_title[2];   /* EDIT 1 and EDIT 2 */
    param_desc_t edit[8];        /* P_E0..P_E7 */
    const preset_t *presets;
    uint8_t npresets;
    uint8_t knob[4];             /* HOME: the four parameters on KNOB 1..4 */
    uint8_t poly;                /* voice cap for POLY and UNISON, 0 = NVOICE */
    uint8_t keep;                /* bit k: s[k] is kept when a sounding voice is retriggered (filters) */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    /* optional: a mode-dependent descriptor of EDIT k (the same range and default as edit[k],
     * another label / value names), 0 = edit[k] */
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    /* bytes of the part's engine memory (engines.c eng_mem, at most ENG_MEM_PART) the engine keeps its state
     * in, 0 = none: a part plays one engine at a time, so they share one region; on a switch to the engine
     * (after the old one's fade) the region is zeroed when another engine had it (eng_mem_claim) */
    uint16_t mem;
    /* own envelope (a 303's MEG/VEG is the instrument): 1 = the engine runs its own amp envelope; voice.c applies no
     * ADSR gain (no velocity scaling either: amp is the whole level) and does not decide when the voice ends */
    uint8_t ownenv;
    int32_t (*amp)(const struct track *t, const voice_t *v);    /* Q15, control rate (ownenv engines only) */
    int (*done)(const struct track *t, const voice_t *v);       /* 1 = the voice may be freed (ownenv engines only) */
    /* optional: the part's own stereo stage (DRIFT's Juno chorus, Task 6.2). fx.c mix_part calls it once a block with the part's mono block b (after
     * DIST, INSERT, the SLICER and the mute, at the point where the part becomes stereo) and a scratch z: it returns 0 and leaves both alone, or 1
     * with b := the mid (L + R) / 2 and z := the side (L - R) / 2 of the pair (same scale). mix_stereo (fx.c) places them with PAN and SPREAD. NULL
     * (OB-Z, WAVE, ACID): the part's path is the same code as without it. The output must stay inside the mixer's integer limits (|L|, |R| <= 5 full
     * scales: task-7.2a-review.md) */
    int (*part_fx)(struct track *t, int32_t *b, int32_t *z, uint32_t n);
    /* optional (2026-10-21, NANO's two models): per track, whether the engine runs its own envelope (else .ownenv) and its voice cap (else .poly) */
    int (*ownenv_of)(const struct track *t);
    uint8_t (*poly_of)(const struct track *t);
    /* optional (V2, GW-Z after Quantum GammaWave): once a block, before the part's voices render (voice.c track_render):
     * per-part values the voices share (an organ's bar gains and rotors, a spectrum's partial gains). NULL: none */
    void (*block)(struct track *t);
} engine_t;

/* ------------------------------------------------- tracks, the song --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
#define SF_RATCH_SH 3u                   /* RATCH: the hits of a NOTE step - 1 (0..3: x1..x4, seq.c seq_ratchet); */
#define SF_RATCH (3u << SF_RATCH_SH)     /* bits 3..4, so a user preset's pattern (flag 4 = tie) carries it too */
typedef struct {                 /* acid-style step: up to 4 notes (POLY), time, accent, slide */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE | SF_RATCH */
    uint8_t vel;
    uint8_t hit, acc;            /* the retired DRUM grid's hits: kept in the struct, never read or stored */
    uint8_t probability;         /* 0 = legacy 100%; 1..100 = percent, 101 = silent; 1.5.5: 102..112 a condition */
} step_t;

/* Probability keeps zero-initialized and legacy patterns at 100%. */
/* 1.5.5 (#209): CONDITIONS, in the chance byte's free values (no format change): COND_FIRST + c - 1 is condition c
 * (1..NCOND): a:b (1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4) plays on loop a of every b of its track (seq.c seq_cond), FILL
 * while FILL is held (GLO + E4), !FILL while not. A step has a chance or a condition (one byte): a condition plays at
 * 100 %. Firmware before 1.5.5 refuses a project holding one (its proj_unpack wants 0..101): the slot reads empty there.
 * The knobs and the lists show the two as one value (step_chance_v): 0..100 %, then 101..100 + NCOND the conditions */
#define COND_FIRST 102u
#define NCOND 11u
#define COND_FILL 10u                    /* FILL, NOT FILL: the last two */
#define COND_NFILL 11u
#define CHANCE_V_MAX (100u + NCOND)
static uint32_t step_cond(const step_t *s)   /* its condition 1..NCOND, 0 none */
{
    return s->probability >= COND_FIRST && s->probability < COND_FIRST + NCOND ? s->probability - COND_FIRST + 1u : 0u;
}
static uint32_t step_chance(const step_t *s)
{
    return !s->probability ? 100u : s->probability <= 100u ? s->probability : s->probability == 101u ? 0u : 100u;
}
static void step_set_chance(step_t *s, uint32_t chance) { s->probability = (uint8_t)(chance >= 100u ? 0u : chance ? chance : 101u); }
static uint32_t step_chance_v(const step_t *s) { uint32_t c = step_cond(s); return c ? 100u + c : step_chance(s); }
static void step_set_chance_v(step_t *s, uint32_t v)
{
    if (v > 100u)
        s->probability = (uint8_t)(COND_FIRST - 101u + (v < CHANCE_V_MAX ? v : CHANCE_V_MAX));
    else
        step_set_chance(s, v);
}
/* RATCH: a NOTE step plays its notes and hits this many times (1..4), in equal parts of the step; every older
 * pattern holds 0 there, x1 */
static uint32_t step_ratchet(const step_t *s) { return ((s->flags & SF_RATCH) >> SF_RATCH_SH) + 1u; }
static void step_set_ratchet(step_t *s, uint32_t hits)
{
    s->flags = (uint8_t)((s->flags & ~SF_RATCH) | ((hits < 1u ? 0u : hits > 4u ? 3u : hits - 1u) << SF_RATCH_SH));
}
/* NUDGE (1.2, #162, D98): a step's timing, in 1/NUDGE_DIV of the track's DIV, -8..+7 (half a step early .. 7/16 late);
 * live recording keeps it, SEQ > AUTOMATION's NUDGE rows and STEP (steps held + PRESETS) set it, the track's QUANTIZE
 * OFF (P_SQNT, AUTOMATION's QUANTIZE row) plays it (seq.c seq_tick).
 * Four bits of the flags no other field uses: 4 (bit 3 of the nudge, its sign) and 32 64 128 (bits 0..2). 0 = on the
 * step: every older pattern. A user preset's pattern keeps none (upreset.c up_pat_norm) */
#define NUDGE_DIV 16
#define SF_NUDGE (4u | 0xE0u)
static int32_t step_nudge(const step_t *s)
{
    uint32_t u = (uint32_t)(s->flags >> 5) | (uint32_t)(s->flags & 4u) << 1;
    return (int32_t)(u ^ 8u) - 8;
}
static void step_set_nudge(step_t *s, int32_t v)
{
    uint32_t u = (uint32_t)(v < -8 ? -8 : v > 7 ? 7 : v) & 15u;
    s->flags = (uint8_t)((s->flags & ~SF_NUDGE) | (u & 7u) << 5 | (u >> 1 & 4u));
}
#define MOTION_MAX 128u          /* 1.2 (FUN10): 64 before */
/* Four tracks x64 steps fit one byte. Values retain their signed parameter range (-64..127: an int8_t, 1.2).
 * param: the P_* id (< 128: P_COUNT is at most 127, project.c), bit 7 (MOTION_LOCK, 1.1) a parameter lock: the value
 * sounds on that step only and goes back after it (motion.c motion_step); without it an automation event, the
 * value holds until another one changes it. One record per (track, step, id), of either kind */
#define MOTION_LOCK 0x80u
#define MOTION_ID(e) ((uint32_t)(e)->param & 0x7Fu)
typedef struct { uint8_t place, param; int8_t value; } motion_event_t;
typedef struct { uint8_t count, on, rsv[2]; motion_event_t event[MOTION_MAX]; } motion_store_t;
_Static_assert(sizeof(motion_store_t) == 388u, "motion disk layout (FUN10)");
#define CHAIN_ROWS 16u
/* A song section (1.2, FUN10): the PROJECT slot each track plays (0..3, A..D; CHAIN_SILENT "-": that track plays
 * nothing there) and its repeats (1..16). */
#define CHAIN_SILENT 4u
typedef struct { uint8_t slot[NTRK], repeat; } chain_row_t;
typedef struct {
    uint8_t count, rsv[3];
    chain_row_t row[CHAIN_ROWS];
} chain_config_t;
_Static_assert(sizeof(chain_config_t) == 84u, "song chain layout (FOR4)");

typedef struct {                 /* DIST's state (fx.c track_dist, the drive of fm1_drive_1.h): all zero = fresh (on == 0) */
    int32_t dcx, dcy, lp;        /* the DC blocker's last input and output, the tone filter */
    int32_t ds;                  /* the smoothed macro, P_DIST in Q8 */
    int32_t gain, level;         /* the gain (Q8.8) and the make-up (Q12) the last block ended on */
    uint8_t on;
} dist_st_t;
typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;      /* engine: what the audio ISR renders */
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    uint8_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    int16_t lfo_prev;            /* the S&H value of the cycle before (mod.c SLEW glides from it to lfo_rnd's) */
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;            /* keys physically held for the arp */
    uint8_t latched;             /* HOLD: keep notes after release */
    /* arp runtime */
    uint32_t arp_pos;            /* q8 samples into the current arp step */
    uint32_t arp_idx;
    uint8_t arp_note;            /* sounding arp note, 0 = none */
    uint8_t arp_walk;            /* WALK: the place in the note list it stands on */
    uint8_t arp_ch[3];           /* CHORD: the other notes sounding with arp_note, 0 = none */
    uint32_t arp_off;            /* q8 sample time of its note-off */
    /* sequencer */
    step_t step[NSTEP];
    uint32_t seq_pos;            /* q8 samples into the current step */
    uint16_t seq_idx;
    uint8_t seq_notes[4];        /* sounding seq notes (the step's notes) */
    uint8_t seq_n;
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t slide_glide;         /* next legato note glides (slide) */
    uint8_t rat_left;            /* RATCH: repeats of the playing step still to come (seq.c seq_ratchet) */
    uint32_t seq_off;
    uint8_t seq_active;          /* any step programmed */
    uint8_t rskip_idx;           /* live recording put notes into the step about to play: */
    uint8_t rskip_n, rskip[4];   /* do not trigger them again there (they sound already) */
    /* NUDGE (QUANTIZE OFF, seq.c seq_tick): a step plays at its start + its nudge. Latched as a step starts: */
    uint32_t nd_late;            /* the step playing now plays at this seq_pos + 1 (0: it played) */
    uint32_t nd_early;           /* the next step plays early, at this seq_pos + 1 of this one (0: on its start) */
    uint8_t nd_fired;            /* that step + 1 once it played early (its start then plays nothing), 0 = none */
    /* live recording of held notes (seq.c rec_hold): the steps they are held into become TIEs */
    uint8_t rh_n, rh_note[4];    /* recorded notes still held, 0 = none */
    uint8_t rh_start;            /* the step they were recorded into */
    uint8_t rh_ties;             /* TIE steps written after it */
    uint8_t rh_last;             /* the last of them; rh_bak: what it held (an early release puts it back) */
    step_t rh_bak;
    /* mono */
    uint8_t mono_stack[8];
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t peak;
    int32_t dist_hp;             /* DIST (fx.c): the 55 Hz low cut's state */
    dist_st_t dst;               /* .. and the Scream-style clipper's (float: filters, feedback, gate, smoothed macro, gains) */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST / INSERT tail) */
    uint8_t ins_run;             /* the INSERT still sounds (fx.c track_insert: its wet share above 0) */
    uint8_t cond_loop;           /* 1.5.5 (#209): the loop of its LEN playing (0..COND_LOOPS - 1, seq.c seq_tick) */
    uint8_t rsv_sz[6];           /* (sizeof(track_t) a multiple of 16: below) */
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[8];           /* P_E0..P_E7 of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
    /* modulation matrix (mod.c): MIDI performance controllers of the track's channel, 0..127 */
    uint8_t mw, at;              /* CC1 mod wheel, channel aftertouch */
    uint8_t ex_off;              /* 127 - CC11 expression (0: full, the MIDI default) */
    uint8_t m_vel, m_key, m_vi;  /* the latest note-on: velocity, note, voice index (per-block destinations) */
    uint8_t sp_alt;              /* SPREAD: the side the next voice starts on (alternating, voice.c voice_start) */
    int16_t m_rnd;               /* .. its RAND */
    int32_t m_env;               /* the amp envelope of voice m_vi, last block (Q15) */
} track_t;
/* the audio ISR indexes trk[] with a multiply by a small immediate and a shift while the size is a multiple of 16; 1.5's
 * new track parameters (the INSERT, the filter TYPE, ENV SYNC) and state would make it no multiple (the constant
 * loaded first: +3 % on the ISR's static cost, target_budget.py): one pad, rsv_sz, keeps it */
_Static_assert(sizeof(track_t) % 16u == 0, "track_t: a multiple of 16 bytes (a cheap trk[] index on the target)");

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t grid;                /* the keys' mode: 0 they play; 2: NAME (ui_name.c): no key plays; 3: SONG, A..D are
                                  * its (seq.c song_key). (1 was the retired DRUM grid) */
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
    uint8_t mch[NTRK];           /* 1.5.5 (#202): MIDI IN TRACK (G_ROUTE ROUTE_TRACK), each track's channel as stored: 0..15
                                  * the channel's distance up from the track's own (track i: channel (i + v) % 16, so 0, the
                                  * zeroed default, is T1..T4 on channels 1..4), MCH_OMNI (any channel no track has), MCH_OFF
                                  * (none); mch_get / mch_set. The project's, in FUN10's spare bytes (project.c PROJ_MIDI_OFF);
                                  * MENU > MIDI > T1..T4 CHANNEL */
} song_t;
#define ROUTE_SEL 1
#define ROUTE_TRACK 5                /* G_ROUTE: TRACK, each track on its own channel (song.mch) */
#define MCH_OMNI 16u
#define MCH_OFF 17u
#define MCH_N 18u                    /* channels 1..16, OMNI, OFF */

static track_t trk[NTRK];        /* the instrument: four parts */
static song_t song;
#define TSEL (&trk[song.sel])    /* the selected track */
/* MIDI IN TRACK: track i's channel 0..15, MCH_OMNI or MCH_OFF (song.mch), and set */
static inline uint32_t mch_get(uint32_t i)
{
    uint32_t v = song.mch[i % NTRK];
    return v < 16u ? (v + i) & 15u : v < MCH_N ? v : (i & 15u);
}
static inline void mch_set(uint32_t i, uint32_t ch) { song.mch[i % NTRK] = (uint8_t)(ch < 16u ? (ch - i) & 15u : ch < MCH_N ? ch : 0u); }

/* SWING of a track's step clock: the track's own plus the global one, at most 100 (#31). The sequencer
 * (seq.c step_samples) and the SLICER (slicer.c sl_enter) both time steps with it. At 100 the even
 * steps are 1.4 x the straight length and the odd ones 0.6 x. */
#define SWING_MAX 100
static inline int32_t track_swing(const track_t *t)
{
    int32_t sw = t->p[P_SSWING] + song.g[G_SWING];
    return sw < 0 ? 0 : sw > SWING_MAX ? SWING_MAX : sw;
}
/* the length of step idx of a straight length `base`, swung: even steps longer, odd ones shorter */
static inline uint32_t swing_step_len(const track_t *t, uint32_t base, uint32_t idx)
{
    int32_t sw = track_swing(t) * (int32_t)base / 250;
    return base + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* ----------------------------------------------------------- system --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
