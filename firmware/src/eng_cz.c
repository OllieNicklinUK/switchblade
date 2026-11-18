/* SPDX-License-Identifier: GPL-3.0-only
 * CZ-Z: Casio CZ phase distortion (the plan 2026-11-17-eight-machines.md §5; V2 Phase 1).
 *
 * The voice is the CZ's sound chip, the NEC/Casio uPD933, after MAME's src/devices/sound/upd933.cpp by Devin Acker
 * (BSD-3-Clause, LICENSES/BSD-3-MAME-upd933.txt): cz_wave below is its upd933_device::update() waveform path line
 * for line in the same integer arithmetic (the transfer functions that bend the phase by the DCW, the window
 * functions of the resonance waves, the cosine, the 13-bit level, the ring modulation), and cz_dcw_limit its pitch
 * limit on the DCW. tests/cz_ref.cpp builds MAME's file on the host and holds cz_wave to it sample for sample, over
 * every waveform and window, pitches, DCW sweeps and levels.
 *
 * What is Switch Blade's own, not the chip's (the CZ's firmware, not its chip, did these, and that firmware is
 * Casio's; none of it is in MAME's file):
 *   - the 8-step DCW envelope (cz_env8): steps of rate and level, a sustain step and an end step, as the CZ panel
 *     has them; the rate curve (tools/gen_cz_tables.py CZ_RATE) is ours, tuned by ear. It is stored in the part's
 *     retired FM1..FM4 operator slots (CZ_ENV), so user presets and projects keep it;
 *   - the amplitude: voice.c's ADSR (the CZ's 8-step DCA is not ported); the pitch: Switch Blade's (glide, bend,
 *     the LFO, the matrix) as every machine's, at 44.1 kHz (the chip runs at 40 kHz on a CZ-101: only the pitch
 *     table differs, and pitch_inc is ours); no 8-step DCO (pitch) envelope;
 *   - the two lines of a note in the CZ's four modes (LINE 1, 2, 1+1', 1+2'), line 2 detuned in cents (DTN) and by
 *     octaves (the preset's oct2), RING (line 2 times line 1) and NOISE (line 2's pitch noise-modulated, as the
 *     chip's pitch modulation register does: 0 or 32 semitones up, latched every 8 samples).
 *
 * Contract: E4 CUT = the DCW (with the filter modulation: ENV, the LFO, the matrix), E5 KEYF = DCW key follow (higher
 * notes darker), E7 DCWE = the 8-step DCW envelope's depth (the machine knob), E0 / E1 the two lines' waveforms
 * (the CZ's 8: SAW SQR PULSE DOUBLE SINE SAW-PULSE RESO 1..3), E2 LINE, E3 DTN, E6 MOD (OFF RING NOISE).
 * No engine memory: a voice's state is its ph[] and s[]. */
#include "cz_core.h"                  /* the chip: cz_wave, cz_dcw_limit, CZ_WV (and cz_tables.h) */

/* ---- the 8-step DCW envelope (Switch Blade's, after the CZ panel) in the part's FM1..FM4 slots ---- */
#define CZ_ENV P_FM1_ATK                 /* + 0..7 rates, 8..15 levels (0..99), 16 SUS step (8 none), 17 END, 18 oct2 + 2 */
#define CZ_ENV_OK (CZ_ENV + 19)          /* 1: the slots hold a CZ envelope (cz_track_loaded); else the default below */
typedef char cz_env_fits[CZ_ENV_OK == P_FM4_LEVEL ? 1 : -1];
static const uint8_t CZ_ENV_DEF[18] = {92, 55, 45, 0, 0, 0, 0, 0,   99, 60, 0, 0, 0, 0, 0, 0,   1, 2};

static void cz_env8(const track_t *t, voice_t *v, uint32_t n)
{
    const int16_t *p = &t->p[CZ_ENV];
    int ok = t->p[CZ_ENV_OK] == 1;
    uint32_t step = (uint32_t)v->s[1], sus, end, r, l;
    int32_t cur = v->s[0], target;
    sus = ok ? (uint32_t)clamp(p[16], 0, 8) : CZ_ENV_DEF[16];
    end = ok ? (uint32_t)clamp(p[17], 0, 7) : CZ_ENV_DEF[17];
    if (!v->gate && sus < 8u && step <= sus)
        step = sus + 1u;                 /* released: on from the step after SUS */
    if (step <= end) {
        r = ok ? (uint32_t)clamp(p[step], 0, 99) : CZ_ENV_DEF[step];
        l = ok ? (uint32_t)clamp(p[8u + step], 0, 99) : CZ_ENV_DEF[8u + step];
        target = (int32_t)(l << 16);
        {
            int32_t inc = (int32_t)(CZ_RATE[r] * n);
            cur = cur < target ? (target - cur <= inc ? target : cur + inc) : (cur - target <= inc ? target : cur - inc);
        }
        if (cur == target && !(step == sus && v->gate))
            step++;                      /* (at SUS while held: stays) */
    }
    v->s[0] = cur;
    v->s[1] = (int32_t)step;
}

/* ---- the voice ---- */
/* the output's DC blocker, a one-pole high-pass at ~20 Hz (1 - 2 pi 20 / 44100, Q15): the chip's resonance windows and
 * pulses sit off zero (MAME's output has the offset too; a CZ's analogue stage after the chip blocks it) */
#define CZ_DC 32674
static void cz_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = 0;             /* the CZ resets the phase at a note (voice.c keeps it on a retrigger) */
    v->s[0] = 0;                         /* the DCW envelope's level (kept on a retrigger: .keep) */
    v->s[1] = 0;                         /* .. its step */
    v->s[4] = -1;                        /* the DCW of the block before: none yet */
    v->s[5] = 0;                         /* samples, for NOISE's latch */
    v->s[6] = v->s[7] = 0;               /* the DC blocker (kept on a retrigger: .keep) */
    v->s[3] = 0x2545F491;                /* NOISE's generator, the same seed every note (any non-zero one) */
}

static void cz_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t mode = (uint32_t)clamp(p[P_E2], 0, 3), md = (uint32_t)clamp(p[P_E6], 0, 2), i;
    uint32_t wa = CZ_WV[clamp(p[P_E0], 0, 7)], wb = CZ_WV[clamp(mode == 2u ? p[P_E0] : p[P_E1], 0, 7)];
    int32_t oct2 = p[CZ_ENV_OK] == 1 ? clamp(p[CZ_ENV + 18], 0, 4) - 2 : 0;
    int32_t f = m->fine + p[P_E3] * 237 / 100;   /* DTN: cents as voice.c's 1/4096 ratio (~2.37 a cent) */
    uint32_t ib = pitch_inc(clamp(m->pitch16 + oct2 * 192, 0, 2047)), sa = m->inc >> 1, sb, sbn, la, lb, lbn;
    int32_t dcw, prev, acc, dstep, add, key;
    uint32_t pa = v->ph[0], pb = v->ph[1], cnt = (uint32_t)v->s[5], rnd = (uint32_t)v->s[3];
    int32_t x1 = v->s[6], y1 = v->s[7];  /* the DC blocker (CZ_DC) */
    ib += (uint32_t)((int32_t)(ib >> 12) * f);
    sb = ib >> 1;
    sbn = md == 2u ? (pitch_inc(clamp(m->pitch16 + oct2 * 192 + 32 * 16, 0, 2047)) >> 1) : sb;   /* NOISE: 32 up */
    if (mode == 1u) {                    /* LINE 2 alone: line 2's waveform, pitch, DTN and phase */
        wa = wb;
        sa = sb;
        pa = v->ph[1];
    }
    la = cz_dcw_limit(sa);
    lb = cz_dcw_limit(sb);
    lbn = cz_dcw_limit(sbn);
    cz_env8(t, v, n);
    add = (((v->s[0] >> 8) * p[P_E7]) * 41) >> 17;           /* DCWE: the envelope (level 0..99) to 0 .. ~1000 */
    key = m->pitch16 > 36 * 16 ? ((m->pitch16 - 36 * 16) * p[P_E5]) >> 8 : 0;   /* KEYF: darker above C2 */
    dcw = clamp(((p[P_E4] << 8) + m->cutoff) >> 5, 0, CZ_DCW_MAX) + add - key;
    dcw = clamp(dcw, 0, CZ_DCW_MAX);
    prev = v->s[4] < 0 ? dcw : v->s[4];
    acc = prev << 8;
    dstep = ((dcw - prev) << 8) / (int32_t)(n ? n : 1u);       /* (linear over the block: no zipper) */
    if (mode < 2u) {                     /* one line */
        for (i = 0; i < n; i++) {
            uint32_t d = (uint32_t)(acc >> 8);
            int32_t a = cz_wave(pa, wa, d < la ? d : la, CZ_VOL);
            acc += dstep;
            pa += sa;
            a <<= 3;                     /* (13 bits -> Q15) */
            y1 = a - x1 + ((y1 * CZ_DC) >> 15);
            x1 = a;
            out[i] += voice_amp(y1, m, i);
        }
        if (mode == 1u)
            pb = pa, pa = v->ph[0];      /* (line 2's phase is line 2's) */
    } else {                             /* 1+1', 1+2': two lines, the second maybe ring- or noise-modulated */
        uint32_t s2 = sb, l2 = lb;
        for (i = 0; i < n; i++) {
            uint32_t d = (uint32_t)(acc >> 8);
            int32_t a = cz_wave(pa, wa, d < la ? d : la, CZ_VOL), b;
            if (md == 2u && !(cnt++ & 7u)) {   /* NOISE: a new pitch every 8 samples */
                rnd ^= rnd << 13;
                rnd ^= rnd >> 17;
                rnd ^= rnd << 5;
                s2 = (rnd & 0x8000u) ? sbn : sb;
                l2 = (rnd & 0x8000u) ? lbn : lb;
            }
            b = cz_wave(pb, wb, d < l2 ? d : l2, CZ_VOL);
            if (md == 1u)
                b = b * a / 0x1000;      /* RING: as the chip's (m_last_sample: line 1's) */
            acc += dstep;
            pa += sa;
            pb += s2;
            a = (a + b) << 2;
            y1 = a - x1 + ((y1 * CZ_DC) >> 15);
            x1 = a;
            out[i] += voice_amp(y1, m, i);
        }
    }
    v->ph[0] = pa;
    v->ph[1] = pb;
    v->s[3] = (int32_t)rnd;
    v->s[4] = dcw;
    v->s[5] = (int32_t)cnt;
    v->s[6] = x1;
    v->s[7] = y1;
}

/* a preset just loaded (ui.c apply_preset_to, hostsim host_preset_values): its 8-step DCW envelope and line 2's
 * octave (CZ_PX, tools/four_presets.py) into the part's slots, so a user preset or a project keeps them */
static void cz_track_loaded(track_t *t)
{
    uint32_t k;
    if (t->eng_req % NENGINES != ENGI_CZZ || t->preset >= NELEM(CZ_PX))
        return;
    for (k = 0; k < 19u; k++)
        t->p[CZ_ENV + k] = CZ_PX[t->preset][k];
    t->p[CZ_ENV_OK] = 1;
}

static const char *const N_CZ_WAVE[] = {"SAW", "SQR", "PULS", "DSIN", "SAWP", "RES1", "RES2", "RES3"};
static const char *const N_CZ_LINE[] = {"1", "2", "1+1'", "1+2'"};
static const char *const N_CZ_MOD[] = {"OFF", "RING", "NOIS"};

static const engine_t ENG_CZZ = {
    .name = "CZ-Z", .page_title = {"OSC", "DCW"},
    .edit = {
        {"WAV1", F_ENUM, 0, 7, 0, N_CZ_WAVE, 0},     {"WAV2", F_ENUM, 0, 7, 0, N_CZ_WAVE, 0},
        {"LINE", F_ENUM, 0, 3, 0, N_CZ_LINE, 0},     {"DTN", F_INT, 0, 127, 0, 0, "ct"},
        {"CUT", F_CUTOFF, 0, 127, 64, 0, 0},        {"KEYF", F_PCT, 0, 127, 30, 0, 0},
        {"MOD", F_ENUM, 0, 2, 0, N_CZ_MOD, 0},      {"DCWE", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = CZZ_PRESETS, .npresets = NELEM(CZZ_PRESETS),   /* presets/four/czz.py */
    .knob = {P_E4, P_E5, P_ED_FLT, P_E7},
    .keep = 0xC1u,                       /* the DCW envelope's level and the DC blocker on a retrigger */
    .note_on = cz_note_on, .render = cz_render,
};
