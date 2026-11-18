/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (the three engines), Guillel51 / Quantum GammaWave
 * (their QG-1 versions), Switch Blade (this port)
 *
 * GW-Z: three engines of Quantum GammaWave (github.com/Guillel51/Quantum-GammaWave, GPL-3.0-only; Felucca's own lineage)
 * as the three models of one machine (the plan 2026-11-17-eight-machines.md §6; V2 Phase 2), picked by the preset (GW_PX,
 * kept in the part's P_RSV_LN + 2 as NANO keeps its model in + 1):
 *   ORGAN  QG's WHEEL (eng_wheel.c): nine free-running drawbar sines, 16 registrations and SUB / BODY / TOP, single-trigger
 *          PERC, key CLICK, the two-rotor speaker folded into the per-block gains;
 *   VOX    QG's VOICE (eng_formant.c): a Rosenberg glottal source into a Klatt cascade of four resonators, two vowels and
 *          TALK between them, BUZZ, BREATH, RAND;
 *   ADD    QG's ADDTV (eng_additive.c): eight partials, SPECTRUM over eight profiles, TILT, spectral DECAY, STRETCH, SUB,
 *          its 2-pole filter and drive.
 * Their DSP is QG's, line for line where it is the sound (the bar levels and normalisation, the rotor, the percussion and
 * click, the glottal source, the resonator coefficients and cascade, the profiles, tilt, decay, stretch and band limit).
 * What changed for Switch Blade:
 *   - the machine contract: every slot 0..127 on all three models (a model's descriptor may rename a slot, not change its
 *     range: params.c track_desc), E4 = CUT and E5 = RES, the machine knob E7 (ORGAN ROTR, VOX VOWEL, ADD SPECTR). ORGAN
 *     gets a 2-pole tone filter on CUT / RES (QG's WHEEL has none: PERC and DRIVE sat there); SUB / BODY / TOP are 64 =
 *     the registration as it is, +-8 bars over the range. DRIVE is left to the part's DIST (as QG's own notes suggest);
 *     CLICK is the preset's (GW_PX). VOX: CUT is the formant SHIFT (+-12 semitones, 64 none) and RES the formant Q.
 *   - QG's CHARACTER page (LEAK, ROTOR+, P.DCY; F3+, ARTIC, NASAL; EVOLVE) is not ported: neutral, as QG's own defaults.
 *   - QG's voice pool (VSLOT, 12 voices): here each part's engine memory holds its voices' state (8) and the per-part
 *     state; the block() hook (engine_t.block, added for this: voice.c track_render calls it before the voices).
 *   - Voice caps: VOX 4 (QG's), ORGAN and ADD the shared 8.
 * The donors are not built as a sample-for-sample reference: QG's files need its whole engine framework (CHARACTER, the
 * pool); tests/gwz_test.c checks the port's behaviour instead (bars, percussion, rotor, vowels, partials, decay). */

enum { GW_ORGAN, GW_VOX, GW_ADD, GW_N };
#define GW_MODEL (P_RSV_LN + 2)          /* the part's model: 0 ORGAN 1 VOX 2 ADD (127, every older sound: ORGAN) */
#define GW_CLICK P_FM1_ATK               /* ORGAN's key click, the preset's (GW_PX) */
static uint32_t gw_model(const track_t *t)
{
    int32_t m = t->p[GW_MODEL];
    return m == GW_VOX ? GW_VOX : m == GW_ADD ? GW_ADD : GW_ORGAN;
}

/* ================================================================== ORGAN (QG's WHEEL) === */
#define DRW_NP 9                         /* partials */
#define DRW_FOLD 584366000u              /* ~6 kHz as a phase increment: partials above fold down an octave */
#define DRW_XOVER 77915000u              /* ~800 Hz: below the low rotor, above the high rotor */
static const uint16_t DRW_LV[9] = {0, 2920, 4125, 5827, 8231, 11627, 16423, 23198, 32767};   /* bar 0..8, 3 dB a step */
/* the 16 registrations, one digit per bar: 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' */
static const char DRW_REG[16][DRW_NP + 1] = {
    "008000000", "008400000", "800800000", "838000000", "888000000", "888800000", "888600006", "888000888",
    "800000888", "008080800", "008765432", "006876540", "004432000", "008888000", "858000000", "888888888",
};
static const uint32_t DRW_FOLD_LIM[DRW_NP] = {
    DRW_FOLD * 2u, DRW_FOLD / 3u * 2u, DRW_FOLD, DRW_FOLD / 2u, DRW_FOLD / 3u, DRW_FOLD / 4u, DRW_FOLD / 5u,
    DRW_FOLD / 6u, DRW_FOLD / 8u,
};
static inline uint32_t drw_ratio(uint32_t k, uint32_t x)
{
    switch (k) {
    case 0: return x >> 1;
    case 1: return x + (x >> 1);
    case 2: return x;
    case 3: return x << 1;
    case 4: return x * 3u;
    case 5: return x << 2;
    case 6: return x * 5u;
    case 7: return x * 6u;
    default: return x << 3;
    }
}
#define DRW_HZ(h) ((int32_t)((h) * 3116457.0))
static const int32_t DRW_ROT_SPD[2][3] = {
    {0, DRW_HZ(0.8), DRW_HZ(6.7)},       /* high rotor: OFF (stops), SLOW, FAST */
    {0, DRW_HZ(0.7), DRW_HZ(5.8)},       /* low rotor */
};
typedef struct {                         /* per part, once a block (gw_block) */
    int32_t g[DRW_NP];
    int32_t pk, plev;
    uint32_t pdec;
    int32_t clk;
    uint32_t rph[2];
    int32_t rsp[2];
    int32_t rwet;
    int32_t am[2];
    int32_t fm[2];
} drw_trk_t;
typedef struct {                         /* per voice */
    uint32_t ph[DRW_NP];
    int32_t gp[DRW_NP];
    int32_t perc, clk, nz, hp;
    int32_t f1, f2;                      /* Switch Blade: the tone filter's states */
    uint8_t gate, init;
} drw_vc_t;

/* =================================================================== ADD (QG's ADDTV) === */
#define AD_NP 8
#define AD_LO 438304000u                 /* ~4.5 kHz: from here up a partial fades out */
#define AD_HI 584366000u                 /* ~6 kHz: at and above it not rendered */
static const uint8_t AD_PROF[AD_NP][AD_NP] = {
    {127, 0, 66, 0, 34, 0, 18, 0},       /* ORGAN */
    {127, 64, 42, 32, 25, 21, 18, 16},   /* SAW */
    {127, 0, 42, 0, 25, 0, 18, 0},       /* SQUARE */
    {127, 0, 26, 0, 62, 34, 0, 46},      /* BELL */
    {127, 18, 48, 74, 92, 60, 34, 22},   /* GLASS */
    {127, 40, 88, 30, 64, 20, 38, 12},   /* REED */
    {127, 0, 96, 0, 0, 0, 22, 0},        /* HOLLOW */
    {127, 96, 74, 110, 52, 88, 40, 66},  /* METAL */
};
typedef struct { int32_t g[AD_NP]; } ad_trk_t;
typedef struct {
    uint32_t ph[AD_NP];
    uint16_t dk[AD_NP];
    int16_t gp[AD_NP];
    uint32_t sub;
} ad_vc_t;

/* ---- the part's engine memory: the model's per-part state and its eight voices ---- */
typedef struct {
    uint32_t model;                      /* the model the state below belongs to (+ 1; 0: none yet) */
    union { drw_trk_t o; ad_trk_t a; } tk;
    union { drw_vc_t o[NVOICE]; ad_vc_t a[NVOICE]; } vc;
} gw_mem_t;
#define GW_MEM sizeof(gw_mem_t)
static gw_mem_t *gw_mem(const track_t *t) { return (gw_mem_t *)ENG_MEM((uint32_t)(t - trk) % NPART); }
static uint32_t gw_vi(const track_t *t, const voice_t *v) { return (uint32_t)(v - t->v) % NVOICE; }
/* another model on the part (a preset of another): its state from rest, as a switch to another machine would be */
static gw_mem_t *gw_sync(const track_t *t)
{
    gw_mem_t *mm = gw_mem(t);
    uint32_t m = gw_model(t) + 1u;
    if (mm->model != m) {
        memset(mm, 0, sizeof *mm);
        mm->model = m;
    }
    return mm;
}

static int32_t drw_level(const int16_t *p, uint32_t k)   /* bar k: the registration plus SUB / BODY / TOP (64: as it is) */
{
    int32_t d = DRW_REG[(uint32_t)clamp(p[P_E0], 0, 127) >> 3][k] - '0';
    int32_t o = k < 2u ? p[P_E1] : k < 4u ? p[P_E2] : p[P_E3];
    d += (clamp(o, 0, 127) - 64) >> 3;
    return clamp(d, 0, 8);
}
/* ROTR (E7, the machine knob) is continuous on GW-Z: below 8 OFF, then from QG's SLOW speed at 8 to its FAST at 127, a
 * Leslie's speed control rather than QG's three-way switch (the machine knob must be a continuous value: CC 70, HOME) */
#define GW_ROT_OFF 8
static inline void drw_rotor(drw_trk_t *T, uint32_t k, int32_t r)
{
    int32_t sp, c, sn, dep, tgt = r < GW_ROT_OFF ? 0 :
        DRW_ROT_SPD[k][1] + (int32_t)(((int64_t)(DRW_ROT_SPD[k][2] - DRW_ROT_SPD[k][1]) * (r - GW_ROT_OFF)) / (127 - GW_ROT_OFF));
    T->rsp[k] += (tgt - T->rsp[k]) >> (k ? 11 : 9);
    sp = T->rsp[k];
    T->rph[k] += (uint32_t)sp;
    sn = sine_i(T->rph[k]);
    c = sine_i(T->rph[k] + 0x40000000u);
    dep = k ? 7000 : 13000;
    T->am[k] = 32767 - mulq15(dep, (32767 - c) >> 1);
    T->fm[k] = (sn * (sp >> (k ? 17 : 16))) >> 15;
}
static uint32_t gw_perc(const int16_t *p) { return (uint32_t)clamp(p[P_E6], 0, 127) >> 4 > 6u ? 6u : (uint32_t)clamp(p[P_E6], 0, 127) >> 4; }

static void organ_block(track_t *t, drw_trk_t *T)
{
    const int16_t *p = t->p;
    uint32_t k, perc = gw_perc(p);
    int32_t rot = clamp(p[P_E7], 0, 127);
    int32_t s = 0, lv[DRW_NP];
    uint32_t norm;
    for (k = 0; k < DRW_NP; k++) {
        lv[k] = perc && k == DRW_NP - 1u ? 0 : DRW_LV[drw_level(p, k)];
        s += lv[k];
    }
    norm = (uint32_t)(2u * 39322u * 32768u) / (uint32_t)(s + 49152);
    if (perc == 1u || perc == 2u || perc == 5u || perc == 6u)
        norm = (norm * 23170u) >> 15;
    for (k = 0; k < DRW_NP; k++)
        T->g[k] = (int32_t)(((uint32_t)lv[k] * norm) >> 15);
    T->pk = perc ? (int32_t)(3u + ((perc - 1u) & 1u)) : DRW_NP;
    T->plev = perc == 3u || perc == 4u ? 12000 : 24000;
    T->pdec = perc >= 5u ? 65457u : 65219u;
    T->clk = clamp(p[GW_CLICK], 0, 127) * 180;
    drw_rotor(T, 0, rot);
    drw_rotor(T, 1, rot);
    T->rwet += ((rot >= GW_ROT_OFF ? 32767 : 0) - T->rwet) >> 5;
    T->am[0] = 32767 - mulq15(T->rwet, 32767 - T->am[0]);
    T->am[1] = 32767 - mulq15(T->rwet, 32767 - T->am[1]);
    T->fm[0] = mulq15(T->fm[0], T->rwet);
    T->fm[1] = mulq15(T->fm[1], T->rwet);
}

static void add_block(track_t *t, ad_trk_t *T)
{
    const int16_t *p = t->p;
    int32_t pr = clamp(p[P_E7], 0, 127) * (AD_NP - 1), tilt = clamp(p[P_E1], 0, 127) - 64, sc[AD_NP], sum = 0, fx;
    uint32_t f0, f1, k, scale;
    f0 = (uint32_t)(pr / 127);
    f1 = f0 + 1u < AD_NP ? f0 + 1u : f0;
    fx = (pr % 127) * 256 / 127;
    for (k = 0; k < AD_NP; k++) {
        int32_t a = AD_PROF[f0][k] * 258, b = AD_PROF[f1][k] * 258, v = a + (((b - a) * fx) >> 8);
        if (tilt) {
            int32_t q = 1024 + tilt * (int32_t)(k + 1u) * 16;
            v = (v * (q < 96 ? 96 : q > 8192 ? 8192 : q)) >> 10;
        }
        sc[k] = v;
        sum += v;
    }
    scale = sum > 0 ? (32767u << 8) / (uint32_t)sum : 0u;
    for (k = 0; k < AD_NP; k++)
        T->g[k] = (int32_t)(((uint32_t)sc[k] * scale) >> 8);
}

static void gw_block(track_t *t)
{
    gw_mem_t *mm = gw_sync(t);
    switch (gw_model(t)) {
    case GW_ORGAN: organ_block(t, &mm->tk.o); break;
    case GW_ADD: add_block(t, &mm->tk.a); break;
    default: break;                      /* VOX: nothing shared */
    }
}

/* ---------------------------------------------------------------- ORGAN voice */
static void organ_note_on(track_t *t, voice_t *v, gw_mem_t *mm)
{
    uint32_t vi = gw_vi(t, v), k, held = 0;
    drw_vc_t *V = &mm->vc.o[vi];
    for (k = 0; k < NVOICE; k++)         /* single trigger: another key of the part held? */
        if (&t->v[k] != v && t->v[k].active && t->v[k].gate && t->v[k].stage != 4u)
            held = 1;
    if (!V->init) {                      /* the wheels: spread start phases, then free running */
        for (k = 0; k < DRW_NP; k++)
            V->ph[k] = (k * 0x9E3779B9u) ^ (vi * 0x7F4A7C15u);
        V->nz = 0x13579BDF + (int32_t)vi;
        V->init = 1;
    }
    V->perc = held ? 0 : 32767;
    V->clk = 32767;
    V->hp = 0;
    V->gate = 1;
}

static void organ_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m, gw_mem_t *mm)
{
    const drw_trk_t *T = &mm->tk.o;
    drw_vc_t *V = &mm->vc.o[gw_vi(t, v)];
    int32_t acc[CTL], perc1, clk0, clk1, f1 = V->f1, f2 = V->f2;
    uint32_t i, k;
    tsvf_t flt;
    if (n > CTL)
        n = CTL;
    for (i = 0; i < n; i++)
        acc[i] = 0;
    perc1 = (int32_t)(((uint32_t)V->perc * T->pdec) >> 16);
    for (k = 0; k < DRW_NP; k++) {
        uint32_t x = m->inc, inc, ph = V->ph[k];
        int32_t g1 = T->g[k], g0 = V->gp[k], r;
        if ((int32_t)k == T->pk)
            g1 += mulq15(T->plev, perc1);
        while (x > DRW_FOLD_LIM[k])
            x >>= 1;
        inc = drw_ratio(k, x);
        r = inc < DRW_XOVER;
        g1 = mulq15(g1, T->am[r]);
        inc += (uint32_t)((int32_t)(inc >> 16) * T->fm[r]);
        V->gp[k] = g1;
        if (!g0 && !g1) {
            V->ph[k] = ph + inc * n;
            continue;
        }
        if (g0 == g1) {
            for (i = 0; i < n; i++) {
                ph += inc;
                acc[i] += (SINE[ph >> 22] * g1) >> 15;
            }
        } else {
            int32_t gq = g0 << 5, dg = g1 - g0;
            for (i = 0; i < n; i++) {
                ph += inc;
                gq += dg;
                acc[i] += (SINE[ph >> 22] * (gq >> 5)) >> 15;
            }
        }
        V->ph[k] = ph;
    }
    V->perc = perc1;
    if (V->gate && !v->gate) {           /* key click: at the contact and (half) at the release */
        V->gate = 0;
        V->clk = 16384;
    }
    clk0 = mulq15(V->clk, T->clk);
    if (clk0 > 16) {
        int32_t nst = V->nz, hp = V->hp, dc;
        V->clk = (V->clk * 3) >> 2;
        clk1 = mulq15(V->clk, T->clk);
        dc = clk1 - clk0;
        clk0 <<= 5;
        for (i = 0; i < n; i++) {
            int32_t z = (int32_t)(noise32(&nst) >> 17) - 16384, y = z - hp;
            hp += y >> 3;
            clk0 += dc;
            acc[i] += (y * (clk0 >> 5)) >> 15;
        }
        V->nz = nst;
        V->hp = hp;
    }
    tsvf_coef(&flt, (t->p[P_E4] << 8) + m->cutoff, clamp(t->p[P_E5], 0, 127));   /* Switch Blade: the tone filter */
    for (i = 0; i < n; i++)
        out[i] += voice_amp(tsvf_lp(&flt, acc[i] >> 1, &f1, &f2), m, i) << 1;
    V->f1 = f1;
    V->f2 = f2;
}

/* ------------------------------------------------------------------ VOX voice (QG's VOICE) */
#define FMT_FOLOW 30
#define FMT_TALK_MASK 0x01FFFFFF
static const uint16_t VOWEL_F[5][3] = {
    {768, 1333, 2522}, {580, 1799, 2605}, {342, 2322, 3000}, {497, 910, 2459}, {378, 997, 2343},
};
static const uint8_t VOWEL_B[5][3] = {
    {90, 100, 160}, {70, 100, 170}, {50, 120, 200}, {70, 80, 150}, {60, 80, 150},
};
static const uint32_t SEMI_Q16[13] = {
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715, 131072,
};
static uint32_t fmt_ratio(int32_t st16)
{
    int32_t o, k;
    uint32_t r;
    st16 = clamp(st16, -4 * 192, 4 * 192 - 1) + 4 * 192;
    o = (st16 * 2731) >> 19;
    k = st16 - o * 192;
    r = SEMI_Q16[k >> 4] + (((SEMI_Q16[(k >> 4) + 1] - SEMI_Q16[k >> 4]) * (uint32_t)(k & 15)) >> 4);
    return o >= 4 ? r << (o - 4) : r >> (4 - o);
}
static void vowel_at(int32_t pos, int32_t *f, int32_t *b)
{
    int32_t x = (clamp(pos, 0, 127 << 8) * 33027) >> 20, i = x >> 8, fr = x & 255, j;
    if (i >= 4) {
        i = 3;
        fr = 256;
    }
    for (j = 0; j < 3; j++) {
        f[j] = (VOWEL_F[i][j] << 4) + (((VOWEL_F[i + 1][j] - VOWEL_F[i][j]) * fr) >> 4);
        b[j] = (VOWEL_B[i][j] << 4) + (((VOWEL_B[i + 1][j] - VOWEL_B[i][j]) * fr) >> 4);
    }
}
typedef struct { int32_t a, b, c; } fres_t;
static int32_t formant_nz = 0x2545F491;
static void fres_coef(fres_t *r, uint32_t f16, uint32_t bw16)
{
    int32_t s = sine_i(f16 * 3044u);
    int32_t x = (int32_t)(bw16 * 4781u);
    int32_t x2 = (int32_t)(((int64_t)x * x) >> 30), x3 = (int32_t)(((int64_t)x2 * x) >> 30);
    int32_t x4 = (int32_t)(((int64_t)x3 * x) >> 30);
    int32_t e = (1 << 30) - x + (x2 >> 1) - (int32_t)(((int64_t)x3 * 178956971) >> 30)
                + (int32_t)(((int64_t)x4 * 44739243) >> 30);
    int32_t cs = (1 << 30) - 2 * s * s;
    r->b = (int32_t)(((int64_t)e * cs) >> 29);
    r->c = -(int32_t)(((int64_t)e * e) >> 30);
    r->a = (1 << 30) - r->b - r->c;
}
static void vox_note_on(voice_t *v)
{
    v->s[7] = (int32_t)((noise32(&formant_nz) >> 25) << 25);   /* TALK starts over; a random vowel (RAND) */
    if (!v->env && !v->env_out) {
        uint32_t i;
        v->ph[0] = 0;
        v->ph[1] = v->ph[2] = 0;
        for (i = 0; i < 7u; i++)
            v->s[i] = 0;
    }
}
/* the slots: E0 BUZZ, E1 VOWL2, E2 TALK, E3 RAND, E4 SHIFT (the formants, 64 none), E5 Q, E6 BREATH, E7 VOWEL */
static void vox_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    fres_t r1, r2, r3, r4;
    int32_t fa[3], ba[3], fb[3], bb[3], k, j, buzz, oq, tilt, gv, gn, og, ogk;
    int32_t shift = (clamp(p[P_E4], 0, 127) - 64) * 12 / 64, q = clamp(p[P_E5], 0, 127), breath = clamp(p[P_E6], 0, 127);
    uint32_t inc = m->inc, i, te, tp, rp, rn, ratio, bws, f0;
    uint32_t ph = v->ph[0];
    int32_t y1 = v->s[0], y2 = v->s[1], y3 = v->s[2], y4 = v->s[3], y5 = v->s[4], y6 = v->s[5];
    int32_t y7 = (int32_t)v->ph[1], y8 = (int32_t)v->ph[2], lp = v->s[6], nst = formant_nz;
    k = 0;
    if (p[P_E2]) {
        uint32_t g = ((uint32_t)v->s[7] & FMT_TALK_MASK) + ENV_LIN[p[P_E2] & 127];
        g = g > (1u << 24) ? 1u << 24 : g;
        v->s[7] = (int32_t)(((uint32_t)v->s[7] & ~(uint32_t)FMT_TALK_MASK) | g);
        k = (int32_t)(g >> 9);
        k = (k * (65536 - k)) >> 15;
    }
    {
        int32_t rv = (int32_t)(((uint32_t)v->s[7] >> 25) << 8), v0 = clamp(p[P_E7], 0, 127) << 8, v1 = clamp(p[P_E1], 0, 127) << 8;
        v0 += ((rv - v0) * clamp(p[P_E3], 0, 127) * 258) >> 15;
        v1 += ((rv - v1) * clamp(p[P_E3], 0, 127) * 258) >> 15;
        vowel_at(v0 + m->cutoff, fa, ba);
        vowel_at(v1 + m->cutoff, fb, bb);
    }
    ratio = fmt_ratio(shift * 16 + ((FMT_FOLOW * (v->pitch_cur - 60 * 16) * 516) >> 16));
    bws = fmt_ratio((64 - q) * 4);
    f0 = (uint32_t)(((uint64_t)inc * 705600u) >> 32);
    for (j = 0; j < 3; j++) {
        int32_t f = fa[j] + (((fb[j] - fa[j]) * k) >> 15), b = ba[j] + (((bb[j] - ba[j]) * k) >> 15);
        f = (int32_t)(((int64_t)f * ratio) >> 16);
        b = (int32_t)(((((int64_t)b * ratio) >> 16) * bws) >> 16);
        if (j == 0 && (uint32_t)f < f0 + (f0 >> 4) && f > 0) {
            b = (int32_t)(((int64_t)b * (int32_t)(f0 + (f0 >> 4))) / f);
            f = (int32_t)(f0 + (f0 >> 4));
        }
        fa[j] = clamp(f, 60 * 16, 7000 * 16);
        ba[j] = clamp(b, 20 * 16, 1500 * 16);
    }
    fres_coef(&r1, (uint32_t)fa[0], (uint32_t)ba[0]);
    fres_coef(&r2, (uint32_t)fa[1], (uint32_t)ba[1]);
    fres_coef(&r3, (uint32_t)fa[2], (uint32_t)ba[2]);
    fres_coef(&r4, (uint32_t)clamp((int32_t)((3300u * 16u * (uint64_t)ratio) >> 16), 60 * 16, 7000 * 16),
              (uint32_t)clamp((int32_t)((((250u * 16u * (uint64_t)ratio) >> 16) * bws) >> 16), 20 * 16, 1500 * 16));
    buzz = clamp((clamp(p[P_E0], 0, 127) << 8) + m->shape - (64 << 8) + (v->vel - 96) * 40, 0, 127 << 8);
    oq = 58982 - ((buzz * 284) >> 8);
    tilt = 2500 + ((buzz * 109) >> 8);
    te = (uint32_t)oq << 16;
    tp = (te >> 8) * 166u;
    rp = (1u << 30) / ((tp >> 16) | 1u);
    rn = (1u << 30) / (((te - tp) >> 16) | 1u);
    gv = 32767 - breath * breath * 2;
    gn = breath * 258;
    og = 13000 - ((q - 64) * 50);
    og = (int32_t)(((int64_t)og * fmt_ratio((60 * 16 - v->pitch_cur) / 4)) >> 16);
    ogk = (og >> 2) * (4 + (buzz >> 10));
    for (i = 0; i < n; i++) {
        int32_t e, x, a, s;
        if (ph < tp)
            e = (sine_i(((ph >> 16) * rp) << 1) * 17644) >> 15;
        else if (ph < te)
            e = -sine_i(((ph - tp) >> 16) * rn);
        else
            e = 0;
        e += blep(ph - te, inc) >> 1;
        lp += ((e - lp) * tilt) >> 14;
        x = (lp * gv) >> 15;
        if (gn) {
            int32_t nz = (((int32_t)(noise32(&nst) >> 16) - 32768) * gn) >> 15;
            x += ph < te ? nz : nz >> 1;
        }
        a = (int32_t)(((int64_t)r1.a * (x << 6) + (int64_t)r1.b * y1 + (int64_t)r1.c * y2 + (1 << 29)) >> 30);
        y2 = y1;
        y1 = a;
        a = (int32_t)(((int64_t)r2.a * a + (int64_t)r2.b * y3 + (int64_t)r2.c * y4 + (1 << 29)) >> 30);
        y4 = y3;
        y3 = a;
        a = (int32_t)(((int64_t)r3.a * a + (int64_t)r3.b * y5 + (int64_t)r3.c * y6 + (1 << 29)) >> 30);
        y6 = y5;
        y5 = a;
        a = (int32_t)(((int64_t)r4.a * a + (int64_t)r4.b * y7 + (int64_t)r4.c * y8 + (1 << 29)) >> 30);
        y8 = y7;
        y7 = clamp(a, -(1 << 28), 1 << 28);
        s = soft_knee(clamp((int32_t)(((int64_t)y7 * og + (int64_t)(y7 - y8) * ogk) >> 21), -200000, 200000), 24000);
        ph += inc;
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph;
    v->ph[1] = (uint32_t)y7;
    v->ph[2] = (uint32_t)y8;
    formant_nz = nst;
    v->s[0] = y1, v->s[1] = y2, v->s[2] = y3, v->s[3] = y4, v->s[4] = y5, v->s[5] = y6, v->s[6] = lp;
}

/* ------------------------------------------------------------------ ADD voice (QG's ADDTV) */
static void add_note_on(track_t *t, voice_t *v, gw_mem_t *mm)
{
    ad_vc_t *V = &mm->vc.a[gw_vi(t, v)];
    uint32_t k;
    for (k = 0; k < AD_NP; k++) {
        V->ph[k] = 0;
        V->dk[k] = 65535;
        V->gp[k] = 0;
    }
    V->sub = 0;
}
/* the slots: E0 SUB, E1 TILT (64 flat), E2 DECAY, E3 STRETCH, E4 CUT, E5 RES, E6 DRIVE, E7 SPECTR */
static void add_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m, gw_mem_t *mm)
{
    const ad_trk_t *T = &mm->tk.a;
    ad_vc_t *V = &mm->vc.a[gw_vi(t, v)];
    const int16_t *p = t->p;
    int32_t acc[CTL], ic1 = v->s[0], ic2 = v->s[1], dcx = v->s[2], dcy = v->s[3];
    int32_t cut = (p[P_E4] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) * 4);
    int32_t drv = clamp(p[P_E6], 0, 127), drive = 32768 + drv * 512;
    int32_t sub = clamp(p[P_E0], 0, 127) * 258, stretch = clamp(p[P_E3], 0, 127), dec = clamp(p[P_E2], 0, 127);
    uint32_t i, k, sph = V->sub;
    tsvf_t flt;
    if (n > CTL)
        n = CTL;
    for (i = 0; i < n; i++)
        acc[i] = 0;
    tsvf_coef(&flt, cut, clamp(p[P_E5], 0, 127));
    for (k = 0; k < AD_NP; k++) {
        uint32_t inc = m->inc, ph = V->ph[k], d = V->dk[k];
        int32_t g1, g0;
        if (k) {
            uint32_t rq = (k + 1u) * 1024u +
                          (uint32_t)(((uint32_t)(k + 1u) * (k + 1u) - 1u) * (uint32_t)stretch * 48u) / 127u;
            inc = (uint32_t)(((uint64_t)inc * rq) >> 10);
        }
        d -= (d * (uint32_t)((dec * (int32_t)k) >> 2)) >> 15;
        V->dk[k] = (uint16_t)d;
        g1 = mulq15(T->g[k], (int32_t)(d >> 1));
        if (inc >= AD_HI)
            g1 = 0;
        else if (inc > AD_LO)
            g1 = (int32_t)(((uint32_t)g1 * ((AD_HI - inc) >> 19)) / 280u);
        g0 = V->gp[k];
        V->gp[k] = (int16_t)g1;
        if (!g0 && !g1) {
            V->ph[k] = ph + inc * n;
            continue;
        }
        if (g0 == g1) {
            for (i = 0; i < n; i++) {
                ph += inc;
                acc[i] += (SINE[ph >> 22] * g1) >> 15;
            }
        } else {
            int32_t gq = g0 << 5, dg = g1 - g0;
            for (i = 0; i < n; i++) {
                ph += inc;
                gq += dg;
                acc[i] += (SINE[ph >> 22] * (gq >> 5)) >> 15;
            }
        }
        V->ph[k] = ph;
    }
    if (sub) {
        uint32_t inc = m->inc >> 1;
        for (i = 0; i < n; i++) {
            sph += inc;
            acc[i] += (SINE[sph >> 22] * sub) >> 15;
        }
    }
    V->sub = sph;
    for (i = 0; i < n; i++) {
        int32_t s = acc[i];
        if (drv)
            s = softclip(((s >> 2) * (drive >> 2)) >> 11);
        s = soft_knee(tsvf_lp(&flt, s >> 1, &ic1, &ic2), 16000) * 2;
        {
            int32_t y = s - dcx + (((dcy * 32744) + 16384) >> 15);
            dcx = s;
            dcy = y;
            s = y;
        }
        out[i] += voice_amp(s, m, i);
    }
    v->s[0] = ic1, v->s[1] = ic2, v->s[2] = dcx, v->s[3] = dcy;
}

/* ------------------------------------------------------------------ the machine */
static void gw_note_on(track_t *t, voice_t *v)
{
    gw_mem_t *mm = gw_sync(t);
    switch (gw_model(t)) {
    case GW_VOX: vox_note_on(v); break;
    case GW_ADD:
        add_note_on(t, v, mm);
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
        break;
    default: organ_note_on(t, v, mm); break;
    }
}
static void gw_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    gw_mem_t *mm = gw_mem(t);
    switch (gw_model(t)) {
    case GW_VOX: vox_render(t, v, out, n, m); break;
    case GW_ADD: add_render(t, v, out, n, m, mm); break;
    default: organ_render(t, v, out, n, m, mm); break;
    }
}
static uint8_t gw_poly_of(const track_t *t) { return gw_model(t) == GW_VOX ? 4u : (uint8_t)NVOICE; }

/* the slot names: ORGAN's are the machine's own edit[] (REG and PERC named per value: an F_ENUM's names are indexed by
 * the value, 0..127 here); VOX's and ADD's through desc */
#define R8(x) x, x, x, x, x, x, x, x
#define R16(x) R8(x), R8(x)
static const char *const N_GW_REG[128] = {
    R8("FLUTE"), R8("MELLO"), R8("HOLLW"), R8("SMOOT"), R8("3BAR"), R8("BLUES"), R8("GOSPL"), R8("ROCK"),
    R8("TOPS"), R8("CLARI"), R8("REED"), R8("STRNG"), R8("CHAPL"), R8("BRITE"), R8("BASS"), R8("FULL"),
};
static const char *const N_GW_PERC[128] = {
    R16("OFF"), R16("2ND"), R16("3RD"), R16("2SOFT"), R16("3SOFT"), R16("2SLOW"), R16("3SLOW"), R16("3SLOW"),
};
/* (the same range and default as edit[k] in every slot: track_desc renames, it does not re-range) */
static const param_desc_t GW_VOX_EDIT[8] = {
    {"BUZZ", F_PCT, 0, 127, 32, 0, 0},          {"VOWL2", F_PCT, 0, 127, 64, 0, 0},
    {"TALK", F_TIME, 0, 127, 64, 0, 0},         {"RAND", F_PCT, 0, 127, 64, 0, 0},
    {"SHIFT", F_PCT, 0, 127, 110, 0, 0},        {"Q", F_PCT, 0, 127, 0, 0, 0},
    {"BRTH", F_PCT, 0, 127, 0, 0, 0},           {"VOWEL", F_PCT, 0, 127, 64, 0, 0},
};
static const param_desc_t GW_ADD_EDIT[8] = {
    {"SUB", F_PCT, 0, 127, 32, 0, 0},           {"TILT", F_PCT, 0, 127, 64, 0, 0},
    {"DECAY", F_PCT, 0, 127, 64, 0, 0},         {"STRCH", F_PCT, 0, 127, 64, 0, 0},
    {"CUT", F_CUTOFF, 0, 127, 110, 0, 0},       {"RES", F_PCT, 0, 127, 0, 0, 0},
    {"DRIVE", F_PCT, 0, 127, 0, 0, 0},          {"SPECT", F_PCT, 0, 127, 64, 0, 0},
};
static const param_desc_t *gw_desc(const track_t *t, uint32_t k)
{
    uint32_t m = gw_model(t);
    return k >= 8u || m == GW_ORGAN ? 0 : m == GW_VOX ? &GW_VOX_EDIT[k] : &GW_ADD_EDIT[k];
}

/* a preset just loaded (ui.c apply_preset_to, hostsim): its model and ORGAN's click (GW_PX, tools/four_presets.py) */
static void gw_track_loaded(track_t *t)
{
    if (t->eng_req % NENGINES != ENGI_GWZ || t->preset >= NELEM(GW_PX))
        return;
    t->p[GW_MODEL] = GW_PX[t->preset][0];
    t->p[GW_CLICK] = GW_PX[t->preset][1];
}

static const engine_t ENG_GWZ = {
    .name = "GW-Z", .page_title = {"VOICE", "TONE"},
    .edit = {                            /* ORGAN's (VOX, ADD: gw_desc); every slot 0..127 on all three */
        {"REG", F_ENUM, 0, 127, 32, N_GW_REG, 0},   {"SUB", F_PCT, 0, 127, 64, 0, 0},
        {"BODY", F_PCT, 0, 127, 64, 0, 0},          {"TOP", F_PCT, 0, 127, 64, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 110, 0, 0},       {"RES", F_PCT, 0, 127, 0, 0, 0},
        {"PERC", F_ENUM, 0, 127, 0, N_GW_PERC, 0},  {"ROTR", F_PCT, 0, 127, 64, 0, 0},   /* (continuous: drw_rotor) */
    },
    .presets = GWZ_PRESETS, .npresets = NELEM(GWZ_PRESETS),   /* presets/four/gwz.py */
    .knob = {P_E4, P_E5, P_ED_FLT, P_E7},
    .poly_of = gw_poly_of,
    .keep = 0x0Fu,                       /* ADD's filter and DC blocker (s0..s3; VOX starts its own over at a fresh note) */
    .note_on = gw_note_on, .render = gw_render,
    .desc = gw_desc,
    .mem = GW_MEM,
    .block = gw_block,
};
