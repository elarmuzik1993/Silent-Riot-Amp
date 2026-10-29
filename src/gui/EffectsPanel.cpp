// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "EffectsPanel.h"
#include "AmpFaceplate.h"
#include "ColourPalette.h"
#include "FontData.h"
#include "LookAndFeel.h"
#include "../utils/Constants.h"

namespace sr {

// Card geometry — kept in sync between paint() and resized(). Cards are a
// fixed size (sized to hug the title row + one knob row) and the 2×2 grid is
// centred in the panel, rather than stretched to fill it — otherwise the knobs
// float in oversized boxes.
namespace {
    constexpr int kCardGap = 12;
    constexpr int kCardW   = 328;  // two fit the 680 px inside the panel margin
    constexpr int kCardH   = 136;  // title row + one knob row (or DELAY's switch stack)

    // Card chrome metrics, shared by paint() and resized().
    constexpr int kCardPadY = 6;
    constexpr int kRowGap   = 3;

    // The effect knobs are the amp row's satin pointer knobs (name on top, value
    // engraved below) drawn smaller. kKnobH must hold the text strip *and* a
    // dial big enough for the pointer bar to read — under ~50 px the bar's
    // chamfer turns to mush. The name keeps its full size at any scale.
    constexpr float kKnobScale = 0.7f;
    constexpr int   kKnobW     = 70;
    constexpr int   kKnobH     = 80;
    constexpr int   kMinDial   = 52;
    static_assert(kKnobH >= NotchKnob::stripHeight(kKnobScale, true) + kMinDial,
                  "effect knobs are too short for their name, dial and value");

    // Height of the row at the top of every card: the title, and the ON
    // toggle(s) beside it. A ToggleSwitch has to fit its whole graphic (here
    // the jewel's glow), and JUCE clips the overflow away without a warning —
    // the toggle then looks dead while its clicks still register. Every card
    // reserves this much, toggle or not, so all four knob rows stay on the
    // same baseline.
    constexpr auto kCardToggleStyle = ToggleSwitch::Style::Jewel;
    constexpr int  kToggleH = ToggleSwitch::minHeight(kCardToggleStyle);
    constexpr int  kToggleW = 40;  // a two-letter label + the jewel and its glow

    // DELAY's SYNC and PP are the amp row's bat toggles, stacked SYNC over PP
    // on the card's left, before TIME. The stack is the tallest thing in any
    // card, so it sets kCardH; every card shares that height, so all four
    // knob rows stay on the same baseline.
    constexpr auto kStackStyle = ToggleSwitch::Style::Switch;
    constexpr int  kStackW = 50;   // the amp row's toggle width
    constexpr int  kStackH = 2 * ToggleSwitch::minHeight(kStackStyle);
    static_assert(kCardH >= 2 * kCardPadY + kToggleH + kRowGap + kStackH,
                  "kCardH leaves the DELAY card too little for its SYNC/PP stack: "
                  "the bat toggles get clipped and look dead");
    static_assert(kToggleH >= ToggleSwitch::minHeight(kCardToggleStyle),
                  "card toggle row is shorter than a ToggleSwitch can paint into: "
                  "the jewel gets clipped and the toggle looks dead");
    static_assert(kCardH >= 2 * kCardPadY + kToggleH + kRowGap + kKnobH,
                  "kCardH leaves the cards too little for a full knob row");
    static_assert(EffectsPanel::panelHeight >= 2 * kCardH + kCardGap + 2 * 10,
                  "panelHeight is shorter than the card grid it holds");
}

EffectsPanel::EffectsPanel()
{
    addAndMakeVisible(boostTgl);
    addAndMakeVisible(delayTgl);
    addAndMakeVisible(reverbTgl);
    addAndMakeVisible(delayPP);
    addAndMakeVisible(delaySyncTgl);

    // Jewels: small lit lamps beside the card title, the same glass as the
    // faceplate's pilot lamp — see kCardToggleStyle, which the layout sizes against.
    for (auto* t : { &boostTgl, &delayTgl, &reverbTgl })
        t->setStyle(kCardToggleStyle);
    for (auto* t : { &delaySyncTgl, &delayPP })
        t->setStyle(kStackStyle);

    for (auto* k : { &gateThresh, &width, &inputLevel, &outputLevel,
                     &boostLevel, &boostTight, &boostBright,
                     &delayTime, &delayFb, &delayMix, &delayTone,
                     &reverbMix, &reverbSize, &reverbTone })
    {
        // Same knob as the amp row, smaller, so the panel reads as part of
        // the same piece of hardware.
        k->setScale(kKnobScale);
        k->setLabelOnTop(true);
        k->setEngravedReadout(true);
        k->getSlider().getProperties().set(SilentRiotLookAndFeel::kPointerKnobProperty, true);
        addAndMakeVisible(*k);
    }

    // Readouts in real units where the parameter has one. TIME's readout (ms,
    // or the division while SYNC is on) is set by DelayTimeAttachment, which
    // knows which of its two parameters the knob is showing.
    auto percent = [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; };
    delayFb.setValueFormatter(percent);
    delayMix.setValueFormatter(percent);
    reverbMix.setValueFormatter(percent);
}

static juce::Rectangle<int> cardBounds(juce::Rectangle<int> area, int col, int row)
{
    const int gridW = kCardW * 2 + kCardGap;
    const int gridH = kCardH * 2 + kCardGap;
    const int ox = area.getCentreX() - gridW / 2;
    const int oy = area.getCentreY() - gridH / 2;
    return { ox + col * (kCardW + kCardGap),
             oy + row * (kCardH + kCardGap),
             kCardW, kCardH };
}

void EffectsPanel::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().reduced(10);

    static const char* titles[4] = { "UTILITY", "BOOST", "DELAY", "REVERB" };
    int idx = 0;
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 2; ++col, ++idx)
        {
            auto card = cardBounds(area, col, row);
            AmpFaceplate::paintPlate(g, card.toFloat().reduced(0.5f), 5.0f);

            // Centred on the toggle row, so the title and the jewels share a line
            g.setColour(Colours::accent);
            g.setFont(Fonts::spaceMono(Fonts::Sizes::title, true));
            g.drawText(titles[idx],
                       card.reduced(12, kCardPadY).removeFromTop(kToggleH),
                       juce::Justification::centredLeft, false);
        }
}

void EffectsPanel::layoutKnobRow(juce::Rectangle<int> row, std::initializer_list<NotchKnob*> knobs)
{
    const int n = static_cast<int>(knobs.size());
    if (n == 0) return;
    const int knobW = juce::jmin(kKnobW, row.getWidth() / n);
    const int totalW = knobW * n;
    int x = row.getCentreX() - totalW / 2;
    // Never give a knob less than it needs to draw its name and value; centre
    // it when the card has room to spare.
    const int h = juce::jmin(row.getHeight(), kKnobH);
    const int y = row.getY() + (row.getHeight() - h) / 2;  // vertically centred
    for (auto* k : knobs)
    {
        k->setBounds(x, y, knobW, h);
        x += knobW;
    }
}

void EffectsPanel::layoutCard(juce::Rectangle<int> card, ToggleSwitch& tgl,
                              std::initializer_list<NotchKnob*> knobs)
{
    // Title row at the top; the toggle sits top-right of the card. The jewel
    // paints flush with its component's right edge, so every card's jewel
    // lines up on the same vertical.
    auto inner = card.reduced(10, kCardPadY);
    auto titleRow = inner.removeFromTop(kToggleH);
    tgl.setBounds(titleRow.removeFromRight(kToggleW));

    inner.removeFromTop(kRowGap);
    layoutKnobRow(inner, knobs);
}

void EffectsPanel::resized()
{
    auto area = getLocalBounds().reduced(10);

    // UTILITY card: no toggle — every knob self-gates or is unity-at-centre
    // (THRESH 0 = gate off, WIDTH 0 = dry, INPUT/OUTPUT centre = unity).
    // Four kKnobW knobs fill 280 of the card's 308 px; a fifth would squeeze
    // them below kKnobW, so check the names still fit if you add one.
    {
        auto inner = cardBounds(area, 0, 0).reduced(10, kCardPadY);
        inner.removeFromTop(kToggleH);  // title row ("UTILITY") — no toggle, but
                                        // reserve the same height so this card's
                                        // knobs line up with the others'
        inner.removeFromTop(kRowGap);
        layoutKnobRow(inner, { &gateThresh, &width, &inputLevel, &outputLevel });
    }
    layoutCard(cardBounds(area, 1, 0), boostTgl,  { &boostLevel, &boostTight, &boostBright });
    layoutCard(cardBounds(area, 1, 1), reverbTgl, { &reverbMix, &reverbSize, &reverbTone });

    // DELAY card: ON in the title row like the others; SYNC over PP as bat
    // toggles down the left, just before TIME (SYNC turns TIME into the
    // division selector). The four knobs share what is left of the row.
    auto delayCard  = cardBounds(area, 0, 1);
    auto delayInner = delayCard.reduced(10, kCardPadY);
    auto titleRow   = delayInner.removeFromTop(kToggleH);
    delayTgl.setBounds(titleRow.removeFromRight(kToggleW));

    delayInner.removeFromTop(kRowGap);
    auto stack = delayInner.removeFromLeft(kStackW)
                           .withSizeKeepingCentre(kStackW, kStackH);
    delaySyncTgl.setBounds(stack.removeFromTop(kStackH / 2));
    delayPP.setBounds(stack);
    delayInner.removeFromLeft(4);
    layoutKnobRow(delayInner, { &delayTime, &delayFb, &delayMix, &delayTone });
}

} // namespace sr
