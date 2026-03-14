#include "IRLoaderPanel.h"
#include "ColourPalette.h"
#include "FontData.h"
#include "../dsp/cabinet/IRLibrary.h"

namespace tt {

IRLoaderPanel::IRLoaderPanel()
{
    addAndMakeVisible(categoryTabs);
    addAndMakeVisible(waveformDisplay);

    categoryTabs.onChange = [this](int) { repaint(); };

    populateIRList();
}

void IRLoaderPanel::populateIRList()
{
    irList.clear();
    for (int i = 0; i < NUM_STOCK_IRS; ++i)
    {
        const auto& desc = getStockIRDescriptor(i);
        irList.push_back({ desc.displayName, desc.mic, desc.samples, i + 1 });
    }
}

void IRLoaderPanel::setSelectedIR(int index)
{
    selectedIRIndex = index;
    refreshWaveform();
    repaint();
}

void IRLoaderPanel::refreshWaveform()
{
    if (selectedIRIndex >= 1 && selectedIRIndex <= NUM_STOCK_IRS)
    {
        int stockIdx = selectedIRIndex - 1;
        const auto& desc = getStockIRDescriptor(stockIdx);
        auto buf = generateSyntheticIR(stockIdx, desc);
        waveformDisplay.setIRData(buf, desc.sampleRate);
        waveformDisplay.setIRInfo(desc.displayName,
            static_cast<float>(desc.samples) / static_cast<float>(desc.sampleRate) * 1000.0f);
    }
    else
    {
        waveformDisplay.clear();
    }
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
    // Check if click is in the IR list area
    auto bounds = getLocalBounds();
    float leftW = bounds.getWidth() * 0.45f;
    float listY = 56.0f; // below category tabs
    float listH = static_cast<float>(listVisibleRows * listRowH);

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
    }

    // Check drop zone click (browse)
    float dropY = listY + listH + 8.0f;
    auto dropArea = juce::Rectangle<float>(8.0f, dropY, leftW - 16.0f, 48.0f);
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

void IRLoaderPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.getHeight() < 20.0f) return;  // too small during animation

    g.setColour(Colours::panelLight);
    g.fillRect(bounds);

    float leftW = bounds.getWidth() * 0.45f;

    // ── Left column: IR list ──
    float y = 56.0f;
    float listH = static_cast<float>(listVisibleRows * listRowH);

    // Header row
    g.setFont(Fonts::spaceMono(7.0f, true));
    g.setColour(Colours::dim);
    g.drawText("NAME", juce::Rectangle<float>(8.0f, y - 16.0f, leftW * 0.5f, 14.0f),
               juce::Justification::centredLeft, false);
    g.drawText("MIC", juce::Rectangle<float>(leftW * 0.55f, y - 16.0f, 50.0f, 14.0f),
               juce::Justification::centredLeft, false);
    g.drawText("SMP", juce::Rectangle<float>(leftW * 0.78f, y - 16.0f, 50.0f, 14.0f),
               juce::Justification::centredLeft, false);

    // IR rows
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
        g.drawText(entry.name, juce::Rectangle<float>(8.0f, rowY, leftW * 0.5f, static_cast<float>(listRowH)),
                   juce::Justification::centredLeft, false);
        g.setColour(Colours::label);
        g.drawText(entry.mic, juce::Rectangle<float>(leftW * 0.55f, rowY, 50.0f, static_cast<float>(listRowH)),
                   juce::Justification::centredLeft, false);
        g.drawText(juce::String(entry.samples), juce::Rectangle<float>(leftW * 0.78f, rowY, 50.0f, static_cast<float>(listRowH)),
                   juce::Justification::centredLeft, false);

        // Row separator
        g.setColour(Colours::border.withAlpha(0.3f));
        g.fillRect(8.0f, rowY + listRowH - 1.0f, leftW - 16.0f, 0.5f);
    }

    // Drop zone
    float dropY = y + listH + 8.0f;
    auto dropArea = juce::Rectangle<float>(8.0f, dropY, leftW - 16.0f, 48.0f);
    g.setColour(dragOver ? Colours::accent.withAlpha(0.2f) : Colours::surface);
    g.fillRoundedRectangle(dropArea, 4.0f);
    g.setColour(dragOver ? Colours::accent : Colours::borderHi);

    // Dashed border
    juce::Path dashRect;
    dashRect.addRoundedRectangle(dropArea, 4.0f);
    float dashes[] = { 4.0f, 4.0f };
    juce::PathStrokeType stroke(1.0f);
    stroke.createDashedStroke(dashRect, dashRect, dashes, 2);
    g.strokePath(dashRect, juce::PathStrokeType(1.0f));

    g.setFont(Fonts::spaceMono(8.0f));
    g.setColour(Colours::label);
    g.drawText("DROP .WAV OR CLICK TO BROWSE", dropArea, juce::Justification::centred, false);

    // ── Divider ──
    g.setColour(Colours::border);
    g.fillRect(leftW, 8.0f, 1.0f, bounds.getHeight() - 16.0f);

    // ── Right column: Detail grid ──
    float rx = leftW + 16.0f;
    float ry = 110.0f; // below waveform display
    float rw = bounds.getWidth() - leftW - 32.0f;

    if (selectedIRIndex >= 1 && selectedIRIndex <= NUM_STOCK_IRS)
    {
        int stockIdx = selectedIRIndex - 1;
        const auto& desc = getStockIRDescriptor(stockIdx);

        struct Detail { const char* key; juce::String val; };
        Detail details[] = {
            { "FILE",     desc.id },
            { "CATEGORY", "STOCK" },
            { "MIC",      desc.mic },
            { "LENGTH",   juce::String(desc.samples) + " smp" },
            { "RATE",     juce::String(desc.sampleRate / 1000) + " kHz" },
            { "LATENCY",  juce::String(static_cast<float>(desc.samples) / desc.sampleRate * 1000.0f, 1) + "ms" },
        };

        for (auto& d : details)
        {
            g.setFont(Fonts::spaceMono(7.0f));
            g.setColour(Colours::label);
            g.drawText(d.key, juce::Rectangle<float>(rx, ry, 60.0f, 14.0f),
                       juce::Justification::centredLeft, false);
            g.setColour(Colours::text);
            g.drawText(d.val, juce::Rectangle<float>(rx + 64.0f, ry, rw - 64.0f, 14.0f),
                       juce::Justification::centredLeft, false);
            ry += 18.0f;
        }

        // Action buttons
        ry += 12.0f;
        auto drawBtn = [&](const juce::String& text, float bx, float bw) {
            g.setColour(Colours::surface);
            g.fillRoundedRectangle(bx, ry, bw, 22.0f, 3.0f);
            g.setColour(Colours::border);
            g.drawRoundedRectangle(bx, ry, bw, 22.0f, 3.0f, 1.0f);
            g.setFont(Fonts::spaceMono(7.0f, true));
            g.setColour(Colours::label);
            g.drawText(text, juce::Rectangle<float>(bx, ry, bw, 22.0f),
                       juce::Justification::centred, false);
        };

        float btnW = (rw - 12.0f) / 3.0f;
        drawBtn("PHASE INV", rx, btnW);
        drawBtn("NORMALIZE", rx + btnW + 6.0f, btnW);
        drawBtn("TRIM", rx + (btnW + 6.0f) * 2.0f, btnW);
    }
}

void IRLoaderPanel::resized()
{
    float leftW = getWidth() * 0.45f;

    // Category tabs at top of left column
    categoryTabs.setBounds(8, 8, static_cast<int>(leftW) - 16, 24);

    // Waveform display at top of right column
    float rx = leftW + 8.0f;
    waveformDisplay.setBounds(static_cast<int>(rx), 8,
                              static_cast<int>(getWidth() - rx - 8.0f), 90);
}

} // namespace tt
