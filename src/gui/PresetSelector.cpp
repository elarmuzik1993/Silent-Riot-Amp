// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "PresetSelector.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace sr {

PresetSelector::PresetSelector()
{
    setWantsKeyboardFocus(true);
    setTitle("Preset");
    setDescription("Preset selector, left and right arrows to change");
}

void PresetSelector::setPresetIndex(int idx)
{
    currentIndex = ((idx % numPresets) + numPresets) % numPresets;
    setTitle(juce::String("Preset: ") + presetName(currentIndex));  // announced by screen readers
    repaint();
}

void PresetSelector::step(int delta)
{
    setPresetIndex(currentIndex + delta);
    if (onPresetChanged) onPresetChanged(currentIndex);
}

void PresetSelector::mouseDown(const juce::MouseEvent& e)
{
    auto bounds = getLocalBounds().toFloat();
    float x = e.position.x;

    if (x < arrowW)
        step(-1);                                  // Left arrow — previous
    else if (x > bounds.getWidth() - arrowW)
        step(+1);                                  // Right arrow — next
}

bool PresetSelector::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey)  { step(-1); return true; }
    if (key == juce::KeyPress::rightKey) { step(+1); return true; }
    return false;
}

void PresetSelector::focusGained(juce::Component::FocusChangeType) { repaint(); }
void PresetSelector::focusLost(juce::Component::FocusChangeType)   { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> PresetSelector::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::comboBox,
        juce::AccessibilityActions()
            .addAction(juce::AccessibilityActionType::showMenu, [this] { step(+1); }));
}

void PresetSelector::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float h = bounds.getHeight();

    // Background
    g.setColour(Colours::surface);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(Colours::border);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    // Left arrow cell
    g.setColour(Colours::border);
    g.fillRect(arrowW, 0.0f, 1.0f, h);
    g.setFont(Fonts::spaceMono(Fonts::Sizes::value));
    g.setColour(Colours::label);
    g.drawText(juce::CharPointer_UTF8("\xe2\x97\x82"),
               juce::Rectangle<float>(0.0f, 0.0f, arrowW, h),
               juce::Justification::centred, false);

    // Right arrow cell
    float rightX = bounds.getWidth() - arrowW;
    g.setColour(Colours::border);
    g.fillRect(rightX, 0.0f, 1.0f, h);
    g.setColour(Colours::label);
    g.drawText(juce::CharPointer_UTF8("\xe2\x96\xb8"),
               juce::Rectangle<float>(rightX, 0.0f, arrowW, h),
               juce::Justification::centred, false);

    // Center: preset name
    float centerW = bounds.getWidth() - arrowW * 2.0f;
    g.setFont(Fonts::spaceMono(Fonts::Sizes::body));
    g.setColour(Colours::bright);
    g.drawText(juce::String(presetName(currentIndex)),
               juce::Rectangle<float>(arrowW, 0.0f, centerW, h),
               juce::Justification::centred, false);

    // Keyboard focus indicator
    if (hasKeyboardFocus(false))
    {
        g.setColour(Colours::accent.withAlpha(0.7f));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 4.0f, 1.5f);
    }
}

} // namespace sr
