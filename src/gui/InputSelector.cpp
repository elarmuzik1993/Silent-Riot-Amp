// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "InputSelector.h"

namespace sr {

int firstActiveInput(const juce::BigInteger& activeInputs, int numInputs)
{
    for (int ch = 0; ch < numInputs; ++ch)
        if (activeInputs[ch])
            return ch;
    return -1;
}

juce::String selectSingleInput(juce::AudioDeviceManager& deviceManager, int channel)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setBit(channel);
    return deviceManager.setAudioDeviceSetup(setup, true);
}

InputSelector::InputSelector(juce::AudioDeviceManager& dm)
    : deviceManager(dm)
{
    addAndMakeVisible(combo);
    combo.setTitle("Input");
    combo.setTooltip("Interface input that feeds the amp");

    combo.onChange = [this] {
        const int channel = combo.getSelectedId() - 1;
        if (channel < 0)
            return;
        const auto error = selectSingleInput(deviceManager, channel);
        if (error.isNotEmpty())
        {
            DBG("Silent Riot: could not select input " << (channel + 1) << ": " << error);
        }
        refresh();
    };

    deviceManager.addChangeListener(this);
    refresh();
}

InputSelector::~InputSelector()
{
    deviceManager.removeChangeListener(this);
}

void InputSelector::resized()
{
    combo.setBounds(getLocalBounds());
}

void InputSelector::refresh()
{
    combo.clear(juce::dontSendNotification);

    auto* device = deviceManager.getCurrentAudioDevice();
    const auto names = device != nullptr ? device->getInputChannelNames() : juce::StringArray{};

    for (int ch = 0; ch < names.size(); ++ch)
    {
        const auto name = names[ch].trim();
        combo.addItem(name.isNotEmpty() ? name : "Input " + juce::String(ch + 1), ch + 1);
    }

    combo.setEnabled(! names.isEmpty());
    combo.setTextWhenNothingSelected(names.isEmpty() ? "NO INPUTS" : "OFF");
    combo.setTextWhenNoChoicesAvailable("NO INPUTS");

    const int active = device != nullptr
                           ? firstActiveInput(device->getActiveInputChannels(), names.size())
                           : -1;
    if (active >= 0)
        combo.setSelectedId(active + 1, juce::dontSendNotification);
}

} // namespace sr
