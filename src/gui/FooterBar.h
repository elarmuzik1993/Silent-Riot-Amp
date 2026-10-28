// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

class FooterBar : public juce::Component {
public:
    FooterBar();
    void paint(juce::Graphics&) override;

    void setStats(int osFactor, float latencyMs, double sampleRate, int bitDepth);

private:
    int osFactor = 4;
    float latencyMs = 0.0f;
    double sampleRate = 48000.0;
    int bitDepth = 32;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FooterBar)
};

} // namespace sr
