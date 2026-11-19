/* SPDX-License-Identifier: GPL-3.0-only */
/* VOID: the VOID WAVE plugin's voice (VoidWave/Source/DSP), in Felucca's fixed point.
 *
 * ONE wavetable oscillator on the plugin's built-in tables 0-8 (tools/gen_void_tables.py: the same
 * frames, keyframes crossfaded, band-limited per octave); then, as the plugin's Filter.cpp: a 2 % x|x|
 * pre-saturation, its drive shaper (gain, sine, tanh), and the filter TYPE (the track's P_FTYPE): LP = the
 * plugin's LP24 tanh ladder (fb 4.2 re, makeup 1 + 1.8 re), BP / HP / LP12 / HP24 / NOTCH its Simper SVF
 * (k = 2 (1 - r)^1.5); then its 0.72 soft clipper (VoiceManager.cpp).
 * Task 4.5, Felucca's own and not in the plugin: TYPE 6 VOWEL (two parallel band-passes at the formants of the vowel CUT stands at: U O A E I
 * from CUT 0 to 127, RES = their Q, 2 .. 12) and WARP (P_E2, 64 = off: a phase bend of the table read, the knee at 0.5 + 0.45 w; read by the
 * slow loop, so the fast loops are as they were).
 * The plugin's movement, per voice: pitch drift (+-1.5 cents, a damped random walk), WT drift (up to
 * +-0.055 of the position), and a micro-LFO (0.01-0.25 Hz, 0.4-2.5 cents, 0.5-2 % level), new random
 * values every note; phase 0 at a new note (its FREE mode); a 5 ms fade-in before the filter.
 *
 * A departure from the plugin, by the user's decision (Task 4.4, 2026-10-18): the plugin has TWO oscillators (OSC2,
 * blended by osc_mix: Blend / Ring / Sync / FM, and a Morph mix of OSC1 reading both tables); WAVE has one. OSC2
 * and MIX are gone, and with them the OSC2 pitch (its semitone and fine steps), MORPH and the second micro-LFO's
 * state. The keyframe crossfade of the one oscillator (the plugin's frame morph along POS) is kept: a position sweep
 * still morphs smoothly. Their edit slots (P_E2 TBL2, P_E3 MIX, P_E6 OSC2) are hidden, not removed: the ids stay, so
 * the stored layout (FOR4, user presets, the editor) does not move, and a value a project saved there sounds nothing.
 * The plugin's detuned pairs (two oscillators of one table a few cents apart) lose their beating here; UNISON and
 * DETUNE (a voice each) bring it back (presets/four/wave.py names the seven).
 *
 * Felucca's own: the ADSR (amp: the plugin's ENV2; ENV -> FILTER, PITCH, SHAPE), the LFO, the matrix, UNISON /
 * SPREAD, glide and the FX. SHAPE moves the WT position (127 steps of it = the whole table): the plugin's LFO -> WT
 * POS. The plugin's ENV1, the filter's own envelope (EnvelopeGenerator.cpp: its EXP curves, velocity 0.5), is the
 * FLT ENV page: the track's P_FM1_ATK .. P_FM1_REL (the retired DIGITAL's operator 1, unused by every other engine
 * of a default build), on while P_FM1_LEVEL > 0 (1..127: its velocity sensitivity 0..1, the plugin's env1_vel_sens);
 * ENV DEST FLT is then its depth, the V2 plugin's: env * depth * 4 octaves (63 = depth 1: four octaves up at the top;
 * V1's was the cutoff times 1 + env * depth, one octave). P_FM1_LEVEL 0: the filter follows the amp ADSR by ENV
 * DEST FLT in cutoff steps, as every other engine.
 *
 * One departure from the plugin: a DC blocker per voice (void_dc, R 0.9985) between the voice's level and the
 * soft clipper. The plugin has none; the FM-1 takes a part's reverb send ahead of the master's DC blocker.
 *
 * V2 (2026-11-19, the lean upgrade to the V2 plugin, D:/VOID SYNTH): the tables at V2's level (tools/gen_void_tables.py: DC out,
 * RMS 0.35; SQUARE by VOID_SQ_GAIN), V2's drive shaper (Filter.cpp softClip: gain 1 + 10 d^2, the fold's input soft-bounded to
 * +-3.2, the fold 1 + 0.25 d, tanh (1.8 + 5 d), blended in by min(1, 4 d); not its 4x oversampling: the FM-1 has no room for it),
 * and three of V2's voice parts, in the two hidden slots and the retired DIGITAL's operator 2 / 3 ids:
 *   PENV (E3, -24..24 semitones): V2's pitch envelope, hardwired to the oscillator: 1 ms up, 50 ms down (sustain 0), its peak
 *        0.5 + 0.5 velocity (velSens 0.5), the EXP curve u^2; per control block. (V2's attack is an edit there; every V2 preset
 *        that uses the envelope has it at 1 ms.) The sub follows the base pitch, as in V2.
 *   SUB (E6, 0..127): V2's sine sub oscillator, one octave down (P_FM2_DEC < 64) or two (>= 64), its level SUB / 127 of
 *        the plugin's 1.0, before the filter, faded in with the oscillator.
 *   NOISE (P_FM2_ATK, the NOISE page): V2's noise, white through a one-pole low-pass at 200 * 100^(COLOR / 127) Hz
 *        (COLOR: P_FM2_SUS, 127 = white), its level NOISE / 127 of the plugin's 1.0, before the filter.
 *   OLVL (P_FM3_ATK, on no page: a preset's own): the oscillator's level, 64 = the plugin's default 0.8 (0: also 64, the
 *        default of every older preset and project), so the V2 presets' own balance of oscillator, sub and noise and their
 *        level (V2 Phase B) come across.
 * SUB and NOISE cost nothing while both are 0 (the fast loops are specialised on them: every older sound is bit for bit as
 * before); PENV costs a pitch lookup per block while it moves.
 *
 * Signals: Q15 with 1.0 = the plugin's 1.0 (the tables hold it as VOID_WT_ONE); products are kept
 * under 2^31 by the shifts noted at them. State: per voice in the part's engine memory (void_vc_t). */
#include "void_tables.h"

/* the hot path's helpers, always inlined. Spelled __attribute: the host simulator (tests/hostsim.c) compiles
 * __attribute__ away (the sections), which would leave the render loop calling them there and its cost (regress.c,
 * void_test.c) unlike the firmware's */
#define VOID_INLINE static inline __attribute((always_inline))

/* Every table read whose index comes from a signal goes through VOID_AT; a host test (tests/void_test.c) defines VOID_CHK to count the
 * reads that fall outside the table. In the firmware it is the plain read. */
#ifndef VOID_CHK
#define VOID_CHK(i, n) (i)
#endif
#define VOID_AT(tab, i) ((tab)[VOID_CHK((i), NELEM(tab))])
/* a test may force the one-copy slow loop for every block, to prove the fast loops bit for bit the same (tests/void_test.c) */
#ifndef VOID_FORCE_SLOW
#define VOID_FORCE_SLOW 0
#endif

static const char *const N_VOID_TBL[VOID_NTBL] = {"SINE", "SAW", "SQUARE", "SPECT", "FORMA", "FORME", "BASS1",
                                                  "BASS2", "NOISE"};
enum { VF_LP24, VF_BP, VF_HP, VF_LP12, VF_HP24, VF_NOTCH, VF_VOWEL };   /* P_FTYPE (params.c N_FTYPE) */
#define VOID_VOWEL_G1 4096       /* VOWEL: formant 1's gain, Q12 (peak gain 1 at its centre) */
#define VOID_VOWEL_G2 2867       /* formant 2's: 0.7 */
/* both times a trim of 1 + 0.0058 RES + 0.00083 RES^2 (Q12): a band-pass of unit peak passes less of a saw as its Q rises (Q 2 -> 12), 14 dB by RES
 * 127; this holds the level within 2 dB of LP24's at the same CUT where LP24 is open (CUT 64 and up; below, the ladder is shutting), tests/void_test.c */
#define VOID_WARP_OFF 64         /* P_E2 = WARP: 64 is off */
#define VOID_WARP_MIN 9          /* WARP's lowest value: TBL2 (this slot until Task 4.5) held 0..8, which a stored project or user preset then loads as 64 */
_Static_assert(VOID_N0LOG2_MIN >= VOID_NLEV, "void_osc_at: the level search shifts by n0l - 2 - lv, lv < NLEV, so n0l - 2 >= NLEV - 1");
#define VOID_SQUARE 2u
#define VOID_WT_NORM ((int32_t)((32768LL * 32768 + VOID_WT_ONE / 2) / VOID_WT_ONE))   /* table -> Q15 */
#define VOID_LIM_INC 1753014234u /* 18 kHz as a phase increment: the top harmonic of a level stays below */
#define VOID_RAMP_STEP 149       /* the 5 ms fade-in, Q15 per sample (32768 / 220.5) */
#define VOID_CLIP_THR 23593      /* the soft clipper's 0.72 */
/* the largest |y| the soft clipper's table reaches: it reads VOID_SOFT[o >> 8] and [(o >> 8) + 1] for
 * o = |y| - VOID_CLIP_THR, so o must stay at or below 255 * 256 + 255. Up to Task 4.2 the clamp at
 * VOID_OUT_LEVEL held that on its own (|y| <= 62224); the DC blocker after it has a peak gain of 2
 * (the l1 norm of 1, -(1 - R) R^n), so its output is clamped here instead. */
#define VOID_CLIP_MAX (VOID_CLIP_THR + 255 * 256 + 255)
_Static_assert(NELEM(VOID_SOFT) == 257, "VOID_CLIP_MAX: VOID_SOFT's length");
#define VOID_OUT_LEVEL 18536     /* the plugin's voice: OSC level 0.8 * centre pan 0.707, Q15 */
#define VOID_DC_R 32719          /* the DC blocker's R, 0.9985 in Q15 (-3 dB at ~10 Hz, 44.1 kHz) */
#define VOID_DC_SH 10            /* bits its state keeps below the signal's Q15 */
/* voices a VOID part plays at most (POLY, UNISON). 6 of its heaviest preset cost about what 8 of PHASE WIRE, the
 * heaviest engine before it, do (tests/void_test.c, host instructions); raise it once the FM-1's PERF shows room */
#define VOID_POLY 6
#define VOID_PENV_A 44           /* PENV: its attack, 1 ms in samples (V2 pitchenv_attack of every preset that uses it) */
#define VOID_PENV_D 2205         /* its decay, V2's fixed 50 ms */
#define VOID_FENV_K 229          /* FLT ENV (V2): 4 octaves of cutoff steps (127 / log2(16000 / 30) a one) per 63 of ENV DEST FLT, Q8 */
#define VOID_XG 228              /* SUB / NOISE: Q14 per step, 127 = the plugin's 1.0 in WAVE's oscillator units (1 / 0.5657, VOID_OUT_LEVEL) */

typedef struct {                 /* per voice (engine memory) */
    int32_t f[4];                /* filter: the ladder's y0..y3; the SVFs' ic1, ic2 (HP24: and its second) */
    int32_t pd, pdv;             /* pitch drift and its velocity, cents Q20 */
    int32_t wd, wdv;             /* WT drift and its velocity, position Q20 */
    uint32_t lph, linc;          /* the micro-LFO: phase, increment per control block */
    int32_t lpit, llev;          /* its depths: cents Q20, level Q15 */
    int32_t rng;
    int32_t ramp;                /* fade-in, Q15 */
    int32_t dcw;                 /* the DC blocker's state, Q(15 + VOID_DC_SH) */
    uint32_t tick;               /* control blocks since the note: the plugin's per-block walk every 4th */
    uint32_t est, eu;            /* the filter envelope: stage (0 off, 1 attack, 2 decay / sustain, 3 release), */
    int32_t elv, epk, ers;       /* progress Q24; level, peak (velocity), the release's start, Q15 */
    uint32_t sph;                /* V2: the sub oscillator's phase */
    int32_t nlp, nrng;           /* the noise: its low-pass state (Q15), its generator */
    uint32_t pen;                /* PENV: samples since the note (stops counting past its end) */
    int32_t pepk;                /* its peak, 0.5 + 0.5 velocity, Q15 */
} void_vc_t;
#define VOID_MEM (sizeof(void_vc_t) * NVOICE)
ENG_MEM_FITS("VOID", VOID_MEM);

static void_vc_t *void_vc(const track_t *t, const voice_t *v)
{
    return (void_vc_t *)ENG_MEM((uint32_t)(t - trk) % NPART) + (uint32_t)(v - t->v) % NVOICE;
}

/* tanh(x / 32768) * 32767 up to x = 4.0 (VOID_TANH), linearly interpolated; past it, its last value */
VOID_INLINE int32_t void_tanh(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    y = i >= 512 ? VOID_TANH[512]
                 : VOID_AT(VOID_TANH, i) + (((VOID_AT(VOID_TANH, i + 1) - VOID_AT(VOID_TANH, i)) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

/* the same for |x| <= 32767 (VOID_TANH1: signed, no clamp): the ladder's stages */
VOID_INLINE int32_t void_tanh1(int32_t x)
{
    uint32_t u = (uint32_t)(x + 32768), i = u >> 8;
    int32_t a = VOID_AT(VOID_TANH1, i);
    return a + (((VOID_AT(VOID_TANH1, i + 1u) - a) * (int32_t)(u & 255u)) >> 8);
}

/* a * b / 32768 for |a| < 2^17 (signals up to 4.0), b <= 32767 */
VOID_INLINE int32_t vmul(int32_t a, int32_t b) { return ((a >> 1) * b) >> 14; }

static inline int32_t void_rnd16(void_vc_t *c) { return (int32_t)(noise32(&c->rng) >> 16); }   /* 0..65535 */

/* ------------------------------------------------------------------ oscillators --- */
typedef struct {
    const int16_t *a, *b;        /* the two keyframes' level (b unused when w = 0) */
    uint32_t sh, mask;           /* index = phase >> sh; the level's length - 1 */
    int32_t w;                   /* crossfade a -> b, Q15 */
    uint32_t pw;                 /* SQUARE: the pulse's width as a phase (0: not the SQUARE) */
    uint32_t kp, m1, m2;         /* WARP: the knee as a phase (0: off), and the slopes 0.5 / k, 0.5 / (1 - k) below and above it, Q16 */
} void_osc_t;

/* table tb at position pos (frames Q8, 0 .. 255 << 8), for a phase increment inc */
static void void_osc_at(void_osc_t *o, uint32_t tb, int32_t pos, uint32_t inc)
{
    uint32_t n0l = VOID_N0LOG2[tb], hmax = (1u << n0l) >> 2, lv = 0, k = VOID_KFIRST[tb], k1 = VOID_KFIRST[tb + 1] - 1u;
    uint32_t ofs;
    while (lv < VOID_NLEV - 1u && inc > (VOID_LIM_INC >> (n0l - 2u - lv)))   /* the most harmonics below 18 kHz: */
        lv++;                                          /* hmax >> lv is a power of two (n0l >= 7), so the divide is a shift */
    ofs = (2u << n0l) - ((2u << n0l) >> lv);
    o->sh = 32u - (n0l - lv);
    o->mask = ((1u << n0l) >> lv) - 1u;
    o->w = 0;
    o->pw = 0;
    if (tb == VOID_SQUARE) {                           /* one saw (1 - 2 phase); the pulse is two readings */
        o->a = o->b = VOID_WAVE + VOID_KOFS[k] + ofs;  /* of it: width 0.5 - 0.45 t (the plugin's 50 -> 5 %) */
        o->pw = 2147483648u - ((uint32_t)pos >> 2) * 118427u;   /* 0.45 * 2^32 / 65280 * 4 */
        return;
    }
    while (k < k1 && ((int32_t)VOID_KPOS[k + 1] << 8) <= pos)
        k++;
    o->a = o->b = VOID_WAVE + VOID_KOFS[k] + ofs;
    if (k < k1) {
        int32_t k0p = (int32_t)VOID_KPOS[k] << 8, span = (int32_t)(VOID_KPOS[k + 1] - VOID_KPOS[k]);
        o->b = VOID_WAVE + VOID_KOFS[k + 1] + ofs;
        o->w = clamp(((pos - k0p) << 7) / span, 0, 32767);
    }
}

/* the oscillator for table tb at position pos and phase increment inc, with WARP wv (P_E2, 64 = off): the knee k = 0.5 + 0.45 w, w = (WARP - 64) / 63.
 * The band limit follows the steepest segment: the level is chosen for inc x max(0.5 / k, 0.5 / (1 - k)), so a heavy warp reads a darker level
 * instead of aliasing. The darkest level (VOID_NLEV - 1) holds n0 / 4 >> 5 harmonics and is the end of that: a note whose steep segment would
 * outrun it (inc x m above LIM_INC >> (n0l - 2 - (NLEV - 1))) gets the knee pulled towards 1/2 until it just fits, so a warp fades out up the keyboard
 * (about 2 kHz and up it does nothing) instead of aliasing worse than the same note unwarped */
static __attribute__((noinline)) void void_osc_warp(void_osc_t *o, uint32_t tb, int32_t pos, uint32_t inc, int32_t wv)
{
    uint32_t einc = inc, kp = 0, m1 = 0, m2 = 0;
    wv = clamp(wv, 0, 127);
    if (wv != VOID_WARP_OFF) {
        int32_t w = clamp((wv - VOID_WARP_OFF) * 520, -32767, 32767);
        uint32_t k16 = (uint32_t)(32768 + ((w * 29491) >> 15));    /* k, Q16: 0.05 .. 0.95 */
        uint32_t emax = VOID_LIM_INC >> (VOID_N0LOG2[tb] - 2u - (VOID_NLEV - 1u));
        uint32_t r8 = ((emax >> 10) << 8) / ((inc >> 10) | 1u);    /* emax / inc, Q8 (the largest slope factor the darkest level takes) */
        uint32_t kmin;
        r8 = r8 > 32767u ? 32767u : r8;
        kmin = r8 > 256u ? (1u << 23) / r8 : 32768u;               /* 0.5 / (that), Q16 */
        k16 = k16 < kmin ? kmin : k16 > 65536u - kmin ? 65536u - kmin : k16;
        if (k16 != 32768u) {
            uint64_t e;
            m1 = (1u << 31) / k16;
            m2 = (1u << 31) / (65536u - k16);
            kp = k16 << 16;
            e = ((uint64_t)inc * (m1 > m2 ? m1 : m2)) >> 16;
            einc = e > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)e;
        }
    }
    void_osc_at(o, tb, pos, einc);
    o->kp = kp;
    o->m1 = m1;
    o->m2 = m2;
}

/* one level read at phase ph, linear between its samples */
VOID_INLINE int32_t void_rd(const int16_t *t, uint32_t ph, uint32_t sh, uint32_t mask)
{
    uint32_t i = ph >> sh;
    int32_t f = (int32_t)((ph << (32u - sh)) >> 17);  /* the fraction, Q15 */
    int32_t a = t[i], b = t[(i + 1u) & mask];
    return a + (((b - a) * f) >> 15);
}

/* WARP's phase bend: the phase through a two-segment knee, [0, k) -> [0, 1/2) and [k, 1) -> [1/2, 1), slopes m1 = 0.5 / k and m2 = 0.5 / (1 - k) in Q16 */
VOID_INLINE uint32_t void_bend(const void_osc_t *o, uint32_t ph)
{
    return ph < o->kp ? (uint32_t)(((uint64_t)ph * o->m1) >> 16)
                      : 0x80000000u + (uint32_t)(((uint64_t)(ph - o->kp) * o->m2) >> 16);
}

/* the oscillator at phase ph, in table units (the SQUARE's pulse halved: it spans twice the table's peak) */
VOID_INLINE int32_t void_osc(const void_osc_t *o, uint32_t ph, const int warp)
{
    int32_t s;
    if (warp && o->kp)                                 /* WARP (warp: a constant 0 in the four fast loops, which are then as they were before it) */
        ph = void_bend(o, ph);
    s = void_rd(o->a, ph, o->sh, o->mask);
    if (o->pw)                                         /* (halved: its weight is doubled) */
        return (s - void_rd(o->a, ph - o->pw, o->sh, o->mask)) >> 1;
    if (o->w)
        s += ((void_rd(o->b, ph, o->sh, o->mask) - s) * o->w) >> 15;
    return s;
}

/* inc * 2^(cents / 1200), cents Q20 (a few cents: the drift, the micro-LFOs) */
static inline uint32_t void_detune(uint32_t inc, int32_t ct20)
{
    int32_t r = ((ct20 >> 4) * 606) >> 16;            /* inc ratio - 1, Q20 (606 / 2^20 = ln 2 / 1200) */
    return inc + (uint32_t)(((int32_t)(inc >> 12) * r) >> 8);
}

/* the filter envelope (the plugin's ENV1: EXP curves, u^2) over one control block: its level, Q15 */
static int32_t void_env1(void_vc_t *c, const int16_t *p, int gate)
{
    static const int8_t ID[3] = {P_FM1_ATK - P_FM1_ATK, P_FM1_DEC - P_FM1_ATK, P_FM1_REL - P_FM1_ATK};
    int32_t sus = p[P_FM1_SUS] * 258, u, w;
    if (c->est && c->est < 3u && !gate) {              /* the release: from where it is */
        c->est = 3;
        c->eu = 0;
        c->ers = c->elv;
    }
    if (!c->est)
        return 0;
    c->eu += ENV_LIN[clamp(p[P_FM1_ATK + ID[c->est - 1u]], 0, 127)];   /* Q24 per block (Felucca's times) */
    if (c->eu >= 1u << 24) {
        c->eu = 1u << 24;
        if (c->est == 1u) {                            /* attack done: the decay */
            c->est = 2;
            c->eu = 0;
        } else if (c->est == 3u) {
            c->est = 0;
            return c->elv = 0;
        }
    }
    u = (int32_t)(c->eu >> 9);                         /* Q15 */
    if (c->est == 1u)
        return c->elv = mulq15(mulq15(u, u), c->epk);  /* curve(n / A) * peak */
    w = mulq15(32767 - u, 32767 - u);                  /* curve(1 - n / X) */
    if (c->est == 2u)                                  /* S peak + (peak - S peak) curve */
        return c->elv = mulq15(sus, c->epk) + mulq15(c->epk - mulq15(sus, c->epk), w);
    return c->elv = mulq15(c->ers, w);                 /* start * curve */
}

/* ------------------------------------------------------------------------ filter --- */
/* Filter.cpp processSample: the pre-saturation, the drive, the TYPE, the close-gain; coefficients per block */
typedef struct {
    uint32_t ft;                 /* VF_* */
    int32_t gq, kq, fb, mk;      /* ladder: g / (1 + g) Q15, -, feedback Q12, makeup Q14; SVF: -, k Q12 */
    int32_t sat;                 /* SVF: its state saturation above RES 0.78 (r - 0.78, Q15; 0 below) */
    tsvf_t sv;
    int32_t drv, dg, dasym, doff, dk, dm, drec;   /* the drive shaper (drv 0: off) */
    int32_t dw;                  /* V2: the shaper's share, min(1, 4 d), Q15 */
    int32_t close;               /* close-gain, Q15 (LP12 / LP24: fades the output as the cutoff reaches 20 Hz) */
    tsvf_t sv2;                  /* VOWEL: sv = formant 1, sv2 = formant 2 (after the fields the loops read: their offsets stay as they were) */
    int32_t vg1, vg2;            /* VOWEL: the formants' gains, Q12 */
} void_flt_t;

/* cut: the cutoff index (CUTOFF_HZ) Q8; res, drv 0..127; shut: the cutoff is at the plugin's 20 Hz (CUT 0, no
 * modulation up: the index stops at 30 Hz) */
static void void_flt_set(void_flt_t *fl, uint32_t ft, int32_t cut, int32_t res, int32_t drv, int shut)
{
    int32_t p15 = VOID_RES_P15[res], ci = cut >> 8;    /* (1 - r)^1.5 */
    fl->ft = ft;
    fl->kq = clamp(p15 >> 2, 40, 8192);                /* SVF k = 2 (1 - r)^1.5, Q12 */
    fl->sat = res * 258 > 25559 ? res * 258 - 25559 : 0;   /* applySVF: above r 0.78 */
    fl->drv = drv;
    if (ft == VF_VOWEL) {                              /* two band-passes at the formants of the vowel CUT stands at; Q = 2 + 10 RES / 127 */
        int32_t i = 2 * ci, fr = cut & 255, a1 = VOID_AT(VOID_VOWEL, i), a2 = VOID_AT(VOID_VOWEL, i + 1),
                b1 = VOID_AT(VOID_VOWEL, i + 2), b2 = VOID_AT(VOID_VOWEL, i + 3), t;
        fl->kq = (int32_t)((8192u * 127u) / (uint32_t)(254 + 10 * res));   /* k = 2 / Q, Q12 */
        tsvf_coef_k(&fl->sv, a1 + (((b1 - a1) * fr) >> 8), fl->kq);
        tsvf_coef_k(&fl->sv2, a2 + (((b2 - a2) * fr) >> 8), fl->kq);
        t = 4096 + res * 24 + ((res * res * 53) >> 6);
        fl->vg1 = (((VOID_VOWEL_G1 * t) >> 12) * fl->kq) >> 12;        /* (k: the band-pass of unit peak gain, v1 times k) */
        fl->vg2 = (((VOID_VOWEL_G2 * t) >> 12) * fl->kq) >> 12;
    } else if (ft == VF_LP24) {
        int32_t g = SVF_G[ci], re = 32767 - p15;       /* tan(pi fc / FS) Q12; re = 1 - (1 - r)^1.5 */
        if (ci < 127)
            g += ((SVF_G[ci + 1] - g) * (cut & 255)) >> 8;
        g = (int32_t)(((uint32_t)g << 15) / (uint32_t)(4096 + g));   /* g / (1 + g), Q15 */
        fl->gq = g > 32000 ? 32000 : g;
        fl->fb = (re * 17203) >> 15;                   /* 4.2 re, Q12 */
        fl->mk = 16384 + ((re * 29491) >> 15);         /* 1 + 1.8 re, Q14 */
    } else {
        tsvf_coef_k(&fl->sv, cut, fl->kq);
    }
    fl->close = 32767;
    if (ft == VF_LP24 || ft == VF_LP12) {              /* (cutoff - 20) / (60 + 220 r^2) */
        int32_t hz = CUTOFF_HZ[ci], r2 = (res * res * 220) / (127 * 127);
        if (ci < 127)
            hz += ((CUTOFF_HZ[ci + 1] - hz) * (cut & 255)) >> 8;
        fl->close = shut ? 0 : clamp(((hz - 20) << 15) / (60 + r2), 0, 32767);
    }
    if (drv) {                                         /* V2's softClip, d = DRV / 127 */
        int32_t d = drv * 258;
        fl->dg = 4096 + ((mulq15(d, d) * 10) >> 3);    /* gain 1 + 10 d^2, Q12 */
        fl->dasym = mulq15(d, 10486);                  /* asym 0.32 d */
        fl->doff = mulq15(fl->dasym, 14746);           /* 0.45 asym */
        fl->dk = 32768 + mulq15(d, 8192);              /* sin(y pi / 2 (1 + 0.25 d)): the phase of y (Q15) */
        fl->dm = 7373 + mulq15(d, 20480);              /* tanh(. (1.8 + 5 d)), Q12 */
        fl->drec = (int32_t)((32768u << 15) / (uint32_t)(32768 + mulq15(d, 5898)));   /* 1 / (1 + 0.18 d) */
        fl->dw = d * 4 > 32767 ? 32767 : d * 4;        /* the dry / wet blend, min(1, 4 d) */
    }
}

/* the input stage: the pre-saturation 0.978 (x + 0.022 x|x|), then the drive (drv: fl->drv != 0, a constant where inlined) */
VOID_INLINE int32_t void_front(const void_flt_t *fl, int32_t s, const int drv)
{
    s = vmul(s + vmul((s >> 1) * ((s < 0 ? -s : s) >> 1) >> 13, 721), 32047);
    if (drv) {
        int32_t yy = ((s * (fl->dg >> 4)) >> 8) + fl->dasym;   /* x gain + asym (|s| < 2^17, gain < 2^12 Q8: |yy| < 2^22) */
        yy = (void_tanh((yy >> 4) * 5) * 26214) >> 13;           /* 3.2 tanh(0.3125 y): the fold's input within +-3.2 (V2) */
        yy = sine_i((uint32_t)yy * (uint32_t)fl->dk);           /* (the phase wraps: sin is periodic) */
        yy = void_tanh((yy * fl->dm) >> 12);
        yy = mulq15(yy - fl->doff, fl->drec);
        s += mulq15(yy - s, fl->dw);                             /* V2: in by min(1, 4 d) (|yy - s| < 2^18) */
    }
    return s;
}

/* applyLadder: four tanh one-poles, a unit-delay feedback (the stages stay within +-1: void_tanh1) */
VOID_INLINE int32_t void_ladder(const void_flt_t *fl, int32_t *f, int32_t s)
{
    int32_t u = clamp(s - ((fl->fb * f[3]) >> 12), -160000, 160000);
    int32_t x = void_tanh(((u >> 2) * 29491) >> 13);   /* tanh(0.9 u) */
    f[0] += mulq15(fl->gq, void_tanh1(x) - f[0]);
    f[1] += mulq15(fl->gq, void_tanh1(f[0]) - f[1]);
    f[2] += mulq15(fl->gq, void_tanh1(f[1]) - f[2]);
    f[3] += mulq15(fl->gq, void_tanh1(f[2]) - f[3]);
    return (f[3] * fl->mk) >> 14;
}

/* applySVF's saturation above r 0.78: x / (1 + a), a = c |x| (c = sat 0.18 or sat 0.08, Q15; a < 0.2), as
 * x (1 - a + a^2) (within 0.7 %) */
VOID_INLINE int32_t void_svf_sat(int32_t x, int32_t c)
{
    int32_t a = (c * (x < 0 ? -x : x)) >> 15;
    return ((x >> 2) * (32768 - a + ((a * a) >> 15))) >> 13;
}

/* Simper SVF on f[o], f[o + 1]: LP v2, BP v1, HP x - k v1 - v2, notch x - k v1 */
VOID_INLINE int32_t void_svf1(const tsvf_t *c, const void_flt_t *fl, int32_t *f, int32_t s, uint32_t ft)
{
    int32_t v3 = s - f[1], v1 = (c->a1 * f[0] + c->a2 * v3) >> 13, v2 = f[1] + ((c->a2 * f[0] + c->a3 * v3) >> 13);
    f[0] = clamp(2 * v1 - f[0], -150000, 150000);
    f[1] = clamp(2 * v2 - f[1], -150000, 150000);
    if (fl->sat) {
        f[0] = void_svf_sat(f[0], (fl->sat * 5898) >> 15);
        f[1] = void_svf_sat(f[1], (fl->sat * 2621) >> 15);
    }
    if (ft == VF_LP12)
        return v2;
    if (ft == VF_BP)
        return v1;
    if (ft == VF_NOTCH)
        return s - ((fl->kq * v1) >> 12);
    return s - ((fl->kq * v1) >> 12) - v2;            /* HP, and the first of HP24 */
}
VOID_INLINE int32_t void_svf(const void_flt_t *fl, int32_t *f, int32_t s)
{
    int32_t y, a;
    if (fl->ft == VF_VOWEL) {          /* two band-passes side by side on f[0..1] and f[2..3], summed with the gains in vg1, vg2 (k v1 is a band-pass of unit */
        a = void_svf1(&fl->sv, fl, f, s, VF_BP) >> 1;      /* peak gain, so those carry k: Q12, at most 4096 x the trim / 4096, about 4096 at RES 0 and 3000 at 127). */
        y = void_svf1(&fl->sv2, fl, f + 2, s, VF_BP) >> 1; /* v1 stays inside the states' +-150000 (a1 f0 + a2 v3 >> 13: at most ~2.5e5), so each product is under */
        return (a * fl->vg1 + y * fl->vg2) >> 11;          /* 1.3e5 x 4096 = 5.1e8 and the sum under 1.1e9: no clamp needed (void_test.c, run under -fsanitize) */
    }
    y = void_svf1(&fl->sv, fl, f, s, fl->ft);
    return fl->ft == VF_HP24 ? void_svf1(&fl->sv, fl, f + 2, y, VF_HP) : y;   /* a second HP, the same coefficients */
}

/* the close-gain; nearly shut, the states drain (0.95 a sample) */
VOID_INLINE int32_t void_close(const void_flt_t *fl, int32_t *f, int32_t y)
{
    if (fl->close < 32767) {
        y = (int32_t)(((int64_t)y * fl->close) >> 15);
        if (fl->close < 1638) {
            uint32_t k;
            for (k = 0; k < 4u; k++)
                f[k] = (f[k] * 31130) >> 15;
        }
    }
    return y;
}

/* one sample s (Q15) through the filter, its state f[4] (the render loop has its own copies of this) */
static inline int32_t void_flt_run(const void_flt_t *fl, int32_t *f, int32_t s)
{
    s = void_front(fl, s, fl->drv != 0);
    return void_close(fl, f, fl->ft == VF_LP24 ? void_ladder(fl, f, s) : void_svf(fl, f, s));
}

/* --------------------------------------------------------------------- DC blocker --- */
/* NOT the plugin's: a deliberate departure. The drive shaper's asymmetry (void_front: + dasym, and the
 * - doff that only approximates its mean) leaves an offset the LP ladder passes straight through, and the
 * part's reverb send is taken ahead of the master's DC blocker (fx.c dc_block), so a WAVE part used to pump
 * that offset into the reverb's combs. One pole per voice, y[n] = x[n] - x[n - 1] + R y[n - 1] with
 * R = 0.9985, in its transposed form: y = x + (w >> SH), w -= (1 - R) y, no x[n - 1] to keep.
 * R = 0.9985 (1 - R = 49) puts the corner at ~10 Hz: DC sits at 0 Hz, so the blocker does its job just as
 * completely there, and C1 (32.7 Hz) loses 0.4 dB instead of the 3.3 dB that R = 0.995 (~35 Hz) cost it --
 * WAVE's BASS category is 10 of its 32 presets (decided 2026-10-17).
 * The state sits SH bits below the signal's Q15 so the subtraction's truncation (half an LSB of w each
 * sample, the floored shift) settles at 0.5 * 2^15 / 49 = 334 units of w -- 0.33 of a Q15 step at SH = 10.
 * SH = 8 would leave 1.3 of one with this R (a settled |y| of 1 .. 2, measured), and holding the state in
 * Q15 would leave about 334: the point of Q(15 + SH) is to keep that error well under one Q15 step, so SH
 * rose with the time constant (it was 8 while 1 - R was 164, where the same sum gives 0.39).
 * Bounds: |x| <= 62224 (the clamp at VOID_OUT_LEVEL) and the peak gain is 2 (the l1 norm of the impulse
 * response, 1 and -(1 - R) R^n, which is 2 for any R), so |y| <= 124448, |w| <= 62224 << 10 = 6.4e7 and
 * y * 49 <= 2^23 -- all comfortably inside int32. VOID_CLIP_MAX does not move with R. 124448 is past what
 * the soft clipper's table covers, so the return is clamped to VOID_CLIP_MAX here rather than at the call
 * site: y is what indexes that table (tests/void_test.c test_dc_range pins both halves of this). */
VOID_INLINE int32_t void_dc_raw(int32_t *w, int32_t x)    /* void_dc without the clamp: void_clip clamps, inside its branch */
{
    int32_t y = x + (*w >> VOID_DC_SH);
    *w -= (y * (32768 - VOID_DC_R)) >> (15 - VOID_DC_SH);
    return y;
}
VOID_INLINE int32_t void_dc(int32_t *w, int32_t x)
{
    int32_t y = x + (*w >> VOID_DC_SH);
    *w -= (y * (32768 - VOID_DC_R)) >> (15 - VOID_DC_SH);
    return clamp(y, -VOID_CLIP_MAX, VOID_CLIP_MAX);    /* the state keeps the unclamped y: the response is */
}                                                      /* linear, the clamp only guards the table index */

/* the soft clipper on the blocker's raw output: above 0.72, thr + o / (1 + o) from VOID_SOFT. The table index must stay in range, which
 * is void_dc's clamp: it is done here, inside the branch (the clamp is the identity at or below VOID_CLIP_THR < VOID_CLIP_MAX, so
 * the common sample does not pay for it); void_clip(y) == the old void_clip'(clamp(y, +-VOID_CLIP_MAX)) for every y (test_dc_range) */
VOID_INLINE int32_t void_clip(int32_t y)
{
    int32_t a = y < 0 ? -y : y;
    if (a > VOID_CLIP_THR) {
        uint32_t o, k;
        if (a > VOID_CLIP_MAX)
            a = VOID_CLIP_MAX;
        o = (uint32_t)(a - VOID_CLIP_THR);
        k = o >> 8;
        a = VOID_CLIP_THR + VOID_AT(VOID_SOFT, k) + (((VOID_AT(VOID_SOFT, k + 1u) - VOID_AT(VOID_SOFT, k)) * (int32_t)(o & 255u)) >> 8);
        y = y < 0 ? -a : a;
    }
    return y;
}

/* ------------------------------------------------------------------------- voice --- */
static void void_note_on(track_t *t, voice_t *v)
{
    void_vc_t *c = void_vc(t, v);
    if (!c->rng)
        c->rng = (int32_t)(0x2545F491u ^ ((uint32_t)(v - t->v) * 0x9E3779B9u) ^ ((uint32_t)(t - trk) << 24));
    v->ph[0] = 0;                                      /* FREE: a new note from phase 0 (a sounding voice */
    if (!v->env) {                                     /* keeps its phases: voice.c) */
        c->f[0] = c->f[1] = c->f[2] = c->f[3] = 0;
        c->ramp = 0;
        c->dcw = 0;
    }
    c->pd = c->pdv = c->wd = c->wdv = 0;               /* the plugin starts its drifts at the note */
    c->est = 1;                                        /* the filter envelope: an attack from 0 (the plugin's */
    c->eu = 0;                                         /* restarts at 0), its peak 1 - vs + vs velocity */
    c->elv = 0;
    {
        int32_t vs = clamp(t->p[P_FM1_LEVEL] - 1, 0, 126) * 260;   /* (LEVEL 1..127: vs 0..1) */
        c->epk = 32767 - vs + ((vs * (int32_t)v->vel) >> 7);
    }
    c->tick = 0;
    c->pen = 0;                                        /* V2's pitch envelope from its start, its peak by velocity */
    c->pepk = 16384 + (int32_t)v->vel * 129;
    if (!c->nrng)
        c->nrng = c->rng ^ 0x6C8E9CF5;
    if (!v->env) {                                     /* the sub from phase 0 with the oscillator, the noise's filter empty */
        c->sph = 0;
        c->nlp = 0;
    }
    {                                                  /* UnisonVoice.cpp, per note: */
        uint32_t u1 = (uint32_t)void_rnd16(c), u2 = (uint32_t)void_rnd16(c);
        int32_t u3 = void_rnd16(c), u4 = void_rnd16(c);
        c->linc = 31165u + ((u1 >> 4) * 747950u >> 12);   /* 0.01 + U 0.24 Hz (Hz * CTL / FS * 2^32) */
        c->lph = u2 << 16;
        c->lpit = 419430 + ((u3 * 8601) >> 8);         /* 0.4 + U 2.1 cents, Q20 */
        c->llev = 164 + ((u4 * 492) >> 16);            /* 0.005 + U 0.015, Q15 */
    }
    /* the plugin's second oscillator drew its micro-LFO here, four more values. Gone with it, but the draws are kept: the
     * walk's random sequence (void_walk) follows them, so dropping them would change the drift, and so the sound, of
     * every preset, including those that never sounded OSC2 (MIX 0) and are otherwise bit for bit as before */
    (void)void_rnd16(c), (void)void_rnd16(c), (void)void_rnd16(c), (void)void_rnd16(c);
}

/* the plugin's per-block random walks (VoiceManager.cpp), every 4th control block (its 128 samples) */
static void void_walk(void_vc_t *c)
{
    c->pdv += ((void_rnd16(c) - 32768) * 1258) >> 16;  /* (U - 0.5) 0.0012 cents */
    c->pdv = mulq15(c->pdv, 32539);                    /* * 0.993 */
    c->pd = clamp(c->pd + c->pdv, -1572864, 1572864);  /* +-1.5 cents */
    c->wdv += ((void_rnd16(c) - 32768) * 524) >> 16;   /* (U - 0.5) 0.0005 */
    c->wdv = mulq15(c->wdv, 32637);                    /* * 0.996 */
    c->wd = clamp(c->wd + c->wdv, -57672, 57672);      /* +-0.055 */
}

/* a block of one voice, the inner loop specialised by its filter (lad: the ladder, else the SVFs) and drive */
typedef struct {
    void_osc_t o1;
    void_flt_t fl;
    int32_t c1;                  /* the oscillator's weight: micro-LFO level, table -> Q15; Q14 */
    uint32_t inc1;
    uint32_t sinc;               /* V2: the sub's phase increment */
    int32_t sk, nk, na;          /* the sub's and the noise's levels (Q14, VOID_XG), the noise's one-pole coefficient (Q15) */
} void_blk_t;

/* the loop of a block, specialised at each call by its filter (lad: the ladder, else the SVFs) and drive */
VOID_INLINE void void_block(const void_blk_t *b, voice_t *v, void_vc_t *c, int32_t *out, uint32_t n, const vmod_t *m,
                            const int lad, const int drv, const int ext, const int slow)
{
    /* everything the loop reads, local: out[] is written through a pointer, so the compiler would read each of
     * these again after every sample */
    const void_osc_t o1 = b->o1;
    const void_flt_t fl = b->fl;
    const int32_t c1 = b->c1;
    const uint32_t inc1 = b->inc1;
    uint32_t i, ph1 = v->ph[0];
    int32_t ramp = c->ramp, dcw = c->dcw, f[4] = {c->f[0], c->f[1], c->f[2], c->f[3]};
    uint32_t sph = c->sph, sinc = b->sinc;             /* V2's sub and noise (ext: either is on, a constant where inlined) */
    int32_t sk = b->sk, nk = b->nk, na = b->na, nlp = c->nlp, nrng = c->nrng;
    if (lad) {             /* the ladder's stages are tanh1 outputs mixed in, |f| <= 32767 while it runs. After a TYPE switch mid-note the state is an
                            * SVF's (up to 150000) and void_tanh1 would index VOID_TANH1 past its end: bound it, once a block (a no-op for a ladder
                            * that ran, so every render is as before) */
        f[0] = clamp(f[0], -32767, 32767);
        f[1] = clamp(f[1], -32767, 32767);
        f[2] = clamp(f[2], -32767, 32767);
        f[3] = clamp(f[3], -32767, 32767);
    }
    int32_t amp = m->amp0 << CTL_LOG2, damp = m->amp1 - m->amp0;   /* voice_amp's ramp, stepped */
    for (i = 0; i < n; i++) {
        int32_t s = void_osc(&o1, ph1, slow) * c1, y;
        s >>= 14;                                      /* Q15, 1.0 = the plugin's */
        ph1 += inc1;
        if (ext) {                                     /* + the sub (a sine) and the noise (white through the one-pole) */
            int32_t w = (int32_t)noise32(&nrng) >> 16;
            nlp += ((w - nlp) * na) >> 15;
            s += ((sine_i(sph) * sk) >> 14) + ((nlp * nk) >> 14);
            sph += sinc;
        }
        if (slow && ramp < 32767) {
            ramp += VOID_RAMP_STEP;
            s = mulq15(s, ramp > 32767 ? 32767 : ramp);
        }
        s = void_front(&fl, s, drv);
        y = lad ? void_ladder(&fl, f, s) : void_svf(&fl, f, s);
        if (slow)
            y = void_close(&fl, f, y);
        /* the voice at the plugin's level, the DC blocker (void_dc: not the plugin's), then its soft clipper:
         * above 0.72, thr + o / (1 + o). The blocker comes first so the clipper keeps its two sides symmetric
         * about 0, as the plugin's does. The plugin's level is ~6 dB below Felucca's other engines, made up
         * after the clipper (which stays at the plugin's level) */
        y = void_clip(void_dc_raw(&dcw, mulq15(clamp(y, -110000, 110000), VOID_OUT_LEVEL)));
        out[i] += mulq15(mulq15(y, amp >> CTL_LOG2), VOICE_FS) << 2;   /* voice_amp; +6 dB after the clipper: */
        amp += damp;                                   /* Felucca's engines' level */
    }
    v->ph[0] = ph1;
    c->ramp = ramp;
    c->dcw = dcw;
    if (ext) {
        c->sph = sph;
        c->nlp = nlp;
        c->nrng = nrng;
    }
    c->f[0] = f[0];
    c->f[1] = f[1];
    c->f[2] = f[2];
    c->f[3] = f[3];
}

/* a block that fades in (the first 5 ms of a note), closes (CUT at 20 Hz) or has a WARP: the same loop with every test in it, one
 * copy, lad / drv at run time, out of line. The four fast loops carry neither: void_close's drain, which the host
 * compiler if-converts, cost ~29 instructions a sample in each of them whether it ran or not (it never runs in the
 * factory sounds), the ramp ~8 more, for the first 7 blocks of a note */
static __attribute__((noinline)) void void_block_slow(const void_blk_t *b, voice_t *v, void_vc_t *c, int32_t *out,
                                                      uint32_t n, const vmod_t *m)
{
    void_block(b, v, c, out, n, m, b->fl.ft == VF_LP24, b->fl.drv != 0, (b->sk | b->nk) != 0, 1);
}

static void void_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    void_vc_t *c = void_vc(t, v);
    void_blk_t b;
    int32_t cutk = clamp(p[P_E4], 0, 127), pos, l1, g1, mc;
    if (n > CTL)
        n = CTL;
    if ((c->tick++ & 3u) == 0u)
        void_walk(c);
    c->lph += c->linc;
    l1 = sine_i(c->lph);
    g1 = 32767 + mulq15(l1, c->llev);                  /* micro-LFO level, Q15 around 1 */
    {
        uint32_t inc = m->inc;
        int32_t pe = clamp(p[P_E3], -24, 24);
        if (pe && c->pen < VOID_PENV_A + VOID_PENV_D) {   /* V2's pitch envelope, at the block's middle */
            uint32_t k = c->pen + (n >> 1);
            int32_t u, e;
            if (k < VOID_PENV_A) {
                u = (int32_t)(k * (32767u / VOID_PENV_A));
                e = mulq15(u, u);                      /* up: u^2 */
            } else {
                k -= VOID_PENV_A;
                u = 32767 - (int32_t)(((k < VOID_PENV_D ? k : VOID_PENV_D) * 15217u) >> 10);
                e = u > 0 ? mulq15(u, u) : 0;          /* down: (1 - t)^2 (15217 / 1024 = 32767 / 2205) */
            }
            e = mulq15(e, c->pepk);
            inc = cents_inc(m->pitch16 + ((e * pe * 16) >> 15), 0, m->fine);
        }
        if (c->pen < VOID_PENV_A + VOID_PENV_D)
            c->pen += n;
        b.inc1 = void_detune(inc, c->pd + ((l1 * (c->lpit >> 8)) >> 7));   /* pitch: the drift and the micro-LFO, in cents */
    }
    b.sk = clamp(p[P_E6], 0, 127) * VOID_XG;           /* V2's sub and noise */
    b.nk = clamp(p[P_FM2_ATK], 0, 127) * VOID_XG;
    b.na = VOID_NCOL[clamp(p[P_FM2_SUS], 0, 127)];
    b.sinc = m->inc >> (p[P_FM2_DEC] >= 64 ? 2 : 1);  /* one octave down, or two */
    /* position: the knob, SHAPE (127 steps = the table), the drift */
    pos = p[P_E1] * 255 * 256 / 127 + (m->shape - (64 << 8)) * 255 / 127 + ((c->wd * 255) >> 12);
    pos = clamp(pos, 0, 255 << 8);
    void_osc_warp(&b.o1, (uint32_t)clamp(p[P_E0], 0, VOID_NTBL - 1), pos, b.inc1, p[P_E2]);
    /* the weight: the micro-LFO level, table -> Q15 (Q14). The SQUARE's pulse is twice the table's peak: its reading
     * is halved (void_osc) and its weight doubled */
    b.c1 = (int32_t)((((int64_t)g1 * 256) * (VOID_WT_NORM >> 1)) >> 23);
    if (b.o1.pw)                                       /* the pulse (doubled: its reading is halved) at V2's level of its width */
        b.c1 = (int32_t)(((int64_t)b.c1 * VOID_SQ_GAIN[(uint32_t)pos >> 8]) >> 14);
    if (p[P_FM3_ATK] && p[P_FM3_ATK] != 64)            /* OLVL: the preset's oscillator level, 64 = the plugin's 0.8 */
        b.c1 = (b.c1 * clamp(p[P_FM3_ATK], 1, 127)) >> 6;
    /* the filter: CUT on the plugin's knob law, plus ENV / LFO / matrix (m->cutoff: cutoff steps << 8). With FLT ENV
     * on, ENV DEST FLT's share of m->cutoff (the amp ADSR's) is the filter envelope's instead, the plugin's way */
    mc = m->cutoff;
    if (p[P_FM1_LEVEL]) {                              /* V2: env * depth * 4 octaves (VoiceManager.cpp env1Octaves; 63 = depth 1) */
        int32_t e1 = void_env1(c, p, v->gate);
        mc -= (m->envq15 * p[P_ED_FLT]) >> 7;
        mc += (e1 * p[P_ED_FLT] * VOID_FENV_K) >> 15;  /* cutoff steps Q8 (|e1 ED K| < 32767 * 64 * 229 = 4.8e8) */
    }
    void_flt_set(&b.fl, (uint32_t)p[P_FTYPE] % 7u, clamp((int32_t)VOID_CUT_Q8[cutk] + mc, 0, 127 << 8),
                 clamp(p[P_E5], 0, 127), clamp(p[P_E7], 0, 127), !cutk && mc <= 0);
    if (VOID_FORCE_SLOW || c->ramp < 32767 || b.fl.close < 32767 || b.o1.kp) {   /* (a WARP is read by the slow loop) */
        void_block_slow(&b, v, c, out, n, m);
    } else if (b.sk | b.nk) {                          /* V2's sub / noise on: the loops with them */
        if (b.fl.ft == VF_LP24) {
            if (b.fl.drv)
                void_block(&b, v, c, out, n, m, 1, 1, 1, 0);
            else
                void_block(&b, v, c, out, n, m, 1, 0, 1, 0);
        } else if (b.fl.drv) {
            void_block(&b, v, c, out, n, m, 0, 1, 1, 0);
        } else {
            void_block(&b, v, c, out, n, m, 0, 0, 1, 0);
        }
    } else if (b.fl.ft == VF_LP24) {
        if (b.fl.drv)
            void_block(&b, v, c, out, n, m, 1, 1, 0, 0);
        else
            void_block(&b, v, c, out, n, m, 1, 0, 0, 0);
    } else if (b.fl.drv) {
        void_block(&b, v, c, out, n, m, 0, 1, 0, 0);
    } else {
        void_block(&b, v, c, out, n, m, 0, 0, 0, 0);
    }
}

/* ----------------------------------------------------------------------- presets --- */
/* the factory sounds: presets/four/wave.py (tools/four_presets.py: WAVE_PRESETS in four_presets.h, category.c).
 * WAVE_PX holds what a preset_t cannot (core.h void_px_t), applied by void_track_loaded */
_Static_assert(NELEM(WAVE_PX) == NELEM(WAVE_PRESETS), "WAVE_PX: one per preset");

/* a preset just loaded on track t (ui.c apply_preset_to, after the defaults and the preset's own values) */
static void void_track_loaded(track_t *t)
{
    const void_px_t *x;
    uint32_t k;
    if (t->eng_req % NENGINES != ENGI_WAVE)
        return;
    if (t->p[P_E2] < VOID_WARP_MIN)                    /* a preset that holds an old TBL2 (0..8) there: WARP off */
        t->p[P_E2] = VOID_WARP_OFF;
    if (t->preset >= NELEM(WAVE_PX))
        return;
    x = &WAVE_PX[t->preset];
    t->p[P_FTYPE] = x->ftype;
    if (x->voice == V_UNISON)
        t->p[P_VOICE] = V_UNISON;
    if (x->detune)
        t->p[P_DETUNE] = (int16_t)(x->detune - 1u);
    t->p[P_SPRD] = x->spread;
    t->p[P_GLIDE] = x->glide;
    if (x->lfo[0]) {
        t->p[P_LRATE] = x->lfo[1];
        t->p[P_LWAVE] = x->lfo[2];
        t->p[P_LTRIG] = x->lfo[3];
        t->p[P_LFADE] = x->lfo[4];
    }
    t->p[P_FM1_LEVEL] = x->fe[0];
    t->p[P_FM1_ATK] = x->fe[1];
    t->p[P_FM1_DEC] = x->fe[2];
    t->p[P_FM1_SUS] = x->fe[3];
    t->p[P_FM1_REL] = x->fe[4];
    t->p[P_LD_FLT] = x->ldflt;
    t->p[P_LSYNC] = x->lsync;
    t->p[P_FM2_ATK] = x->nz[0];                        /* V2: NOISE, its COLOR, the sub's octave (>= 64: two down), OLVL */
    t->p[P_FM2_SUS] = x->nz[1];
    t->p[P_FM2_DEC] = x->nz[2];
    t->p[P_FM3_ATK] = x->olvl;
    for (k = 0; k < 4u; k++) {                         /* (mod.c's NMSLOT slots) */
        t->p[P_M1SRC + 3u * k] = x->mx[k][0];
        t->p[P_M1DST + 3u * k] = x->mx[k][1];
        t->p[P_M1AMT + 3u * k] = x->mx[k][2];
    }
}

static const engine_t ENG_WAVE = {
    .name = "WAVE",
    .page_title = {"OSC", "FILTER"},
    .edit = {
        {"TBL1", F_ENUM, 0, VOID_NTBL - 1, 1, N_VOID_TBL, 0},
        {"POS", F_PCT, 0, 127, 0, 0, 0},
        {"WARP", F_INT, VOID_WARP_MIN, 127, VOID_WARP_OFF, 0, 0},   /* E2: was TBL2 (hidden by Task 4.4); 64 = off */
        {"PENV", F_INT, -24, 24, 0, 0, 0},          /* E3 (was MIX, hidden by Task 4.4): V2's pitch envelope, semitones */
        {"CUT", F_INT, 0, 127, 100, 0, 0},
        {"RES", F_PCT, 0, 127, 0, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},            /* E6 (was OSC2, hidden by Task 4.4): V2's sub oscillator */
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = WAVE_PRESETS,
    .npresets = NELEM(WAVE_PRESETS),
    .knob = {P_E4, P_E5, P_ED_FLT, P_E1},
    .poly = VOID_POLY,
    .note_on = void_note_on,
    .render = void_render,
    .mem = VOID_MEM,
};
