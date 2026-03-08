#pragma once
#include "PluginProcessor.h"
#include "gui/LookAndFeel.h"
#include "gui/AmpKnob.h"

class TinyTerrorEditor : public juce::AudioProcessorEditor {
public:
    explicit TinyTerrorEditor(TinyTerrorProcessor&);
    ~TinyTerrorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    TinyTerrorProcessor& proc;
    tt::OrangeLookAndFeel lnf;
    tt::AmpKnob gainKnob{"GAIN"}, toneKnob{"TONE"}, volumeKnob{"VOLUME"};
    juce::ToggleButton powerModeBtn{"7W"}, cabSimBtn{"CAB"};
    juce::ComboBox oversampleBox;
    using SliderA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboA  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderA> gainA, toneA, volA;
    std::unique_ptr<ButtonA> powerA, cabA;
    std::unique_ptr<ComboA>  osA;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TinyTerrorEditor)
};
