// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "NotchKnob.h"
#include "ToggleSwitch.h"

#include <cstdlib>

namespace sr {

// MOCKUP ONLY (not for merge): SR_MOCKUP=A|B renders candidate layouts.
//   0 = today; A = trims+gate/width beside the meters; B = under the meters.
inline int layoutMockup()
{
    static const int m = [] {
        const char* e = std::getenv("SR_MOCKUP");
        return e == nullptr ? 0 : (e[0] == 'A' ? 1 : e[0] == 'B' ? 2 : 0);
    }();
    return m;
}

// Collapsible EFFECTS panel — 2×2 grid of effect "cards":
//   [ UTILITY ] [ BOOST ]    UTILITY = noise gate + stereo width, both
//   [ DELAY ]   [ REVERB ]   autonomous (no toggle; 0 = off/mono).
// Owns the controls; the editor creates APVTS attachments via the accessors.
class EffectsPanel : public juce::Component {
public:
    EffectsPanel();
    void paint(juce::Graphics&) override;
    void resized() override;

    // Two card rows, each a jewel-toggle title row over the tallest thing a
    // card holds — DELAY's SYNC/PP bat-toggle stack, which out-measures the
    // scaled pointer knobs (kKnobH):
    // 2 × kCardH + kCardGap + the 10 px margin either side. Kept in sync with
    // the card metrics in EffectsPanel.cpp, which static_assert it — a
    // too-short panel shows up as knob values drawn past the bottom edge and
    // silently clipped.
    static constexpr int panelHeight = 304;
    static constexpr int rowPanelHeight = 156;   // mockup: one row of three cards
    static int height() { return layoutMockup() ? rowPanelHeight : panelHeight; }

    // Toggles (the UTILITY card's gate/width knobs self-gate — no toggle)
    ToggleSwitch& boostToggle()  { return boostTgl; }
    ToggleSwitch& delayToggle()  { return delayTgl; }
    ToggleSwitch& reverbToggle() { return reverbTgl; }
    ToggleSwitch& delayPingPongToggle() { return delayPP; }
    // SYNC: off = TIME sets the delay in ms; on = TIME steps through the tempo
    // divisions. One knob, two parameters — wired by DelayTimeAttachment in
    // the editor, not by a plain APVTS attachment.
    ToggleSwitch& delaySyncToggle() { return delaySyncTgl; }

    // Knobs
    NotchKnob& gateThreshKnob()  { return gateThresh; }
    NotchKnob& widthKnob()       { return width; }
    NotchKnob& inputLevelKnob()  { return inputLevel; }
    NotchKnob& outputLevelKnob() { return outputLevel; }
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

    NotchKnob gateThresh { "THRESH", 0.0f };
    NotchKnob width      { "WIDTH",  0.0f };   // 0=off (dry/mono), 10=wide
    NotchKnob inputLevel { "INPUT",  0.5f };   // centre = unity, ±12 dB
    NotchKnob outputLevel{ "OUTPUT", 0.5f };   // centre = unity, silence..+12 dB
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
