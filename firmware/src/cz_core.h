/* SPDX-License-Identifier: GPL-3.0-only
 * CZ-Z (eng_cz.c): the uPD933 core, after MAME's src/devices/sound/upd933.cpp by Devin Acker (BSD-3-Clause,
 * LICENSES/BSD-3-MAME-upd933.txt). Its own header so tests/cz_ref.cpp (C++) holds exactly this code to MAME's. */
#ifndef CZ_CORE_H
#define CZ_CORE_H
#include <stdint.h>
#include "cz_tables.h"

#define CZ_VOL 8192                      /* MAME m_volume[0x1ff]: pow(2 << 12, 1), the full level */
#define CZ_DCW_MAX 1016                  /* the chip's DCW envelope top: 127 << 19 >> 16 */
#define CZ_PITCH_SHIFT 20u

/* the CZ's 8 waveforms as the chip's (transfer function, window): RESO 1..3 are the resonance transfer with the
 * saw, triangle and trapezoid windows. Packed: bits 0-2 the first half-cycle's transfer, 4-6 the second's (the
 * chip alternates two per cycle: position bit 31), 8-10 the window */
static const uint16_t CZ_WV[8] = {0x000, 0x011, 0x022, 0x044, 0x055, 0x166, 0x266, 0x366};

/* MAME upd933_device::update(): one sample of a line at position (pos: bits 20..30, the waveform half: bit 31), its
 * DCW (already limited) and level vol (m_volume[]); -vol / 2 .. vol / 2 */
static inline int32_t cz_wave(uint32_t position, uint32_t wv, uint32_t dcw, int32_t vol)
{
    uint32_t pos = (position >> CZ_PITCH_SHIFT) & 0x7ffu;
    uint32_t w = (position >> 31) ? (wv >> 4) & 7u : wv & 7u;
    uint32_t pivot = 0x400u - dcw, phase = 0, window = 0;
    int32_t sample;
    switch (w) {
    case 0:                              /* sawtooth: rises over [0, pivot), falls over [pivot, 800) */
        phase = pos < pivot ? pos * 0x400u / pivot : 0x400u + (pos - pivot) * 0x400u / (0x800u - pivot);
        break;
    case 1:                              /* square: rises over [0, pivot), high to 400, then the same inverted */
        phase = ((pos & 0x3ffu) < pivot ? (pos & 0x3ffu) * 0x400u / pivot : 0x3ffu) | (pos & 0x400u);
        break;
    case 2:                              /* pulse: rises and falls over [0, pivot * 2), then low */
        phase = pos < pivot * 2u ? pos * 0x800u / (pivot * 2u) : 0x7ffu;
        break;
    case 3:                              /* silent (undocumented) */
        break;
    case 4:                              /* double sine: rises and falls over [0, pivot), then over [pivot, 800) */
        phase = pos < pivot ? pos * 0x800u / pivot : (pos - pivot) * 0x800u / (0x800u - pivot);
        break;
    case 5:                              /* saw pulse: rises over [0, 400), falls over [400, 400 + pivot), then low */
        phase = pos < 0x400u ? pos : pos < pivot + 0x400u ? 0x400u + (pos & 0x3ffu) * 0x400u / pivot : 0x7ffu;
        break;
    case 6:                              /* resonance: the frequency times the DCW, hard-synced to the fundamental */
        phase = (pos + ((pos * dcw) >> 6)) & 0x7ffu;
        break;
    default:                             /* double pulse (undocumented) */
        phase = (pos & 0x3ffu) < pivot ? (pos & 0x3ffu) * 0x400u / pivot : 0x7ffu;
        break;
    }
    switch ((wv >> 8) & 7u) {
    case 0:
        break;
    case 1:                              /* sawtooth: falls over [0, 800) */
        window = pos;
        break;
    case 2:                              /* triangle */
        window = (pos & 0x3ffu) * 2u;
        if (pos < 0x400u)
            window ^= 0x7feu;
        break;
    case 3:                              /* trapezoid: falls over [400, 800) */
        if (pos >= 0x400u)
            window = (pos & 0x3ffu) * 2u;
        break;
    case 4:                              /* pulse (undocumented) */
        window = pos < 0x400u ? pos * 2u : 0x7ffu;
        break;
    default:                             /* double saw (undocumented) */
        window = (0x3ffu ^ (pos & 0x3ffu)) * 2u;
        break;
    }
    sample = CZ_COS[phase];
    if (window)
        sample = sample * (int32_t)(0x800u - window) / 0x800;
    sample = (sample * vol) >> 12;
    return sample - vol / 2;
}

/* MAME update_pitch_step: the DCW a line may reach at step (high notes: less, so a narrow pulse stays audible and the
 * top does not alias) */
static inline uint32_t cz_dcw_limit(uint32_t step)
{
    uint32_t s = step >> (CZ_PITCH_SHIFT - 2u);
    return 0x400u - (s < 0x400u ? s : 0x400u);
}

#endif
