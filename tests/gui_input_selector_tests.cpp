// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Standalone INPUT selector. The amp reads bus channel 0 only, and JUCE's
// standalone dialog offers inputs as stereo pairs, so Input 2 of a pair
// landed on channel 1 and was never heard. The selector opens the device
// with exactly one input active; JUCE packs active inputs, so that input
// arrives on channel 0. Driven here against a 4-input mock device, modelled
// on the mock in JUCE's own AudioDeviceManager tests.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include "gui/InputSelector.h"
#include "gui/SettingsPanel.h"
#include "StandaloneDevice.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

namespace {

class MockDevice final : public juce::AudioIODevice
{
public:
    MockDevice(const juce::String& name, const juce::String& type, juce::StringArray ins)
        : juce::AudioIODevice(name, type), inNames(std::move(ins)) {}

    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2" }; }
    juce::StringArray getInputChannelNames() override  { return inNames; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override    { return { 128 }; }
    int getDefaultBufferSize() override                    { return 128; }

    juce::String open(const juce::BigInteger& ins, const juce::BigInteger& outs,
                      double, int) override
    {
        inChannels = ins;
        outChannels = outs;
        on = true;
        return {};
    }
    void close() override  { on = false; }
    bool isOpen() override { return on; }
    void start(juce::AudioIODeviceCallback* c) override
    {
        callback = c;
        if (callback != nullptr) callback->audioDeviceAboutToStart(this);
        playing = true;
    }
    void stop() override
    {
        playing = false;
        if (callback != nullptr) callback->audioDeviceStopped();
        callback = nullptr;
    }
    bool isPlaying() override                { return playing; }
    juce::String getLastError() override     { return {}; }
    int getCurrentBufferSizeSamples() override { return 128; }
    double getCurrentSampleRate() override   { return 48000.0; }
    int getCurrentBitDepth() override        { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return outChannels; }
    juce::BigInteger getActiveInputChannels() const override  { return inChannels; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override  { return 0; }

private:
    juce::StringArray inNames;
    juce::BigInteger inChannels, outChannels;
    juce::AudioIODeviceCallback* callback = nullptr;
    bool on = false, playing = false;
};

class MockDeviceType final : public juce::AudioIODeviceType
{
public:
    explicit MockDeviceType(juce::StringArray ins)
        : juce::AudioIODeviceType("mock"), inNames(std::move(ins)) {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override { return { "Interface" }; }
    int getDefaultDeviceIndex(bool) const override        { return 0; }
    int getIndexOfDevice(juce::AudioIODevice* d, bool) const override
    {
        return d != nullptr && d->getName() == "Interface" ? 0 : -1;
    }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice(const juce::String& out, const juce::String& in) override
    {
        if (out == "Interface" || in == "Interface")
            return new MockDevice("Interface", getTypeName(), inNames);
        return nullptr;
    }

private:
    juce::StringArray inNames;
};

// A device manager that knows only the mock type, opened the way the
// standalone opens it by default: a stereo input pair (channels 0 and 1).
struct Rig
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // first: outlives everything below
    juce::AudioDeviceManager dm;

    explicit Rig(juce::StringArray ins = { "Input 1", "Input 2", "Input 3", "Input 4" })
    {
        dm.addAudioDeviceType(std::make_unique<MockDeviceType>(std::move(ins)));
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        setup.inputDeviceName = setup.outputDeviceName = "Interface";
        const auto err = dm.initialise(2, 2, nullptr, false, {}, &setup);
        REQUIRE(err.isEmpty());
        REQUIRE(dm.getCurrentAudioDevice() != nullptr);
    }

    juce::BigInteger active() const
    {
        return dm.getCurrentAudioDevice()->getActiveInputChannels();
    }

    void setActive(std::initializer_list<int> channels)
    {
        auto setup = dm.getAudioDeviceSetup();
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        for (int ch : channels) setup.inputChannels.setBit(ch);
        REQUIRE(dm.setAudioDeviceSetup(setup, true).isEmpty());
    }
};

juce::BigInteger bits(std::initializer_list<int> channels)
{
    juce::BigInteger b;
    for (int ch : channels) b.setBit(ch);
    return b;
}

} // namespace

TEST_CASE("firstActiveInput: lowest active input below the device's count", "[input-select]")
{
    CHECK(sr::firstActiveInput(bits({}), 4) == -1);
    CHECK(sr::firstActiveInput(bits({ 1 }), 4) == 1);
    CHECK(sr::firstActiveInput(bits({ 0, 1 }), 4) == 0);
    CHECK(sr::firstActiveInput(bits({ 2, 3 }), 4) == 2);
    CHECK(sr::firstActiveInput(bits({ 5 }), 4) == -1);   // past the device's inputs
    CHECK(sr::firstActiveInput(bits({ 0 }), 0) == -1);
}

TEST_CASE("selectSingleInput: reopens the device with only that input active", "[input-select]")
{
    Rig rig;
    REQUIRE(rig.active() == bits({ 0, 1 }));   // the standalone's default pair

    CHECK(sr::selectSingleInput(rig.dm, 1).isEmpty());
    CHECK(rig.dm.getCurrentAudioDevice()->isOpen());
    CHECK(rig.active() == bits({ 1 }));

    CHECK(sr::selectSingleInput(rig.dm, 3).isEmpty());
    CHECK(rig.active() == bits({ 3 }));

    // Outputs are left alone.
    CHECK(rig.dm.getCurrentAudioDevice()->getActiveOutputChannels() == bits({ 0, 1 }));
}

TEST_CASE("InputSelector: lists the inputs and picks one", "[input-select]")
{
    Rig rig;
    sr::InputSelector selector(rig.dm);
    auto& combo = selector.comboBox();

    REQUIRE(combo.getNumItems() == 4);
    CHECK(combo.getItemText(1) == "Input 2");
    CHECK(combo.isEnabled());
    CHECK(combo.getSelectedId() == 1);   // pair 1+2 live → the amp hears Input 1

    combo.setSelectedId(2, juce::sendNotificationSync);
    CHECK(rig.active() == bits({ 1 }));
    CHECK(combo.getSelectedId() == 2);
}

TEST_CASE("InputSelector: refresh shows the first active input", "[input-select]")
{
    Rig rig;
    sr::InputSelector selector(rig.dm);
    auto& combo = selector.comboBox();

    combo.setSelectedId(2, juce::sendNotificationSync);
    REQUIRE(rig.active() == bits({ 1 }));

    // The pair re-enabled from JUCE's own audio dialog: Input 1 is what the amp hears.
    rig.setActive({ 0, 1 });
    selector.refresh();
    CHECK(combo.getSelectedId() == 1);

    // Every input switched off: nothing selected, still usable.
    rig.setActive({});
    selector.refresh();
    CHECK(combo.getSelectedId() == 0);
    CHECK(combo.isEnabled());
}

TEST_CASE("InputSelector: unnamed inputs fall back to their number", "[input-select]")
{
    Rig rig({ "", "Guitar" });
    sr::InputSelector selector(rig.dm);
    CHECK(selector.comboBox().getItemText(0) == "Input 1");
    CHECK(selector.comboBox().getItemText(1) == "Guitar");
}

TEST_CASE("InputSelector: a device with no inputs disables it", "[input-select]")
{
    Rig rig({});
    sr::InputSelector selector(rig.dm);
    CHECK(selector.comboBox().getNumItems() == 0);
    CHECK_FALSE(selector.comboBox().isEnabled());
}

TEST_CASE("SettingsPanel: the AUDIO INPUT section appears only once attached", "[input-select]")
{
    Rig rig;
    sr::SettingsPanel panel;
    CHECK_FALSE(panel.hasInputSelector());
    CHECK(panel.panelHeight() == sr::SettingsPanel::panelH);

    panel.attachInputSelector(rig.dm);
    REQUIRE(panel.hasInputSelector());
    CHECK(panel.panelHeight() == sr::SettingsPanel::panelH + sr::SettingsPanel::kInputSectionH);

    panel.setSize(sr::SettingsPanel::panelW, panel.panelHeight());
    auto* selector = panel.inputSelector();
    REQUIRE(selector != nullptr);
    CHECK(selector->isVisible());
    CHECK(panel.getLocalBounds().contains(selector->getBounds()));
    CHECK(selector->getHeight() > 0);

    panel.attachInputSelector(rig.dm);   // a second attach is a no-op
    CHECK(panel.inputSelector() == selector);
}

TEST_CASE("standaloneDeviceManager: none outside the standalone app", "[input-select]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CHECK(sr::standaloneDeviceManager() == nullptr);
}
