// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "SettingsPanel.h"
#include "ColourPalette.h"
#include "FontData.h"

#include <cmath>

namespace sr {

namespace {
// Row labels, top to bottom. The controls are laid out in the same order.
const char* const kLeftLabels[]  = { "Theme", "UI scale", "Tooltips" };
const char* const kRightLabels[] = { "Ballistics", "Peak hold", "Knob mode" };
const char* const kInputLabel    = "Input";
} // namespace

SettingsPanel::SettingsPanel()
{
    addAndMakeVisible(themeSelector);
    addAndMakeVisible(scaleSelector);
    addAndMakeVisible(tooltipsToggle);
    addAndMakeVisible(ballisticsSelector);
    addAndMakeVisible(peakHoldToggle);
    addAndMakeVisible(knobModeSelector);
    addAndMakeVisible(closeBtn);

    // Jewel lamps, like the EFFECTS cards' ON. The panel paints the row label,
    // so each toggle spans its whole row (click the label or the lamp).
    for (auto* t : { &peakHoldToggle, &tooltipsToggle })
    {
        t->setStyle(ToggleSwitch::Style::Jewel);
        t->setShowLabel(false);
    }

    peakHoldToggle.getButton().setToggleState(true, juce::dontSendNotification);
    tooltipsToggle.getButton().setToggleState(true, juce::dontSendNotification);

    themeSelector.setTitleText("Theme");
    ballisticsSelector.setTitleText("Meter ballistics");
    knobModeSelector.setTitleText("Knob mode");
    scaleSelector.setTitleText("UI scale");
    setTitle("Settings");
    setWantsKeyboardFocus(true);

    auto fireChanged = [this] { if (onSettingsChanged) onSettingsChanged(); };

    themeSelector.onChange      = [this, fireChanged](int i) { themeIndex = i; fireChanged(); };
    ballisticsSelector.onChange = [this, fireChanged](int i) { meterBallistics = i; fireChanged(); };
    knobModeSelector.onChange   = [this, fireChanged](int i) { knobMode = i; fireChanged(); };
    scaleSelector.onChange      = [this](int i) {
        uiScale = i;
        if (onScaleChanged) onScaleChanged(i);
    };

    // ToggleSwitch animates via onStateChange internally — external listeners
    // must use onClick (see CLAUDE.md pitfall).
    peakHoldToggle.getButton().onClick = [this, fireChanged] {
        peakHold = peakHoldToggle.getButton().getToggleState();
        fireChanged();
    };
    tooltipsToggle.getButton().onClick = [this, fireChanged] {
        tooltipsEnabled = tooltipsToggle.getButton().getToggleState();
        fireChanged();
    };

    closeBtn.onClick = [this] { if (onClose) onClose(); };
}

void SettingsPanel::attachInputSelector(juce::AudioDeviceManager& deviceManager)
{
    if (inputSel != nullptr)
        return;
    inputSel = std::make_unique<InputSelector>(deviceManager);
    addAndMakeVisible(*inputSel);
    resized();
    repaint();
}

void SettingsPanel::syncControls()
{
    themeSelector.setSelectedIndex(themeIndex);
    ballisticsSelector.setSelectedIndex(meterBallistics);
    knobModeSelector.setSelectedIndex(knobMode);
    scaleSelector.setSelectedIndex(uiScale);
    peakHoldToggle.getButton().setToggleState(peakHold, juce::dontSendNotification);
    tooltipsToggle.getButton().setToggleState(tooltipsEnabled, juce::dontSendNotification);
}

bool SettingsPanel::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose) onClose();
        return true;
    }
    return false;
}

// ── Layout ───────────────────────────────────────────────────────────────

juce::Font SettingsPanel::rowLabelFont()
{
    return Fonts::spaceMono(Fonts::Sizes::body);
}

int SettingsPanel::labelColumnWidth()
{
    const auto font = rowLabelFont();
    float widest = font.getStringWidthFloat(kInputLabel);
    for (auto* l : kLeftLabels)  widest = juce::jmax(widest, font.getStringWidthFloat(l));
    for (auto* l : kRightLabels) widest = juce::jmax(widest, font.getStringWidthFloat(l));
    return static_cast<int>(std::ceil(widest)) + kLabelGap;
}

SettingsPanel::Layout SettingsPanel::layout() const
{
    Layout L;
    const int innerW = getWidth() - 2 * kPad;
    const int colW   = (innerW - kGutter) / 2;
    const int labelW = labelColumnWidth();
    const int leftX  = kPad;
    const int rightX = getWidth() - kPad - colW;

    // A row: label on the left, control centred vertically in the rest.
    const auto makeRow = [&](const juce::String& label, int x, int w, int y) {
        Row r;
        r.label     = label;
        r.labelArea = { x, y, labelW, kRowPitch };
        r.control   = { x + labelW, y + (kRowPitch - kControlH) / 2, w - labelW, kControlH };
        return r;
    };

    L.title = { kPad, kPad, innerW - kCloseSize, kTitleH };
    L.close = { getWidth() - kPad - kCloseSize + 6, kPad + (kTitleH - kCloseSize) / 2,
                kCloseSize, kCloseSize };
    int y = kPad + kTitleH + kTitleGap;

    if (hasInputSelector())
    {
        L.inputHead = { kPad, y, innerW, kHeadH };
        y += kHeadH + kHeadGap;
        L.input = makeRow(kInputLabel, kPad, innerW, y);
        y += kRowPitch;
        L.inputDivider = { kPad, y + kDividerGap, innerW, 1 };
        y += kDividerH;
    }

    L.leftHead  = { leftX,  y, colW, kHeadH };
    L.rightHead = { rightX, y, colW, kHeadH };
    y += kHeadH + kHeadGap;
    for (int i = 0; i < kRowsPerColumn; ++i)
    {
        L.left[static_cast<size_t>(i)]  = makeRow(kLeftLabels[i],  leftX,  colW, y + i * kRowPitch);
        L.right[static_cast<size_t>(i)] = makeRow(kRightLabels[i], rightX, colW, y + i * kRowPitch);
    }
    y += kRowsPerColumn * kRowPitch;

    L.aboutDivider = { kPad, y + kDividerGap, innerW, 1 };
    y += kDividerH;
    L.aboutHead = { kPad, y, innerW, kHeadH };
    y += kHeadH + kHeadGap;
    L.aboutName   = { leftX, y,      colW, kAboutH / 2 + 1 };
    L.aboutDetail = { leftX, y + kAboutH / 2 + 1, colW, kAboutH / 2 - 1 };
    L.licence     = { rightX, y + (kAboutH - 28) / 2, colW, 28 };
    return L;
}

// ── Painting ─────────────────────────────────────────────────────────────

void SettingsPanel::CloseButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat();
    if (highlighted || down)
    {
        g.setColour(Colours::borderHi.withAlpha(down ? 1.0f : 0.7f));
        g.fillRoundedRectangle(r.reduced(2.0f), 4.0f);
    }
    if (hasKeyboardFocus(false))
    {
        g.setColour(Colours::accent.withAlpha(0.7f));
        g.drawRoundedRectangle(r.reduced(2.5f), 4.0f, 1.5f);
    }
    const auto c = r.reduced(9.5f);
    juce::Path x;
    x.startNewSubPath(c.getTopLeft());     x.lineTo(c.getBottomRight());
    x.startNewSubPath(c.getTopRight());    x.lineTo(c.getBottomLeft());
    g.setColour(highlighted ? Colours::bright : Colours::label);
    g.strokePath(x, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded));
}

void SettingsPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto L = layout();

    // Drop shadow
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(bounds.translated(2.0f, 2.0f), 6.0f);

    // Panel background
    g.setColour(Colours::panel);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(Colours::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    g.setFont(Fonts::spaceMono(Fonts::Sizes::title, true));
    g.setColour(Colours::bright);
    g.drawText("SETTINGS", L.title, juce::Justification::centredLeft, false);

    const auto head = [&](juce::Rectangle<int> r, const juce::String& text) {
        g.setFont(Fonts::spaceMono(Fonts::Sizes::small, true));
        g.setColour(Colours::accent);
        g.drawText(text, r, juce::Justification::centredLeft, false);
    };
    const auto divider = [&](juce::Rectangle<int> r) {
        g.setColour(Colours::border);
        g.fillRect(r);
    };
    const auto rowLabel = [&](const Row& row) {
        g.setFont(rowLabelFont());
        g.setColour(Colours::label);
        g.drawText(row.label, row.labelArea, juce::Justification::centredLeft, false);
    };

    // ── AUDIO INPUT (standalone only) ──
    if (hasInputSelector())
    {
        head(L.inputHead, "AUDIO INPUT");
        rowLabel(L.input);
        divider(L.inputDivider);
    }

    // ── Preferences ──
    head(L.leftHead,  "DISPLAY");
    head(L.rightHead, "METERS & KNOBS");
    for (const auto& r : L.left)  rowLabel(r);
    for (const auto& r : L.right) rowLabel(r);

    // ── ABOUT ──
    divider(L.aboutDivider);
    head(L.aboutHead, "ABOUT");

    const auto nameFont = Fonts::spaceMono(Fonts::Sizes::body, true);
    g.setFont(nameFont);
    g.setColour(Colours::text);
    const juce::String product = "Silent Riot";
    g.drawText(product, L.aboutName, juce::Justification::centredLeft, false);
    const int versionX = static_cast<int>(std::ceil(nameFont.getStringWidthFloat(product))) + 10;
    g.setFont(Fonts::spaceMono(Fonts::Sizes::body));
    g.setColour(Colours::label);
    g.drawText("v" JucePlugin_VersionString, L.aboutName.withTrimmedLeft(versionX),
               juce::Justification::centredLeft, false);

    g.setFont(Fonts::spaceMono(Fonts::Sizes::small));
    g.setColour(Colours::label);
    g.drawText(juce::CharPointer_UTF8("VST3 / Standalone \xe2\x80\x94 Monolit Beatz"), L.aboutDetail,
               juce::Justification::centredLeft, false);

    // License: free and open source (GPLv3) — there is no activation
    const auto pill = L.licence.toFloat();
    g.setColour(Colours::surface);
    g.fillRoundedRectangle(pill, 4.0f);
    g.setColour(Colours::border);
    g.drawRoundedRectangle(pill.reduced(0.5f), 4.0f, 1.0f);
    g.setColour(Colours::green);
    g.fillEllipse(pill.getX() + 10.0f, pill.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setFont(Fonts::spaceMono(Fonts::Sizes::small));
    g.setColour(Colours::text);
    g.drawText(juce::CharPointer_UTF8("Free & open source \xe2\x80\x94 GPLv3"),
               pill.withTrimmedLeft(26.0f), juce::Justification::centredLeft, false);
}

void SettingsPanel::resized()
{
    const auto L = layout();

    closeBtn.setBounds(L.close);
    if (inputSel != nullptr)
        inputSel->setBounds(L.input.control);

    themeSelector.setBounds(L.left[0].control);
    scaleSelector.setBounds(L.left[1].control);
    ballisticsSelector.setBounds(L.right[0].control);
    knobModeSelector.setBounds(L.right[2].control);

    // A jewel paints its lamp at its right edge; spanning label + control
    // puts the lamp flush with the selectors' right edges.
    const auto wholeRow = [](const Row& r) { return r.labelArea.getUnion(r.control).withSizeKeepingCentre(
                                                 r.labelArea.getUnion(r.control).getWidth(), kControlH); };
    tooltipsToggle.setBounds(wholeRow(L.left[2]));
    peakHoldToggle.setBounds(wholeRow(L.right[1]));
}

} // namespace sr
