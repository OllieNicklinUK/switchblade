/* SPDX-License-Identifier: GPL-3.0-only
 * JP-Z: the Roland JP-8000's Super Saw (2026-11-17-supr-jp8000.md, the plan 2026-11-17-eight-machines.md §4; V2 Phase 3).
 *
 * The oscillator is the JP-8000's ESP2 microcode as The Usual Suspects extracted it and atosynth worked it through
 * (atosynth.blogspot.com, January and February 2026; Szabó 2010 is the older fit to recordings): seven naive saws on
 * free-running 32-bit phases (the JP's are 24-bit; the aliasing is part of the sound), detuned asymmetrically by the
 * microcode's integer law inc[i] = pitch + DT[i] * (pitch * detune >> 23) >> 7, DT = {0, 128, -128, 408, -412, 704,
 * -720}, detune from the MCU's piecewise knob curve (JPZ_DTN, x 512); the centre saw at 25 / 128 and the six side saws at
 * MIX / 128; never reset at a note. Then OSC2 (a saw, -24..+24 semitones) against it by BAL, a one-pole high-pass at the
 * note's fundamental, and a clean 12 / 24 dB state-variable filter (dsp.c's TPT SVF; the resonance on the first stage
 * only), as the JP's: LP, BP, HP at 12 or 24 dB.
 *
 * NOT verified against a JP-8000: the spec's reference is gearmulator's JE-8086 running the JP's own OS, which needs
 * Roland's OS files (the user's; §10 question 5 of the spec). Until those renders exist the numbers above are the
 * published analysis, typed in, and tests/jpz_test.c checks that the code does what they say, not that they are right.
 * Also unmeasured: the knob curve at 127 (the blog does not give its last step: +96 assumed, as 126's) and the
 * centre/side normalisation (here: the incoherent sum's RMS, so MIX changes the mix, not the level).
 *
 * Switch Blade's own: the rate is 44.1 kHz (SUPR_OS 1: the JP runs at 88.2 kHz; the dearer rate is left for after the
 * device's PERF); amplitude from voice.c's ADSR, ENV / LFO / the matrix on the cutoff through vmod_t, as every machine;
 * KTR around C4; WID is the part's stereo: the Juno BBD chorus (dsp_chorus.c, DRIFT's) after the part, OFF / I / II /
 * I+II over the knob's range: it runs after DIST, the SLICER and the mute, so it stays inside the part's own gating (the
 * spec's side-saw stereo would have bypassed them, §4.3; this is its fallback, a chorus). UNISON at most 2 voices
 * (7 saws each). The phases live in the part's engine memory, seeded once each time JP-Z claims it (a zeroed region would
 * start all seven in phase: the spec's revision note 2). */

#define JPZ_POLY 8u                      /* POLY: the shared budget's eight (measured: tests/regress.c cpu/JP-Z) */
#define JPZ_UNI 2u                       /* UNISON: 2 voices x 7 saws */
#define JPZ_NSAW 7u

static const int16_t JPZ_DT[JPZ_NSAW] = {0, 128, -128, 408, -412, 704, -720};
/* the detune knob, the MCU's curve: 0..63 +1 every second step, 64..80 +1, 81..120 +2, 121..123 +8, 124 +16, 125 +32,
 * 126 +96 (127: +96 assumed); x 512 at use */
static const uint16_t JPZ_DTN[128] = {
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,
    8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15,
    16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23,
    24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30, 30, 31, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78,
    80, 82, 84, 86, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 108, 110,
    112, 114, 116, 118, 120, 122, 124, 126, 128, 136, 144, 152, 168, 200, 296, 392,
};

typedef struct {
    uint32_t ph[NVOICE][JPZ_NSAW];       /* the seven saws of each voice: free-running, never reset */
    uint32_t seeded;                     /* 1: the phases were seeded since this region was last zeroed (eng_mem_claim) */
    chorus_t chorus;                     /* WID: the part's stereo stage */
} jpz_mem_t;
#define JPZ_MEM sizeof(jpz_mem_t)

static jpz_mem_t *jpz_mem(const track_t *t) { return (jpz_mem_t *)ENG_MEM((uint32_t)(t - trk) % NPART); }

static void jpz_seed(jpz_mem_t *mm, uint32_t part)
{
    uint32_t i, k, x = 0x9E3779B9u ^ (part * 0x85EBCA6Bu);
    for (i = 0; i < NVOICE; i++)
        for (k = 0; k < JPZ_NSAW; k++) {
            x = x * 1664525u + 1013904223u;      /* (an LCG: any spread of phases will do, the JP's are whatever they were) */
            mm->ph[i][k] = x;
        }
    mm->seeded = 1;
}

static uint8_t jpz_poly_of(const track_t *t) { return t->p[P_VOICE] == V_UNISON ? (uint8_t)JPZ_UNI : (uint8_t)JPZ_POLY; }

static void jpz_note_on(track_t *t, voice_t *v)
{
    jpz_mem_t *mm = jpz_mem(t);
    if (!mm->seeded)
        jpz_seed(mm, (uint32_t)(t - trk));
    v->s[0] = 0;                         /* the HPF's low-pass state; the SVFs' (kept on a retrigger: .keep) */
    v->s[1] = v->s[4] = v->s[5] = v->s[6] = 0;
}

/* the mix's level: the incoherent sum of 25 / 128 and six x MIX / 128 back to about one saw (x 0.5: half scale, as the
 * other machines peak), Q12: 4096 * 0.5 * 128 / sqrt(25^2 + 6 MIX^2) */
static int32_t jpz_norm(int32_t mix)
{
    static const uint16_t N[9] = {10486, 5639, 3186, 2181, 1651, 1327, 1109, 952, 840};   /* MIX 0, 16 .. 128 (127) */
    int32_t i = clamp(mix, 0, 127) >> 4, f = mix & 15;
    return N[i] + (((N[i + 1] - N[i]) * f) >> 4);
}

/* one TPT SVF step on (ic1, ic2): out by mode 0 LP, 1 BP, 2 HP, 3 notch (dsp.c tsvf_lp's arithmetic) */
static inline int32_t jpz_svf(const tsvf_t *c, int32_t k, int32_t in, int32_t *ic1, int32_t *ic2, uint32_t mode)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    switch (mode) {
    case 0: return v2;
    case 1: return v1;
    case 2: return in - ((k * v1) >> 12) - v2;
    default: return in - ((k * v1) >> 12);
    }
}

/* P_FTYPE (params.c's fixed names) as the JP's filter: stage mode, and whether the second stage runs */
static void jpz_ftype(int32_t ft, uint32_t *mode, uint32_t *two)
{
    switch (ft) {
    case 1: *mode = 1; *two = 0; break;          /* BP */
    case 2: *mode = 2; *two = 0; break;          /* HP (12 dB) */
    case 3: *mode = 0; *two = 0; break;          /* LP12 */
    case 4: *mode = 2; *two = 1; break;          /* HP24 */
    case 5: *mode = 3; *two = 0; break;          /* NOTCH */
    default: *mode = 0; *two = 1; break;         /* LP (24 dB), VOWEL: the JP's has none */
    }
}

static void jpz_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    jpz_mem_t *mm = jpz_mem(t);
    const int16_t *p = t->p;
    uint32_t *ph = mm->ph[(uint32_t)(v - t->v) % NVOICE];
    uint32_t inc[JPZ_NSAW], i, k, mode, two, ph2 = v->ph[0], inc2;
    int32_t det = (int32_t)JPZ_DTN[clamp(p[P_E0], 0, 127)] * 512, side = clamp(p[P_E1], 0, 127);
    int32_t norm = jpz_norm(side), wb = clamp(p[P_E2], -64, 63) + 64, kres, cut, hk;
    int32_t hlp = v->s[0], a1 = v->s[1], a2 = v->s[4], b1 = v->s[5], b2 = v->s[6];
    tsvf_t ca, cb;
    if (!mm->seeded)
        jpz_seed(mm, (uint32_t)(t - trk));
    for (k = 0; k < JPZ_NSAW; k++)             /* the microcode's detune: >> 23 then >> 7 */
        inc[k] = m->inc + (uint32_t)(int32_t)(((int64_t)(int32_t)(m->inc >> 1) * JPZ_DT[k] * det) >> 29);
    inc2 = pitch_inc(clamp(m->pitch16 + p[P_E3] * 16, 0, 2047));
    if (m->fine)
        inc2 += (uint32_t)((int32_t)(inc2 >> 12) * m->fine);
    hk = (int32_t)(((uint64_t)m->inc * 411775u) >> 32);   /* the HPF at f0: 2 pi f / fs, Q16 */
    hk = hk > 58000 ? 58000 : hk;
    cut = (p[P_E4] << 8) + m->cutoff + (((m->pitch16 - 60 * 16) * p[P_E6]) >> 3);   /* KTR: about 1 x at 127, C4 the zero */
    kres = 8192 - clamp(p[P_E5], 0, 127) * 7600 / 127;     /* RES: damping 2.0 .. 0.15 (Q12), the first stage only */
    tsvf_coef_k(&ca, cut, kres);
    tsvf_coef_k(&cb, cut, 5793);                           /* the second stage: Butterworth (sqrt 2) */
    jpz_ftype(p[P_FTYPE], &mode, &two);
    for (i = 0; i < n; i++) {
        int32_t c = (int32_t)(ph[0] >> 16) - 32768, s = 0, x;
        ph[0] += inc[0];
        for (k = 1; k < JPZ_NSAW; k++) {
            s += (int32_t)(ph[k] >> 16) - 32768;
            ph[k] += inc[k];
        }
        x = ((((c * 25 + s * side) >> 4) * norm) >> 12);   /* the mix, at about one saw's level x 0.5 */
        if (wb > 1) {                                      /* BAL: the Super Saw against OSC2 */
            int32_t o2 = ((int32_t)(ph2 >> 16) - 32768) >> 1;
            ph2 += inc2;
            x = (x * (127 - wb) + o2 * wb) >> 7;
        } else
            ph2 += inc2;
        hlp += ((((x << 8) - hlp) >> 6) * (hk >> 3) + 4096) >> 13;   /* the high-pass at the fundamental: a one-pole low-pass, Q8 (no truncation DC), taken off */
        x -= hlp >> 8;
        x = jpz_svf(&ca, kres, x, &a1, &a2, mode);
        if (two)
            x = jpz_svf(&cb, 5793, x, &b1, &b2, mode);
        out[i] += voice_amp(x, m, i);
    }
    v->ph[0] = ph2;
    v->s[0] = hlp;
    v->s[1] = a1, v->s[4] = a2, v->s[5] = b1, v->s[6] = b2;
}

/* WID: the Juno chorus on the part (OFF, I, II, I+II over 0..127) */
static int jpz_part_fx(track_t *t, int32_t *b, int32_t *z, uint32_t n)
{
    int32_t w = clamp(t->p[P_E7], 0, 127), mode = w < 1 ? 0 : w < 43 ? 1 : w < 86 ? 2 : 3;
    return chorus_part(&jpz_mem(t)->chorus, mode, b, z, n);
}

static const engine_t ENG_JPZ = {
    .name = "JP-Z", .page_title = {"OSC", "FLT"},
    .edit = {
        {"DTN", F_PCT, 0, 127, 64, 0, 0},           {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"BAL", F_BIPCT, -64, 63, -64, 0, 0},       {"OSC2", F_SEMI, -24, 24, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 100, 0, 0},       {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"KTR", F_PCT, 0, 127, 64, 0, 0},           {"WID", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = JPZ_PRESETS, .npresets = NELEM(JPZ_PRESETS),   /* presets/four/jpz.py */
    .knob = {P_E4, P_E5, P_ED_FLT, P_E0},
    .poly = JPZ_POLY, .poly_of = jpz_poly_of,
    .keep = 0x73u,                       /* the HPF's and both SVFs' states on a retrigger (s0 s1 s4 s5 s6) */
    .note_on = jpz_note_on, .render = jpz_render,
    .mem = JPZ_MEM,
    .part_fx = jpz_part_fx,
};
