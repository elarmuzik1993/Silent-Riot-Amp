// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Settings overlay layout. The old panel gave its bat toggles 28px of the
// 48px they paint in, so JUCE clipped them to a bare label, and kept its
// labels and controls on two hand-kept y sequences. These pin the fix: every
// control lies inside the panel, nothing overlaps, every label fits its column
// and the toggles get the room they paint in — with and without the
// standalone's AUDIO INPUT section.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include "gui/SettingsPanel.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <vector>

namespace {

// The panel's rows, in one list, for the checks below.
std::vector<sr::SettingsPanel::Row> allRows(const sr::SettingsPanel& panel)
{
    const auto L = panel.layout();
    std::vector<sr::SettingsPanel::Row> rows(L.left.begin(), L.left.end());
    rows.insert(rows.end(), L.right.begin(), L.right.end());
    if (panel.hasInputSelector())
        rows.push_back(L.input);
    return rows;
}

void checkLayout(sr::SettingsPanel& panel)
{
    panel.setSize(sr::SettingsPanel::panelW, panel.panelHeight());
    const auto bounds = panel.getLocalBounds();
    const auto L = panel.layout();

    // Every child is fully inside the panel, with room to spare at the edges.
    for (auto* child : panel.getChildren())
    {
        INFO("child " << child->getTitle() << " " << child->getBounds().toString());
        CHECK(child->isVisible());
        CHECK(bounds.reduced(4).contains(child->getBounds()));
        CHECK(child->getHeight() > 0);
    }

    // The last thing painted (the ABOUT block) ends at the bottom padding.
    CHECK(L.licence.getBottom() <= panel.getHeight() - sr::SettingsPanel::kPad + 3);
    CHECK(L.aboutDetail.getBottom() <= panel.getHeight() - sr::SettingsPanel::kPad);

    const auto rows = allRows(panel);
    const auto font = sr::SettingsPanel::rowLabelFont();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& r = rows[i];
        INFO("row " << r.label);
        // The label fits its column, and the control sits beside it, not on it.
        CHECK(font.getStringWidthFloat(r.label) <= static_cast<float>(r.labelArea.getWidth()));
        CHECK_FALSE(r.labelArea.intersects(r.control));
        CHECK(r.control.getHeight() == sr::SettingsPanel::kControlH);
        CHECK(r.control.getWidth() >= 120);   // room for four segments ("100%")
        CHECK(bounds.contains(r.control));

        // No two rows' controls overlap.
        for (size_t j = i + 1; j < rows.size(); ++j)
        {
            INFO("against " << rows[j].label);
            CHECK_FALSE(r.control.intersects(rows[j].control));
            CHECK_FALSE(r.labelArea.intersects(rows[j].labelArea));
        }
    }

    // The two columns line up row for row.
    for (size_t i = 0; i < L.left.size(); ++i)
        CHECK(L.left[i].control.getY() == L.right[i].control.getY());

    // The close button sits in the title row, clear of the title text.
    CHECK_FALSE(L.close.intersects(L.title));
    CHECK(L.close.getY() >= 0);
}

} // namespace

TEST_CASE("SettingsPanel: every control fits, nothing overlaps", "[settings]")
{
    sr::SettingsPanel panel;
    checkLayout(panel);
}

TEST_CASE("SettingsPanel: the AUDIO INPUT section keeps the layout clean", "[settings]")
{
    juce::AudioDeviceManager dm;   // never opened: the list is just empty
    sr::SettingsPanel panel;
    panel.attachInputSelector(dm);
    REQUIRE(panel.hasInputSelector());
    checkLayout(panel);

    // The section sits above the preferences, which move down by exactly its height.
    sr::SettingsPanel plain;
    plain.setSize(sr::SettingsPanel::panelW, plain.panelHeight());
    CHECK(panel.layout().leftHead.getY()
          == plain.layout().leftHead.getY() + sr::SettingsPanel::kInputSectionH);
    CHECK(panel.layout().input.control.getBottom() < panel.layout().leftHead.getY());
}

TEST_CASE("SettingsPanel: toggles get the room they paint in", "[settings]")
{
    sr::SettingsPanel panel;
    panel.setSize(sr::SettingsPanel::panelW, panel.panelHeight());
    int toggles = 0;
    for (auto* child : panel.getChildren())
        if (auto* t = dynamic_cast<sr::ToggleSwitch*>(child))
        {
            ++toggles;
            INFO("toggle " << t->getTitle());
            CHECK(t->getHeight() >= sr::ToggleSwitch::minHeight(sr::ToggleSwitch::Style::Jewel));
        }
    CHECK(toggles == 2);   // PEAK HOLD and TOOLTIPS
}

TEST_CASE("SettingsPanel: close button and Escape both close it", "[settings]")
{
    sr::SettingsPanel panel;
    panel.setSize(sr::SettingsPanel::panelW, panel.panelHeight());
    int closes = 0;
    panel.onClose = [&] { ++closes; };

    // triggerClick() posts the click, and the plugin target has no modal loop
    // to deliver it; the button's own handler is what a click runs.
    REQUIRE(panel.closeButton().onClick != nullptr);
    panel.closeButton().onClick();
    CHECK(closes == 1);
    CHECK(panel.closeButton().getWidth() >= sr::SettingsPanel::kCloseSize);

    CHECK(panel.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK(closes == 2);
    CHECK_FALSE(panel.keyPressed(juce::KeyPress('a')));
    CHECK(closes == 2);
}
