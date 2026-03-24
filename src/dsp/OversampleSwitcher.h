#pragma once
// Oversampling State Machine (Spec §5.1)
// Deferred crossfade: FadingOut → Reconfiguring → FadingIn
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>

namespace sr {

enum class OversampleTransition { Idle, FadingOut, Reconfiguring, FadingIn };

struct OversampleSwitcher {
    OversampleTransition state = OversampleTransition::Idle;
    int fadeLength  = 128;
    int fadeCounter = 0;
    int pendingMode = -1;
    int currentMode = 1;   // index into oversamplers: 0=2x, 1=4x, 2=8x

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;

    void init(int numChannels, int maxBlockSize);
    juce::dsp::Oversampling<float>* active();
    int activeFactor() const;
    void requestModeChange(int newMode);
    bool isTransitioning() const { return state != OversampleTransition::Idle; }
    bool readyToSwitch() const { return state == OversampleTransition::Reconfiguring; }
    void commitSwitch();
    void applyTransitionFade(juce::AudioBuffer<float>& buffer);
};

} // namespace sr
