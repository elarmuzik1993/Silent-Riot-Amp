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

    // Lamps, not pills: a pill per card is heavy chrome next to half-scale
    // knobs. The amp row keeps its switches — see kCardToggleStyle, which the
    // card layout sizes against.
    for (auto* t : { &boostTgl, &delayTgl, &reverbTgl, &delayPP })
        t->setStyle(ToggleSwitch::Style::Led);

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

    // Readouts in real units where the parameter has one. TIME mirrors the
    // 0–1 → ms mapping the processor applies (see PluginProcessor::processBlock),
    // so the display cannot drift from what the delay line actually does.
    // Note: with DELAY SYNC set to anything but MANUAL the host tempo drives
    // delay time and this knob is inactive — the readout still shows its own value.
    delayTime.setValueFormatter([](float v) {
        float ms = DELAY_TIME_MIN_MS + v * (DELAY_TIME_KNOB_MAX_MS - DELAY_TIME_MIN_MS);
        return juce::String(juce::roundToInt(ms)) + "ms";
    });

    auto percent = [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; };
    delayFb.setValueFormatter(percent);
    delayMix.setValueFormatter(percent);
    reverbMix.setValueFormatter(percent);
}

// Card geometry — kept in sync between paint() and resized(). Cards are a
// fixed compact size (sized to hug the half-scale knobs + the delay sync row)
// and the 2×2 grid is centred in the panel, rather than stretched to fill it —
// otherwise the small knobs float in oversized boxes.
namespace {
    constexpr int kCardGap = 12;
    constexpr int kCardW   = 290;  // floor set by the 8-division delay sync row
    constexpr int kCardH   = 126;  // title + (optional sync row) + one knob row

    // Card chrome metrics, shared by paint() and resized(). Kept tight so the
    // DELAY card can fit its sync row *and* a knob row inside kCardH.
    constexpr int kCardPadY = 5;
    constexpr int kTitleH   = 15;
    constexpr int kRowGap   = 3;
    constexpr int kSyncH    = 17;

    // Height reserved at the top of every card. This is NOT kTitleH: kTitleH
    // only has to fit the title *text*, while a ToggleSwitch also has to fit its
    // lamp below its own label, and JUCE clips the overflow away without a
    // warning — the toggle then looks like a dead text label while its clicks
    // still register. Every card reserves this much, toggle or not, so all four
    // knob rows stay on the same baseline.
    constexpr auto kCardToggleStyle = ToggleSwitch::Style::Led;
    constexpr int  kToggleH = ToggleSwitch::minHeight(kCardToggleStyle);
    static_assert(kToggleH >= ToggleSwitch::minHeight(kCardToggleStyle),
                  "card toggle row is shorter than a ToggleSwitch can paint into: "
                  "the lamp gets clipped and the toggle looks dead");
    static_assert(kCardH >= 2 * kCardPadY + kToggleH + kRowGap + kSyncH + kRowGap + 72,
                  "kCardH leaves the DELAY card too little for a full knob row");
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
    // Effect knobs are sized at half the main amp knobs (100x120) so the
    // effects section reads as compact next to the amp controls.
    const int knobW = juce::jmin(50, row.getWidth() / n);
    const int totalW = knobW * n;
    int x = row.getCentreX() - totalW / 2;
    // Never give a knob less than it needs to draw its name; centre it when the
    // card has room to spare. 72 is the half-scale equivalent of
    // NotchKnob::preferredHeight (which is the figure for a full-size knob) —
    // go below it and the names are drawn past the bottom edge and vanish.
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
    auto inner = card.reduced(10, kCardPadY);
    auto titleRow = inner.removeFromTop(kToggleH);
    tgl.setBounds(titleRow.removeFromRight(44).withSizeKeepingCentre(44, kToggleH));

    inner.removeFromTop(kRowGap);
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
        auto inner = cardBounds(area, 0, 0).reduced(10, kCardPadY);
        inner.removeFromTop(kToggleH);  // title strip ("UTILITY") — no toggle, but
                                        // reserve the same height so this card's
                                        // knobs line up with the others'
        inner.removeFromTop(kRowGap);
        layoutKnobRow(inner, { &gateThresh, &width, &inputLevel, &outputLevel });
    }
    layoutCard(cardBounds(area, 1, 0), boostTgl,  { &boostLevel, &boostTight, &boostBright });
    layoutCard(cardBounds(area, 1, 1), reverbTgl, { &reverbMix, &reverbSize, &reverbTone });

    // DELAY card is bespoke: a second small toggle (PING-PONG) shares the
    // title row with ON, and a sync-division row sits above the knobs. That
    // extra row is why panelHeight is sized off this card — what's left here
    // must still clear NotchKnob::preferredHeight.
    auto delayCard  = cardBounds(area, 0, 1);
    auto delayInner = delayCard.reduced(10, kCardPadY);
    auto titleRow   = delayInner.removeFromTop(kToggleH);
    delayTgl.setBounds(titleRow.removeFromRight(40).withSizeKeepingCentre(40, kToggleH));
    titleRow.removeFromRight(4);
    delayPP.setBounds(titleRow.removeFromRight(40).withSizeKeepingCentre(40, kToggleH));

    delayInner.removeFromTop(kRowGap);
    delaySync.setBounds(delayInner.removeFromTop(kSyncH));

    delayInner.removeFromTop(kRowGap);
    layoutKnobRow(delayInner, { &delayTime, &delayFb, &delayMix, &delayTone });
}

} // namespace sr
