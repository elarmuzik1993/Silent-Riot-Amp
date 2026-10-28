// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
// Oversampling State Machine (Spec §5.1)
// Deferred crossfade: FadingOut → Reconfiguring → FadingIn
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>

namespace sr {

// Polyphase-IIR oversampler with whole-sample latency. JUCE adds a Thiran
// all-pass after the downsampler so getLatencyInSamples() is an integer: the
// host can be told the latency exactly instead of a truncated guess (review B4).
// Magnitude response is unchanged; latency rounds up by 0.6–1.6 samples.
std::unique_ptr<juce::dsp::Oversampling<float>> makeIntegerLatencyOversampler(
    int numChannels, int factorLog2, int maxBlockSize);

enum class OversampleTransition { Idle, FadingOut, Reconfiguring, FadingIn };

struct OversampleSwitcher {
    OversampleTransition state = OversampleTransition::Idle;
    int fadeLength  = 128;
    int fadeCounter = 0;
    int pendingMode = -1;
    int currentMode = 1;   // index into oversamplers: 0=2x, 1=4x, 2=8x

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;

    // Starts in initialMode (clamped to 0..2) with no transition pending, so a
    // session saved at 2x/8x does not fade out and back in on its first blocks
    // after every prepare (review B5).
    void init(int numChannels, int maxBlockSize, int initialMode = 1);
    juce::dsp::Oversampling<float>* active();
    int activeFactor() const;
    void requestModeChange(int newMode);   // out-of-range modes are ignored
    bool isTransitioning() const { return state != OversampleTransition::Idle; }
    bool readyToSwitch() const { return state == OversampleTransition::Reconfiguring; }
    void commitSwitch();
    void applyTransitionFade(juce::AudioBuffer<float>& buffer);
};

} // namespace sr
