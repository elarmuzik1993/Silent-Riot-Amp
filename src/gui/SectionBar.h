#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace tt {

class SectionBar : public juce::Component {
public:
    explicit SectionBar(const juce::String& label);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    bool isOpen() const { return open; }
    void setOpen(bool shouldOpen);
    void setHintText(const juce::String& hint) { hintText = hint; repaint(); }

    std::function<void(bool)> onToggle;

private:
    juce::String label;
    juce::String hintText;
    bool open = false;
    float chevronAngle = 0.0f; // 0=closed, 180=open (animated)

    static constexpr float barHeight = 26.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SectionBar)
};

} // namespace tt
