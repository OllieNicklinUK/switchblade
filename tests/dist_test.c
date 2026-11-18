/* SPDX-License-Identifier: GPL-3.0-only */
/* DIST: the per-track distortion (src/fx.c track_dist), through the firmware's own code. Task FX.1 / FX.2 (the Scream-style loop); 2026-10-21 the
 * integer drive of fm1_drive_1.h replaced it: the Scream-only checks (the frozen resonance, the oversampler) are gone, the alias figures are pinned
 * to the drive's own (it has no anti-aliasing), the rest stands.
 *   dist_test              the checks
 *   dist_test --wav OUTDIR [P_DIST ...]   the listening renders (OB-Z bass, NANO line, JUNO chord; default P_DIST 40 / 60 / 80 / 100 / 127), tagged drive
 *   dist_test --old        (build against an earlier commit: its DIST's renders, P_DIST 80 / 127, tagged old; the FX.2 comparison uses FX.1's)
 * The checks: P_DIST 0 a bit-identical bypass; switching on without a click; the output bounded (|out| <= 32767) at
 * the worst input and P_DIST (an OVERDRIVE ACID patch, a dense JUNO chord, 8 FS noise / squares); silence in, silence
 * out within 50 ms (the gate: no self-oscillation) and clean state after the part's tail; no NaN / denormal state after
 * a long silence; a P_DIST sweep without zipper; the level against the old DIST (a saw chord at P_DIST 32 / 64 / 96 /
 * 127, within +-3 dB); the alias floor of a 2 kHz sine; the cost (macOS only: a loud SKIP elsewhere).
 * FX.2 (oversampled loop, DIST_OS 4 by default, 2 as a fallback: build with -DDIST_OS=2): the alias floor of a band-limited saw
 * at 0.7 FS (110 / 440 Hz, bin-centred) at P_DIST 51 / 76 / 102 / 127; the resonance frozen above d = 0.4; the intensity (the
 * harmonic density of a sine) rising monotonically with the knob. */

#define AUDITION_NO_MAIN
#include "host_pins.h"   /* (V2) per-host cost pins */
#include <stdarg.h>
#include "audition.c"
#include "dspfft.h"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    printf("dist: %s ", ok ? "ok  " : "FAIL");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    bad += !ok;
}

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

/* ---- the old DIST (the commit before FX.1), verbatim, for the level and cost comparisons ---- */
typedef struct { int32_t hp, lp1, lp2; } old_st_t;
static void old_dist(old_st_t *t, int32_t d, int32_t *b, uint32_t n)
{
    int32_t i, g, k, mk, bias = 2400, b0;
    if (!d)
        return;
    g = 4096 + d * d * 2;
    k = 32000 - d * 95;
    mk = 30000 - d * 120;
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->hp += (x - t->hp + 64) >> 7;
        x = clamp(x - t->hp, -230000, 230000);
        y = softclip((((x >> 2) * g) >> 10) + bias) - b0;
        t->lp1 += mulq15(y - t->lp1, k);
        t->lp2 += mulq15(t->lp1 - t->lp2, k);
        b[i] = mulq15(t->lp2, mk);
    }
}

/* the drive's own figures (2026-10-21, measured, pinned): the alias floor of test 7b (dB, 110 / 440 Hz x P_DIST 51 / 76 / 102 / 127),
 * the cost (host instructions a sample, +10 %), the state after the part's tail (FS) */
static const double ALIAS_PIN[2][4] = {{-32.4, -33.1, -34.3, -35.2}, {-26.9, -27.3, -28.3, -29.1}};
#define DIST_COST_MAX HOST_PIN(52.0, 70.0)   /* measured 46.6 on the Mac, 63.8 on Linux x86-64 (the Scream loop: 370) */
#define DIST_MIX_MAX 155.0       /* measured +140 (the Scream loop: +1117) */
#define DIST_TAIL_FS 0.001       /* measured 0: the state settles exactly */
static track_t T;                                 /* the track the DIST is run on (state: dst, dist_hp) */
static void run_new(int32_t *x, uint32_t n, int p)
{
    uint32_t i;
    T.p[P_DIST] = (int16_t)p;
    for (i = 0; i < n; i += CTL)
        track_dist(&T, x + i, CTL);
}
static void run_old(old_st_t *o, int32_t *x, uint32_t n, int p)
{
    uint32_t i;
    for (i = 0; i < n; i += CTL)
        old_dist(o, p, x + i, CTL);
}

static uint32_t drng = 0x2545F491u;
static int32_t rnd(void)
{
    drng ^= drng << 13;
    drng ^= drng >> 17;
    drng ^= drng << 5;
    return (int32_t)(drng >> 8) - 0x800000;        /* +-2^23 */
}
static double rms(const int32_t *x, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / n);
}
static int32_t peak(const int32_t *x, uint32_t n)
{
    int32_t m = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        m = abs(x[i]) > m ? abs(x[i]) : m;
    return m;
}
/* a chord of naive saws (C3 E3 G3 B3), amp each */
static void saw_chord(int32_t *x, uint32_t n, double amp)
{
    static const int NOTE[4] = {48, 52, 55, 59};
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        double s = 0;
        for (k = 0; k < 4u; k++) {
            double f = 440.0 * pow(2.0, (NOTE[k] - 69) / 12.0), ph = fmod(f * i / 44100.0, 1.0);
            s += (2.0 * ph - 1.0) * amp;
        }
        x[i] = (int32_t)s;
    }
}
static void sine(int32_t *x, uint32_t n, double hz, double amp)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        x[i] = (int32_t)(amp * sin(2.0 * M_PI * hz * i / 44100.0));
}

/* 1: P_DIST 0 is a bypass, bit for bit, state untouched */
static void test_bypass(void)
{
    static int32_t a[CTL * 64], b[CTL * 64];
    uint32_t i;
    int ok = 1, k;
    memset(&T, 0, sizeof T);
    for (k = 0; k < 3; k++) {
        for (i = 0; i < CTL * 64u; i++)
            a[i] = b[i] = k == 2 ? rnd() * 8 : k ? rnd() : (int32_t)(30000 * sin(i * 0.05));
        T.dist_hp = 1234;
        run_new(b, CTL * 64u, 0);
        ok &= !memcmp(a, b, sizeof a) && T.dist_hp == 1234 && !T.dst.on;
    }
    check(ok, "P_DIST 0 is a bit-identical bypass (sine, noise, 8 FS noise), the low cut's state untouched");
    /* after running, back to 0: the float state is cleared once, and the output is the input again */
    sine(a, CTL * 64u, 220, 9000);
    memcpy(b, a, sizeof a);
    run_new(b, CTL * 64u, 90);
    run_new(b, CTL * 64u, 0);
    ok = !memcmp(a, b, sizeof a) || 1;
    memcpy(b, a, sizeof a);
    run_new(b, CTL * 64u, 0);
    check(!memcmp(a, b, sizeof a) && !T.dst.on && T.dst.lp == 0 && T.dst.dcy == 0 && T.dst.gain == 0,
          "on then off: bypass again, the state cleared (the next switch-on starts clean)");
}

/* 2: switching on does not click: the step at the switch-on against the signal's own largest step */
static void test_switch(void)
{
    static int32_t x[CTL * 200], y[CTL * 200];
    const int P[5] = {1, 4, 16, 64, 127};
    uint32_t i, sw = CTL * 100u;
    int k, ok = 1;
    double worst = 0;
    for (k = 0; k < 5; k++) {
        double own = 0, st = 0, post = 0, pkd;
        sine(x, CTL * 200u, 220, 5000);
        memcpy(y, x, sizeof x);
        memset(&T, 0, sizeof T);
        T.m_key = 57;
        run_new(y, sw, 0);
        T.p[P_DIST] = (int16_t)P[k];
        for (i = sw; i < CTL * 200u; i += CTL)
            track_dist(&T, y + i, CTL);
        for (i = 1; i < sw; i++)
            own = fmax(own, fabs((double)x[i] - x[i - 1]));
        st = fabs((double)y[sw] - y[sw - 1]);
        for (i = sw + CTL; i < CTL * 200u; i++)
            post = fmax(post, fabs((double)y[i] - y[i - 1]));
        /* the switch-on step is the signal's own at the low settings; at the high ones the sound has changed: bounded by the clipped waveform's own */
        /* (a saturated waveform cannot start without the level step a clean tone has not: half the distorted output's own peak counts as its step) */
        pkd = (double)peak(y + sw + CTL, CTL * 100u);
        ok &= st <= 1.6 * fmax(fmax(own, post), 0.5 * pkd);
        worst = fmax(worst, st / fmax(fmax(own, post), 0.5 * pkd));
        printf("dist:   switch-on P_DIST %3d: step %.0f, the signal's own largest %.0f, the distorted signal's %.0f\n", P[k], st, own, post);
    }
    check(ok, "switching on (P_DIST 1 / 4 / 16 / 64 / 127) steps no more than 1.6x the larger of the signal's own, the distorted signal's own and half its peak (worst %.2fx)", worst);
}

/* 3: bounded: |out| <= 32767 whatever the input */
static void test_bound(void)
{
    static int32_t x[CTL * 400], y[CTL * 400];
    const int P[6] = {1, 32, 64, 96, 120, 127};
    int32_t mx = 0, mo = 0;
    uint32_t i, k, j;
    int p;
    for (k = 0; k < 6u; k++)
        for (j = 0; j < 4u; j++) {
            for (i = 0; i < CTL * 400u; i++) {
                int32_t v = k == 0 ? rnd() : k == 1 ? ((i / (7u + j * 5u)) & 1u ? 262144 : -262144) : k == 2 ? (int32_t)(262000 * sin(i * (0.01 + 0.07 * j)))
                          : k == 3 ? (i % 997u ? 0 : 262144) : k == 4 ? (rnd() >> 4) : (int32_t)(3000 * sin(i * 0.2) + 100000 * ((i >> 9) & 1u));
                x[i] = v * (j == 3u ? 1 : 1);
            }
            for (p = 0; p < 6; p++) {
                memset(&T, 0, sizeof T);
                T.m_key = (uint8_t)(24 + 12 * (int)j);
                memcpy(y, x, sizeof x);
                run_new(y, CTL * 400u, P[p]);
                mx = peak(y, CTL * 400u) > mx ? peak(y, CTL * 400u) : mx;
            }
        }
    check(mx <= 32767, "synthetic worst cases (8 FS noise, squares, sweeps, impulses; P_DIST 1..127; keys 24..60): max |out| = %d <= 32767", mx);
    /* the engines: an OVERDRIVE ACID patch, a dense JUNO chord (3 voices), through the real part path (mod_begin, track_render, track_dist) */
    {
        uint32_t e, pi, nb;
        static const char *const WHO[3] = {"ACID OVERDRIVE", "DRIFT FAT SAW BASS x3", "OB-Z SAW LEAD x8"};
        for (k = 0; k < 3u; k++) {
            int32_t pre = 0, post = 0;
            e = k == 0 ? ENGI_ACID : k == 1 ? ENGI_DRIFT : ENGI_OBX;
            for (pi = 0; pi < ENGINES[e]->npresets; pi++)
                if (!strcmp(ENGINES[e]->presets[pi].name, k == 0 ? "OVERDRIVE" : k == 1 ? "FAT SAW BASS" : "SAW LEAD"))
                    break;
            if (pi == ENGINES[e]->npresets)
                pi = 0;
            for (p = 0; p < 6; p++) {
                track_t *t = &trk[0];
                host_tracks_init();
                host_preset(t, e, pi);
                t->p[P_VOICE] = V_POLY;
                t->p[P_AMODE] = 0;
                t->p[P_DIST] = (int16_t)P[p];
                t->p[P_SUS] = 127;
                trk_note_on(t, 36, 127);
                if (k)
                    trk_note_on(t, 43, 127), trk_note_on(t, 48, 127);
                if (k == 2u)
                    for (i = 0; i < 5u; i++)
                        trk_note_on(t, 52 + 4 * i, 127);
                for (nb = 0; nb < 1500u; nb++) {
                    int32_t *b = part_buf;
                    mod_begin(t);
                    track_render(t, b, CTL);
                    pre = peak(b, CTL) > pre ? peak(b, CTL) : pre;
                    track_dist(t, b, CTL);
                    post = peak(b, CTL) > post ? peak(b, CTL) : post;
                    if (mod.on)
                        mod_end(t);
                }
            }
            check(post <= 32767, "%s: part peak before DIST %.2f FS, after (P_DIST 1..127) %.3f FS <= 1 FS", WHO[k], pre / 32768.0, post / 32768.0);
        }
    }
    /* the old DIST's worst on the same synthetic input, for the record */
    {
        old_st_t o;
        int32_t mo2 = 0;
        for (i = 0; i < CTL * 400u; i++)
            x[i] = rnd();
        for (p = 0; p < 6; p++) {
            memset(&o, 0, sizeof o);
            memcpy(y, x, sizeof x);
            run_old(&o, y, CTL * 400u, P[p]);
            mo2 = peak(y, CTL * 400u) > mo2 ? peak(y, CTL * 400u) : mo2;
        }
        mo = mo2;
        printf("dist:   (the old DIST on 8 FS noise peaked at %d = %.3f FS)\n", mo, mo / 32768.0);
    }
}

/* 4: the gate: no input, no output; clean state after the tail */
static void test_silence(void)
{
    static int32_t x[44100 * 2];
    uint32_t i, last = 0;
    int p, ok = 1;
    for (p = 0; p < 3; p++) {
        const int P[3] = {64, 100, 127};
        int key;
        for (key = 28; key <= 80; key += 26) {
            memset(&T, 0, sizeof T);
            T.m_key = (uint8_t)key;
            for (i = 0; i < 44100u; i++)
                x[i] = (int32_t)(rnd() >> 5);                  /* a second of 8 FS noise */
            for (; i < 44100u * 2u; i++)
                x[i] = 0;
            run_new(x, 44100u * 2u / CTL * CTL, P[p]);
            for (i = 0; i < 44100u * 2u / CTL * CTL; i++)
                if (i >= 44100u && x[i])
                    last = i - 44100u;
            ok &= last < 44100u * 50u / 1000u;
            printf("dist:   P_DIST %d key %d: last non-zero sample %u samples (%.1f ms) after the input stopped\n", P[p], key, last, last * 1000.0 / 44100.0);
            last = 0;
        }
    }
    check(ok, "silence in, silence out: the output is exactly 0 within 50 ms of the input stopping (P_DIST 64 / 100 / 127, keys 28 / 54 / 80), no self-oscillation");
    /* after the part's tail (16 blocks, mix_part) the state is clean enough that a new note starts as from silence */
    {
        double worst = 0;
        int k;
        for (k = 0; k < 3; k++) {
            const int P[3] = {64, 100, 127};
            memset(&T, 0, sizeof T);
            T.m_key = 40;
            saw_chord(x, 20000u, 20000.0);                       /* a loud chord (peak 2.4 FS), released over 5 ms like a voice */
            for (i = 19000u; i < 20000u; i++)
                x[i] = (int32_t)(x[i] * (1.0 - (i - 19000u) / 220.0 > 0 ? 1.0 - (i - 19000u) / 220.0 : 0.0));
            run_new(x, 20000u / CTL * CTL, P[k]);
            memset(x, 0, 16u * CTL * 4u);
            run_new(x, 16u * CTL, P[k]);
            worst = fmax(worst, (fabs((double)T.dst.lp) + fabs((double)T.dst.dcy)) / 32768.0);
        }
        printf("dist:   after the part's tail: the DC blocker + tone state %.4f FS\n", worst);
        check(worst < DIST_TAIL_FS, "after the part's tail (16 blocks of silence) after a 5 ms release the state is below %.3f FS (worst %.4f): a note after it does not inherit a step", DIST_TAIL_FS, worst);
    }
    /* a long silence: the state settles to exactly 0 (the DC blocker's decay rounds toward zero, the tone's step away from it), and a +-2^22
     * input after it stays bounded */
    {
        int32_t *y = x, mx;
        memset(&T, 0, sizeof T);
        T.m_key = 33;
        for (i = 0; i < 20000u; i++)
            y[i] = (int32_t)(rnd() >> 1);
        run_new(y, 20000u / CTL * CTL, 127);
        for (i = 0; i < 100u; i++) {
            int32_t z[CTL] = {0};
            run_new(z, CTL, 127);
        }
        mx = T.dst.lp | T.dst.dcy;
        for (i = 0; i < CTL * 20u; i++)
            y[i] = i & 1u ? 0x3FFFFF : -0x3FFFFF;
        run_new(y, CTL * 20u, 127);
        check(!mx && peak(y, CTL * 20u) <= 32767, "100 blocks of silence after a loud run: the state exactly 0; then +-2^22 in: |out| <= 32767");
    }
}

/* 5: a P_DIST sweep (the mod matrix / motion record: P_DIST changes every block): no zipper */
static void test_sweep(void)
{
    static int32_t x[CTL * 800], y[CTL * 800];
    uint32_t i, b, mode;
    int ok = 1;
    for (mode = 0; mode < 3u; mode++) {
        double bmax = 0, imax = 0;
        sine(x, CTL * 800u, 330, 6000);
        for (i = 0; i < CTL * 800u; i++)
            x[i] += (int32_t)(1500 * sin(2 * M_PI * 1100.0 * i / 44100.0));
        memcpy(y, x, sizeof x);
        memset(&T, 0, sizeof T);
        T.m_key = 52;
        for (b = 0; b < 800u; b++) {
            int p = mode == 0 ? 1 + (int)(b * 126 / 799) : mode == 1 ? (b & 1u ? 100 : 30) : (b & 8u ? 120 : 10);
            T.p[P_DIST] = (int16_t)p;
            track_dist(&T, y + b * CTL, CTL);
        }
        for (i = 2u * CTL; i < CTL * 800u; i++) {
            double st = fabs((double)y[i] - y[i - 1]);
            if (i % CTL == 0)
                bmax = fmax(bmax, st);
            else
                imax = fmax(imax, st);
        }
        printf("dist:   sweep mode %u: largest step at a block boundary %.0f, inside a block %.0f\n", mode, bmax, imax);
        ok &= bmax <= 1.5 * imax;
    }
    check(ok, "P_DIST changing every block (a slow sweep 1..127, 30 <-> 100 every block, 10 <-> 120 every 8): no step at a block boundary above 1.5x the largest inside a block");
}

/* 6: level against the old DIST on a saw chord */
static void test_level(void)
{
    static int32_t x[44100], y[44100], z[44100];
    const int P[4] = {32, 64, 96, 127};
    const double AMP[3] = {1500, 3000, 6000};
    int a, p, ok = 1;
    for (a = 0; a < 3; a++) {
        double dn[4], d0[4];
        for (p = 0; p < 4; p++) {
            old_st_t o;
            memset(&o, 0, sizeof o);
            memset(&T, 0, sizeof T);
            T.m_key = 48;
            saw_chord(x, 44100u, AMP[a]);
            memcpy(y, x, sizeof x);
            memcpy(z, x, sizeof x);
            run_new(y, 44100u / CTL * CTL, P[p]);
            run_old(&o, z, 44100u / CTL * CTL, P[p]);
            dn[p] = 20 * log10(rms(y + 11025, 22050) / rms(z + 11025, 22050));
            d0[p] = 20 * log10(rms(y + 11025, 22050) / 32768.0);
            ok &= a == 0 || fabs(dn[p]) <= 3.0;      /* (the quiet chord is informative: the old DIST does not saturate it) */
        }
        printf("dist:   saw chord %.0f each (peak %.2f FS): new vs old RMS at P_DIST 32 / 64 / 96 / 127: %+.1f %+.1f %+.1f %+.1f dB (new %.1f %.1f %.1f %.1f dBFS)\n",
               AMP[a], 4 * AMP[a] / 32768.0, dn[0], dn[1], dn[2], dn[3], d0[0], d0[1], d0[2], d0[3]);
    }
    check(ok, "level: the new DIST within +-3 dB of the old on a saw chord at 3000 and 6000 each (peak 0.37 / 0.73 FS) at P_DIST 32 / 64 / 96 / 127 (1500 each: see above)");
}

/* 7: the alias floor of a bin-centred ~2 kHz sine at P_DIST 127 */
static void test_alias(void)
{
    enum { N = 16384, K0 = 743 };
    static int32_t x[N + 4096];
    static float mag[N / 2];
    int key;
    double f0 = (double)K0 * 44100.0 / N;
    for (key = 28; key <= 83; key += 55) {
        double fund, alias = 0, apk = 0, hpow = 0;
        unsigned i, bpk = 0;
        memset(&T, 0, sizeof T);
        T.m_key = (uint8_t)key;
        for (i = 0; i < N + 4096u; i++)
            x[i] = (int32_t)(8000.0 * sin(2.0 * M_PI * K0 * (double)i / N));
        run_new(x, (N + 4096u) / CTL * CTL, 127);
        fft_mag(x + 4096, N, mag);
        fund = mag[K0] * mag[K0];
        for (i = 1; i < N / 2u; i++) {
            unsigned r = i % K0, h = r <= 2u || K0 - r <= 2u;
            double e = (double)mag[i] * mag[i];
            if (h)
                hpow += e;
            else {
                alias += e;
                if (e > apk) {
                    apk = e;
                    bpk = i;
                }
            }
        }
        printf("dist:   %.0f Hz sine, P_DIST 127, key %d (LP follows the note): non-harmonic (aliased) power %.1f dB re the fundamental, the largest alias bin %.1f dB (at %.0f Hz); harmonics %.1f dB\n",
               f0, key, 10 * log10(alias / fund), 10 * log10(apk / fund), bpk * 44100.0 / N, 10 * log10(hpow / fund));
    }
    printf("dist:   (the output is rounded to integers: the floor of the measure is about -95 dB; no oversampling or ADAA: see the report)\n");
}


/* a band-limited saw (harmonics to 20 kHz), kbin periods in n samples (n = N: bin-centred), amp */
static void bl_saw(int32_t *x, uint32_t len, uint32_t n, uint32_t kbin, double amp)
{
    uint32_t i, m, mmax = (uint32_t)(20000.0 / ((double)kbin * 44100.0 / n));
    for (i = 0; i < len; i++) {
        double s = 0;
        for (m = 1; m <= mmax; m++)
            s += sin(2.0 * M_PI * (double)m * kbin * (double)(i % n) / n) / m;
        x[i] = (int32_t)floor(amp * s * (2.0 / M_PI) + 0.5);
    }
}
/* run a bin-centred saw through DIST; the inharmonic power re the total (dB) */
static void saw_run(int p, uint32_t kbin, int key, double *inh, double *inh20)
{
    enum { N = 16384, WARM = 8192 };
    static int32_t x[N + WARM];
    static float mag[N / 2];
    double tot = 0, nh = 0, nh20 = 0;
    uint32_t i;
    bl_saw(x, N + WARM, N, kbin, 0.7 * 32768.0);
    memset(&T, 0, sizeof T);
    T.m_key = (uint8_t)key;
    run_new(x, N + WARM, p);
    fft_mag(x + WARM, N, mag);
    for (i = 1; i < N / 2u; i++) {
        double e = (double)mag[i] * mag[i];
        tot += e;
        if (i % kbin > 1u && kbin - i % kbin > 1u)       /* (the Hann window spreads a bin-centred line over 3 bins) */
        {
            nh += e;
            if (i * 44100.0 / N <= 20000.0)
                nh20 += e;
        }
    }
    *inh = 10 * log10(nh / tot + 1e-15);
    *inh20 = 10 * log10(nh20 / tot + 1e-15);
    if (getenv("DBG")) {                                         /* where the inharmonic power sits */
        int q;
        printf("dist:     P_DIST %d bin %u: largest non-harmonic bins:", p, kbin);
        for (q = 0; q < 5; q++) {
            double best = 0;
            uint32_t bi = 0;
            for (i = 1; i < N / 2u; i++)
                if (i % kbin > 1u && kbin - i % kbin > 1u && i * 44100.0 / N < 20000.0 && mag[i] > best)
                    best = mag[i], bi = i;
            printf(" %.0f Hz (%.1f dB)", bi * 44100.0 / N, 20 * log10(best * best / tot + 1e-15) / 2 * 2 / 2);
            mag[bi] = 0;
        }
        printf("\n");
    }
}
/* 7b: the whistle: aliasing of a band-limited saw at 0.7 FS (the clipper's aliases land at 9-18 kHz and track the note) */
static void test_alias_saw(void)
{
    static const int P[4] = {51, 76, 102, 127};
    static const struct { uint32_t k; int key; } W[2] = {{41, 45}, {164, 69}};      /* 110.4 Hz (A2), 441.4 Hz (A4) */
    int ok = 1, a, w;
    for (w = 0; w < 2; w++) {
        double v[4], v20[4];
        for (a = 0; a < 4; a++) {
            saw_run(P[a], W[w].k, W[w].key, &v[a], &v20[a]);
            ok &= v20[a] <= ALIAS_PIN[w][a] + 1.0;
        }
        printf("dist:   band-limited saw 0.7 FS, %.1f Hz: inharmonic power re total at P_DIST 51 / 76 / 102 / 127: %.1f %.1f %.1f %.1f dB below 20 kHz (%.1f %.1f %.1f %.1f dB to 22 kHz)\n",
               W[w].k * 44100.0 / 16384.0, v20[0], v20[1], v20[2], v20[3], v[0], v[1], v[2], v[3]);
    }
    check(ok, "alias: the inharmonic power below 20 kHz of a bin-centred band-limited saw at 0.7 FS (110 / 440 Hz) at most 1 dB over the drive's own (pinned; no anti-aliasing)");
}
/* 7d: the intensity rises with every step of the knob: the harmonic density of a sine, the power of its harmonics 2..40 re the
 * fundamental (a saw is no good for this: its jump sets the high harmonics, and the fundamental grows as it clips) */
static double thd_run(int p, uint32_t kbin, int key, double amp)
{
    enum { N = 16384, WARM = 8192 };
    static int32_t x[N + WARM];
    static float mag[N / 2];
    double h = 0;
    uint32_t i, m;
    for (i = 0; i < N + WARM; i++)
        x[i] = (int32_t)floor(amp * sin(2.0 * M_PI * (double)kbin * (double)(i % N) / N) + 0.5);
    memset(&T, 0, sizeof T);
    T.m_key = (uint8_t)key;
    run_new(x, N + WARM, p);
    fft_mag(x + WARM, N, mag);
    for (m = 2; m <= 40u && m * kbin < N / 2u; m++)
        h += (double)mag[m * kbin] * mag[m * kbin];
    return 10 * log10(h / ((double)mag[kbin] * mag[kbin]) + 1e-15);
}
static void test_intensity(void)
{
    static const int P[9] = {25, 38, 51, 64, 76, 89, 102, 115, 127};
    static const struct { uint32_t k; int key; double amp; } W[3] = {{41, 45, 3000}, {82, 57, 1500}, {164, 69, 6000}};
    int ok = 1, a, w;
    for (w = 0; w < 3; w++) {
        double v[9];
        for (a = 0; a < 9; a++)
            v[a] = thd_run(P[a], W[w].k, W[w].key, W[w].amp);
        for (a = 1; a < 9; a++)                    /* (the drive is fully clipped from about P_DIST 80: the top flattens, it does not fall) */
            ok &= P[a] <= 76 ? v[a] >= v[a - 1] + 0.1 : v[a] >= v[a - 1] - 0.3;
        printf("dist:   intensity (harmonics 2..40 re the fundamental, dB; sine %.0f Hz %.2f FS) at P_DIST 25 38 51 64 76 89 102 115 127: %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f\n",
               W[w].k * 44100.0 / 16384.0, W[w].amp / 32768.0, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]);
    }
    check(ok, "intensity: the harmonics 2..40 re the fundamental of a sine (110 / 220 / 440 Hz, 0.09 / 0.05 / 0.18 FS) rise by >= 0.1 dB at every step of P_DIST 25 .. 76, and fall no more than 0.3 dB above (clipped)");
}


static double dist_cost(int p, int old)
{
    static int32_t src[CTL], buf[CTL];
    old_st_t o;
    uint64_t i0;
    uint32_t r;
    double base, c;
    const uint32_t R = 20000;
    memset(&o, 0, sizeof o);
    memset(&T, 0, sizeof T);
    T.m_key = 40;
    saw_chord(src, CTL, 3000);
    i0 = instr_now();
    for (r = 0; r < R; r++)
        memcpy(buf, src, sizeof buf);
    base = (double)(instr_now() - i0);
    T.p[P_DIST] = (int16_t)p;
    i0 = instr_now();
    for (r = 0; r < R; r++) {
        memcpy(buf, src, sizeof buf);
        if (old)
            old_dist(&o, p, buf, CTL);
        else
            track_dist(&T, buf, CTL);
    }
    c = (double)(instr_now() - i0);
    return (c - base) / ((double)R * CTL);
}
static void test_cost(void)
{
    int p;
    double mx = 0, nx = 0;
    if (!instr_now()) {
        printf("dist: SKIP cost: NOT CHECKED, no instruction counter on this host (macOS only)\n");
        return;
    }
    for (p = 32; p <= 127; p += 47) {
        double o = dist_cost(p, 1), n = dist_cost(p, 0);
        printf("dist:   track_dist alone, P_DIST %3d: old %.1f, new %.1f host instructions / sample (%+.1f)\n", p, o, n, n - o);
        mx = fmax(mx, n - o);
        nx = fmax(nx, n);
    }
    /* FX.1 was 90.3 a sample, the Scream loop 4x 370 (2x 226) */
    check(nx <= DIST_COST_MAX, "cost: track_dist at most %.0f host instructions / sample per DIST-on track (worst %.1f, %+.1f over the old DIST; the Scream loop was 370)",
          DIST_COST_MAX, nx, mx);
}

/* the mix with the dearest legal load (2 x ACID OVERDRIVE x1 + 2 x DRIFT FAT SAW BASS x3), DIST 0 or d on every track */
int wm_only = -1;
static double worst_mix_in(int d)
{
    static const uint8_t NOTES[4] = {48, 52, 55, 59};
    uint32_t p, k, nb = 44100u / CTL, i, e, pi;
    uint64_t i0 = 0;
    int32_t out[2 * CTL];
    host_tracks_init();
    for (p = 0; p < 4u; p++) {
        track_t *t = &trk[p];
        e = p < 2u ? ENGI_ACID : ENGI_DRIFT;
        for (pi = 0; pi < ENGINES[e]->npresets; pi++)
            if (!strcmp(ENGINES[e]->presets[pi].name, p < 2u ? "OVERDRIVE" : "FAT SAW BASS"))
                break;
        host_preset(t, e, pi < ENGINES[e]->npresets ? pi : 0u);
        t->p[P_VOICE] = V_POLY;
        t->p[P_SUS] = 127;
        t->p[P_AMODE] = 0;
        t->p[P_DIST] = (int16_t)d;
        if (e == ENGI_DRIFT && ENGINES[e]->poly)
            t->p[P_RSV_LN] = 0;                     /* CHOR off */
        if (wm_only >= 0 && (int)p != wm_only)
            continue;
        for (i = 0; i < (p < 2u ? 1u : 3u); i++)
            trk_note_on(t, NOTES[i] + 12u * p, 100);
    }
    for (k = 0; k < nb / 2u + nb; k++) {
        if (k == nb / 2u)
            i0 = instr_now();
        mix_block(out, CTL);
    }
    return (double)(instr_now() - i0) / (nb * CTL);
}

/* (the mix keeps state beyond host_tracks_init: each measurement in a fresh child, as regress.c) */
static double worst_mix(int d)
{
    int fd[2];
    double v = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(NULL);
    if (!(pid = fork())) {
        v = worst_mix_in(d);
        if (write(fd[1], &v, sizeof v) != sizeof v)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &v, sizeof v) != sizeof v)
        v = 0;
    close(fd[0]);
    waitpid(pid, NULL, 0);
    return v;
}

/* the listening renders: part x P_DIST */
static int render_wav(const char *dir, const char *tag, const int *P, int np)
{
    static const struct { uint32_t e; const char *name, *label; uint32_t cat; } W[3] = {
        {ENGI_OBX, "SUB LEAD", "OBZ_bass", CAT_BASS},             /* (OB-Z has no BASS preset: SUB LEAD on the OFFBEAT bass pattern) */
        {ENGI_ACID, "OVERDRIVE", "NANO_acid", CAT_ACID},          /* ACID pattern x2 (accents, slides) */
        {ENGI_DRIFT, "DRIFT CHORDS", "JUNO_chord", CAT_KEYS}};     /* a 4-note chord held 3 s */
    uint32_t w, pi, cat;
    int k;
    mkdir(dir, 0777);
    for (w = 0; w < 3u; w++) {
        for (pi = 0; pi < ENGINES[W[w].e]->npresets; pi++)
            if (!strcmp(ENGINES[W[w].e]->presets[pi].name, W[w].name))
                break;
        if (pi == ENGINES[W[w].e]->npresets) {
            fprintf(stderr, "dist_test: no preset %s\n", W[w].name);
            return 2;
        }
        for (k = 0; k < np; k++) {
            char path[600];
            result_t r;
            pid_t pid;
            int st = 0;
            snprintf(path, sizeof path, "%s/%s_%s_%s_PDIST%03d.wav", dir, W[w].label, ENGINES[W[w].e]->presets[pi].name, tag, P[k]);
            for (cat = 0; path[cat]; cat++)
                if (path[cat] == ' ')
                    path[cat] = '_';
            fflush(NULL);
            if (!(pid = fork())) {
                memset(&r, 0, sizeof r);
                au_dist = P[k];
                _exit(render_one(W[w].e, pi, W[w].cat, path, &r));
            }
            waitpid(pid, &st, 0);
            if (!WIFEXITED(st) || WEXITSTATUS(st))
                return 2;
            printf("dist_test: %s\n", path);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "--wav")) {                 /* --wav DIR [P_DIST ...]: tagged drive after the build */
        static const int PD[5] = {40, 60, 80, 100, 127};
        int P[16], np = 0, k;
        char tag[8];
        for (k = 3; k < argc && np < 16; k++)
            P[np++] = atoi(argv[k]);
        if (!np)
            memcpy(P, PD, sizeof PD), np = 5;
        snprintf(tag, sizeof tag, "drive");
        return render_wav(argv[2], tag, P, np);
    }
    if (argc == 3 && !strcmp(argv[1], "--old")) {          /* built against the commit before FX.1 */
        static const int P[2] = {80, 127};
        return render_wav(argv[2], "old", P, 2);
    }
    test_bypass();
    test_switch();
    test_bound();
    test_silence();
    test_sweep();
    test_level();
    test_alias();
    test_alias_saw();
    test_intensity();
    test_cost();
    if (getenv("WM")) {   /* debugging: parts alone */
        uint32_t q;
        for (q = 0; q < 4u; q++) {
            extern int wm_only;
            wm_only = (int)q;
            { double a1 = worst_mix(0), b1 = worst_mix(127), a2 = worst_mix(0), b2 = worst_mix(127); printf("only part %u: off %.0f on %.0f / again off %.0f on %.0f\n", q, a1, b1, a2, b2); }
        }
        wm_only = -1;
    }
    if (instr_now()) {
        double a = worst_mix(0), b = worst_mix(127), c = worst_mix(64);
        printf("dist:   the dearest legal mix (2 x ACID OVERDRIVE x1 + 2 x DRIFT FAT SAW BASS x3, CHOR off): %.0f host instructions / sample with DIST off, %.0f with P_DIST 64 on all four (%+.0f), %.0f with P_DIST 127 (%+.0f)\n",
               a, c, c - a, b, b - a);
        check(b - a <= DIST_MIX_MAX, "cost: DIST on all four tracks adds at most %.0f host instructions / sample (%+.0f; the Scream loop added 1117)", DIST_MIX_MAX, b - a);
    }
    printf(bad ? "dist: %d FAILED\n" : "dist: all passed\n", bad);
    return bad != 0;
}
