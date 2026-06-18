#pragma once
#include <juce_core/juce_core.h>

namespace sr::Params {

// Parameter IDs — must match UI spec §7.1 and DSP spec §7
// DSP internal range: 0.0–1.0 (knobs), bool (toggles)
// UI display range:   0.0–10.0 (knobs show scaled display value)
namespace IDs {
    static const juce::String GAIN        = "gain";
    static const juce::String TONE        = "tone";
    static const juce::String VOLUME      = "volume";
    static const juce::String HALF_POWER  = "halfPower";   // bool: false=15W, true=7W
    static const juce::String CAB_SIM     = "cabSim";      // bool: on/off
    static const juce::String POWER       = "power";       // bool: on/off (master bypass)
    static const juce::String OVERSAMPLE  = "oversample";  // choice: 2x/4x/8x
    static const juce::String PRESET_IDX  = "presetIndex"; // int: 0–4
    static const juce::String IR_INDEX    = "irIndex";     // int: 0–N
}

} // namespace sr::Params

namespace sr::Presets {

// Factory presets — order MUST match PresetSelector::presetNames.
// Knob values are internal 0–1 (UI shows ×10). halfPower: false=15W, true=7W.
struct Preset {
    const char* name;
    float gain;
    float tone;     // Shape: 0 = warm/mid-forward, 1 = scooped/modern
    float volume;
    bool  halfPower;
    bool  cabSim;
};

inline constexpr Preset table[] = {
    // name               gain  tone  vol   7W     cab
    { "Classic Crunch",   0.55f, 0.45f, 0.55f, false, true },
    { "Edge of Breakup",  0.35f, 0.50f, 0.62f, false, true },
    { "Full Drive",       0.85f, 0.62f, 0.58f, false, true },
    { "Clean Shimmer",    0.18f, 0.60f, 0.50f, false, true },
    { "Heavy Sag",        0.70f, 0.45f, 0.82f, true,  true },
};

inline constexpr int count = static_cast<int>(sizeof(table) / sizeof(table[0]));

} // namespace sr::Presets
