// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "CabinetView.h"
#include "IRWaveformDisplay.h"
#include "SegmentedSelector.h"

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
    void fileDragEnter(const juce::StringArray&, int, int) override { setDragOver(true); }
    void fileDragExit(const juce::StringArray&) override { setDragOver(false); }

    void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Live output level / compression heat (0..1) for the cab's piping glow.
    void setDrive(float level, float heat) { cabView.setDrive(level, heat); }

    void setSelectedIR(int index);
    int getSelectedIR() const { return selectedIRIndex; }

    // CAB shows the 2x12 cabinet with arrows that step through the built-in
    // cab (IR_INDEX 0) and every loaded IR; LIBRARY is the list, waveform and
    // details. The editor persists the choice with the UI state.
    enum class View { Cab = 0, Library = 1 };
    void setView(View v);
    View getView() const { return view; }

    // Select the entry `delta` steps away in the CAB view's cycle (built-in
    // cab, then the loaded IRs in list order, wrapping), and report it
    // through onIRSelected. An index that is in neither starts from the
    // built-in cab.
    void stepIR(int delta);

    // User IR list management
    void addUserIR(const juce::String& name, int samples, double sampleRate,
                   int irIndex, const juce::AudioBuffer<float>& audioData);
    void clearIRList();

    std::function<void(int)> onIRSelected;
    std::function<void(const juce::File&)> onUserIRLoaded;
    std::function<void(View)> onViewChanged;

    static constexpr int panelHeight = 240;

private:
    void refreshWaveform();
    void refreshCabView();
    void setDragOver(bool over);
    void openFileBrowser();
    juce::Rectangle<float> loadButtonArea() const;

    IRWaveformDisplay waveformDisplay;
    CabinetView cabView;
    SegmentedSelector viewSwitch { { "CAB", "LIBRARY" }, 0 };
    View view = View::Cab;

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

    // CAB view's backdrop: the brick-wall photo, dimmed (washed pale on a light palette),
    // rendered once like the cab front and keyed on size, physical pixel scale
    // and a theme token. LIBRARY view keeps the plain panel, for its list text.
    juce::Image wallCache;
    float wallCacheScale = 0.0f;
    juce::uint32 wallCacheTheme = 0;

    static constexpr int headerH = 28;   // title + view switch row, both views
    static constexpr int listRowH = 20;
    static constexpr int listVisibleRows = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRLoaderPanel)
};

} // namespace sr
