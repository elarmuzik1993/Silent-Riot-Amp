#include "PluginEditor.h"
#include "utils/Parameters.h"

TinyTerrorEditor::TinyTerrorEditor(TinyTerrorProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&lnf);
    setSize(500, 320);

    addAndMakeVisible(gainKnob);
    addAndMakeVisible(toneKnob);
    addAndMakeVisible(volumeKnob);
    addAndMakeVisible(powerModeBtn);
    addAndMakeVisible(cabSimBtn);

    oversampleBox.addItemList({"2x", "4x", "8x"}, 1);
    addAndMakeVisible(oversampleBox);

    using namespace tt::Params;
    gainA = std::make_unique<SliderA>(proc.apvts, IDs::GAIN,   gainKnob.getSlider());
    toneA = std::make_unique<SliderA>(proc.apvts, IDs::TONE,   toneKnob.getSlider());
    volA  = std::make_unique<SliderA>(proc.apvts, IDs::VOLUME, volumeKnob.getSlider());
    powerA = std::make_unique<ButtonA>(proc.apvts, IDs::HALF_POWER, powerModeBtn);
    cabA   = std::make_unique<ButtonA>(proc.apvts, IDs::CAB_SIM, cabSimBtn);
    osA    = std::make_unique<ComboA>(proc.apvts, IDs::OVERSAMPLE, oversampleBox);
}

TinyTerrorEditor::~TinyTerrorEditor() { setLookAndFeel(nullptr); }

void TinyTerrorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xFF1A1A1A));
    g.setColour(juce::Colour(0xFFFF6600));
    g.setFont(juce::FontOptions(28.0f));
    g.drawText("TINY TERROR", getLocalBounds().removeFromTop(50), juce::Justification::centred);
    g.setFont(juce::FontOptions(11.0f));
    g.setColour(juce::Colour(0xFF888888));
    g.drawText("MONOLIT BEATZ", getLocalBounds().removeFromBottom(20), juce::Justification::centred);
}

void TinyTerrorEditor::resized() {
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(50);
    auto knobArea = area.removeFromTop(180);
    int kw = knobArea.getWidth() / 3;
    gainKnob.setBounds(knobArea.removeFromLeft(kw));
    toneKnob.setBounds(knobArea.removeFromLeft(kw));
    volumeKnob.setBounds(knobArea);

    area.removeFromTop(8);
    auto controls = area.removeFromTop(28);
    powerModeBtn.setBounds(controls.removeFromLeft(70));
    cabSimBtn.setBounds(controls.removeFromLeft(60));
    controls.removeFromLeft(10);
    oversampleBox.setBounds(controls.removeFromLeft(80));
}
