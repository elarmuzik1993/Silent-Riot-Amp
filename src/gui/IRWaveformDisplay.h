#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace sr {

class IRWaveformDisplay : public juce::Component {
public:
    IRWaveformDisplay();
    void paint(juce::Graphics&) override;

    void setIRData(const juce::AudioBuffer<float>& buffer, int sampleRate);
    void setIRInfo(const juce::String& name, float durationMs);
    void clear();

private:
    std::vector<float> waveformData;
    juce::String irName = "IR IMPULSE";
    float durationMs = 0.0f;
    bool hasData = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRWaveformDisplay)
};

} // namespace sr
