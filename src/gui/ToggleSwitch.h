// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

class ToggleSwitch : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer {
public:
    ToggleSwitch(const juce::String& label, juce::Colour activeColor = juce::Colour(0xFFFF6600));
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(juce::Component::FocusChangeType) override;
    void focusLost(juce::Component::FocusChangeType) override;
    juce::ToggleButton& getButton() { return button; }

    void toggle();  // flip state with notification

    // How the state is drawn under the label.
    //   Switch — a chrome bat toggle through a hex nut on a glass ring that
    //            lights in activeColor; lever up is on. The amp row uses this.
    //   Jewel  — a small glass jewel in a chrome bezel (the pilot lamp's glass,
    //            AmpFaceplate::paintLamp), lit in activeColor, with the label
    //            to its left rather than above, so it fits a card's title row.
    //            The effects cards use this.
    // Both animate off the same thumbPos, so the jewel fades rather than snaps.
    enum class Style { Switch, Jewel };
    void setStyle(Style s);

    // Vertical room paint() needs: the label, the gap under it, and the state
    // graphic. JUCE clips paint() to the component's own bounds, so a
    // ToggleSwitch laid out shorter than this loses its lever/lamp *silently* —
    // the label still draws, the control looks like a plain text label, and
    // clicks keep working with no visible feedback. Lay one out against this,
    // not against a title-strip height that happens to fit the label.
    // Jewel puts its label beside the lamp, so only the lamp's glow counts.
    static constexpr int minHeight(Style s)
    {
        if (s == Style::Jewel)
            return static_cast<int>(2.0f * kJewelGlowR);
        return static_cast<int>(kLabelH + kLabelGap + kBatH);
    }

private:
    void timerCallback() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    void paintSwitch(juce::Graphics&, juce::Rectangle<float> bounds);
    void paintJewel(juce::Graphics&, juce::Rectangle<float> bounds);

    juce::ToggleButton button;
    juce::String label;
    juce::Colour activeColor;
    Style style = Style::Switch;
    float thumbPos = 0.0f; // 0=off, 1=on (animated)

    static constexpr float kLabelH   = 14.0f;
    static constexpr float kLabelGap =  4.0f;
    // Bat toggle: drawn at kBatScale around a nut centred kBatPivot below the
    // label gap; the lever tip, its ball and their shadows need kBatH in all.
    static constexpr float kBatScale = 1.15f;
    static constexpr float kBatH     = 30.0f;
    static constexpr float kBatPivot = 14.0f;
    // Jewel: lens radius, and the glow's radius from the lamp's centre. The
    // glow shows as a ring ~3 px wide around the bezel; minHeight() is the glow's diameter, so it never clips.
    static constexpr float kJewelR        =  4.5f;
    static constexpr float kJewelBezelR   = kJewelR + 4.0f;  // paintLamp's bezel
    static constexpr float kJewelGlowR    = 12.0f;
    static constexpr float kJewelLabelGap =  1.5f;  // label to bezel

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToggleSwitch)
};

} // namespace sr
