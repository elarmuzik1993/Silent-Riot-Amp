// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "utils/VoltageScale.h"
using namespace sr;
using Catch::Approx;

// ═══════════════════════════════════════════════════════════════
// Spec Section 2.5 — Voltage Reference Table
// ═══════════════════════════════════════════════════════════════

TEST_CASE("VS: B+ 300V maps to 1.0", "[voltage_scale]") {
    REQUIRE(VS::v(300.0f) == Approx(1.0f));
}

TEST_CASE("VS: ground 0V maps to 0.0", "[voltage_scale]") {
    REQUIRE(VS::v(0.0f) == Approx(0.0f));
}

TEST_CASE("VS: thermal voltage 26mV maps to 0.0000867", "[voltage_scale]") {
    REQUIRE(VS::v(0.026f) == Approx(0.0000867f).margin(1e-6f));
}

TEST_CASE("VS: guitar +/-0.5V maps to +/-0.00167", "[voltage_scale]") {
    REQUIRE(VS::v(0.5f) == Approx(0.001667f).margin(1e-5f));
    REQUIRE(VS::v(-0.5f) == Approx(-0.001667f).margin(1e-5f));
}

TEST_CASE("VS: grid bias -1.5V maps to -0.005", "[voltage_scale]") {
    REQUIRE(VS::v(-1.5f) == Approx(-0.005f));
}

TEST_CASE("VS: plate swing +/-60V maps to +/-0.2", "[voltage_scale]") {
    REQUIRE(VS::v(60.0f) == Approx(0.2f));
    REQUIRE(VS::v(-60.0f) == Approx(-0.2f));
}

TEST_CASE("VS: max plate swing +/-150V maps to +/-0.5", "[voltage_scale]") {
    REQUIRE(VS::v(150.0f) == Approx(0.5f));
}

TEST_CASE("VS: preamp supply 250V maps to 0.833", "[voltage_scale]") {
    REQUIRE(VS::v(250.0f) == Approx(0.8333f).margin(1e-3f));
}

TEST_CASE("VS: PI/screen supply 280V maps to 0.933", "[voltage_scale]") {
    REQUIRE(VS::v(280.0f) == Approx(0.9333f).margin(1e-3f));
}

// ═══════════════════════════════════════════════════════════════
// Spec Section 2.5 — Resistance Reference Table
// ═══════════════════════════════════════════════════════════════

TEST_CASE("VS: 100 ohm maps to 0.000333", "[voltage_scale]") {
    REQUIRE(VS::R_100 == Approx(0.000333f).margin(1e-5f));
}

TEST_CASE("VS: 300 ohm maps to 0.001", "[voltage_scale]") {
    REQUIRE(VS::R_300 == Approx(0.001f));
}

TEST_CASE("VS: 1.2K maps to 0.004", "[voltage_scale]") {
    REQUIRE(VS::R_1K2 == Approx(0.004f));
}

TEST_CASE("VS: 1.5K maps to 0.005", "[voltage_scale]") {
    REQUIRE(VS::R_1K5 == Approx(0.005f));
}

TEST_CASE("VS: 68K maps to 0.227", "[voltage_scale]") {
    REQUIRE(VS::R_68K == Approx(0.2267f).margin(1e-3f));
}

TEST_CASE("VS: 100K maps to 0.333", "[voltage_scale]") {
    REQUIRE(VS::R_100K == Approx(0.3333f).margin(1e-3f));
}

TEST_CASE("VS: 500K maps to 1.667", "[voltage_scale]") {
    REQUIRE(VS::R_500K == Approx(1.6667f).margin(1e-3f));
}

TEST_CASE("VS: 1M maps to 3.333", "[voltage_scale]") {
    REQUIRE(VS::R_1M == Approx(3.3333f).margin(1e-3f));
}

// ═══════════════════════════════════════════════════════════════
// Ohm's law preserved in normalized domain
// ═══════════════════════════════════════════════════════════════

TEST_CASE("VS: Ohm's law - 1mA through 100K = 100V", "[voltage_scale]") {
    float i_n = VS::i(0.001f);
    float r_n = VS::R_100K;
    float v_n = i_n * r_n;
    REQUIRE(VS::toVolts(v_n) == Approx(100.0f).margin(0.01f));
}

TEST_CASE("VS: Ohm's law - 1.2mA through 62.5K = 75V", "[voltage_scale]") {
    float i_n = VS::i(0.0012f);
    float r_n = VS::r(62500.0f);
    float v_n = i_n * r_n;
    REQUIRE(VS::toVolts(v_n) == Approx(75.0f).margin(0.1f));
}

TEST_CASE("VS: Ohm's law - tail current through 1.2K", "[voltage_scale]") {
    float iTail_n = VS::i(0.0024f);
    float vCathode_n = iTail_n * VS::R_1K2;
    REQUIRE(VS::toVolts(vCathode_n) == Approx(2.88f).margin(0.01f));
}

// ═══════════════════════════════════════════════════════════════
// Round-trip conversions
// ═══════════════════════════════════════════════════════════════

TEST_CASE("VS: voltage round-trip", "[voltage_scale]") {
    REQUIRE(VS::toVolts(VS::v(123.45f)) == Approx(123.45f));
}

TEST_CASE("VS: resistance round-trip", "[voltage_scale]") {
    REQUIRE(VS::toOhms(VS::r(47000.0f)) == Approx(47000.0f));
}

TEST_CASE("VS: current round-trip", "[voltage_scale]") {
    REQUIRE(VS::toAmps(VS::i(0.0015f)) == Approx(0.0015f));
}

// ═══════════════════════════════════════════════════════════════
// Named constants cross-check
// ═══════════════════════════════════════════════════════════════

TEST_CASE("VS: reference constants", "[voltage_scale]") {
    REQUIRE(VS::V_REF == 300.0f);
    REQUIRE(VS::R_REF == 300000.0f);
    REQUIRE(VS::I_REF == Approx(0.001f));
}

TEST_CASE("VS: named voltage constants match spec", "[voltage_scale]") {
    REQUIRE(VS::V_B_PLUS == Approx(1.0f));
    REQUIRE(VS::V_SCREEN == Approx(0.9333f).margin(1e-3f));
    REQUIRE(VS::V_PI == VS::V_SCREEN);
    REQUIRE(VS::V_PREAMP == Approx(0.8333f).margin(1e-3f));
    REQUIRE(VS::V_GUITAR == Approx(0.001667f).margin(1e-5f));
    REQUIRE(VS::V_BIAS_12AX7 == Approx(-0.005f));
}

TEST_CASE("VS: V1a Zout is 100K||62.5K", "[voltage_scale]") {
    float expected = 100000.0f * 62500.0f / (100000.0f + 62500.0f);
    REQUIRE(VS::toOhms(VS::Z_OUT_V1A) == Approx(expected).margin(10.0f));
}

TEST_CASE("VS: guitar is tiny relative to supply", "[voltage_scale]") {
    REQUIRE(VS::V_GUITAR / VS::V_B_PLUS < 0.002f);
}
