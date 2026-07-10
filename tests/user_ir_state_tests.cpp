// User IR State Tests — ValueTree (de)serialization + untrusted-state validation
// Requires JUCE (separate test target: SilentRiotJuceTests)

#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "dsp/cabinet/UserIRState.h"

using namespace sr;

namespace {

UserIREntry makeEntry(const juce::String& name, int numChannels, int numSamples,
                      double sampleRate = 48000.0)
{
    UserIREntry e;
    e.name = name;
    e.sampleRate = sampleRate;
    e.numChannels = numChannels;
    e.numSamples = numSamples;
    e.audioData.setSize(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            e.audioData.setSample(ch, s, static_cast<float>(ch * 1000 + s) * 0.001f);
    return e;
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// Round-trip
// ═══════════════════════════════════════════════════════════════

TEST_CASE("User IR state round-trips mono and stereo entries", "[cabinet][ir][state]")
{
    std::vector<UserIREntry> in;
    in.push_back(makeEntry("mono_ir", 1, 512, 44100.0));
    in.push_back(makeEntry("stereo_ir", 2, 2048, 96000.0));

    auto out = userIRsFromValueTree(userIRsToValueTree(in));

    REQUIRE(out.size() == 2);
    for (size_t i = 0; i < 2; ++i)
    {
        REQUIRE(out[i].name == in[i].name);
        REQUIRE(out[i].sampleRate == in[i].sampleRate);
        REQUIRE(out[i].numChannels == in[i].numChannels);
        REQUIRE(out[i].numSamples == in[i].numSamples);
        for (int ch = 0; ch < in[i].numChannels; ++ch)
            for (int s = 0; s < in[i].numSamples; ++s)
                REQUIRE(out[i].audioData.getSample(ch, s)
                        == in[i].audioData.getSample(ch, s));
    }
}

TEST_CASE("Invalid or missing node yields empty list", "[cabinet][ir][state]")
{
    REQUIRE(userIRsFromValueTree(juce::ValueTree()).empty());
    REQUIRE(userIRsFromValueTree(juce::ValueTree("SomethingElse")).empty());
}

// ═══════════════════════════════════════════════════════════════
// Untrusted-state validation (state comes from DAW project files)
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Truncated audio blob rejects entry and stops parsing", "[cabinet][ir][state]")
{
    std::vector<UserIREntry> in;
    in.push_back(makeEntry("ok", 1, 256));
    in.push_back(makeEntry("corrupt", 2, 1024));
    in.push_back(makeEntry("after_corrupt", 1, 256));

    auto tree = userIRsToValueTree(in);

    // Truncate the second entry's blob so it can't hold numChannels*numSamples floats
    juce::MemoryBlock small(64, true);
    tree.getChild(1).setProperty("audioData", juce::var(small), nullptr);

    auto out = userIRsFromValueTree(tree);

    // Parsing stops at the corrupt entry — keeping later entries would shift
    // their 1-based IR_INDEX mapping.
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].name == "ok");
}

TEST_CASE("Out-of-range dimensions are rejected", "[cabinet][ir][state]")
{
    auto corrupt = [](const char* prop, const juce::var& value) {
        std::vector<UserIREntry> in;
        in.push_back(makeEntry("bad", 1, 256));
        auto tree = userIRsToValueTree(in);
        tree.getChild(0).setProperty(prop, value, nullptr);
        return userIRsFromValueTree(tree);
    };

    REQUIRE(corrupt("numChannels", 0).empty());
    REQUIRE(corrupt("numChannels", 3).empty());
    REQUIRE(corrupt("numChannels", -1).empty());
    REQUIRE(corrupt("numSamples", 0).empty());
    REQUIRE(corrupt("numSamples", -5).empty());
    REQUIRE(corrupt("numSamples", MAX_USER_IR_SAMPLES + 1).empty());
    REQUIRE(corrupt("sampleRate", 0.0).empty());
    REQUIRE(corrupt("sampleRate", -48000.0).empty());
    REQUIRE(corrupt("sampleRate", 1.0e9).empty());
}

TEST_CASE("Missing audioData property is rejected", "[cabinet][ir][state]")
{
    std::vector<UserIREntry> in;
    in.push_back(makeEntry("no_audio", 1, 256));
    auto tree = userIRsToValueTree(in);
    tree.getChild(0).removeProperty("audioData", nullptr);

    REQUIRE(userIRsFromValueTree(tree).empty());
}

TEST_CASE("Entry count is capped at MAX_USER_IRS", "[cabinet][ir][state]")
{
    std::vector<UserIREntry> in;
    for (int i = 0; i < MAX_USER_IRS + 5; ++i)
        in.push_back(makeEntry("ir_" + juce::String(i), 1, 16));

    auto out = userIRsFromValueTree(userIRsToValueTree(in));

    REQUIRE(static_cast<int>(out.size()) == MAX_USER_IRS);
    REQUIRE(out.back().name == "ir_" + juce::String(MAX_USER_IRS - 1));
}
