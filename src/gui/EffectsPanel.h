#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "NotchKnob.h"
#include "ToggleSwitch.h"
#include "SegmentedSelector.h"

namespace sr {

// Collapsible EFFECTS panel — 2×2 grid of effect "cards":
//   [ UTILITY ] [ BOOST ]    UTILITY = noise gate + stereo width, both
//   [ DELAY ]   [ REVERB ]   autonomous (no toggle; 0 = off/mono).
// Owns the controls; the editor creates APVTS attachments via the accessors.
class EffectsPanel : public juce::Component {
public:
    EffectsPanel();
    void paint(juce::Graphics&) override;
    void resized() override;

    // Two card rows tall enough to fully render NotchKnob
    // (NotchKnob::preferredHeight) plus each card's title strip — and, in the
    // DELAY card, its extra sync-division row. That card is the binding
    // constraint: at the old 320 its knob row came out 21px short and the knob
    // names were drawn past the bottom edge, so they never appeared.
    static constexpr int panelHeight = 352;

    // Toggles (the UTILITY card's gate/width knobs self-gate — no toggle)
    ToggleSwitch& boostToggle()  { return boostTgl; }
    ToggleSwitch& delayToggle()  { return delayTgl; }
    ToggleSwitch& reverbToggle() { return reverbTgl; }
    ToggleSwitch& delayPingPongToggle() { return delayPP; }

    // Knobs
    NotchKnob& gateThreshKnob()  { return gateThresh; }
    NotchKnob& widthKnob()       { return width; }
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

    // Sync division selector — index 0 ("MANUAL") means the TIME knob drives
    // delay time; any other index syncs to host tempo (see TempoSync.h, whose
    // kSyncDivisions order this list must match).
    SegmentedSelector& delaySyncSelector() { return delaySync; }

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

    NotchKnob gateThresh { "THRESH", 0.0f };
    NotchKnob width      { "WIDTH",  0.0f };   // 0=off (dry/mono), 10=wide
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

    SegmentedSelector delaySync { { "MANUAL", "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2" }, 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsPanel)
};

} // namespace sr
