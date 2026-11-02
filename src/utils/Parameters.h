// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
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
    static const juce::String IR_INDEX    = "irIndex";     // int: 0–N
    static const juce::String TUNER_MUTE  = "tunerMute";   // bool: silence output while tuning

    // ── Effects module (all default OFF; tone-neutral when bypassed) ──
    // Pre-amp: noise gate + boost/tight-EQ. Post-cab: delay + reverb.
    // The noise gate has no on/off toggle — it self-gates: THRESHOLD 0 = off.
    static const juce::String GATE_THRESHOLD  = "gateThreshold"; // float dB
    static const juce::String BOOST_ON        = "boostOn";       // bool
    static const juce::String BOOST_LEVEL     = "boostLevel";    // float 0–1 → 0..+18 dB
    static const juce::String BOOST_TIGHT     = "boostTight";    // float 0–1 (low-cut)
    static const juce::String BOOST_BRIGHT    = "boostBright";   // float 0–1 (high shelf)
    static const juce::String DELAY_ON        = "delayOn";       // bool
    static const juce::String DELAY_TIME      = "delayTime";     // float ms
    static const juce::String DELAY_FEEDBACK  = "delayFeedback"; // float 0–1
    static const juce::String DELAY_MIX       = "delayMix";      // float 0–1
    static const juce::String DELAY_TONE      = "delayTone";     // float 0–1 (feedback high-cut: 0=dark, 1=bright)
    static const juce::String DELAY_PINGPONG  = "delayPingPong"; // bool: cross-fed bouncing stereo repeats
    static const juce::String DELAY_SYNC_DIV  = "delaySyncDiv";  // choice: MANUAL (TIME knob) or a note division
    static const juce::String REVERB_ON       = "reverbOn";      // bool
    static const juce::String REVERB_MIX      = "reverbMix";     // float 0–1
    static const juce::String REVERB_SIZE     = "reverbSize";    // float 0–1
    static const juce::String REVERB_TONE     = "reverbTone";    // float 0–1 (loop damping)
    static const juce::String WIDTH           = "width";         // float 0–1: 0=off (dry/mono), 1=wide

    // ── Gain staging (utility trims, unity at knob centre 0.5) ──
    static const juce::String INPUT_LEVEL  = "inputLevel";   // float 0–1: ±12 dB
    static const juce::String OUTPUT_LEVEL = "outputLevel";  // float 0–1: silence..+12 dB
}

// Defaults of the amp knobs, shared by the parameter layout and by
// processBlock's NaN fallback, so a bad host value reads as the default knob.
namespace Defaults {
    constexpr float GAIN   = 0.6f;
    constexpr float TONE   = 0.5f;
    constexpr float VOLUME = 0.5f;
}

} // namespace sr::Params
