// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

// ═══════════════════════════════════════════════════════════════
// Standalone INPUT selector: which interface input feeds the amp.
//
// The amp reads bus channel 0 only, and JUCE's standalone audio dialog offers
// inputs as stereo pairs, so the second input of a pair (Input 2 on most
// interfaces) lands on channel 1 and is never heard. Picking an input here
// reopens the device with exactly that input active; JUCE hands the callback
// only active inputs, packed, so it arrives on channel 0 and the processor,
// meters and tuner need no change. The choice is part of the device setup,
// which the standalone saves and restores itself; it is not a parameter.
// ═══════════════════════════════════════════════════════════════

// Lowest active input below numInputs, or -1 when none is active.
int firstActiveInput(const juce::BigInteger& activeInputs, int numInputs);

// Reopens the current device with only `channel` active (outputs untouched).
// Returns JUCE's error message, empty on success.
juce::String selectSingleInput(juce::AudioDeviceManager& deviceManager, int channel);

class InputSelector : public juce::Component, private juce::ChangeListener {
public:
    explicit InputSelector(juce::AudioDeviceManager& deviceManager);
    ~InputSelector() override;

    void resized() override;

    // Re-reads the device's inputs and shows the one the amp hears (the first
    // active input). Runs on every device change and after every pick, so the
    // list never shows a choice the driver refused.
    void refresh();

    juce::ComboBox& comboBox() { return combo; }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }

    juce::AudioDeviceManager& deviceManager;
    juce::ComboBox combo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InputSelector)
};

} // namespace sr
