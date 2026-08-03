#include "EffectsPanel.h"
#include "ColourPalette.h"
#include "FontData.h"
#include "../utils/Constants.h"

namespace sr {

EffectsPanel::EffectsPanel()
{
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

    // Readouts in real units where the parameter has one. TIME mirrors the
    // 0–1 → ms mapping the processor applies (see PluginProcessor::processBlock),
    // so the display cannot drift from what the delay line actually does.
    // Note: with DELAY SYNC set to anything but MANUAL the host tempo drives
    // delay time and this knob is inactive — the readout still shows its own value.
    delayTime.setValueFormatter([](float v) {
        float ms = DELAY_TIME_MIN_MS + v * (DELAY_TIME_MAX_MS - DELAY_TIME_MIN_MS);
        return juce::String(juce::roundToInt(ms)) + "ms";
    });

    auto percent = [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; };
    delayFb.setValueFormatter(percent);
    delayMix.setValueFormatter(percent);
    reverbMix.setValueFormatter(percent);
}

// Card chrome metrics, shared by paint() and resized(). Kept tight so the
// DELAY card can fit its sync row *and* a full-height knob row.
static constexpr int kCardPadY = 5;
static constexpr int kTitleH   = 15;
static constexpr int kRowGap   = 3;
static constexpr int kSyncH    = 17;

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
            g.setFont(Fonts::spaceMono(Fonts::Sizes::body, true));
            g.drawText(titles[idx],
                       card.reduced(10, kCardPadY).removeFromTop(kTitleH),
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
    // Never give a knob less than it needs to draw its name (see
    // NotchKnob::preferredHeight); centre it when the card has room to spare.
    const int h = juce::jmin(row.getHeight(), NotchKnob::preferredHeight);
    const int y = row.getY() + (row.getHeight() - h) / 2;
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
    auto inner = card.reduced(10, kCardPadY);
    auto titleRow = inner.removeFromTop(kTitleH);
    tgl.setBounds(titleRow.removeFromRight(44).withSizeKeepingCentre(44, kTitleH));

    inner.removeFromTop(kRowGap);
    layoutKnobRow(inner, knobs);
}

void EffectsPanel::resized()
{
    auto area = getLocalBounds().reduced(10);

    // UTILITY card: no toggle — both knobs self-gate (THRESH 0 = gate off,
    // WIDTH centre = unity). Lay the title strip, then the two knobs.
    {
        auto inner = cardBounds(area, 0, 0).reduced(10, kCardPadY);
        inner.removeFromTop(kTitleH);   // title strip ("UTILITY")
        inner.removeFromTop(kRowGap);
        layoutKnobRow(inner, { &gateThresh, &width });
    }
    layoutCard(cardBounds(area, 1, 0), boostTgl,  { &boostLevel, &boostTight, &boostBright });
    layoutCard(cardBounds(area, 1, 1), reverbTgl, { &reverbMix, &reverbSize, &reverbTone });

    // DELAY card is bespoke: a second small toggle (PING-PONG) shares the
    // title row with ON, and a sync-division row sits above the knobs. That
    // extra row is why panelHeight is sized off this card — what's left here
    // must still clear NotchKnob::preferredHeight.
    auto delayCard  = cardBounds(area, 0, 1);
    auto delayInner = delayCard.reduced(10, kCardPadY);
    auto titleRow   = delayInner.removeFromTop(kTitleH);
    delayTgl.setBounds(titleRow.removeFromRight(40).withSizeKeepingCentre(40, kTitleH));
    titleRow.removeFromRight(4);
    delayPP.setBounds(titleRow.removeFromRight(40).withSizeKeepingCentre(40, kTitleH));

    delayInner.removeFromTop(kRowGap);
    delaySync.setBounds(delayInner.removeFromTop(kSyncH));

    delayInner.removeFromTop(kRowGap);
    layoutKnobRow(delayInner, { &delayTime, &delayFb, &delayMix, &delayTone });
}

} // namespace sr
