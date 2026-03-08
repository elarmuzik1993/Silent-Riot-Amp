#pragma once
#include <juce_core/juce_core.h>

namespace tt::Params {

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

} // namespace tt::Params
