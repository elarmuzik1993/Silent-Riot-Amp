// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "dsp/analysis/PitchDetector.h"

namespace sr {

// Compact toggle button ("MUTE") painted as a pill — orange when engaged.
// A juce::Button so it wires straight to an APVTS ButtonAttachment.
class MuteButton : public juce::Button {
public:
    MuteButton();
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

// Always-on live tuner strip. Pulls the most-recent DI window from the audio
// engine (via readSamples), runs monophonic pitch detection off the audio
// thread, and paints a note name + cents meter. Driven by the editor's shared
// 30Hz timer (no timer of its own) to match the rest of the GUI.
class TunerBar : public juce::Component {
public:
    TunerBar();
    void paint(juce::Graphics&) override;
    void resized() override;

    // Mute-while-tuning toggle — silences the amp output while engaged. Wired
    // to the TUNER_MUTE parameter via a ButtonAttachment in the editor.
    juce::Button& getMuteButton() { return muteButton; }

    // Size the detector for the current base sample rate. Call from prepare /
    // editor construction before the first refresh().
    void prepare(double sampleRate);

    // Pull a window from the engine and update the display. Editor calls this
    // once per timer tick. readSamples must be set first.
    void refresh();

    // Copies up to maxN of the most recent captured samples into dest (oldest
    // first) and returns the count. Wired to SilentRiotProcessor.
    std::function<int(float* dest, int maxN)> readSamples;

    static constexpr int barHeight = 34;

private:
    PitchDetector      detector;
    std::vector<float> scratch;
    MuteButton         muteButton;

    // Smoothed / held display state
    bool  hasPitch      = false;   // true while a confident note is shown
    int   holdFrames    = 0;       // keeps the last note briefly after signal drops
    float displayCents  = 0.0f;    // eased needle position
    float displayFreq   = 0.0f;    // eased frequency readout
    NoteInfo note;                 // last resolved note

    static constexpr int   kHoldFrames  = 12;    // ~0.4s at 30Hz
    static constexpr float kCentsInTune = 5.0f;  // green window
    static constexpr float kEase        = 0.35f; // needle/freq lerp

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerBar)
};

} // namespace sr
