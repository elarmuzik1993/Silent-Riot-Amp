// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sr {

// The IR LOADER's CAB view: a 2x12 cabinet front (tolex, plain black
// corner caps, orange piping, a black expanded-metal grille with the two
// speakers showing through) at 2:1, round arrow buttons either side of it
// that step the cab, and the name of what is playing underneath. Drawn in
// code so it follows the theme and the UI scale.
//
// It only displays and reports: IRLoaderPanel owns the IR list and decides
// what a step selects. File drops land on the panel (JUCE walks up to the
// nearest FileDragAndDropTarget), which mirrors its drag state here.
class CabinetView : public juce::Component {
public:
    CabinetView();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    // What the cab plays: its name, a dim detail line under it, and whether
    // there is anything to step to (with nothing loaded the arrows go dim).
    void setSelection(const juce::String& name, const juce::String& detail, bool canStep);
    void setDragOver(bool over);

    std::function<void(int)> onStep;   // -1 previous, +1 next

    // The cabinet front alone, filling `bounds`. The trim (frame, corners,
    // speakers) scales with the shorter side, so any width at least as wide
    // as it is tall reads as the same 2x12.
    static void paintCabinet(juce::Graphics&, juce::Rectangle<float> bounds);

    static constexpr float kCabAspect = 2.0f;    // a little wider than a real 2x12's 3:2
    static constexpr float kArrowR    = 15.0f;   // arrow button radius
    static constexpr float kArrowGap  = 48.0f;   // cab edge to arrow centre
    static constexpr float kNameH     = 36.0f;   // name + detail strip under the cab

private:
    juce::Rectangle<float> cabBounds() const;
    juce::Point<float> arrowCentre(int direction) const;
    int arrowAt(juce::Point<float> p) const;   // -1, +1, or 0 for neither

    juce::String name, detail;
    bool canStep = false;
    bool dragOver = false;
    int hoverArrow = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CabinetView)
};

} // namespace sr
