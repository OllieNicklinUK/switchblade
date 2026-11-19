// SPDX-License-Identifier: GPL-3.0-only
// The Ultramaster KR-106 factory presets' slider values in real units, from its own curves (Source/DSP/KR106ParamValue.h;
// GPL-3.0, Karl LaRocca): presets/kr106/kr106_values.txt, which tools/drift_kr106_presets.py reads.
//   c++ -std=c++17 -I../ultramaster_kr106-main/Source/DSP -I../ultramaster_kr106-main/Source tools/kr106_values.cpp && ./a.out > presets/kr106/kr106_values.txt
// columns: index|name|VCF Hz|LFO Hz|LFO delay ms|DCO LFO semitones|VCF LFO semitones|attack ms|decay ms|release ms|porta ms/oct
#include <cstdio>
#include "KR106ParamValue.h"
#include "KR106_Presets_JUCE.h"
using namespace kr106;
int main() {
  for (int k = 0; k < kNumFactoryPresets; k++) {
    const auto &p = kFactoryPresets[k];
    bool j6 = k < 128;
    auto s = [&](int i) { return p.values[i] / 127.f; };
    printf("%d|%s|%.1f|%.3f|%.0f|%.3f|%.3f|%.0f|%.0f|%.0f|%.0f\n", k, p.name, ParamValue::vcfFreqHz(s(10), j6),
           ParamValue::lfoRateHz(s(3), j6), ParamValue::lfoDelayMs(s(4)), ParamValue::dcoLfoSemitones(s(5), j6),
           ParamValue::vcfLfoSemitones(s(13), j6), ParamValue::attackMs(s(16), j6), ParamValue::decRelMs(s(17), j6),
           ParamValue::decRelMs(s(19), j6), ParamValue::portaMsPerOct(s(40)));
  }
}
