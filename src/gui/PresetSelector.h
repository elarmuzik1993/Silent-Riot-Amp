#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace tt {

class PresetSelector : public juce::Component {
public:
    PresetSelector();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    int getPresetIndex() const { return currentIndex; }
    void setPresetIndex(int idx);

    std::function<void(int)> onPresetChanged;

private:
    int currentIndex = 0;

    static constexpr int numPresets = 5;
    static inline const juce::String presetNames[numPresets] = {
        "Classic Crunch",
        "Edge of Breakup",
        "Full Drive",
        "Clean Shimmer",
        "Heavy Sag"
    };

    static constexpr float arrowW = 28.0f;
    static constexpr float minCenterW = 140.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetSelector)
};

} // namespace tt
