// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "ToggleSwitch.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace sr {

ToggleSwitch::ToggleSwitch(const juce::String& l, juce::Colour color)
    : label(l), activeColor(color)
{
    button.setClickingTogglesState(true);
    button.onStateChange = [this] { startTimerHz(60); };
    addChildComponent(button); // hidden — we handle clicks

    setWantsKeyboardFocus(true);
    setTitle(l);
}

void ToggleSwitch::toggle()
{
    button.setToggleState(!button.getToggleState(), juce::sendNotification);
}

void ToggleSwitch::mouseDown(const juce::MouseEvent&)
{
    toggle();
}

bool ToggleSwitch::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
    {
        toggle();
        return true;
    }
    return false;
}

void ToggleSwitch::focusGained(juce::Component::FocusChangeType) { repaint(); }
void ToggleSwitch::focusLost(juce::Component::FocusChangeType)   { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> ToggleSwitch::createAccessibilityHandler()
{
    class Handler : public juce::AccessibilityHandler
    {
    public:
        explicit Handler(ToggleSwitch& s)
            : juce::AccessibilityHandler(
                  s, juce::AccessibilityRole::toggleButton,
                  juce::AccessibilityActions().addAction(
                      juce::AccessibilityActionType::toggle, [&s] { s.toggle(); })),
              owner(s) {}

        juce::AccessibleState getCurrentState() const override
        {
            auto state = juce::AccessibilityHandler::getCurrentState().withCheckable();
            return owner.getButton().getToggleState() ? state.withChecked() : state;
        }

    private:
        ToggleSwitch& owner;
    };
    return std::make_unique<Handler>(*this);
}

void ToggleSwitch::timerCallback()
{
    float target = button.getToggleState() ? 1.0f : 0.0f;
    // Overshoot easing approximation
    float diff = target - thumbPos;
    thumbPos += diff * 0.2f;
    if (std::abs(diff) < 0.005f)
    {
        thumbPos = target;
        stopTimer();
    }
    repaint();
}

void ToggleSwitch::setStyle(Style s)
{
    if (style == s)
        return;
    style = s;
    repaint();
}

void ToggleSwitch::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Label above — shared by both styles
    g.setFont(Fonts::spaceMono(Fonts::Sizes::small));
    g.setColour(Colours::label);
    g.drawText(label.toUpperCase(), juce::Rectangle<float>(0, 0, bounds.getWidth(), kLabelH),
               juce::Justification::centred, false);

    if (style == Style::Led)
        paintLed(g, bounds);
    else
        paintSwitch(g, bounds);
}

void ToggleSwitch::paintLed(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto led = juce::Rectangle<float>(kLedW, kLedH)
                         .withCentre({ bounds.getWidth() * 0.5f,
                                       kLabelH + kLabelGap + kLedH * 0.5f });
    const float r = kLedH * 0.5f;   // fully rounded caps

    // Halo only once lit, and scaled by thumbPos so the bar fades up on the
    // same animation the pill uses to slide its thumb.
    if (thumbPos > 0.01f)
    {
        g.setColour(activeColor.withAlpha(0.20f * thumbPos));
        g.fillRoundedRectangle(led.expanded(kLedGlow), r + kLedGlow);
    }

    // Unlit stays a visible dark bar, not an empty gap — an absent indicator and
    // an off one have to look different or the control reads as missing.
    g.setColour(Colours::track.interpolatedWith(activeColor, thumbPos));
    g.fillRoundedRectangle(led, r);

    if (hasKeyboardFocus(false))
    {
        g.setColour(Colours::accent.withAlpha(0.7f));
        g.drawRoundedRectangle(led.expanded(kLedGlow + 1.5f), r + kLedGlow + 1.5f, 1.0f);
    }
}

void ToggleSwitch::paintSwitch(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Track
    float trackX = (bounds.getWidth() - trackW) * 0.5f;
    float trackY = kLabelH + kLabelGap;
    float trackRadius = trackH * 0.5f;

    juce::Colour trackColour = button.getToggleState()
        ? activeColor
        : Colours::track;
    g.setColour(trackColour);
    g.fillRoundedRectangle(trackX, trackY, trackW, trackH, trackRadius);

    // Thumb
    float thumbTravel = trackW - thumbD - 4.0f;
    float thumbX = trackX + 2.0f + thumbPos * thumbTravel;
    float thumbY = trackY + (trackH - thumbD) * 0.5f;

    if (button.getToggleState())
    {
        // Glow
        g.setColour(activeColor.withAlpha(0.25f));
        g.fillEllipse(thumbX - 5.0f, thumbY - 5.0f, thumbD + 10.0f, thumbD + 10.0f);
    }

    juce::Colour thumbCol = button.getToggleState() ? Colours::white : Colours::dim;
    g.setColour(thumbCol);
    g.fillEllipse(thumbX, thumbY, thumbD, thumbD);

    // Keyboard focus ring around the track
    if (hasKeyboardFocus(false))
    {
        g.setColour(Colours::accent.withAlpha(0.7f));
        g.drawRoundedRectangle(trackX - 3.0f, trackY - 3.0f,
                               trackW + 6.0f, trackH + 6.0f, trackRadius + 3.0f, 1.5f);
    }
}

} // namespace sr
