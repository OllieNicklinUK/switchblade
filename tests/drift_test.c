/* SPDX-License-Identifier: GPL-3.0-only */
/* drift_test.c: DRIFT, the Juno voice (eng_drift.c, dsp_va.c; Ultramaster KR-106, GPL-3.0), in two tiers.
 *   cc -O2 -w -ffp-contract=off -Ibuild/gen -Ifirmware/src -Itests -o build/host/drift_test tests/drift_test.c -lm
 *   build/host/drift_test [build/drift_ref]
 * Tier 1 holds the lifted filter tight: dsp_va.c's va_ladder is the donor's algorithm at the donor's precision, so it must
 * agree with the donor's own kr106::VCF almost exactly. Tier 2 holds the adapted voice (the oscillator, the control rate, the
 * parameter mapping are ours) to its behaviour, through the real voice path (trk_note_on, track_render).
 *   (a) every case of drift_ref_cfg.h: max |y - y_ref| / max |y_ref| < 1e-4 against build/drift_ref/NN.f32 (tests/drift_ref.cpp);
 *   (b) 24 dB/oct: the magnitude at 4 x cutoff is >= 20 dB under the passband;
 *   (c) DRV 0 -> 127 takes the THD of a 200 Hz sine through the open filter from < 1 % to > 10 %;
 *   (d) RES 127 self-oscillates within +-3 % of the cutoff;
 *   (e) DRFT 0: two renders byte-identical; DRFT 127: a slow wander within +-8 cents;
 *   (f) VAR 0: every voice has the same cutoff; VAR 127: measurably different per voice;
 *   (g) MIX: sub at 0, saw at 64, pulse at 127, each by its spectrum;
 *   (h) P_FTYPE moves the HPF corner across its four positions;
 *   (h) also: P_FTYPE above DRIFT's four positions is the flat one, never a hidden cut;
 *   (i) CPU: the DRIFT part inside its pin (+10 % of its measured worst, -O2); the dearest legal four-track mix is measured and
 *       printed, not asserted (the 80 % claim is Task 8.1's, on the device);
 *   (j) the donor's linear ramp bandlimited: inharmonic energy at MIDI 72 / 84 / 96 below -50 dB;
 *   (k) the DCO's spectrum against the ideal waveform of the donor's amplitudes and the PW law (amplitudes, PW, polarity, MIX);
 *   (l) the auto PWM swings the pulse's 2nd harmonic; (m) KTR: 1 octave per octave at 127, none at 0, C4 the zero point;
 *   (n) the two 1.59 Hz DC blockers: -6 dB at 1.59 Hz; (o) a new note on a finished voice starts clean, a retrigger keeps the states;
 * plus the halfband pair (gain, image rejection) and a bounded / finite sweep of the corners.
 * The DRIFT_IN seam (eng_drift.c) replaces the oscillator by silence, an impulse, a sine or noise, to measure the filter alone. */
#include "host_pins.h"   /* (V2) per-host cost pins */
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "dsp_va.c"
#include "drift_ref_cfg.h"
enum { INJ_OFF, INJ_SILENCE, INJ_IMPULSE, INJ_SINE, INJ_NOISE, INJ_SQUARE };
static int inj_mode, inj_pending;
static float inj_amp, inj_step;
static double inj_phase;
static uint32_t inj_rng = 0x2545F491u;
static uint32_t inj_t, inj_n, inj_half = 1, inj_blk = 1, inj_os = 1;   /* INJ_SQUARE: the sample count (set per block), the call within the block (per voice), the half period */
static float drift_in(float s)
{
    switch (inj_mode) {
    case INJ_SILENCE:
        return 0.0f;
    case INJ_IMPULSE:
        s = inj_pending ? inj_amp : 0.0f;
        inj_pending = 0;
        return s;
    case INJ_SINE:
        inj_phase += (double)inj_step;
        return inj_amp * (float)sin(inj_phase);
    case INJ_SQUARE:                               /* the same square in every voice (test_headroom): CTL calls a voice and block at DRIFT_OS 1 */
        return (((inj_t + inj_n++ % inj_blk / inj_os) / inj_half) & 1u) ? inj_amp : -inj_amp;
    case INJ_NOISE:
        inj_rng = inj_rng * 1664525u + 1013904223u;
        return inj_amp * ((float)(int32_t)inj_rng * (1.0f / 2147483648.0f));
    default:
        return s;
    }
}
#define DRIFT_IN(s) drift_in(s)
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "dspfft.h"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    printf("drift: %s ", ok ? "ok  " : "FAIL");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    bad += !ok;
}

#define FSF 44100.0f
#define FSD 44100.0

/* ------------------------------------------------------------ harness --- */
static track_t *T;
static void drift_fresh(void)                      /* part 0: DRIFT, POLY, dry, the amp envelope open, the engine's state zero */
{
    memset(trk, 0, sizeof trk);
    memset(&mod, 0, sizeof mod);
    vage = 0;
    rng_state = 0x1234567u;
    mod_seed = 0x2545F491u;
    inj_mode = INJ_OFF;
    inj_pending = 0;
    inj_phase = 0.0;
    inj_rng = 0x2545F491u;
    host_tracks_init();
    memset(eng_mem[0].b, 0, sizeof eng_mem[0].b);
    eng_mem_owner[0] = 0;
    host_preset(&trk[0], ENGI_DRIFT, 0);
    T = &trk[0];
    T->p[P_VOICE] = V_POLY;
    T->p[P_DIST] = T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 0;
    T->p[P_LD_AMP] = T->p[P_LD_FLT] = T->p[P_LD_PIT] = T->p[P_ED_PIT] = T->p[P_LD_SHP] = T->p[P_ED_SHP] = 0;
    T->p[P_ED_FLT] = 0;
    T->p[P_GLIDE] = 0;
    T->p[P_FTYPE] = 0;
    T->p[P_ATK] = 0;
    T->p[P_SUS] = 127;
    T->slide_glide = 0;
}

/* MIX, PW, DRFT, VAR, CUT, RES, DRV, KTR */
static void pots(int mix, int pw, int drft, int var, int cut, int res, int drv, int ktr)
{
    T->p[P_E0] = (int16_t)mix;
    T->p[P_E1] = (int16_t)pw;
    T->p[P_E2] = (int16_t)drft;
    T->p[P_E3] = (int16_t)var;
    T->p[P_E4] = (int16_t)cut;
    T->p[P_E5] = (int16_t)res;
    T->p[P_E6] = (int16_t)drv;
    T->p[P_E7] = (int16_t)ktr;
}

static int32_t o_blk[CTL];
static void run(int32_t *out, unsigned n)          /* n samples (a multiple of CTL) of the part into out */
{
    unsigned b, i;
    for (b = 0; b < n / CTL; b++) {
        track_render(T, o_blk, CTL);
        if (out)
            for (i = 0; i < CTL; i++)
                out[b * CTL + i] = o_blk[i];
    }
}

static drift_vc_t *vc_of(unsigned i) { return (drift_vc_t *)ENG_MEM(0) + i; }

static const char *refdir = "build/drift_ref";

/* ------------------------------------------------------------ analysis --- */
#define NFFT 16384u
static float mag_[NFFT / 2];
static double psd_[NFFT / 2];
/* the Welch mean of |X|^2 over nseg segments of NFFT */
static void psd_of(const int32_t *x, unsigned nseg)
{
    unsigned s, k;
    for (k = 0; k < NFFT / 2u; k++)
        psd_[k] = 0.0;
    for (s = 0; s < nseg; s++) {
        fft_mag(x + (size_t)s * NFFT, NFFT, mag_);
        for (k = 0; k < NFFT / 2u; k++)
            psd_[k] += (double)mag_[k] * mag_[k] / nseg;
    }
}
static double psd_db(double lo, double hi)         /* mean power per bin of the band, dB */
{
    unsigned k0 = (unsigned)ceil(lo * NFFT / FSD), k1 = (unsigned)floor(hi * NFFT / FSD), k;
    double e = 0.0;
    if (k1 >= NFFT / 2u)
        k1 = NFFT / 2u - 1u;
    if (k0 > k1)
        k0 = k1;
    for (k = k0; k <= k1; k++)
        e += psd_[k];
    return 10.0 * log10(e / (k1 - k0 + 1u) + 1e-30);
}
/* the largest magnitude within +-3 bins of hz, dB (mag_ of the last fft_mag of n samples) */
static double peak_db(const float *mag, unsigned n, double hz)
{
    int c = (int)(hz * n / FSD + 0.5), k;
    double m = 0.0;
    for (k = c - 3; k <= c + 3; k++)
        if (k > 0 && k < (int)(n / 2u) && mag[k] > m)
            m = mag[k];
    return 20.0 * log10(m + 1e-30);
}
static double pk_hz(const float *mag, unsigned n, double lo, double hi)   /* the strongest partial, parabolic */
{
    unsigned k0 = (unsigned)(lo * n / FSD), k1 = (unsigned)(hi * n / FSD), k, kb = k0;
    double a, b, c, d;
    for (k = k0; k <= k1; k++)
        if (mag[k] > mag[kb])
            kb = k;
    a = log((double)mag[kb - 1] + 1e-30);
    b = log((double)mag[kb] + 1e-30);
    c = log((double)mag[kb + 1] + 1e-30);
    d = a - 2.0 * b + c;
    return ((double)kb + (d != 0.0 ? 0.5 * (a - c) / d : 0.0)) * FSD / n;
}

static uint32_t cut_for_hz(uint32_t hz)            /* the CUT index whose displayed frequency is nearest hz */
{
    uint32_t k, best = 0;
    for (k = 0; k < 128u; k++) {
        int32_t a = (int32_t)CUTOFF_HZ[k] - (int32_t)hz, b = (int32_t)CUTOFF_HZ[best] - (int32_t)hz;
        if ((a < 0 ? -a : a) < (b < 0 ? -b : b))
            best = k;
    }
    return best;
}

static int32_t buf[NFFT * 12u];                    /* 196608 samples, 4.5 s */
/* a note held with the seam set, rendered: skip the first `skip` samples, then n into buf */
static void play(unsigned note, unsigned skip, unsigned n)
{
    trk_note_on(T, note, 100);                     /* (VEL 100: above 110 voice.c adds the accent's cutoff boost) */
    run(0, skip);
    run(buf, n);
}

/* ------------------------------------------------------------ (a) --- */
static void test_ladder_ref(void)
{
    static float ref[DRIFT_REF_N * 4];
    static float got[DRIFT_REF_N * 4];
    int c, nok = 0, nrun = 0;
    double worst = 0.0;
    int worst_c = -1;
    for (c = 0; c < DRIFT_REF_NCFG; c++) {
        const drift_ref_cfg_t *k = &DRIFT_REF_CFG[c];
        char path[512];
        FILE *f;
        va_ladder_t l;
        float peak = 0.0f;
        size_t n = (size_t)DRIFT_REF_N * k->os, i, got_n;
        double e = 0.0, m = 0.0;
        unsigned s, j;
        snprintf(path, sizeof path, "%s/%02d.f32", refdir, c);
        if (!(f = fopen(path, "rb")))
            continue;
        got_n = fread(ref, sizeof(float), n, f);
        fclose(f);
        if (got_n != n)
            continue;
        memset(&l, 0, sizeof l);
        va_ladder_reset(&l);
        va_ladder_setup(&l, k->os, 1, FSF);
        for (s = 0; s < DRIFT_REF_N; s++) {
            va_ladder_env(&l, peak);
            va_ladder_coeffs(&l, k->hz, k->res, FSF);
            peak = 0.0f;
            for (j = 0; j < k->os; j++) {
                float x = (float)drift_ref_input(s * k->os + j, k->f0, k->os), a = x < 0.0f ? -x : x;
                peak = a > peak ? a : peak;
                got[(size_t)s * k->os + j] = va_ladder_tick(&l, x * k->drv);
            }
        }
        for (i = 0; i < n; i++) {
            double d = fabs((double)got[i] - (double)ref[i]), r = fabs((double)ref[i]);
            e = d > e ? d : e;
            m = r > m ? r : m;
        }
        nrun++;
        if (m > 0.0 && e / m < 1e-4)
            nok++;
        if (m > 0.0 && e / m > worst) {
            worst = e / m;
            worst_c = c;
        }
        if (!(m > 0.0 && e / m < 1e-4))
            printf("drift:   case %2d (%.0f Hz res %.2f drv %.0f os %d): rel err %.3g\n", c, k->hz, k->res, k->drv, k->os,
                   m > 0.0 ? e / m : 1.0);
    }
    if (!nrun) {
        printf("drift: SKIP (a) NOT CHECKED: no reference files in %s (build tests/drift_ref.cpp against the donor, see its header)\n",
               refdir);
        return;
    }
    check(nrun == DRIFT_REF_NCFG && nok == nrun,
          "(a) va_ladder against the donor's kr106::VCF: %d of %d cases (of %d) under 1e-4 of the peak, worst %.3g (case %d)", nok, nrun,
          DRIFT_REF_NCFG, worst, worst_c);
}

/* ------------------------------------------------------------ (a2) the exact optimisations (Task 6.3) --- */
/* The k-cache in va_ladder_coeffs only skips what a repeat of the same k would recompute, so it is exact: after any walk of (cutoff,
 * resonance) every coefficient equals, bit for bit, what a filter that had never seen anything else computes for the final pair. A
 * wrong invalidation (the resonance moved but the k-parts kept; the 1x fade at the top of the range) shows as a differing field. */
static int coef_same(const va_ladder_t *a, const va_ladder_t *b)
{
    return a->k == b->k && a->g == b->g && a->g1 == b->g1 && a->g1_2 == b->g1_2 && a->g1_3 == b->g1_3 && a->G == b->G && a->inv_den == b->inv_den &&
           a->fb_scale == b->fb_scale && a->inv_fb == b->inv_fb && a->c0 == b->c0 && a->c1 == b->c1 && a->c2 == b->c2 && a->c3 == b->c3 && a->xa == b->xa &&
           a->xg == b->xg && a->ko == b->ko && a->noise_lvl == b->noise_lvl && a->ce == b->ce && a->h0 == b->h0 && a->tk == b->tk && a->c1s == b->c1s &&
           a->c2s == b->c2s && a->c3s == b->c3s;
}
static void test_ladder_cache(void)
{
    static const float RES[8] = {0.0f, 0.3f, 0.55f, 0.7f, 0.8f, 0.9f, 0.97f, 1.0f};
    uint32_t r = 0x2545F491u;
    unsigned os, n, bad = 0, steps = 0;
    for (os = 1; os <= 2u; os++) {
        va_ladder_t a;
        float res = 0.5f;
        memset(&a, 0, sizeof a);
        va_ladder_reset(&a);
        va_ladder_setup(&a, (int)os, 8, FSF);
        for (n = 0; n < 6000u; n++) {
            va_ladder_t b;
            float hz;
            r = r * 1664525u + 1013904223u;
            hz = 25.0f * powf(2.0f, (float)(r >> 8) * (1.0f / 16777216.0f) * 10.2f);          /* 25 Hz .. 22 kHz: the 1x fade zone is in it */
            if ((r & 7u) == 0)                                                                  /* a resonance move one time in eight, else a sweep */
                res = RES[(r >> 4) & 7u];
            va_ladder_coeffs(&a, hz, res, FSF);
            memset(&b, 0, sizeof b);
            va_ladder_reset(&b);
            va_ladder_setup(&b, (int)os, 8, FSF);
            va_ladder_coeffs(&b, hz, res, FSF);
            bad += !coef_same(&a, &b);
            steps++;
        }
    }
    check(bad == 0, "(a2) the coefficient k-cache is exact: %u of %u steps of a cutoff sweep with the resonance moving (OS 1 and 2, up to the 1x fade) equal a cold filter's, bit for bit", steps - bad, steps);
}

/* va_ladder_run writes back only the four integrators and the noise seed (nothing else changes in a tick): over several blocks it equals the same
 * number of va_ladder_tick calls, outputs and the whole state, bit for bit */
static void test_ladder_run(void)
{
    va_ladder_t a, b;
    float x[32], ya[32], yb[32];
    uint32_t r = 0x9E3779B9u, blk, i;
    int same = 1;
    memset(&a, 0, sizeof a);
    va_ladder_reset(&a);
    va_ladder_setup(&a, 1, 8, FSF);
    b = a;
    for (blk = 0; blk < 200u; blk++) {
        for (i = 0; i < 32u; i++) {
            r = r * 1664525u + 1013904223u;
            x[i] = (0.4f + (float)(blk % 5u)) * ((float)(int32_t)r * (1.0f / 2147483648.0f));
        }
        va_ladder_coeffs(&a, 300.0f + 40.0f * (float)blk, 0.3f + 0.003f * (float)blk, FSF);
        b = a;
        va_ladder_run(&a, x, ya, 32);
        for (i = 0; i < 32u; i++)
            yb[i] = va_ladder_tick(&b, x[i]);
        same &= !memcmp(ya, yb, sizeof ya) && !memcmp(&a, &b, sizeof a);
    }
    check(same, "(a2) va_ladder_run is 32 calls of va_ladder_tick: outputs and the whole state (integrators, noise seed, coefficients) equal bit for bit over 200 blocks");
}

/* The small-signal path of va_ladder_tick against the exact tick it replaces (the arithmetic of the tick before 6.3, kept here as the
 * reference): the same input and coefficients through both, over levels, cutoffs and resonances short of self-oscillation. The fast
 * path's series is within 4.8e-5 of the exact step, a small term of the output: the output must agree within 1.5e-5 of its peak. */
static float ref_tick(va_ladder_t *f, float x)
{
    float S, u, amp, q, sq, g1nl, gnl, lp;
    union { uint32_t u; float f; } n;
    f->seed = f->seed * 196314165u + 907633515u;
    n.u = (f->seed >> 9) | 0x40000000u;
    x += (n.f - 3.0f) * f->noise_lvl;
    f->s[0] += 1e-20f;
    S = f->s[0] * f->c0 + f->s[1] * f->c1 + f->s[2] * f->c2 + f->s[3] * f->c3;
    u = x * f->xg + va_ota_sat(x * f->xa - S) * f->ko;
    amp = fm_fabsf(f->s[3]);
    if (amp < 0.1f)
        sq = 1.0f + 0.3f * amp * amp;
    else {
        q = 1.0f + 0.6f * amp * amp;
        sq = q * fm_rsqrtf(q);
    }
    g1nl = f->g1 * (sq < 1.53846154f ? sq : 1.53846154f);
    g1nl = g1nl < 0.98f ? g1nl : 0.98f;
    gnl = g1nl / (1.0f - g1nl);
    {
        float h = VA_OTA_SCALE * (1.0f - g1nl), kg = (8.0f / VA_OTA_SCALE) * gnl, g27 = 27.0f * gnl, gk = gnl * (1.0f / VA_OTA_SCALE);
        lp = va_stage(&f->s[0], u, h, g1nl, kg, g27, gk);
        lp = va_stage(&f->s[1], lp, h, g1nl, kg, g27, gk);
        lp = va_stage(&f->s[2], lp, h, g1nl, kg, g27, gk);
        lp = va_stage(&f->s[3], lp, h, g1nl, kg, g27, gk);
    }
    return lp * VA_OUT_GAIN;
}
/* the small-signal stage by itself, step by step over xs = 0.35 (1 - g1) (x - s) from -4 to 4 at several cutoffs: inside the series' range (xs^2 < 0.25) it is within
 * 5e-5 of the exact stage's step (the series, relative to the term it replaces), outside it is the exact stage, bit for bit */
static void test_stage_fast(void)
{
    static const float HZ[5] = {80.0f, 400.0f, 2000.0f, 7000.0f, 14000.0f};
    unsigned ih, k, nin = 0, nout = 0, bad_out = 0;
    double worst = 0.0;
    for (ih = 0; ih < 5u; ih++) {
        va_ladder_t l;
        float g1, h, gnl, kg, g27, gk;
        memset(&l, 0, sizeof l);
        va_ladder_reset(&l);
        va_ladder_setup(&l, 1, 8, FSF);
        va_ladder_coeffs(&l, HZ[ih], 0.3f, FSF);
        g1 = l.g1;
        h = VA_OTA_SCALE * (1.0f - g1);
        gnl = g1 / (1.0f - g1), kg = (8.0f / VA_OTA_SCALE) * gnl, g27 = 27.0f * gnl, gk = gnl * (1.0f / VA_OTA_SCALE);
        for (k = 0; k <= 800u; k++) {
            float xs = -4.0f + 0.01f * (float)k, s0 = 0.3f, sf = s0, se = s0, yf, ye, x = s0 + xs / h, w = xs * xs;
            yf = va_stage_f(&sf, x, h, g1, l.tk, l.c1s, l.c2s, l.c3s);
            ye = va_stage(&se, x, h, g1, kg, g27, gk);
            if (w < 0.24f) {
                double t = fabs((double)ye - (double)(s0 + g1 * (x - s0))) + 1e-9, d = fabs((double)yf - (double)ye);   /* (the nonlinear term the series replaces) */
                worst = d / t > worst ? d / t : worst;
                nin++;
            } else if (w > 0.26f) {                                                   /* (the gate's own rounding in between) */
                bad_out += !(yf == ye && sf == se);
                nout++;
            }
        }
    }
    check(worst < 5e-5 && bad_out == 0, "(a2) the small-signal stage: inside xs^2 < 0.25 (%u steps) within %.2g of the nonlinear term it replaces (< 5e-5); outside (%u steps) the exact stage, bit for bit (%u differ)", nin, worst, nout, bad_out);
}

static void test_ladder_fast(void)
{
    static const float AMP[6] = {0.02f, 0.1f, 0.3f, 0.6f, 1.0f, 2.5f}, HZ[4] = {120.0f, 600.0f, 2500.0f, 9000.0f}, RES[4] = {0.0f, 0.4f, 0.6f, 0.8f};
    unsigned ia, ih, ir, i;
    uint64_t fast = 0, all = 0;
    double worst = 0.0;
    for (ia = 0; ia < 6u; ia++)
        for (ih = 0; ih < 4u; ih++)
            for (ir = 0; ir < 4u; ir++) {
                va_ladder_t a, b;
                double e = 0.0, m = 0.0;
                memset(&a, 0, sizeof a);
                va_ladder_reset(&a);
                va_ladder_setup(&a, 1, 1, FSF);
                for (i = 0; i < 12000u; i++) {
                    float x = AMP[ia] * (0.7f * sinf((float)i * 0.0431f * (1.0f + HZ[ih] * 0.0004f)) + 0.3f * sinf((float)i * 0.31f)), ya, yb;
                    if (i == 0) {
                        va_ladder_coeffs(&a, HZ[ih], RES[ir], FSF);
                        b = a;
                    }
                    va_ladder_coeffs(&a, HZ[ih], RES[ir], FSF);
                    b.noise_lvl = a.noise_lvl;                                        /* (the noise level follows the filter's own state) */
                    fast += a.s[3] * a.s[3] < VA_A2MAX;
                    all++;
                    ya = va_ladder_tick(&a, x);
                    yb = ref_tick(&b, x);
                    e = fabs((double)ya - (double)yb) > e ? fabs((double)ya - (double)yb) : e;
                    m = fabs((double)yb) > m ? fabs((double)yb) : m;
                }
                worst = m > 0.0 && e / m > worst ? e / m : worst;
            }
    check(worst < 1.5e-5 && fast > all / 2u, "(a2) the small-signal ladder path against the exact tick it replaces: 96 settings (6 levels x 4 cutoffs x 4 resonances), worst %.2g of the peak (< 1.5e-5); the path is taken on %.0f %% of the samples (> 50)",
          worst, 100.0 * (double)fast / (double)all);
}

/* ------------------------------------------------------------ halfband --- */
static void test_halfband(void)
{
    static const double F[3] = {1000.0, 8000.0, 15000.0};
    unsigned t, n;
    char msg[200] = "";
    int ok = 1;
    double rej;
    for (t = 0; t < 3u; t++) {                     /* up then down: unity gain through the pair */
        va_hb_t u, d;
        double si = 0.0, so = 0.0;
        memset(&u, 0, sizeof u);
        memset(&d, 0, sizeof d);
        va_hb_reset(&u);
        va_hb_reset(&d);
        for (n = 0; n < 8192u; n++) {
            float x = (float)sin(2.0 * M_PI * F[t] * n / FSD), a, b, y;
            va_hb_up(&u, x, &a, &b);
            y = va_hb_down(&d, a, b);
            if (n >= 4096u) {
                si += (double)x * x;
                so += (double)y * y;
            }
        }
        {
            double g = 10.0 * log10(so / si);
            ok &= fabs(g) < 0.3;
            snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " %.0f Hz %+.2f dB", F[t], g);
        }
    }
    check(ok, "halfband up -> down is unity gain (within 0.3 dB):%s", msg);
    {                                              /* a tone at 30 kHz in the 88.2 kHz stream aliases to 14.1 kHz: the decimator must stop it */
        va_hb_t d;
        double si = 0.0, so = 0.0;
        memset(&d, 0, sizeof d);
        va_hb_reset(&d);
        for (n = 0; n < 8192u; n++) {
            float a = (float)sin(2.0 * M_PI * 30000.0 * (2 * n) / (2 * FSD)), b = (float)sin(2.0 * M_PI * 30000.0 * (2 * n + 1) / (2 * FSD)), y;
            y = va_hb_down(&d, a, b);
            if (n >= 4096u) {
                si += 0.5 * 2;
                so += (double)y * y;
            }
        }
        rej = 10.0 * log10(so / si + 1e-30);
        check(rej < -50.0, "halfband down: a 30 kHz tone at 88.2 kHz (aliasing to 14.1 kHz) is %.0f dB down (< -50)", rej);
    }
}

/* ------------------------------------------------------------ (b) --- */
static void test_slope(void)
{
    uint32_t ci = cut_for_hz(1000), fc = CUTOFF_HZ[ci];
    double pass, f4, f2;
    drift_fresh();
    pots(64, 64, 0, 0, (int)ci, 0, 0, 0);
    inj_mode = INJ_NOISE;
    inj_amp = 0.05f;
    inj_step = 0.0f;
    play(60, 4096, NFFT * 8u);
    psd_of(buf, 8);
    pass = psd_db(100.0, 200.0);
    f2 = psd_db(1.9 * fc, 2.1 * fc);
    f4 = psd_db(3.8 * fc, 4.2 * fc);
    check(pass - f4 >= 20.0 && f2 - f4 >= 19.0,
          "(b) 24 dB/oct: cutoff %u Hz, 4 x cutoff is %.1f dB under the passband (>= 20), 2 x -> 4 x drops %.1f dB (>= 19)", fc, pass - f4, f2 - f4);
}

/* ------------------------------------------------------------ (c) --- */
static double thd_at(int drv)
{
    const unsigned K = 149;                        /* 149 cycles in NFFT: 200.5 Hz, bin-centred */
    double f1, e1 = 0.0, eh = 0.0;
    unsigned h, k;
    drift_fresh();
    pots(64, 64, 0, 0, 127, 0, drv, 0);
    inj_mode = INJ_SINE;
    inj_amp = 0.5f;
    inj_step = (float)(2.0 * M_PI * K / 32768.0 / DRIFT_OS);   /* per oversampled sample; 32768 base samples = K cycles */
    {
        static int32_t b2[32768];
        trk_note_on(T, 60, 100);
        run(0, 8192);
        run(b2, 32768);
        {
            static float m2[16384];
            fft_mag(b2, 32768, m2);
            f1 = K;
            for (k = (unsigned)f1 - 3; k <= (unsigned)f1 + 3; k++)
                e1 += (double)m2[k] * m2[k];
            for (h = 2; h <= 10; h++)
                for (k = K * h - 3; k <= K * h + 3; k++)
                    if (k < 16384u)
                        eh += (double)m2[k] * m2[k];
        }
    }
    return sqrt(eh / e1);
}
static void test_thd(void)
{
    double a = thd_at(0), b = thd_at(127);
    check(a < 0.01 && b > 0.10, "(c) THD of a 200 Hz sine, filter open: DRV 0 %.2f %% (< 1), DRV 127 %.1f %% (> 10)", 100.0 * a, 100.0 * b);
}

/* ------------------------------------------------------------ (d) --- */
static void test_selfosc(void)
{
    static const uint32_t HZ[3] = {300, 1000, 3000};
    unsigned i;
    char msg[240] = "";
    int ok = 1;
    for (i = 0; i < 3u; i++) {
        uint32_t ci = cut_for_hz(HZ[i]), fc = CUTOFF_HZ[ci];
        double hz, err;
        drift_fresh();
        pots(64, 64, 0, 0, (int)ci, 127, 0, 0);
        inj_mode = INJ_SILENCE;
        trk_note_on(T, 60, 100);
        run(0, 16384);
        run(buf, NFFT);
        fft_mag(buf, NFFT, mag_);
        hz = pk_hz(mag_, NFFT, 0.5 * fc, 2.0 * fc);
        err = hz / fc - 1.0;
        ok &= fabs(err) <= 0.03;
        snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " [%u Hz: %.1f Hz, %+.2f %%]", fc, hz, 100.0 * err);
    }
    check(ok, "(d) RES 127 self-oscillates within +-3 %% of the cutoff:%s", msg);
}

/* ------------------------------------------------------------ (e) --- */
/* the pitch of a held saw (filter open): the rising zero crossings of `secs` s, a window of 100 cycles each, cents against
 * `nominal` Hz into cents[]; returns the windows */
static double zc_[16384];
static unsigned pitch_track(double *cents, unsigned maxw, unsigned secs, double nominal)
{
    static int32_t blk[CTL * 64u];
    unsigned total = secs * 44100u / (CTL * 64u) * (CTL * 64u), done = 0, nz = 0, w = 0, i;
    int32_t prev = 0;
    uint64_t pos = 0;
    while (done < total) {
        run(blk, CTL * 64u);
        for (i = 0; i < CTL * 64u; i++, pos++) {
            int32_t y = blk[i];
            if (pos > 8192u && prev < 0 && y >= 0 && nz < NELEM(zc_))
                zc_[nz++] = (double)pos - 1.0 + (double)(-prev) / (double)(y - prev);
            prev = y;
        }
        done += CTL * 64u;
    }
    for (i = 0; i + 100u < nz && w < maxw; i += 100u)
        cents[w++] = 1200.0 * log2(99.0 * FSD / (zc_[i + 99u] - zc_[i]) / nominal);
    return w;
}
static void test_drift(void)
{
    static int32_t a[CTL * 400u], b[CTL * 400u];
    double cents[64], lo = 1e9, hi = -1e9, mx = 0.0;
    unsigned w, i;
    drift_fresh();
    pots(64, 64, 0, 0, 127, 0, 0, 0);
    trk_note_on(T, 57, 100);
    run(a, CTL * 400u);
    drift_fresh();
    pots(64, 64, 0, 0, 127, 0, 0, 0);
    trk_note_on(T, 57, 100);
    run(b, CTL * 400u);
    check(!memcmp(a, b, sizeof a), "(e) DRFT 0: two renders are byte-identical");
    drift_fresh();
    pots(64, 64, 0, 0, 127, 0, 0, 0);
    trk_note_on(T, 57, 100);
    w = pitch_track(cents, 64, 14, 220.0);
    for (i = 0; i < w; i++) {
        mx = fmax(mx, fabs(cents[i]));
    }
    check(w >= 20 && mx < 0.3, "(e) DRFT 0: the pitch is steady (%u windows, worst %.2f cents off 220 Hz)", w, mx);
    drift_fresh();
    pots(64, 64, 127, 0, 127, 0, 0, 0);
    trk_note_on(T, 57, 100);
    w = pitch_track(cents, 64, 14, 220.0);
    mx = 0.0;
    for (i = 0; i < w; i++) {
        lo = fmin(lo, cents[i]);
        hi = fmax(hi, cents[i]);
        mx = fmax(mx, fabs(cents[i]));
    }
    check(w >= 20 && mx <= 8.3 && hi - lo >= 0.5,
          "(e) DRFT 127: a slow wander, %.2f .. %.2f cents (within +-8, moving by >= 0.5) over %u windows of 100 cycles", lo, hi, w);
}

/* ------------------------------------------------------------ (f) --- */
/* one voice of the engine by itself: voice index i, `note`, nblk blocks, the modulation neutral (amp 1, cutoff 0, shape centred) */
static void direct_voice(unsigned i, unsigned note, unsigned nblk, int32_t *out)
{
    voice_t *v = &T->v[i];
    vmod_t m;
    unsigned b, k;
    memset(v, 0, sizeof *v);
    v->active = v->gate = 1;
    v->stage = 2;
    v->note = (uint8_t)note;
    v->vel = 100;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    memset(&m, 0, sizeof m);
    m.pitch16 = v->pitch16;
    m.inc = pitch_inc(m.pitch16);
    m.amp0 = m.amp1 = 32767;
    m.shape = 64 << 8;
    m.envq15 = 32767;
    ENGINES[ENGI_DRIFT]->note_on(T, v);
    for (b = 0; b < nblk; b++) {
        int32_t o[CTL];
        for (k = 0; k < CTL; k++)
            o[k] = 0;
        ENGINES[ENGI_DRIFT]->render(T, v, o, CTL, &m);
        if (out)
            for (k = 0; k < CTL; k++)
                out[b * CTL + k] = o[k];
    }
}
static void test_variance(void)
{
    double hz[2][2 * DRIFT_POLY], lo, hi;
    unsigned var, i, part, k;
    int ok0, ok1;
    for (var = 0; var < 2u; var++) {
        k = 0;
        for (part = 0; part < 2u; part++) {            /* two parts, so four voices' offsets (they are seeded by part and voice) */
            for (i = 0; i < DRIFT_POLY; i++) {
                drift_fresh();
                pots(64, 64, 0, var ? 127 : 0, 80, 127, 0, 0);
                trk[1] = trk[0];
                memset(eng_mem[1].b, 0, sizeof eng_mem[1].b);
                T = &trk[part];
                inj_mode = INJ_SILENCE;
                direct_voice(i, 60, NFFT / CTL + 512u, buf);
                fft_mag(buf + 512 * CTL, NFFT, mag_);
                hz[var][k++] = pk_hz(mag_, NFFT, 200.0, 4000.0);
            }
        }
    }
    lo = hi = hz[0][0];
    for (i = 1; i < k; i++) {
        lo = hz[0][i] < lo ? hz[0][i] : lo;
        hi = hz[0][i] > hi ? hz[0][i] : hi;
    }
    ok0 = hi / lo - 1.0 < 0.002;
    check(ok0, "(f) VAR 0: the %u voices self-oscillate at %.1f .. %.1f Hz (the same cutoff, within 0.2 %%)", k, lo, hi);
    lo = hi = hz[1][0];
    for (i = 1; i < k; i++) {
        lo = hz[1][i] < lo ? hz[1][i] : lo;
        hi = hz[1][i] > hi ? hz[1][i] : hi;
    }
    ok1 = hi / lo - 1.0 > 0.01;
    check(ok1, "(f) VAR 127: the %u voices self-oscillate at %.1f .. %.1f Hz (%.2f %% apart, >= 1)", k, lo, hi, 100.0 * (hi / lo - 1.0));
    {   /* and within each part the voices differ from one another (each voice has its own tolerances), not only part from part: the smallest
         * gap between any two of a part's DRIFT_POLY voices, and VAR 0 puts them all on one cutoff */
        double gap = 1e9, gap0 = 0.0;
        unsigned a, c;
        for (part = 0; part < 2u; part++)
            for (a = 0; a < DRIFT_POLY; a++)
                for (c = a + 1u; c < DRIFT_POLY; c++) {
                    double x = hz[1][part * DRIFT_POLY + a], y = hz[1][part * DRIFT_POLY + c], r = x > y ? x / y : y / x;
                    double x0 = hz[0][part * DRIFT_POLY + a], y0 = hz[0][part * DRIFT_POLY + c], r0 = x0 > y0 ? x0 / y0 : y0 / x0;
                    gap = r - 1.0 < gap ? r - 1.0 : gap;
                    gap0 = r0 - 1.0 > gap0 ? r0 - 1.0 : gap0;
                }
        check(DRIFT_POLY < 2 || (gap > 0.003 && gap0 < 0.002),
              "(f) VAR 127: within a part any two of its %d voices self-oscillate at least %.2f %% apart (> 0.3); VAR 0: at most %.2f %% apart (< 0.2)", (int)DRIFT_POLY, 100.0 * gap, 100.0 * gap0);
    }
}

/* ------------------------------------------------------------ (g) --- */
static float mag2_[NFFT];
static void test_mix(void)
{
    int k;
    unsigned mixes[3] = {0, 64, 127};
    char msg[3][220];
    int ok[3];
    for (k = 0; k < 3; k++) {
        double f1, f2, f3, f4, s1;
        drift_fresh();
        pots((int)mixes[k], 64, 0, 0, 127, 0, 0, 0);
        trk_note_on(T, 57, 100);
        run(0, 16384);
        run(buf, NFFT * 2u);
        fft_mag(buf, NFFT * 2u, mag2_);
        s1 = peak_db(mag2_, NFFT * 2u, 110.0);
        f1 = peak_db(mag2_, NFFT * 2u, 220.0);
        f2 = peak_db(mag2_, NFFT * 2u, 440.0);
        f3 = peak_db(mag2_, NFFT * 2u, 660.0);
        f4 = peak_db(mag2_, NFFT * 2u, 880.0);
        if (k == 0) {                              /* sub: a square an octave down: 110, 330, 550 Hz; nothing at 220 */
            double s3 = peak_db(mag2_, NFFT * 2u, 330.0);
            ok[k] = s1 > f1 + 20.0 && s1 > f2 + 20.0 && s1 >= s3 + 6.0 && s1 > f4 + 20.0;
            snprintf(msg[k], sizeof msg[k], "MIX 0: 110 Hz (the sub) %.1f dB, 220 %.1f, 330 %.1f, 440 %.1f", s1, f1, s3, f2);
        } else if (k == 1) {                       /* saw: 1/n harmonics, no sub */
            ok[k] = s1 < f1 - 25.0 && fabs((f2 - f1) + 6.0) < 3.0 && fabs((f3 - f1) + 9.5) < 3.0 && fabs((f4 - f1) + 12.0) < 3.5;
            snprintf(msg[k], sizeof msg[k], "MIX 64 (saw): harmonics 2 3 4 at %+.1f %+.1f %+.1f dB against the fundamental (-6 -9.5 -12), the sub %.1f dB under",
                     f2 - f1, f3 - f1, f4 - f1, f1 - s1);
        } else {                                   /* pulse at PW 64: odd harmonics only */
            ok[k] = s1 < f1 - 25.0 && f2 < f1 - 25.0 && f4 < f1 - 25.0 && fabs((f3 - f1) + 9.5) < 3.0;
            snprintf(msg[k], sizeof msg[k], "MIX 127 (pulse, PW 64): the 2nd and 4th harmonics are %.1f and %.1f dB under the fundamental (>= 25), the 3rd %+.1f (-9.5), the sub %.1f under",
                     f1 - f2, f1 - f4, f3 - f1, f1 - s1);
        }
    }
    for (k = 0; k < 3; k++)
        check(ok[k], "(g) %s", msg[k]);
}

/* ------------------------------------------------------------ (h) --- */
static void test_hpf(void)
{
    double p[4][4], ref[4];
    static const double AT[4] = {60.0, 100.0, 236.0, 754.0};
    unsigned ft, a;
    for (ft = 0; ft < 4u; ft++) {
        drift_fresh();
        pots(64, 64, 0, 0, 127, 0, 0, 0);
        T->p[P_FTYPE] = (int16_t)ft;
        inj_mode = INJ_NOISE;
        inj_amp = 0.05f;
        play(60, 4096, NFFT * 10u);
        psd_of(buf, 10);
        ref[ft] = psd_db(2000.0, 4000.0);
        for (a = 0; a < 4u; a++)
            p[ft][a] = psd_db(AT[a] * 0.93, AT[a] * 1.07) - ref[ft];
    }
    check(fabs(p[0][0]) < 1.0 && fabs(p[0][1]) < 1.0 && fabs(p[0][2]) < 1.0 && fabs(p[0][3]) < 1.0,
          "(h) P_FTYPE 0: flat (60 100 236 754 Hz at %+.1f %+.1f %+.1f %+.1f dB)", p[0][0], p[0][1], p[0][2], p[0][3]);
    check(p[1][0] > 5.0 && p[1][1] > 3.0 && p[1][3] < 3.0,
          "(h) P_FTYPE 1: the bass boost (60 100 236 754 Hz at %+.1f %+.1f %+.1f %+.1f dB)", p[1][0], p[1][1], p[1][2], p[1][3]);
    check(fabs(p[2][2] + 3.0) < 1.5 && p[2][0] < -10.0 && p[2][3] > -1.5,
          "(h) P_FTYPE 2: a 236 Hz corner (60 100 236 754 Hz at %+.1f %+.1f %+.1f %+.1f dB; -3 dB at 236)", p[2][0], p[2][1], p[2][2], p[2][3]);
    check(fabs(p[3][3] + 3.0) < 1.5 && p[3][2] < -8.0 && p[3][2] < p[2][2] - 5.0,
          "(h) P_FTYPE 3: a 754 Hz corner (60 100 236 754 Hz at %+.1f %+.1f %+.1f %+.1f dB; -3 dB at 754)", p[3][0], p[3][1], p[3][2], p[3][3]);
}

/* ------------------------------------------------------------ the corners --- */
static void test_corners(void)
{
    static const int MIXES[3] = {0, 64, 127}, RES[2] = {0, 127}, DRV[2] = {0, 127}, CUT[3] = {0, 60, 127}, KTR[2] = {0, 127};
    static const unsigned NOTES[4] = {12, 60, 108, 127};
    unsigned a, b, c, d, e, n, i, bad_n = 0, nz = 0, cases = 0;
    int32_t peak = 0;
    for (a = 0; a < 3u; a++)
        for (b = 0; b < 2u; b++)
            for (c = 0; c < 2u; c++)
                for (d = 0; d < 3u; d++)
                    for (e = 0; e < 2u; e++)
                        for (n = 0; n < 4u; n++) {
                            int32_t mx = 0;
                            drift_fresh();
                            pots(MIXES[a], 100, 127, 127, CUT[d], RES[b], DRV[c], KTR[e]);
                            T->p[P_ED_FLT] = 63;
                            trk_note_on(T, NOTES[n], 100);
                            run(buf, 4096);
                            for (i = 0; i < 4096u; i++)
                                mx = (buf[i] < 0 ? -buf[i] : buf[i]) > mx ? (buf[i] < 0 ? -buf[i] : buf[i]) : mx;
                            for (i = 0; i < DRIFT_POLY; i++) {
                                const drift_vc_t *c2 = vc_of(i);
                                int s;
                                for (s = 0; s < 4; s++)
                                    if (!(c2->l.s[s] > -1e4f && c2->l.s[s] < 1e4f))
                                        bad_n++;
                            }
                            cases++;
                            nz += mx > 0;
                            peak = mx > peak ? mx : peak;
                        }
    check(bad_n == 0 && peak <= 70000 && nz > cases / 2u,
          "corners: %u settings (MIX x RES x DRV x CUT x KTR x notes C0..G9, DRFT VAR PWM ENV on): every filter state finite, peak %d (<= 70000), %u sounded",
          cases, (int)peak, nz);
}


/* ------------------------------------------------------------ the DCO (I4, I3) --- */
/* The DCO is the donor's (KR106Oscillators.h): a linear ramp, the comparator pulse, the CD4013 sub, one polyBLEP at each edge.
 * Inharmonic energy is what a wrong ramp or an un-bandlimited step shows as: every partial of a bandlimited saw sits on a multiple
 * of the fundamental. Measured with MIX 64, the filter open, DRFT 0, a 32768-point Hann FFT; a harmonic owns its bin +-5. */
static double inharmonic_db(unsigned note)
{
    const unsigned N = 32768;
    double f0 = 440.0 * pow(2.0, ((double)note - 69.0) / 12.0), eh = 0.0, ei = 0.0;
    unsigned k;
    drift_fresh();
    pots(64, 64, 0, 0, 127, 0, 0, 0);
    trk_note_on(T, note, 100);
    run(0, 16384);
    run(buf, N);
    fft_mag(buf, N, mag2_);
    for (k = (unsigned)(0.5 * f0 * N / FSD); k < N / 2u; k++) {
        double h = (double)k * FSD / N / f0, d = h - floor(h + 0.5);
        double e = (double)mag2_[k] * mag2_[k];
        if (fabs(d) * f0 * N / FSD <= 5.0 && floor(h + 0.5) >= 1.0)
            eh += e;
        else
            ei += e;
    }
    return 10.0 * log10(ei / eh + 1e-30);
}
static void test_inharmonic(void)
{
    static const unsigned NOTES[3] = {72, 84, 96};
    double d[3], worst = -1e9;
    unsigned i;
    for (i = 0; i < 3u; i++) {
        d[i] = inharmonic_db(NOTES[i]);
        worst = d[i] > worst ? d[i] : worst;
    }
    check(worst < -50.0, "(j) the donor's linear ramp, bandlimited: inharmonic energy at MIDI 72 / 84 / 96 is %.1f / %.1f / %.1f dB (all < -50)",
          d[0], d[1], d[2]);
}

/* the Hann-windowed DFT of x[0..n) evaluated exactly at hz, dB: no scalloping (a peak search over FFT bins reads a harmonic up to
 * 1.4 dB low where it falls between two bins) */
static double tone_db(const int32_t *x, unsigned n, double hz)
{
    double re = 0.0, im = 0.0;
    unsigned i;
    for (i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)n), ph = 2.0 * M_PI * hz * (double)i / FSD;
        re += (double)x[i] * w * cos(ph);
        im -= (double)x[i] * w * sin(ph);
    }
    return 20.0 * log10(sqrt(re * re + im * im) + 1e-30);
}

/* the ideal (not bandlimited) waveform gs (2 x - 1) + gp (x < e ? -1 : 1): the magnitude of its harmonics 1..4 */
static void ideal_h(double gs, double gp, double e, double *h)
{
    const int M = 16384;
    int n, i;
    for (n = 1; n <= 4; n++) {
        double re = 0.0, im = 0.0;
        for (i = 0; i < M; i++) {
            double x = ((double)i + 0.5) / M, w = gs * (2.0 * x - 1.0) + gp * (x < e ? -1.0 : 1.0);
            re += w * cos(2.0 * M_PI * n * x);
            im -= w * sin(2.0 * M_PI * n * x);
        }
        h[n] = sqrt(re * re + im * im) / M;
    }
}
/* MIX -> the DCO's gains (the plan's table, the donor's J106 amplitudes saw 0.6 / pulse 0.5), PW -> the duty (the donor's pw range
 * 0.5 .. 0.95, J106 inverts it: the low part is 1 - pw of the cycle). The spectrum of 220 Hz at the filter open must follow it:
 * this pins the amplitudes, the PW law, the PW polarity against the saw and the MIX law, from the output. */
static void test_dco_spectrum(void)
{
    static const struct { int mix, pw; } C[] = {{64, 64}, {127, 64}, {127, 0}, {127, 40}, {96, 64}, {80, 64}, {96, 40}, {112, 20}};
    unsigned c, ok = 1;
    char msg[600] = "";
    for (c = 0; c < NELEM(C); c++) {
        double e = (double)C[c].mix, gs = (e <= 64.0 ? 1.0 : (127.0 - e) / 63.0) * 0.6, gp = (e <= 64.0 ? 0.0 : (e - 64.0) / 63.0) * 0.5;
        double pwn = C[c].pw < 64 ? (64.0 - C[c].pw) / 64.0 : 0.0, pw = 0.5 + pwn * 0.45, h[5], f[5], worst = 0.0;
        unsigned n;
        ideal_h(gs, gp, 1.0 - pw, h);
        drift_fresh();
        pots(C[c].mix, C[c].pw, 0, 0, 127, 0, 0, 0);
        trk_note_on(T, 57, 100);
        run(0, 16384);
        run(buf, NFFT * 2u);
        for (n = 1; n <= 4; n++)
            f[n] = tone_db(buf, NFFT * 2u, 220.0 * n);
        for (n = 2; n <= 4; n++) {
            double want = 20.0 * log10(h[n] / h[1] + 1e-30), got = f[n] - f[1];
            if (want > -30.0) {
                double d = fabs(got - want);
                worst = d > worst ? d : worst;
                ok &= d < 0.3;
            } else {
                ok &= got < -25.0;
            }
        }
        snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " [MIX %d PW %d: 2nd %+.1f (%+.1f) 3rd %+.1f (%+.1f) 4th %+.1f (%+.1f)]", C[c].mix, C[c].pw,
                 f[2] - f[1], 20.0 * log10(h[2] / h[1] + 1e-30), f[3] - f[1], 20.0 * log10(h[3] / h[1] + 1e-30), f[4] - f[1],
                 20.0 * log10(h[4] / h[1] + 1e-30));
    }
    check(ok, "(k) the DCO's spectrum at 220 Hz against the ideal waveform of the donor's amplitudes / PW law (harmonics 2 3 4 re the 1st, ideal in brackets, within 0.3 dB):%s", msg);
}

/* The waveform itself, not its magnitude spectrum: the saw's charge curvature is in quadrature with its harmonics and the pulse's
 * polarity against the saw is a sign on the real part, so neither moves a harmonic's magnitude; both move the shape. The output of a
 * held 659 Hz note (filter open) folded onto one cycle (512 bins, over 32768 samples), against the ideal waveform of the donor's
 * amplitudes (gs (2 x - 1) + gp (x < e ? -1 : 1), e = 1 - pw): best circular shift and a positive gain / offset by least squares,
 * the 10 % of the cycle around each edge left out (the blep, the filter's ringing). The relative rms error of the rest. */
static double wave_err(int mix, int pw, double *gain)
{
    enum { B = 512 };
    const unsigned N = NFFT * 2u;
    const double f0 = 440.0 * pow(2.0, 7.0 / 12.0);        /* MIDI 76 = 659.255 Hz */
    double fold[B], cnt[B], ideal[B], e = (double)mix, gs = (e <= 64.0 ? 1.0 : (127.0 - e) / 63.0) * 0.6, gp = (e <= 64.0 ? 0.0 : (e - 64.0) / 63.0) * 0.5;
    double pwn = pw < 64 ? (64.0 - pw) / 64.0 : 0.0, ed = 1.0 - (0.5 + pwn * 0.45), best = 1e9;
    unsigned i, sh, b;
    drift_fresh();
    pots(mix, pw, 0, 0, 127, 0, 0, 0);
    trk_note_on(T, 76, 100);
    run(0, 16384);
    run(buf, N);
    memset(fold, 0, sizeof fold);
    memset(cnt, 0, sizeof cnt);
    for (i = 0; i < N; i++) {
        double x = fmod(f0 * (double)i / FSD, 1.0);
        b = (unsigned)(x * B) % B;
        fold[b] += (double)buf[i];
        cnt[b] += 1.0;
    }
    for (b = 0; b < B; b++) {
        double x = ((double)b + 0.5) / B;
        fold[b] /= cnt[b] > 0.0 ? cnt[b] : 1.0;
        ideal[b] = gs * (2.0 * x - 1.0) + gp * (x < ed ? -1.0 : 1.0);
    }
    for (sh = 0; sh < B; sh++) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0, n = 0, a, c, err;
        for (b = 0; b < B; b++) {
            double x = ((double)b + 0.5) / B, y = fold[(b + sh) % B], v = ideal[b];
            if (x < 0.05 || x > 0.95 || fabs(x - ed) < 0.05)
                continue;
            sx += v, sy += y, sxx += v * v, sxy += v * y, syy += y * y, n += 1.0;
        }
        a = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        c = (sy - a * sx) / n;
        err = (syy - 2.0 * a * sxy - 2.0 * c * sy + a * a * sxx + 2.0 * a * c * sx + n * c * c) / n;
        err = sqrt(err > 0.0 ? err : 0.0) / sqrt(syy / n - (sy / n) * (sy / n));
        if (a > 0.0 && err < best) {
            best = err;
            *gain = a;
        }
    }
    return best;
}
static void test_waveform(void)
{
    static const struct { int mix, pw; } C[] = {{64, 64}, {96, 20}, {96, 40}, {80, 0}};
    double g, w[4];
    unsigned c, ok = 1;
    char msg[300] = "";
    for (c = 0; c < NELEM(C); c++) {
        w[c] = wave_err(C[c].mix, C[c].pw, &g);
        ok &= w[c] < 0.012;
        snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " [MIX %d PW %d: %.2f %%]", C[c].mix, C[c].pw, 100.0 * w[c]);
    }
    check(ok, "(p) the DCO's waveform at 659 Hz against the ideal (a linear ramp, the J106-inverted pulse): relative rms error, edges left out (< 1.2 %%):%s", msg);
}

/* the auto PWM: PW >= 64 sweeps the duty by a triangle (0.8 Hz), so the 2nd harmonic of a pulse (nil at a square) swings; at PW 64 it does not */
static double h2_series(int pw, double *lo, double *hi)
{
    unsigned i;
    drift_fresh();
    pots(127, pw, 0, 0, 127, 0, 0, 0);
    trk_note_on(T, 57, 100);
    run(0, 4096);
    *lo = 1e9;
    *hi = -1e9;
    for (i = 0; i < 8u; i++) {                     /* eight windows of 0.19 s, 0.37 s apart: the triangle's 1.25 s period is covered */
        double d;
        run(buf, 8192);
        run(0, 8192);
        fft_mag(buf, 8192, mag2_);
        d = peak_db(mag2_, 8192, 440.0) - peak_db(mag2_, 8192, 220.0);
        d = d < -60.0 ? -60.0 : d;                 /* (a true square has no 2nd harmonic: the floor is numerical noise, not a swing) */
        *lo = d < *lo ? d : *lo;
        *hi = d > *hi ? d : *hi;
    }
    return *hi - *lo;
}
static void test_pwm(void)
{
    double lo0, hi0, lo1, hi1, s0 = h2_series(64, &lo0, &hi0), s1 = h2_series(127, &lo1, &hi1);
    check(s0 < 1.0 && s1 > 10.0, "(l) PWM: the pulse's 2nd harmonic over 3 s swings %.1f dB at PW 64 (< 1, a fixed square) and %.1f dB at PW 127 (> 10, %.1f .. %.1f dB)",
          s0, s1, lo1, hi1);
}

/* ------------------------------------------------------------ KTR (I3) --- */
static double ktr_hz(unsigned note, int ktr)
{
    drift_fresh();
    pots(64, 64, 0, 0, (int)cut_for_hz(1000), 127, 0, ktr);
    inj_mode = INJ_SILENCE;
    trk_note_on(T, note, 100);
    run(0, 16384);
    run(buf, NFFT);
    fft_mag(buf, NFFT, mag_);
    return pk_hz(mag_, NFFT, 80.0, 9000.0);
}
static void test_ktr(void)
{
    double a127 = ktr_hz(60, 127), b127 = ktr_hz(72, 127), c127 = ktr_hz(48, 127);
    double a0 = ktr_hz(60, 0), b0 = ktr_hz(72, 0), a64 = ktr_hz(60, 64), b64 = ktr_hz(72, 64);
    int ok = fabs(b127 / a127 - 2.0) < 0.06 && fabs(a127 / c127 - 2.0) < 0.06 &&      /* KTR 127: one octave per octave */
             fabs(b0 / a0 - 1.0) < 0.03 &&                                            /* KTR 0: none */
             fabs(b64 / a64 - 1.418) < 0.05 &&                                        /* KTR 64: 64/127 of an octave per octave */
             fabs(a127 / a0 - 1.0) < 0.03;                                            /* C4 (60) is the zero point */
    check(ok, "(m) KTR: the self-oscillation of RES 127 at CUT 1 kHz: C5/C4 %.3f at KTR 127 (2), %.3f at KTR 64 (1.418), %.3f at KTR 0 (1); C4/C3 %.3f at 127 (2); "
              "C4 at 127 / at 0 %.3f (1: C4 is the zero point)", b127 / a127, b64 / a64, b0 / a0, a127 / c127, a127 / a0);
}

/* ------------------------------------------------------------ the DC blockers (I3) --- */
/* The coupling caps: a 1.59 Hz TPT high-pass after the DCO and another after the VCF (KR106Voice.h mPostOscDcG / mPostVcfDcG), so
 * -6 dB at 1.59 Hz in series (-3 dB each) against the passband. Measured by correlation over whole periods of a seam sine. */
static double lf_gain(double f)
{
    const unsigned P = (unsigned)(FSD / f + 0.5);        /* the period in samples: a multiple of CTL, so f is 44100 / P */
    const unsigned N = P * (f < 5.0 ? 3u : 30u);
    double re = 0.0, im = 0.0, fr = FSD / P;
    unsigned i;
    drift_fresh();
    pots(64, 64, 0, 0, 127, 0, 0, 0);
    inj_mode = INJ_SINE;
    inj_amp = 0.05f;
    inj_step = (float)(2.0 * M_PI * fr / FSD / DRIFT_OS);
    trk_note_on(T, 60, 100);
    run(0, 44160);
    run(buf, N);
    for (i = 0; i < N; i++) {
        re += (double)buf[i] * cos(2.0 * M_PI * fr * i / FSD);
        im += (double)buf[i] * sin(2.0 * M_PI * fr * i / FSD);
    }
    return 2.0 * sqrt(re * re + im * im) / N;
}
static void test_dc(void)
{
    double lo = lf_gain(44100.0 / 27648.0), ref = lf_gain(44100.0 / 2208.0), db = 20.0 * log10(lo / ref);
    check(fabs(db + 6.0) < 0.8, "(n) the two 1.59 Hz DC blockers in series: %.2f Hz is %.2f dB against %.2f Hz (-6 +- 0.8; one blocker alone would read -3)",
          44100.0 / 27648.0, db, 44100.0 / 2208.0);
}

/* ------------------------------------------------------------ the voice start (I3) --- */
/* The donor's contract (KR106Voice.h Trigger): a retrigger of a sounding voice keeps the oscillator phase and the filter (no click);
 * a voice that has finished starts from the reset. Ours: a fresh voice (voice.c leaves env 0 only then) zeroes the whole state and
 * seeds s[0]; a sounding one is not touched. (The donor never resets its filter, since its idle voice is not skipped; ours is, so
 * a stale resonance would otherwise burst out of the next note.) */
/* At DRIFT_POLY voices: a chord of DRIFT_POLY notes (voice.c's round robin puts them on voices 0 .. POLY-1, and back on the same ones after
 * a full round), every voice of it checked; a note past the cap steals one (a sounding voice), and MONO retriggers voice 0. */
static void test_voice_start(void)
{
    static const uint8_t CH[8] = {60, 64, 67, 71, 55, 52, 48, 72};
    drift_vc_t ref[DRIFT_POLY], before[DRIFT_POLY], mref, mbefore;
    unsigned i, b, on;
    int ok_fresh, ok_keep, ok_mono, live;
    drift_fresh();
    pots(127, 64, 127, 127, 100, 127, 40, 64);
    T->p[P_REL] = 20;
    for (i = 0; i < DRIFT_POLY; i++)
        trk_note_on(T, CH[i], 100);
    memcpy(ref, vc_of(0), sizeof ref);
    run(0, 22050);                                           /* loud, resonant, the filters ringing */
    for (live = 1, i = 0; i < DRIFT_POLY; i++)
        live &= vc_of(i)->l.s[1] != 0.0f && vc_of(i)->pos != 0.0f;
    for (i = 0; i < DRIFT_POLY; i++)
        trk_note_off(T, CH[i]);
    for (b = 0; b < 400u; b++) {
        for (on = 0, i = 0; i < DRIFT_POLY; i++)
            on |= T->v[i].active;
        if (!on)
            break;
        run(0, CTL);
    }
    live &= !on;
    for (i = 0; i < DRIFT_POLY; i++)
        trk_note_on(T, CH[i], 100);
    ok_fresh = live && !memcmp(vc_of(0), ref, sizeof ref);
    check(ok_fresh, "(o) %d new note(s) on finished voices start from the reset: every voice's state equals a never-played voice's (after %u blocks of release, state was dirty: %d)", (int)DRIFT_POLY, b, live);
    run(0, 8192);
    memcpy(before, vc_of(0), sizeof before);
    trk_note_on(T, CH[DRIFT_POLY], 100);                     /* a note past the cap: a steal of a sounding voice */
    for (ok_keep = 1, i = 0; i < DRIFT_POLY; i++)
        ok_keep &= before[i].pos != 0.0f && before[i].l.s[1] != 0.0f;
    ok_keep &= !memcmp(vc_of(0), before, sizeof before);
    check(ok_keep, "(o) a note past the voice cap steals a sounding voice and keeps every voice's phase (%.3f ..) and filter states (s1 %.3g ..): untouched", (double)before[0].pos, (double)before[0].l.s[1]);
    drift_fresh();                                           /* MONO: the next note retriggers voice 0 */
    pots(127, 64, 127, 127, 100, 127, 40, 64);
    T->p[P_VOICE] = V_MONO;
    trk_note_on(T, 60, 100);
    mref = *vc_of(0);
    run(0, 8192);
    mbefore = *vc_of(0);
    trk_note_on(T, 64, 100);
    ok_mono = mref.l.s[1] == 0.0f && mbefore.pos != 0.0f && mbefore.l.s[1] != 0.0f && !memcmp(vc_of(0), &mbefore, sizeof mbefore);
    check(ok_mono, "(o) MONO: a retrigger of the sounding voice keeps its phase (%.3f) and filter states (s1 %.3g)", (double)mbefore.pos, (double)mbefore.l.s[1]);
}

/* ------------------------------------------------------------ P_FTYPE beyond DRIFT's four positions (I2) --- */
/* The track-persistent P_FTYPE takes the values other engines' filters use (4 HP24, 5 NOTCH ..): on DRIFT, whose HPF has four
 * positions and no page of its own, every value above them is the flat position, never the 754 Hz bass cut */
static void test_ftype_fallback(void)
{
    static int32_t ref[CTL * 64u], got[CTL * 64u];
    static const int V[5] = {4, 5, 7, 127, -3};
    unsigned i;
    int ok = 1;
    drift_fresh();
    pots(96, 64, 0, 0, 100, 40, 0, 64);
    T->p[P_FTYPE] = 0;
    trk_note_on(T, 40, 100);
    run(ref, CTL * 64u);
    for (i = 0; i < 5u; i++) {
        drift_fresh();
        pots(96, 64, 0, 0, 100, 40, 0, 64);
        T->p[P_FTYPE] = (int16_t)V[i];
        trk_note_on(T, 40, 100);
        run(got, CTL * 64u);
        ok &= !memcmp(ref, got, sizeof ref);
    }
    check(ok, "(h) P_FTYPE 4, 5, 7, 127 and -3 render exactly as P_FTYPE 0 (flat): no hidden bass cut from another engine's setting");
}

/* ------------------------------------------------------------ (i) --- */
#include "instr_linux.h"   /* (V2) the Linux instruction counter */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#elif defined(__linux__)
    return instr_linux();   /* (V2) tests/instr_linux.h */
#endif
    return 0;
}
static int32_t mix_out[2 * CTL];
static int mix_chor;                                         /* the chorus mode of every DRIFT part in mix_prepare: OFF unless a caller says (tests/chorus_test.c has its cost) */
/* regress.c's job_cpu: parts[k] = {engine, preset, notes} (POLY, SUS 127, they ring out); the notes are on and nothing has rendered yet */
static void mix_prepare(const uint8_t (*parts)[3])
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    uint32_t p, i, k;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(rev_ap, 0, sizeof rev_ap);
    memset(&fx, 0, sizeof fx);
    memset(&pf, 0, sizeof pf);
    for (k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    perf_held = perf_latched = 0;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    vage = 0;
    rng_state = 0x1234567u;
    mod_seed = 0x2545F491u;
    memset(trk, 0, sizeof trk);
    memset(&mod, 0, sizeof mod);
    memset(sl, 0, sizeof sl);
    memset(eng_mem, 0, sizeof eng_mem);
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    inj_mode = INJ_OFF;
    host_tracks_init();
    song.sel = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
    for (p = 0; p < NPART; p++) {
        host_preset(&trk[p], parts[p][0], parts[p][1]);
        trk[p].p[P_RSV_LN] = parts[p][0] == ENGI_DRIFT ? (int16_t)mix_chor : 0;   /* CHOR OFF: this is the voice's cost and its pin (the chorus' is tests/chorus_test.c) */
        trk[p].p[P_VOICE] = V_POLY;
        trk[p].p[P_SUS] = 127;
        trk[p].p[P_AMODE] = 0;
        for (i = 0; i < parts[p][2]; i++)
            trk_note_on(&trk[p], NOTES[i] + 12u * p, 100);
        if (!parts[p][2])
            trk[p].p[P_MUTE] = 1;
    }
}
/* 0.5 s to settle, then 1 s counted */
static double mix_cost(const uint8_t (*parts)[3])
{
    uint32_t k, nb = FS / CTL;
    uint64_t i0 = 0;
    mix_prepare(parts);
    for (k = 0; k < nb / 2u + nb; k++) {
        if (k == nb / 2u)
            i0 = instr_now();
        mix_block(mix_out, CTL);
    }
    return i0 ? (double)(instr_now() - i0) / (nb * CTL) : 0.0;
}

/* What is asserted, and what is only measured. The target is a full four-track pattern at <= 80 % of one core on the device. The host
 * figure is the only instrument here (target_budget.py counts integer loops and cannot see FPU opcodes, and a divide is one
 * instruction here and several on the device), so this file cannot establish that claim and does not assert it: Task 8.1's PERF run
 * on the FM-1 is the gate. Two things it can do:
 *  - DRIFT_COST_MAX pins the DRIFT part itself (its dearest preset at the voice cap, the mix included) at +10 % of the measured worst,
 *    at -O2 (the units of regress.c and acid_test.c; another compiler or -O level moves it: re-measure and move the pin with a stated
 *    reason, never to hide a regression), as ACID's (f) does for its voice;
 *  - it MEASURES and prints the dearest legal four-track mix it can construct (the figure Task 8.1 must check on the device,
 *    recorded in docs/BASELINE.md): the engines' dearest presets, every split of the shared 8-voice budget over the four parts, any engine
 *    on any part (the same one on several), the dearest total; 4parts_light and 4parts_full are printed for reference. */
#define DRIFT_COST_MAX 1885.0  /* the measured worst (HOOVER, 1712 at DRIFT_POLY 3, DRIFT_OS 1, DRIFT_CTRL 8, -O2) + 10 %: FX.2 moved it from 1525 (FAT SAW BASS 1386 + 10 %), because the part path includes its DIST (4x oversampled: +280) and HOOVER has one */

static uint32_t engine_cap(uint32_t e)
{
    uint32_t c = ENGINES[e]->poly;
    return c && c < NVOICE ? c : NVOICE;
}
/* the dearest preset of engine e playing v notes alone (the mix included) */
static uint32_t dearest_preset(uint32_t e, uint32_t v, double *cost)
{
    uint8_t parts[NPART][3];
    uint32_t k, best = 0;
    double worst = 0.0;
    for (k = 0; k < ENGINES[e]->npresets; k++) {
        double c;
        memset(parts, 0, sizeof parts);
        parts[0][0] = (uint8_t)e, parts[0][1] = (uint8_t)k, parts[0][2] = (uint8_t)v;
        c = mix_cost((const uint8_t(*)[3])parts);
        if (c > worst) {
            worst = c;
            best = k;
        }
    }
    *cost = worst;
    return best;
}

/* a candidate mix: engine and voices of each of the four parts, and its cost (measured, or the sum of the parts' own costs) */
typedef struct { double c; uint8_t e[4], v[4]; } mixc_t;
static double mixc_cost(const mixc_t *m, const uint32_t *pre, const uint8_t *ENG)
{
    uint8_t parts[NPART][3];
    uint32_t i;
    memset(parts, 0, sizeof parts);
    for (i = 0; i < 4u; i++)
        parts[i][0] = ENG[m->e[i]], parts[i][1] = (uint8_t)pre[m->e[i]], parts[i][2] = m->v[i];
    return mix_cost((const uint8_t(*)[3])parts);
}
static void mixc_print(const char *tag, const mixc_t *m, const uint32_t *pre, const uint8_t *ENG)
{
    uint32_t i, n = 0;
    printf("drift: (i) %s %.0f =", tag, m->c);
    for (i = 0; i < 4u; i++)
        if (m->v[i])
            printf("%s %s %s x%u", n++ ? " +" : "", ENGINES[ENG[m->e[i]]]->name, ENGINES[ENG[m->e[i]]]->presets[pre[m->e[i]]].name, m->v[i]);
    printf("\n");
}
#define NTOP 6u
static void top_add(mixc_t *top, const mixc_t *m)          /* the NTOP dearest by estimate, dearest first */
{
    uint32_t i, j;
    for (i = 0; i < NTOP; i++)
        if (m->c > top[i].c) {
            for (j = NTOP - 1u; j > i; j--)
                top[j] = top[j - 1u];
            top[i] = *m;
            return;
        }
}

static void test_cost(void)
{
    uint8_t parts[NPART][3];
    double idle, full, light, worst, c1[4][NVOICE + 1u], wone = 0.0, ctop = 0.0;
    uint32_t pre[4], k, drift_k, v[4], e[4], i;
    mixc_t top[NTOP], topnd[NTOP], one = {0}, bd, bnd;
    static const uint8_t ENG[4] = {ENGI_OBX, ENGI_WAVE, ENGI_ACID, ENGI_DRIFT};
    if (!instr_now()) {
        printf("drift: SKIP (i) cost: NOT CHECKED, there is no instruction counter on this host (macOS proc_pid_rusage only); "
               "run it on a Mac, and Task 8.1 PERF is the gate on the device\n");
        return;
    }
    memset(parts, 0, sizeof parts);
    idle = mix_cost((const uint8_t(*)[3])parts);
    parts[0][0] = ENGI_WAVE, parts[0][2] = 8;
    parts[1][0] = ENGI_OBX, parts[1][2] = 8;
    parts[2][0] = ENGI_DRIFT, parts[2][2] = 4;
    parts[3][0] = ENGI_ACID, parts[3][2] = 1;
    full = mix_cost((const uint8_t(*)[3])parts);
    parts[0][2] = 3, parts[1][2] = 3, parts[2][2] = 2, parts[3][2] = 1;
    light = mix_cost((const uint8_t(*)[3])parts);
    drift_k = dearest_preset(ENGI_DRIFT, DRIFT_POLY, &worst);
    printf("drift: (i) DRIFT_POLY %d, DRIFT_OS %d, DRIFT_CTRL %d: the mix alone %.0f; the DRIFT part alone, worst preset %s %.0f (%.0f per voice)\n", (int)DRIFT_POLY,
           (int)DRIFT_OS, (int)DRIFT_CTRL, idle, ENGINES[ENGI_DRIFT]->presets[drift_k].name, worst, (worst - idle) / DRIFT_POLY);
    check(worst <= DRIFT_COST_MAX, "(i) the DRIFT part %.0f (%s, %d voices) is within its pin %.0f (the measured worst + 10 %%, -O2)", worst,
          ENGINES[ENGI_DRIFT]->presets[drift_k].name, (int)DRIFT_POLY, DRIFT_COST_MAX);
    /* the dearest legal mix: each engine's dearest preset at its cap (for the ranking), the cost of v voices of it alone (above the idle mix),
     * every assignment of engines to parts (a nondecreasing engine per part: the order of the parts is only the notes' octave) and every split
     * of the 8 voices (each engine within its own voice cap), ranked by the sum of the single-part costs; the NTOP dearest are then measured as mixes */
    for (k = 0; k < 4u; k++) {
        double c;
        pre[k] = dearest_preset(ENG[k], engine_cap(ENG[k]), &c);
        c1[k][0] = 0.0;
        for (i = 1; i <= engine_cap(ENG[k]); i++) {
            memset(parts, 0, sizeof parts);
            parts[0][0] = ENG[k], parts[0][1] = (uint8_t)pre[k], parts[0][2] = (uint8_t)i;
            c1[k][i] = mix_cost((const uint8_t(*)[3])parts) - idle;
        }
    }
    memset(top, 0, sizeof top);
    memset(topnd, 0, sizeof topnd);
    for (e[0] = 0; e[0] < 4u; e[0]++)
        for (e[1] = e[0]; e[1] < 4u; e[1]++)
            for (e[2] = e[1]; e[2] < 4u; e[2]++)
                for (e[3] = e[2]; e[3] < 4u; e[3]++)
                    for (v[0] = 0; v[0] <= engine_cap(ENG[e[0]]); v[0]++)
                        for (v[1] = 0; v[1] <= engine_cap(ENG[e[1]]); v[1]++)
                            for (v[2] = 0; v[2] <= engine_cap(ENG[e[2]]); v[2]++)
                                for (v[3] = 0; v[3] <= engine_cap(ENG[e[3]]); v[3]++) {
                                    mixc_t m;
                                    int nd = 0, distinct = 1;
                                    if (v[0] + v[1] + v[2] + v[3] != NVOICE)   /* the dearest has the whole budget in play (more voices never cost less) */
                                        continue;
                                    m.c = idle;
                                    for (i = 0; i < 4u; i++) {
                                        m.e[i] = (uint8_t)e[i], m.v[i] = (uint8_t)v[i];
                                        m.c += c1[e[i]][v[i]];
                                        nd |= e[i] == 3u && v[i];
                                        distinct &= i == 0 || e[i] != e[i - 1u];
                                    }
                                    top_add(nd ? top : topnd, &m);
                                    if (distinct) {
                                        mixc_t mm = m;
                                        mm.c = mixc_cost(&m, pre, ENG);
                                        if (mm.c > wone) {
                                            wone = mm.c;
                                            one = mm;
                                        }
                                    }
                                }
    bd = bnd = top[0];
    bd.c = bnd.c = 0.0;
    for (i = 0; i < NTOP; i++) {
        mixc_t m = top[i];
        m.c = mixc_cost(&m, pre, ENG);
        if (m.c > bd.c)
            bd = m;
        m = topnd[i];
        m.c = mixc_cost(&m, pre, ENG);
        if (m.c > bnd.c)
            bnd = m;
    }
    printf("drift: (i) measured, not asserted (Task 8.1 checks the device): 4parts_light %.0f, 4parts_full %.0f (idle mix %.0f)\n", light, full, idle);
    mixc_print("the dearest legal mix (8 voices, any engine on any part):", bd.c > bnd.c ? &bd : &bnd, pre, ENG);
    mixc_print("the dearest with DRIFT in it:", &bd, pre, ENG);
    mixc_print("the dearest without a DRIFT voice:", &bnd, pre, ENG);
    mixc_print("the dearest with one part per engine:", &one, pre, ENG);
    mix_chor = 2;                                            /* the chorus (Task 6.2) on every DRIFT part: II, the dearest by a few instructions */
    ctop = mixc_cost(&bd, pre, ENG);
    mix_chor = 0;
    printf("drift: (i) the dearest with DRIFT in it, the chorus (CHOR II) on every DRIFT part: %.0f (+%.0f)\n", ctop, ctop - bd.c);
}

/* ------------------------------------------------------------ (a2) the ladder's cost (Task 6.3) --- */
/* The filter alone, 32-sample blocks of a steady 0.3 x noise at 1.2 kHz, RES 0.5 (the small-signal ladder, as the presets play it): va_ladder_run, the
 * firmware's loop, pinned at +10 % of its measured cost, -O2 (host instructions a sample, the units of (i)): 240 before Task 6.3 (the exact tick, not
 * inlined, the whole state written back), 156 with the small-signal path, always_inline and the short write-back. The exact tick it replaced (ref_tick in
 * registers) is printed for comparison; how a compiler lays that one out is not asserted. */
#define LADDER_COST_MAX HOST_PIN(172.0, 198.0)   /* the measured va_ladder_run (156 at -O2 on the Mac, 180 on Linux x86-64; DRIFT_OS 1) + 10 % */
static double ladder_cost(int exact)
{
    static float x[32], y[32];
    va_ladder_t l;
    uint32_t r = 0x2545F491u, b, i;
    uint64_t i0 = 0;
    memset(&l, 0, sizeof l);
    va_ladder_reset(&l);
    va_ladder_setup(&l, 1, 8, FSF);
    for (b = 0; b < 400u + 3000u; b++) {
        if (b == 400u)
            i0 = instr_now();
        for (i = 0; i < 32u; i++) {
            r = r * 1664525u + 1013904223u;
            x[i] = 0.3f * ((float)(int32_t)r * (1.0f / 2147483648.0f));
        }
        va_ladder_coeffs(&l, 1200.0f, 0.5f, FSF);
        if (exact) {                                         /* (in registers, as va_ladder_run does) */
            va_ladder_t t = l;
            for (i = 0; i < 32u; i++)
                y[i] = ref_tick(&t, x[i]);
            l = t;
        } else
            va_ladder_run(&l, x, y, 32);
    }
    return i0 ? (double)(instr_now() - i0) / (3000.0 * 32.0) : 0.0;
}
static void test_ladder_cost(void)
{
    double fast = ladder_cost(0), exact = ladder_cost(1);
    if (!instr_now()) {
        printf("drift: SKIP (a2) ladder cost: NOT CHECKED, there is no instruction counter on this host\n");
        return;
    }
    printf("drift: (a2) the ladder alone, instructions a sample: va_ladder_run %.0f; the exact tick it replaced, in registers, %.0f\n", fast, exact);
    check(fast <= LADDER_COST_MAX, "(a2) va_ladder_run costs %.0f instructions a sample, within its pin %.0f (the measured + 10 %%, -O2)", fast, LADDER_COST_MAX);
}

/* ------------------------------------------------------------ the integer headroom at DRIFT_POLY voices (Task 6.3) --- */
/* A DRIFT voice is held to +-2 FS before the integer conversion (drift_render), so a part's sum is at most DRIFT_POLY x 2 FS: 6 FS at three voices, which is
 * x = 466,224 in mix_part (fx.c: x = ((b >> 2) * 9713) >> 10, 77,704 per FS; x * gl needs x <= 524,287). With the chorus on, chorus_part holds each of L and R
 * to the 5 FS guard (dsp_chorus.c CHORUS_GUARD_FS), x <= 388,520. The master forms ((mix + wet) >> 2) * mg and needs |mix + wet| < 2^21 over four parts and
 * the wet bus; the budget of 8 voices means 3 + 3 + 1 + 1 at the most for DRIFT parts. Here every voice is driven into its clamp (the oscillator replaced by a
 * 100 x square, the same in every voice, through the seam) with LEVEL 127, PAN centre, every send 127, REVERB HALL at SIZE 127 / DAMP 0 and the delay's
 * feedback up, CHOR OFF and I+II, and the real mix_block's buses are read back. */
static void test_headroom(void)
{
    enum { NBLK = 1000 };
    static const uint32_t HALF[5] = {7, 40, 97, 211, 1500};
    static const uint8_t NV[2][4] = {{3, 3, 1, 1}, {3, 3, 2, 0}};
    int32_t worst[2] = {0, 0}, wx[2] = {0, 0};
    uint32_t chor, h, p, b, i, cfg;
    for (chor = 0; chor < 2u; chor++)
        for (cfg = 0; cfg < 2u; cfg++)
            for (h = 0; h < 5u; h++) {
                uint8_t parts[NPART][3];
                memset(parts, 0, sizeof parts);
                mix_chor = chor ? 3 : 0;
                for (p = 0; p < 4u; p++)
                    parts[p][0] = ENGI_DRIFT, parts[p][1] = 8, parts[p][2] = 0;
                mix_prepare((const uint8_t(*)[3])parts);
                song.g[G_RTYPE] = 2, song.g[G_RSIZE] = 127, song.g[G_RDAMP] = 0, song.g[G_DTIME] = 3, song.g[G_DFDBK] = 120;
                song.master_q12 = 4096;
                for (p = 0; p < 4u; p++) {
                    T = &trk[p];
                    T->p[P_MUTE] = 0, T->p[P_PAN] = 0, T->p[P_SPRD] = 0, T->p[P_LEVEL] = 127, T->p[P_DIST] = 0;
                    T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 127;
                    T->p[P_RSV_LN] = chor ? 3 : 0;
                    pots(64, 64, 0, 0, 127, 127, 0, 0);
                    for (i = 0; i < NV[cfg][p]; i++)
                        trk_note_on(T, 36 + 7 * i + p, 127);
                }
                T = &trk[0];
                inj_mode = INJ_SQUARE, inj_amp = 100.0f, inj_half = HALF[h], inj_t = 0, inj_os = DRIFT_OS, inj_blk = CTL * DRIFT_OS;
                for (b = 0; b < NBLK; b++) {
                    inj_n = 0;
                    mix_block(mix_out, CTL);
                    inj_t += CTL;
                    for (p = 0; p < 4u; p++) {
                        wx[chor] = trk[p].peak > wx[chor] ? trk[p].peak : wx[chor];
                    }
                    for (i = 0; i < CTL; i++) {
                        int64_t sl_ = (int64_t)mix_l[i] + wet[i], sr_ = (int64_t)mix_r[i] + wet[i];
                        sl_ = sl_ < 0 ? -sl_ : sl_, sr_ = sr_ < 0 ? -sr_ : sr_;
                        worst[chor] = (int32_t)(sl_ > worst[chor] ? sl_ : worst[chor]);
                        worst[chor] = (int32_t)(sr_ > worst[chor] ? sr_ : worst[chor]);
                    }
                }
                inj_mode = INJ_OFF;
            }
    mix_chor = 0;
    printf("drift: headroom, 4 DRIFT parts at %d voices (3+3+1+1, 3+3+2), LEVEL 127, sends 127, HALL SIZE 127 DAMP 0, every voice driven into its +-2 FS clamp: CHOR OFF part x %d (6 FS = 466224), |mix + wet| %d; "
           "CHOR I+II part x %d (5 FS = 388520), |mix + wet| %d (limit 2097151)\n", (int)DRIFT_POLY, wx[0], worst[0], wx[1], worst[1]);
    check(wx[0] <= 524287 && worst[0] < 2097151 && wx[0] >= 466224 - 16,
          "headroom: CHOR OFF, the voices at their clamp (3 x 2 FS a part): part x %d <= 524287 (x * gl stays in int32; the clamp reached: >= 466208), |mix + wet| %d < 2097151 (%.1f %% left)", wx[0], worst[0], 100.0 * (2097151.0 - (double)worst[0]) / 2097151.0);
    check(wx[1] <= 388520 + 16 && worst[1] < 2097151 && wx[1] >= 388520 - 16,
          "headroom: CHOR I+II, the chorus input at 6 FS held to the 5 FS guard: part x %d <= 388536, |mix + wet| %d < 2097151 (%.1f %% left)", wx[1], worst[1], 100.0 * (2097151.0 - (double)worst[1]) / 2097151.0);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    if (argc > 1)
        refdir = argv[1];
    test_ladder_ref();
    test_ladder_cache();
    test_ladder_run();
    test_stage_fast();
    test_ladder_fast();
    test_halfband();
    test_slope();
    test_thd();
    test_selfosc();
    test_drift();
    test_variance();
    test_mix();
    test_hpf();
    test_corners();
    test_inharmonic();
    test_dco_spectrum();
    test_waveform();
    test_pwm();
    test_ktr();
    test_dc();
    test_voice_start();
    test_ftype_fallback();
    test_headroom();
    test_cost();
    test_ladder_cost();
    printf("drift_test: %s\n", bad ? "FAIL" : "ok");
    return bad ? 1 : 0;
}
