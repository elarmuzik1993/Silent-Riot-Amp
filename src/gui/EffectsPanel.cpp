#include "EffectsPanel.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace sr {

EffectsPanel::EffectsPanel()
{
    addAndMakeVisible(gateTgl);
    addAndMakeVisible(boostTgl);
    addAndMakeVisible(delayTgl);
    addAndMakeVisible(reverbTgl);
    addAndMakeVisible(delayPP);
    addAndMakeVisible(delaySync);
    delaySync.setTitleText("Delay sync");

    for (auto* k : { &gateThresh, &width, &boostLevel, &boostTight, &boostBright,
                     &delayTime, &delayFb, &delayMix, &delayTone,
                     &reverbMix, &reverbSize, &reverbTone })
        addAndMakeVisible(*k);
}

// Card geometry — kept in sync between paint() and resized().
static juce::Rectangle<int> cardBounds(juce::Rectangle<int> area, int col, int row)
{
    const int gap = 10;
    const int cardW = (area.getWidth() - gap) / 2;
    const int cardH = (area.getHeight() - gap) / 2;
    return { area.getX() + col * (cardW + gap),
             area.getY() + row * (cardH + gap),
             cardW, cardH };
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
    const int knobW = juce::jmin(80, row.getWidth() / n);
    const int totalW = knobW * n;
    int x = row.getCentreX() - totalW / 2;
    const int y = row.getY();
    const int h = juce::jmin(row.getHeight(), 112);
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

    // UTILITY card: noise gate (ON toggle + THRESH) plus the always-on WIDTH
    // knob. The ON toggle gates only the gate; WIDTH is neutral at centre.
    layoutCard(cardBounds(area, 0, 0), gateTgl,   { &gateThresh, &width });
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
