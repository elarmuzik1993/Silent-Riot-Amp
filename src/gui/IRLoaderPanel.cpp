// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include "IRLoaderPanel.h"
#include "ColourPalette.h"
#include "FontData.h"
#include "ProceduralTexture.h"
#include "BinaryData.h"

#include <algorithm>
#include <cmath>

namespace sr {

// ── The CAB view's backdrop: a brick wall ──
//
// A photo of an old wall supplied by the owner (`resources/textures/brick-wall.jpg`,
// embedded via BinaryData; source to confirm and credit in attribution/CREDITS.md before
// the repo goes public), scaled to cover the panel and centred. A dark
// palette gets it dimmed to half with a vignette, so the cab, arrows and text
// stay the brightest things in the section; a light palette gets it washed
// pale. Either way it is quietened under the header and the cab's name lines,
// so small type stays legible.
namespace {

const juce::Image& wallPhoto()
{
    static const juce::Image photo = juce::ImageCache::getFromMemory(BinaryData::brickwall_jpg,
                                                                     BinaryData::brickwall_jpgSize);
    return photo;
}

// `calm` is where text sits on the wall (the cab's name lines)
juce::Image renderBrickWall(float w, float h, float headerH, juce::Rectangle<float> calm, float scale)
{
    using texture::smoothstep;
    const bool dark = Colours::panel.getPerceivedBrightness() < 0.5f;
    const int pw = juce::jmax(1, juce::roundToInt(w * scale));
    const int ph = juce::jmax(1, juce::roundToInt(h * scale));
    const float inv = 1.0f / scale;

    juce::Image img(juce::Image::RGB, pw, ph, true);
    {
        juce::Graphics g(img);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(wallPhoto(), juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(pw), static_cast<float>(ph)),
                    juce::RectanglePlacement(juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination));
    }

    const juce::Colour wash { 0xFFE6E2DC };
    {   // the pixel access is released before the image is returned
        juce::Image::BitmapData px(img, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < ph; ++y)
            for (int x = 0; x < pw; ++x)
            {
                const float u = static_cast<float>(x) * inv, v = static_cast<float>(y) * inv;

                // Quiet zones: a wide oval round the text (a box shows as a box),
                // and the header strip
                const float qx = (u - calm.getCentreX()) / (calm.getWidth() * 0.5f + 40.0f);
                const float qy = (v - calm.getCentreY()) / (calm.getHeight() * 0.5f + 16.0f);
                const float quiet = juce::jmax(smoothstep(1.0f, 0.45f, std::sqrt(qx * qx + qy * qy)),
                                               smoothstep(headerH + 4.0f, headerH - 6.0f, v));
                const float ex = (u - w * 0.5f) / (w * 0.5f), ey = (v - h * 0.55f) / (h * 0.55f);
                const float edge = smoothstep(0.35f, 1.25f, std::sqrt(ex * ex + ey * ey));

                juce::Colour c = px.getPixelColour(x, y);
                if (dark) // dimmed to half and vignetted, darkest under the text
                {
                    c = c.withMultipliedBrightness(0.50f * (1.0f - 0.55f * edge) * (1.0f - 0.5f * quiet));
                }
                else // washed pale, flattened toward the panel under the text
                {
                    c = c.interpolatedWith(wash, 0.62f + 0.08f * edge);
                    c = c.interpolatedWith(Colours::panelLight, 0.78f * quiet);
                }
                px.setPixelColour(x, y, c);
            }
    }
    return img;
}

} // namespace

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

// The CAB view's click-to-browse chip, at the header's left edge (where a
// "CABINET" caption used to sit — the cab itself says what the view is), on
// the view switch's row.
juce::Rectangle<float> IRLoaderPanel::loadButtonArea() const
{
    return viewSwitch.getBounds().toFloat().withX(8.0f).withWidth(72.0f);
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

    if (view == View::Cab)
    {
        // Rendered at the full panel height and drawn from the top, so the
        // open/close animation (which resizes this panel every frame) only blits it
        const float scale = juce::jmax(0.01f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const float fullH = static_cast<float>(panelHeight);
        const int w = juce::jmax(1, juce::roundToInt(bounds.getWidth() * scale));
        const int h = juce::jmax(1, juce::roundToInt(fullH * scale));
        const juce::uint32 theme = Colours::panel.getARGB() ^ (Colours::panelLight.getARGB() * 31u);
        if (wallCache.isNull() || wallCacheScale != scale || wallCacheTheme != theme
            || wallCache.getWidth() != w || wallCache.getHeight() != h)
        {
            // The cab's name lines: centred in the bottom kNameH of the panel
            const auto calm = juce::Rectangle<float>(bounds.getCentreX() - 190.0f, fullH - CabinetView::kNameH + 2.0f,
                                                     380.0f, CabinetView::kNameH - 4.0f);
            wallCache = renderBrickWall(bounds.getWidth(), fullH, static_cast<float>(headerH), calm, scale);
            wallCacheScale = scale;
            wallCacheTheme = theme;
        }
        g.drawImage(wallCache, juce::Rectangle<float>(0.0f, 0.0f, bounds.getWidth(), fullH));
    }
    else
    {
        g.setColour(Colours::panelLight);
        g.fillRect(bounds);
    }

    if (view == View::Cab)
    {
        const auto load = loadButtonArea();
        g.setColour(dragOver ? Colours::accent.withAlpha(0.2f) : Colours::surface);
        g.fillRoundedRectangle(load, 4.0f);
        g.setColour(dragOver ? Colours::accent : Colours::border);
        g.drawRoundedRectangle(load, 4.0f, 1.0f);
        g.setFont(Fonts::ui(Fonts::Sizes::small, true));
        g.setColour(Colours::label);
        g.drawText("LOAD IR", load, juce::Justification::centred, false);
        return;
    }

    float leftW = bounds.getWidth() * 0.45f;

    // ── Left column: IR list ──
    float y = 34.0f;
    float listH = static_cast<float>(listVisibleRows * listRowH);

    // Header
    g.setFont(Fonts::ui(Fonts::Sizes::micro, true));
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
        g.setFont(Fonts::ui(Fonts::Sizes::small));
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

            g.setFont(Fonts::ui(Fonts::Sizes::small));
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

    g.setFont(Fonts::ui(Fonts::Sizes::small));
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
            g.setFont(Fonts::ui(Fonts::Sizes::micro));
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
        g.setFont(Fonts::ui(Fonts::Sizes::small));
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
