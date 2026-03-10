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

    addAndMakeVisible(header);
    addAndMakeVisible(footer);
    addAndMakeVisible(vuIn);
    addAndMakeVisible(vuOut);
    addAndMakeVisible(gainKnob);
    addAndMakeVisible(toneKnob);
    addAndMakeVisible(volumeKnob);
    addAndMakeVisible(halfPowerToggle);
    addAndMakeVisible(cabSimToggle);
    addAndMakeVisible(powerToggle);

    // Section bars
    addAndMakeVisible(irSectionBar);
    addChildComponent(irLoaderPanel);

    addAndMakeVisible(advSectionBar);
    addChildComponent(advancedPanel);

    irSectionBar.onToggle = [](bool) {};
    advSectionBar.onToggle = [](bool) {};

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

    // CAB toggle auto-opens IR panel
    cabSimToggle.getButton().onStateChange = [this] {
        if (cabSimToggle.getButton().getToggleState() && !irSectionBar.isOpen())
        {
            irSectionBar.setOpen(true);
        }
    };

    // Settings overlay
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

    // Tab order: GAIN → TONE → VOLUME → 7W → CAB → PWR
    gainKnob.getSlider().setExplicitFocusOrder(1);
    toneKnob.getSlider().setExplicitFocusOrder(2);
    volumeKnob.getSlider().setExplicitFocusOrder(3);
    halfPowerToggle.getButton().setExplicitFocusOrder(4);
    cabSimToggle.getButton().setExplicitFocusOrder(5);
    powerToggle.getButton().setExplicitFocusOrder(6);

    setSize(700, calculateTotalHeight());
    startTimerHz(30);
}

TinyTerrorEditor::~TinyTerrorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

int TinyTerrorEditor::calculateTotalHeight() const
{
    int h = 38;   // header
    h += 232;     // main body (knobs + toggles)
    h += 26;      // IR section bar
    h += static_cast<int>(irPanelHeight);
    h += 26;      // advanced section bar
    h += static_cast<int>(advPanelHeight);
    h += 30;      // footer
    return h;
}

void TinyTerrorEditor::timerCallback()
{
    bool needsResize = false;

    // Animate IR panel
    {
        float targetH = irSectionBar.isOpen() ? static_cast<float>(tt::IRLoaderPanel::panelHeight) : 0.0f;
        float diff = targetH - irPanelHeight;
        if (std::abs(diff) > 0.5f)
        {
            irPanelHeight += diff * 0.15f;
            irLoaderPanel.setVisible(irPanelHeight > 1.0f);
            needsResize = true;
        }
        else if (std::abs(diff) > 0.01f)
        {
            irPanelHeight = targetH;
            irLoaderPanel.setVisible(irPanelHeight > 1.0f);
            needsResize = true;
        }
    }

    // Animate advanced panel
    {
        float targetH = advSectionBar.isOpen() ? static_cast<float>(tt::AdvancedPanel::panelHeight) : 0.0f;
        float diff = targetH - advPanelHeight;
        if (std::abs(diff) > 0.5f)
        {
            advPanelHeight += diff * 0.15f;
            advancedPanel.setVisible(advPanelHeight > 1.0f);
            needsResize = true;
        }
        else if (std::abs(diff) > 0.01f)
        {
            advPanelHeight = targetH;
            advancedPanel.setVisible(advPanelHeight > 1.0f);
            needsResize = true;
        }
    }

    if (needsResize)
        setSize(700, calculateTotalHeight());

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
    auto area = getLocalBounds();

    // Header
    header.setBounds(area.removeFromTop(38));

    // Footer (always at bottom)
    footer.setBounds(area.removeFromBottom(30));

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
    if (irPanelHeight > 0.0f)
        irLoaderPanel.setBounds(area.removeFromTop(static_cast<int>(irPanelHeight)));

    advSectionBar.setBounds(area.removeFromTop(26));
    if (advPanelHeight > 0.0f)
        advancedPanel.setBounds(area.removeFromTop(static_cast<int>(advPanelHeight)));

    // Settings overlay (absolute positioned)
    if (settingsPanel.isVisible())
    {
        settingsPanel.setBounds(getWidth() - 340, 38, 340, 420);
        settingsPanel.toFront(false);
    }
}
