#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../utils/Parameters.h"

namespace sr {

class PresetSelector : public juce::Component {
public:
    PresetSelector();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(juce::Component::FocusChangeType) override;
    void focusLost(juce::Component::FocusChangeType) override;

    int getPresetIndex() const { return currentIndex; }
    void setPresetIndex(int idx);

    std::function<void(int)> onPresetChanged;

private:
    void step(int delta);   // change preset by ±1 and notify
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    int currentIndex = 0;

    // Single source of truth: names come from the factory preset table.
    static constexpr int numPresets = sr::Presets::count;
    static const char* presetName(int i) { return sr::Presets::table[i].name; }

    static constexpr float arrowW = 28.0f;
    static constexpr float minCenterW = 140.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetSelector)
};

} // namespace sr
