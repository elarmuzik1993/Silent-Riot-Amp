#include "OversampleSwitcher.h"

namespace sr {

void OversampleSwitcher::init(int numChannels, int maxBlockSize) {
    // 0=2x, 1=4x, 2=8x
    for (size_t i = 0; i < 3; ++i) {
        oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>>(
            numChannels, static_cast<int>(i) + 1,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
        oversamplers[i]->initProcessing(static_cast<size_t>(maxBlockSize));
    }
    currentMode = 1; // default 4x
}

juce::dsp::Oversampling<float>* OversampleSwitcher::active() {
    return oversamplers[static_cast<size_t>(currentMode)].get();
}

int OversampleSwitcher::activeFactor() const {
    return 1 << (currentMode + 1); // 0→2, 1→4, 2→8
}

void OversampleSwitcher::requestModeChange(int newMode) {
    if (newMode != currentMode && state == OversampleTransition::Idle) {
        pendingMode = newMode;
        state = OversampleTransition::FadingOut;
        fadeCounter = 0;
    }
}

void OversampleSwitcher::commitSwitch() {
    currentMode = pendingMode;
    oversamplers[static_cast<size_t>(currentMode)]->reset();
    state = OversampleTransition::FadingIn;
    fadeCounter = 0;
}

void OversampleSwitcher::applyTransitionFade(juce::AudioBuffer<float>& buffer) {
    if (state == OversampleTransition::Idle) return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const float invFade = 1.0f / static_cast<float>(fadeLength);

    for (int i = 0; i < numSamples; ++i) {
        float gain = 1.0f;

        if (state == OversampleTransition::FadingOut) {
            if (fadeCounter < fadeLength) {
                gain = 1.0f - static_cast<float>(fadeCounter) * invFade;
                ++fadeCounter;
            } else {
                gain = 0.0f;
                state = OversampleTransition::Reconfiguring;
            }
        } else if (state == OversampleTransition::Reconfiguring) {
            gain = 0.0f;
        } else if (state == OversampleTransition::FadingIn) {
            if (fadeCounter < fadeLength) {
                gain = static_cast<float>(fadeCounter) * invFade;
                ++fadeCounter;
            } else {
                state = OversampleTransition::Idle;
                return; // rest of buffer at full gain
            }
        }

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer(ch)[i] *= gain;
    }
}

} // namespace sr
