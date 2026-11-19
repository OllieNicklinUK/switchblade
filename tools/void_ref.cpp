// SPDX-License-Identifier: GPL-3.0-only
// The VOID WAVE V2 plugin's own filter (D:/VOID SYNTH/VoidWave/Source/DSP/Filter.cpp, built here without JUCE: tools/void_ref/
// JuceHeader.h) on the cases of tests/void_ref_cfg.h: build/void_ref/NN.f32 (float, VOID_REF_N samples each),
// the reference tests/void_test.c compares eng_void.c's filter against.
//   c++ -std=c++17 -O2 -Itools/void_ref -I"../../VOID SYNTH/VoidWave/Source/DSP" tools/void_ref.cpp "../../VOID SYNTH/VoidWave/Source/DSP/Filter.cpp"
//   ./a.out build/void_ref
#include "Filter.h"
#include <cmath>
#include <cstdio>
#include <string>
extern "C" {
#include "../tests/void_ref_cfg.h"
}

int main(int argc, char **argv)
{
    static const FilterType FT[6] = {FilterType::LP24, FilterType::BP, FilterType::HP12, FilterType::LP12,
                                     FilterType::HP24, FilterType::NOTCH};   // eng_void.c VF_* order
    std::string dir = argc > 1 ? argv[1] : "build/void_ref";
    for (unsigned k = 0; k < VOID_REF_NCFG; k++) {
        const void_ref_cfg_t &c = VOID_REF_CFG[k];
        Filter f;
        f.prepare(VOID_REF_FS, 128);
        f.reset();
        f.setType(FT[c.vf]);
        f.setResonance(std::round(c.res * 127.0f) / 127.0f);   // VOID's knobs: 0..127
        f.setDrive(std::round(c.drv * 127.0f) / 127.0f);
        f.setCutoff(c.hz);
        std::vector<float> out(VOID_REF_N);
        for (unsigned n = 0; n < VOID_REF_N; n++) {
            f.setCutoff(c.hz);                      // as VoiceManager: every sample
            out[n] = f.processSample((float)void_ref_input(n, c.f0));
        }
        char name[32];
        snprintf(name, sizeof name, "/%02u.f32", k);
        FILE *fp = fopen((dir + name).c_str(), "wb");
        if (!fp)
            return 1;
        fwrite(out.data(), sizeof(float), out.size(), fp);
        fclose(fp);
    }
    printf("void_ref: %u cases through the plugin's Filter.cpp -> %s\n", (unsigned)VOID_REF_NCFG, dir.c_str());
    return 0;
}
