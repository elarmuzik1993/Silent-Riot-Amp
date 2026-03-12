#include "PluginEditor.h"
#include "gui/ColourPalette.h"
#include "gui/FontData.h"
#include "utils/Parameters.h"
#include "dsp/cabinet/IRLibrary.h"

TinyTerrorEditor::TinyTerrorEditor(TinyTerrorProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    tt::Fonts::init();
    setLookAndFeel(&lnf);
    setOpaque(true);

    // Content container — children laid out inside, window never resizes
    addAndMakeVisible(contentArea);
    contentArea.setInterceptsMouseClicks(false, true);

    contentArea.addAndMakeVisible(header);
    contentArea.addAndMakeVisible(footer);
    contentArea.addAndMakeVisible(vuIn);
    contentArea.addAndMakeVisible(vuOut);
    contentArea.addAndMakeVisible(gainKnob);
    contentArea.addAndMakeVisible(toneKnob);
    contentArea.addAndMakeVisible(volumeKnob);
    contentArea.addAndMakeVisible(halfPowerToggle);
    contentArea.addAndMakeVisible(cabSimToggle);
    contentArea.addAndMakeVisible(powerToggle);

    // Section bars
    contentArea.addAndMakeVisible(irSectionBar);
    contentArea.addChildComponent(irLoaderPanel);

    contentArea.addAndMakeVisible(advSectionBar);
    contentArea.addChildComponent(advancedPanel);

    irSectionBar.onToggle = [this](bool open) {
        irPanelOpen = open;
        irLoaderPanel.setVisible(open);
        layoutContent();
    };
    advSectionBar.onToggle = [this](bool open) {
        advPanelOpen = open;
        advancedPanel.setVisible(open);
        layoutContent();
    };

    irSectionBar.setHintText("IIR Mode");

    // IR loader panel wiring
    irLoaderPanel.onIRSelected = [this](int irIdx) {
        if (auto* param = proc.apvts.getParameter(tt::Params::IDs::IR_INDEX))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(irIdx)));
        // Update hint text
        if (irIdx >= 1 && irIdx <= tt::NUM_STOCK_IRS)
            irSectionBar.setHintText(tt::getStockIRDescriptor(irIdx - 1).displayName);
        else
            irSectionBar.setHintText("IIR Mode");
    };

    // CAB toggle auto-opens IR panel (setOpen fires onToggle which handles layout)
    cabSimToggle.getButton().onStateChange = [this] {
        if (cabSimToggle.getButton().getToggleState() && !irSectionBar.isOpen())
            irSectionBar.setOpen(true);
    };

    // Settings overlay (added directly to editor, not contentArea, so it floats on top)
    addChildComponent(settingsPanel);
    header.onGearClick = [this] {
        settingsPanel.setVisible(!settingsPanel.isVisible());
        resized();
    };
    settingsPanel.onClose = [this] {
        settingsPanel.setVisible(false);
    };
    settingsPanel.onScaleChanged = [this](int scaleIdx) {
        static const float scales[] = { 0.75f, 1.0f, 1.25f, 1.5f };
        float s = scales[juce::jlimit(0, 3, scaleIdx)];
        setTransform(juce::AffineTransform::scale(s));
    };

    // Preset selector wiring
    header.getPresetSelector().onPresetChanged = [this](int idx) {
        if (auto* param = proc.apvts.getParameter(tt::Params::IDs::PRESET_IDX))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(idx)));
    };

    // APVTS attachments
    using namespace tt::Params;
    gainA = std::make_unique<SliderA>(proc.apvts, IDs::GAIN,   gainKnob.getSlider());
    toneA = std::make_unique<SliderA>(proc.apvts, IDs::TONE,   toneKnob.getSlider());
    volA  = std::make_unique<SliderA>(proc.apvts, IDs::VOLUME, volumeKnob.getSlider());
    halfPowerA = std::make_unique<ButtonA>(proc.apvts, IDs::HALF_POWER, halfPowerToggle.getButton());
    cabSimA    = std::make_unique<ButtonA>(proc.apvts, IDs::CAB_SIM,    cabSimToggle.getButton());
    powerA     = std::make_unique<ButtonA>(proc.apvts, IDs::POWER,      powerToggle.getButton());

    // Keyboard focus and accessibility
    gainKnob.getSlider().setWantsKeyboardFocus(true);
    toneKnob.getSlider().setWantsKeyboardFocus(true);
    volumeKnob.getSlider().setWantsKeyboardFocus(true);
    gainKnob.getSlider().setDescription("Gain control, 0 to 10");
    toneKnob.getSlider().setDescription("Tone control, 0 to 10");
    volumeKnob.getSlider().setDescription("Volume control, 0 to 10");
    halfPowerToggle.getButton().setDescription("Half power 7W mode toggle");
    cabSimToggle.getButton().setDescription("Cabinet simulation toggle");
    powerToggle.getButton().setDescription("Power on/off toggle");

    // Tab order: GAIN -> TONE -> VOLUME -> 7W -> CAB -> PWR
    gainKnob.getSlider().setExplicitFocusOrder(1);
    toneKnob.getSlider().setExplicitFocusOrder(2);
    volumeKnob.getSlider().setExplicitFocusOrder(3);
    halfPowerToggle.getButton().setExplicitFocusOrder(4);
    cabSimToggle.getButton().setExplicitFocusOrder(5);
    powerToggle.getButton().setExplicitFocusOrder(6);

    setSize(kWindowW, kWindowH);
    startTimerHz(30);
}

TinyTerrorEditor::~TinyTerrorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void TinyTerrorEditor::timerCallback()
{
    // Live VU meters from processor
    bool powerOn = proc.apvts.getRawParameterValue(tt::Params::IDs::POWER)->load() > 0.5f;
    if (powerOn)
    {
        vuIn.setLevel(proc.inputLevelForGUI.load(std::memory_order_relaxed));
        vuOut.setLevel(proc.outputLevelForGUI.load(std::memory_order_relaxed));
    }
    else
    {
        vuIn.setLevel(0.0f);
        vuOut.setLevel(0.0f);
    }

    // Live footer stats
    {
        auto dspR = proc.getDSPReadout();
        float latMs = static_cast<float>(proc.getLatencySamples()) / static_cast<float>(proc.getSampleRate()) * 1000.0f;
        footer.setStats(dspR.osFactor, latMs, proc.getSampleRate(), 32);
    }

    // Update advanced panel readout
    if (advancedPanel.isVisible())
    {
        auto dspR = proc.getDSPReadout();
        tt::DSPReadout guiR;
        guiR.vBPlus = dspR.vBPlus;
        guiR.vScreen = dspR.vScreen;
        guiR.vPi = dspR.vPi;
        guiR.vPreamp = dspR.vPreamp;
        guiR.sagPercent = dspR.sagPercent;
        guiR.biasV = dspR.biasV;
        guiR.gridCurrentMA = dspR.gridCurrentMA;
        guiR.osFactor = dspR.osFactor;
        guiR.is7W = dspR.is7W;
        guiR.tempC = 420.0f;
        advancedPanel.setReadout(guiR);
    }
}

void TinyTerrorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Gradient background
    g.setGradientFill(juce::ColourGradient(
        tt::Colours::bg2, 0.0f, 0.0f,
        tt::Colours::bg,  0.0f, bounds.getHeight(),
        false));
    g.fillAll();

    // 1px border with 6px radius
    g.setColour(tt::Colours::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
}

void TinyTerrorEditor::resized()
{
    contentArea.setBounds(getLocalBounds());
    layoutContent();
}

void TinyTerrorEditor::layoutContent()
{
    auto area = contentArea.getLocalBounds();

    // Header
    header.setBounds(area.removeFromTop(38));

    // Main body area
    auto body = area.removeFromTop(232);
    int vuW = 40;

    vuIn.setBounds(body.removeFromLeft(vuW).reduced(8, 20));
    vuOut.setBounds(body.removeFromRight(vuW).reduced(8, 20));

    // Center controls
    int cx = body.getCentreX();

    // Knobs
    int knobW = 100, knobH = 120, knobGap = 36;
    int totalKnobW = knobW * 3 + knobGap * 2;
    int knobStartX = cx - totalKnobW / 2;
    int knobY = body.getY() + 16;

    gainKnob.setBounds(knobStartX, knobY, knobW, knobH);
    toneKnob.setBounds(knobStartX + knobW + knobGap, knobY, knobW, knobH);
    volumeKnob.setBounds(knobStartX + (knobW + knobGap) * 2, knobY, knobW, knobH);

    // Toggles
    int toggleW = 50, toggleH = 36, toggleGap = 32;
    int totalToggleW = toggleW * 3 + toggleGap * 2;
    int toggleStartX = cx - totalToggleW / 2;
    int toggleY = knobY + knobH + 40;

    halfPowerToggle.setBounds(toggleStartX, toggleY, toggleW, toggleH);
    cabSimToggle.setBounds(toggleStartX + toggleW + toggleGap, toggleY, toggleW, toggleH);
    powerToggle.setBounds(toggleStartX + (toggleW + toggleGap) * 2, toggleY, toggleW, toggleH);

    // Section bars + panels
    irSectionBar.setBounds(area.removeFromTop(26));
    if (irPanelOpen)
        irLoaderPanel.setBounds(area.removeFromTop(tt::IRLoaderPanel::panelHeight));

    advSectionBar.setBounds(area.removeFromTop(26));
    if (advPanelOpen)
        advancedPanel.setBounds(area.removeFromTop(tt::AdvancedPanel::panelHeight));

    // Footer immediately after content
    footer.setBounds(area.removeFromTop(30));

    // Settings overlay (absolute positioned on editor, not contentArea)
    if (settingsPanel.isVisible())
    {
        settingsPanel.setBounds(getWidth() - 340, 38, 340, 420);
        settingsPanel.toFront(false);
    }
}
