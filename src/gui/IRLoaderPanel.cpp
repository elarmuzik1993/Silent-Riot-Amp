// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "IRLoaderPanel.h"
#include "ColourPalette.h"
#include "FontData.h"

#include <algorithm>

namespace sr {

IRLoaderPanel::IRLoaderPanel()
{
    addChildComponent(waveformDisplay);
    addAndMakeVisible(cabView);
    addAndMakeVisible(viewSwitch);
    viewSwitch.setTitleText("IR view");
    viewSwitch.onChange = [this](int idx) {
        setView(idx == 0 ? View::Cab : View::Library);
        if (onViewChanged) onViewChanged(view);
    };
    cabView.onStep = [this](int delta) { stepIR(delta); };
    refreshCabView();
}

void IRLoaderPanel::setView(View v)
{
    view = v;
    viewSwitch.setSelectedIndex(view == View::Cab ? 0 : 1);
    cabView.setVisible(view == View::Cab);
    waveformDisplay.setVisible(view == View::Library);
    repaint();
}

void IRLoaderPanel::stepIR(int delta)
{
    // The cycle: the built-in cab (IR_INDEX 0), then the IRs in list order
    std::vector<int> cycle { 0 };
    for (auto& entry : irList)
        cycle.push_back(entry.irIndex);
    const int n = static_cast<int>(cycle.size());
    if (n < 2) return;

    const auto it = std::find(cycle.begin(), cycle.end(), selectedIRIndex);
    const int pos = it == cycle.end() ? 0 : static_cast<int>(it - cycle.begin());
    const int next = cycle[static_cast<size_t>(((pos + delta) % n + n) % n)];
    setSelectedIR(next);
    if (onIRSelected) onIRSelected(next);
}

void IRLoaderPanel::refreshCabView()
{
    const int numIRs = static_cast<int>(irList.size());
    for (size_t i = 0; i < irList.size(); ++i)
    {
        const auto& entry = irList[i];
        if (entry.irIndex != selectedIRIndex)
            continue;
        const int rateKHz = static_cast<int>(entry.sampleRate / 1000.0);
        cabView.setSelection(entry.name,
                             "IR " + juce::String(static_cast<int>(i) + 1) + " OF " + juce::String(numIRs)
                                 + "   " + juce::String(rateKHz) + " KHZ   "
                                 + juce::String(entry.samples) + " SMP",
                             true);
        return;
    }
    cabView.setSelection("BUILT-IN CAB",
                         numIRs == 0 ? juce::String("DROP A .WAV HERE OR LOAD IR TO ADD ONE")
                                     : "IIR CAB MODEL   " + juce::String(numIRs)
                                           + (numIRs == 1 ? " IR LOADED" : " IRS LOADED"),
                         numIRs > 0);
}

void IRLoaderPanel::setDragOver(bool over)
{
    dragOver = over;
    cabView.setDragOver(over);
    repaint();
}

// The CAB view's click-to-browse chip, left of the view switch
juce::Rectangle<float> IRLoaderPanel::loadButtonArea() const
{
    return viewSwitch.getBounds().toFloat().withWidth(72.0f).translated(-80.0f, 0.0f);
}

void IRLoaderPanel::openFileBrowser()
{
    auto chooser = std::make_shared<juce::FileChooser>("Load IR",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.wav;*.ir");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc) {
            auto result = fc.getResult();
            if (result.existsAsFile() && onUserIRLoaded)
                onUserIRLoaded(result);
        });
}

void IRLoaderPanel::addUserIR(const juce::String& name, int samples, double sampleRate,
                              int irIndex, const juce::AudioBuffer<float>& audioData)
{
    irList.push_back({ name, samples, sampleRate, irIndex,
                       juce::AudioBuffer<float>(audioData) });
    refreshCabView();
    repaint();
}

void IRLoaderPanel::clearIRList()
{
    irList.clear();
    selectedIRIndex = 0;
    scrollOffset = 0;
    waveformDisplay.clear();
    refreshCabView();
    repaint();
}

void IRLoaderPanel::setSelectedIR(int index)
{
    selectedIRIndex = index;
    refreshWaveform();
    refreshCabView();
    repaint();
}

void IRLoaderPanel::refreshWaveform()
{
    for (auto& entry : irList)
    {
        if (entry.irIndex == selectedIRIndex)
        {
            waveformDisplay.setIRData(entry.waveformData, entry.sampleRate);
            float durationMs = static_cast<float>(entry.samples)
                             / static_cast<float>(entry.sampleRate) * 1000.0f;
            waveformDisplay.setIRInfo(entry.name, durationMs);
            return;
        }
    }
    waveformDisplay.clear();
}

bool IRLoaderPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".ir"))
            return true;
    return false;
}

void IRLoaderPanel::filesDropped(const juce::StringArray& files, int, int)
{
    setDragOver(false);
    for (auto& f : files)
    {
        juce::File file(f);
        if (file.hasFileExtension("wav") || file.hasFileExtension("ir"))
        {
            if (onUserIRLoaded) onUserIRLoaded(file);
            break;
        }
    }
    repaint();
}

void IRLoaderPanel::mouseDown(const juce::MouseEvent& e)
{
    if (view == View::Cab)
    {
        if (loadButtonArea().contains(e.position))
            openFileBrowser();
        return;
    }

    auto bounds = getLocalBounds();
    float leftW = bounds.getWidth() * 0.45f;
    float listY = 34.0f;
    float listH = static_cast<float>(listVisibleRows * listRowH);

    // Check IR list click
    auto listArea = juce::Rectangle<float>(0.0f, listY, leftW, listH);
    if (listArea.contains(e.position))
    {
        int row = static_cast<int>((e.position.y - listY) / listRowH) + scrollOffset;
        if (row >= 0 && row < static_cast<int>(irList.size()))
        {
            selectedIRIndex = irList[static_cast<size_t>(row)].irIndex;
            refreshWaveform();
            repaint();
            if (onIRSelected) onIRSelected(selectedIRIndex);
        }
        return;
    }

    // Check drop zone click (browse)
    float dropY = listY + listH + 6.0f;
    auto dropArea = juce::Rectangle<float>(8.0f, dropY, leftW - 16.0f, 30.0f);
    if (dropArea.contains(e.position))
        openFileBrowser();
}

void IRLoaderPanel::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (view != View::Library) return;
    int maxScroll = std::max(0, static_cast<int>(irList.size()) - listVisibleRows);
    scrollOffset = juce::jlimit(0, maxScroll, scrollOffset - static_cast<int>(wheel.deltaY * 3.0f));
    repaint();
}

void IRLoaderPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.getHeight() < 20.0f) return;

    g.setColour(Colours::panelLight);
    g.fillRect(bounds);

    if (view == View::Cab)
    {
        g.setFont(Fonts::spaceMono(Fonts::Sizes::micro, true));
        g.setColour(Colours::dim);
        g.drawText("CABINET", juce::Rectangle<float>(8.0f, 5.0f, 200.0f, 20.0f),
                   juce::Justification::centredLeft, false);

        const auto load = loadButtonArea();
        g.setColour(dragOver ? Colours::accent.withAlpha(0.2f) : Colours::surface);
        g.fillRoundedRectangle(load, 4.0f);
        g.setColour(dragOver ? Colours::accent : Colours::border);
        g.drawRoundedRectangle(load, 4.0f, 1.0f);
        g.setFont(Fonts::spaceMono(Fonts::Sizes::small, true));
        g.setColour(Colours::label);
        g.drawText("LOAD IR", load, juce::Justification::centred, false);
        return;
    }

    float leftW = bounds.getWidth() * 0.45f;

    // ── Left column: IR list ──
    float y = 34.0f;
    float listH = static_cast<float>(listVisibleRows * listRowH);

    // Header
    g.setFont(Fonts::spaceMono(Fonts::Sizes::micro, true));
    g.setColour(Colours::dim);
    g.drawText("USER IMPULSE RESPONSES", juce::Rectangle<float>(8.0f, 6.0f, leftW - 16.0f, 14.0f),
               juce::Justification::centredLeft, false);

    // Column headers
    g.drawText("NAME", juce::Rectangle<float>(8.0f, y - 14.0f, leftW * 0.55f, 12.0f),
               juce::Justification::centredLeft, false);
    g.drawText("RATE", juce::Rectangle<float>(leftW * 0.58f, y - 14.0f, 50.0f, 12.0f),
               juce::Justification::centredLeft, false);
    g.drawText("SMP", juce::Rectangle<float>(leftW * 0.80f, y - 14.0f, 50.0f, 12.0f),
               juce::Justification::centredLeft, false);

    // IR rows
    if (irList.empty())
    {
        // Empty state
        g.setFont(Fonts::spaceMono(Fonts::Sizes::small));
        g.setColour(Colours::dim);
        g.drawText("No IRs loaded", juce::Rectangle<float>(8.0f, y, leftW - 16.0f, listH),
                   juce::Justification::centred, false);
    }
    else
    {
        for (int i = 0; i < listVisibleRows && (i + scrollOffset) < static_cast<int>(irList.size()); ++i)
        {
            int idx = i + scrollOffset;
            auto& entry = irList[static_cast<size_t>(idx)];
            float rowY = y + i * listRowH;
            bool selected = (entry.irIndex == selectedIRIndex);

            if (selected)
            {
                g.setColour(Colours::accent.withAlpha(0.12f));
                g.fillRect(0.0f, rowY, leftW, static_cast<float>(listRowH));
            }

            g.setFont(Fonts::spaceMono(Fonts::Sizes::small));
            g.setColour(selected ? Colours::bright : Colours::text);
            g.drawText(entry.name, juce::Rectangle<float>(8.0f, rowY, leftW * 0.52f, static_cast<float>(listRowH)),
                       juce::Justification::centredLeft, true);

            g.setColour(Colours::label);
            int rateKHz = static_cast<int>(entry.sampleRate / 1000.0);
            g.drawText(juce::String(rateKHz) + "k",
                       juce::Rectangle<float>(leftW * 0.58f, rowY, 50.0f, static_cast<float>(listRowH)),
                       juce::Justification::centredLeft, false);
            g.drawText(juce::String(entry.samples),
                       juce::Rectangle<float>(leftW * 0.80f, rowY, 50.0f, static_cast<float>(listRowH)),
                       juce::Justification::centredLeft, false);

            // Row separator
            g.setColour(Colours::border.withAlpha(0.3f));
            g.fillRect(8.0f, rowY + listRowH - 1.0f, leftW - 16.0f, 0.5f);
        }
    }

    // Drop zone
    float dropY = y + listH + 6.0f;
    auto dropArea = juce::Rectangle<float>(8.0f, dropY, leftW - 16.0f, 30.0f);
    g.setColour(dragOver ? Colours::accent.withAlpha(0.2f) : Colours::surface);
    g.fillRoundedRectangle(dropArea, 4.0f);
    g.setColour(dragOver ? Colours::accent : Colours::borderHi);

    // Dashed border (source and dest must be distinct paths)
    juce::Path dropOutline;
    dropOutline.addRoundedRectangle(dropArea, 4.0f);
    juce::Path dashed;
    const float dashes[] = { 4.0f, 4.0f };
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, dropOutline, dashes, 2);
    g.strokePath(dashed, juce::PathStrokeType(1.0f));

    g.setFont(Fonts::spaceMono(Fonts::Sizes::small));
    g.setColour(Colours::label);
    g.drawText("DROP .WAV OR CLICK TO BROWSE", dropArea, juce::Justification::centred, false);

    // ── Divider ──
    g.setColour(Colours::border);
    g.fillRect(leftW, 6.0f, 1.0f, bounds.getHeight() - 12.0f);

    // ── Right column: Detail grid ──
    float rx = leftW + 12.0f;
    float ry = static_cast<float>(headerH) + 82.0f;
    float rw = bounds.getWidth() - leftW - 24.0f;

    // Find selected entry
    const IREntry* selectedEntry = nullptr;
    for (auto& entry : irList)
        if (entry.irIndex == selectedIRIndex)
            selectedEntry = &entry;

    if (selectedEntry != nullptr)
    {
        struct Detail { const char* key; juce::String val; };
        float durationMs = static_cast<float>(selectedEntry->samples)
                         / static_cast<float>(selectedEntry->sampleRate) * 1000.0f;
        Detail details[] = {
            { "FILE",    selectedEntry->name },
            { "LENGTH",  juce::String(selectedEntry->samples) + " smp" },
            { "RATE",    juce::String(static_cast<int>(selectedEntry->sampleRate / 1000)) + " kHz" },
            { "LATENCY", juce::String(durationMs, 1) + " ms" },
        };

        for (auto& d : details)
        {
            g.setFont(Fonts::spaceMono(Fonts::Sizes::micro));
            g.setColour(Colours::label);
            g.drawText(d.key, juce::Rectangle<float>(rx, ry, 56.0f, 13.0f),
                       juce::Justification::centredLeft, false);
            g.setColour(Colours::text);
            g.drawText(d.val, juce::Rectangle<float>(rx + 56.0f, ry, rw - 56.0f, 13.0f),
                       juce::Justification::centredLeft, false);
            ry += 15.0f;
        }
    }
    else if (irList.empty())
    {
        g.setFont(Fonts::spaceMono(Fonts::Sizes::small));
        g.setColour(Colours::dim);
        g.drawText("Drop a .wav impulse response\nto get started",
                   juce::Rectangle<float>(rx, ry, rw, 60.0f),
                   juce::Justification::centred, true);
    }
}

void IRLoaderPanel::resized()
{
    float leftW = getWidth() * 0.45f;

    viewSwitch.setBounds(getWidth() - 8 - 140, 5, 140, 20);

    // Waveform display at top of right column, under the view switch
    float rx = leftW + 8.0f;
    waveformDisplay.setBounds(static_cast<int>(rx), headerH + 4,
                              static_cast<int>(getWidth() - rx - 8.0f), 72);

    cabView.setBounds(getLocalBounds().withTrimmedTop(headerH));
}

} // namespace sr
