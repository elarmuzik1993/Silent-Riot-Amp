// ═══════════════════════════════════════════════════════════════
// Processor wiring of IR_INDEX on state restore (review S2) — the real
// SilentRiotProcessor, driven like a host that loads a project or preset
// into a live instance.
//
// applyUserIRIndex() is unit-tested in user_ir_state_tests.cpp; what only the
// processor can show is that the restore path actually calls it. processBlock
// reacts to IR_INDEX only when it differs from the last index it saw, so a
// restore that leaves the index unchanged but shrinks the IR list is caught
// by nothing else — the previous IR would keep playing in IIR mode's place.
//
// Also: the active IR is installed synchronously by prepareToPlay (IR-path
// plan Task 1), so a session restored before prepare plays its cab from the
// first block instead of JUCE's pass-through.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include "processor_harness.h"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace sr::test;
namespace IDs = sr::Params::IDs;

namespace {

// A mono IR with a ~1 s tail (8192 samples at 8 kHz), so its ring outlasts the
// amp's own ~0.5 s settling and is unmistakable on silence.
std::vector<float> longDecayIR(uint32_t seed)
{
    constexpr int n = 8192;
    constexpr double irRate = 8000.0;
    std::vector<float> ir(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const float noise = static_cast<float>(seed >> 8) / static_cast<float>(1u << 24) - 0.5f;
        ir[static_cast<size_t>(i)] =
            noise * static_cast<float>(std::exp(-static_cast<double>(i) / (0.25 * irRate)));
    }
    ir[0] = 1.0f;
    return ir;
}

int irIndex(ProcessorHarness& h)
{
    return static_cast<int>(h.proc.apvts.getRawParameterValue(IDs::IR_INDEX)->load());
}

// Peak on silence once a note has ended and the amp alone has settled: an IR
// tail rings well above -100 dBFS here, the IIR path is below -120 dBFS.
float tailAfterNote(ProcessorHarness& h)
{
    h.run(1.0, sine(220.0f, 0.25f));
    h.silence(0.6);
    return h.silence(0.1);
}

// Mean-square energy above 7 kHz (8th-order Butterworth high-pass at 44.1 kHz)
// of x[from, from + n), in dB. The high-pass runs over all of x from its
// start, so a window boundary adds no step of its own.
double energyAbove7kDb(const std::vector<float>& x, size_t from, size_t n)
{
    auto sections = juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(
        7000.0f, 44100.0, 8);
    std::vector<juce::dsp::IIR::Filter<float>> hp;
    for (auto& c : sections) hp.emplace_back(c);
    double sum = 0.0;
    for (size_t i = 0; i < from + n; ++i)
    {
        float v = x[i];
        for (auto& f : hp) v = f.processSample(v);
        if (i >= from) sum += static_cast<double>(v) * v;
    }
    return 10.0 * std::log10(std::max(sum / static_cast<double>(n), 1e-30));
}

// A cab-like IR for the >7 kHz check: Hann-windowed sinc low-pass at 3 kHz,
// 127 taps at 44.1 kHz — at least ~40 dB down everywhere above 5 kHz.
std::vector<float> lowPassIR()
{
    constexpr int taps = 127;
    constexpr double fc = 3000.0 / 44100.0;
    std::vector<float> ir(static_cast<size_t>(taps));
    for (int i = 0; i < taps; ++i)
    {
        const double m = i - (taps - 1) / 2.0;
        const double sinc = m == 0.0 ? 2.0 * fc
                                     : std::sin(2.0 * juce::MathConstants<double>::pi * fc * m)
                                           / (juce::MathConstants<double>::pi * m);
        const double hann = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * i / (taps - 1));
        ir[static_cast<size_t>(i)] = static_cast<float>(sinc * hann);
    }
    return ir;
}

// 3 cycles per 512-sample block: the first and the last block of a render see
// the note at the same phase.
Fill blockPeriodicNote() { return sine(3.0f * 44100.0f / 512.0f, 0.25f); }

} // namespace

TEST_CASE("Processor: restoring a state whose IR_INDEX is past its IR list disables the IR (review S2)",
          "[processor][ir][state]")
{
    juce::TemporaryFile irFile1(".wav"), irFile2(".wav");
    ProcessorHarness h;
    h.loadIR(writeWav(irFile1, longDecayIR(12345u), 8000.0));
    h.loadIR(writeWav(irFile2, longDecayIR(54321u), 8000.0));   // IR_INDEX = 2, playing
    REQUIRE(irIndex(h) == 2);
    REQUIRE(h.proc.getTailLengthSeconds() > 0.5);
    REQUIRE(tailAfterNote(h) > dB(-100.0));

    // The review's repro: a saved state that still says IR_INDEX = 2 but holds
    // only one IR (here: this session's state with its second IR removed),
    // restored into the live instance. The index the processor last saw is
    // unchanged, so only the restore path itself can drop IR 2.
    juce::MemoryBlock saved;
    h.proc.getStateInformation(saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(),
                                                      static_cast<int>(saved.getSize()));
    REQUIRE(xml != nullptr);
    auto state = juce::ValueTree::fromXml(*xml);
    auto irs = state.getChildWithName("UserIRs");
    REQUIRE(irs.getNumChildren() == 2);
    irs.removeChild(1, nullptr);

    juce::MemoryBlock shrunk;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), shrunk);
    h.proc.setStateInformation(shrunk.getData(), static_cast<int>(shrunk.getSize()));

    REQUIRE(irIndex(h) == 2);
    REQUIRE(h.proc.getNumUserIRs() == 1);
    INFO("IR 2 kept playing after a restore that no longer holds it");
    REQUIRE(h.proc.getTailLengthSeconds() <= 0.0);   // no IR tail reported
    REQUIRE(tailAfterNote(h) < dB(-120.0));          // and none heard: IIR path
}

TEST_CASE("Processor: a session restored before prepare plays its IR from the first block",
          "[processor][ir][state]")
{
    // A host loads a project, then prepares. Before Task 1 the restore queued
    // the IR for JUCE's background thread and prepareToPlay did the same, so
    // the first blocks ran JUCE's pass-through engine: IR mode with no cab,
    // its >7 kHz energy level with CAB off.
    juce::TemporaryFile irFile(".wav");
    const juce::File wav = writeWav(irFile, lowPassIR(), 44100.0);

    juce::MemoryBlock saved;
    {
        ProcessorHarness src;
        REQUIRE(src.proc.loadUserIR(wav));
        src.proc.getStateInformation(saved);
    }

    // The amp's own >7 kHz content changes over the note's onset, so a first
    // block is compared through the IR-to-CAB-off ratio: in the first block it
    // must be no worse than once both have settled (the last of 44 blocks,
    // 0.5 s on), i.e. the cab is the IR from the start, not a pass-through
    // that hands over later. (Measured: first -57.8 dB, settled -53.1 dB;
    // before Task 1 the first block read 0.0 dB.) One-sided, because the
    // onset's spectrum differs enough to move the ratio a few dB either way.
    struct Hf { double first, settled; };
    const double blockSec = 512.0 / 44100.0;
    auto render = [&](bool cabOn) {
        ProcessorHarness h;
        h.proc.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        h.set(IDs::GAIN, 0.8f);
        h.set(IDs::CAB_SIM, cabOn ? 1.0f : 0.0f);
        h.proc.prepareToPlay(h.sampleRate, h.blockSize);
        std::vector<float> out;
        h.run(44.0 * blockSec, blockPeriodicNote(), ProcessorHarness::Mode::Process, &out);
        const size_t block = static_cast<size_t>(h.blockSize);
        return Hf { energyAbove7kDb(out, 0, block), energyAbove7kDb(out, out.size() - block, block) };
    };

    const Hf ir  = render(true);
    const Hf off = render(false);
    const double firstRatio   = ir.first - off.first;
    const double settledRatio = ir.settled - off.settled;

    INFO("IR minus CAB off, >7 kHz: first block " << firstRatio << " dB, settled "
         << settledRatio << " dB");
    REQUIRE(settledRatio < -20.0);   // the IR does cut >7 kHz once it plays
    REQUIRE(firstRatio < settledRatio + 3.0);
}

TEST_CASE("Processor: IR mode plays a 220 Hz note at the built-in cab's level",
          "[processor][ir][level]")
{
    // IR-path plan Task 2. JUCE's Normalise::yes left the neutral fixture
    // 10.9 dB under the built-in cab at P-EDGE (sr_irpath.py, Task 0). Now the
    // IR is scaled to the built-in cab's 100 Hz–5 kHz band level.
    const juce::File neutral =
        juce::File(SOURCE_DIR).getChildFile("tests/fixtures/ir/neutral.wav");
    REQUIRE(neutral.existsAsFile());

    auto noteRmsDb = [&](bool withIR) {
        ProcessorHarness h;
        if (withIR)
            h.loadIR(neutral);
        h.set(IDs::GAIN, 0.625f);   // P-EDGE
        h.set(IDs::VOLUME, 0.5f);
        h.set(IDs::TONE, 0.5f);    // SHAPE
        h.silence(0.3);
        std::vector<float> out;
        h.run(1.0, sine(220.0f, 0.1f), ProcessorHarness::Mode::Process, &out);
        double sum = 0.0;
        const size_t from = out.size() / 2;
        for (size_t i = from; i < out.size(); ++i)
            sum += static_cast<double>(out[i]) * out[i];
        return 10.0 * std::log10(sum / static_cast<double>(out.size() - from));
    };

    const double builtIn = noteRmsDb(false);
    const double ir = noteRmsDb(true);
    INFO("built-in " << builtIn << " dBFS, IR " << ir << " dBFS, IR - built-in " << ir - builtIn);
    REQUIRE(std::abs(ir - builtIn) < 1.5);
}

TEST_CASE("Processor: saved state keeps the raw IR, not the level-matched copy",
          "[processor][ir][state][level]")
{
    // The level match scales only the copy handed to the convolution, so a
    // later retune of IRLevel.h never needs saved projects rewritten.
    const juce::File neutral =
        juce::File(SOURCE_DIR).getChildFile("tests/fixtures/ir/neutral.wav");
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(neutral));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> file(static_cast<int>(reader->numChannels),
                                  static_cast<int>(reader->lengthInSamples));
    reader->read(&file, 0, file.getNumSamples(), 0, true, true);

    ProcessorHarness h;
    REQUIRE(h.proc.loadUserIR(neutral));
    h.silence(0.1);
    juce::MemoryBlock saved;
    h.proc.getStateInformation(saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(),
                                                      static_cast<int>(saved.getSize()));
    REQUIRE(xml != nullptr);
    const auto irs = sr::userIRsFromValueTree(
        juce::ValueTree::fromXml(*xml).getChildWithName("UserIRs"));
    REQUIRE(irs.size() == 1);
    const auto& stored = irs[0].audioData;
    REQUIRE(stored.getNumChannels() == file.getNumChannels());
    REQUIRE(stored.getNumSamples() == file.getNumSamples());
    for (int ch = 0; ch < file.getNumChannels(); ++ch)
        REQUIRE(std::equal(file.getReadPointer(ch), file.getReadPointer(ch) + file.getNumSamples(),
                           stored.getReadPointer(ch)));
}
