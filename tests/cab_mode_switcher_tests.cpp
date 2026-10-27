// CabModeSwitcher (IR-path plan Task 3): the fade state machine behind every
// change between CAB off, the built-in cab and IR mode. Pure C++; the
// processor wiring is tested in processor_ir_tests.cpp.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "dsp/cabinet/CabModeSwitcher.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

using namespace sr;

namespace {

constexpr float kStep = 1.0f / static_cast<float>(CabModeSwitcher::kFadeSamples);

struct Trace
{
    std::vector<float>   gain;     // per sample
    std::vector<CabMode> mode;     // active mode per sample
    std::vector<int>     commits;  // sample index of each commit (block start)
};

// Runs `total` samples in blocks of `block`. Each block asks for
// request(blockStartSample) with irReady(blockStartSample).
Trace run(CabModeSwitcher& sw, int total, int block,
          const std::function<CabMode(int)>& request,
          const std::function<bool(int)>& irReady = [](int) { return true; })
{
    Trace t;
    for (int start = 0; start < total; start += block)
    {
        const int n = std::min(block, total - start);
        if (sw.beginBlock(request(start), irReady(start)))
            t.commits.push_back(start);
        // Exercise applyGain, the processor's entry point, on a unit block.
        std::vector<float> ones(static_cast<size_t>(n), 1.0f);
        float* ch[1] = { ones.data() };
        sw.applyGain(ch, 1, n);
        for (int i = 0; i < n; ++i)
        {
            t.gain.push_back(ones[static_cast<size_t>(i)]);
            t.mode.push_back(sw.activeMode());
        }
    }
    return t;
}

float maxStep(const std::vector<float>& g)
{
    float m = std::abs(1.0f - g.front());   // the gain before the run is 1
    for (size_t i = 1; i < g.size(); ++i)
        m = std::max(m, std::abs(g[i] - g[i - 1]));
    return m;
}

} // namespace

TEST_CASE("CabModeSwitcher: an unchanged mode is gain exactly 1 with no commit", "[cab][switcher]")
{
    const CabMode mode = GENERATE(CabMode::Off, CabMode::BuiltIn, CabMode::IR);
    CabModeSwitcher sw;
    sw.reset(mode);
    const Trace t = run(sw, 10000, 64, [=](int) { return mode; });
    REQUIRE(t.commits.empty());
    REQUIRE(std::all_of(t.gain.begin(), t.gain.end(), [](float g) { return g == 1.0f; }));
    REQUIRE(sw.isIdle());
    REQUIRE(sw.activeMode() == mode);
}

TEST_CASE("CabModeSwitcher: built-in -> Off dips to exactly 0, commits once, returns to 1",
          "[cab][switcher]")
{
    const int block = GENERATE(1, 7, 64, 512, 4096);
    CabModeSwitcher sw;
    sw.reset(CabMode::BuiltIn);
    const int switchAt = 3 * block;   // a block start
    const Trace t = run(sw, switchAt + 2 * block + 4 * CabModeSwitcher::kFadeSamples, block,
                        [=](int s) { return s >= switchAt ? CabMode::Off : CabMode::BuiltIn; });

    INFO("block " << block);
    REQUIRE(t.commits.size() == 1);
    const int commit = t.commits[0];
    // Gain 1 before the request, exactly 0 just before the commit.
    REQUIRE(std::all_of(t.gain.begin(), t.gain.begin() + switchAt, [](float g) { return g == 1.0f; }));
    REQUIRE(t.gain[static_cast<size_t>(commit - 1)] == 0.0f);
    // Old mode until the commit, new mode from it.
    REQUIRE(t.mode[static_cast<size_t>(commit - 1)] == CabMode::BuiltIn);
    REQUIRE(t.mode[static_cast<size_t>(commit)] == CabMode::Off);
    // The fade-out takes exactly kFadeSamples; then silence to the block end.
    REQUIRE(t.gain[static_cast<size_t>(switchAt + CabModeSwitcher::kFadeSamples - 1)] == 0.0f);
    REQUIRE(t.gain[static_cast<size_t>(switchAt + CabModeSwitcher::kFadeSamples - 2)] > 0.0f);
    // Back to exactly 1, and idle.
    REQUIRE(t.gain.back() == 1.0f);
    REQUIRE(sw.isIdle());
    REQUIRE(maxStep(t.gain) <= kStep * (1.0f + 1e-6f));
}

TEST_CASE("CabModeSwitcher: -> IR waits at full gain until the IR is ready", "[cab][switcher]")
{
    const int block = GENERATE(1, 7, 64, 512, 4096);
    CabModeSwitcher sw;
    sw.reset(CabMode::BuiltIn);
    const int readyAt = 20 * block + 1000;   // not a block start
    const Trace t = run(sw, readyAt + 3 * block + 4 * CabModeSwitcher::kFadeSamples, block,
                        [](int) { return CabMode::IR; },
                        [=](int s) { return s >= readyAt; });

    INFO("block " << block);
    REQUIRE(t.commits.size() == 1);
    const int firstReadyBlock = ((readyAt + block - 1) / block) * block;
    // Full gain and the built-in cab for every block that saw the IR not ready.
    for (int i = 0; i < firstReadyBlock; ++i)
    {
        REQUIRE(t.gain[static_cast<size_t>(i)] == 1.0f);
        REQUIRE(t.mode[static_cast<size_t>(i)] == CabMode::BuiltIn);
    }
    REQUIRE(t.commits[0] > firstReadyBlock);
    REQUIRE(t.gain[static_cast<size_t>(t.commits[0] - 1)] == 0.0f);
    REQUIRE(sw.activeMode() == CabMode::IR);
    REQUIRE(t.gain.back() == 1.0f);
    REQUIRE(maxStep(t.gain) <= kStep * (1.0f + 1e-6f));
}

TEST_CASE("CabModeSwitcher: waiting for an IR that never arrives never commits", "[cab][switcher]")
{
    CabModeSwitcher sw;
    sw.reset(CabMode::Off);
    const Trace t = run(sw, 100000, 512, [](int) { return CabMode::IR; }, [](int) { return false; });
    REQUIRE(t.commits.empty());
    REQUIRE(std::all_of(t.gain.begin(), t.gain.end(), [](float g) { return g == 1.0f; }));
    REQUIRE(sw.isWaitingForIR());
    REQUIRE(sw.waitedSamples() == 100000);
}

TEST_CASE("CabModeSwitcher: a request reversed mid-fade-out never commits the abandoned mode",
          "[cab][switcher]")
{
    const int block = GENERATE(1, 7, 64);   // a fade-out spans several blocks
    CabModeSwitcher sw;
    sw.reset(CabMode::IR);
    const int switchAt = 2 * block;
    // Ask for built-in for ~half a fade, then IR again.
    const int backAt = switchAt + ((CabModeSwitcher::kFadeSamples / 2) / block) * block;
    const Trace t = run(sw, backAt + 4 * CabModeSwitcher::kFadeSamples, block,
                        [=](int s) { return s >= switchAt && s < backAt ? CabMode::BuiltIn : CabMode::IR; });

    INFO("block " << block);
    REQUIRE(t.commits.empty());
    REQUIRE(std::all_of(t.mode.begin(), t.mode.end(), [](CabMode m) { return m == CabMode::IR; }));
    const float lowest = *std::min_element(t.gain.begin(), t.gain.end());
    REQUIRE(lowest > 0.0f);     // turned back before reaching 0
    REQUIRE(lowest < 1.0f);     // but it did start fading
    REQUIRE(t.gain.back() == 1.0f);
    REQUIRE(maxStep(t.gain) <= kStep * (1.0f + 1e-6f));
}

TEST_CASE("CabModeSwitcher: a request reversed after gain 0 but before the commit never commits",
          "[cab][switcher]")
{
    // Block 512: the fade-out ends mid-block and the rest of it is silent;
    // the request turns back at the next block start, where the commit
    // would have been.
    CabModeSwitcher sw;
    sw.reset(CabMode::BuiltIn);
    const Trace t = run(sw, 4 * 512, 512,
                        [](int s) { return s == 512 ? CabMode::Off : CabMode::BuiltIn; });
    REQUIRE(t.commits.empty());
    REQUIRE(t.gain[1023] == 0.0f);
    REQUIRE(t.gain.back() == 1.0f);
    REQUIRE(sw.activeMode() == CabMode::BuiltIn);
    REQUIRE(maxStep(t.gain) <= kStep * (1.0f + 1e-6f));
}

TEST_CASE("CabModeSwitcher: a request during fade-in fades out from the current gain",
          "[cab][switcher]")
{
    const int block = GENERATE(1, 7, 16);   // a fade-in spans several blocks
    CabModeSwitcher sw;
    sw.reset(CabMode::BuiltIn);
    // Off at sample 0; once committed and part-way up, ask for IR.
    int commitAt = -1;
    const Trace t = run(sw, 2000, block, [&](int s) {
        if (commitAt < 0 && sw.activeMode() == CabMode::Off) commitAt = s;
        return (commitAt >= 0 && s >= commitAt + CabModeSwitcher::kFadeSamples / 4) ? CabMode::IR
                                                                                   : CabMode::Off;
    });

    INFO("block " << block);
    REQUIRE(t.commits.size() == 2);
    REQUIRE(sw.activeMode() == CabMode::IR);
    // Between the commits the gain rose, then fell, with no step.
    const auto from = t.gain.begin() + t.commits[0];
    const auto to = t.gain.begin() + t.commits[1];
    const float peak = *std::max_element(from, to);
    REQUIRE(peak > 0.0f);
    REQUIRE(peak < 1.0f);   // never reached full gain in Off
    REQUIRE(t.gain[static_cast<size_t>(t.commits[1] - 1)] == 0.0f);
    REQUIRE(t.gain.back() == 1.0f);
    REQUIRE(maxStep(t.gain) <= kStep * (1.0f + 1e-6f));
}

TEST_CASE("CabModeSwitcher: holdSilence after a commit delays the fade-in", "[cab][switcher]")
{
    const int block = GENERATE(1, 7, 64, 512);
    const int hold = GENERATE(0, 12, 300);
    CabModeSwitcher sw;
    sw.reset(CabMode::Off);
    std::vector<float> g;
    int commit = -1;
    for (int start = 0; start < 4096; start += block)
    {
        if (sw.beginBlock(CabMode::IR, true))
        {
            commit = start;
            sw.holdSilence(hold);
        }
        for (int i = 0; i < block; ++i) g.push_back(sw.nextGain());
    }
    INFO("block " << block << ", hold " << hold);
    REQUIRE(commit > 0);
    for (int i = commit; i < commit + hold; ++i)
        REQUIRE(g[static_cast<size_t>(i)] == 0.0f);
    REQUIRE(g[static_cast<size_t>(commit + hold)] == kStep);
    REQUIRE(g.back() == 1.0f);
    REQUIRE(maxStep(g) <= kStep * (1.0f + 1e-6f));
}

TEST_CASE("CabModeSwitcher: requestedCabMode", "[cab][switcher]")
{
    REQUIRE(requestedCabMode(false, false) == CabMode::Off);
    REQUIRE(requestedCabMode(false, true) == CabMode::Off);
    REQUIRE(requestedCabMode(true, false) == CabMode::BuiltIn);
    REQUIRE(requestedCabMode(true, true) == CabMode::IR);
}
