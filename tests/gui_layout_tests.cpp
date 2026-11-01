// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// The editor's layout after the UTILITY card moved out of EFFECTS
// (2026-10-31): INPUT over THRESH beside the IN meter, OUTPUT over WIDTH
// beside the OUT meter, and BOOST / DELAY / REVERB in one row.
//
//   - The four meter-column knobs live in the editor, not the EFFECTS
//     panel, sit between their meter and the faceplate, don't overlap, and
//     still drive their parameters.
//   - Every EFFECTS control lies inside the panel and no two overlap, so the
//     one-row cards clip nothing; SYNC and PP are jewels on DELAY's title line.
//   - A tight dial (NotchKnob::setTightDial) keeps its size in less height.
// ═══════════════════════════════════════════════════════════════

#include "processor_harness.h"
#include "PluginEditor.h"
#include "gui/AmpFaceplate.h"
#include "gui/EffectsPanel.h"
#include "gui/NotchKnob.h"
#include "gui/ToggleSwitch.h"
#include "gui/VUMeter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace {

template <typename T>
std::vector<T*> findAll(juce::Component& root)
{
    std::vector<T*> found;
    std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (auto* t = dynamic_cast<T*>(child))
                found.push_back(t);
            walk(*child);
        }
    };
    walk(root);
    return found;
}

// The NotchKnob whose slider carries this accessibility title.
sr::NotchKnob* knobTitled(juce::Component& root, const juce::String& title)
{
    for (auto* k : findAll<sr::NotchKnob>(root))
        if (k->getSlider().getTitle() == title)
            return k;
    return nullptr;
}

struct EditorRig
{
    sr::test::ProcessorHarness h;   // first: the processor outlives the editor
    std::unique_ptr<SilentRiotEditor> editor;

    EditorRig()
    {
        editor.reset(dynamic_cast<SilentRiotEditor*>(h.proc.createEditor()));
        REQUIRE(editor != nullptr);
    }
    ~EditorRig() { editor.reset(); }

    juce::Rectangle<int> inEditor(juce::Component& c) const
    {
        return editor->getLocalArea(&c, c.getLocalBounds());
    }
};

} // namespace

TEST_CASE("Layout: the gate, width and trims sit beside the meters, not in EFFECTS", "[gui][layout]")
{
    EditorRig r;
    auto* input  = knobTitled(*r.editor, "Input trim");
    auto* gate   = knobTitled(*r.editor, "Gate threshold");
    auto* output = knobTitled(*r.editor, "Output trim");
    auto* width  = knobTitled(*r.editor, "Stereo width");
    REQUIRE(input != nullptr);
    REQUIRE(gate != nullptr);
    REQUIRE(output != nullptr);
    REQUIRE(width != nullptr);

    for (auto* k : { input, gate, output, width })
    {
        CHECK(k->isVisible());
        CHECK(k->findParentComponentOfClass<sr::EffectsPanel>() == nullptr);
    }

    const auto meters = findAll<sr::VUMeter>(*r.editor);
    REQUIRE(meters.size() == 2);
    auto* plate = findAll<sr::AmpFaceplate>(*r.editor).front();
    const auto inMeter  = r.inEditor(*meters[0]).getX() < r.inEditor(*meters[1]).getX()
                              ? r.inEditor(*meters[0]) : r.inEditor(*meters[1]);
    const auto outMeter = r.inEditor(*meters[0]).getX() < r.inEditor(*meters[1]).getX()
                              ? r.inEditor(*meters[1]) : r.inEditor(*meters[0]);
    const auto plateR = r.inEditor(*plate);

    // Left column: between the IN meter and the faceplate, INPUT above THRESH.
    for (auto* k : { input, gate })
    {
        const auto b = r.inEditor(*k);
        CHECK(b.getX() >= inMeter.getRight());
        CHECK(b.getRight() <= plateR.getX());
    }
    CHECK(r.inEditor(*input).getBottom() <= r.inEditor(*gate).getY());

    // Right column: between the faceplate and the OUT meter, OUTPUT above WIDTH.
    for (auto* k : { output, width })
    {
        const auto b = r.inEditor(*k);
        CHECK(b.getX() >= plateR.getRight());
        CHECK(b.getRight() <= outMeter.getX());
    }
    CHECK(r.inEditor(*output).getBottom() <= r.inEditor(*width).getY());

    // Within the amp row: no lower than the faceplate.
    for (auto* k : { input, gate, output, width })
        CHECK(r.inEditor(*k).getBottom() <= plateR.getBottom() + 8);
}

TEST_CASE("Layout: the meter-column knobs still drive their parameters", "[gui][layout]")
{
    EditorRig r;
    namespace IDs = sr::Params::IDs;
    struct Case { const char* title; juce::String id; };
    for (const auto& c : { Case{ "Input trim", IDs::INPUT_LEVEL },
                           Case{ "Gate threshold", IDs::GATE_THRESHOLD },
                           Case{ "Output trim", IDs::OUTPUT_LEVEL },
                           Case{ "Stereo width", IDs::WIDTH } })
    {
        INFO(c.title);
        auto* k = knobTitled(*r.editor, c.title);
        REQUIRE(k != nullptr);
        auto* p = r.h.proc.apvts.getParameter(c.id);
        REQUIRE(p != nullptr);

        // Parameter -> knob
        p->setValueNotifyingHost(0.8f);
        CHECK_THAT(k->getSlider().getValue(), Catch::Matchers::WithinAbs(0.8, 1e-4));

        // Knob -> parameter
        k->getSlider().setValue(0.25, juce::sendNotificationSync);
        CHECK_THAT(p->getValue(), Catch::Matchers::WithinAbs(0.25, 1e-4));
    }
}

TEST_CASE("Layout: EFFECTS holds one row of controls, all inside it, none overlapping", "[gui][layout]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sr::EffectsPanel fx;
    fx.setSize(700, sr::EffectsPanel::panelHeight);

    std::vector<juce::Component*> controls;
    for (auto* child : fx.getChildren())
        controls.push_back(child);
    // 10 knobs (BOOST 3, DELAY 4, REVERB 3) + 3 ON jewels + SYNC and PP.
    REQUIRE(controls.size() == 15);
    CHECK(findAll<sr::NotchKnob>(fx).size() == 10);

    const auto inside = fx.getLocalBounds().reduced(10);
    for (size_t i = 0; i < controls.size(); ++i)
    {
        const auto a = controls[i]->getBounds();
        CHECK(! a.isEmpty());
        CHECK(inside.contains(a));
        for (size_t j = i + 1; j < controls.size(); ++j)
            CHECK(! a.intersects(controls[j]->getBounds()));
    }

    // One row: every knob shares a baseline.
    const auto knobs = findAll<sr::NotchKnob>(fx);
    for (auto* k : knobs)
        CHECK(k->getBottom() == knobs.front()->getBottom());

    // Every toggle is a jewel on a card's title line, above the knobs.
    for (auto* t : findAll<sr::ToggleSwitch>(fx))
    {
        CHECK(t->getHeight() >= sr::ToggleSwitch::minHeight(sr::ToggleSwitch::Style::Jewel));
        CHECK(t->getBottom() <= knobs.front()->getY());
    }
}

TEST_CASE("Layout: a tight dial keeps its size and sits between its name and value", "[gui][layout]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sr::NotchKnob plain{ "LEVEL", 0.5f }, tight{ "LEVEL", 0.5f };
    for (auto* k : { &plain, &tight })
        sr::EffectsPanel::styleKnob(*k, 1);
    plain.setSize(54, 80);   // the EFFECTS knob before tight dials
    tight.setTightDial(true);
    tight.setSize(54, 66);   // and after: 14 px shorter

    // No smaller a dial in less height
    CHECK(tight.getSlider().getWidth() >= plain.getSlider().getWidth());

    // What drawRotarySlider paints (ticks to skirt) clears the name above it
    // and the engraved value below it.
    const auto side = static_cast<float>(tight.getSlider().getWidth());
    const float paintedTop = tight.getSlider().getY() + side * (0.5f - sr::NotchKnob::kDialTopReach);
    const float paintedBottom = paintedTop + sr::NotchKnob::tightDialHeight(side);
    const float scale = sr::EffectsPanel::knobScale;
    const float nameH  = juce::jmax(14.0f, 14.0f * scale);                          // labelHFor
    const float valueH = juce::jmax(14.0f, 16.0f * scale) - sr::NotchKnob::kEngravedSaving;  // engraved row
    CHECK(paintedTop >= nameH);
    CHECK(paintedBottom <= static_cast<float>(tight.getHeight()) - valueH + 0.5f);
}
