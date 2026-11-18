/* SPDX-License-Identifier: GPL-3.0-only
 * MICRO: after the Waldorf Microwave I (the plan 2026-11-17-eight-machines.md §7; V2 Phase 4). Written for Switch Blade.
 *
 * What makes it the Microwave and not WAVE: WAVE is smooth (interpolated frames, a mip-map); the Microwave is stepped and
 * gritty, and so is this:
 *   - wavetables of 64 waves, switched with NO crossfade: the position steps from wave to wave at the control rate (once a
 *     block), the PPG's stepped sweep. The tables are our own PPG-style ones (tools/gen_mw_tables.py: 16 tables of 8
 *     keyframes, 64 waves between them), 8-bit as the MW's; PPG's and Waldorf's own tables are their data and not used;
 *   - a fixed-step playback: 128 samples a cycle read with no interpolation between samples, so the top aliases as the
 *     MW's does;
 *   - GRIT, the MW's roughness on purpose: the bit depth from 8 down to 3, and above half a sample-and-hold of up to 4;
 *   - two oscillators on the same wave, the second detuned (OSC2, cents; 0 = one oscillator);
 *   - a 24 dB low-pass (two TPT SVF stages, resonance on the first, a soft knee after: brighter and cleaner than JUNO's
 *     ladder, as the plan asks) with key tracking.
 * The MW's sweep: POS the start wave, EPOS (the machine knob) the envelope's reach over the table (voice.c's ADSR, bipolar),
 * and the LFO / matrix on SHAPE move it too. No reference: Xenia (gearmulator) emulates the Microwave II/XT and needs its ROM
 * (the user's, still open); tests/micro_test.c checks the behaviour. Voice state in voice_t (no engine memory). */
#include "mw_tables.h"

#define MW_WAVES 64                      /* waves a table, between its 8 keyframes */
#define MW_DC 32674                      /* the output's DC blocker (~20 Hz): a pulse or a bit pattern sits off zero */

static void micro_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[0] = v->s[1] = v->s[4] = v->s[5] = v->s[6] = v->s[7] = 0;   /* the SVFs and the DC blocker (kept: .keep) */
    v->ph[2] = 0;                        /* GRIT's hold: its count and the held sample */
}

static void micro_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const int8_t *k0, *k1;
    int32_t pos, w, kf, fr, grit = clamp(p[P_E6], 0, 127), sh, hold, cnt, held, cut, kres;
    int32_t a1 = v->s[0], a2 = v->s[1], b1 = v->s[4], b2 = v->s[5], dcx = v->s[6], dcy = v->s[7];
    uint32_t i, ph = v->ph[0], ph2 = v->ph[1], inc = m->inc, inc2 = 0, tb = (uint32_t)clamp(p[P_E0], 0, MW_NT - 1);
    int two = p[P_E3] > 0;
    tsvf_t ca, cb;
    /* the wave: POS + EPOS x the envelope + SHAPE (LFO, matrix), stepped to one of 64 (no crossfade) */
    pos = (clamp(p[P_E1], 0, 127) << 8) + ((m->envq15 * clamp(p[P_E2], -64, 63)) >> 6) + (m->shape - (64 << 8));
    w = (clamp(pos, 0, 127 << 8) * (MW_WAVES - 1) + (127 << 7)) / (127 << 8);   /* 0..63 */
    kf = w * (MW_NK - 1);                                                     /* the keyframes around it, x 63 */
    k0 = MW_WT[tb][kf / (MW_WAVES - 1)];
    k1 = MW_WT[tb][kf / (MW_WAVES - 1) + 1 < MW_NK ? kf / (MW_WAVES - 1) + 1 : MW_NK - 1];
    fr = (kf % (MW_WAVES - 1)) * 256 / (MW_WAVES - 1);
    if (two) {
        inc2 = inc + (uint32_t)((int32_t)(inc >> 12) * (p[P_E3] * 237 / 100));   /* OSC2: cents (voice.c's 1/4096 ratio) */
    }
    sh = (grit * 5) / 127;                                                    /* GRIT: 0..5 bits off the 8 */
    hold = grit > 64 ? 1 + ((grit - 64) * 3) / 63 : 1;                        /* .. and above half a hold of 1..4 */
    cnt = (int32_t)(v->ph[2] & 0xFFFFu);
    held = (int16_t)(v->ph[2] >> 16);
    cut = (p[P_E4] << 8) + m->cutoff + (((m->pitch16 - 60 * 16) * clamp(p[P_E7], 0, 127)) >> 3);   /* KTR, C4 the zero */
    kres = 8192 - clamp(p[P_E5], 0, 127) * 7600 / 127;
    tsvf_coef_k(&ca, cut, kres);
    tsvf_coef_k(&cb, cut, 5793);
    for (i = 0; i < n; i++) {
        uint32_t x = ph >> 25;
        int32_t s = k0[x] + (((k1[x] - k0[x]) * fr) >> 8), y;
        ph += inc;
        if (two) {
            uint32_t x2 = ph2 >> 25;
            s = (s + k0[x2] + (((k1[x2] - k0[x2]) * fr) >> 8)) >> 1;
            ph2 += inc2;
        }
        if (sh)
            s = (s >> sh) << sh;                                              /* the bits */
        if (++cnt >= hold) {                                                  /* the hold */
            cnt = 0;
            held = s;
        }
        y = held << 8;                                                        /* 8 bits -> Q15, full scale (the 24 dB filter takes its share) */
        y = tsvf_lp(&ca, y, &a1, &a2);
        y = tsvf_lp(&cb, y, &b1, &b2);
        y = soft_knee(y, 28000);
        {
            int32_t z = y - dcx + ((dcy * MW_DC) >> 15);
            dcx = y;
            dcy = z;
            y = z;
        }
        out[i] += voice_amp(y, m, i) << 1;                                    /* (as VOX and ORGAN: the filtered tables sit low) */
    }
    v->ph[0] = ph;
    v->ph[1] = ph2;
    v->ph[2] = (uint32_t)(cnt & 0xFFFF) | (uint32_t)(uint16_t)(int16_t)held << 16;
    v->s[0] = a1, v->s[1] = a2, v->s[4] = b1, v->s[5] = b2, v->s[6] = dcx, v->s[7] = dcy;
}

static const engine_t ENG_MICRO = {
    .name = "MICRO", .page_title = {"WAVE", "FLT"},
    .edit = {
        {"WT", F_ENUM, 0, MW_NT - 1, 0, MW_TNAME, 0},   {"POS", F_PCT, 0, 127, 0, 0, 0},
        {"EPOS", F_BIPCT, -64, 63, 32, 0, 0},           {"OSC2", F_INT, 0, 127, 0, 0, "ct"},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},            {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"GRIT", F_PCT, 0, 127, 32, 0, 0},              {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = MICRO_PRESETS, .npresets = NELEM(MICRO_PRESETS),   /* presets/four/micro.py */
    .knob = {P_E4, P_E5, P_ED_FLT, P_E2},
    .keep = 0xF3u,                       /* both SVFs and the DC blocker on a retrigger (s0 s1 s4 s5 s6 s7) */
    .note_on = micro_note_on, .render = micro_render,
};
