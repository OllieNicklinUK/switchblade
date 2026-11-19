/* SPDX-License-Identifier: GPL-3.0-only */
/* DRIFT: the Juno voice. A float port of the Ultramaster KR-106 (kayrockscreenprinting/ultramaster_kr106, GPL-3.0; Karl LaRocca):
 * the DCO from Source/DSP/KR106Oscillators.h (one phase accumulator behind a LINEAR polyBLEP saw, a comparator pulse, J106-inverted,
 * and the CD4013 sub-octave, its transitions smoothed by the BLEP and nothing else: no charge curvature, no discharge blip, no sub RC;
 * the donor's own comment, KR106Oscillators.h:187-196, says the curvature seen in recordings was the interface's AC-coupling), the voice's drift and component variance from Source/DSP/KR106Voice.h (InitVariance, SetDriftAmount,
 * UpdateDriftWalk), the filter and the half-band resampler from dsp_va.c, the 4-position HPF from Source/DSP/KR106_HPF.h. This
 * file is the machine: the EDIT slots, one voice per slot in the part's engine memory, the control rate. Design and the exact
 * mapping: docs/plans/2026-10-17-drift-juno.md. The polyBLEP path only; the donor's wavetable oscillator needs tables built above
 * its DSP layer and does not stand alone.
 * Departures from the donor's oscillator: the DCO runs at DRIFT_OS (1) x the base rate, the donor's at 4 x; the three gains are
 * mixed by the MIX law below (the donor has three switches), and the voice's output is levelled by it; the pulse width has no
 * LFO selector but the track's SHAPE and an auto PWM above PW 64; the DC blockers are the donor's (mPostOscDcG, mPostVcfDcG).
 * Departures in the voice start: a finished voice resets the filter too (the donor's idle voice keeps ringing, ours is skipped, so
 * a stale state would burst out of the next note); a retrigger keeps phase and filter, as the donor.
 *
 *   E0 MIX  sub-heavy below 32, saw at 64, pulse at 127     E1 PW   64 = a square, below: narrower, above: auto PWM
 *   E2 DRFT DCO drift                                       E3 VAR  per-voice component variance
 *   E4 CUT  the display law (CUTOFF_HZ)                     E5 RES  the J106 resonance curve
 *   E6 DRV  OTA input drive + the BA662's Q compensation    E7 KTR  keyboard tracking, C4 the zero point, 127 = 1:1
 *   ENV (P_ED_FLT) -> cutoff through voice.c, as OB-Z and WAVE (ownenv = 0).   P_FTYPE (0..3): HPF flat / bass boost / 236 Hz / 754 Hz.
 * Cost: the filter is most of it (a Newton step per stage; since 6.3 its small-signal series leaves one divide a sample, the OTA's, at
 * DRIFT_OS times the base rate; the exact step and its six stay for a loud or self-oscillating filter). */
#include "dsp_va.c"
#include "dsp_chorus.c"       /* the Juno chorus: one instance per part, after the voices (drift_part_fx) */

/* DRIFT_OS, DRIFT_POLY and DRIFT_CTRL are the plan's measured choices (docs/plans/2026-10-17-drift-juno.md "Measured result", "Task 6.3";
 * tests/drift_test.c (i)). OS 1: the brief's fallback ladder took 2 -> 1. POLY: 1 shipped (6.1); 3 since 6.3, once the ladder's
 * small-signal path, the coefficient k-cache and the blep's reciprocal (dsp_va.c, here; all of them exact to the donor within 1e-5) took
 * a marginal voice from 325 to 300 host instructions a sample and left the dearest legal four-track mix where it was without DRIFT.
 * CTRL: the coefficients are made every 8 base samples (5.5 kHz). 32 would save a further 10 % of a voice (mean marginal 300 -> 269;
 * 16: 290) at the price of about 9 dB more sideband on a bare sine under a deep, fast LFO on the cutoff (-55 -> -45 dB re the signal;
 * masked on a saw); that is a matter of ear, so it stays 8 and is this one number (CTL must divide by it). Raise them when the device's
 * PERF run (Task 8.1) shows the room, or lower them when it does not; every other line follows. */
#ifndef DRIFT_OS
#define DRIFT_OS 1               /* the filter's and the DCO's oversampling: 1 or 2 */
#endif
#ifndef DRIFT_POLY
#define DRIFT_POLY 3             /* voices a DRIFT part plays at most (POLY, UNISON) */
#endif
#ifndef DRIFT_CTRL
#define DRIFT_CTRL 8             /* base-rate samples between two filter coefficient updates (CTL / DRIFT_CTRL per block): 8, 16 or 32 */
#endif
/* test seam: the oscillator's output as the filter's input. tests/drift_test.c replaces it with silence, an impulse, a sine or
 * noise, to measure the filter alone; the product's is the identity */
#ifndef DRIFT_IN
#define DRIFT_IN(s) (s)
#endif
_Static_assert(DRIFT_OS == 1 || DRIFT_OS == 2, "DRIFT_OS: 1 or 2");
_Static_assert(CTL % DRIFT_CTRL == 0, "DRIFT_CTRL divides a control block");

#define DRIFT_FS 44100.0f
/* the DCO's gains: the donor's J106 set (KR106Oscillators.h kSawAmpJ106 / kPulseAmpJ106 / kSubAmpJ106) */
#define DRIFT_SAW_AMP 0.6f
#define DRIFT_PULSE_AMP 0.5f
#define DRIFT_SUB_AMP 0.75f
/* the pulse knob's range, KR106Voice.h: pw 0.5 .. 0.95, so PW 64 is a true square (odd harmonics only) */
#define DRIFT_PW_MIN 0.5f
#define DRIFT_PW_MAX 0.95f
#if DRIFT_OS == 2
#define DRIFT_DC1 5.6631e-5f     /* the DC blockers' g/(1+g), 1.59 Hz at the oversampled rate (the post-DCO one) ... */
#else
#define DRIFT_DC1 1.13255e-4f
#endif
#define DRIFT_DC2 1.13255e-4f    /* ... and at the base rate (the post-VCF one) */
#define DRIFT_OUT 1.7f           /* the voice's level into the part's sum: a saw at the donor's level reads about as loud as OB-Z's SAW LEAD */
#define DRIFT_SAW_RMS (0.6f * 0.57735027f)   /* the saw's rms, the reference the MIX law's loudness is held to */

/* the 4-position HPF (KR106_HPF.h getJuno106HPFFreq / BassBoostFilter), per voice at the base rate, linear and so the same as the
 * donor's bus filter. Position 2: 236 Hz, 3: 754 Hz, TPT one-pole g/(1+g) for 44.1 kHz. Position 1, the bass boost (+10.5 dB at
 * DC, +1.4 dB above 300 Hz): the donor's 2-pole 2-zero biquad (poles 59.41 / 72.34 Hz, zeros 169.98 / 72.02 Hz, from its component
 * values) as two first-order shelves in series, each (1 + s/wz) / (1 + s/wp) = a + (1 - a) lp, a = wp/wz, plus the DC gain 3.35:
 * float-safe at these low corners where a biquad's coefficients are not. */
#define DRIFT_HP236 0.016535708f
#define DRIFT_HP754 0.051021915f
#define DRIFT_BB_G1 0.00421431673f
#define DRIFT_BB_A1 0.349504025f
#define DRIFT_BB_G2 0.00512719856f
#define DRIFT_BB_A2 1.00454189f
#define DRIFT_BB_K 3.35f

typedef struct {                 /* per voice (engine memory) */
    va_ladder_t l;
#if DRIFT_OS == 2
    va_hb_t hb;                  /* the decimator */
#endif
    float pos;                   /* the DCO: phase */
    float dc1, dc2;              /* the post-DCO (oversampled) and post-VCF (base rate) DC blockers */
    float hp[2];                 /* the HPF / bass boost shelves */
    float walk[3];               /* drift: three slow sines' phases, in turns */
    float unit;                  /* drift: the static offset, [-1, 1] */
    float v_cut, v_pit, v_vca, v_wid, v_pwl, v_pwh;   /* variance, each in [-1, 1] (times VAR): cutoff, pitch, VCA, width, PW lo / hi */
    float pwm;                   /* the auto-PWM triangle's phase, turns */
    float peak;                  /* the oscillator's peak since the filter's follower last saw it */
    float hz_prev;               /* the cutoff the last block ended on (0 = none yet) */
    float fc;                    /* the cutoff the last block ended on, Hz (the tests read it) */
    uint8_t sub_state, init, pad[2];
} drift_vc_t;
#define DRIFT_MEM (sizeof(drift_vc_t) * DRIFT_POLY + sizeof(chorus_t))   /* the voices, then the part's chorus */
/* (ENG_MEM_FITS("DRIFT", DRIFT_MEM) is in engines.c, once eng_wave.c has defined VOID_MEM that ENG_MEM_PART needs) */

static drift_vc_t *drift_vc(const track_t *t, const voice_t *v)
{
    return (drift_vc_t *)ENG_MEM((uint32_t)(t - trk) % NPART) + (uint32_t)(v - t->v) % DRIFT_POLY;
}

/* the part's chorus (Task 6.2): the state after the voices in the same region, zero (OFF) when the engine claims it */
static chorus_t *drift_chorus(const track_t *t)
{
    return (chorus_t *)((drift_vc_t *)ENG_MEM((uint32_t)(t - trk) % NPART) + DRIFT_POLY);
}

/* 2^x for the pitch and the key tracking, once a block each, as e^(x ln 2): fastmath.h's fm_expf (rel 6e-7). Not fm_exp2f, though it is the
 * same thing a few operations cheaper: ACID's acid_hz calls fm_exp2f, and a second engine calling it from a hot function makes the
 * compiler inline it into acid_render, which changes that function's code and tests/target_budget.py's count of it (86 -> 3623, an
 * artefact: the count cannot decode FPU opcodes) for no gain here */
static inline float drift_exp2f(float x) { return fm_expf(x * FM_LN2); }

/* the 4th-order polyBLEP residual of a step of height 2 (KR106Oscillators.h PolyBLEP): t the phase [0, 1), dt the increment.
 * 4 samples wide (2 before and 2 after the transition). Derived from the integrated cubic B-spline. rdt = 1 / dt, made once a block by the
 * caller: a divide per window hit (up to four a sample at a high note) is dearer than the multiply it replaces */
static inline float drift_blep(float t, float dt, float rdt)
{
    float dt2 = dt + dt;
    if (t < dt) {
        float n = t * rdt, n2 = n * n;
        return 0.25f * n2 * n2 - 0.66666667f * n2 * n + 1.33333333f * n - 1.0f;
    } else if (t < dt2) {
        float u = 1.0f - (t - dt) * rdt, u2 = u * u;
        return -0.08333333f * u2 * u2;
    } else if (t > 1.0f - dt) {
        float n = (t - 1.0f) * rdt, n2 = n * n;
        return -0.25f * n2 * n2 - 0.66666667f * n2 * n + 1.33333333f * n + 1.0f;
    } else if (t > 1.0f - dt2) {
        float u = 1.0f + (t - 1.0f + dt) * rdt, u2 = u * u;
        return 0.08333333f * u2 * u2;
    }
    return 0.0f;
}

/* the voice's deterministic offsets (KR106Voice.h InitVariance: an LCG seeded by the voice, the same every time, one unit's
 * fixed component tolerances) */
static float drift_rng(uint32_t *seed)
{
    *seed = *seed * 196314165u + 907633515u;
    return (float)(int32_t)*seed * (1.0f / 2147483648.0f);
}

static void drift_note_on(track_t *t, voice_t *v)
{
    drift_vc_t *c = drift_vc(t, v);
    if (!v->env || !c->init) {                           /* a fresh voice (voice.c leaves env 0 only then); a retriggered one keeps its states */
        uint32_t i, seed, vi = (uint32_t)(v - t->v) % DRIFT_POLY, part = (uint32_t)(t - trk) % NPART;
        float res = (float)clamp(t->p[P_E5], 0, 127) * (1.0f / 127.0f);
        uint32_t *w = (uint32_t *)c;
        for (i = 0; i < sizeof *c / 4u; i++)
            w[i] = 0;
        va_ladder_reset(&c->l);
        va_ladder_setup(&c->l, DRIFT_OS, DRIFT_CTRL, DRIFT_FS);
        /* the donor's filter startup: s[0] seeded to approximate the steady-state self-oscillation, an instant resonant character
         * without waiting for the thermal noise to build up */
        c->l.s[0] = res > 0.7f ? res - 0.7f : 0.0f;
        seed = (vi + 1u + 4u * part) * 2654435761u + 0x46756E6Bu;
        c->v_cut = drift_rng(&seed);
        c->v_pit = drift_rng(&seed);
        c->v_vca = drift_rng(&seed);
        c->v_wid = drift_rng(&seed);
        c->v_pwl = drift_rng(&seed);
        c->v_pwh = drift_rng(&seed);
        c->unit = drift_rng(&seed) + drift_rng(&seed) + drift_rng(&seed);         /* the donor's sigma-1 sample (three uniforms), */
        c->unit = c->unit < -1.0f ? -1.0f : c->unit > 1.0f ? 1.0f : c->unit;          /* held to [-1, 1]: 5 cents at most */
        for (i = 0; i < 3u; i++)
            c->walk[i] = 0.5f * (drift_rng(&seed) + 1.0f);
        c->init = 1;
    }
}

/* the piecewise MIX law (the plan's table): 0 sub, 64 saw, 127 pulse */
static inline float drift_clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

/* the OTA input drive: a soft clip, linear to 0.66 (so DRV 0 and the quiet end of the knob are the donor's filter exactly), then the
 * last 0.34 of headroom taken by t - t^2/4 (slope 1 at the knee, 0 at the ceiling: smooth, no divide) */
static inline float drift_drive(float u)
{
    float a = u < 0.0f ? -u : u, t;
    if (a <= 0.66f)
        return u;
    t = (a - 0.66f) * (1.0f / 0.34f);
    t = t < 2.0f ? t * (1.0f - 0.25f * t) : 1.0f;
    a = 0.66f + 0.34f * t;
    return u < 0.0f ? -a : a;
}

static void drift_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    drift_vc_t *c = drift_vc(t, v);
    const int16_t *p = t->p;
    int32_t mixp = clamp(p[P_E0], 0, 127), pwp = clamp(p[P_E1], 0, 127), cutq, drvp = clamp(p[P_E6], 0, 127);
    float drft = (float)clamp(p[P_E2], 0, 127) * (1.0f / 127.0f), var = (float)clamp(p[P_E3], 0, 127) * (1.0f / 127.0f);
    float ktr = (float)clamp(p[P_E7], 0, 127) * (1.0f / 127.0f), res = (float)clamp(p[P_E5], 0, 127) * (1.0f / 127.0f);
    float gsaw, gpul, gsub, pw, pwn, effpw, cps, hz, hz0, dhz, cents, drive_g = 1.0f, drive_n = 1.0f;
    float amp, damp, lvl;
    int ft = p[P_FTYPE] < 0 || p[P_FTYPE] > 3 ? 0 : p[P_FTYPE];   /* (the track-persistent value of another engine's filter, 4 HP24 / 5 NOTCH: flat, not a hidden cut) */
    uint32_t i, seg, o, nseg;
    int32_t ci;
    if (n > CTL)
        n = CTL;
    n -= n % DRIFT_CTRL;                                 /* whole control segments only (n is CTL in the product; this keeps damp / the tail aligned if not) */
    if (!c->init || n < DRIFT_CTRL)
        return;
    /* ---- the MIX law ---- */
    {
        float e = (float)mixp;
        gsub = drift_clamp01((64.0f - e) * (1.0f / 48.0f));
        gsaw = mixp <= 64 ? drift_clamp01((e - 8.0f) * (1.0f / 56.0f)) : (127.0f - e) * (1.0f / 63.0f);
        gpul = mixp <= 64 ? 0.0f : (e - 64.0f) * (1.0f / 63.0f);
        gsaw *= DRIFT_SAW_AMP;
        gpul *= DRIFT_PULSE_AMP;
        gsub *= DRIFT_SUB_AMP;
    }
    /* ---- the pulse width: the knob, the auto PWM above 64, SHAPE (LFO / ENV / matrix), the variance ---- */
    c->pwm += (float)n * (0.8f / DRIFT_FS);
    c->pwm -= c->pwm >= 1.0f ? 1.0f : 0.0f;
    {
        float tri = 1.0f - 4.0f * (c->pwm < 0.5f ? 0.5f - c->pwm : c->pwm - 0.5f);      /* -1 .. 1 */
        float pwmin = DRIFT_PW_MIN + 0.02f * var * c->v_pwl, pwmax = DRIFT_PW_MAX + 0.02f * var * c->v_pwh;
        pwn = pwp < 64 ? (float)(64 - pwp) * (1.0f / 64.0f) : (float)(pwp - 64) * (1.0f / 63.0f) * 0.5f * (1.0f + tri);
        pwn += (float)(m->shape - (64 << 8)) * (1.0f / (127.0f * 256.0f));
        pwn = drift_clamp01(pwn);
        pw = pwmin + pwn * (pwmax - pwmin);
    }
    effpw = 1.0f - pw;                                    /* J106: the inverted duty cycle (KR106Oscillators.h mPulseInvert) */
    effpw = effpw < 0.01f ? 0.01f : effpw > 0.99f ? 0.99f : effpw;
    /* ---- the pitch: the note, the drift (a static offset and three slow sines, at most 8 cents at DRFT 127), the variance ---- */
    c->walk[0] += (float)n * (0.07f / DRIFT_FS);
    c->walk[1] += (float)n * (0.13f / DRIFT_FS);
    c->walk[2] += (float)n * (0.31f / DRIFT_FS);
    for (i = 0; i < 3u; i++)
        c->walk[i] -= c->walk[i] >= 1.0f ? 1.0f : 0.0f;
    cents = drft * (5.0f * c->unit + (fm_sin_turns(c->walk[0]) + fm_sin_turns(c->walk[1]) + fm_sin_turns(c->walk[2])) * 1.0f) + var * 3.0f * c->v_pit;
    {
        float e = (float)(m->pitch16 - 69 * 16) * (1.0f / 192.0f) + cents * (1.0f / 1200.0f);
        if (t->p[P_FM4_DEC])                       /* a KR-106 preset's octave switch (DRIFT_LX[7]: 0 none, else - 3 octaves) */
            e += (float)(clamp(t->p[P_FM4_DEC], 1, 5) - 3);
        hz = 440.0f * drift_exp2f(e);
        if (m->fine)
            hz *= 1.0f + (float)m->fine * (1.0f / 4096.0f);
    }
    cps = hz * (1.0f / (DRIFT_FS * (float)DRIFT_OS));
    cps = cps > 0.25f ? 0.25f : cps;
    /* ---- the cutoff: CUT and what ENV / LFO / the matrix added, on the display law, times keyboard tracking (C4 the zero point,
     * 127 = one octave per octave) and the variance, over the block from where the last one ended ---- */
    cutq = clamp((int32_t)p[P_E4] * 256 + m->cutoff, 0, 127 * 256);
    ci = cutq >> 8;
    {
        float a = (float)CUTOFF_HZ[ci], b = (float)CUTOFF_HZ[ci < 127 ? ci + 1 : 127], oct;
        hz0 = a + (b - a) * (float)(cutq & 255) * (1.0f / 256.0f);
        oct = (float)(v->pitch_cur - 60 * 16) * (1.0f / 192.0f) * ktr * (1.0f + var * 0.0083f * c->v_wid);
        hz0 *= drift_exp2f(oct + var * 0.07f * c->v_cut);
        hz0 = hz0 < 20.0f ? 20.0f : hz0 > 0.82f * (DRIFT_FS * 0.5f) ? 0.82f * (DRIFT_FS * 0.5f) : hz0;
    }
    if (c->hz_prev <= 0.0f)
        c->hz_prev = hz0;
    nseg = n / DRIFT_CTRL;
    dhz = (hz0 - c->hz_prev) * (1.0f / (float)nseg);
    hz = c->hz_prev;
    c->hz_prev = hz0;
    c->fc = hz0;
    /* ---- the drive: the OTA's input gain and the BA662's Q compensation, with a matching trim ---- */
    if (drvp) {
        float d = (float)drvp * (1.0f / 127.0f), g;
        g = (1.0f + 24.0f * d * d) * (1.0f + 0.5f * res * res * d);
        drive_g = g;
        drive_n = 1.0f / (1.0f + 0.02f * (g - 1.0f));
    }
    /* ---- the amplitude: voice.c's ramp (the ADSR, velocity, LFO, the fade), the variance, the level ---- */
    {   /* MIX loudness: the sub (peak-matched to the saw in the donor, so 6 dB louder by rms) and the pulse are brought down to the
         * saw's rms, after the filter, so the OTA still sees the donor's levels. The three are taken as uncorrelated; never a boost */
        float r1 = gsaw * 0.57735027f, r2 = gpul, r3 = gsub, rms = fm_sqrtf(r1 * r1 + r2 * r2 + r3 * r3);
        lvl = DRIFT_OUT * (rms > DRIFT_SAW_RMS ? DRIFT_SAW_RMS / rms : 1.0f);
    }
    lvl *= 1.0f + 0.06f * var * c->v_vca;
    amp = (float)m->amp0 * (1.0f / 32768.0f);
    damp = ((float)m->amp1 * (1.0f / 32768.0f) - amp) * (1.0f / (float)n);
    {
        /* three passes a segment (DRIFT_CTRL base samples, the filter's coefficients constant): the DCO and the input stage into xin[],
         * the ladder over it (its state in registers: dsp_va.c va_ladder_run), then the decimator, the DC blocker, the HPF and the
         * amplitude into out[]. The state is in locals: out[] and the voice are reached through pointers, and the compiler would
         * otherwise reload them after every store */
        va_ladder_t *l = &c->l;
        float xin[DRIFT_CTRL * DRIFT_OS], yout[DRIFT_CTRL * DRIFT_OS];
        float pos = c->pos, dc1 = c->dc1, dc2 = c->dc2, hp0 = c->hp[0], hp1 = c->hp[1], peak = c->peak;
        int sub_state = c->sub_state;
        const float dt2 = cps + cps, hi = 1.0f - dt2, rcps = 1.0f / cps;
        uint32_t k = 0;
        for (seg = 0; seg < nseg; seg++) {
            va_ladder_env(l, peak);
            peak = 0.0f;
            hz += dhz;
            va_ladder_coeffs(l, hz, res, DRIFT_FS);
            for (i = 0; i < DRIFT_CTRL * DRIFT_OS; i++) {
                float s = 0.0f, bl = 0.0f, a;
                pos += cps;
                if (pos >= 1.0f) {
                    pos -= 1.0f;
                    sub_state ^= 1;
                }
                if (pos < dt2 || pos > hi)
                    bl = drift_blep(pos, cps, rcps);
                if (gsaw != 0.0f)                        /* saw: the linear ramp and the reset blep */
                    s += (2.0f * pos - 1.0f - bl) * gsaw;
                if (gpul != 0.0f) {                      /* pulse: the comparator on the ramp, a blep at each edge */
                    float pw2 = pos - effpw, b2 = 0.0f;
                    pw2 += pw2 < 0.0f ? 1.0f : 0.0f;
                    if (pw2 < dt2 || pw2 > hi)
                        b2 = drift_blep(pw2, cps, rcps);
                    s += ((pos < effpw ? -1.0f : 1.0f) + b2 - bl) * gpul;
                }
                if (gsub != 0.0f)                        /* sub: the CD4013's half-rate square, its transitions smoothed by the blep */
                    s += (sub_state ? -1.0f : 1.0f) * (1.0f - (bl < 0.0f ? -bl : bl)) * gsub;
                s = DRIFT_IN(s);
                a = s < 0.0f ? -s : s;
                peak = a > peak ? a : peak;
                {   /* the post-DCO DC blocker: the coupling cap (TPT one-pole high-pass) */
                    float v1 = (s - dc1) * DRIFT_DC1, lp1 = dc1 + v1;
                    dc1 = lp1 + v1;
                    s -= lp1;
                }
                if (drvp)
                    s = drift_drive(s * drive_g) * drive_n;
                xin[i] = s;
            }
            va_ladder_run(l, xin, yout, DRIFT_CTRL * DRIFT_OS);
            for (i = 0; i < DRIFT_CTRL; i++, k++) {
                float s;
#if DRIFT_OS == 2
                s = va_hb_down(&c->hb, yout[2 * i], yout[2 * i + 1]);
#else
                s = yout[i];
#endif
                {   /* the post-VCF DC blocker, then the HPF */
                    float v2 = (s - dc2) * DRIFT_DC2, lp2 = dc2 + v2;
                    dc2 = lp2 + v2;
                    s -= lp2;
                }
                if (ft == 1) {
                    float v1 = (s - hp0) * DRIFT_BB_G1, l1 = hp0 + v1, x1, v2, l2;
                    hp0 = l1 + v1;
                    x1 = DRIFT_BB_A1 * s + (1.0f - DRIFT_BB_A1) * l1;
                    v2 = (x1 - hp1) * DRIFT_BB_G2;
                    l2 = hp1 + v2;
                    hp1 = l2 + v2;
                    s = DRIFT_BB_K * (DRIFT_BB_A2 * x1 + (1.0f - DRIFT_BB_A2) * l2);
                } else if (ft >= 2) {
                    float g = ft == 2 ? DRIFT_HP236 : DRIFT_HP754, v1 = (s - hp0) * g, l1 = hp0 + v1;
                    hp0 = l1 + v1;
                    s -= l1;
                }
                amp += damp;
                s *= amp * lvl;
                if (!(s > -2.0f && s < 2.0f))            /* a runaway or a NaN never reaches the integer conversion */
                    s = s >= 2.0f ? 2.0f : s <= -2.0f ? -2.0f : 0.0f;
                out[k] += (int32_t)(s * 32768.0f);
            }
        }
        c->peak = peak;
        c->pos = pos;
        c->dc1 = dc1;
        c->dc2 = dc2;
        c->hp[0] = hp0;
        c->hp[1] = hp1;
        c->sub_state = (uint8_t)sub_state;
    }
}

/* The chorus: the part's stereo stage (engine_t.part_fx, fx.c mix_part), on the sum of the voices. P_RSV_LN is its mode (CHOR: OFF I II I+II;
 * the first of the reserved ids, params.c). A value outside 0..3, such as the 127 an older project stores there, is OFF. */
static int drift_part_fx(track_t *t, int32_t *b, int32_t *z, uint32_t n)
{
    return chorus_part(drift_chorus(t), t->p[P_RSV_LN], b, z, n);
}

/* a preset just loaded on track t (ui.c apply_preset_to, after the defaults and the preset's own values; tests/hostsim.c): its chorus */
_Static_assert(NELEM(DRIFT_PX) == NELEM(DRIFT_PRESETS), "DRIFT_PX: one per preset");
_Static_assert(NELEM(DRIFT_LX) == NELEM(DRIFT_PRESETS), "DRIFT_LX: one per preset");
static void drift_track_loaded(track_t *t)
{
    const int8_t *x;
    if (t->eng_req % NENGINES != ENGI_DRIFT || t->preset >= NELEM(DRIFT_PX))
        return;
    t->p[P_RSV_LN] = DRIFT_PX[t->preset];
    x = DRIFT_LX[t->preset];                       /* (2026-11-19) the KR-106 presets' HPF and LFO; zeros: the defaults, */
    t->p[P_FTYPE] = x[0];                          /* the 32 seeds' sound unchanged */
    if (x[1]) {
        t->p[P_LRATE] = x[2], t->p[P_LWAVE] = x[3], t->p[P_LFADE] = x[4], t->p[P_LTRIG] = 1;
    }
    t->p[P_LD_PIT] = x[5];
    t->p[P_LD_FLT] = x[6];
    t->p[P_FM4_DEC] = x[7];                        /* the octave switch (retired DIGITAL operator 4's DEC, no page) */
}

static const engine_t ENG_DRIFT = {
    .name = "JUNO", .page_title = {"OSC", "FLT"},
    .edit = {
        {"MIX", F_PCT, 0, 127, 64, 0, 0},           {"PW", F_PCT, 0, 127, 64, 0, 0},
        {"DRFT", F_PCT, 0, 127, 20, 0, 0},          {"VAR", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},            {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = DRIFT_PRESETS, .npresets = NELEM(DRIFT_PRESETS),   /* presets/four/drift.py */
    .knob = {P_E4, P_E5, P_ED_FLT, P_E6}, .poly = DRIFT_POLY, .keep = 0,
    .note_on = drift_note_on, .render = drift_render,
    .mem = DRIFT_MEM,
    .part_fx = drift_part_fx,
};
