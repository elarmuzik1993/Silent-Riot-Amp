// IR level metric (dsp/cabinet/IRLevel.h) — IR-path plan Task 2.
// Pure C++: the band-power metric every loaded IR is scaled by, and the
// built-in cab reference it is scaled to.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/cabinet/IRLevel.h"
#include "dsp/filters/Biquad.h"

#include <cmath>
#include <vector>

using namespace sr;
using Catch::Matchers::WithinAbs;

namespace {

double levelDb(const std::vector<float>& ir, double rate)
{
    const float* ch[] = { ir.data() };
    const auto p = irlevel::bandPower(ch, 1, static_cast<int>(ir.size()), rate);
    REQUIRE(p.has_value());
    return irlevel::powerToDb(*p);
}

// Impulse response of an 80 Hz–5 kHz band-pass (two RBJ high-passes and two
// RBJ low-passes, Q 0.707), designed at `rate` from the same analog corners.
std::vector<float> bandPassIR(double rate, int n)
{
    Biquad hp1, hp2, lp1, lp2;
    const float fs = static_cast<float>(rate);
    hp1.makeHighPass(80.0f, 0.7071f, fs);   hp2.makeHighPass(80.0f, 0.7071f, fs);
    lp1.makeLowPass(5000.0f, 0.7071f, fs);  lp2.makeLowPass(5000.0f, 0.7071f, fs);
    std::vector<float> ir(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const float x = i == 0 ? 1.0f : 0.0f;
        ir[static_cast<size_t>(i)] = lp2.process(lp1.process(hp2.process(hp1.process(x))));
    }
    return ir;
}

} // namespace

TEST_CASE("IR level: a unit impulse reads 0 dB and doubling it adds 6.02 dB", "[irlevel]")
{
    std::vector<float> ir(256, 0.0f);
    ir[0] = 1.0f;
    CHECK_THAT(levelDb(ir, 48000.0), WithinAbs(0.0, 0.01));
    ir[0] = 2.0f;
    CHECK_THAT(levelDb(ir, 48000.0), WithinAbs(20.0 * std::log10(2.0), 0.01));
    // A delayed impulse has the same magnitude response.
    ir[0] = 0.0f;
    ir[100] = 1.0f;
    CHECK_THAT(levelDb(ir, 48000.0), WithinAbs(0.0, 0.01));
}

TEST_CASE("IR level: the band has 69 log-spaced points, 100 Hz to 5 kHz", "[irlevel]")
{
    const auto band = irlevel::bandFor(48000.0);
    CHECK(band.points == 69);
    CHECK_THAT(band.loHz, WithinAbs(100.0, 1e-9));
    CHECK_THAT(band.hiHz, WithinAbs(5000.0, 1e-9));
}

TEST_CASE("IR level: one analog band-pass reads the same at 48 kHz and 96 kHz", "[irlevel]")
{
    // The same 170 ms either way, so the low-frequency tail is cut at the
    // same time, not at the same sample.
    const double at48 = levelDb(bandPassIR(48000.0, 8192), 48000.0);
    const double at96 = levelDb(bandPassIR(96000.0, 16384), 96000.0);
    INFO("48 kHz " << at48 << " dB, 96 kHz " << at96 << " dB");
    CHECK(std::abs(at48 - at96) < 0.2);
    CHECK(at48 < 0.0);   // a band-pass rolls off inside 100 Hz–5 kHz
}

TEST_CASE("IR level: the built-in cab reference is -0.90 dB", "[irlevel]")
{
    // Pinned: cab_presence × cab_rolloff × cab_notch over the band. A change to
    // any CAB_* constant moves this — and with it the level of every IR.
    const double refDb = irlevel::powerToDb(irlevel::builtInCabBandPower());
    INFO("built-in cab band level " << refDb << " dB");
    CHECK_THAT(refDb, WithinAbs(-0.90, 0.05));

    // It is the processor's own biquads: designed at an oversampled rate, the
    // same three filters read within 0.02 dB of the reference at every OS
    // factor of 44.1 kHz and 48 kHz.
    for (double rate : { 88200.0, 96000.0, 192000.0, 352800.0, 384000.0 })
        CHECK_THAT(irlevel::powerToDb(irlevel::builtInCabBandPower(irlevel::fullBand(), rate)),
                   WithinAbs(refDb, 0.02));
}

TEST_CASE("IR level: an all-zero IR has no level and is left unscaled", "[irlevel]")
{
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    const float* ch[] = { l.data(), r.data() };
    CHECK_FALSE(irlevel::bandPower(ch, 2, 512, 48000.0).has_value());
    CHECK_FALSE(irlevel::bandPower(ch, 0, 512, 48000.0).has_value());
    CHECK_FALSE(irlevel::bandPower(ch, 2, 0, 48000.0).has_value());
    CHECK(irlevel::levelMatchGain(std::nullopt, 48000.0) == 1.0);
}

TEST_CASE("IR level: gain matches the reference and averages the channels", "[irlevel]")
{
    // Unit impulse on the left, half on the right: channel-averaged power
    // (1 + 0.25) / 2. One gain for both channels.
    std::vector<float> l(64, 0.0f), r(64, 0.0f);
    l[0] = 1.0f;
    r[0] = 0.5f;
    const float* ch[] = { l.data(), r.data() };
    const auto p = irlevel::bandPower(ch, 2, 64, 48000.0);
    REQUIRE(p.has_value());
    CHECK_THAT(*p, WithinAbs(0.625, 1e-6));
    const double g = irlevel::levelMatchGain(p, 48000.0);
    CHECK_THAT(irlevel::powerToDb(g * g * *p),
               WithinAbs(irlevel::powerToDb(irlevel::builtInCabBandPower()), 1e-9));

    // Capped for an IR with next to nothing in band.
    CHECK_THAT(20.0 * std::log10(irlevel::levelMatchGain(1e-20, 48000.0)),
               WithinAbs(IR_LEVEL_MAX_GAIN_DB, 1e-9));
}

TEST_CASE("IR level: the band's top edge is clamped to 0.45 x the IR's rate", "[irlevel]")
{
    const auto band = irlevel::bandFor(8000.0);
    CHECK_THAT(band.hiHz, WithinAbs(3600.0, 1e-9));
    CHECK(band.points == 64);   // ceil(12 × log2(36)) + 1

    std::vector<float> ir(1024, 0.0f);
    ir[0] = 1.0f;
    CHECK_THAT(levelDb(ir, 8000.0), WithinAbs(0.0, 0.01));
}

TEST_CASE("IR level: a low-rate IR is matched to the cab over its own clamped band", "[irlevel]")
{
    // The reference is the built-in cab over the band the IR is measured
    // over, not the full 100 Hz–5 kHz: at 8 kHz that band stops at 3.6 kHz,
    // below the cab's 5.5 kHz roll-off, and reads -0.72 dB, not -0.89.
    const double fullDb = irlevel::powerToDb(irlevel::builtInCabBandPower());
    const double at8kDb = irlevel::powerToDb(irlevel::referenceBandPower(8000.0));
    INFO("reference: full band " << fullDb << " dB, 8 kHz band " << at8kDb << " dB");
    CHECK_THAT(at8kDb, WithinAbs(-0.72, 0.05));
    CHECK(std::abs(at8kDb - fullDb) > 0.1);   // the clamp makes a difference here
    CHECK_THAT(irlevel::powerToDb(irlevel::referenceBandPower(44100.0)), WithinAbs(fullDb, 1e-12));

    // A unit impulse (band power 1 at any rate) gets the clamped reference's gain.
    std::vector<float> ir(1024, 0.0f);
    ir[0] = 1.0f;
    const float* ch[] = { ir.data() };
    const double g = irlevel::levelMatchGain(irlevel::bandPower(ch, 1, 1024, 8000.0), 8000.0);
    CHECK_THAT(20.0 * std::log10(g), WithinAbs(at8kDb, 1e-9));
}

TEST_CASE("IR level: the built-in cab's own response needs no gain", "[irlevel]")
{
    // The three cab biquads' impulse response, sampled at 48 kHz, reads at
    // the reference to within the bilinear warp between 48 and 176.4 kHz.
    Biquad presence, rolloff, notch;
    presence.makePeakEQ(CAB_PRESENCE_FREQ, CAB_PRESENCE_Q, CAB_PRESENCE_GAIN_DB, 48000.0f);
    rolloff.makeLowPass(CAB_ROLLOFF_FREQ, CAB_ROLLOFF_Q, 48000.0f);
    notch.makeNotch(CAB_NOTCH_FREQ, CAB_NOTCH_Q, 48000.0f);
    std::vector<float> ir(8192);
    for (size_t i = 0; i < ir.size(); ++i)
        ir[i] = notch.process(rolloff.process(presence.process(i == 0 ? 1.0f : 0.0f)));
    const float* ch[] = { ir.data() };
    const double g = irlevel::levelMatchGain(irlevel::bandPower(ch, 1, 8192, 48000.0), 48000.0);
    INFO("gain " << 20.0 * std::log10(g) << " dB");
    CHECK(std::abs(20.0 * std::log10(g)) < 0.1);
}
