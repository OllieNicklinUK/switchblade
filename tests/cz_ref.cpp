// SPDX-License-Identifier: GPL-3.0-only
// CZ-Z (V2 Phase 1): firmware/src/cz_core.h held to MAME's uPD933 (tests/cz_ref/upd933.cpp, Devin Acker,
// BSD-3-Clause, unmodified), sample for sample.
//
// The chip runs as MAME runs it: registers written as the CZ's CPU writes them (pitch, waveform, phase, the DCA and
// DCW envelope steps), then update() per sample. Before each sample the test asks cz_wave for the same sample from
// the chip's own state (its position, waveforms, window, its DCW under the pitch limit, its level) and compares.
// Every waveform (both halves of a cycle: the CZ's two-waveform lines), every window, five pitches, DCW sweeps up and
// down at two speeds, three levels, the ring modulator. Also the cosine (tools/gen_cz_tables.py) entry for entry,
// and the DCW pitch limit (cz_dcw_limit) at every sample.
//
//   g++ -std=c++20 -O2 -w -Ibuild/gen -Ifirmware/src -Itests/cz_ref -o build/host/cz_ref tests/cz_ref.cpp
#include "emu.h"
#define private public
#define protected public
#include "upd933.cpp"
#undef private
#undef protected
#include "cz_core.h"

static machine_config mc;
static long fails, samples, dcw_on, ring_on, nonzero;

static void wr(upd933_device &c, u8 reg, u16 v)
{
    c.write(reg);
    c.write((u8)(v >> 8));
    c.write((u8)(v & 0xff));
}

// one case: voice vn plays note at waveforms w0 / w1 and window; its DCW goes to dcw_to at rate, its DCA to level;
// halfway the DCW turns back down to 0; ring: voice vn ring-modulated by the voice updated just before it
static void run_case(upd933_device &c, int vn, int note, int w0, int w1, int window, int dcw_to, int rate, int level, int ring)
{
    const int N = 1500;
    c.device_reset();
    c.cs_w(0);
    wr(c, 0x60 + vn, (u16)(note << 9));
    wr(c, 0x60 + ((vn + 5) & 7), (u16)((note + 7) << 9));                           // the ring's other voice
    {
        u16 wf = (u16)(w0 << 13 | w1 << 10 | (w1 != w0) << 9 | window << 6);
        wr(c, 0x68 + vn, wf);
        if (ring) {                                    // regs 68-6f: even n sets the modulation of voice n - 2
            int setter = (vn + 2) & 7;
            wr(c, 0x68 + setter, (u16)(0x4000 | 1 << 5));
        }
    }
    wr(c, 0x98 + vn, 0);
    wr(c, 0x00 + vn, (u16)(0 << 15 | 100 << 8 | 0x80 | level));                    // DCA up, fast, sustain, level
    wr(c, 0x00 + ((vn + 5) & 7), (u16)(100 << 8 | 0x80 | 127));
    wr(c, 0x20 + vn, (u16)(0 << 15 | rate << 8 | 0x80 | dcw_to));                    // DCW up
    for (int i = 0; i < N; i++) {
        if (i == N / 2)
            wr(c, 0x20 + vn, (u16)(1 << 15 | rate << 8 | 0x80 | 0));               // .. and back down
        c.update((vn + 5) & 7);                        // (the voice before: m_last_sample for the ring)
        auto &v = c.m_voice[vn];
        u16 dcw = std::min(u16(c.m_dcw[vn].m_current >> 16), v.m_dcw_limit);
        u32 wv = (u32)(v.m_wave[0] | v.m_wave[1] << 4 | v.m_window << 8);
        int vol = c.m_volume[c.m_dca[vn].m_current >> 16];
        s32 want = cz_wave(v.m_position, wv, dcw, vol);
        if (v.m_ring_mod)
            want = want * c.m_last_sample / 0x1000;
        if (v.m_mute_other)
            want -= c.m_last_sample;
        u32 lim = cz_dcw_limit(v.m_pitch_step);
        s16 got = c.update(vn);
        c.m_sample_count++;
        samples++;
        dcw_on += dcw > 0;
        ring_on += v.m_ring_mod != 0;
        nonzero += got != 0;
        if ((s16)want != got || lim != v.m_dcw_limit) {
            if (fails < 10)
                printf("cz_ref: MISMATCH voice %d note %d wave %d/%d window %d dcw->%d rate %d level %d ring %d, sample %d: "
                       "cz_wave %d, MAME %d (dcw %u, limit %u / %u)\n", vn, note, w0, w1, window, dcw_to, rate, level, ring,
                       i, (int)(s16)want, (int)got, dcw, lim, v.m_dcw_limit);
            fails++;
        }
    }
}

int main()
{
    upd933_device chip(mc, "cz", nullptr, 4480000);   // a CZ-101's: 40 kHz
    long cos_bad = 0;
    chip.device_start();
    for (int i = 0; i < 0x800; i++)
        cos_bad += CZ_COS[i] != chip.m_cosine[i];
    printf("cz_ref: the cosine (tools/gen_cz_tables.py) against MAME's m_cosine: %ld of 2048 differ %s\n", cos_bad,
           cos_bad ? "FAIL" : "ok");
    static const int NOTES[] = {12, 36, 62, 84, 108};
    static const int DCW[] = {0, 40, 127};
    static const int RATE[] = {24, 64};
    static const int LEVEL[] = {127, 60, 10};
    for (int w0 = 0; w0 < 8; w0++)
        for (int pair = 0; pair < 2; pair++)
            for (int window = 0; window < 8; window++)
                for (int n : NOTES)
                    for (int d : DCW)
                        for (int r : RATE)
                            for (int l : LEVEL)
                                run_case(chip, (w0 + window) & 1 ? 1 : 0, n, w0, pair ? (w0 + 3) & 7 : w0, window, d, r, l, 0);
    for (int w0 = 0; w0 < 8; w0++)                     // the ring modulator, on an even voice (its modulation is set by n + 2)
        for (int n : NOTES)
            run_case(chip, 0, n, w0, w0, w0 >= 5 ? w0 - 4 : 0, 100, 40, 127, 1);
    printf("cz_ref: cz_wave against MAME's upd933 update(): %ld samples, %ld differ %s\n", samples, fails, fails ? "FAIL" : "ok");
    printf("cz_ref: (coverage: the DCW above 0 in %ld, the ring on in %ld, a non-zero sample in %ld)\n", dcw_on, ring_on, nonzero);
    if (dcw_on < samples / 5 || ring_on < 5000 || nonzero < samples / 2) {   // (a third of the cases hold the DCW at 0)
        printf("cz_ref: coverage too thin: FAIL\n");
        return 1;
    }
    return cos_bad || fails ? 1 : 0;
}
