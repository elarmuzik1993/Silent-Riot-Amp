// ═══════════════════════════════════════════════════════════════
// PitchDetector Tests — live-tuner pitch tracking (MPM / NSDF)
//
//   - Detects standard guitar open-string frequencies within a few cents
//   - Resolves frequencies to the correct chromatic note + octave
//   - Reports low clarity (no pitch) on silence and white noise
// ═══════════════════════════════════════════════════════════════

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/analysis/PitchDetector.h"
#include <cmath>
#include <vector>
#include <random>
#include <string>

using namespace sr;

namespace {
constexpr double kSR = 48000.0;
constexpr float  kPi = 3.14159265358979323846f;

std::vector<float> makeSine(float freq, int n, float amp = 0.3f)
{
    std::vector<float> buf(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        buf[static_cast<size_t>(i)] = amp * std::sin(2.0f * kPi * freq * i / static_cast<float>(kSR));
    return buf;
}

// Cents error between a detected frequency and the target.
float centsError(float detected, float target)
{
    return 1200.0f * std::log2(detected / target);
}
} // namespace

TEST_CASE("PitchDetector tracks standard guitar strings", "[pitch]")
{
    PitchDetector det;
    det.prepare(kSR);
    const int n = det.getWindowSize();

    struct Str { float hz; const char* name; };
    const Str strings[] = {
        { 82.41f,  "E2" }, { 110.00f, "A2" }, { 146.83f, "D3" },
        { 196.00f, "G3" }, { 246.94f, "B3" }, { 329.63f, "E4" },
    };

    for (const auto& s : strings)
    {
        auto buf = makeSine(s.hz, n);
        auto res = det.process(buf.data(), n);
        REQUIRE(res.frequencyHz > 0.0f);
        REQUIRE(std::abs(centsError(res.frequencyHz, s.hz)) < 5.0f);
        REQUIRE(res.clarity > 0.8f);
    }
}

TEST_CASE("frequencyToNote resolves note names and octaves", "[pitch]")
{
    auto a4 = frequencyToNote(440.0f);
    REQUIRE(a4.valid);
    REQUIRE(std::string(a4.name) == "A");
    REQUIRE(a4.octave == 4);
    REQUIRE(a4.cents == Catch::Approx(0.0f).margin(0.01f));

    auto e2 = frequencyToNote(82.41f);
    REQUIRE(std::string(e2.name) == "E");
    REQUIRE(e2.octave == 2);
    REQUIRE(std::abs(e2.cents) < 1.0f);

    auto c4 = frequencyToNote(261.63f);   // middle C
    REQUIRE(std::string(c4.name) == "C");
    REQUIRE(c4.octave == 4);
}

TEST_CASE("frequencyToNote reports sharp and flat offsets", "[pitch]")
{
    // 15 cents sharp of A4
    float sharp = 440.0f * std::pow(2.0f, 15.0f / 1200.0f);
    auto n = frequencyToNote(sharp);
    REQUIRE(std::string(n.name) == "A");
    REQUIRE(n.cents == Catch::Approx(15.0f).margin(0.5f));

    // 20 cents flat of A4
    float flat = 440.0f * std::pow(2.0f, -20.0f / 1200.0f);
    auto f = frequencyToNote(flat);
    REQUIRE(std::string(f.name) == "A");
    REQUIRE(f.cents == Catch::Approx(-20.0f).margin(0.5f));
}

TEST_CASE("PitchDetector rejects silence", "[pitch]")
{
    PitchDetector det;
    det.prepare(kSR);
    const int n = det.getWindowSize();
    std::vector<float> silence(static_cast<size_t>(n), 0.0f);
    auto res = det.process(silence.data(), n);
    REQUIRE(res.frequencyHz == 0.0f);
}

TEST_CASE("PitchDetector rejects white noise", "[pitch]")
{
    PitchDetector det;
    det.prepare(kSR);
    const int n = det.getWindowSize();

    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-0.3f, 0.3f);
    std::vector<float> noise(static_cast<size_t>(n));
    for (auto& s : noise) s = dist(rng);

    auto res = det.process(noise.data(), n);
    // Noise should not produce a confident pitch.
    REQUIRE(res.frequencyHz == 0.0f);
}

TEST_CASE("frequencyToNote rejects invalid input", "[pitch]")
{
    REQUIRE_FALSE(frequencyToNote(0.0f).valid);
    REQUIRE_FALSE(frequencyToNote(-100.0f).valid);
}

// ── Regression: the detector's assumed rate must match the audio's ──
// process() converts a lag in samples to Hz via its own `sampleRate`, so the
// rate handed to prepare() is load-bearing: it silently transposes every
// reading if it disagrees with the audio actually being captured. The editor
// must therefore re-prepare whenever the host's rate changes (it may also build
// the editor before prepareToPlay, when getSampleRate() is still 0).
namespace {

std::vector<float> makeSineAt(double sampleRate, float freq, int n, float amp = 0.3f)
{
    std::vector<float> buf(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        buf[static_cast<size_t>(i)] =
            amp * std::sin(2.0f * kPi * freq * i / static_cast<float>(sampleRate));
    return buf;
}

} // namespace

TEST_CASE("PitchDetector is accurate at every supported sample rate", "[pitch][regression]")
{
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        PitchDetector det;
        det.prepare(rate);
        auto buf = makeSineAt(rate, 440.0f, det.getWindowSize());
        auto res = det.process(buf.data(), static_cast<int>(buf.size()));

        INFO("sample rate " << rate);
        REQUIRE(res.frequencyHz > 0.0f);
        REQUIRE(std::abs(1200.0f * std::log2(res.frequencyHz / 440.0f)) < 5.0f);

        const NoteInfo note = frequencyToNote(res.frequencyHz);
        REQUIRE(note.valid);
        REQUIRE(std::string(note.name) == "A");
        REQUIRE(note.octave == 4);
    }
}

TEST_CASE("PitchDetector transposes when prepared for the wrong rate", "[pitch][regression]")
{
    // Characterises the failure the editor's re-prepare exists to prevent: a
    // detector left at 44100 while the host runs at 48000 reports a true A4 as
    // a badly flat G#4. Guards against anyone "simplifying" prepare() away.
    PitchDetector det;
    det.prepare(44100.0);
    auto buf = makeSineAt(48000.0, 440.0f, det.getWindowSize());
    auto res = det.process(buf.data(), static_cast<int>(buf.size()));

    REQUIRE(res.frequencyHz > 0.0f);
    const float centsOff = 1200.0f * std::log2(res.frequencyHz / 440.0f);
    REQUIRE(centsOff < -100.0f);                       // more than a semitone flat
    REQUIRE(std::string(frequencyToNote(res.frequencyHz).name) == "G#");
}
