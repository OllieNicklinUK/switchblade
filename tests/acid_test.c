/* SPDX-License-Identifier: GPL-3.0-only */
/* acid_test.c: ACID, the 303 voice (eng_acid.c, bass303.c), measured through the real voice path (trk_note_on, track_render).
 *   cc -O2 -w -ffp-contract=off -Ibuild/gen -Ifirmware/src -Itests -o build/host/acid_test tests/acid_test.c -lm
 *   build/host/acid_test [tests/acid_ref]
 * The reference vectors are the donor's (tests/acid_ref.c, regenerate with tests/acid_ref.sh); tests/acid_meas.h makes the
 * figures on both sides. The B3_IN seam (bass303.c) replaces the oscillator by silence plus one impulse, so (a) and (b) measure the
 * FILTER alone: the donor has no other way to give it one.
 *   (a) the filter's impulse response, a 5 x 5 CUT / RES grid, every band within 1 dB of the donor's;
 *   (b) RES 127: the resonance peak within +-3 % of the donor's, prominent, and the ring's decay as the donor's. The donor's
 *       TB_303-mode filter (Open303) rings for tens of ms at RES 127 but does not sustain with no input -- see the report
 *   (c) an accented step peaks >= 3 dB above the same step plain, with a higher centroid, and both are the donor's;
 *   (d) a 48 -> 60 slide through the real legato (and MONO) path: no envelope retrigger, the pitch rises monotonically across
 *       50-70 ms, no amplitude dip over 1 dB, the 5 ms rms the donor's;
 *   (e) DECY 0 against 127: the filter envelope's time constant differs by >= 5 x (the audio's, and the engine's own);
 *   (f) the cost of a voice, against OB-Z and against the 4600 instruction / sample bound;
 *   (g) amp() stays in [0, 32767] whatever the settings; done() ends a released voice and a held one; the state fits. */
#include "host_pins.h"   /* (V2) per-host cost pins */
#include <stdint.h>
#include <stdarg.h>
static int imp_mode, imp_pending;                  /* the B3_IN seam: the oscillator off, one impulse on request; mode 2 a square (test_wet_bus) */
static uint32_t imp_t, imp_n, imp_half, imp_os = 1, imp_blk = 1;            /* mode 2: the square's sample count (set per block), the call within the block, its half period */
static float imp_in(float s)
{
    float r;
    if (!imp_mode)
        return s;
    if (imp_mode == 2)                             /* a 100 x square, the same for every track (calls come CTL x oversampling per track and block) */
        return (((imp_t + imp_n++ % imp_blk / imp_os) / imp_half) & 1u) ? 100.0f : -100.0f;
    r = imp_pending ? 100.0f : 0.0f;               /* acid_meas.h AM_IMP */
    imp_pending = 0;
    return r;
}
#define B3_IN(s) imp_in(s)
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "acid_meas.h"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    printf("acid: %s ", ok ? "ok  " : "FAIL");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    bad += !ok;
}

/* ------------------------------------------------------------ harness --- */
static track_t *T;
static acid_t *ac_of(void) { return (acid_t *)ENG_MEM(0); }

static void acid_fresh(void)                      /* part 0: ACID, LEGATO, no sends, the engine's state zero */
{
    memset(trk, 0, sizeof trk);
    memset(&mod, 0, sizeof mod);
    vage = 0;
    rng_state = 0x1234567u;
    mod_seed = 0x2545F491u;
    imp_mode = imp_pending = 0;
    host_tracks_init();
    memset(eng_mem[0].b, 0, sizeof eng_mem[0].b);
    eng_mem_owner[0] = 0;
    host_preset(&trk[0], ENGI_ACID, 0);
    T = &trk[0];
    T->p[P_VOICE] = V_LEGATO;
    T->p[P_DIST] = T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 0;
    T->p[P_LD_AMP] = T->p[P_LD_FLT] = T->p[P_LD_PIT] = T->p[P_ED_PIT] = 0;
    T->p[P_GLIDE] = 0;
    T->slide_glide = 0;
}

/* WAVE 0/1, TUNE, DECY, ACC, CUT, RES, DRV, SQL and ENV's pot (ENVMOD pot 0..127 -> the ENV knob that maps to it) */
static void pots(int wave, int tune, int decy, int acc, int cut, int res, int drv, int sql, int envmod)
{
    T->p[P_E0] = (int16_t)wave;
    T->p[P_E1] = (int16_t)tune;
    T->p[P_E2] = (int16_t)decy;
    T->p[P_E3] = (int16_t)acc;
    T->p[P_E4] = (int16_t)cut;
    T->p[P_E5] = (int16_t)res;
    T->p[P_E6] = (int16_t)drv;
    T->p[P_E7] = (int16_t)sql;
    T->p[P_ED_FLT] = (int16_t)(envmod <= 0 ? 0 : (envmod * 63 + 63) / 127);   /* the ED value whose pot is `envmod` (ED 40 -> 81) */
}

static int32_t o_blk[CTL];
static void run(int32_t *out, unsigned nblk)       /* nblk control blocks of the part into out (CTL samples each) */
{
    unsigned b, i;
    for (b = 0; b < nblk; b++) {
        track_render(T, o_blk, CTL);
        if (out)
            for (i = 0; i < CTL; i++)
                out[b * CTL + i] = o_blk[i];
    }
}

/* ------------------------------------------------------- reference files --- */
static const char *refdir = "tests/acid_ref";
static double ref_grid[5][5][AM_NB];
static int ref_grid_ok;
static double ref_so[4][3];
static int ref_so_ok;
static double ref_step[4][2];
static int ref_step_ok;
static double ref_rms[AM_SLIDE_END * AM_BLK / AM_RMS_WIN];
static unsigned ref_nrms;

static FILE *ref_open(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", refdir, name);
    return fopen(path, "r");
}

static void load_refs(void)
{
    char line[1024];
    FILE *f;
    unsigned c, r, k, n;
    if ((f = ref_open("grid.txt"))) {
        n = 0;
        while (fgets(line, sizeof line, f)) {
            char *p = line;
            int used;
            unsigned cu, re;
            if (strncmp(line, "grid ", 5))
                continue;
            if (sscanf(p + 5, "%u %u%n", &cu, &re, &used) != 2)
                continue;
            p += 5 + used;
            for (c = 0; c < 5u && AM_CUT[c] != cu; c++)
                ;
            for (r = 0; r < 5u && AM_RES[r] != re; r++)
                ;
            if (c == 5u || r == 5u)
                continue;
            for (k = 0; k < AM_NB; k++) {
                double v;
                if (sscanf(p, "%lf%n", &v, &used) != 1)
                    break;
                ref_grid[c][r][k] = v;
                p += used;
            }
            n += k == AM_NB;
        }
        fclose(f);
        ref_grid_ok = n == 25u;
    }
    if ((f = ref_open("selfosc.txt"))) {
        n = 0;
        while (fgets(line, sizeof line, f)) {
            unsigned cu;
            double hz, pr, de;
            if (sscanf(line, "selfosc %u %lf %lf %lf", &cu, &hz, &pr, &de) != 4)
                continue;
            for (c = 0; c < 4u; c++)
                if (AM_SO_CUT[c] == cu) {
                    ref_so[c][0] = hz;
                    ref_so[c][1] = pr;
                    ref_so[c][2] = de;
                    n++;
                }
        }
        fclose(f);
        ref_so_ok = n == 4u;
    }
    if ((f = ref_open("phrase_acc.txt"))) {
        n = 0;
        while (fgets(line, sizeof line, f)) {
            unsigned s;
            double pk, ce;
            if (sscanf(line, "step %u %lf %lf", &s, &pk, &ce) == 3 && s < 4u) {
                ref_step[s][0] = pk;
                ref_step[s][1] = ce;
                n++;
            }
        }
        fclose(f);
        ref_step_ok = n == 4u;
    }
    if ((f = ref_open("phrase_slide.txt"))) {
        while (fgets(line, sizeof line, f)) {
            unsigned w;
            double db;
            if (sscanf(line, "rms %u %lf", &w, &db) == 2 && w < NELEM(ref_rms)) {
                ref_rms[w] = db;
                if (w + 1u > ref_nrms)
                    ref_nrms = w + 1u;
            }
        }
        fclose(f);
    }
}

/* ------------------------------------------------------------ (a), (b) --- */
static int32_t ibuf[AM_N];
static float imag[AM_N / 2u];

/* one impulse response of the engine through the real voice path: C2 held, the impulse 1.2 s in */
static void impulse_response(int cut, int res)
{
    acid_fresh();
    pots(0, 0, 0, 0, cut, res, 0, 0, 0);
    imp_mode = 1;
    trk_note_on(T, 36, 96);
    run(0, AM_IMP_AT);
    imp_pending = 1;
    run(ibuf, AM_N / AM_BLK);
    imp_mode = 0;
    fft_mag(ibuf, AM_N, imag);
}

static void test_grid(void)
{
    unsigned c, r, k, worst_c = 0, worst_r = 0, nfail = 0, ncmp = 0;
    double worst = 0.0;
    if (!ref_grid_ok) {
        check(0, "(a) the reference grid.txt is missing or short (run tests/acid_ref.sh)");
        return;
    }
    for (c = 0; c < 5u; c++)
        for (r = 0; r < 5u; r++) {
            double mx = -1e9;
            impulse_response(AM_CUT[c], AM_RES[r]);
            for (k = 0; k < AM_NB; k++)
                mx = ref_grid[c][r][k] > mx ? ref_grid[c][r][k] : mx;
            for (k = 0; k < AM_NB; k++) {
                double d;
                if (ref_grid[c][r][k] < mx - 50.0)            /* under the reference's -50 dB: the Q15 floor of the response */
                    continue;
                d = fabs(am_band(imag, k) - ref_grid[c][r][k]);
                ncmp++;
                if (d > 1.0)
                    nfail++;
                if (d > worst) {
                    worst = d;
                    worst_c = c;
                    worst_r = r;
                }
            }
        }
    check(nfail == 0u && ncmp > 400u, "(a) filter impulse response, 5 x 5 CUT / RES grid: %u bands compared, %u over 1 dB, worst %.2f dB (CUT %u RES %u)",
          ncmp, nfail, worst, AM_CUT[worst_c], AM_RES[worst_r]);
}

static void test_selfosc(void)
{
    unsigned c;
    double prev = 0.0;
    int track = 1;
    if (!ref_so_ok) {
        check(0, "(b) the reference selfosc.txt is missing or short");
        return;
    }
    for (c = 0; c < 4u; c++) {
        double prom, hz, dec;
        impulse_response(AM_SO_CUT[c], 127);
        hz = am_peak(imag, AM_N, 60.0, 8000.0, &prom);
        dec = am_decay(ibuf);
        {   /* independent of the donor: the ring sits just under the cutoff the pot maps to (314 Hz * 2^(2.93 CUT / 127), the 303's
             * 314 Hz .. 2.4 kHz) and rises with CUT. Measured 0.83 .. 0.91 x the cutoff */
            double fc = 314.0 * pow(2.0, 2.930586688 * AM_SO_CUT[c] / 127.0);
            track &= hz > prev && hz > 0.7 * fc && hz < 1.05 * fc;
            prev = hz;
        }
        check(fabs(hz / ref_so[c][0] - 1.0) <= 0.03 && prom >= 15.0 && fabs(dec / ref_so[c][2] - 1.0) <= 0.25,
              "(b) RES 127, CUT %u: resonance %.1f Hz (reference %.1f, %+.2f %%), prominence %.1f dB, ring decay %.0f dB/s (reference %.0f)",
              AM_SO_CUT[c], hz, ref_so[c][0], 100.0 * (hz / ref_so[c][0] - 1.0), prom, dec, ref_so[c][2]);
    }
    check(track, "(b) the resonance rises with CUT and stays within 0.7 .. 1.05 x the cutoff the pot maps to (independent of the donor)");
}

/* ------------------------------------------------------------------ (c) --- */
static void test_accent(void)
{
    static int32_t sb[AM_STEP_BLK * CTL];
    double pk[4], ce[4];
    unsigned s;
    int ok = ref_step_ok;
    acid_fresh();
    pots(0, 0, 0, 100, 50, 80, 0, 0, 81);
    for (s = 0; s < 4u; s++) {
        trk_note_on(T, 36, (s & 1u) ? 127u : 96u);
        run(sb, AM_GATE_BLK);
        trk_note_off(T, 36);
        run(sb + AM_GATE_BLK * CTL, AM_STEP_BLK - AM_GATE_BLK);
        pk[s] = am_peak_db(sb, AM_STEP_BLK * CTL);
        ce[s] = am_centroid(sb);
    }
    if (!ref_step_ok)
        check(0, "(c) the reference phrase_acc.txt is missing or short");
    for (s = 0; s < 4u && ref_step_ok; s++)
        ok &= fabs(pk[s] - ref_step[s][0]) <= 1.0 && fabs(ce[s] / ref_step[s][1] - 1.0) <= 0.05;
    check(ok && ref_step_ok, "(c) accent phrase against the donor: peaks %.1f %.1f %.1f %.1f dB (reference %.1f %.1f %.1f %.1f), centroids %.0f %.0f %.0f %.0f Hz (reference %.0f %.0f %.0f %.0f)",
          pk[0], pk[1], pk[2], pk[3], ref_step[0][0], ref_step[1][0], ref_step[2][0], ref_step[3][0],
          ce[0], ce[1], ce[2], ce[3], ref_step[0][1], ref_step[1][1], ref_step[2][1], ref_step[3][1]);
    check(pk[1] - pk[0] >= 3.0 && pk[3] - pk[2] >= 3.0 && ce[1] > ce[0] && ce[3] > ce[2],
          "(c) an accented step is >= 3 dB louder (%+.1f, %+.1f dB) with a higher centroid (%.0f > %.0f Hz, %.0f > %.0f Hz)",
          pk[1] - pk[0], pk[3] - pk[2], ce[1], ce[0], ce[3], ce[2]);
}

/* ------------------------------------------------------------------ (d) --- */
#define SL_N (AM_SLIDE_END * CTL)
static int32_t slb[SL_N];
static float sl_slew[AM_SLIDE_END], sl_env[AM_SLIDE_END];

/* the 48 -> 60 pair as the sequencer plays it: the new note on first, the old one off after (seq.c), slide_glide set.
 * retrig: the old one off first and no slide (the control: the detector must see that) */
static void slide_phrase(unsigned mode, int retrig)
{
    unsigned b;
    acid_fresh();
    T->p[P_VOICE] = (int16_t)mode;
    pots(0, 0, 0, 0, 20, 0, 0, 0, 0);
    trk_note_on(T, 48, 96);
    for (b = 0; b < AM_SLIDE_END; b++) {
        if (b == AM_SLIDE_AT) {
            if (retrig) {
                T->slide_glide = 0;
                trk_note_off(T, 48);
                trk_note_on(T, 60, 96);
            } else {
                T->slide_glide = 1;
                trk_note_on(T, 60, 96);
                trk_note_off(T, 48);
            }
        }
        if (b == AM_SLIDE_AT + 1u)
            T->slide_glide = 0;
        if (b == AM_SLIDE_OFF)
            trk_note_off(T, 60);
        run(slb + b * CTL, 1);
        sl_slew[b] = ac_of()->b.slew_y;
        sl_env[b] = ac_of()->b.env_y;
    }
}

/* the envelope of x over a sliding peak window (peak-hold of 352 samples, 8 ms: more than a period of 130 Hz) */
static double peak_env(const int32_t *x, unsigned at)
{
    unsigned i;
    int32_t m = 1;
    for (i = at; i < at + 352u; i++)
        if (labs((long)x[i]) > m)
            m = labs((long)x[i]);
    return (double)m;
}

static double f0_slope(const int32_t *x, unsigned from, unsigned to, double *f_from, double *f_to)
{
    /* rising zero crossings (interpolated) between from and to: f = 1 / period at each mid-time; least-squares slope in Hz / s */
    double t[64], f[64], last = -1.0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    unsigned i, n = 0, k;
    for (i = from; i + 1u < to && n < 63u; i++)
        if (x[i] < 0 && x[i + 1] >= 0) {
            double c = (double)i + (double)(-x[i]) / (double)(x[i + 1] - x[i]);
            if (last >= 0.0) {
                t[n] = 0.5 * (c + last) / AM_FS;
                f[n] = AM_FS / (c - last);
                n++;
            }
            last = c;
        }
    if (n < 3u)
        return 0.0;
    for (k = 0; k < n; k++) {
        sx += t[k];
        sy += f[k];
        sxx += t[k] * t[k];
        sxy += t[k] * f[k];
    }
    *f_from = f[0];
    *f_to = f[n - 1];
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

static void test_slide_mode(unsigned mode, const char *name)
{
    unsigned w, ok_rms = 1, nw = 0, b;
    double worst = 0.0, pre, post, mn, dip;
    double f0, f1, slope;
    unsigned s0 = AM_SLIDE_AT * CTL;
    slide_phrase(mode, 0);
    /* the 5 ms rms against the donor's */
    if (!ref_nrms)
        check(0, "(d) the reference phrase_slide.txt is missing");
    for (w = 0; w < ref_nrms; w++) {
        double d = fabs(am_rms_db(slb + w * AM_RMS_WIN, AM_RMS_WIN) - ref_rms[w]);
        if (ref_rms[w] < -50.0)
            continue;
        nw++;
        if (d > 1.0)
            ok_rms = 0;
        if (d > worst)
            worst = d;
    }
    check(ok_rms && nw > 40u && ref_nrms, "(d) %s: slide 48 -> 60 5 ms rms against the donor over %u windows, worst %.2f dB", name, nw, worst);
    /* no amplitude dip over 1 dB between the two notes' levels */
    pre = peak_env(slb, s0 - 1024u);
    post = peak_env(slb, s0 + 4410u);
    mn = 1e30;
    for (w = s0 + 64u; w < s0 + 4410u; w += 44u) {
        double e = peak_env(slb, w);
        mn = e < mn ? e : mn;
    }
    dip = 20.0 * log10((pre < post ? pre : post) / mn);
    check(dip <= 1.0, "(d) %s: no amplitude dip across the slide (%.2f dB below the lower of the two levels)", name, dip);
    /* the pitch climbs across 50-70 ms: the audio's zero crossings, and the engine's own slew */
    slope = f0_slope(slb, s0 + (unsigned)(0.050 * AM_FS), s0 + (unsigned)(0.070 * AM_FS), &f0, &f1);
    check(slope > 0.0 && f0 < f1 + 3.0, "(d) %s: pitch rising across 50-70 ms (audio: %.1f -> %.1f Hz, slope %+.0f Hz/s)", name, f0, f1, slope);
    {
        unsigned b0 = AM_SLIDE_AT + (unsigned)(0.050 * AM_FS / CTL), b1 = AM_SLIDE_AT + (unsigned)(0.070 * AM_FS / CTL), mono = 1, ok_env = 1;
        float fa = 440.0f * powf(2.0f, (48 - 69) / 12.0f), fb = 440.0f * powf(2.0f, (60 - 69) / 12.0f);
        for (b = b0 + 1u; b <= b1; b++)
            mono &= sl_slew[b] > sl_slew[b - 1u];
        check(mono && sl_slew[b0] > fa && sl_slew[b1] < fb * 1.0001f, "(d) %s: the slew rises strictly across 50-70 ms (%.2f -> %.2f Hz of %.2f -> %.2f)",
              name, sl_slew[b0], sl_slew[b1], fa, fb);
        for (b = AM_SLIDE_AT; b < AM_SLIDE_AT + 40u; b++)                 /* the filter envelope never jumps back up: no retrigger */
            ok_env &= sl_env[b] <= sl_env[b - 1u] * 1.0001f;
        check(ok_env, "(d) %s: the filter envelope keeps decaying through the slide (no retrigger)", name);
    }
    slide_phrase(mode, 1);                                                 /* the control: retriggered, the detector must trip */
    {
        unsigned jumped = 0;
        for (b = AM_SLIDE_AT; b < AM_SLIDE_AT + 4u; b++)
            jumped |= sl_env[b] > sl_env[b - 1u] * 1.05f;
        check(jumped, "(d) %s: control, the same pair retriggered, does show the envelope restarting", name);
    }
}

static void test_slide(void)
{
    test_slide_mode(V_LEGATO, "LEGATO");
    test_slide_mode(V_MONO, "MONO");
}

/* an unaccented slide target after an accented step is unaccented (the legato branch of mono_play must hand the new velocity on) */
static void test_legato_accent(void)
{
    acid_fresh();
    pots(0, 0, 0, 100, 50, 80, 0, 0, 81);
    trk_note_on(T, 36, 127);
    run(0, 20);
    check(ac_of()->b.accent_gain > 0.0f, "legato: an accented first step is accented");
    T->slide_glide = 1;
    trk_note_on(T, 48, 96);                          /* the slide target is plain */
    trk_note_off(T, 36);
    run(0, 2);
    check(ac_of()->b.accent_gain == 0.0f && T->v[0].vel == 96, "legato: the plain slide target after an accented step is plain (vel %u)", T->v[0].vel);
    T->slide_glide = 1;
    trk_note_on(T, 40, 127);                         /* and an accented one after it is accented again */
    trk_note_off(T, 48);
    run(0, 2);
    check(ac_of()->b.accent_gain > 0.0f && T->v[0].vel == 127, "legato: an accented slide target is accented (vel %u)", T->v[0].vel);
    {   /* overlapped notes with no slide: the pitch jumps (no slew), and still no envelope retrigger */
        float e0, f52 = 440.0f * powf(2.0f, (52 - 69) / 12.0f);
        run(0, 4);
        e0 = ac_of()->b.env_y;
        T->slide_glide = 0;
        trk_note_on(T, 52, 96);
        trk_note_off(T, 40);
        run(0, 1);
        check(fabsf(ac_of()->b.slew_y / f52 - 1.0f) < 0.0005f && ac_of()->b.env_y < e0, "legato: overlapped notes with no slide jump to the pitch (%.2f Hz of %.2f) without a retrigger", ac_of()->b.slew_y, f52);
    }
}

/* the cutoff the voice gets is CUT plus the LFO and the matrix only: ENV x ED_FLT and the accent's boost (voice.c m->cutoff) are
 * taken out, the 303's own filter envelope being ENVMOD */
static void test_cutoff_path(void)
{
    unsigned b;
    int32_t lo = 1 << 30, hi = -(1 << 30);
    acid_fresh();
    pots(0, 0, 60, 80, 50, 80, 0, 0, 127);
    T->p[P_ED_FLT] = 63;
    trk_note_on(T, 36, 127);                          /* accented, ENV at full: m->cutoff carries both */
    for (b = 0; b < 120u; b++) {
        run(0, 1);
        lo = ac_of()->cutq < lo ? ac_of()->cutq : lo;
        hi = ac_of()->cutq > hi ? ac_of()->cutq : hi;
    }
    check(lo == 50 * 256 && hi == 50 * 256, "cutoff: ENV x ED_FLT and the accent boost never reach the filter's CUT (%d .. %d of %d)", lo, hi, 50 * 256);
    T->p[P_LD_FLT] = 40;                              /* an LFO on the cutoff does */
    lo = 1 << 30;
    hi = -(1 << 30);
    for (b = 0; b < 600u; b++) {
        run(0, 1);
        lo = ac_of()->cutq < lo ? ac_of()->cutq : lo;
        hi = ac_of()->cutq > hi ? ac_of()->cutq : hi;
    }
    check(hi - lo > 3000, "cutoff: the LFO's depth reaches the filter (%d .. %d, 1/256 pot steps)", lo, hi);
}

/* ------------------------------------------------------------------ (e) --- */
/* the instantaneous cutoff by an impulse at time t into a note: the resonance peak of 2048 samples of the response (RES 100) */
static double fc_at(int decy, double t)
{
    static int32_t b2[2048];
    static float m2[1024];
    double prom;
    unsigned blk = (unsigned)(t * AM_FS / CTL);
    acid_fresh();
    pots(0, 0, decy, 0, 20, 100, 0, 0, 127);
    imp_mode = 1;
    trk_note_on(T, 36, 96);
    run(0, blk);
    imp_pending = 1;
    run(b2, 2048u / CTL);
    imp_mode = 0;
    fft_mag(b2, 2048u, m2);
    return am_peak(m2, 2048u, 200.0, 12000.0, &prom);
}

/* tau from three equally spaced cutoffs of an exponential log2 f = C + A exp(-t / tau): (x2 - x1) / (x1 - x0) = exp(-dt / tau) */
static double tau_of(int decy, double t0, double dt)
{
    double x0 = log2(fc_at(decy, t0)), x1 = log2(fc_at(decy, t0 + dt)), x2 = log2(fc_at(decy, t0 + 2.0 * dt)), q = (x2 - x1) / (x1 - x0);
    return q > 0.0 && q < 1.0 ? -dt / log(q) : 0.0;
}

static void test_decay(void)
{
    double t_fast = tau_of(0, 0.10, 0.12), t_slow = tau_of(127, 0.10, 0.60);
    check(t_fast > 0.0 && t_slow / t_fast >= 5.0, "(e) filter envelope time constant from the audio: DECY 0 %.0f ms, DECY 127 %.0f ms (%.1f x)",
          1000.0 * t_fast, 1000.0 * t_slow, t_fast > 0.0 ? t_slow / t_fast : 0.0);
    {
        double ta, tb;
        acid_fresh();
        pots(0, 0, 0, 0, 50, 64, 0, 0, 64);
        trk_note_on(T, 36, 96);
        run(0, 2);
        ta = -1.0 / (44100.0 * log(ac_of()->b.env_c));
        T->slide_glide = 0;
        trk_note_off(T, 36);
        run(0, 1000);
        pots(0, 0, 127, 0, 50, 64, 0, 0, 64);
        trk_note_on(T, 36, 96);
        run(0, 2);
        tb = -1.0 / (44100.0 * log(ac_of()->b.env_c));
        check(fabs(ta - 0.2) < 0.01 && fabs(tb - 2.5) < 0.12 && tb / ta >= 5.0, "(e) the engine's own time constants: DECY 0 %.0f ms, DECY 127 %.0f ms (200 ms .. 2.5 s)", ta * 1000, tb * 1000);
    }
}

/* ------------------------------------------------------------------ (g) --- */
static void test_envelope_hooks(void)
{
    const engine_t *e = ENGINES[ENGI_ACID];
    unsigned b, lo = 0, hi = 0, k, ended = 0, raw_over = 0;
    int32_t a;
    check(e->ownenv == 1 && e->amp && e->done && e->poly == 1u && e->mem == ACID_MEM && ACID_MEM != 0, "ACID: ownenv, amp, done, mono, mem hooks set (own state %u bytes)", (unsigned)ACID_MEM);
    if (!(e->ownenv && e->amp && e->done)) {
        check(0, "amp() / done(): ACID has no own-envelope hooks (still the placeholder)");
        return;
    }
    check(ACID_MEM == sizeof(acid_t) && ACID_MEM <= ENG_MEM_PART && ENG_MEM_PART <= (DRIFT_MEM > 800u ? DRIFT_MEM : 800u), "ACID: state %u bytes in the region of %u (ACID adds nothing to the pool: the region is WAVE's 800 or, since Task 6.2, DRIFT's voice + chorus)", (unsigned)ACID_MEM, (unsigned)ENG_MEM_PART);
    /* amp() in [0, 32767] across the corners (accent, drive, resonance, envelope depth, both waves, slide) */
    for (k = 0; k < 16u; k++) {
        acid_fresh();
        pots((int)(k & 1u), 0, (k & 2u) ? 127 : 0, 127, (k & 4u) ? 127 : 0, 127, (k & 8u) ? 127 : 0, 127, 127);
        T->p[P_ED_FLT] = 63;
        trk_note_on(T, 24 + (int)k, 127);
        for (b = 0; b < 300u; b++) {
            run(0, 1);
            a = e->amp(T, &T->v[0]);
            lo += a < 0;
            hi += a > 32767;
            raw_over += acid_vca(&ac_of()->b) > 1.0f;
            if (b == 100u) {
                T->slide_glide = 1;
                trk_note_on(T, 60 + (int)k, 127);
            }
        }
    }
    check(raw_over > 100u, "amp(): the VCA command really does exceed 1.0 here (%u ticks), so the clamp is what keeps it in range", raw_over);
    check(!lo && !hi, "amp(): inside [0, 32767] on every tick of 16 extreme settings (%u below, %u above)", lo, hi);
    /* done(): a released note ends (plain 1 ms release, and accented 50 ms) */
    for (k = 0; k < 2u; k++) {
        acid_fresh();
        pots(0, 0, 60, 80, 50, 80, 0, 0, 64);
        trk_note_on(T, 36, k ? 127u : 96u);
        run(0, 100);
        trk_note_off(T, 36);
        for (b = 0; b < 1200u && T->v[0].active; b++)
            run(0, 1);
        ended = !T->v[0].active;
        check(ended, "done(): a released %s note ends its voice (after %u blocks)", k ? "accented" : "plain", b);
    }
    /* a held note's VCA decays to 0 (1.23 s time constant): the voice must end by itself, never leak the slot */
    acid_fresh();
    pots(0, 0, 60, 80, 50, 80, 0, 0, 64);
    trk_note_on(T, 36, 96);
    for (b = 0; b < 40000u && T->v[0].active; b++)
        run(0, 1);
    check(!T->v[0].active, "done(): a held note ends by itself once its VCA reaches 0 (after %.1f s)", b * (double)CTL / AM_FS);
}


/* ------------------------------------------------- the edit slots (I1) --- */
/* E0 WAVE, E1 TUNE, E6 DRV (the soft drive) and E7 SQL: the four pots the donor vectors do not reach. Each is measured on the
 * rendered audio of a held note through the real voice path (the 303's own pot store is read as well where the audio cannot tell). */
static int32_t slot_buf[4096];
static float slot_mag[2048];

/* a held note, `skip` blocks settled, 4096 samples kept and its magnitude spectrum in slot_mag */
static void slot_note(int note, int vel, unsigned skip)
{
    trk_note_on(T, note, vel);
    run(0, skip);
    run(slot_buf, 4096u / CTL);
    fft_mag(slot_buf, 4096u, slot_mag);
}

/* the level (dB, relative) of the harmonic h of f0 Hz: the strongest bin within +-3 % of h * f0 */
static double slot_harm(double f0, unsigned h)
{
    unsigned k, a = (unsigned)(h * f0 * 0.97 * 4096.0 / AM_FS), b = (unsigned)(h * f0 * 1.03 * 4096.0 / AM_FS) + 1u, best = a;
    for (k = a; k <= b; k++)
        if (slot_mag[k] > slot_mag[best])
            best = k;
    return 20.0 * log10((double)slot_mag[best] + 1e-9);
}

static double slot_rms(const int32_t *x, unsigned n) { return am_rms_db(x, n); }

/* the fundamental (Hz) of the held note in slot_mag: the strongest bin around the nominal, parabolic */
static double slot_f0(double nominal)
{
    double prom;
    return am_peak(slot_mag, 4096u, nominal * 0.8, nominal * 1.3, &prom);
}

static void test_slots(void)
{
    static int32_t sb[4][AM_GATE_BLK * CTL];
    double h2s, h3s, h2q, h3q, fm64, f0, fp63, e_lo, e_hi, lv[2], h4[2], ce[4], tail[4];
    int rs[2] = {0, 0}, ad[2] = {0, 0};
    unsigned k;
    int plain_same;
    /* E0 WAVE: SAW against SQR (A3, filter open, no resonance). The 303's saw keeps its 2nd harmonic above its 3rd; its square is
     * the odd-harmonic wave, whose 2nd is below its 3rd. Measured +6.7 / -7.1 dB */
    acid_fresh();
    pots(0, 0, 60, 0, 127, 0, 0, 0, 0);
    slot_note(57, 96, 40);
    h2s = slot_harm(220.0, 2);
    h3s = slot_harm(220.0, 3);
    acid_fresh();
    pots(1, 0, 60, 0, 127, 0, 0, 0, 0);
    slot_note(57, 96, 40);
    h2q = slot_harm(220.0, 2);
    h3q = slot_harm(220.0, 3);
    check(h2s - h3s > 0.0 && h2q - h3q < -3.0 && (h2s - h3s) - (h2q - h3q) >= 10.0,
          "E0 WAVE: the 2nd harmonic against the 3rd is %+.1f dB for SAW and %+.1f dB for SQR (SAW > 0, SQR < -3, apart by >= 10)", h2s - h3s, h2q - h3q);
    /* E1 TUNE: the measured fundamental. pot = E1 + 64, 0.625 Hz of A4 per step (b3_set): E1 -64 / 0 / +63 is 400 / 440 / 479.4 Hz,
     * +-1.6 semitones. The FFT's bin is 10.8 Hz wide, so the ratios (parabolic peak) are compared: within 1 % */
    for (k = 0; k < 3u; k++) {
        acid_fresh();
        pots(0, k == 0u ? -64 : k == 1u ? 0 : 63, 60, 0, 127, 0, 0, 0, 0);
        slot_note(57, 96, 40);
        (k == 0u ? &fm64 : k == 1u ? &f0 : &fp63)[0] = slot_f0(220.0);
    }
    e_lo = 400.0 / 440.0;
    e_hi = (440.0 + 63.0 * 0.625) / 440.0;
    check(fabs(fm64 / f0 / e_lo - 1.0) < 0.01 && fabs(fp63 / f0 / e_hi - 1.0) < 0.01,
          "E1 TUNE: A3 measures %.1f / %.1f / %.1f Hz at -64 / 0 / +63, ratios %.4f / %.4f (expected %.4f / %.4f, +-1.6 semitones)", fm64, f0, fp63, fm64 / f0, fp63 / f0, e_lo, e_hi);
    /* E6 DRV, with the soft drive selected: a held A2 through a half-open filter at DRV 0 and 127. The level falls (the drive's
     * make-up is below its gain) by 14 dB and the 4th harmonic comes up by 11 dB against the fundamental */
    for (k = 0; k < 2u; k++) {
        acid_fresh();
        pots(0, 0, 60, 0, 70, 0, k ? 127 : 0, 0, 0);
        slot_note(45, 96, 40);
        lv[k] = am_rms_db(slot_buf, 4096u);
        h4[k] = slot_harm(110.0, 4) - slot_harm(110.0, 1);
    }
    check(ac_of()->b.drv_type == BASS303_DRV_SOFT && ac_of()->b.pot[BASS303_DRIVE] == 127 && fabs(lv[1] - lv[0]) >= 6.0 && fabs(h4[1] - h4[0]) >= 6.0,
          "E6 DRV (soft): DRV 127 against 0 moves the level %+.1f dB and the 4th harmonic %+.1f dB against the fundamental (>= 6 each)", lv[1] - lv[0], h4[1] - h4[0]);
    /* E7 SQL, on a plain and on an accented step (CUT 50 RES 80, ACC 100, ENV): on an accented one RES gains up to 24 pots and the accent
     * decay runs 7 -> 25 pots; a plain one is untouched. Measured: centroid 319 -> 566 Hz, the tail of the step +2.6 dB against its start */
    for (k = 0; k < 4u; k++) {
        acid_fresh();
        pots(0, 0, 60, 100, 50, 80, 0, (k & 1u) ? 127 : 0, 81);
        trk_note_on(T, 36, (k & 2u) ? 127 : 96);
        run(sb[k], AM_GATE_BLK);
        ce[k] = am_centroid(sb[k] + 1024);
        tail[k] = am_rms_db(sb[k] + 4096, 1024) - am_rms_db(sb[k], 1024);
        if (k >= 2u) {
            rs[k - 2u] = ac_of()->b.pot[BASS303_RESO];
            ad[k - 2u] = ac_of()->b.pot[BASS303_ACCDEC];
        }
    }
    plain_same = !memcmp(sb[0], sb[1], sizeof sb[0]);
    check(plain_same, "E7 SQL: a plain step is unchanged by SQL (bit for bit)");
    check(rs[0] == 80 && rs[1] == 104 && ad[0] == 7 && ad[1] == 25,
          "E7 SQL: an accented step gets RES pot %d -> %d and ACCDEC pot %d -> %d (SQL 0 -> 127)", rs[0], rs[1], ad[0], ad[1]);
    check(ce[3] > 1.3 * ce[2] && tail[3] - tail[2] >= 1.5,
          "E7 SQL: an accented step is brighter (centroid %.0f -> %.0f Hz) and rings on (tail against start %+.1f -> %+.1f dB) at SQL 127", ce[2], ce[3], tail[2], tail[3]);
}

/* ------------------------------------------------------------------ (f) --- */
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

/* (f) in the units of cpu_baseline.txt (tests/regress.c, void_test.c): the whole mix path, mix_block, one part sounding, the
 * other three muted. The brief's 4600 is 3 x the 1524 of OB-Z SAW LEAD there */
static int32_t mix_out[2 * CTL];
static void mix_fresh(uint32_t e, uint32_t pi)
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
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], k ? 0u : e, k ? 0u : pi);
    for (k = 1; k < NPART; k++)
        trk[k].p[P_MUTE] = 1;
    song.sel = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
    T = &trk[0];
    imp_mode = imp_pending = 0;
}

static double mix_cost(void)                       /* instructions / sample over 0.5 s of mix_block, the best of 3 */
{
    uint32_t rep, n;
    double best = 1e30;
    for (n = 0; n < FS / 4u / CTL; n++)
        mix_block(mix_out, CTL);
    for (rep = 0; rep < 3u; rep++) {
        uint64_t i0 = instr_now();
        double ipc;
        for (n = 0; n < FS / 2u / CTL; n++)
            mix_block(mix_out, CTL);
        ipc = (double)(instr_now() - i0) / (FS / 2u);
        best = ipc < best ? ipc : best;
    }
    return best;
}

/* The bound: the host figure (worst of the 32 seeds and the stress setting) is 972 .. 983 run to run; 1080 is 10 % above the worst. The
 * brief's first bound (4600, three times OB-Z's 1524 of the old baseline) was a tripwire at 3.5 x: it passed -DBASS303_OS=4 (1171)
 * and -DBASS303_CTRL=1 (1113). 1080 fails both (and OS 4 + CTRL 1: 1310). A host-instruction count: a different compiler or
 * -O level moves it, so on such a change re-measure and move the bound with a stated reason (never to hide a regression). */
#define ACID_COST_MAX HOST_PIN(1080.0, 1196.0)   /* (V2) Linux x86-64: 1087 measured + 10 % */

static void test_cost(void)
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    double idle, obz, worst = 0.0, stress;
    uint32_t k, i, worst_k = 0, ob = 0;
    if (!instr_now()) {
        printf("acid: SKIP (f) cost: NOT CHECKED, there is no instruction counter on this host (macOS proc_pid_rusage only); "
               "run it on a Mac, and Task 8.1 PERF is the gate on the device\n");
        return;
    }
    mix_fresh(ENGI_ACID, 0);
    idle = mix_cost();
    for (k = 0; k < ENGINES[ENGI_OBX]->npresets; k++)
        if (!strcmp(ENGINES[ENGI_OBX]->presets[k].name, "SAW LEAD"))
            ob = k;
    mix_fresh(ENGI_OBX, ob);
    T->p[P_VOICE] = V_POLY;
    T->p[P_SUS] = 127;
    T->p[P_DIST] = T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 0;
    for (i = 0; i < 8u; i++)
        trk_note_on(T, NOTES[i], 100);
    obz = mix_cost();
    for (k = 0; k < ENGINES[ENGI_ACID]->npresets; k++) {              /* every ACID seed, one accented note held */
        double c;
        mix_fresh(ENGI_ACID, k);
        T->p[P_DIST] = T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 0;
        trk_note_on(T, 36, 127);
        c = mix_cost();
        if (c > worst) {
            worst = c;
            worst_k = k;
        }
    }
    mix_fresh(ENGI_ACID, 0);                                          /* the dearest setting: square, resonance, soft drive 127, accent, envelope */
    T->p[P_DIST] = T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 0;
    pots(1, 0, 127, 127, 127, 127, 127, 127, 127);
    T->p[P_ED_FLT] = 63;
    trk_note_on(T, 36, 127);
    stress = mix_cost();
    printf("acid: (f) per voice, regress units (mix included): worst seed %s %.0f, stress (SQR RES 127 DRV 127 ACC 127 ENV) %.0f; "
           "OB-Z SAW LEAD (8 voices) %.0f; the mix alone %.0f (so the voice itself: %.0f / %.0f)\n",
           ENGINES[ENGI_ACID]->presets[worst_k].name, worst, stress, obz, idle, worst - idle, stress - idle);
    worst = worst > stress ? worst : stress;
    check(worst <= 3.0 * obz && worst <= ACID_COST_MAX, "(f) ACID per voice %.0f <= 3 x OB-Z %.0f (= %.0f) and <= %.0f (%.1f %% of the bound)", worst, obz, 3.0 * obz, ACID_COST_MAX, 100.0 * worst / ACID_COST_MAX);
}

#define ACID_LEVEL_DB (-18.3)                /* ACID TRAX */
#define ACID_LEVEL_DRV_DB (-17.9)            /* DIRTY 303 */
#define OTHERS_MEDIAN_DB (-17.3)             /* the median of OB-Z, WAVE and DRIFT in the audition (build/audition/summary.json) */
/* ------------------------------------------------------------ level --- */
/* Task 7.2a: ACID sat 14 dB under the other machines (VOLUME 96 in bass303.c). The same two-bar phrase on ACID preset 1, a drive
 * preset (DIRTY 303) and OB-Z SAW LEAD, through mix_block (the part, its sends, the master and its limiter: the path the audition
 * renders): the rms of the blocks above -60 dBFS. The pinned figures are the ones after the fix, +-1.5 dB; before it they were
 * -27.2 and -30.3 dBFS. The phrase is shorter than the audition's but the same shape: accents, slides, rests. */
static double phrase_rms(uint32_t eng, uint32_t pi)
{
    static const uint8_t NOTE[16] = {36, 36, 48, 36, 39, 36, 43, 36, 36, 36, 48, 36, 39, 36, 46, 36};
    static const uint8_t VEL[16] = {127, 96, 96, 96, 127, 96, 96, 96, 96, 96, 127, 96, 96, 96, 96, 127};
    static const uint8_t SLD[16] = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0};
    double ss = 0.0;
    unsigned st, n = 0, b, i;
    mix_fresh(eng, pi);
    T->p[P_DIST] = T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 0;
    for (st = 0; st < 16u; st++) {
        T->slide_glide = SLD[st];
        trk_note_on(T, NOTE[st], VEL[st]);
        for (b = 0; b < 24u * 5u; b++) {              /* 24 blocks of 5 ms ... */
            double e = 0.0;
            if (b == 14u * 5u)
                trk_note_off(T, NOTE[st]);
            mix_block(mix_out, CTL);
            for (i = 0; i < 2u * CTL; i++)
                e += (double)mix_out[i] * mix_out[i];
            if (10.0 * log10(e / (2u * CTL) / (32768.0 * 32768.0) + 1e-20) > -60.0) {
                ss += e;
                n += 2u * CTL;
            }
        }
    }
    return 10.0 * log10(ss / (n ? n : 1u) / (32768.0 * 32768.0) + 1e-20);
}
static void test_level(void)
{
    double a1, ad, ob;
    uint32_t k, ob_k = 0, dirty = 0;
    for (k = 0; k < ENGINES[ENGI_OBX]->npresets; k++)
        if (!strcmp(ENGINES[ENGI_OBX]->presets[k].name, "SAW LEAD"))
            ob_k = k;
    for (k = 0; k < ENGINES[ENGI_ACID]->npresets; k++)
        if (!strcmp(ENGINES[ENGI_ACID]->presets[k].name, "DIRTY 303"))
            dirty = k;
    a1 = phrase_rms(ENGI_ACID, 0);
    ad = phrase_rms(ENGI_ACID, dirty);
    ob = phrase_rms(ENGI_OBX, ob_k);
    printf("acid: level phrase rms: ACID TRAX %.1f dB, DIRTY 303 %.1f dB, OB-Z SAW LEAD %.1f dB\n", a1, ad, ob);
    check(fabs(a1 - ACID_LEVEL_DB) <= 1.5, "level: ACID TRAX phrase rms %.1f dBFS within 1.5 dB of %.1f", a1, ACID_LEVEL_DB);
    check(fabs(ad - ACID_LEVEL_DRV_DB) <= 1.5, "level: DIRTY 303 (DRV 75) phrase rms %.1f dBFS within 1.5 dB of %.1f", ad, ACID_LEVEL_DRV_DB);
    check(fabs(a1 - OTHERS_MEDIAN_DB) <= 3.0 && fabs(ad - OTHERS_MEDIAN_DB) <= 3.0,
          "level: ACID %.1f / %.1f dB within 3 dB of the other machines' median %.1f dB (OB-Z SAW LEAD on this phrase: %.1f)", a1, ad, OTHERS_MEDIAN_DB, ob);
    check(fabs(AM_TRIM_DB - (double)ACID_TRIM_DB) < 1e-9, "level: acid_meas.h AM_TRIM_DB %.1f is eng_acid.c's ACID_TRIM_DB %.1f", AM_TRIM_DB, (double)ACID_TRIM_DB);
}

/* ------------------------------------------------------- int headroom --- */
/* Task 7.2a fix 1: the integer headroom of mix_part (fx.c) that the ACID guard clamp (eng_acid.c ACID_GUARD_FS) has to respect.
 * mix_part computes x = ((b >> 2) * LEVEL_Q12[127]) >> 10 and then x * gl, with gl <= 4096: x must stay <= 524287 or the product
 * wraps int32 (a polarity-flipped crackle burst). The master takes ((mix_l + wet) >> 2) * mg with mg <= 4096, so the four-track sum
 * has to stay under 2^21 as well. The worst ACID settings (the reviewer's reproducer, then a sweep of WAVE, RES, SQL, ENV, DRV, CUT
 * and DECY with accent and slides, LEVEL 127) go through the real mix_block; t->peak is mix_part's own x. */
#define HR_X_LIMIT 524287
#define HR_SUM_LIMIT 2097151
typedef struct { int p[9]; } hr_cfg_t;
static int32_t hr_x, hr_sum, hr_prod_ok = 1;
static void hr_run(const hr_cfg_t *c, unsigned ntr)
{
    unsigned k, s, b, i;
    hr_x = hr_sum = 0;
    mix_fresh(ENGI_ACID, 1);                 /* (BASSLINE: the 303 on SQR; NANO's 3..32 are the SUB model since 2026-10-21) */
    for (k = 0; k < ntr; k++) {
        if (k)
            host_preset(&trk[k], ENGI_ACID, 1);
        T = &trk[k];
        T->p[P_MUTE] = 0;
        T->p[P_PAN] = 0;
        T->p[P_DIST] = T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 0;
        pots(c->p[0], c->p[1], c->p[2], c->p[3], c->p[4], c->p[5], c->p[6], c->p[7], c->p[8]);
        T->p[P_LEVEL] = 127;
    }
    T = &trk[0];
    song.master_q12 = 4096;
    for (s = 0; s < 4u; s++) {
        for (k = 0; k < ntr; k++) {
            trk[k].slide_glide = s & 1u;
            trk_note_on(&trk[k], 26, 127);
        }
        for (b = 0; b < 40u * 5u; b++) {
            if (b == 100u)
                for (k = 0; k < ntr; k++)
                    trk_note_off(&trk[k], 26);
            mix_block(mix_out, CTL);
            for (k = 0; k < ntr; k++)
                if (trk[k].peak > hr_x)
                    hr_x = trk[k].peak;
            for (i = 0; i < CTL; i++) {
                int64_t sm = (int64_t)mix_l[i] + wet[i], a = sm < 0 ? -sm : sm;
                if (a > hr_sum)
                    hr_sum = (int32_t)(a > 0x7FFFFFFF ? 0x7FFFFFFF : a);
            }
        }
    }
}
static void test_headroom(void)
{
    static const int WAVE[] = {0, 1}, RES[] = {100, 127}, SQL[] = {64, 127}, ENV[] = {90, 119, 127}, DRV[] = {0, 40}, CUT[] = {40, 51, 64},
                     DECY[] = {8, 60};
    hr_cfg_t repro = {{0, 58, 8, 127, 51, 127, 0, 127, 119}}, worst = repro, c;
    int32_t wx = 0, rx, rsum, n = 0;
    unsigned a, b, d, e, f, g, h;
    hr_run(&repro, 1);
    rx = hr_x;
    hr_run(&repro, 4);
    rsum = hr_sum;
    printf("acid: headroom reproducer (RES 127 SQL 127 ENV 119 accent DRV 0 LEVEL 127): x %d (limit %d), 4 tracks mix_l+wet %d (limit %d)\n", rx, HR_X_LIMIT, rsum, HR_SUM_LIMIT);
    check(rx <= HR_X_LIMIT, "headroom: reproducer, one track: mix_part x %d <= %d (x * 4096 stays in int32)", rx, HR_X_LIMIT);
    check(rsum <= HR_SUM_LIMIT && 4 * (int64_t)hr_x <= HR_SUM_LIMIT, "headroom: reproducer, four tracks: |mix_l + wet| %d and 4 x %d <= %d ((sum >> 2) * master stays in int32)", rsum, 4 * (int)hr_x, HR_SUM_LIMIT);
    for (a = 0; a < 2u; a++) for (b = 0; b < 2u; b++) for (d = 0; d < 2u; d++) for (e = 0; e < 3u; e++)
    for (f = 0; f < 2u; f++) for (g = 0; g < 3u; g++) for (h = 0; h < 2u; h++) {
        c.p[0] = WAVE[a]; c.p[1] = 58; c.p[2] = DECY[h]; c.p[3] = 127; c.p[4] = CUT[g]; c.p[5] = RES[b]; c.p[6] = DRV[f];
        c.p[7] = SQL[d]; c.p[8] = ENV[e];
        hr_run(&c, 1);
        n++;
        if (hr_x > wx) { wx = hr_x; worst = c; }
    }
    hr_run(&worst, 4);
    printf("acid: headroom sweep of %d settings: worst x %d (limit %d) = %.3f FS, its four-track sum %d (limit %d)\n", n, wx, HR_X_LIMIT, wx / 77704.0, hr_sum, HR_SUM_LIMIT);
    check(wx <= HR_X_LIMIT, "headroom: sweep, one track: worst mix_part x %d <= %d", wx, HR_X_LIMIT);
    check(hr_sum <= HR_SUM_LIMIT && 4 * (int64_t)hr_x <= HR_SUM_LIMIT, "headroom: sweep worst setting, four tracks: |mix_l + wet| %d and 4 x %d <= %d", hr_sum, 4 * (int)hr_x, HR_SUM_LIMIT);
    /* the clamp itself, whatever the voice does: 4 tracks at the guard's own full scale must fit */
    check(4 * (int64_t)((((ACID_GUARD_FS * 32768 - 1) >> 2) * 9713) >> 10) <= HR_SUM_LIMIT && (int64_t)((((ACID_GUARD_FS * 32768 - 1) >> 2) * 9713) >> 10) <= HR_X_LIMIT,
          "headroom: the guard clamp +-%d FS by itself: x %d, four tracks %d", ACID_GUARD_FS, (int)((((ACID_GUARD_FS * 32768 - 1) >> 2) * 9713) >> 10), 4 * (int)((((ACID_GUARD_FS * 32768 - 1) >> 2) * 9713) >> 10));
}

/* Follow-up F2 of task-7.2a-review-r2: the wet bus. Four ACID parts driven to the guard clamp (a 100 x square through the voice, LEVEL 127, PAN centre,
 * every send 127, REVERB HALL at SIZE 127 / DAMP 0, the delay's feedback up): |mix + wet| in both channels must stay under 2^21, the master's ((x >> 2) * mg).
 * At 6 FS the four dry parts are 1.865 M and the wet bus does not fit in the 232 k that is left; at 5 FS there are 543 k. */
static void test_wet_bus(void)
{
    enum { NBLK = 1200 };
    static const uint32_t HALF[5] = {7, 40, 97, 211, 1500};
    int32_t worst = 0, wx = 0, wn = 0x7FFFFFFF, guard_x = (int32_t)((((ACID_GUARD_FS * 32768) >> 2) * 9713) >> 10);
    uint32_t h, p, b, i;
    for (h = 0; h < 5u; h++) {
        mix_fresh(ENGI_ACID, 1);                 /* (BASSLINE: the 303 on SQR; NANO's 3..32 are the SUB model since 2026-10-21) */
        song.g[G_RTYPE] = 2, song.g[G_RSIZE] = 127, song.g[G_RDAMP] = 0, song.g[G_DTIME] = 3, song.g[G_DFDBK] = 120;
        song.master_q12 = 4096;
        for (p = 0; p < 4u; p++) {
            if (p)
                host_preset(&trk[p], ENGI_ACID, 1);
            T = &trk[p];
            T->p[P_MUTE] = 0, T->p[P_PAN] = 0, T->p[P_DIST] = 0, T->p[P_LEVEL] = 127;
            T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 127;
            pots(0, 58, 60, 127, 127, 127, 0, 0, 127);
            T->p[P_LEVEL] = 127;
        }
        T = &trk[0];
        imp_mode = 2, imp_half = HALF[h], imp_t = 0, imp_os = BASS303_OS, imp_blk = CTL * BASS303_OS;
        for (p = 0; p < 4u; p++)
            trk_note_on(&trk[p], 26, 127);
        for (b = 0; b < NBLK; b++) {
            imp_n = 0;
            mix_block(mix_out, CTL);
            imp_t += CTL;
            for (p = 0; p < 4u; p++) {
                wx = trk[p].peak > wx ? trk[p].peak : wx;
                wn = b > 200u && trk[p].peak < wn ? trk[p].peak : wn;
            }
            for (i = 0; i < CTL; i++) {
                int64_t sl_ = (int64_t)mix_l[i] + wet[i], sr_ = (int64_t)mix_r[i] + wet[i];
                sl_ = sl_ < 0 ? -sl_ : sl_, sr_ = sr_ < 0 ? -sr_ : sr_;
                worst = (int32_t)(sl_ > worst ? sl_ : worst);
                worst = (int32_t)(sr_ > worst ? sr_ : worst);
            }
        }
        imp_mode = 0;
    }
    printf("acid: wet bus, four parts at the %d FS guard (x %d at the guard), sends 127, HALL SIZE 127 DAMP 0: worst |mix + wet| %d (limit %d), a part's x up to %d (and at least %d once settled)\n",
           ACID_GUARD_FS, guard_x, worst, HR_SUM_LIMIT, wx, wn);
    check(wx >= guard_x - 16, "headroom: the square drives the parts into the guard (x %d, the guard's %d)", wx, guard_x);
    check(wx <= HR_X_LIMIT && worst < 2097151, "headroom: four ACID parts at the guard, sends 127, HALL SIZE 127 DAMP 0, square halves 7 .. 1500 samples: |mix + wet| %d < 2097151 (%.2f %% left), a part's x %d <= %d",
          (int)worst, 100.0 * (2097151.0 - (double)worst) / 2097151.0, (int)wx, HR_X_LIMIT);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    if (argc > 1)
        refdir = argv[1];
    load_refs();
    test_grid();
    test_selfosc();
    test_accent();
    test_slide();
    test_legato_accent();
    test_cutoff_path();
    test_decay();
    test_envelope_hooks();
    test_slots();
    test_level();
    test_headroom();
    test_wet_bus();
    test_cost();
    printf("acid_test: %s\n", bad ? "FAIL" : "ok");
    return bad ? 1 : 0;
}
