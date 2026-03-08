#include "OversampleSwitcher.h"

namespace tt {

void OversampleSwitcher::init(int numChannels, int maxBlockSize) {
    // 0=2x, 1=4x, 2=8x
    for (int i = 0; i < 3; ++i) {
        oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>>(
            numChannels, i + 1,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
        oversamplers[i]->initProcessing(static_cast<size_t>(maxBlockSize));
    }
    currentMode = 1; // default 4x
}

juce::dsp::Oversampling<float>* OversampleSwitcher::active() {
    return oversamplers[currentMode].get();
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

} // namespace tt
