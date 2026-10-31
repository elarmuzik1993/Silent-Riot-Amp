// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "NotchKnob.h"
#include "ToggleSwitch.h"

namespace sr {

// Collapsible EFFECTS panel — one row of effect "cards":
//   [ BOOST ] [ DELAY ] [ REVERB ]
// The gate, width and INPUT/OUTPUT trims are not here: they sit beside the
// level meters, in the editor (the old UTILITY card made the panel two rows
// tall). Owns the controls; the editor creates APVTS attachments via the
// accessors.
class EffectsPanel : public juce::Component {
public:
    EffectsPanel();
    void paint(juce::Graphics&) override;
    void resized() override;

    // One card row: a jewel-toggle title row over the tallest thing a card
    // holds — DELAY's SYNC/PP bat-toggle stack, which out-measures the scaled
    // pointer knobs (kKnobH): kCardH + the 10 px margin either side. Kept in sync with
    // the card metrics in EffectsPanel.cpp, which static_assert it — a
    // too-short panel shows up as knob values drawn past the bottom edge and
    // silently clipped.
    static constexpr int panelHeight = 156;

    // Styles a knob as an EFFECTS knob: the amp row's satin pointer knob at
    // the cards' scale, name on top, engraved value, handling wear from
    // `wearSeed`. The editor uses it for the gate/width/trim knobs beside the
    // meters, so they match the cards.
    static void styleKnob(NotchKnob& knob, int wearSeed);
    static constexpr float knobScale = 0.7f;   // the scale styleKnob() sets

    // Toggles
    ToggleSwitch& boostToggle()  { return boostTgl; }
    ToggleSwitch& delayToggle()  { return delayTgl; }
    ToggleSwitch& reverbToggle() { return reverbTgl; }
    ToggleSwitch& delayPingPongToggle() { return delayPP; }
    // SYNC: off = TIME sets the delay in ms; on = TIME steps through the tempo
    // divisions. One knob, two parameters — wired by DelayTimeAttachment in
    // the editor, not by a plain APVTS attachment.
    ToggleSwitch& delaySyncToggle() { return delaySyncTgl; }

    // Knobs
    NotchKnob& boostLevelKnob()  { return boostLevel; }
    NotchKnob& boostTightKnob()  { return boostTight; }
    NotchKnob& boostBrightKnob() { return boostBright; }
    NotchKnob& delayTimeKnob()   { return delayTime; }
    NotchKnob& delayFbKnob()     { return delayFb; }
    NotchKnob& delayMixKnob()    { return delayMix; }
    NotchKnob& delayToneKnob()   { return delayTone; }
    NotchKnob& reverbMixKnob()   { return reverbMix; }
    NotchKnob& reverbSizeKnob()  { return reverbSize; }
    NotchKnob& reverbToneKnob()  { return reverbTone; }

private:
    // Lay out N knobs centred in a horizontal row inside `row`.
    void layoutKnobRow(juce::Rectangle<int> row, std::initializer_list<NotchKnob*> knobs);
    // Lay out one card's title toggle + knob row inside `card`.
    void layoutCard(juce::Rectangle<int> card, ToggleSwitch& tgl,
                    std::initializer_list<NotchKnob*> knobs);

    ToggleSwitch boostTgl  { "ON" };
    ToggleSwitch delayTgl  { "ON" };
    ToggleSwitch reverbTgl { "ON" };
    ToggleSwitch delayPP   { "PP" };
    ToggleSwitch delaySyncTgl { "SYNC" };

    NotchKnob boostLevel { "LEVEL",  0.5f };
    NotchKnob boostTight { "TIGHT",  0.0f };
    NotchKnob boostBright{ "BRIGHT", 0.0f };
    NotchKnob delayTime  { "TIME",   0.337f };
    NotchKnob delayFb    { "FBK",    0.35f };
    NotchKnob delayMix   { "MIX",    0.3f };
    NotchKnob delayTone  { "TONE",   0.5f };
    NotchKnob reverbMix  { "MIX",    0.25f };
    NotchKnob reverbSize { "SIZE",   0.5f };
    NotchKnob reverbTone { "TONE",   0.5f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsPanel)
};

} // namespace sr
