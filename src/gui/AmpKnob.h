#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace sr {
class AmpKnob : public juce::Component {
public:
    explicit AmpKnob(const juce::String& label);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Slider& getSlider() { return slider; }
private:
    juce::Slider slider;
    juce::String label;
};
} // namespace sr
