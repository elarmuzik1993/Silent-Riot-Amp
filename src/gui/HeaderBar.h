// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

// Transparent focusable hit-area for the gear icon (HeaderBar paints the icon).
class GearButton : public juce::Component {
public:
    GearButton();
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(juce::Component::FocusChangeType) override;
    void focusLost(juce::Component::FocusChangeType) override;

    std::function<void()> onActivate;

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GearButton)
};

class HeaderBar : public juce::Component, private juce::Timer {
public:
    HeaderBar();
    void paint(juce::Graphics&) override;
    void resized() override;


    std::function<void()> onGearClick;

    // Keeps the gear's rotation in step when Settings closes some other way
    // (its close button, Escape, a click outside). No callback.
    void setGearActive(bool active);
    const juce::Component& getGearButton() const { return gearButton; }

private:
    void timerCallback() override;
    void paintGearIcon(juce::Graphics& g, juce::Rectangle<float> area) const;
    void activateGear();

    GearButton gearButton;
    bool gearActive = false;
    float gearRotation = 0.0f; // 0=inactive, 60=active (animated degrees)

    static constexpr float barHeight = 38.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeaderBar)
};

} // namespace sr
