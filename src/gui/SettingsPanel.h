// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SegmentedSelector.h"
#include "ToggleSwitch.h"
#include "InputSelector.h"

#include <array>
#include <memory>

namespace sr {

// Settings overlay, opened from the header's gear. Two columns on one row grid:
//
//   SETTINGS                                            X
//   AUDIO INPUT  (standalone only, full width)
//   DISPLAY                     METERS & KNOBS
//   Theme / UI scale / Tooltips Ballistics / Peak hold / Knob mode
//   ABOUT        name + version, format, developer      licence pill
//
// Every rectangle comes from layout(), which both resized() and paint() read,
// so labels and controls cannot drift apart. The editor places the panel inside
// its scaled content area, so it follows the UI scale like the rest of the UI.
class SettingsPanel : public juce::Component {
public:
    SettingsPanel();
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    // ── Metrics (unscaled px) ──
    static constexpr int kPad        = 20;  // outer padding
    static constexpr int kTitleH     = 24;  // SETTINGS + close button row
    static constexpr int kTitleGap   = 14;  // title row to the first section head
    static constexpr int kHeadH      = 16;  // section head
    static constexpr int kHeadGap    =  8;  // section head to its first row
    static constexpr int kRowPitch   = 34;  // one settings row
    static constexpr int kControlH   = 26;  // selector / toggle height inside a row
    static constexpr int kDividerGap = 13;  // space either side of a 1px divider
    static constexpr int kDividerH   = 2 * kDividerGap + 1;
    static constexpr int kGutter     = 32;  // between the two columns
    static constexpr int kLabelGap   = 14;  // row label to its control
    static constexpr int kAboutH     = 34;  // two ABOUT text lines / the licence pill
    static constexpr int kCloseSize  = 28;  // close button hit target
    static constexpr int kRowsPerColumn = 3;

    // Authoritative overlay dimensions (single source of truth — the editor
    // positions the panel using these instead of hardcoded literals).
    static constexpr int panelW = 560;
    static constexpr int panelH = kPad + kTitleH + kTitleGap
                                + kHeadH + kHeadGap + kRowsPerColumn * kRowPitch
                                + kDividerH
                                + kHeadH + kHeadGap + kAboutH
                                + kPad;
    // Extra height of the AUDIO INPUT section (standalone only).
    static constexpr int kInputSectionH = kHeadH + kHeadGap + kRowPitch + kDividerH;

    static_assert(kControlH <= kRowPitch, "a control must fit its row");
    static_assert(ToggleSwitch::minHeight(ToggleSwitch::Style::Jewel) <= kControlH,
                  "the jewel toggles would clip (see ToggleSwitch::minHeight)");

    // Standalone only: adds the AUDIO INPUT section (an InputSelector on the
    // app's device manager) above the preferences. A second call is a no-op.
    void attachInputSelector(juce::AudioDeviceManager& deviceManager);
    bool hasInputSelector() const { return inputSel != nullptr; }
    InputSelector* inputSelector() const { return inputSel.get(); }

    // Height the editor gives the overlay: panelH, plus the AUDIO INPUT section
    // when it is attached.
    int panelHeight() const { return panelH + (hasInputSelector() ? kInputSectionH : 0); }

    // ── Layout ──
    struct Row {
        juce::String label;
        juce::Rectangle<int> labelArea, control;
    };
    struct Layout {
        juce::Rectangle<int> title, close;
        juce::Rectangle<int> inputHead, inputDivider;   // empty without AUDIO INPUT
        Row input;
        juce::Rectangle<int> leftHead, rightHead;
        std::array<Row, kRowsPerColumn> left, right;
        juce::Rectangle<int> aboutDivider, aboutHead, aboutName, aboutDetail, licence;
    };
    // Every rectangle for the panel's current width and sections.
    Layout layout() const;
    // Width of the row-label column: the widest label in the row font, plus kLabelGap.
    static int labelColumnWidth();
    static juce::Font rowLabelFont();

    // Settings state
    int  themeIndex = 0;        // 0=Dark, 1=Light
    int  meterBallistics = 1;   // 0=Fast, 1=Medium, 2=Slow
    int  knobMode = 0;          // 0=Linear, 1=Rotary
    int  uiScale = 1;           // 0=75%, 1=100%, 2=125%, 3=150%
    bool peakHold = true;
    bool tooltipsEnabled = true;

    // Push current state into the controls (after restoring from saved state)
    void syncControls();

    juce::Button& closeButton() { return closeBtn; }

    std::function<void()> onClose;
    std::function<void(int)> onScaleChanged;
    std::function<void()> onSettingsChanged;  // theme/ballistics/knob/peak/tooltips

private:
    // Drawn X (a path, not the letter) with a hover tint. A juce::Button, so
    // it is focusable and Return/Space and accessibility press it.
    class CloseButton : public juce::Button {
    public:
        CloseButton() : juce::Button("Close settings") { setTooltip("Close (Esc)"); }
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };

    SegmentedSelector themeSelector     { {"DARK", "LIGHT"}, 0 };
    SegmentedSelector scaleSelector     { {"75%", "100%", "125%", "150%"}, 1 };
    ToggleSwitch tooltipsToggle         { "Tooltips" };
    SegmentedSelector ballisticsSelector{ {"FAST", "MED", "SLOW"}, 1 };
    ToggleSwitch peakHoldToggle         { "Peak hold" };
    SegmentedSelector knobModeSelector  { {"LINEAR", "ROTARY"}, 0 };
    CloseButton closeBtn;
    std::unique_ptr<InputSelector> inputSel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};

} // namespace sr
