// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

class SegmentedSelector : public juce::Component {
public:
    SegmentedSelector(const juce::StringArray& options, int defaultIndex = 0);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(juce::Component::FocusChangeType) override;
    void focusLost(juce::Component::FocusChangeType) override;

    int getSelectedIndex() const { return selectedIndex; }
    void setSelectedIndex(int idx);
    void setTitleText(const juce::String& t);  // sets AX title prefix

    std::function<void(int)> onChange;

private:
    void selectIndex(int idx);  // change + notify
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    juce::StringArray options;
    int selectedIndex;
    juce::String titlePrefix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SegmentedSelector)
};

} // namespace sr
