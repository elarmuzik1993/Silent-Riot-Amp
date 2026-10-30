// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ── Offline editor screenshot (tools/README.md) ─────────────────────────────
// Renders the full plugin editor to a PNG for the user manual
// (docs/manual/, resources/images/). Built only when -DSR_BUILD_TOOLS=ON.
//
//   sr_editor_snapshot <out.png> [fx] [ir] [adv] [off] [scale=<0..n>] [theme=<n>]
//                      [set=<paramID>:<0..1>]... [loadir=<file.wav>]... [irview=<0|1>]
//                      [settings] [settings=input] [font=<regular.ttf>[,<bold.ttf>]]
//
// The flags open the matching panels (EFFECTS, IR LOADER, ADVANCED). With
// the CAB toggle on (the default) the editor opens the IR LOADER itself. `off`
// switches PWR off; `set=` sets any parameter by ID to a normalised value
// (e.g. set=boostOn:1 to light a card's jewel); `theme=` picks a SettingsPanel theme index.
// `loadir=` loads a user IR as the IR loader does (the last one stays selected);
// `irview=` picks the IR LOADER view (0 = CAB, 1 = LIBRARY). `settings` opens the
// Settings overlay (as the gear does); `settings=input` adds the standalone's AUDIO INPUT
// section first, on a device manager with no device open (its list is empty).
// `font=` draws the whole UI in another typeface (Fonts::previewUIFont), to compare
// candidate fonts before embedding one. Panel states go through the same apvts.state
// properties the editor restores from, so the panels snap open with no
// animation. Paints offscreen via paintEntireComponent; on Linux the editor
// still opens an X11 connection, so run it under Xvfb (xvfb-run) when
// there is no display.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "gui/HeaderBar.h"
#include "gui/SettingsPanel.h"
#include "gui/FontData.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String outPath = (argc > 1) ? argv[1] : "sr_editor.png";

    auto proc = std::make_unique<SilentRiotProcessor>();
    proc->setRateAndBufferSizeDetails(48000.0, 512);
    proc->prepareToPlay(48000.0, 512);
    {
        // One block of silence publishes the readouts the footer and the
        // ADVANCED panel show (oversample factor, supply rails).
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        proc->processBlock(buffer, midi);
    }

    bool openSettings = false, settingsInput = false;
    juce::AudioDeviceManager deviceManager;   // only for settings=input; never opened

    auto& state = proc->apvts.state;
    state.setProperty("fxPanelOpen",  false, nullptr);
    state.setProperty("irPanelOpen",  false, nullptr);
    state.setProperty("advPanelOpen", false, nullptr);
    state.setProperty("tooltips",     false, nullptr);

    for (int i = 2; i < argc; ++i)
    {
        const juce::String arg(argv[i]);
        if (arg == "fx")  state.setProperty("fxPanelOpen",  true, nullptr);
        if (arg == "ir")  state.setProperty("irPanelOpen",  true, nullptr);
        if (arg == "adv") state.setProperty("advPanelOpen", true, nullptr);
        if (arg == "off")
            proc->apvts.getParameter(sr::Params::IDs::POWER)->setValueNotifyingHost(0.0f);
        if (arg.startsWith("set="))
        {
            const auto spec = arg.fromFirstOccurrenceOf("=", false, false);
            if (auto* p = proc->apvts.getParameter(spec.upToFirstOccurrenceOf(":", false, false)))
                p->setValueNotifyingHost(spec.fromFirstOccurrenceOf(":", false, false).getFloatValue());
            else
                juce::Logger::writeToLog("Unknown parameter in " + arg);
        }
        if (arg.startsWith("loadir="))
            if (!proc->loadUserIR(juce::File::getCurrentWorkingDirectory().getChildFile(
                    arg.fromFirstOccurrenceOf("=", false, false))))
                juce::Logger::writeToLog("Couldn't load " + arg);
        if (arg.startsWith("irview="))
            state.setProperty("irView", arg.fromFirstOccurrenceOf("=", false, false).getIntValue(), nullptr);
        if (arg.startsWith("theme="))
            state.setProperty("theme", arg.fromFirstOccurrenceOf("=", false, false).getIntValue(), nullptr);
        if (arg.startsWith("font="))
        {
            const auto load = [](const juce::String& path) -> juce::Typeface::Ptr {
                juce::MemoryBlock data;
                if (path.isEmpty() || !juce::File::getCurrentWorkingDirectory().getChildFile(path).loadFileAsData(data))
                    return nullptr;
                return juce::Typeface::createSystemTypefaceFor(data.getData(), data.getSize());
            };
            const auto spec = arg.fromFirstOccurrenceOf("=", false, false);
            auto regular = load(spec.upToFirstOccurrenceOf(",", false, false));
            if (regular == nullptr)
                juce::Logger::writeToLog("Couldn't load " + arg);
            else
                sr::Fonts::previewUIFont(regular, load(spec.fromFirstOccurrenceOf(",", false, false)));
        }
        if (arg == "settings") openSettings = true;
        if (arg == "settings=input") openSettings = settingsInput = true;
        if (arg.startsWith("scale="))
            state.setProperty("uiScale", arg.fromFirstOccurrenceOf("=", false, false).getIntValue(), nullptr);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc->createEditor());

    // The "Made with JUCE" splash is a transient overlay that fades out a few
    // seconds after the editor opens; a manual screenshot shows the editor
    // the player looks at, so leave it out of the picture.
    for (auto* child : editor->getChildren())
        if (dynamic_cast<juce::JUCESplashScreen*>(child) != nullptr)
            child->setVisible(false);

    if (openSettings)
    {
        // Find the panel and the gear by type: the editor keeps them private.
        std::function<juce::Component*(juce::Component&, bool)> find =
            [&](juce::Component& c, bool gear) -> juce::Component* {
                for (auto* child : c.getChildren())
                {
                    if (gear ? dynamic_cast<sr::GearButton*>(child) != nullptr
                             : dynamic_cast<sr::SettingsPanel*>(child) != nullptr)
                        return child;
                    if (auto* hit = find(*child, gear))
                        return hit;
                }
                return nullptr;
            };
        if (settingsInput)
            if (auto* panel = dynamic_cast<sr::SettingsPanel*>(find(*editor, false)))
                panel->attachInputSelector(deviceManager);
        if (auto* gear = dynamic_cast<sr::GearButton*>(find(*editor, true)))
            if (gear->onActivate)
                gear->onActivate();
    }

    // Toggle thumbs, meters and panel heights animate on the editor's timers
    // from their initial values (the CAB toggle opens the IR LOADER as it
    // syncs, as it does in a host); run the message loop until they come to
    // rest, then size the picture to the window the editor settled on.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);
    const int W = editor->getWidth();
    const int H = editor->getHeight();

    juce::Image image(juce::Image::ARGB, W, H, true);
    {
        juce::Graphics g(image);
        editor->paintEntireComponent(g, false);
    }

    editor.reset();
    proc->releaseResources();

    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(outPath));
    out.deleteFile();
    juce::FileOutputStream stream(out);
    if (stream.openedOk())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream(image, stream);
        stream.flush();
        juce::Logger::writeToLog("Wrote " + out.getFullPathName()
                                 + " (" + juce::String(W) + "x" + juce::String(H) + ")");
        return 0;
    }

    juce::Logger::writeToLog("Failed to open output: " + out.getFullPathName());
    return 1;
}
