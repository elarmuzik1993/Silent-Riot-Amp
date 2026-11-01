// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "EffectsPanel.h"
#include "AmpFaceplate.h"
#include "ColourPalette.h"
#include "FontData.h"
#include "LookAndFeel.h"
#include "../utils/Constants.h"

namespace sr {

// Card geometry — kept in sync between paint() and resized(). Each card hugs a
// title line and one knob row, and the row of three is centred in the panel,
// rather than stretched to fill it — otherwise the knobs float in oversized
// boxes.
//
// The title line sits on the card's top edge, like a legend: the plate starts
// half a line down, the title is printed on a chip of panel background across
// its edge, and the jewels sit on the same line. So a card is the title line,
// one knob row and a bottom pad.
namespace {
    constexpr int kCardGap  = 12;
    constexpr int kCardPadX = 10;
    constexpr int kCardPadB = 6;   // under the knob row
    constexpr int kPanelPadT = 10, kPanelPadB = 8;

    // The effect knobs are the amp row's satin pointer knobs (name on top, value
    // engraved below) drawn smaller, with tight dials (NotchKnob::setTightDial):
    // the name sits on the ticks and the value under the skirt. kKnobH holds
    // the text strip and the dial's painted height; under ~52 px the pointer
    // bar's chamfer turns to mush. The name keeps its full size at any scale.
    // kKnobW is what three cards in one row leave each of their ten knobs
    // inside the 680 px panel; the names still fit at body size.
    constexpr float kKnobScale = EffectsPanel::knobScale;
    constexpr int   kKnobW     = 54;
    constexpr int   kKnobH     = 66;
    constexpr int   kMinDial   = 52;
    static_assert(kKnobH >= NotchKnob::stripHeight(kKnobScale, true)
                            + NotchKnob::tightDialHeight(static_cast<float>(kKnobW)),
                  "effect knobs are too short for their name, dial and value");
    static_assert(kKnobW >= kMinDial, "effect knobs are too narrow for a readable dial");

    // The title line: the title, and the jewel toggles. A ToggleSwitch has to
    // fit its whole graphic (here the jewel's glow), and JUCE clips the
    // overflow away without a warning — the toggle then looks dead while its
    // clicks still register.
    constexpr auto kCardToggleStyle = ToggleSwitch::Style::Jewel;
    constexpr int  kToggleH = ToggleSwitch::minHeight(kCardToggleStyle);
    constexpr int  kToggleW = 40;  // a two-letter label + the jewel and its glow
    constexpr int  kSyncW   = 56;  // "SYNC" + the jewel
    constexpr int  kTitleW  = 64;  // room left for the widest title chip

    constexpr int kCardH = kToggleH + kKnobH + kCardPadB;
    static_assert(EffectsPanel::panelHeight >= kPanelPadT + kCardH + kPanelPadB,
                  "panelHeight is shorter than the card row it holds");

    // BOOST and REVERB hold three knobs; DELAY four, and three jewels on its title line.
    constexpr int kCardW[3] = { 2 * kCardPadX + 3 * kKnobW,
                                2 * kCardPadX + 4 * kKnobW + 20,
                                2 * kCardPadX + 3 * kKnobW };
    static_assert(kCardW[1] - 2 * kCardPadX >= kTitleW + kSyncW + 2 * kToggleW,
                  "DELAY's title line has no room for its title and SYNC/PP/ON");
    static_assert(kCardW[0] - 2 * kCardPadX >= kTitleW + kToggleW,
                  "a card's title line has no room for its title and ON");
    constexpr int kRowW = kCardW[0] + kCardW[1] + kCardW[2] + 2 * kCardGap;
    static_assert(kRowW <= 700 - 2 * 10,
                  "the three cards no longer fit the window's width in one row");
}

EffectsPanel::EffectsPanel()
{
    addAndMakeVisible(boostTgl);
    addAndMakeVisible(delayTgl);
    addAndMakeVisible(reverbTgl);
    addAndMakeVisible(delayPP);
    addAndMakeVisible(delaySyncTgl);

    // Jewels: small lit lamps on the card's title line, the same glass as the
    // faceplate's pilot lamp — see kCardToggleStyle, which the layout sizes
    // against. DELAY's SYNC and PP are jewels too, beside its ON: as bat
    // toggles they were the tallest thing in any card.
    for (auto* t : { &boostTgl, &delayTgl, &reverbTgl, &delaySyncTgl, &delayPP })
        t->setStyle(kCardToggleStyle);

    // Each knob wears differently (see kKnobWearProperty). Seeds 100..103
    // went to the gate/width/trim knobs, which the editor now owns and seeds
    // the same, so every knob keeps the wear it had.
    int wearSeed = 104;
    for (auto* k : { &boostLevel, &boostTight, &boostBright,
                     &delayTime, &delayFb, &delayMix, &delayTone,
                     &reverbMix, &reverbSize, &reverbTone })
    {
        styleKnob(*k, wearSeed++);
        k->setTightDial(true);   // name on the ticks, value under the skirt
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

void EffectsPanel::styleKnob(NotchKnob& k, int wearSeed)
{
    // Same knob as the amp row, smaller, so the panel reads as part of the
    // same piece of hardware.
    k.setScale(kKnobScale);
    k.setLabelOnTop(true);
    k.setEngravedReadout(true);
    k.getSlider().getProperties().set(SilentRiotLookAndFeel::kPointerKnobProperty, true);
    k.getSlider().getProperties().set(SilentRiotLookAndFeel::kKnobWearProperty, wearSeed);
}


// Card i of the row (0 BOOST, 1 DELAY, 2 REVERB), centred across the panel.
static juce::Rectangle<int> cardBounds(juce::Rectangle<int> panel, int i)
{
    int x = panel.getCentreX() - kRowW / 2;
    for (int j = 0; j < i; ++j)
        x += kCardW[j] + kCardGap;
    return { x, kPanelPadT, kCardW[i], kCardH };
}

void EffectsPanel::paint(juce::Graphics& g)
{
    static const char* titles[3] = { "BOOST", "DELAY", "REVERB" };
    g.setFont(Fonts::ui(Fonts::Sizes::small, true));
    for (int i = 0; i < 3; ++i)
    {
        const auto card = cardBounds(getLocalBounds(), i);
        AmpFaceplate::paintPlate(g, card.withTrimmedTop(kToggleH / 2).toFloat().reduced(0.5f), 5.0f);

        // The title on its top edge, on a chip of panel background
        const int tw = juce::roundToInt(g.getCurrentFont().getStringWidthFloat(titles[i])) + 10;
        const juce::Rectangle<int> chip(card.getX() + kCardPadX, card.getY() + 4, tw, kToggleH - 8);
        g.setColour(Colours::panel);
        g.fillRoundedRectangle(chip.toFloat(), 3.0f);
        g.setColour(Colours::accent);
        g.drawText(titles[i], chip, juce::Justification::centred, false);
    }
}

void EffectsPanel::layoutKnobRow(juce::Rectangle<int> row, std::initializer_list<NotchKnob*> knobs)
{
    const int n = static_cast<int>(knobs.size());
    if (n == 0) return;
    const int knobW = juce::jmin(kKnobW, row.getWidth() / n);
    int x = row.getCentreX() - knobW * n / 2;
    for (auto* k : knobs)
    {
        k->setBounds(x, row.getY(), knobW, row.getHeight());
        x += knobW;
    }
}

void EffectsPanel::layoutCard(juce::Rectangle<int> card, std::initializer_list<ToggleSwitch*> toggles,
                              std::initializer_list<NotchKnob*> knobs)
{
    // The title line on the card's top edge: the jewels right-aligned, listed
    // right to left (each jewel hugs its component's right edge, so every
    // card's ON lines up on the same vertical); the knob row under it.
    auto inner = card.reduced(kCardPadX, 0);
    auto titleLine = inner.removeFromTop(kToggleH);
    for (auto* t : toggles)
        t->setBounds(titleLine.removeFromRight(t == &delaySyncTgl ? kSyncW : kToggleW));

    layoutKnobRow(inner.withTrimmedBottom(kCardPadB), knobs);
}

void EffectsPanel::resized()
{
    const auto panel = getLocalBounds();
    layoutCard(cardBounds(panel, 0), { &boostTgl }, { &boostLevel, &boostTight, &boostBright });
    // DELAY: SYNC (TIME becomes the division selector) and PP beside ON.
    layoutCard(cardBounds(panel, 1), { &delayTgl, &delayPP, &delaySyncTgl },
               { &delayTime, &delayFb, &delayMix, &delayTone });
    layoutCard(cardBounds(panel, 2), { &reverbTgl }, { &reverbMix, &reverbSize, &reverbTone });
}

} // namespace sr
