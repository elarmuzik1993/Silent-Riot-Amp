// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "SettingsPanel.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace sr {

SettingsPanel::SettingsPanel()
{
    addAndMakeVisible(themeSelector);
    addAndMakeVisible(ballisticsSelector);
    addAndMakeVisible(knobModeSelector);
    addAndMakeVisible(scaleSelector);
    addAndMakeVisible(peakHoldToggle);
    addAndMakeVisible(tooltipsToggle);

    peakHoldToggle.getButton().setToggleState(true, juce::dontSendNotification);
    tooltipsToggle.getButton().setToggleState(true, juce::dontSendNotification);

    themeSelector.setTitleText("Theme");
    ballisticsSelector.setTitleText("Meter ballistics");
    knobModeSelector.setTitleText("Knob mode");
    scaleSelector.setTitleText("UI scale");
    setTitle("Settings");

    auto fireChanged = [this] { if (onSettingsChanged) onSettingsChanged(); };

    themeSelector.onChange      = [this, fireChanged](int i) { themeIndex = i; fireChanged(); };
    ballisticsSelector.onChange = [this, fireChanged](int i) { meterBallistics = i; fireChanged(); };
    knobModeSelector.onChange   = [this, fireChanged](int i) { knobMode = i; fireChanged(); };
    scaleSelector.onChange      = [this](int i) {
        uiScale = i;
        if (onScaleChanged) onScaleChanged(i);
    };

    // ToggleSwitch animates via onStateChange internally — external listeners
    // must use onClick (see CLAUDE.md pitfall).
    peakHoldToggle.getButton().onClick = [this, fireChanged] {
        peakHold = peakHoldToggle.getButton().getToggleState();
        fireChanged();
    };
    tooltipsToggle.getButton().onClick = [this, fireChanged] {
        tooltipsEnabled = tooltipsToggle.getButton().getToggleState();
        fireChanged();
    };
}

void SettingsPanel::syncControls()
{
    themeSelector.setSelectedIndex(themeIndex);
    ballisticsSelector.setSelectedIndex(meterBallistics);
    knobModeSelector.setSelectedIndex(knobMode);
    scaleSelector.setSelectedIndex(uiScale);
    peakHoldToggle.getButton().setToggleState(peakHold, juce::dontSendNotification);
    tooltipsToggle.getButton().setToggleState(tooltipsEnabled, juce::dontSendNotification);
}

void SettingsPanel::mouseDown(const juce::MouseEvent& e)
{
    // Close button area (top-right X)
    auto closeArea = juce::Rectangle<float>(getWidth() - 32.0f, 8.0f, 24.0f, 24.0f);
    if (closeArea.contains(e.position))
    {
        if (onClose) onClose();
    }
}

void SettingsPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Drop shadow
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(bounds.translated(2.0f, 2.0f), 6.0f);

    // Panel background
    g.setColour(Colours::panel);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(Colours::border);
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    float y = 16.0f;
    float px = 16.0f;
    float w = bounds.getWidth() - 32.0f;

    // Close X
    g.setFont(Fonts::spaceMono(Fonts::Sizes::title));
    g.setColour(Colours::label);
    g.drawText("X", juce::Rectangle<float>(bounds.getWidth() - 32.0f, 8.0f, 24.0f, 24.0f),
               juce::Justification::centred, false);

    // ── UI PREFERENCES ──
    g.setFont(Fonts::spaceMono(Fonts::Sizes::small, true));
    g.setColour(Colours::accent);
    g.drawText("UI PREFERENCES", juce::Rectangle<float>(px, y, w, 14.0f),
               juce::Justification::centredLeft, false);
    y += 24.0f;

    // Row labels
    auto drawLabel = [&](float rowY, const juce::String& text) {
        g.setFont(Fonts::spaceMono(Fonts::Sizes::small));
        g.setColour(Colours::label);
        g.drawText(text, juce::Rectangle<float>(px, rowY, 100.0f, 20.0f),
                   juce::Justification::centredLeft, false);
    };

    drawLabel(y, "Theme");
    y += 30.0f;
    drawLabel(y, "Meter Ballistics");
    y += 30.0f;
    drawLabel(y, "Peak Hold");
    y += 30.0f;
    drawLabel(y, "Knob Mode");
    y += 30.0f;
    drawLabel(y, "Tooltips");
    y += 30.0f;
    drawLabel(y, "UI Scale");
    y += 40.0f;

    // ── ABOUT ──
    g.setColour(Colours::border);
    g.fillRect(px, y - 10.0f, w, 1.0f);

    g.setFont(Fonts::spaceMono(Fonts::Sizes::small, true));
    g.setColour(Colours::accent);
    g.drawText("ABOUT", juce::Rectangle<float>(px, y, w, 14.0f),
               juce::Justification::centredLeft, false);
    y += 24.0f;

    // Product info
    struct InfoRow { const char* key; juce::String val; };
    InfoRow info[] = {
        { "Product",   "Silent Riot" },
        { "Version",   "v" JucePlugin_VersionString },
        { "Format",    "VST3 / Standalone" },
        { "Developer", "Monolit Beatz" },
    };

    for (auto& row : info)
    {
        g.setFont(Fonts::spaceMono(Fonts::Sizes::small));
        g.setColour(Colours::label);
        g.drawText(row.key, juce::Rectangle<float>(px, y, 80.0f, 16.0f),
                   juce::Justification::centredLeft, false);
        g.setColour(Colours::text);
        g.drawText(row.val, juce::Rectangle<float>(px + 84.0f, y, w - 84.0f, 16.0f),
                   juce::Justification::centredLeft, false);
        y += 20.0f;
    }

    // License: free and open source (GPLv3) — there is no activation
    y += 8.0f;
    g.setColour(Colours::surface);
    g.fillRoundedRectangle(px, y, w, 28.0f, 4.0f);
    g.setColour(Colours::border);
    g.drawRoundedRectangle(px, y, w, 28.0f, 4.0f, 1.0f);

    // Green dot
    g.setColour(Colours::green);
    g.fillEllipse(px + 10.0f, y + 10.0f, 8.0f, 8.0f);
    g.setFont(Fonts::spaceMono(Fonts::Sizes::small));
    g.setColour(Colours::text);
    g.drawText("Free & open source — GPLv3", juce::Rectangle<float>(px + 24.0f, y, w - 24.0f, 28.0f),
               juce::Justification::centredLeft, false);
}

void SettingsPanel::resized()
{
    float px = 16.0f;
    float w = getWidth() - 32.0f;
    float selectorW = w - 110.0f;
    float selectorX = px + 110.0f;
    float y = 40.0f;

    themeSelector.setBounds(static_cast<int>(selectorX), static_cast<int>(y),
                            static_cast<int>(selectorW), 22);
    y += 30.0f;

    ballisticsSelector.setBounds(static_cast<int>(selectorX), static_cast<int>(y),
                                 static_cast<int>(selectorW), 22);
    y += 30.0f;

    peakHoldToggle.setBounds(static_cast<int>(selectorX), static_cast<int>(y - 4),
                             60, 28);
    y += 30.0f;

    knobModeSelector.setBounds(static_cast<int>(selectorX), static_cast<int>(y),
                               static_cast<int>(selectorW), 22);
    y += 30.0f;

    tooltipsToggle.setBounds(static_cast<int>(selectorX), static_cast<int>(y - 4),
                             60, 28);
    y += 30.0f;

    scaleSelector.setBounds(static_cast<int>(selectorX), static_cast<int>(y),
                            static_cast<int>(selectorW), 22);
}

} // namespace sr
