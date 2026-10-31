// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// Processor wiring of IR_INDEX on state restore (review S2) — the real
// SilentRiotProcessor, driven like a host that loads a project or preset
// into a live instance.
//
// applyUserIRIndex() is unit-tested in user_ir_state_tests.cpp; what only the
// processor can show is that the restore path actually calls it. The
// processor's timer reacts to IR_INDEX only when it differs from the last
// index applied (RT-3; processBlock no longer looks), so a restore that leaves
// the index unchanged but shrinks the IR list is caught by nothing else — the
// previous IR would keep playing in IIR mode's place.
//
// Also: the active IR is installed synchronously by prepareToPlay (IR-path
// plan Task 1), so a session restored before prepare plays its cab from the
// first block instead of JUCE's pass-through.
// ═══════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "processor_harness.h"
#include "dsp/cabinet/CabModeSwitcher.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
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

// ═══════════════════════════════════════════════════════════════
// Cab-mode switches fade, and IR mode never plays un-cabbed (IR-path plan
// Task 3). P-EDGE, a sustained 220 Hz note; every switch is made mid-note.
// ═══════════════════════════════════════════════════════════════

namespace {

const juce::File& neutralFixture()
{
    static const juce::File f = juce::File(SOURCE_DIR).getChildFile("tests/fixtures/ir/neutral.wav");
    return f;
}

// A host restoring this session with IR_INDEX changed — the one host path,
// besides automation (which needs the message loop), that moves IR_INDEX.
void restoreWithIRIndex(ProcessorHarness& h, int index)
{
    juce::MemoryBlock saved;
    h.proc.getStateInformation(saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(xml != nullptr);
    auto state = juce::ValueTree::fromXml(*xml);
    bool found = false;
    for (auto child : state)
        if (child.hasType("PARAM") && child.getProperty("id").toString() == IDs::IR_INDEX)
        {
            child.setProperty("value", index, nullptr);
            found = true;
        }
    REQUIRE(found);
    juce::MemoryBlock edited;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), edited);
    h.proc.setStateInformation(edited.getData(), static_cast<int>(edited.getSize()));
    REQUIRE(irIndex(h) == index);
}

float maxStep(const std::vector<float>& x, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t>(from, 1); i < to; ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

// Plays the P-EDGE note through a mode switch. `out` holds 0.21 s before the
// switch, then 0.41 s after it (whole 512-sample blocks, phase continuous).
struct SwitchRender
{
    std::vector<float> out;
    size_t switchAt = 0;   // first sample rendered after doSwitch()
};

constexpr int kBlocksBefore = 18, kBlocksAfter = 35;

// The steady state's own largest step drifts by ~1e-4 relative between the
// 0.2 s before a switch and the last 0.15 s (sag is still settling); a hard
// cut steps 2–30x past it.
constexpr float kSteadyTolerance = 1.01f;

SwitchRender renderSwitch(ProcessorHarness& h, const std::function<void()>& doSwitch)
{
    h.set(IDs::GAIN, 0.625f);   // P-EDGE
    h.set(IDs::VOLUME, 0.5f);
    h.set(IDs::TONE, 0.5f);
    const auto note = sine(220.0f, 0.1f);
    int t = 0;
    auto from = [&](int offset) { return [=](int i, int ch) { return note(i + offset, ch); }; };
    const double block = 512.0 / 44100.0;

    h.run(52 * block, from(t));   // settle on the note (0.6 s)
    t += 52 * 512;
    SwitchRender r;
    h.run(kBlocksBefore * block, from(t), ProcessorHarness::Mode::Process, &r.out);
    t += kBlocksBefore * 512;
    r.switchAt = r.out.size();
    doSwitch();
    h.run(kBlocksAfter * block, from(t), ProcessorHarness::Mode::Process, &r.out);
    return r;
}

// Largest sample-to-sample step after the switch, against the steady state
// on either side of it (before the switch, and the last 0.15 s).
struct Steps { float before, after, post; long where; };

// End of the first run of >= 64 exact zeros at or after `from` (the dip's
// silent stretch), or `from` when there is none.
size_t dipEnd(const std::vector<float>& x, size_t from)
{
    for (size_t i = from, run = 0; i < x.size(); ++i)
    {
        run = x[i] == 0.0f ? run + 1 : 0;
        if (run >= 64)
        {
            while (i < x.size() && x[i] == 0.0f) ++i;
            return i;
        }
    }
    return from;
}

// Steps are checked from the switch to 20 ms past the dip's end (past the
// switch itself, when there is no dip); the steady state is the 0.2 s before
// the switch and the last 0.15 s.
Steps stepsOf(const SwitchRender& r)
{
    const size_t tail = r.out.size() - static_cast<size_t>(0.15 * 44100.0);
    const size_t end = std::min(tail, dipEnd(r.out, r.switchAt) + static_cast<size_t>(0.02 * 44100.0));
    size_t at = r.switchAt;
    float post = 0.0f;
    for (size_t i = r.switchAt; i < end; ++i)
        if (std::abs(r.out[i] - r.out[i - 1]) > post) { post = std::abs(r.out[i] - r.out[i - 1]); at = i; }
    return { maxStep(r.out, 0, r.switchAt), maxStep(r.out, tail, r.out.size()), post,
             static_cast<long>(at) - static_cast<long>(dipEnd(r.out, r.switchAt)) };
}

} // namespace

TEST_CASE("Processor: the first IR of a session fades in and never plays un-cabbed",
          "[processor][ir][switch]")
{
    // built-in -> IR, async: the convolution has only ever held JUCE's
    // pass-through, which plays the amp with no cab until the real engine is
    // built. Before Task 3 IR mode took over at once, on the pass-through.
    // The 3 kHz low-pass IR: >7 kHz cut far below the built-in cab's, so a
    // pass-through stretch stands out by tens of dB.
    juce::TemporaryFile irFile(".wav");
    const juce::File wav = writeWav(irFile, lowPassIR(), 44100.0);
    ProcessorHarness h;
    const SwitchRender r = renderSwitch(h, [&] { REQUIRE(h.proc.loadUserIR(wav)); });

    // The fade dips to exact zeros (the rest of the fade-out's block, then
    // the hold); IR mode starts at the end of them. With no dip (a hard
    // switch) IR mode started at the load itself.
    const size_t irFrom = dipEnd(r.out, r.switchAt);
    size_t zerosFrom = irFrom;
    while (zerosFrom > r.switchAt && r.out[zerosFrom - 1] == 0.0f) --zerosFrom;
    const size_t fadeOutFrom = irFrom > r.switchAt ? zerosFrom - static_cast<size_t>(sr::CabModeSwitcher::kFadeSamples) : r.switchAt;
    // The IR windows start 2 ms past the end of the fade-in: a linear ramp's
    // end is a corner in the envelope, broadband on its own (-99 dB here, the
    // fade's, not the cab's), and the analysis high-pass rings on it ~1 ms.
    const size_t cornerClear = 88;
    const size_t fullFrom = (irFrom > r.switchAt ? irFrom + static_cast<size_t>(sr::CabModeSwitcher::kFadeSamples) : r.switchAt)
                            + cornerClear;

    // >7 kHz energy, 10 ms windows: while waiting (before the fade-out) it
    // must be the built-in cab's; in the 20 ms from the end of the fade-in,
    // the IR's. A pass-through stretch reads at CAB-off level, 40 dB above.
    const size_t win = 441;
    const double builtInHf = energyAbove7kDb(r.out, r.switchAt - win, win);
    const double settledIrHf = energyAbove7kDb(r.out, r.out.size() - win, win);
    double worstWaiting = -300.0, worstIr = -300.0;
    for (size_t w = r.switchAt; w + win <= fadeOutFrom; w += win)
        worstWaiting = std::max(worstWaiting, energyAbove7kDb(r.out, w, win));
    for (size_t w = fullFrom; w < fullFrom + 2 * win; w += win)
        worstIr = std::max(worstIr, energyAbove7kDb(r.out, w, win));

    INFO(">7 kHz, dB: built-in " << builtInHf << ", settled IR " << settledIrHf
         << ", waiting (worst 10 ms) " << worstWaiting << ", 20 ms from full IR gain " << worstIr
         << "; IR mode from " << (irFrom - r.switchAt) / 44.1 << " ms after the load");
    REQUIRE(settledIrHf < builtInHf - 20.0);    // the IR does cut >7 kHz
    REQUIRE(irFrom > r.switchAt);               // it faded (dipped), it did not cut
    REQUIRE(worstWaiting < builtInHf + 3.0);    // waiting: the built-in cab, not pass-through
    REQUIRE(worstIr < settledIrHf + 3.0);       // IR mode is the IR from its first sample

    const Steps s = stepsOf(r);
    INFO("steps: before " << s.before << ", after " << s.after << ", across the switch " << s.post << " at " << s.where << " from the dip's end");
    REQUIRE(s.post <= kSteadyTolerance * std::max(s.before, s.after));
}

TEST_CASE("Processor: IR -> built-in cab fades", "[processor][ir][switch]")
{
    ProcessorHarness h;
    h.loadIR(neutralFixture());
    const SwitchRender r = renderSwitch(h, [&] { restoreWithIRIndex(h, 0); });
    const Steps s = stepsOf(r);
    INFO("steps: before " << s.before << ", after " << s.after << ", across the switch " << s.post << " at " << s.where << " from the dip's end");
    REQUIRE(s.post <= kSteadyTolerance * std::max(s.before, s.after));
}

TEST_CASE("Processor: switching CAB on or off fades", "[processor][ir][switch]")
{
    const bool withIR = GENERATE(false, true);
    const bool turnOn = GENERATE(false, true);
    ProcessorHarness h;
    if (withIR) h.loadIR(neutralFixture());
    h.set(IDs::CAB_SIM, turnOn ? 0.0f : 1.0f);
    const SwitchRender r = renderSwitch(h, [&] { h.set(IDs::CAB_SIM, turnOn ? 1.0f : 0.0f); });
    const Steps s = stepsOf(r);
    INFO((withIR ? "IR" : "built-in") << (turnOn ? ", CAB off -> on" : ", CAB on -> off")
         << ". steps: before " << s.before << ", after " << s.after << ", across the switch " << s.post << " at " << s.where << " from the dip's end");
    REQUIRE(s.post <= kSteadyTolerance * std::max(s.before, s.after));
}

TEST_CASE("Processor: a cab mode set after prepare, before the first block, does not fade",
          "[processor][ir][switch]")
{
    // Hosts set their knobs after prepareToPlay. With no output yet there is
    // nothing to fade from, so the first block plays the requested mode, as
    // if it had been set before prepare (sr_render relies on this too).
    ProcessorHarness early, late;
    early.set(IDs::CAB_SIM, 0.0f);
    early.proc.prepareToPlay(early.sampleRate, early.blockSize);
    late.set(IDs::CAB_SIM, 0.0f);
    std::vector<float> a, b;
    early.run(0.1, sine(220.0f, 0.25f), ProcessorHarness::Mode::Process, &a);
    late.run(0.1, sine(220.0f, 0.25f), ProcessorHarness::Mode::Process, &b);
    REQUIRE(a == b);
}

// The IR loader's CAB view steps to the built-in cab as well as to each IR.
// selectBuiltInCab() applies IR_INDEX 0 itself, as selectUserIR() does for an
// IR, so the convolution is off before the next block, not a timer tick later.
TEST_CASE("Processor: selectBuiltInCab turns the IR off, selectUserIR brings it back",
          "[processor][ir]")
{
    juce::TemporaryFile irFile(".wav");
    ProcessorHarness h;
    h.loadIR(writeWav(irFile, longDecayIR(777u), 8000.0));
    REQUIRE(irIndex(h) == 1);
    REQUIRE(tailAfterNote(h) > dB(-100.0));

    h.proc.selectBuiltInCab();
    REQUIRE(irIndex(h) == 0);
    REQUIRE(h.proc.getTailLengthSeconds() <= 0.0);
    h.silence(0.5);   // past the cab-mode fade
    INFO("the IR kept ringing after the built-in cab was selected");
    REQUIRE(tailAfterNote(h) < dB(-100.0));

    h.proc.selectUserIR(0);
    REQUIRE(irIndex(h) == 1);
    for (int i = 0; i < 60; ++i)   // JUCE builds the engine on its own thread
    {
        h.silence(512.0 / 44100.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    h.silence(0.5);
    REQUIRE(tailAfterNote(h) > dB(-100.0));
}

// IR_INDEX automation (RT-3). processBlock used to notice the change and post
// it to the message queue with triggerAsyncUpdate() — a lock, a write()
// syscall and a possible allocation on the audio thread. Now the processor's
// message-thread timer polls the parameter, so this needs a pumped message
// loop, as in a host: the audio runs on a thread of its own and sets the
// parameter the way host automation does.
TEST_CASE("Processor: automating IR_INDEX swaps the cab via the message-thread timer (RT-3, review S2)",
          "[processor][ir]")
{
    juce::TemporaryFile irFile1(".wav"), irFile2(".wav");
    ProcessorHarness h;
    h.loadIR(writeWav(irFile1, longDecayIR(4242u), 8000.0));
    h.loadIR(writeWav(irFile2, longDecayIR(2424u), 8000.0));   // IR_INDEX = 2, playing
    REQUIRE(irIndex(h) == 2);
    REQUIRE(tailAfterNote(h) > dB(-100.0));

    // Blocks at roughly real-time pace, so the timer ticks several times and
    // JUCE's background thread can build an engine; then past the cab fade.
    auto settle = [&h] {
        for (int i = 0; i < 60; ++i)
        {
            h.silence(512.0 / 44100.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        h.silence(0.5);
    };

    // getTailLengthSeconds() reads the cab state the timer writes, so read it
    // on the message thread, between ticks, not on this audio thread.
    auto tailLength = [&h] {
        std::atomic<bool> done { false };
        double v = std::numeric_limits<double>::quiet_NaN();   // NaN fails every check below
        if (! juce::MessageManager::callAsync([&] { v = h.proc.getTailLengthSeconds(); done = true; }))
            return v;
        while (! done.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return v;
    };

    float tailBuiltIn = 0.0f, tailIR1 = 0.0f, tailPast = 0.0f;
    const double kUnread = std::numeric_limits<double>::quiet_NaN();
    double lengthBuiltIn = kUnread, lengthIR1 = kUnread, lengthPast = kUnread;
    h.withMessageLoop([&] {
        h.setFromAudioThread(IDs::IR_INDEX, 0.0f);   // the built-in cab
        settle();
        lengthBuiltIn = tailLength();
        tailBuiltIn = tailAfterNote(h);

        h.setFromAudioThread(IDs::IR_INDEX, 1.0f);   // the first IR
        settle();
        lengthIR1 = tailLength();
        tailIR1 = tailAfterNote(h);

        h.setFromAudioThread(IDs::IR_INDEX, 5.0f);   // past the list: convolution off
        settle();
        lengthPast = tailLength();
        tailPast = tailAfterNote(h);
    });

    INFO("IR_INDEX 0: the IR kept ringing");
    CHECK(lengthBuiltIn <= 0.0);
    CHECK(tailBuiltIn < dB(-100.0));
    INFO("IR_INDEX 1: the IR did not come back");
    CHECK(lengthIR1 > 0.5);
    CHECK(tailIR1 > dB(-100.0));
    INFO("IR_INDEX past the list: a stale IR kept playing (review S2)");
    CHECK(lengthPast <= 0.0);
    CHECK(tailPast < dB(-100.0));
}
