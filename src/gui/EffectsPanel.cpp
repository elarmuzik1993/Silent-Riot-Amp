#include "EffectsPanel.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace sr {

EffectsPanel::EffectsPanel()
{
    addAndMakeVisible(boostTgl);
    addAndMakeVisible(delayTgl);
    addAndMakeVisible(reverbTgl);
    addAndMakeVisible(delayPP);
    addAndMakeVisible(delaySync);
    delaySync.setTitleText("Delay sync");

    for (auto* k : { &gateThresh, &width, &inputLevel, &outputLevel,
                     &boostLevel, &boostTight, &boostBright,
                     &delayTime, &delayFb, &delayMix, &delayTone,
                     &reverbMix, &reverbSize, &reverbTone })
    {
        // Effect knobs render at half the size of the main amp knobs so the
        // effects section reads as compact.
        k->setScale(0.5f);
        addAndMakeVisible(*k);
    }
}

// Card geometry — kept in sync between paint() and resized(). Cards are a
// fixed compact size (sized to hug the half-scale knobs + the delay sync row)
// and the 2×2 grid is centred in the panel, rather than stretched to fill it —
// otherwise the small knobs float in oversized boxes.
namespace {
    constexpr int kCardGap = 12;
    constexpr int kCardW   = 290;  // floor set by the 8-division delay sync row
    constexpr int kCardH   = 120;  // title + (optional sync row) + one knob row
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
            g.setColour(Colours::panel);
            g.fillRoundedRectangle(card.toFloat(), 5.0f);
            g.setColour(Colours::border);
            g.drawRoundedRectangle(card.toFloat(), 5.0f, 1.0f);

            g.setColour(Colours::accent);
            g.setFont(Fonts::spaceMono(10.0f, true));
            g.drawText(titles[idx],
                       card.reduced(10, 6).removeFromTop(14),
                       juce::Justification::centredLeft, false);
        }
}

void EffectsPanel::layoutKnobRow(juce::Rectangle<int> row, std::initializer_list<NotchKnob*> knobs)
{
    const int n = static_cast<int>(knobs.size());
    if (n == 0) return;
    // Effect knobs are sized at half the main amp knobs (100x120) so the
    // effects section reads as compact next to the amp controls.
    const int knobW = juce::jmin(50, row.getWidth() / n);
    const int totalW = knobW * n;
    int x = row.getCentreX() - totalW / 2;
    const int h = juce::jmin(row.getHeight(), 72);
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
    // Title strip at the top; toggle sits top-right of the card.
    auto inner = card.reduced(10, 6);
    auto titleRow = inner.removeFromTop(16);
    tgl.setBounds(titleRow.removeFromRight(44).withSizeKeepingCentre(44, 16));

    inner.removeFromTop(4);
    layoutKnobRow(inner, knobs);
}

void EffectsPanel::resized()
{
    auto area = getLocalBounds().reduced(10);

    // UTILITY card: no toggle — every knob self-gates or is unity-at-centre
    // (THRESH 0 = gate off, WIDTH 0 = dry, INPUT/OUTPUT centre = unity).
    // Four knobs take layoutKnobRow from 80 px to ~76 px each; that fits, but
    // check the labels render if you add a fifth.
    {
        auto inner = cardBounds(area, 0, 0).reduced(10, 6);
        inner.removeFromTop(16);   // title strip ("UTILITY")
        inner.removeFromTop(4);
        layoutKnobRow(inner, { &gateThresh, &width, &inputLevel, &outputLevel });
    }
    layoutCard(cardBounds(area, 1, 0), boostTgl,  { &boostLevel, &boostTight, &boostBright });
    layoutCard(cardBounds(area, 1, 1), reverbTgl, { &reverbMix, &reverbSize, &reverbTone });

    // DELAY card is bespoke: a second small toggle (PING-PONG) shares the
    // title row with ON, and a sync-division row sits above the knobs. The
    // knob row shrinks to fit (layoutKnobRow clamps to the available height).
    auto delayCard  = cardBounds(area, 0, 1);
    auto delayInner = delayCard.reduced(10, 6);
    auto titleRow   = delayInner.removeFromTop(16);
    delayTgl.setBounds(titleRow.removeFromRight(40).withSizeKeepingCentre(40, 16));
    titleRow.removeFromRight(4);
    delayPP.setBounds(titleRow.removeFromRight(40).withSizeKeepingCentre(40, 16));

    delayInner.removeFromTop(4);
    delaySync.setBounds(delayInner.removeFromTop(18));

    delayInner.removeFromTop(4);
    layoutKnobRow(delayInner, { &delayTime, &delayFb, &delayMix, &delayTone });
}

} // namespace sr
