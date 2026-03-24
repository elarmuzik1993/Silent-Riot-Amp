#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "IRWaveformDisplay.h"

namespace sr {

class IRLoaderPanel : public juce::Component,
                      public juce::FileDragAndDropTarget {
public:
    IRLoaderPanel();
    void paint(juce::Graphics&) override;
    void resized() override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragOver = false; repaint(); }

    void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void setSelectedIR(int index);
    int getSelectedIR() const { return selectedIRIndex; }

    // User IR list management
    void addUserIR(const juce::String& name, int samples, double sampleRate,
                   int irIndex, const juce::AudioBuffer<float>& audioData);
    void clearIRList();

    std::function<void(int)> onIRSelected;
    std::function<void(const juce::File&)> onUserIRLoaded;

    static constexpr int panelHeight = 240;

private:
    void refreshWaveform();

    IRWaveformDisplay waveformDisplay;

    struct IREntry {
        juce::String name;
        int samples;
        double sampleRate;
        int irIndex; // maps to IR_INDEX param (1-based)
        juce::AudioBuffer<float> waveformData;
    };
    std::vector<IREntry> irList;
    int selectedIRIndex = 0;
    int scrollOffset = 0;
    bool dragOver = false;

    static constexpr int listRowH = 20;
    static constexpr int listVisibleRows = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRLoaderPanel)
};

} // namespace sr
