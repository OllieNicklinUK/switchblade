/* SPDX-License-Identifier: GPL-3.0-only */
/* dsp_chorus.c: the JUNO machine's chorus (2026-10-21), the Juno-60-style BBD chorus of fm1_juno_chorus.h (written for this project,
 * DIST CHORUS SUB/; docs/plans/2026-10-21-dist-chorus-sub.md), integer, behind the part hook it replaced the KR-106 float port in.
 *
 * From the module: one delay line fed the same mono signal through a one-pole BBD input filter (~9 kHz); one triangle LFO, the left tap
 * at centre + depth x lfo and the right at centre - depth x lfo (L and R move against each other); a linear-interpolated read; a one-pole
 * BBD output filter per tap; each output dry + its own wet. The Juno-60 figures: I 0.513 Hz and II 0.863 Hz sweeping 1.66 .. 5.35 ms,
 * I+II 9.75 Hz over 3.3 .. 3.7 ms; a mode change glides the centre and depth (~23 ms) instead of clicking.
 * Departures (the integer ranges of the mixer, and how a part switches it):
 *   - int32 and Q12 coefficients: a part's sum reaches several full scales (DRIFT's chords, DIST), the module's Q15 x int16 does not fit;
 *     the input is held to CHORUS_GUARD_FS first, so every product stays inside int32 (the worst, (2 x 5 FS) x 4096 = 1.34e9);
 *   - the depth x LFO product in 32 bits (the depth in Q8 samples), no 64-bit multiply (this code runs from RAM, which may call nothing);
 *   - the module's dry and wet gains as they are (-3 dB each: about the dry's level on average, +3 dB where the two are in phase);
 *   - the one-pole filters round away from zero (the module's arithmetic shift leaves a negative state stuck at -1), so they settle exactly and
 *     the line rings out in a fixed time (CHORUS_TAIL);
 *   - switching on starts a clean line (at the steady state of the first input: no step a delay later), off fades the wet out over 5 ms
 *     and the line rings out (CHORUS_TAIL) before the chorus stops; the module's optional BBD hiss is left out (its default, 0).
 * One instance per DRIFT part (the summed voices), not per voice; the state is zero when off (the part's engine memory is zeroed on a claim). */
#ifndef DSP_CHORUS_C
#define DSP_CHORUS_C
#include <stdint.h>

#define CHORUS_LEN 256u                  /* the line: the longest delay is 5.35 ms (235.9 samples) + the second tap */
#define CHORUS_CHUNK 32u                 /* chorus_block's int scratch (the mixer's block, CTL) */
#define CHORUS_SUB 8u                    /* the on / off fade is stepped every this many samples */
/* the tail: after the last non-zero input the line is heard for the longest delay, 235.9 samples: the read at 236 and its neighbour 237,
 * + 1 for the write offset = 238; then the input and output poles (0.277 a sample) settle below 1e-6 in 11 samples each: 260, as a whole
 * number of sub-blocks 264. tests/chorus_test.c measures the last audible sample from a burst and pins this from both sides. */
#define CHORUS_TAIL 264u
#define CHORUS_TRIM 1.0f                 /* the module's gains as they are (the KR-106 port trimmed its 2.12 gain sum) */
/* the guard clamp, in whole full scales (inclusive: |s| <= CHORUS_GUARD_FS), on the input and on each of L and R, so |mid| + |side| <=
 * 5 x 32768 = 163,840: mix_part (fx.c) forms x = ((b >> 2) * LEVEL_Q12[127]) >> 10 (x <= 388,520 at 5 FS, under the 524,287 of x * gl) and the
 * master needs mix + wet < 2^21 with four parts (.superpowers/sdd/task-7.2a-review.md). It is for DIST's hot sums and a runaway, not a limiter. */
#define CHORUS_GUARD_FS 5
#define CH_GUARD (CHORUS_GUARD_FS * 32768)

#define CH_LP 2960                       /* the module's BBD one-pole, 1 - exp(-2 pi 9 kHz / 44.1 kHz) = 0.7226, in Q12 */
#define CH_DRY 2896                      /* the module's -3 dB (0.7071) in Q12 */
#define CH_WET 2896
/* a one-pole step toward x, rounded away from zero: never stuck short of x, so a state reaches 0 exactly */
#define CH_POLE(s, x) ((s) += ((((x) - (s)) * CH_LP) + (((x) - (s)) > 0 ? 4095 : 0)) >> 12)
#define CH_FADE_STEP 1189                /* the 5 ms fade in Q15 per CHORUS_SUB samples: 32768 x 8 / 220.5 */

typedef struct {
    int32_t buf[CHORUS_LEN];             /* the line: the input-filtered signal (int, Q15 full scale) */
    int32_t pre, post_l, post_r;         /* the BBD input filter and the two output filters */
    int32_t center, depth;               /* the delay's centre and half-swing now, Q16 samples (gliding) */
    int32_t center_t, depth_t;           /* the mode's targets */
    int32_t dm, wm;                      /* the dry and wet gains at the start of the sub-block, Q12 */
    int32_t ddm, dwm;                    /* .. and at its end (the ramp's target) */
    int32_t fade;                        /* the wet's share, Q15 0 .. 32768, at the end of the sub-block */
    uint32_t w;                          /* the write position */
    uint32_t ph, inc;                    /* the LFO phase (2^32 = one turn) and its step per sample */
    uint8_t on, mode, req, sub;          /* running; the configured mode (1..3); the requested one; the sample in the sub-block */
    uint16_t zrun;                       /* chorus_part: the samples of exact zero input in a row (saturating); CHORUS_TAIL of them and the line has rung out */
} chorus_t;
#define CHORUS_MEM (sizeof(chorus_t))

/* the mode's LFO step and delays (the module's jch_set_mode, worked out from its microseconds and millihertz) */
static void chorus_cfg(chorus_t *c, uint32_t mode)
{
    static const uint32_t INC[4] = {0u, 49962u, 84049u, 949568u};                 /* 0.513, 0.863, 9.75 Hz x 2^32 / 44100 */
    static const int32_t CEN[4] = {0, 10129932, 10129932, 10115482};             /* (1.66 + 5.35) / 2 ms, (3.3 + 3.7) / 2 ms in Q16 samples */
    static const int32_t DEP[4] = {0, 5332304, 5332304, 578028};                  /* (5.35 - 1.66) / 2 ms, (3.7 - 3.3) / 2 ms */
    c->mode = (uint8_t)mode;
    c->inc = INC[mode];
    c->center_t = CEN[mode];
    c->depth_t = DEP[mode];
}

/* chorus_run, the per-sample loop, runs from RAM on the device (.ram_text): from the XIP flash the old chorus cost the FM-1 about a quarter of the
 * audio budget on a JUNO track. It calls nothing (tools/build.py refuses a call in .ram_text). On the host (Mach-O has no such section; the tests)
 * an ordinary function */
#if defined(__APPLE__)
#define CHORUS_HOT static __attribute__((noinline))
#define CHORUS_RUN chorus_run
#else
#define CHORUS_HOT static __attribute__((section(".ram_text"), noinline, used))
/* XIP (0x02xxxxxx) -> RAM (0x01C0xxxx) is beyond the 23-bit call range: through a pointer the compiler cannot fold (hal/fm1_flash.h FL_FAR) */
static inline void *ch_far(void *p) { void *volatile q = p; return q; }
#define CHORUS_RUN ((__typeof__(&chorus_run))ch_far((void *)&chorus_run))
#endif

/* m <= CHORUS_SUB samples: in (held to the guard) -> l, r; the gains ramp from c->dm / c->wm to dm1 / wm1 over the sub-block (k / 8 of the way after
 * sample k, so a block that ends mid-sub-block stops on the ramp's own point) */
CHORUS_HOT void chorus_run(chorus_t *c, const int32_t *in, int32_t *l, int32_t *r, uint32_t m, int32_t dm1, int32_t wm1)
{
    int32_t pre = c->pre, pl = c->post_l, pr = c->post_r, cen = c->center, dep = c->depth, ctg = c->center_t, dtg = c->depth_t;
    int32_t dm0 = c->dm, wm0 = c->wm;
    uint32_t w = c->w, ph = c->ph, inc = c->inc, i, s = c->sub;
    for (i = 0; i < m; i++) {
        int32_t k = (int32_t)(s + i + 1u), x = in[i];
        int32_t dm = dm0 + (((dm1 - dm0) * k) >> 3), wm = wm0 + (((wm1 - wm0) * k) >> 3);
        int32_t tri, mod, dl, dr, s0, s1;
        uint32_t p, idx;
        CH_POLE(pre, x);                                           /* the BBD input filter, then into the line */
        c->buf[w & (CHORUS_LEN - 1u)] = pre;
        cen += (ctg - cen) >> 10;                                  /* (the module's glide toward the mode's figures) */
        dep += (dtg - dep) >> 10;
        ph += inc;                                                 /* the triangle, Q15 */
        p = ph >> 16;
        tri = p < 32768u ? (int32_t)(p * 2u) - 32768 : (int32_t)((65535u - p) * 2u) - 32768;
        mod = (((dep >> 8) * tri) >> 15) << 8;                     /* depth x lfo in Q8 samples (32 bits), back to Q16 */
        dl = cen + mod;
        dr = cen - mod;
        idx = (uint32_t)dl >> 16;                                  /* the left tap: linear between idx and idx + 1 behind the write */
        s0 = c->buf[(w - idx) & (CHORUS_LEN - 1u)];
        s1 = c->buf[(w - idx - 1u) & (CHORUS_LEN - 1u)];
        s0 += ((s1 - s0) * ((dl & 0xFFFF) >> 4)) >> 12;
        CH_POLE(pl, s0);                                           /* its BBD output filter */
        idx = (uint32_t)dr >> 16;
        s0 = c->buf[(w - idx) & (CHORUS_LEN - 1u)];
        s1 = c->buf[(w - idx - 1u) & (CHORUS_LEN - 1u)];
        s0 += ((s1 - s0) * ((dr & 0xFFFF) >> 4)) >> 12;
        CH_POLE(pr, s0);
        l[i] = (x * dm + pl * wm) >> 12;
        r[i] = (x * dm + pr * wm) >> 12;
        w++;
    }
    c->pre = pre, c->post_l = pl, c->post_r = pr, c->center = cen, c->depth = dep, c->w = w, c->ph = ph;
}

/* n samples of the int input (Q15 full scale, any range: held to the guard) into L and R for the requested mode (0 = OFF, 1 I, 2 II, 3 I+II;
 * anything else is OFF). Returns 1 when the chorus ran for the block, 0 when it was off (L = R = the input, untouched). The fade is stepped
 * every CHORUS_SUB samples counted from the start, so any block size gives the same bits. */
static int chorus_block_i(chorus_t *c, uint32_t req, const int32_t *in, int32_t *l, int32_t *r, uint32_t n)
{
    int32_t x[CHORUS_SUB];
    uint32_t i = 0, k, m;
    if (req > 3u)
        req = 0u;
    if (!c->on) {
        if (!req) {
            for (; i < n; i++)
                l[i] = r[i] = in[i];
            return 0;
        }
        {   /* start: a clean line (never stale audio) at the steady state of the first input, so that no step from silence is heard a delay later */
            uint32_t *z = (uint32_t *)c, j;
            int32_t x0 = in[0] > CH_GUARD ? CH_GUARD : in[0] < -CH_GUARD ? -CH_GUARD : in[0];
            for (j = 0; j < sizeof *c / 4u; j++)
                z[j] = 0u;
            for (j = 0; j < CHORUS_LEN; j++)
                c->buf[j] = x0;
            c->pre = c->post_l = c->post_r = x0;
        }
        c->on = 1;
        chorus_cfg(c, req);
        c->center = c->center_t;
        c->depth = c->depth_t;
        c->dm = 4096;
    }
    c->req = (uint8_t)req;
    if (req && req != c->mode)                                     /* mode to mode: the module's glide */
        chorus_cfg(c, req);
    while (i < n) {
        int32_t dm1, wm1;
        if (!c->sub) {                                             /* a sub-block's edge: the fade's next step */
            int32_t tgt = req ? 32768 : 0, f = c->fade;
            if (!req && f <= 0) {
                c->on = 0;                                         /* faded out: stop (the next start is clean) */
                for (; i < n; i++)
                    l[i] = r[i] = in[i];
                break;
            }
            /* toward the target and no further: (f < tgt ? +step : -step) bounced 32768 <-> 31579 every sub-block once faded in,
             * a 16-sample square on the dry / wet gains (sidebands at odd multiples of FS / 16 around every note, ~-35 dB) */
            f += f < tgt ? CH_FADE_STEP : f > tgt ? -CH_FADE_STEP : 0;
            c->fade = f < 0 ? 0 : f > 32768 ? 32768 : f;
            c->ddm = 4096 - (int32_t)(((4096 - CH_DRY) * c->fade) >> 15);   /* the gains at this sub-block's end: fade 0 the input as it is, 1 the chorus */
            c->dwm = (int32_t)((CH_WET * c->fade) >> 15);
        }
        m = CHORUS_SUB - c->sub;
        m = m < n - i ? m : n - i;
        for (k = 0; k < m; k++) {
            int32_t v = in[i + k];
            x[k] = v > CH_GUARD ? CH_GUARD : v < -CH_GUARD ? -CH_GUARD : v;
        }
        dm1 = c->ddm, wm1 = c->dwm;
        CHORUS_RUN(c, x, l + i, r + i, m, dm1, wm1);
        c->sub = (uint8_t)((c->sub + m) & (CHORUS_SUB - 1u));
        if (!c->sub)                                               /* (the ramp's start for the next sub-block) */
            c->dm = dm1, c->wm = wm1;
        i += m;
    }
    return 1;
}

/* the same on floats (1.0 = full scale), for the tests */
static int chorus_block(chorus_t *c, uint32_t req, const float *in, float *l, float *r, uint32_t n)
{
    int32_t xi[CHORUS_CHUNK], li[CHORUS_CHUNK], ri[CHORUS_CHUNK];
    uint32_t o, i, m;
    int ran = 0;
    if (req > 3u)
        req = 0u;
    if (!c->on && !req) {
        for (i = 0; i < n; i++)
            l[i] = r[i] = in[i];
        return 0;
    }
    for (o = 0; o < n; o += m) {
        m = n - o < CHORUS_CHUNK ? n - o : CHORUS_CHUNK;
        for (i = 0; i < m; i++)
            xi[i] = (int32_t)(in[o + i] * 32768.0f);
        if (chorus_block_i(c, req, xi, li, ri, m)) {
            ran = 1;
            for (i = 0; i < m; i++)
                l[o + i] = (float)li[i] * (1.0f / 32768.0f), r[o + i] = (float)ri[i] * (1.0f / 32768.0f);
        } else {
            for (i = 0; i < m; i++)
                l[o + i] = r[o + i] = in[o + i];
        }
    }
    return ran;
}

/* The part's block, for fx.c's hook: b[0..n) the mono sum (Q15 full scale 32768), out: b the mid (L + R) / 2 and z the side (L - R) / 2 of the
 * chorus' pair, in the same scale. Returns 0, b untouched, when the mode is OFF (0, or anything outside 0..3: the old reserved lane level 127 a
 * stored project carries) and the chorus is not fading out. Each of L and R is held to CHORUS_GUARD_FS. */
static int chorus_part(chorus_t *c, int32_t mode, int32_t *b, int32_t *z, uint32_t n)
{
    int32_t l[CHORUS_CHUNK], r[CHORUS_CHUNK];
    uint32_t req = (uint32_t)mode <= 3u ? (uint32_t)mode : 0u, o, i, m, zr;
    int ran = 0;
    if (!c->on && !req)
        return 0;
    zr = c->zrun;
    for (o = 0; o < n; o += m) {
        m = n - o < CHORUS_CHUNK ? n - o : CHORUS_CHUNK;
        for (i = m; i > 0u && !b[o + i - 1u]; i--)         /* the zeros at the end of the chunk (one compare for a chunk that ends in sound) */
            ;
        zr = i ? m - i : (zr + m < 65535u ? zr + m : 65535u);   /* (saturating) */
        if (!c->on && !i) {                                 /* idle and silent (the tail has rung out): never starts on nothing; b is zero already */
            for (i = 0; i < m; i++)
                z[o + i] = 0;
            continue;
        }
        ran |= chorus_block_i(c, req, b + o, l, r, m);
        c->zrun = (uint16_t)zr;                             /* (after: a start clears the state) */
        if (zr >= CHORUS_TAIL)                              /* nothing went in for the whole line: it has rung out, stop (the next start is clean) */
            c->on = 0;
        for (i = 0; i < m; i++) {
            int32_t li = l[i] > CH_GUARD ? CH_GUARD : l[i] < -CH_GUARD ? -CH_GUARD : l[i];
            int32_t ri = r[i] > CH_GUARD ? CH_GUARD : r[i] < -CH_GUARD ? -CH_GUARD : r[i];
            b[o + i] = (li + ri) >> 1;
            z[o + i] = (li - ri) >> 1;
        }
    }
    return ran;
}
#endif
