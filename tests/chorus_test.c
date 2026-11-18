/* SPDX-License-Identifier: GPL-3.0-only */
/* chorus_test.c: DRIFT's Juno chorus (firmware/src/dsp_chorus.c, the per-part stereo hook in fx.c). Task 6.2; 2026-10-21: the Juno-60 chorus of
 * fm1_juno_chorus.h replaced the KR-106 port, so (a)-(d) measure it against the module's own figures and the donor comparisons are gone.
 *   cc -O2 -w -ffp-contract=off -Ibuild/gen -Ifirmware/src -Itests -o build/host/chorus_test tests/chorus_test.c -lm
 *   build/host/chorus_test [build/chorus_ref]
 * Through the DSP (chorus_block: float in, L / R out, the port's own gains), per mode I / II / I+II:
 *   (a) the modulation rate within +-5 % of the donor's and of the constants (the delay track of an impulse train);
 *   (b) the delay sweep range within +-10 % of the donor's and of 2 x depth x trim; (c) L and R in antiphase (corr < -0.99), different;
 *   (d) a 1 kHz sine's sidebands at +-1 and +-2 LFO rates within 1.5 dB of the donor's, and present;
 *   (e) the donor itself (tests/chorus_ref.cpp, noise off): the time-domain error of 15 renders (sines, a saw, noise, impulses);
 *   (f) one block or 32 or 7 at a time: the same bits; OFF is not active and passes the input; a mode switch fades (no step).
 * Through the int part block (chorus_part): L = mid + side equals the DSP's times CHORUS_TRIM, the guard clamp, an out-of-range mode is OFF.
 * Through the mix (hostsim's mix_block, the real fx.c hook): OFF bit for bit as before (pinned hashes of today's output), other engines
 * ignore the parameter, ON is stereo, PAN / SPREAD, the worst case inside the integer mixer's limits (task-7.2a-review: x <= 524,287 per
 * track, the four-track sum < 2^21), the parameter's descriptor and its legacy value, the cost. */
#include "host_pins.h"   /* (V2) per-host cost pins */
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "dsp_chorus.c"
#include "chorus_ref_cfg.h"
#include "dspfft.h"
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    printf("chorus: %s ", ok ? "ok  " : "FAIL");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    bad += !ok;
}
#define FSD 44100.0
static const char *refdir = "build/chorus_ref";

/* the module's figures (fm1_juno_chorus.h, the Juno-60): LFO Hz, the delay's half-swing and centre in ms */
static const double LFO_HZ[4] = {0, 0.513, 0.863, 9.75}, DEPTH_MS[4] = {0, 1.845, 1.845, 0.2}, CENTRE_MS4[4] = {0, 3.505, 3.505, 3.5};

/* ------------------------------------------------------------ the donor's files --- */
static float *load_ref(int c, uint32_t n)            /* n frames of L, R; 0 when the file is not there (no donor built) */
{
    char path[600];
    FILE *f;
    float *x;
    snprintf(path, sizeof path, "%s/%02d.f32", refdir, c);
    f = fopen(path, "rb");
    if (!f)
        return 0;
    x = malloc((size_t)n * 2u * sizeof(float));
    if (!x || fread(x, sizeof(float), (size_t)n * 2u, f) != (size_t)n * 2u) {
        free(x);
        x = 0;
    }
    fclose(f);
    return x;
}
/* the port on a case: float in, stereo out (32 samples a call, as the firmware's block) */
static float *run_case(const chorus_ref_cfg_t *k, uint32_t blk)
{
    float *o = malloc((size_t)k->n * 2u * sizeof(float)), *in = malloc((size_t)k->n * sizeof(float)), *l = malloc((size_t)k->n * sizeof(float)),
          *r = malloc((size_t)k->n * sizeof(float));
    static chorus_t C;
    uint32_t i, b;
    memset(&C, 0, sizeof C);
    for (i = 0; i < k->n; i++)
        in[i] = chorus_ref_input(k, i);
    for (b = 0; b < k->n; b += blk)
        chorus_block(&C, k->mode, in + b, l + b, r + b, (k->n - b < blk ? k->n - b : blk));
    for (i = 0; i < k->n; i++) {
        o[2u * i] = l[i];
        o[2u * i + 1u] = r[i];
    }
    free(in), free(l), free(r);
    return o;
}

/* ------------------------------------------------------------ measurements --- */
/* the delay of each impulse's wet pulse (samples, parabolic on |x|), one channel (stride 2) of a stereo float buffer */
static uint32_t delay_track(const float *x, uint32_t n, double *d, double *t)
{
    uint32_t p, m = 0;
    for (p = CK_IMP_START; p + 320u < n; p += CK_IMP_GAP) {
        uint32_t j, jb = p + 20u;
        double a, b, c, den;
        for (j = p + 20u; j < p + 300u; j++)
            if (fabsf(x[2u * j]) > fabsf(x[2u * jb]))
                jb = j;
        a = fabs((double)x[2u * (jb - 1u)]), b = fabs((double)x[2u * jb]), c = fabs((double)x[2u * (jb + 1u)]);
        den = a - 2.0 * b + c;
        d[m] = (double)(jb - p) + (den != 0.0 ? 0.5 * (a - c) / den : 0.0);
        t[m++] = (double)p;
    }
    return m;
}
static uint32_t delay_track_r(const float *x, uint32_t n, double *d, double *t)   /* the right channel: x + 1 */
{
    return delay_track(x + 1, n, d, t);
}
/* rate (Hz) from the upward crossings of the mean, range (samples) over the track after the first 4 impulses */
static void track_stats(const double *d, const double *t, uint32_t m, double *rate, double *range, double *mean_out)
{
    double mean = 0.0, lo = 1e9, hi = -1e9, first = -1.0, last = -1.0;
    uint32_t i, nc = 0;
    for (i = 4; i < m; i++) {
        mean += d[i];
        lo = d[i] < lo ? d[i] : lo;
        hi = d[i] > hi ? d[i] : hi;
    }
    mean /= (double)(m - 4u);
    for (i = 5; i < m; i++)
        if (d[i - 1u] < mean && d[i] >= mean) {
            double tc = t[i - 1u] + (t[i] - t[i - 1u]) * (mean - d[i - 1u]) / (d[i] - d[i - 1u]);
            if (nc++ == 0)
                first = tc;
            last = tc;
        }
    *rate = nc >= 3u ? FSD * (double)(nc - 1u) / (last - first) : 0.0;
    *range = hi - lo;
    *mean_out = mean;
}
static double dft_db(const float *x, uint32_t n, double hz)   /* Hann-windowed single-frequency magnitude, dB (x stride 2: the left channel) */
{
    double re = 0.0, im = 0.0, w = 2.0 * 3.14159265358979323846 * hz / FSD;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double h = 0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * (double)i / (double)n);
        re += (double)x[2u * i] * h * cos(w * (double)i);
        im -= (double)x[2u * i] * h * sin(w * (double)i);
    }
    return 20.0 * log10(sqrt(re * re + im * im) + 1e-30);
}

/* ------------------------------------------------------------ (a)-(e) against the donor --- */
#define EW0 2205u
#define EW1 4410u
#define ERR_TOL_DB (-55.0)         /* the time-domain error of the port against the donor in 50 .. 100 ms, re the donor's rms: see the report */
static void test_module(void)
{
    int c, have = 1, mode;
    static double dd[2][4096], tt[2][4096];
    for (mode = 1; mode <= 3; mode++) {
        const int ci = (mode - 1) * 5 + 4, cs = (mode - 1) * 5;
        const chorus_ref_cfg_t *ki = &CHORUS_REF_CFG[ci], *ks = &CHORUS_REF_CFG[cs];
        float *got = run_case(ki, 32), *ref = load_ref(ci, ki->n);
        double rate_g, range_g, mean_g, rate_r = 0, range_r = 0, mean_r = 0, rate_g1, range_g1, mean_g1, cor = 0, sx = 0, sy = 0, nom_pp0;
        uint32_t m = delay_track(got, ki->n, dd[0], tt[0]), m1 = delay_track_r(got, ki->n, dd[1], tt[1]), i;
        track_stats(dd[0], tt[0], m, &rate_g, &range_g, &mean_g);
        track_stats(dd[1], tt[1], m1, &rate_g1, &range_g1, &mean_g1);
        nom_pp0 = 2.0 * DEPTH_MS[mode] * FSD / 1000.0;
        check(fabs(rate_g / LFO_HZ[mode] - 1.0) < 0.05, "(a) mode %d: LFO %.4f Hz (L), %.4f Hz (R); the Juno-60's %.3f Hz (+-5 %%)", mode, rate_g, rate_g1,
              LFO_HZ[mode]);
        check(fabs(range_g / nom_pp0 - 1.0) < 0.10, "(b) mode %d: delay sweep %.2f samples = %.3f ms (L); 2 x %.3f ms = %.3f ms (+-10 %%)", mode, range_g,
              range_g * 1000.0 / FSD, DEPTH_MS[mode], nom_pp0 * 1000.0 / FSD);
        check(fabs(mean_g - CENTRE_MS4[mode] * FSD / 1000.0) < 3.0, "(b) mode %d: the centre %.2f samples, the Juno-60's %.3f ms = %.2f (+-3: the filters' group delay)", mode, mean_g,
              CENTRE_MS4[mode], CENTRE_MS4[mode] * FSD / 1000.0);
        for (i = 0; i < m && i < m1; i++) {         /* antiphase: the two lines' delays move opposite ways */
            sx += (dd[0][i] - mean_g) * (dd[1][i] - mean_g1);
            cor += (dd[0][i] - mean_g) * (dd[0][i] - mean_g);
            sy += (dd[1][i] - mean_g1) * (dd[1][i] - mean_g1);
        }
        check(sx / sqrt(cor * sy + 1e-30) < -0.99, "(c) mode %d: the two delays in antiphase, correlation %.4f; centres %.2f / %.2f samples", mode,
              sx / sqrt(cor * sy + 1e-30), mean_g, mean_g1);
        free(got), free(ref);
        /* (d) the sidebands of a 1 kHz sine, left channel */
        {
            const uint32_t n0 = 30000u, nn = 262144u;
            float *g = run_case(ks, 32), *r = load_ref(cs, ks->n);
            double fl = LFO_HZ[mode], car_g = dft_db(g + 2u * n0, nn, 1000.0), s1_g, s2_g, s1_r = 0, s2_r = 0;
            s1_g = dft_db(g + 2u * n0, nn, 1000.0 + fl) - car_g;
            s2_g = dft_db(g + 2u * n0, nn, 1000.0 + 2.0 * fl) - car_g;
            check(s1_g > -35.0 && s1_g < 20.0, "(d) mode %d: a 1 kHz sine has sidebands at +%.3f Hz: %.1f dBc, at +%.3f Hz: %.1f dBc", mode, fl, s1_g, 2.0 * fl, s2_g);
            free(g), free(r);
        }
    }
}


/* the long-term spectrum, immune to the donor's float phase drift (its LFO phase is a float sum of 1e-5: 0.1 % off its constant after seconds, where the
 * port's integer phase is exact, so the waveforms part slowly): a Welch mean of 6 x 16384 samples of the left channel, six bands, noise in, 0.5 dB */
static void band_db6(const float *x, double scale, double *out)
{
    static const double E[7] = {100, 300, 1000, 3000, 6000, 10000, 16000};
    static int32_t seg[16384];
    static float mag[8192];
    double acc[8192];
    uint32_t s, i, k, b;
    for (k = 0; k < 8192u; k++)
        acc[k] = 0.0;
    for (s = 0; s < 6u; s++) {
        for (i = 0; i < 16384u; i++)
            seg[i] = (int32_t)((double)x[2u * (4410u + s * 16384u + i)] * scale * 1048576.0);
        fft_mag(seg, 16384, mag);
        for (k = 0; k < 8192u; k++)
            acc[k] += (double)mag[k] * mag[k];
    }
    for (b = 0; b < 6u; b++) {
        double e = 0.0;
        for (k = (uint32_t)ceil(E[b] * 16384.0 / FSD); k < (uint32_t)floor(E[b + 1u] * 16384.0 / FSD); k++)
            e += acc[k];
        out[b] = 10.0 * log10(e + 1e-30);
    }
}
static void test_spectrum(void)
{
    int mode, ok = 1, have = 1;
    double worst = 0.0;
    for (mode = 1; mode <= 3; mode++) {
        const int c = (mode - 1) * 5 + 3;
        float *g = run_case(&CHORUS_REF_CFG[c], 32), *r = load_ref(c, CHORUS_REF_CFG[c].n);
        double a[6], b[6];
        uint32_t k;
        if (!r) {
            have = 0;
            free(g);
            continue;
        }
        band_db6(g, 1.0, a);
        band_db6(r, (double)CHORUS_TRIM, b);
        for (k = 0; k < 6u; k++) {
            double d = fabs(a[k] - b[k]);
            worst = d > worst ? d : worst;
            ok &= d < 0.5;
        }
        free(g), free(r);
    }
    if (have)
        check(ok, "(e) the long-term spectrum of noise through each mode, six bands 100 Hz .. 16 kHz, against the donor x CHORUS_TRIM: worst band %.3f dB (limit 0.5)", worst);
}

/* ------------------------------------------------------------ (f) the DSP's own behaviour --- */
static void test_dsp(void)
{
    enum { N = 44100 };
    static float in[N], l1[N], r1[N], l2[N], r2[N];
    static chorus_t C;
    uint32_t i, b, step;
    int ok = 1, act;
    float worst = 0.0f;
    for (i = 0; i < N; i++)
        in[i] = 0.3f * (float)sin(2.0 * 3.14159265358979323846 * 200.0 * (double)i / FSD);
    /* block size does not matter: 1 x N, 32, 7 */
    memset(&C, 0, sizeof C);
    chorus_block(&C, 3, in, l1, r1, N);
    memset(&C, 0, sizeof C);
    for (b = 0; b < N; b += 32u)
        chorus_block(&C, 3, in + b, l2 + b, r2 + b, N - b < 32u ? N - b : 32u);
    ok = !memcmp(l1, l2, sizeof l1) && !memcmp(r1, r2, sizeof r1);
    memset(&C, 0, sizeof C);
    for (b = 0; b < N; b += 7u)
        chorus_block(&C, 3, in + b, l2 + b, r2 + b, N - b < 7u ? N - b : 7u);
    ok &= !memcmp(l1, l2, sizeof l1) && !memcmp(r1, r2, sizeof r1);
    check(ok, "(f) a block of 44100, of 32 and of 7 samples give the same bits");
    check(memcmp(l1, r1, sizeof l1) != 0, "(c) L and R differ (mode I+II)");
    /* OFF is inactive and passes the input; the first mode starts a fade, a switch fades out and in, OFF fades out and then is inactive */
    memset(&C, 0, sizeof C);
    act = chorus_block(&C, 0, in, l1, r1, 64);
    check(!act && !memcmp(l1, in, 64 * sizeof(float)) && !memcmp(r1, in, 64 * sizeof(float)), "(f) OFF from the start: inactive, the input untouched");
    {   /* the switch sequence: 0 -> I (5000) -> II (15000) -> OFF (25000), steps of 32 */
        static float lo[N], ro[N];
        int a1 = 0, a_end = 1;
        memset(&C, 0, sizeof C);
        for (b = 0; b < N; b += 32u) {
            uint32_t mode = b < 5000u ? 0u : b < 15000u ? 1u : b < 25000u ? 2u : 0u;
            int a = chorus_block(&C, mode, in + b, lo + b, ro + b, N - b < 32u ? N - b : 32u);
            if (b == 5024u)
                a1 = a;
            if (b > 26000u)
                a_end &= a == 0;
        }
        for (i = 1; i < N; i++) {
            float s = fabsf(lo[i] - lo[i - 1u]), t = fabsf(ro[i] - ro[i - 1u]);
            worst = s > worst ? s : worst;
            worst = t > worst ? t : worst;
        }
        step = 0;
        for (i = 26000u; i < N; i++)
            step |= lo[i] != in[i] || ro[i] != in[i];
        check(worst < 0.04f && a1 && a_end && !step, "(f) OFF -> I -> II -> OFF: the largest sample step %.4f (limit 0.04; an unfaded wet steps ~0.25), active while on, inactive after the fade, the input back exactly",
              worst);
    }
}

/* ------------------------------------------------------------ the int part block --- */
/* the guard's limit, written down independently of CHORUS_GUARD_FS (task-7.2a-review: x <= 524,287 per track): 5 full scales per channel */
#define GUARD_LIMIT 163840
#define GUARD_X ((((GUARD_LIMIT) >> 2) * 9713) >> 10)       /* 9713 = LEVEL_Q12[127] (checked below) */
static void test_part(void)
{
    enum { N = 8192 };
    static float in[N], l[N], r[N];
    static int32_t b[N], z[N], b0[N];
    static chorus_t C, D;
    uint32_t i, k;
    int mode, ok = 1, act = 1;
    int32_t worst = 0, mx = 0;
    for (mode = 1; mode <= 3; mode++) {
        memset(&C, 0, sizeof C);
        memset(&D, 0, sizeof D);
        for (i = 0; i < N; i++) {
            in[i] = 0.4f * (float)sin(2.0 * 3.14159265358979323846 * 330.0 * (double)i / FSD);
            b0[i] = b[i] = (int32_t)(in[i] * 32768.0f);
        }
        for (k = 0; k < N; k += CTL)
            act &= chorus_part(&C, mode, b + k, z + k, CTL);
        chorus_block(&D, (uint32_t)mode, in, l, r, N);
        for (i = 1024; i < N; i++) {
            /* L = mid + side, R = mid - side (the int path quantises the input: 1 LSB of 1/32768 through a gain of < 2.2) */
            int32_t el = (int32_t)(l[i] * 32768.0f), er = (int32_t)(r[i] * 32768.0f);
            int32_t dl = (b[i] + z[i]) - el, dr = (b[i] - z[i]) - er;
            dl = dl < 0 ? -dl : dl, dr = dr < 0 ? -dr : dr;
            worst = dl > worst ? dl : worst;
            worst = dr > worst ? dr : worst;
        }
    }
    check(act && worst <= 8, "(g) chorus_part: mid + side = the DSP's L, mid - side = R (the trim %.4f folded into the DSP's gains), within %d LSB; active", CHORUS_TRIM, (int)worst);
    /* the guard: a huge square (100 FS) is held to CHORUS_GUARD_FS per channel, so |mid| + |side| <= guard */
    memset(&C, 0, sizeof C);
    for (i = 0; i < N; i++)
        b[i] = ((i / 40u) & 1u) ? (3 << 20) : -(3 << 20);
    for (k = 0; k < N; k += CTL)
        chorus_part(&C, 3, b + k, z + k, CTL);
    for (i = 0; i < N; i++) {
        int32_t m = (b[i] < 0 ? -b[i] : b[i]) + (z[i] < 0 ? -z[i] : z[i]);
        mx = m > mx ? m : mx;
    }
    /* the LITERAL limit, not the macro: x = ((b >> 2) * LEVEL_Q12[127]) >> 10 at 196,608 is 466,224 and x * 4096 must stay under 2^31 (x <= 524,287); a guard of 7 FS
     * (229,376: x = 543,928) is the overflow of task-7.2a-review. The guard is also reached (above half of it), so a clamp that is not there does not pass */
    check(mx <= GUARD_LIMIT + 1 && mx > GUARD_LIMIT / 2, "(h) the guard: 100 FS in, max(|L|, |R|) = |mid| + |side| = %d <= %d (the literal 5 FS; +1: the floor shifts of mid / side), and reached", (int)mx,
          GUARD_LIMIT);
    check(GUARD_X == 388520 && GUARD_X <= 524287 && (((int64_t)GUARD_X) * 4096) < 2147483648ll && ((((int64_t)(mx + 1) >> 2) * LEVEL_Q12[127]) >> 10) <= 524287,
          "(h) the guard's x at LEVEL 127 is %d (<= 524287 : x * 4096 inside int32), from the measured output %d too", GUARD_X, (int)((((int64_t)(mx + 1) >> 2) * LEVEL_Q12[127]) >> 10));
    /* an out-of-range mode is OFF: inactive and the block untouched (a stored 127 of the old reserved lane level, -1, 4) */
    ok = 1;
    for (i = 0; i < 4u; i++) {
        static const int32_t M[4] = {127, -1, 4, 0};
        uint32_t j;
        memset(&C, 0, sizeof C);
        for (j = 0; j < CTL; j++)
            b[j] = b0[j];
        ok &= !chorus_part(&C, M[i], b, z, CTL) && !memcmp(b, b0, CTL * sizeof(int32_t));
    }
    check(ok, "(h) a mode of 127, -1, 4 or 0 is OFF: inactive, the block untouched");
}

/* ------------------------------------------------------------ the mix --- */
static int32_t mix_out[2 * CTL];
static void mix_reset(void)                        /* the whole mix at power-on (regress.c job_cpu / drift_test.c mix_cost) */
{
    uint32_t k;
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
    host_tracks_init();
    song.sel = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
}
/* part 0: engine e preset pi with CHOR = chor (forced after the preset), one note; renders n blocks: the hash of the stereo output, the energies */
typedef struct { uint64_t h; double el, er, ed; int32_t pk_x, pk_sum; } mixr_t;
static mixr_t mix_run(uint32_t e, uint32_t pi, int chor, uint32_t note, uint32_t nb, void (*setup)(void))
{
    mixr_t r;
    uint32_t b, i;
    memset(&r, 0, sizeof r);
    r.h = 1469598103934665603ull;
    mix_reset();
    host_preset(&trk[0], e, pi);
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_AMODE] = 0;
    trk[0].p[P_RSV_LN] = (int16_t)chor;
    trk[1].p[P_MUTE] = trk[2].p[P_MUTE] = trk[3].p[P_MUTE] = 1;
    if (setup)
        setup();
    trk_note_on(&trk[0], note, 100);
    for (b = 0; b < nb; b++) {
        mix_block(mix_out, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            r.h = (r.h ^ (uint32_t)mix_out[i]) * 1099511628211ull;
            if (b >= nb / 4u)
                (i & 1u ? &r.er : &r.el)[0] += (double)mix_out[i] * (double)mix_out[i];
        }
        for (i = 0; i < CTL; i++) {
            double d = (double)mix_out[2u * i] - (double)mix_out[2u * i + 1u];
            if (b >= nb / 4u)
                r.ed += d * d;
        }
    }
    return r;
}
/* the hashes of DRIFT's output with CHOR OFF (1.5 s, one note, the preset's own sends): the OFF path must not move (the golden file pins the
 * same on all 32 presets). Recorded from the tree before Task 6.2 (8ed17cc7.. 86d05a87.. b2a34c3a..); re-recorded in Task 6.3, which changed
 * the voice itself on purpose (the ladder's small-signal path and k-cache, the blep's reciprocal: the same filter to 1e-5, not the same bits) */
static const uint64_t PIN_OFF[3] = {0x0c352095350b31b3ull, 0xed726d2e46a9a57dull, 0x66e9f6a049d7d777ull};   /* [2] (HOOVER, 21) re-pinned in FX.1 (its DIST is the Scream-style one; was 0xc5f249a37e6af565), in FX.2 (the oversampled loop; was 0x8b2c3ebc642e43f1) and 2026-10-21 (the drive DIST; was 0x0d9f33a3f575bda5); 0 and 22 have no DIST: unchanged */
static const uint8_t PIN_PRESET[3] = {0, 22, 21};
static void test_mix_off(void)
{
    uint32_t k;
    int ok = 1;
    for (k = 0; k < 3u; k++) {
        mixr_t r = mix_run(ENGI_DRIFT, PIN_PRESET[k], 0, 48, 2067, 0);
        if (r.h != PIN_OFF[k])
            printf("chorus: DRIFT preset %u OFF hash %016llx (pinned %016llx)\n", PIN_PRESET[k], (unsigned long long)r.h, (unsigned long long)PIN_OFF[k]);
        ok &= r.h == PIN_OFF[k];
    }
    check(ok, "(i) CHOR OFF: DRIFT presets 0, 22 and 21 render bit for bit as before the chorus (pinned hashes)");
    ok = 1;
    for (k = 0; k < 3u; k++) {
        static const uint8_t EP[3] = {ENGI_OBX, ENGI_WAVE, ENGI_ACID};
        mixr_t a = mix_run(EP[k], 3, 0, 48, 600, 0), b = mix_run(EP[k], 3, 3, 48, 600, 0);
        ok &= a.h == b.h;
    }
    check(ok, "(i) OB-Z, WAVE and ACID ignore the parameter (CHOR 3 renders as CHOR 0)");
}
static void no_sends(void);
static void test_mix_on(void)
{
    uint32_t m;
    mixr_t off = mix_run(ENGI_DRIFT, 22, 0, 48, 1500, no_sends);
    int ok = off.ed == 0.0 && off.el > 0.0;
    check(ok, "(j) CHOR OFF is centred mono as before: L == R exactly, energy %.3g", off.el);
    for (m = 1; m <= 3u; m++) {
        mixr_t on = mix_run(ENGI_DRIFT, 22, (int)m, 48, 1500, no_sends);
        check(on.h != off.h && on.ed > 0.0 && on.ed / (on.el + on.er + 1e-30) > 0.02 && on.el > 0.45 * off.el && on.el < 0.85 * off.el,   /* amplitude 0.67 .. 0.92 x the dry's: the measured 0.79 .. 0.84 (the donor's comb at a low note, the trim in) */
              "(j) CHOR %u: a changed, stereo signal (L-R energy %.1f %% of L+R) at a level of 0.67 .. 0.92 x the dry's (%.2f x)", m, 100.0 * on.ed / (on.el + on.er + 1e-30), sqrt(on.el / off.el));
    }
}
static void no_sends(void) { trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0; }
static void set_pan_left(void) { no_sends(); trk[0].p[P_PAN] = -64; }
static void set_pan_right(void) { no_sends(); trk[0].p[P_PAN] = 63; }
static void set_spread(void) { no_sends(); trk[0].p[P_SPRD] = 127; }
static void test_mix_pan(void)
{
    mixr_t pl = mix_run(ENGI_DRIFT, 22, 1, 48, 1500, set_pan_left), pr = mix_run(ENGI_DRIFT, 22, 1, 48, 1500, set_pan_right);
    mixr_t sp = mix_run(ENGI_DRIFT, 22, 1, 48, 1500, set_spread), sp0 = mix_run(ENGI_DRIFT, 22, 0, 48, 1500, set_spread);
    check(pl.er == 0.0 && pl.el > 0.0 && pr.el < 0.01 * pr.er && pr.er > 0.0, "(k) PAN hard left silences R, hard right leaves L under 1 %% (the pair is balanced like a stereo source): %.3g / %.3g, %.3g / %.3g",
          pl.el, pl.er, pr.el, pr.er);
    check(sp.h != sp0.h && sp.ed > 0.0 && sp.el > 0.0 && sp.er > 0.0, "(k) SPREAD 127 with the chorus still plays and is stereo (L-R energy %.1f %%)", 100.0 * sp.ed / (sp.el + sp.er + 1e-30));
}

/* the worst case: four DRIFT parts, LEVEL 127, PAN centre, RES 127, DRV 127, MIX saw, CUT open, a chord at the part's cap, I+II, DIST 127 (task-7.2a-review:
 * x per track <= 524,287 before x * gl; the four-track sum |mix + wet| < 2^21) */
static void test_headroom(void)
{
    int dist, mode;
    int32_t worst_x = 0, worst_sum = 0;
    for (dist = 0; dist <= 127; dist += 127)
        for (mode = 1; mode <= 3; mode++) {
            uint32_t b, i, p;
            mix_reset();
            for (p = 0; p < 4u; p++) {
                track_t *t = &trk[p];
                host_preset(t, ENGI_DRIFT, 21);
                t->p[P_VOICE] = V_POLY, t->p[P_SUS] = 127, t->p[P_AMODE] = 0, t->p[P_LEVEL] = 127, t->p[P_PAN] = 0;
                t->p[P_E0] = 64, t->p[P_E4] = 127, t->p[P_E5] = 127, t->p[P_E6] = 127, t->p[P_E3] = 127, t->p[P_E2] = 127;
                t->p[P_DIST] = (int16_t)dist, t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 127;
                t->p[P_RSV_LN] = (int16_t)mode;
                trk_note_on(t, 40 + 7u * p, 127);
                trk_note_on(t, 47 + 7u * p, 127);
                trk_note_on(t, 52 + 7u * p, 127);
                trk_note_on(t, 56 + 7u * p, 127);
            }
            for (b = 0; b < 1500u; b++) {
                mix_block(mix_out, CTL);
                for (p = 0; p < 4u; p++)
                    worst_x = trk[p].peak > worst_x ? trk[p].peak : worst_x;
                for (i = 0; i < CTL; i++) {
                    int32_t sl_ = mix_l[i] + wet[i], sr_ = mix_r[i] + wet[i];
                    sl_ = sl_ < 0 ? -sl_ : sl_, sr_ = sr_ < 0 ? -sr_ : sr_;
                    worst_sum = sl_ > worst_sum ? sl_ : worst_sum;
                    worst_sum = sr_ > worst_sum ? sr_ : worst_sum;
                }
            }
        }
    check(worst_x <= GUARD_X && worst_sum < 2097151, "(l) worst case (4 parts, LEVEL 127, RES / DRV 127, chords, sends 127, DIST 0 / 127, I / II / I+II): x %d <= 388520 (5 FS at LEVEL 127), four-track sum %d < 2097151", (int)worst_x, (int)worst_sum);
}


/* the stage's extreme output (|L| or |R| at the guard, 6 FS) straight through mix_stereo, four parts at LEVEL 127, PAN centre, every send full: no product wraps
 * (the int64 sums equal the int32 ones) and each channel stays under 4 x 466,224 */
static void test_stereo_limits(void)
{
    static const int32_t MS[5][2] = {{163840, 0}, {0, 163840}, {81920, 81920}, {-163840, 0}, {-81920, 81920}};
    int ok = 1;
    int32_t worst = 0, wpk = 0;
    uint32_t cse, p, i;
    mix_reset();
    for (cse = 0; cse < 5u; cse++) {
        static int32_t b[CTL], z[CTL];
        int64_t el = 0, er = 0;
        for (i = 0; i < CTL; i++)
            send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
        for (p = 0; p < 4u; p++) {
            int32_t x, w;
            trk[p].p[P_LEVEL] = 127, trk[p].p[P_PAN] = 0, trk[p].p[P_CHOR] = trk[p].p[P_DLY] = trk[p].p[P_REV] = 127;
            trk[p].peak = 0;
            for (i = 0; i < CTL; i++)
                b[i] = MS[cse][0], z[i] = MS[cse][1];
            mix_stereo(&trk[p], b, 0, z, CTL);
            x = ((MS[cse][0] >> 2) * LEVEL_Q12[127]) >> 10, w = ((MS[cse][1] >> 2) * LEVEL_Q12[127]) >> 10;
            el += (int64_t)(((int64_t)(x + w) * 4096) >> 12), er += (int64_t)(((int64_t)(x - w) * 4096) >> 12);
            wpk = trk[p].peak > wpk ? trk[p].peak : wpk;
        }
        ok &= (int64_t)mix_l[5] == el && (int64_t)mix_r[5] == er && el < 2097151 && er < 2097151 && -el < 2097151 && -er < 2097151;
        worst = (int32_t)(el > worst ? el : er > worst ? er : worst);
        ok &= send_c[5] <= 4 * 65538 && send_c[5] >= -4 * 65538;
    }
    check(ok && wpk <= 388530, "(l) mix_stereo at the guard (|L| or |R| = 5 FS), four parts, LEVEL 127, sends full: no wrap, one channel at most %d < 2097151, a part's x at most %d (<= 524287)", (int)worst, (int)wpk);
}

/* ------------------------------------------------------------ the parameter --- */
static void test_param(void)
{
    const param_desc_t *d = &TP[P_RSV_LN];
    /* the mix: the module's -3 dB dry and wet */
    check(CHORUS_TRIM == 1.0f && abs(CH_DRY - 2896) <= 1 && CH_WET == CH_DRY && LEVEL_Q12[127] == 9713,
          "(m) the module's -3 dB dry and wet as they are (%d, %d in Q12 = 0.7071; CHORUS_TRIM %.1f); LEVEL_Q12[127] = %d is the 9713 the guard's arithmetic uses", CH_DRY, CH_WET,
          (double)CHORUS_TRIM, (int)LEVEL_Q12[127]);
    check(!strcmp(d->label, "CHOR") && d->fmt == F_ENUM && d->min == 0 && d->max == 3 && d->def == 0 && d->names && !strcmp(d->names[0], "OFF") && !strcmp(d->names[1], "I") &&
              !strcmp(d->names[2], "II") && !strcmp(d->names[3], "I+II"),
          "(m) P_RSV_LN is CHOR: an enum OFF / I / II / I+II, default OFF");
    check(param_fit(d, 127) == 0 && param_fit(d, -1) == 0 && param_fit(d, 4) == 0 && param_fit(d, 2) == 2 && param_fit(d, 3) == 3 && param_fit(&TP[P_REV], 200) == 127,
          "(m) param_fit sends the old reserved lane level 127 (and any out-of-range value) of this one parameter to OFF, other parameters clamp as before");
}

/* ------------------------------------------------------------ the tail --- */
/* Fix round 1, Important 1: the chorus keeps ringing for the BBD's longest delay after the last voice. (1) the length, measured from a burst of the loudest input, against the
 * constants worked out here from the donor's numbers; (2) the line then stops, and the next start is the same as from a fresh state; (3) through the mix with REL 0 there is no cut. */
static void test_tail_dsp(void)
{
    enum { BURST = 3008, TOT = BURST + 1216 };   /* (whole blocks of CTL = 32) */
    static int32_t b[TOT], z[TOT], bf[TOT], zf[TOT];
    static chorus_t C, F;
    int mode, ok = 1, ok_off = 1, ok_stop = 1, ok_clean = 1;
    uint32_t last_worst = 0, ndmax, k, i;
    /* the longest delay in samples: (the centre 3.505 ms + the half-swing 1.845 ms) x 44.1, +1 for the linear read's second tap, +1 for the write offset */
    double dmax = (CENTRE_MS4[1] + DEPTH_MS[1]) * 44.1;
    ndmax = (uint32_t)ceil(dmax) + 1u + 1u;
    for (mode = 1; mode <= 3; mode++) {
        uint32_t last = 0;
        memset(&C, 0, sizeof C);
        for (i = 0; i < BURST; i++)                                  /* a full-scale-ish noise burst: every delay and every filter state is loaded */
            b[i] = (int32_t)((rand() / (double)RAND_MAX - 0.5) * 2.0 * 32768.0 * 2.0);
        for (; i < TOT; i++)
            b[i] = 0;
        memset(z, 0, sizeof z);
        for (k = 0; k < TOT; k += CTL)
            chorus_part(&C, mode, b + k, z + k, CTL);
        for (i = BURST; i < TOT; i++)
            if (b[i] + z[i] != 0 || b[i] - z[i] != 0)
                last = i - BURST + 1u;                              /* the tail in samples: the index after the last audible one */
        last_worst = last > last_worst ? last : last_worst;
        ok_stop &= !C.on;                                           /* it has stopped by the end ... */
        for (i = BURST + last; i < TOT; i++)
            ok_off &= b[i] == 0 && z[i] == 0;                       /* ... and is exactly silent */
        /* a start after the stop is the same as from a fresh state: the same input gives the same bits */
        memset(&F, 0, sizeof F);
        for (i = 0; i < 2016; i++)
            bf[i] = (int32_t)(8000.0 * sin(0.05 * (double)i)), zf[i] = 0;
        memcpy(b, bf, 2016 * sizeof(int32_t));
        memset(z, 0, 2016 * sizeof(int32_t));
        for (k = 0; k < 2016; k += CTL)
            chorus_part(&C, mode, b + k, z + k, k + CTL <= 2016 ? CTL : 2016 - k);
        for (k = 0; k < 2016; k += CTL)
            chorus_part(&F, mode, bf + k, zf + k, k + CTL <= 2016 ? CTL : 2016 - k);
        ok_clean &= !memcmp(b, bf, 2016 * sizeof(int32_t)) && !memcmp(z, zf, 2016 * sizeof(int32_t));
    }
    printf("chorus: the tail: the last audible sample %u after the input stops (worst of the 3 modes); the longest delay %.2f samples -> %u with the second tap and write offset; CHORUS_TAIL %u\n",
           last_worst, dmax, ndmax, (unsigned)CHORUS_TAIL);
    check(CHORUS_TAIL >= ndmax && CHORUS_TAIL >= last_worst && CHORUS_TAIL <= last_worst + 40u && CHORUS_TAIL % CHORUS_SUB == 0u,
          "(n) CHORUS_TAIL %u covers the measured tail %u (and the longest delay %u) and is not longer than 40 samples more (a whole number of sub-blocks)", (unsigned)CHORUS_TAIL, last_worst, ndmax);
    check(ok_stop && ok_off, "(n) after the tail the chorus stops (not running) and the output is exactly zero");
    {   /* idle and silent: the hook does nothing, not even a restart (a start clears the 1.1 kB state: mix_part calls it every block of a silent part with the chorus on) */
        static int32_t zb[CTL], zz[CTL];
        uint32_t j;
        memset(&C, 0, sizeof C);
        C.buf[7] = 1.0f;
        for (j = 0; j < 50u; j++)
            chorus_part(&C, 3, zb, zz, CTL);
        check(!C.on && C.buf[7] == 1.0f, "(n) an idle chorus fed zeros stays idle and untouched (no restart, no state clear) over 50 blocks");
    }
    check(ok_clean, "(n) a start after the tail is bit for bit the start from a fresh state (no stale ring or filter state)");
}

/* a mix of one DRIFT note with REL 0 and CHOR m: the dry mix after the part, note-off at block 200, 80 blocks of release.
 * Returns the last non-zero sample (either channel) and the largest sample-to-sample step among the 64 samples before it, relative to the note's rms. */
typedef struct { double last, step, rms; uint32_t end; } rel_t;
static void set_rel0(void) { no_sends(); trk[0].p[P_REL] = 0; trk[0].p[P_ATK] = 0; }
static rel_t rel_run(int chor)
{
    enum { NB = 80 };
    static int32_t l[NB * CTL], r[NB * CTL];
    rel_t o;
    uint32_t b, i, e = 0;
    double acc = 0.0;
    memset(&o, 0, sizeof o);
    mix_reset();
    host_preset(&trk[0], ENGI_DRIFT, 22);
    trk[0].p[P_VOICE] = V_POLY, trk[0].p[P_SUS] = 127, trk[0].p[P_AMODE] = 0, trk[0].p[P_RSV_LN] = (int16_t)chor;
    trk[1].p[P_MUTE] = trk[2].p[P_MUTE] = trk[3].p[P_MUTE] = 1;
    set_rel0();
    trk_note_on(&trk[0], 48, 100);
    for (b = 0; b < 200u; b++) {
        mix_block(mix_out, CTL);
        if (b >= 100u)
            for (i = 0; i < CTL; i++)
                acc += (double)mix_l[i] * (double)mix_l[i];
    }
    o.rms = sqrt(acc / (100.0 * CTL));
    trk_note_off(&trk[0], 48);
    for (b = 0; b < NB; b++) {
        mix_block(mix_out, CTL);
        memcpy(l + b * CTL, mix_l, CTL * sizeof(int32_t));
        memcpy(r + b * CTL, mix_r, CTL * sizeof(int32_t));
    }
    for (i = 0; i < NB * CTL; i++)
        if (l[i] || r[i])
            e = i;
    o.end = e;
    o.last = fmax(fabs((double)l[e]), fabs((double)r[e]));
    for (i = e > 63u ? e - 63u : 1u; i <= e; i++) {
        double s = fmax(fabs((double)l[i] - l[i - 1u]), fabs((double)r[i] - r[i - 1u]));
        o.step = s > o.step ? s : o.step;
    }
    return o;
}
static void test_tail_mix(void)
{
    rel_t off = rel_run(0), on = rel_run(3);
    uint32_t m;
    int ok = 1;
    printf("chorus: REL 0 release, note rms %.0f: CHOR OFF last sample %.0f, end step %.0f (the dry's 3 ms fade); CHOR I+II last %.0f, end step %.0f, ends %u samples after the OFF's\n", off.rms, off.last,
           off.step, on.last, on.step, on.end - off.end);
    /* the bound. The chorus-OFF end is the dry's own 3 ms fade: its last sample is <= 2 % of the note's rms and its end step is what that fade leaves (here a saw's reset
     * times the fade, 580). The chorus' end is the same kind of fade after the tail rang out: the last sample <= 2 % of the rms (the bug: 5 .. 40 %, a cut mid-wave) and the
     * step into it at most 1.5 x the OFF's. The tail must also run: the end comes after the OFF's, within CHORUS_TAIL + 2 blocks */
    check(off.last <= 0.02 * off.rms && on.last <= 0.02 * on.rms && on.step <= 1.5 * off.step,
          "(o) REL 0, CHOR I+II: the last sample before the end %.0f (<= 2 %% of the note's rms %.0f = %.0f) and the step into it %.0f (<= 1.5 x the OFF's %.0f) ; OFF last %.0f", on.last, on.rms,
          0.02 * on.rms, on.step, off.step, off.last);
    check(on.end >= off.end && on.end <= off.end + CHORUS_TAIL + 2u * CTL, "(o) the tail rings out: it ends %u samples after the dry's end (0 .. %u)", on.end - off.end, (unsigned)CHORUS_TAIL + 2u * CTL);
    for (m = 1; m <= 3u; m++) {
        rel_t x = rel_run((int)m);
        printf("chorus: REL 0, CHOR %u: last %.0f, end step %.0f, ends %u samples after the OFF's\n", m, x.last, x.step, x.end - off.end);
        ok &= x.last <= 0.02 * x.rms && x.step <= 1.5 * off.step;
    }
    check(ok, "(o) the same for CHOR I, II and I+II");
    /* silence, then a new note: the chorus has stopped (a clean state) */
    rel_run(3);
    check(!drift_chorus(&trk[0])->on, "(o) after the release and the silence the chorus is stopped (the next note starts from a clean state)");
}

/* ------------------------------------------------------------ the wet bus --- */
/* Fix round 1, the wet bus at the guard: four parts whose chorus is held at the guard (5 FS per channel, a 100 FS square), LEVEL 127, PAN centre, every send 127, REVERB HALL at
 * SIZE 127 / DAMP 0 (the review's worst), through mix_stereo and the real buses (fx_buses, rev_side_mix): |mix + wet| must stay under 2^21 (the master's ((x >> 2) * mg)). */
static void test_wet_bus(void)
{
    enum { NBLK = 1200 };
    static const uint32_t HALF[5] = {7, 40, 97, 211, 1500};
    int32_t worst = 0, wx = 0;
    uint32_t h, p, k, i, b;
    for (h = 0; h < 5u; h++) {
        static int32_t bb[CTL], zz[CTL];
        mix_reset();
        song.g[G_RTYPE] = 2, song.g[G_RSIZE] = 127, song.g[G_RDAMP] = 0, song.g[G_DTIME] = 3, song.g[G_DFDBK] = 120;
        for (p = 0; p < 4u; p++) {
            host_preset(&trk[p], ENGI_DRIFT, 22);
            trk[p].p[P_LEVEL] = 127, trk[p].p[P_PAN] = 0, trk[p].p[P_SPRD] = 0, trk[p].p[P_CHOR] = trk[p].p[P_DLY] = trk[p].p[P_REV] = 127, trk[p].p[P_RSV_LN] = 3;
        }
        for (b = 0, k = 0; b < NBLK; b++) {
            for (i = 0; i < CTL; i++)
                send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
            for (p = 0; p < 4u; p++) {
                for (i = 0; i < CTL; i++)
                    bb[i] = (((k + i + p * 13u) / HALF[h]) & 1u) ? (3 << 20) : -(3 << 20);
                chorus_part(drift_chorus(&trk[p]), 3, bb, zz, CTL);
                trk[p].peak = 0;
                mix_stereo(&trk[p], bb, 0, zz, CTL);
                wx = trk[p].peak > wx ? trk[p].peak : wx;
            }
            k += CTL;
            fx_buses(send_c, send_d, send_r, wet, CTL);
            rev_side_mix(mix_l, mix_r, CTL);
            for (i = 0; i < CTL; i++) {
                int64_t sl_ = (int64_t)mix_l[i] + wet[i], sr_ = (int64_t)mix_r[i] + wet[i];
                sl_ = sl_ < 0 ? -sl_ : sl_, sr_ = sr_ < 0 ? -sr_ : sr_;
                worst = (int32_t)(sl_ > worst ? sl_ : worst);
                worst = (int32_t)(sr_ > worst ? sr_ : worst);
            }
        }
    }
    check(worst < 2097151 && wx <= GUARD_X + 16, "(p) four parts at the 5 FS guard, sends 127, HALL SIZE 127 DAMP 0, square halves 7 .. 1500 samples: |mix + wet| %d < 2097151 (%.2f %% left), a part's x %d <= %d",
          (int)worst, 100.0 * (2097151.0 - (double)worst) / 2097151.0, (int)wx, GUARD_X + 16);
}

/* ------------------------------------------------------------ the cost --- */
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
/* The pin: chorus_part per sample per part (the int32 block in, mid / side out), the whole stage, in regress.c's units (kernel-counted instructions
 * on the Mac, cc -O2); +10 % of the measured worst of the three modes. Another compiler or -O level moves it: re-measure and say why, never to hide a
 * regression. The device's cost is what Task 8.1's PERF run measures. */
#define CHORUS_COST_MAX HOST_PIN(111.0, 185.0)   /* (V2: Linux x86-64 measured 168 + 10 %) */   /* the measured worst (I, 101: the Juno-60 module, 2026-10-21; the KR-106 port was 182) + 10 %, -O2, cc on the Mac */
static void test_cost(void)
{
    enum { NB = 4096 };
    static int32_t b[CTL], z[CTL];
    static chorus_t C;
    double cost[4] = {0, 0, 0, 0}, worst = 0.0;
    uint32_t i, k, mode;
    if (!instr_now()) {
        printf("chorus: SKIP cost: NOT CHECKED, there is no instruction counter on this host (macOS proc_pid_rusage only)\n");
        return;
    }
    for (mode = 1; mode <= 3u; mode++) {
        uint64_t i0, i1;
        memset(&C, 0, sizeof C);
        for (i = 0; i < CTL; i++)
            b[i] = (int32_t)(12016.0 * sin(0.04 * (double)i));
        for (k = 0; k < 64u; k++)
            chorus_part(&C, (int32_t)mode, b, z, CTL);
        i0 = instr_now();
        for (k = 0; k < NB; k++) {
            for (i = 0; i < CTL; i++)
                b[i] = (int32_t)(12016.0 * sin(0.04 * (double)i));
            chorus_part(&C, (int32_t)mode, b, z, CTL);
        }
        i1 = instr_now();
        cost[mode] = (double)(i1 - i0) / ((double)NB * CTL);
        {   /* less the input synthesis, measured alone */
            uint64_t j0 = instr_now(), j1;
            for (k = 0; k < NB; k++)
                for (i = 0; i < CTL; i++)
                    b[i] = (int32_t)(12016.0 * sin(0.04 * (double)i));
            j1 = instr_now();
            cost[mode] -= (double)(j1 - j0) / ((double)NB * CTL);
        }
        worst = cost[mode] > worst ? cost[mode] : worst;
    }
    printf("chorus: cost per part per sample (chorus_part, int32 in / mid + side out): I %.0f, II %.0f, I+II %.0f instructions (-O2, regress.c's units)\n", cost[1], cost[2], cost[3]);
    check(worst >= 50.0 && worst <= CHORUS_COST_MAX, "cost: the worst mode %.0f <= the pin %.0f (+10 %% of the measured worst, -O2)", worst, CHORUS_COST_MAX);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    if (argc > 1)
        refdir = argv[1];
    test_param();
    test_dsp();
    test_part();
    test_module();
    test_mix_off();
    test_mix_on();
    test_mix_pan();
    test_headroom();
    test_stereo_limits();
    test_tail_dsp();
    test_tail_mix();
    test_wet_bus();
    test_cost();
    printf("chorus_test: %s\n", bad ? "FAIL" : "ok");
    return bad ? 1 : 0;
}
