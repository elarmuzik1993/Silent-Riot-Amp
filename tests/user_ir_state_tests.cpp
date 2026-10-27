// User IR State Tests — ValueTree (de)serialization + untrusted-state validation
// Requires JUCE (separate test target: SilentRiotJuceTests)

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <limits>
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

TEST_CASE("A non-IR child stops parsing instead of being skipped", "[cabinet][ir][state]")
{
    // The writer emits only "IR" children, so a foreign child is corruption —
    // e.g. an IR node whose type was mangled. Skipping it would shift every
    // later IR down one slot, so IR_INDEX would play the wrong IR.
    std::vector<UserIREntry> in;
    in.push_back(makeEntry("ok", 1, 256));
    in.push_back(makeEntry("mangled", 1, 256));
    in.push_back(makeEntry("after_mangled", 1, 256));

    auto tree = userIRsToValueTree(in);
    auto mangled = tree.getChild(1).createCopy();
    tree.removeChild(1, nullptr);
    juce::ValueTree foreign("Iq");
    foreign.copyPropertiesFrom(mangled, nullptr);
    tree.addChild(foreign, 1, nullptr);

    auto out = userIRsFromValueTree(tree);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].name == "ok");
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

// ═══════════════════════════════════════════════════════════════
// Shared format validation (load path and restore path share this,
// so an IR that loads in-session can always be restored from state)
// ═══════════════════════════════════════════════════════════════

TEST_CASE("isValidUserIRFormat accepts typical IR formats", "[cabinet][ir][state]")
{
    REQUIRE(isValidUserIRFormat(1, 512, 44100.0));
    REQUIRE(isValidUserIRFormat(2, MAX_USER_IR_SAMPLES, 96000.0));
    REQUIRE(isValidUserIRFormat(1, 1, MIN_USER_IR_SAMPLE_RATE));   // lower boundary
    REQUIRE(isValidUserIRFormat(2, 2048, MAX_USER_IR_SAMPLE_RATE)); // upper boundary
}

TEST_CASE("isValidUserIRFormat rejects out-of-range formats", "[cabinet][ir][state]")
{
    // Channels
    REQUIRE_FALSE(isValidUserIRFormat(0, 512, 48000.0));
    REQUIRE_FALSE(isValidUserIRFormat(3, 512, 48000.0));
    // Samples
    REQUIRE_FALSE(isValidUserIRFormat(1, 0, 48000.0));
    REQUIRE_FALSE(isValidUserIRFormat(1, MAX_USER_IR_SAMPLES + 1, 48000.0));
    // Sample rate: out of band, zero, negative, non-finite
    REQUIRE_FALSE(isValidUserIRFormat(1, 512, MIN_USER_IR_SAMPLE_RATE - 1.0));
    REQUIRE_FALSE(isValidUserIRFormat(1, 512, MAX_USER_IR_SAMPLE_RATE + 1.0));
    REQUIRE_FALSE(isValidUserIRFormat(1, 512, 0.0));
    REQUIRE_FALSE(isValidUserIRFormat(1, 512, -48000.0));
    REQUIRE_FALSE(isValidUserIRFormat(1, 512, std::numeric_limits<double>::infinity()));
    REQUIRE_FALSE(isValidUserIRFormat(1, 512, std::numeric_limits<double>::quiet_NaN()));
}

// ═══════════════════════════════════════════════════════════════
// Samples JUCE can't normalise (review S4) — NaN/Inf, or energy that
// overflows its float sum of squares — rejected on both paths. A channel
// with nothing at or above JUCE's -80 dBFS trim is rejected on LOAD only:
// restore keeps it (harmless silence) rather than drop it and later slots.
// ═══════════════════════════════════════════════════════════════

TEST_CASE("IR_TRIM_THRESHOLD is the very float JUCE's Convolution trims at", "[cabinet][ir][state]")
{
    // Computed at run time in this TU (volatile: no constant folding), so it
    // is JUCE's own decibelsToGain(-80.0f) against the constexpr literal in
    // CabinetSim.h, compared bit for bit.
    volatile float minus80 = -80.0f;
    const float juceThreshold = juce::Decibels::decibelsToGain(static_cast<float>(minus80));
    std::uint32_t a = 0, b = 0;
    std::memcpy(&a, &juceThreshold, sizeof a);
    std::memcpy(&b, &IR_TRIM_THRESHOLD, sizeof b);
    REQUIRE(a == b);
}

TEST_CASE("Restored entry with a NaN/Inf or overflowing sample is rejected and stops parsing",
          "[cabinet][ir][state]")
{
    for (float bad : { std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity(),
                       1.0e20f })   // finite, but its square overflows a float
    {
        std::vector<UserIREntry> in;
        in.push_back(makeEntry("ok", 1, 256));
        in.push_back(makeEntry("poisoned", 2, 512));
        in.push_back(makeEntry("after_poisoned", 1, 256));
        in[1].audioData.setSample(1, 300, bad);  // inside the body, channel 2

        auto out = userIRsFromValueTree(userIRsToValueTree(in));

        // Stops at the bad entry, like any other invalid one, so no later
        // entry shifts onto its 1-based IR_INDEX.
        REQUIRE(out.size() == 1);
        REQUIRE(out[0].name == "ok");
    }
}

TEST_CASE("Restored entry whose channel energy overflows is rejected and stops parsing",
          "[cabinet][ir][state]")
{
    // Every sample is finite and so is every square (1e36), but the float sum
    // of squares JUCE's Convolution normalises by overflows to Inf, which
    // silences IR mode exactly as a NaN would.
    std::vector<UserIREntry> in;
    in.push_back(makeEntry("ok", 1, 256));
    in.push_back(makeEntry("too_hot", 2, 4096));
    in.push_back(makeEntry("after_too_hot", 1, 256));
    for (int s = 0; s < 4096; ++s)
        in[1].audioData.setSample(1, s, (s % 2) ? 1.0e18f : -1.0e18f);

    auto out = userIRsFromValueTree(userIRsToValueTree(in));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].name == "ok");
}

TEST_CASE("Restored silent or dead-channel entry is kept in its slot", "[cabinet][ir][state]")
{
    // A load refuses these (isAudibleOnEveryChannel), but a project saved
    // before that check can hold one. It is harmless — JUCE trims it to
    // silence, the same silence it played when saved — so restore keeps it:
    // dropping it would stop parsing and lose every later slot, and the next
    // save would delete them for good.
    for (float level : { 0.0f, 0.5f * IR_TRIM_THRESHOLD })
    {
        std::vector<UserIREntry> in;
        in.push_back(makeEntry("ok", 1, 256));
        in.push_back(makeEntry("silent", 2, 512));
        in.push_back(makeEntry("after_silent", 1, 256));
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                in[1].audioData.setSample(ch, s, (s % 2) ? level : -level);
        REQUIRE_FALSE(isAudibleOnEveryChannel(in[1].audioData));

        auto out = userIRsFromValueTree(userIRsToValueTree(in));
        REQUIRE(out.size() == 3);
        REQUIRE(out[1].name == "silent");
        REQUIRE(out[1].audioData.getSample(1, 511) == in[1].audioData.getSample(1, 511));
        REQUIRE(out[2].name == "after_silent");   // still on IR_INDEX 3
    }

    // Quiet but above the threshold in one channel only, the other dead:
    // refused at load (on a mono bus only channel 0 plays, so this one would be
    // pure silence there), kept at restore.
    std::vector<UserIREntry> in;
    in.push_back(makeEntry("one_dead_channel", 2, 512));
    in.push_back(makeEntry("after", 1, 256));
    in[0].audioData.clear();
    in[0].audioData.setSample(1, 100, 2.0f * IR_TRIM_THRESHOLD);
    REQUIRE_FALSE(isAudibleOnEveryChannel(in[0].audioData));
    auto out = userIRsFromValueTree(userIRsToValueTree(in));
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].name == "one_dead_channel");
    REQUIRE(out[1].name == "after");
}

namespace {

juce::File writeFloatWav(const juce::String& fileName, const juce::AudioBuffer<float>& audio,
                         double sampleRate)
{
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(fileName);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(f), sampleRate,
                            static_cast<unsigned int>(audio.getNumChannels()), 32, {}, 0));
    REQUIRE(writer != nullptr);
    writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
    return f;
}

} // namespace

TEST_CASE("readUserIRFromFile loads a clean float WAV, rejects unsafe, silent or dead-channel ones",
          "[cabinet][ir][state]")
{
    auto clean = makeEntry("src", 2, 300, 44100.0);

    SECTION("clean IR loads with its format")
    {
        auto f = writeFloatWav("sr_user_ir_clean.wav", clean.audioData, 44100.0);
        UserIREntry e;
        REQUIRE(readUserIRFromFile(f, e));
        REQUIRE(e.name == "sr_user_ir_clean");
        REQUIRE(e.numChannels == 2);
        REQUIRE(e.numSamples == 300);
        REQUIRE(e.sampleRate == 44100.0);
        REQUIRE(e.audioData.getSample(1, 299) == clean.audioData.getSample(1, 299));
        f.deleteFile();
    }

    SECTION("NaN, Inf or an overflowing sample inside the body is rejected")
    {
        for (float bad : { std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::infinity(),
                           1.0e20f })
        {
            auto poisoned = clean.audioData;
            poisoned.setSample(0, 150, bad);
            auto f = writeFloatWav("sr_user_ir_bad.wav", poisoned, 44100.0);
            UserIREntry e;
            REQUIRE_FALSE(readUserIRFromFile(f, e));
            f.deleteFile();
        }
    }

    SECTION("finite samples whose channel energy overflows are rejected")
    {
        auto tooHot = clean.audioData;
        for (int s = 0; s < tooHot.getNumSamples(); ++s)
            tooHot.setSample(0, s, 1.0e18f);   // 300 × 1e36 > FLT_MAX
        auto f = writeFloatWav("sr_user_ir_hot.wav", tooHot, 44100.0);
        UserIREntry e;
        REQUIRE_FALSE(readUserIRFromFile(f, e));
        f.deleteFile();
    }

    SECTION("a silent file (nothing at or above JUCE's -80 dBFS trim) is rejected")
    {
        for (float level : { 0.0f, 0.5f * IR_TRIM_THRESHOLD })
        {
            juce::AudioBuffer<float> silent(2, 300);
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < 300; ++s)
                    silent.setSample(ch, s, (s % 2) ? level : -level);
            auto f = writeFloatWav("sr_user_ir_silent.wav", silent, 44100.0);
            UserIREntry e;
            REQUIRE_FALSE(readUserIRFromFile(f, e));
            f.deleteFile();
        }
    }

    SECTION("a stereo file with one dead channel is rejected")
    {
        // JUCE's trim keeps it (the trim is a union over channels), but each
        // IR channel plays on its own output channel, and on a mono bus only
        // channel 0 is convolved — so either dead channel silences a side,
        // and a dead channel 0 silences a mono session outright.
        for (int dead : { 0, 1 })
        {
            auto oneDead = clean.audioData;
            oneDead.clear(dead, 0, oneDead.getNumSamples());
            auto f = writeFloatWav("sr_user_ir_dead_channel.wav", oneDead, 44100.0);
            UserIREntry e;
            REQUIRE_FALSE(readUserIRFromFile(f, e));
            f.deleteFile();
        }
    }

    SECTION("unreadable file is rejected")
    {
        UserIREntry e;
        REQUIRE_FALSE(readUserIRFromFile(juce::File("/tmp/sr_no_such_ir_98765.wav"), e));
    }
}

// ═══════════════════════════════════════════════════════════════
// IR_INDEX → convolution (review S2)
// ═══════════════════════════════════════════════════════════════

TEST_CASE("applyUserIRIndex disables the convolution for an index past the list",
          "[cabinet][ir][state]")
{
    CabinetSim cab;
    cab.prepare(48000.0, 512, 2);

    std::vector<UserIREntry> five;
    for (int i = 0; i < 5; ++i)
        five.push_back(makeEntry("ir_" + juce::String(i), 1, 256));

    REQUIRE(applyUserIRIndex(cab, five, 5, IRLoad::Async));
    REQUIRE(cab.useIR.load());
    REQUIRE(cab.getIRTailSeconds() > 0.0);

    // The review's repro: IR 5 live, then a state with only three IRs but the
    // same IR_INDEX = 5 is restored. The old IR 5 must not keep playing.
    std::vector<UserIREntry> three(five.begin(), five.begin() + 3);
    REQUIRE_FALSE(applyUserIRIndex(cab, three, 5, IRLoad::Async));
    REQUIRE_FALSE(cab.useIR.load());
    REQUIRE(cab.getIRTailSeconds() == 0.0);

    // In range again → loads; 0 (IIR mode) and garbage → disabled
    REQUIRE(applyUserIRIndex(cab, three, 2, IRLoad::Async));
    REQUIRE(cab.useIR.load());
    REQUIRE_FALSE(applyUserIRIndex(cab, three, 0, IRLoad::Async));
    REQUIRE_FALSE(cab.useIR.load());
    REQUIRE(applyUserIRIndex(cab, three, 3, IRLoad::Async));
    REQUIRE_FALSE(applyUserIRIndex(cab, three, -1, IRLoad::Async));
    REQUIRE_FALSE(cab.useIR.load());
    REQUIRE(applyUserIRIndex(cab, three, 1, IRLoad::Async));
    REQUIRE_FALSE(applyUserIRIndex(cab, {}, 1, IRLoad::Async));  // empty list
    REQUIRE_FALSE(cab.useIR.load());

    // The same through IRLoad::Now, the branch prepareToPlay takes
    REQUIRE(applyUserIRIndex(cab, five, 5, IRLoad::Now));
    REQUIRE(cab.useIR.load());
    REQUIRE(cab.getIRTailSeconds() > 0.0);
    REQUIRE_FALSE(applyUserIRIndex(cab, three, 5, IRLoad::Now));
    REQUIRE_FALSE(cab.useIR.load());
    REQUIRE(cab.getIRTailSeconds() == 0.0);
    REQUIRE(applyUserIRIndex(cab, three, 2, IRLoad::Now));
    REQUIRE(cab.useIR.load());
    REQUIRE_FALSE(applyUserIRIndex(cab, three, 0, IRLoad::Now));
    REQUIRE_FALSE(cab.useIR.load());
}
