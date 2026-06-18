#include "IRLoaderPanel.h"
#include "ColourPalette.h"
#include "FontData.h"

namespace sr {

IRLoaderPanel::IRLoaderPanel()
{
    addAndMakeVisible(waveformDisplay);
}

void IRLoaderPanel::addUserIR(const juce::String& name, int samples, double sampleRate,
                              int irIndex, const juce::AudioBuffer<float>& audioData)
{
    irList.push_back({ name, samples, sampleRate, irIndex,
                       juce::AudioBuffer<float>(audioData) });
    repaint();
}

void IRLoaderPanel::clearIRList()
{
    irList.clear();
    selectedIRIndex = 0;
    scrollOffset = 0;
    waveformDisplay.clear();
    repaint();
}

void IRLoaderPanel::setSelectedIR(int index)
{
    selectedIRIndex = index;
    refreshWaveform();
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
    dragOver = false;
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
}

void IRLoaderPanel::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
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

    float leftW = bounds.getWidth() * 0.45f;

    // ── Left column: IR list ──
    float y = 34.0f;
    float listH = static_cast<float>(listVisibleRows * listRowH);

    // Header
    g.setFont(Fonts::spaceMono(7.0f, true));
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
        g.setFont(Fonts::spaceMono(8.0f));
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

            g.setFont(Fonts::spaceMono(8.0f));
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

    g.setFont(Fonts::spaceMono(8.0f));
    g.setColour(Colours::label);
    g.drawText("DROP .WAV OR CLICK TO BROWSE", dropArea, juce::Justification::centred, false);

    // ── Divider ──
    g.setColour(Colours::border);
    g.fillRect(leftW, 6.0f, 1.0f, bounds.getHeight() - 12.0f);

    // ── Right column: Detail grid ──
    float rx = leftW + 12.0f;
    float ry = 84.0f;
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
            g.setFont(Fonts::spaceMono(7.0f));
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
        g.setFont(Fonts::spaceMono(8.0f));
        g.setColour(Colours::dim);
        g.drawText("Drop a .wav impulse response\nto get started",
                   juce::Rectangle<float>(rx, ry, rw, 60.0f),
                   juce::Justification::centred, true);
    }
}

void IRLoaderPanel::resized()
{
    float leftW = getWidth() * 0.45f;

    // Waveform display at top of right column
    float rx = leftW + 8.0f;
    waveformDisplay.setBounds(static_cast<int>(rx), 6,
                              static_cast<int>(getWidth() - rx - 8.0f), 72);
}

} // namespace sr
