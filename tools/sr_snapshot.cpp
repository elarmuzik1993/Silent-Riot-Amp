// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ── Offline GUI snapshot (tools/README.md) ──────────────────────────────────
// Renders the EFFECTS panel to a PNG so knob sizing can be reviewed without
// launching the plugin. Built only when -DSR_BUILD_TOOLS=ON.
//
//   sr_snapshot <out.png>
//
// Headless: paints components into a juce::Image via paintEntireComponent — no
// window/peer required. Fonts come from BinaryData (Freetype, no X needed).

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

#include "gui/ColourPalette.h"
#include "gui/NotchKnob.h"
#include "gui/EffectsPanel.h"

using namespace sr;

// A row of three full-size (scale 1.0) knobs mimicking the main amp controls,
// shown above the effects panel purely as a size reference.
class ReferenceRow : public juce::Component
{
public:
    ReferenceRow()
    {
        gain.getSlider().setValue(0.62, juce::dontSendNotification);
        shape.getSlider().setValue(0.5, juce::dontSendNotification);
        vol.getSlider().setValue(0.55, juce::dontSendNotification);
        for (auto* k : { &gain, &shape, &vol })
            addAndMakeVisible(*k);
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(Colours::label);
        g.setFont(12.0f);
        g.drawText("MAIN AMP KNOBS  (100x120, full size)",
                   getLocalBounds().removeFromTop(20).reduced(12, 0),
                   juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromTop(22);
        const int knobW = 100, knobH = 120, gap = 36;
        const int total = knobW * 3 + gap * 2;
        int x = b.getCentreX() - total / 2;
        int y = b.getY();
        gain.setBounds(x, y, knobW, knobH);            x += knobW + gap;
        shape.setBounds(x, y, knobW, knobH);           x += knobW + gap;
        vol.setBounds(x, y, knobW, knobH);
    }

private:
    NotchKnob gain  { "GAIN",   0.62f };
    NotchKnob shape { "SHAPE",  0.5f  };
    NotchKnob vol   { "VOLUME", 0.55f };
};

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Colours::setTheme(Colours::Dark);

    const juce::String outPath = (argc > 1) ? argv[1] : "sr_effects.png";

    const int W = 720;
    const int refH = 160;
    const int fxH  = EffectsPanel::panelHeight;
    const int H = refH + fxH + 24;

    // Build the composite: reference row on top, effects panel below.
    juce::Component root;
    root.setSize(W, H);

    ReferenceRow ref;
    root.addAndMakeVisible(ref);
    ref.setBounds(0, 8, W, refH);

    EffectsPanel fx;
    root.addAndMakeVisible(fx);
    fx.setBounds(0, refH + 16, W, fxH);

    // Dial in representative values so the arcs light up (default is minimum).
    auto set = [](NotchKnob& k, double v) { k.getSlider().setValue(v, juce::dontSendNotification); };
    set(fx.boostLevelKnob(), 0.6);  set(fx.boostTightKnob(), 0.4); set(fx.boostBrightKnob(), 0.55);
    set(fx.delayTimeKnob(), 0.34);  set(fx.delayFbKnob(), 0.45);   set(fx.delayMixKnob(), 0.3); set(fx.delayToneKnob(), 0.6);
    set(fx.reverbMixKnob(), 0.4);   set(fx.reverbSizeKnob(), 0.65);set(fx.reverbToneKnob(), 0.5);

    // Render into an offscreen image with the app background.
    juce::Image image(juce::Image::ARGB, W, H, true);
    {
        juce::Graphics g(image);
        g.fillAll(Colours::bg);
        root.paintEntireComponent(g, false);
    }

    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(outPath));
    out.deleteFile();
    juce::FileOutputStream stream(out);
    if (stream.openedOk())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream(image, stream);
        stream.flush();
        juce::Logger::writeToLog("Wrote " + out.getFullPathName());
        return 0;
    }

    juce::Logger::writeToLog("Failed to open output: " + out.getFullPathName());
    return 1;
}
