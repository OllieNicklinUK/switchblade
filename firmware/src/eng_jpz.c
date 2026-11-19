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
 * start all seven in phase: the spec's revision note 2).
 *
 * 2026-11-19, for Alan Marcero's JP-8000 bank (tools/jpz_alan_presets.py; his permission, CREDITS.md): the JP's patch
 * parts JP-Z had not, in the track's retired DIGITAL operator ids (a preset load resets them: 0 / their defaults = off):
 *   OSC2's waveform P_FM4_ATK (0 SAW, 1 PULSE, 2 TRI; the JP's naive ones, as its saws) and the pulse's width P_FM4_DEC
 *        (the JP's OSC2 CTRL1: 0 = 50 %, 127 = 15 %, unverified: by ear); OSC2 FINE P_FM3_REL (0 none, else - 64 cents);
 *   the JP's filter envelope: FLT ENV (P_FM1_ATK .. P_FM1_REL, on while P_FM1_LEVEL > 0, its velocity sensitivity),
 *        ENV DEST FLT its depth, the amp ADSR then the amp's only (WAVE's arrangement);
 *   the JP's pitch envelope: depth P_FM2_ATK (0 off, else - 64: 63 = +24 semitones), attack P_FM2_REL, decay P_FM2_DEC
 *        (Felucca's times; up, then back to the pitch), to both oscillators or OSC2 only (P_FM2_SUS < 64);
 *   the JP's OSCILLATOR SHIFT P_FM3_DEC (0 none, else - 3 octaves) and its AMP LEVEL P_FM3_ATK (0 = 127, else x / 127).
 * All of it per control block but OSC2's waveform: one test a sample on OSC2. */

#define JPZ_POLY 8u                      /* POLY: the shared budget's eight (measured: tests/regress.c cpu/JP-Z) */
#define JPZ_UNI 2u                       /* UNISON: 2 voices x 7 saws */
#define JPZ_NSAW 7u
#define JPZ_GAIN 2                       /* after voice_amp (whose input must stay under 2^16): the machine's level */

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
    v->s[2] = 0;                         /* the filter envelope: its level, stage (s3 bits 0-1; the pitch envelope's 4-5), */
    v->s[3] = 1 | (1 << 4);              /* the release's start (s7); progress in ph[1] (filter), ph[2] (pitch), Q24 */
    v->s[7] = 0;
    v->ph[1] = v->ph[2] = 0;
}

/* the JP's filter envelope (FLT ENV, P_FM1_*) over one control block, WAVE's law (void_env1): attack u^2 to the peak (velocity
 * by P_FM1_LEVEL), decay (1 - u)^2 to the sustain, release (1 - u)^2 from where it is; Q15 */
static int32_t jpz_fenv(const int16_t *p, voice_t *v)
{
    uint32_t st = (uint32_t)v->s[3] & 3u, u;
    int32_t vs = clamp(p[P_FM1_LEVEL] - 1, 0, 126) * 260, pk = 32767 - vs + ((vs * (int32_t)v->vel) >> 7);
    int32_t sus = mulq15(clamp(p[P_FM1_SUS], 0, 127) * 258, pk), w, lv;
    if (st && st < 3u && !v->gate) {
        st = 3;
        v->ph[1] = 0;
        v->s[7] = v->s[2];
    }
    if (!st)
        return 0;
    v->ph[1] += ENV_LIN[clamp(p[st == 1u ? P_FM1_ATK : st == 2u ? P_FM1_DEC : P_FM1_REL], 0, 127)];
    if (v->ph[1] >= 1u << 24) {
        v->ph[1] = st == 1u ? 0 : 1u << 24;
        if (st == 1u)
            st = 2;
        else if (st == 3u)
            st = 0;
    }
    u = v->ph[1] >> 9;                                 /* Q15 */
    w = mulq15(32767 - (int32_t)u, 32767 - (int32_t)u);
    lv = st == 1u ? mulq15(mulq15((int32_t)u, (int32_t)u), pk) : st == 2u ? sus + mulq15(pk - sus, w) : st == 3u ? mulq15(v->s[7], w) : 0;
    v->s[3] = (int32_t)(((uint32_t)v->s[3] & ~3u) | st);
    return v->s[2] = lv;
}

/* the JP's pitch envelope over one control block: up to its depth in the attack, back to 0 in the decay; Q15 of the depth */
static int32_t jpz_penv(const int16_t *p, voice_t *v)
{
    uint32_t st = ((uint32_t)v->s[3] >> 4) & 3u, u;
    if (!st)
        return 0;
    v->ph[2] += ENV_LIN[clamp(p[st == 1u ? P_FM2_REL : P_FM2_DEC], 0, 127)];
    if (v->ph[2] >= 1u << 24) {
        v->ph[2] = st == 1u ? 0 : 1u << 24;
        st = st == 1u ? 2u : 0u;
    }
    u = v->ph[2] >> 9;
    v->s[3] = (int32_t)(((uint32_t)v->s[3] & ~0x30u) | (st << 4));
    return st == 1u ? (int32_t)u : st == 2u ? 32767 - (int32_t)u : 0;
}

/* a preset of Alan Marcero's bank just loaded (ui.c apply_preset_to): what a preset_t cannot hold (JPZ_PX, four_presets.h) */
static void jpz_track_loaded(track_t *t)
{
    const int16_t *x;
    uint32_t k;
    if (t->eng_req % NENGINES != ENGI_JPZ || t->preset >= NELEM(JPZ_PX))
        return;
    x = JPZ_PX[t->preset];
    t->p[P_FTYPE] = x[0];
    t->p[P_FM1_LEVEL] = x[1], t->p[P_FM1_ATK] = x[2], t->p[P_FM1_DEC] = x[3], t->p[P_FM1_SUS] = x[4], t->p[P_FM1_REL] = x[5];
    t->p[P_FM2_ATK] = x[6], t->p[P_FM2_REL] = x[7], t->p[P_FM2_DEC] = x[8], t->p[P_FM2_SUS] = x[9];
    t->p[P_FM4_ATK] = x[10], t->p[P_FM4_DEC] = x[11];
    t->p[P_FM3_REL] = x[12], t->p[P_FM3_DEC] = x[13], t->p[P_FM3_ATK] = x[14];
    if (x[15]) {
        t->p[P_LRATE] = x[16], t->p[P_LWAVE] = x[17], t->p[P_LTRIG] = x[18], t->p[P_LFADE] = x[19];
    }
    t->p[P_LD_PIT] = x[20], t->p[P_LD_FLT] = x[21], t->p[P_LD_AMP] = x[22];
    t->p[P_GLIDE] = x[23];
    for (k = 0; k < 4u; k++) {
        t->p[P_M1SRC + 3u * k] = x[24 + 3 * k];
        t->p[P_M1DST + 3u * k] = x[25 + 3 * k];
        t->p[P_M1AMT + 3u * k] = x[26 + 3 * k];
    }
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
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3 + 4096) >> 13;          /* rounded: a floored >> 13 is a bias that, at a cutoff */
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3 + 4096) >> 13); /* near 30 Hz, the states integrate into DC (2026-11-19) */
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
    uint32_t inc[JPZ_NSAW], i, k, mode, two, ph2 = v->ph[0], inc2, inc1 = m->inc, w2 = (uint32_t)clamp(p[P_FM4_ATK], 0, 2), pwth;
    int32_t det = (int32_t)JPZ_DTN[clamp(p[P_E0], 0, 127)] * 512, side = clamp(p[P_E1], 0, 127);
    int32_t norm = jpz_norm(side), wb = clamp(p[P_E2], -64, 63) + 64, kres, cut, hk;
    int32_t pdc;
    int32_t sh = p[P_FM3_DEC] ? (clamp(p[P_FM3_DEC], 1, 5) - 3) * 192 : 0, pe = 0, pit1, lvl = p[P_FM3_ATK] ? clamp(p[P_FM3_ATK], 1, 127) : 127;
    int32_t hlp = v->s[0], a1 = v->s[1], a2 = v->s[4], b1 = v->s[5], b2 = v->s[6];
    tsvf_t ca, cb;
    if (!mm->seeded)
        jpz_seed(mm, (uint32_t)(t - trk));
    {
        int32_t e = jpz_penv(p, v);                    /* (run every block: its state moves whether or not it is on) */
        if (p[P_FM2_ATK])                              /* the pitch envelope's depth: 63 = +24 semitones, 1/16 semitone */
            pe = (int32_t)(((int64_t)e * (clamp(p[P_FM2_ATK], 1, 127) - 64) * 384 / 63) >> 15);
    }
    pit1 = m->pitch16 + sh + (p[P_FM2_SUS] >= 64 ? pe : 0);
    if (pit1 != m->pitch16)                            /* OSC1: the shift, the pitch envelope when it is on both */
        inc1 = cents_inc(pit1, 0, m->fine);
    for (k = 0; k < JPZ_NSAW; k++)             /* the microcode's detune: >> 23 then >> 7 */
        inc[k] = inc1 + (uint32_t)(int32_t)(((int64_t)(int32_t)(inc1 >> 1) * JPZ_DT[k] * det) >> 29);
    if (sh || pe || p[P_FM3_REL])                      /* OSC2: its interval, FINE (cents), the shift, the pitch envelope */
        inc2 = cents_inc(m->pitch16 + sh + pe + p[P_E3] * 16, p[P_FM3_REL] ? clamp(p[P_FM3_REL], 1, 127) - 64 : 0, m->fine);
    else {
        inc2 = pitch_inc(clamp(m->pitch16 + p[P_E3] * 16, 0, 2047));
        if (m->fine)
            inc2 += (uint32_t)((int32_t)(inc2 >> 12) * m->fine);
    }
    pwth = 0x80000000u - (uint32_t)clamp(p[P_FM4_DEC], 0, 127) * 11836782u;   /* the pulse: 50 % .. 15 % (the JP's range: by ear) */
    pdc = (int32_t)(((uint64_t)pwth * 32767u) >> 32) - 16384;               /* its mean, taken off: DC-free at any width */
    hk = (int32_t)(((uint64_t)m->inc * 411775u) >> 32);   /* the HPF at f0: 2 pi f / fs, Q16 */
    hk = hk > 58000 ? 58000 : hk;
    cut = (p[P_E4] << 8) + m->cutoff + (((m->pitch16 - 60 * 16) * p[P_E6]) >> 3);   /* KTR: about 1 x at 127, C4 the zero */
    if (p[P_FM1_LEVEL])                                /* FLT ENV on: ENV DEST FLT is its depth, not the amp ADSR's */
        cut += ((jpz_fenv(p, v) - m->envq15) * p[P_ED_FLT]) >> 7;
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
        x = ((((c * 25 + s * side) >> 7) * norm) >> 12);   /* the mix, at about one saw's level x 0.5 (RMS; the peaks,
                                                            * the saws in phase, about 4x: under the SVF's 150000 and
                                                            * voice_amp's 2^16; >> 4 here was 8x that: device test 2) */
        if (wb > 1) {                                      /* BAL: the Super Saw against OSC2 (SAW, PULSE, TRI: half scale) */
            int32_t r2 = (int32_t)(ph2 >> 16) - 32768,
                    o2 = w2 == 1u ? (ph2 < pwth ? 16383 : -16384) - pdc : w2 == 2u ? ((r2 < 0 ? -r2 : r2) * 2 - 32768) >> 1 : r2 >> 1;
            ph2 += inc2;
            x = (x * (127 - wb) + o2 * wb) >> 7;
        } else
            ph2 += inc2;
        hlp += (mulq16((x << 8) - hlp, (uint32_t)hk) + 32) >> 6;   /* the high-pass at the fundamental: a one-pole low-pass, Q8 (no truncation DC), taken off; mulq16: no 32-bit wrap */
        x -= hlp >> 8;
        x = jpz_svf(&ca, kres, x, &a1, &a2, mode);
        if (two)
            x = jpz_svf(&cb, 5793, x, &b1, &b2, mode);
        if (lvl < 127)
            x = (x * lvl) >> 7;                            /* the JP's AMP LEVEL */
        out[i] += voice_amp(x, m, i) << JPZ_GAIN;
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
