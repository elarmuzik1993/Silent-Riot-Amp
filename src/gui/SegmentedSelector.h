#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

class SegmentedSelector : public juce::Component {
public:
    SegmentedSelector(const juce::StringArray& options, int defaultIndex = 0);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    int getSelectedIndex() const { return selectedIndex; }
    void setSelectedIndex(int idx);

    std::function<void(int)> onChange;

private:
    juce::StringArray options;
    int selectedIndex;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SegmentedSelector)
};

} // namespace sr
