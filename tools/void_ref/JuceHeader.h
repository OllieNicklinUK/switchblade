/* SPDX-License-Identifier: GPL-3.0-only */
/* The few JUCE names the VOID WAVE plugin's DSP files use (tools/void_ref.cpp builds them without JUCE). */
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
namespace juce {
template <typename T> T jlimit(T lo, T hi, T v) { return v < lo ? lo : v > hi ? hi : v; }
template <typename T> T jmin(T a, T b) { return a < b ? a : b; }
template <typename T> T jmax(T a, T b) { return a > b ? a : b; }
template <typename T> struct MathConstants {
    static constexpr T pi = (T)3.141592653589793238;
    static constexpr T twoPi = (T)6.283185307179586477;
    static constexpr T halfPi = (T)1.570796326794896619;
};
template <typename... T> void ignoreUnused(T &&...) {}
/* the V2 plugin's Filter.h (D:/VOID SYNTH): its drive / resonance smoothers, JUCE's linear SmoothedValue */
namespace ValueSmoothingTypes { struct Linear {}; }
template <typename F, typename Kind = ValueSmoothingTypes::Linear> struct SmoothedValue {
    F cur = 0, tgt = 0, step = 0;
    int left = 0, len = 0;
    void reset(double sr, double sec) { len = (int)std::floor(sr * sec); left = 0; cur = tgt; }
    void setCurrentAndTargetValue(F v) { cur = tgt = v; left = 0; }
    void setTargetValue(F v)
    {
        if (v == tgt) return;
        if (len <= 0) { setCurrentAndTargetValue(v); return; }
        tgt = v; left = len; step = (tgt - cur) / (F)left;
    }
    F getNextValue()
    {
        if (left <= 0) return tgt;
        if (--left == 0) cur = tgt; else cur += step;
        return cur;
    }
    F getCurrentValue() const { return cur; }
    F getTargetValue() const { return tgt; }
    bool isSmoothing() const { return left > 0; }
};
}
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(x)
