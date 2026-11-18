/* SPDX-License-Identifier: GPL-3.0-only */
/* sub_test.c: NANO's SUB model (firmware/src/eng_sub.c, 2026-10-21; docs/plans/2026-10-21-dist-chorus-sub.md), through the real voice path
 * (host_preset, trk_note_on / off, mod_begin, track_render, mod_end).
 *   cc -O2 -w -ffp-contract=off -Ibuild/gen -Ifirmware/src -Itests -o build/host/sub_test tests/sub_test.c -lm
 * The checks: the two models by preset (1-2 the 303, one voice; 3-32 SUB, SUB_POLY; both their own envelopes), a stored sound
 * from before (the lane's 127) is the 303; the pitch of a saw and a pulse; the polyphony cap; every filter TYPE at full resonance and the
 * cutoff swept, with noise in: bounded, no voice stuck, exact silence after the release; a narrow pulse without DC; the alias floor of a
 * saw (pinned); the cost per voice (pinned). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <stdarg.h>
#include "dspfft.h"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    printf("sub: %s ", ok ? "ok  " : "FAIL");
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
#define ALIAS_PIN (-45.4)               /* the saw's inharmonic power below 20 kHz at 1.76 kHz (measured 2026-10-21, +1 dB allowed) */
#define COST_PIN 352.0                  /* host instructions a sample a voice, voice.c included (the dearest: LP24, both oscillators, sub, noise; the module's envelopes and LFO: measured 320 + 10 %; the 303 313, JUNO 297) */

/* a SUB track (preset pi) with the given EDIT values, SUS 127, POLY */
static track_t *sub_track(uint32_t pi)
{
    track_t *t = &trk[0];
    memset(trk, 0, sizeof trk);                      /* (host_tracks_init leaves the voices: an earlier check's notes would still sound) */
    memset(eng_mem, 0, sizeof eng_mem);
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    host_tracks_init();
    host_preset(t, ENGI_ACID, pi);
    t->p[P_SUS] = 127;
    t->p[P_VOICE] = V_POLY;
    t->p[P_AMODE] = 0;
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    return t;
}
static void block(track_t *t, int32_t *b)
{
    memset(b, 0, CTL * sizeof(int32_t));
    mod_begin(t);
    track_render(t, b, CTL);
    if (mod.on)
        mod_end(t);
}
static uint32_t active(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}

static void test_models(void)
{
    track_t *t;
    uint32_t k;
    int ok = 1;
    for (k = 0; k < 32u; k++) {
        t = sub_track(k);
        ok &= (k < 2u) == !sub_on(t) && trk_ownenv(t) && trk_nvoice(t) == (k < 2u ? 1u : SUB_POLY);
    }
    check(ok, "presets 1-2 are the 303 (one voice), 3-32 SUB (%d voices); both run their own envelopes (the 303's VCA, the module's ADSRs)", SUB_POLY);
    t = sub_track(5);
    t->p[P_RSV_LN + 1] = 127;
    ok = !sub_on(t) && trk_ownenv(t);
    t->p[P_RSV_LN + 1] = 0;
    ok &= !sub_on(t);
    check(ok, "a stored sound from before (the lane's 127, or 0) plays the 303");
    t = sub_track(5);
    ok = !strcmp(track_desc(t, P_E1)->label, "DTUN") && !strcmp(track_desc(t, P_E7)->label, "NOIS");
    t = sub_track(0);
    ok &= !strcmp(track_desc(t, P_E1)->label, "TUNE") && !strcmp(track_desc(t, P_E7)->label, "SQL");
    check(ok, "EDIT's labels follow the model (SUB: DTUN .. NOIS, the 303: TUNE .. SQL)");
}

/* the pitch from the upward zero crossings of a second of one voice */
static double pitch_of(int pulse, uint32_t note)
{
    static int32_t x[44100 + CTL];
    track_t *t = sub_track(5);
    uint32_t b, i, n = 0;
    double first = -1, last = -1;
    t->p[P_E0] = (int16_t)pulse, t->p[P_E2] = t->p[P_E3] = t->p[P_E7] = 0, t->p[P_E4] = 127, t->p[P_E5] = 0, t->p[P_FTYPE] = 3, t->p[P_ED_FLT] = 0;
    trk_note_on(t, note, 100);
    for (b = 0; b < 44100u / CTL; b++)
        block(t, x + b * CTL);
    for (i = 4410; i < 44100u / CTL * CTL; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            double tc = (double)(i - 1) + (double)-x[i - 1] / (double)(x[i] - x[i - 1]);
            if (first < 0)
                first = tc;
            last = tc;
            n++;
        }
    return n > 2 ? 44100.0 * (double)(n - 1) / (last - first) : 0.0;
}
static void test_pitch(void)
{
    double s = pitch_of(0, 69), q = pitch_of(1, 57), cs = 1200.0 * log2(s / 440.0), cq = 1200.0 * log2(q / 220.0);
    check(fabs(cs) < 3.0 && fabs(cq) < 3.0, "pitch: saw A4 %.2f Hz (%+.2f cents), pulse A3 %.2f Hz (%+.2f cents), within 3 cents", s, cs, q, cq);
}

static void test_poly(void)
{
    static int32_t b[CTL];
    track_t *t = sub_track(20);                                   /* POLY KEYS */
    uint32_t i, mx = 0;
    for (i = 0; i < 7u; i++)
        trk_note_on(t, 48 + 3 * i, 100);
    for (i = 0; i < 50u; i++) {
        block(t, b);
        mx = active(t) > mx ? active(t) : mx;
    }
    check(mx == SUB_POLY, "seven keys on a SUB track: %u voices at most (SUB_POLY %d)", mx, SUB_POLY);
}

/* every TYPE at RES 127, the cutoff swept 0 .. 127 every 8 blocks, noise + both oscillators + the sub in, a chord; then the release */
static void test_stable(void)
{
    static int32_t b[CTL];
    int32_t pk = 0;
    int ft, ok = 1, silent = 1;
    for (ft = 0; ft < 4; ft++) {
        track_t *t = sub_track(5);
        uint32_t k, i;
        t->p[P_FTYPE] = (int16_t)ft, t->p[P_E5] = 127, t->p[P_E2] = 127, t->p[P_E3] = 127, t->p[P_E7] = 127, t->p[P_REL] = 10;
        for (i = 0; i < 4u; i++)
            trk_note_on(t, 36 + 12 * i, 127);
        for (k = 0; k < 2048u; k++) {
            t->p[P_E4] = (int16_t)((k / 8u) % 128u);
            block(t, b);
            for (i = 0; i < CTL; i++)
                pk = abs(b[i]) > pk ? abs(b[i]) : pk;
        }
        for (i = 0; i < 4u; i++)
            trk_note_off(t, 36 + 12 * i);
        for (k = 0; k < 400u; k++)
            block(t, b);
        ok &= active(t) == 0u;
        for (i = 0; i < CTL; i++)
            silent &= b[i] == 0;
    }
    check(ok && silent && pk <= 4 * 32767, "every TYPE at RES 127, the cutoff swept, everything in, 4 voices: peak %.2f FS (<= 4: one voice is bounded by 1), all voices free and exact silence after the release", pk / 32768.0);
}

static void test_dc(void)
{
    static int32_t b[CTL];
    track_t *t = sub_track(5);
    uint32_t k, i;
    double s = 0, a = 0;
    t->p[P_E0] = 1, t->p[P_E6] = 127, t->p[P_E2] = 0, t->p[P_E3] = 0, t->p[P_E4] = 127, t->p[P_E5] = 0, t->p[P_ED_FLT] = 0;
    trk_note_on(t, 45, 100);
    for (k = 0; k < 2000u; k++) {
        block(t, b);
        if (k > 200u)
            for (i = 0; i < CTL; i++)
                s += b[i], a = fmax(a, fabs((double)b[i]));
    }
    s /= 1799.0 * CTL;
    check(fabs(s) < 0.01 * a, "a 5 %% pulse: DC %.3f %% of the peak (< 1 %%: the pulse less its mean)", 100.0 * s / a);
}

static void test_alias(void)
{
    enum { N = 16384, K0 = 653 };                                 /* ~1758 Hz, bin-centred by the pitch below */
    static int32_t x[N + 8192];
    static float mag[N / 2];
    track_t *t = sub_track(5);
    uint32_t b, i;
    double tot = 0, nh = 0, f0;
    t->p[P_E0] = 0, t->p[P_E2] = t->p[P_E3] = t->p[P_E7] = 0, t->p[P_E4] = 127, t->p[P_E5] = 0, t->p[P_FTYPE] = 3, t->p[P_ED_FLT] = 0;
    trk_note_on(t, 93, 100);                                      /* A6 = 1760 Hz */
    for (b = 0; b < (N + 8192u) / CTL; b++)
        block(t, x + b * CTL);
    for (i = 0; i < N; i++)                          /* (Hann: the tone is not bin-centred) */
        x[8192 + i] = (int32_t)(x[8192 + i] * (0.5 - 0.5 * cos(2.0 * M_PI * i / N)));
    fft_mag(x + 8192, N, mag);
    f0 = 1760.0 * N / 44100.0;
    for (i = 1; i < N / 2u; i++) {
        double e = (double)mag[i] * mag[i], h = fmod((double)i, f0);
        if (i * 44100.0 / N > 20000.0)
            break;
        tot += e;
        if (h > 3.0 && f0 - h > 3.0)
            nh += e;
    }
    printf("sub:   the saw at 1760 Hz: inharmonic power below 20 kHz %.1f dB re the total (PolyBLEP)\n", 10.0 * log10(nh / tot + 1e-15));
    check(10.0 * log10(nh / tot + 1e-15) <= ALIAS_PIN + 1.0, "alias: the saw's inharmonic power at most %.0f dB (pinned)", ALIAS_PIN + 1.0);
}

static void test_cost(void)
{
    static int32_t b[CTL];
    track_t *t;
    uint64_t i0;
    uint32_t k;
    double c;
    if (!instr_now()) {
        printf("sub: SKIP cost: no instruction counter on this host (macOS only)\n");
        return;
    }
    t = sub_track(5);
    t->p[P_FTYPE] = 0, t->p[P_E2] = 127, t->p[P_E3] = 127, t->p[P_E7] = 127;
    trk_note_on(t, 48, 100);
    for (k = 0; k < 100u; k++)
        block(t, b);
    i0 = instr_now();
    for (k = 0; k < 20000u; k++)
        block(t, b);
    c = (double)(instr_now() - i0) / (20000.0 * CTL);
    check(c <= COST_PIN, "cost: one SUB voice (LP24, osc 2, sub, noise) %.0f host instructions a sample, voice.c included (pin %.0f)", c, COST_PIN);
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    test_models();
    test_pitch();
    test_poly();
    test_stable();
    test_dc();
    test_alias();
    test_cost();
    printf("sub_test: %s\n", bad ? "FAIL" : "ok");
    return bad ? 1 : 0;
}
