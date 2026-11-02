// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "HeaderBar.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace sr {

GearButton::GearButton()
{
    setWantsKeyboardFocus(true);
    setTitle("Settings");
    setDescription("Open settings panel");
}

void GearButton::mouseDown(const juce::MouseEvent&) { if (onActivate) onActivate(); }

bool GearButton::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
    {
        if (onActivate) onActivate();
        return true;
    }
    return false;
}

void GearButton::focusGained(juce::Component::FocusChangeType) { if (auto* p = getParentComponent()) p->repaint(); }
void GearButton::focusLost(juce::Component::FocusChangeType)   { if (auto* p = getParentComponent()) p->repaint(); }

std::unique_ptr<juce::AccessibilityHandler> GearButton::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::button,
        juce::AccessibilityActions().addAction(
            juce::AccessibilityActionType::press, [this] { if (onActivate) onActivate(); }));
}

HeaderBar::HeaderBar()
{
    addAndMakeVisible(gearButton);
    gearButton.onActivate = [this] { activateGear(); };
}

void HeaderBar::activateGear()
{
    gearActive = !gearActive;
    startTimerHz(60); // animate gear rotation
    if (onGearClick) onGearClick();
}

void HeaderBar::setGearActive(bool active)
{
    if (gearActive == active)
        return;
    gearActive = active;
    startTimerHz(60);
}

void HeaderBar::timerCallback()
{
    float target = gearActive ? 60.0f : 0.0f;
    float diff = target - gearRotation;
    // ~400ms at 60Hz: lerp factor 0.14
    gearRotation += diff * 0.14f;
    if (std::abs(diff) < 0.3f)
    {
        gearRotation = target;
        stopTimer();
    }
    repaint();
}

void HeaderBar::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Left: "SR" brand + divider + "SILENT RIOT"
    float x = 16.0f;
    g.setFont(Fonts::ui(Fonts::Sizes::title, true));
    g.setColour(Colours::accent);
    g.drawText("SR", juce::Rectangle<float>(x, 0.0f, 28.0f, bounds.getHeight()),
               juce::Justification::centredLeft, false);
    x += 30.0f;

    g.setColour(Colours::border);
    g.fillRect(x, 10.0f, 1.0f, bounds.getHeight() - 20.0f);
    x += 8.0f;

    g.setFont(Fonts::ui(Fonts::Sizes::body));
    g.setColour(Colours::label);
    g.drawText("SILENT RIOT", juce::Rectangle<float>(x, 0.0f, 100.0f, bounds.getHeight()),
               juce::Justification::centredLeft, false);

    // Right: "MONOLIT" + gear icon
    g.setFont(Fonts::ui(Fonts::Sizes::small));
    g.setColour(Colours::dim);
    g.drawText("MONOLIT", juce::Rectangle<float>(bounds.getWidth() - 100.0f, 0.0f, 56.0f, bounds.getHeight()),
               juce::Justification::centredRight, false);

    // Gear icon
    auto gearArea = juce::Rectangle<float>(bounds.getWidth() - 36.0f, 7.0f, 24.0f, 24.0f);
    paintGearIcon(g, gearArea);

    // Bottom border
    g.setColour(Colours::border);
    g.fillRect(0.0f, bounds.getHeight() - 1.0f, bounds.getWidth(), 1.0f);
}

void HeaderBar::paintGearIcon(juce::Graphics& g, juce::Rectangle<float> area) const
{
    float cx = area.getCentreX();
    float cy = area.getCentreY();
    float outerR = 9.0f;
    float innerR = 5.0f;
    int numTeeth = 6;

    if (gearActive)
    {
        g.setColour(Colours::accent.withAlpha(0.18f));
        g.fillEllipse(area.expanded(2.0f));
    }

    // Keyboard focus indicator
    if (gearButton.hasKeyboardFocus(false))
    {
        g.setColour(Colours::accent.withAlpha(0.7f));
        g.drawRoundedRectangle(area.expanded(1.0f), 4.0f, 1.5f);
    }

    float rotRad = gearRotation * juce::MathConstants<float>::pi / 180.0f;

    juce::Path gear;
    for (int i = 0; i < numTeeth * 2; ++i)
    {
        float angle = static_cast<float>(i) * juce::MathConstants<float>::pi / static_cast<float>(numTeeth);
        angle += rotRad;
        float r = (i % 2 == 0) ? outerR : innerR;
        float px = cx + std::cos(angle) * r;
        float py = cy + std::sin(angle) * r;
        if (i == 0) gear.startNewSubPath(px, py);
        else gear.lineTo(px, py);
    }
    gear.closeSubPath();

    g.setColour(gearActive ? Colours::accent : Colours::dim);
    g.strokePath(gear, juce::PathStrokeType(1.5f));

    // Center hole
    g.drawEllipse(cx - 2.5f, cy - 2.5f, 5.0f, 5.0f, 1.0f);
}

void HeaderBar::resized()
{
    // Gear hit-area overlays the painted icon (see paint())
    gearButton.setBounds(getWidth() - 36, 7, 24, 24);
}

} // namespace sr
