// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
// ═══════════════════════════════════════════════════════════════
// rt_stress — real-time stress harness for the shipped processor.
//
// Drives the real SilentRiotProcessor from a dedicated audio thread while a
// live JUCE message thread runs beside it, the way a host does, and holds it
// to the three real-time contracts (tests/rt_probe.h): no allocation, no
// blocking lock, no block over its own deadline. Every scenario also checks
// what the host receives — finite samples inside ±1 — because a plugin that
// meets its deadline by emitting NaN has not met anything.
//
// Not a Catch2 test: it runs minutes, needs its own main() to install the
// global operator new / pthread interposers before anything else starts, and
// is meant to be run on demand rather than in ctest.
//
//   build-verify/tests/SilentRiotRTStress --help
//   build-verify/tests/SilentRiotRTStress --quick
//   build-verify/tests/SilentRiotRTStress --only=load,modes --md report.md
//
// Exit code 0 when every scenario passes, 1 when a CRITICAL finding is
// recorded, so it can gate a release.
// ═══════════════════════════════════════════════════════════════

#include "rt_probe.h"

#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
 #include <dlfcn.h>
 #include <pthread.h>
 #include <sched.h>
 #include <time.h>
#endif

// ═══════════════════════════════════════════════════════════════
// 1. Interposers. Exactly one TU may define these, and they have to live in
//    the executable to win symbol resolution against libc.
//
//    Under a sanitizer they are compiled out: ASan/TSan install their own
//    operator new and pthread interceptors, and replacing those would
//    silence the sanitizer, which is the more valuable instrument in that
//    build. A sanitizer run therefore reports races and memory errors; the
//    plain Release run reports allocations and locks.
// ═══════════════════════════════════════════════════════════════

#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
 #define SR_RT_INTERPOSE_NEW 0
#elif defined(__has_feature)
 #if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
  #define SR_RT_INTERPOSE_NEW 0
 #else
  #define SR_RT_INTERPOSE_NEW 1
 #endif
#else
 #define SR_RT_INTERPOSE_NEW 1
#endif

#if SR_RT_INTERPOSE_NEW

void* operator new(std::size_t n)
{
    if (rt::g_depth > 0)
    {
        rt::g_allocs.fetch_add(1, std::memory_order_relaxed);
        rt::g_allocBytes.fetch_add(static_cast<long long>(n), std::memory_order_relaxed);
        rt::note("operator new", n);
    }
    void* p = std::malloc(n != 0 ? n : 1);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t n) { return ::operator new(n); }

void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
    if (rt::g_depth > 0)
    {
        rt::g_allocs.fetch_add(1, std::memory_order_relaxed);
        rt::g_allocBytes.fetch_add(static_cast<long long>(n), std::memory_order_relaxed);
        rt::note("operator new (nothrow)", n);
    }
    return std::malloc(n != 0 ? n : 1);
}

void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept
{
    return ::operator new(n, t);
}

void* operator new(std::size_t n, std::align_val_t a)
{
    if (rt::g_depth > 0)
    {
        rt::g_allocs.fetch_add(1, std::memory_order_relaxed);
        rt::g_allocBytes.fetch_add(static_cast<long long>(n), std::memory_order_relaxed);
        rt::note("operator new (aligned)", n);
    }
    const std::size_t align = static_cast<std::size_t>(a);
    void* p = nullptr;
    if (posix_memalign(&p, align < sizeof(void*) ? sizeof(void*) : align, n != 0 ? n : 1) != 0)
        throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }

static inline void srNoteFree(void* p)
{
    if (p != nullptr && rt::g_depth > 0) rt::g_frees.fetch_add(1, std::memory_order_relaxed);
}

void operator delete(void* p) noexcept                        { srNoteFree(p); std::free(p); }
void operator delete[](void* p) noexcept                      { srNoteFree(p); std::free(p); }
void operator delete(void* p, std::size_t) noexcept           { srNoteFree(p); std::free(p); }
void operator delete[](void* p, std::size_t) noexcept         { srNoteFree(p); std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { srNoteFree(p); std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { srNoteFree(p); std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept      { srNoteFree(p); std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept    { srNoteFree(p); std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept   { srNoteFree(p); std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { srNoteFree(p); std::free(p); }

#endif // SR_RT_INTERPOSE_NEW

// ThreadSanitizer installs its own pthread interceptors to track
// happens-before; ours would displace them and turn every real race into a
// miss. Under TSan the lock counters simply stay at zero.
#if defined(__linux__) && SR_RT_INTERPOSE_NEW
 #define SR_RT_INTERPOSE_PTHREAD 1
#else
 #define SR_RT_INTERPOSE_PTHREAD 0
#endif

#if SR_RT_INTERPOSE_PTHREAD
namespace {
using SrLockFn = int (*)(pthread_mutex_t*);
SrLockFn srRealLock    = nullptr;
SrLockFn srRealTryLock = nullptr;

void srResolvePthread()
{
    if (srRealLock == nullptr)
        srRealLock = reinterpret_cast<SrLockFn>(dlsym(RTLD_NEXT, "pthread_mutex_lock"));
    if (srRealTryLock == nullptr)
        srRealTryLock = reinterpret_cast<SrLockFn>(dlsym(RTLD_NEXT, "pthread_mutex_trylock"));
}

// Runs before any other constructor that might take a lock, so the hot path
// below never has to resolve anything.
__attribute__((constructor(101))) void srRtInit() { srResolvePthread(); }
} // namespace

// A lock taken on the audio thread is a priority-inversion dropout waiting to
// happen, whether or not it was contended this time; both are counted, and
// the contended ones separately, because those are the ones that already cost
// wall time. Only calls from inside an rt::Scope are counted.
extern "C" int pthread_mutex_lock(pthread_mutex_t* m)
{
    if (srRealLock == nullptr)
    {
        srResolvePthread();
        if (srRealLock == nullptr) return 0;   // pre-loader; nothing to serialise yet
    }
    if (rt::g_depth > 0 && !rt::g_inNote)
    {
        rt::g_locks.fetch_add(1, std::memory_order_relaxed);
        if (srRealTryLock != nullptr && srRealTryLock(m) == 0)
        {
            rt::note("pthread_mutex_lock", 0);
            return 0;
        }
        rt::g_contended.fetch_add(1, std::memory_order_relaxed);
        rt::note("pthread_mutex_lock (CONTENDED)", 0);
    }
    return srRealLock(m);
}
#endif // SR_RT_INTERPOSE_PTHREAD

// ═══════════════════════════════════════════════════════════════
// 2. rt_probe.h out-of-line parts
// ═══════════════════════════════════════════════════════════════

namespace rt {

void warmUp()
{
#if SR_RT_HAVE_BACKTRACE
    void* frames[4];
    const int n = backtrace(frames, 4);
    if (char** s = backtrace_symbols(frames, n)) std::free(s);
#endif
#if SR_RT_INTERPOSE_PTHREAD
    srResolvePthread();
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&m);
    pthread_mutex_unlock(&m);
#endif
#if defined(__linux__)
    Dl_info info {};
    dladdr(reinterpret_cast<void*>(&warmUp), &info);
#endif
    volatile auto* p = new char[64];
    delete[] const_cast<char*>(p);
}

std::string formatTraces()
{
    std::ostringstream out;
    const int n = std::min(g_traceCount.load(), kMaxTraces);
    if (n == 0) return {};
    for (int t = 0; t < n; ++t)
    {
        const Trace& tr = g_traces[t];
        out << "  [" << t << "] " << (tr.kind != nullptr ? tr.kind : "?");
        if (tr.bytes != 0) out << " (" << tr.bytes << " bytes)";
        out << "\n";
#if defined(__linux__)
        for (int f = 2; f < tr.depth && f < 14; ++f)   // skip our own two frames
        {
            Dl_info info {};
            if (dladdr(tr.frames[f], &info) != 0 && info.dli_fname != nullptr)
            {
                const auto off = reinterpret_cast<const char*>(tr.frames[f])
                               - reinterpret_cast<const char*>(info.dli_fbase);
                out << "        " << (info.dli_sname != nullptr ? info.dli_sname : "??")
                    << "  +0x" << std::hex << off << std::dec
                    << "  " << info.dli_fname << "\n";
            }
            else
            {
                out << "        " << tr.frames[f] << "\n";
            }
        }
#endif
    }
    return out.str();
}

} // namespace rt

// ═══════════════════════════════════════════════════════════════
// 3. Harness
// ═══════════════════════════════════════════════════════════════

namespace {

using Clock = std::chrono::steady_clock;
namespace P = sr::Params::IDs;

double threadCpuSeconds()
{
#if defined(__linux__)
    timespec ts {};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) + 1.0e-9 * static_cast<double>(ts.tv_nsec);
#else
    return 0.0;
#endif
}

// ── Findings ────────────────────────────────────────────────────
struct Finding { std::string level, scenario, text; };
std::vector<Finding> g_findings;
bool g_anyCritical = false;

void report(const char* level, const std::string& scenario, const std::string& text)
{
    g_findings.push_back({ level, scenario, text });
    if (std::strcmp(level, "CRITICAL") == 0) g_anyCritical = true;
    std::cout << "    " << level << ": " << text << "\n";
}

// ── Signals ─────────────────────────────────────────────────────
enum class Sig { Silence, Sine, DI, Noise, Square, Impulse, Hot, Denormal, NaN_, Inf_, Huge, Nyquist };

const char* sigName(Sig s)
{
    switch (s)
    {
        case Sig::Silence:  return "silence";
        case Sig::Sine:     return "sine 110 Hz -18 dBFS";
        case Sig::DI:       return "DI-like chord";
        case Sig::Noise:    return "white noise 0 dBFS";
        case Sig::Square:   return "square 1.6 kHz full scale";
        case Sig::Impulse:  return "impulse train";
        case Sig::Hot:      return "sine +20 dBFS (10.0 peak)";
        case Sig::Denormal: return "denormals (1e-40)";
        case Sig::NaN_:     return "NaN";
        case Sig::Inf_:     return "+/-Inf";
        case Sig::Huge:     return "1e30";
        case Sig::Nyquist:  return "alternating +/-1";
    }
    return "?";
}

struct SignalGen
{
    Sig sig = Sig::Sine;
    double sr = 44100.0;
    std::mt19937 rng { 0x51107u };

    float operator()(long long i, int)
    {
        const auto t = static_cast<double>(i) / sr;
        switch (sig)
        {
            case Sig::Silence:  return 0.0f;
            case Sig::Sine:     return 0.125f * std::sin(6.283185307 * 110.0 * t);
            case Sig::DI:
            {
                double v = 0.0;
                const double f[6] = { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63 };
                for (double fr : f) v += std::sin(6.283185307 * fr * t);
                return static_cast<float>(0.08 * v * std::exp(-std::fmod(t, 2.0)));
            }
            case Sig::Noise:
                return std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
            case Sig::Square:
                return std::sin(6.283185307 * 1600.0 * t) >= 0.0 ? 1.0f : -1.0f;
            case Sig::Impulse:
                return (i % 1024) == 0 ? 1.0f : 0.0f;
            case Sig::Hot:      return 10.0f * std::sin(6.283185307 * 110.0 * t);
            case Sig::Denormal: return 1.0e-40f * ((i & 1) ? 1.0f : -1.0f);
            case Sig::NaN_:     return std::numeric_limits<float>::quiet_NaN();
            case Sig::Inf_:
                return (i & 1) ? std::numeric_limits<float>::infinity()
                               : -std::numeric_limits<float>::infinity();
            case Sig::Huge:     return (i & 1) ? 1.0e30f : -1.0e30f;
            case Sig::Nyquist:  return (i & 1) ? 1.0f : -1.0f;
        }
        return 0.0f;
    }
};

using Fill = std::function<float(long long, int)>;

// ── One measured run ────────────────────────────────────────────
struct Row
{
    std::string name, config;
    rt::Timing  t;
    rt::AudioCheck a;
    rt::Counts  c {};
    double cpuSeconds = 0.0, audioSeconds = 0.0;
};

// Defined below, with the audio-thread runner.
bool onMessageThread(std::function<void()> f, int timeoutMs = 5000);

// A host creates and destroys the processor on the message thread, and JUCE
// relies on that: the APVTS owns a juce::Timer, whose start/stop is not
// thread-safe against the timer thread. Constructing or destroying it on a
// worker thread is a race in the harness, not in the plugin, so do it the
// way a host does.
std::unique_ptr<SilentRiotProcessor> makeProcessor()
{
    std::unique_ptr<SilentRiotProcessor> p;
    onMessageThread([&p] { p = std::make_unique<SilentRiotProcessor>(); });
    if (p == nullptr) p = std::make_unique<SilentRiotProcessor>();   // loop already stopped
    return p;
}

// Every host attaches a listener to the processor. Without one,
// AudioProcessor::updateHostDisplay() iterates an empty list and takes no
// lock at all, so setLatencySamples() called from processBlock looks free
// when it is not. Attaching this makes the harness represent a host.
struct HostListener : public juce::AudioProcessorListener
{
    std::atomic<long long> changed { 0 }, paramChanged { 0 };
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override
    { changed.fetch_add(1, std::memory_order_relaxed); }
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override
    { paramChanged.fetch_add(1, std::memory_order_relaxed); }
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override {}
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override {}
};

struct Amp
{
    std::unique_ptr<SilentRiotProcessor> owned { makeProcessor() };
    SilentRiotProcessor& proc { *owned };
    HostListener host;
    double sr; int maxBlock, ch;
    juce::AudioBuffer<float> buf;
    juce::MidiBuffer midi;
    long long frame = 0;

    Amp(double sampleRate, int block, int channels)
        : sr(sampleRate), maxBlock(block), ch(channels)
    {
        proc.setPlayConfigDetails(channels, channels, sampleRate, block);
        proc.addListener(&host);
        buf.setSize(channels, block);
    }

    ~Amp()
    {
        proc.removeListener(&host);
        onMessageThread([this] { owned.reset(); });
    }

    void prepare() { proc.prepareToPlay(sr, maxBlock); }

    void set(const juce::String& id, float natural)
    {
        if (auto* p = proc.apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(natural));
    }

    // Writes the raw atomic the way a misbehaving host's automation would:
    // past the parameter object, so NaN/Inf actually reach processBlock.
    void poke(const juce::String& id, float raw)
    {
        if (auto* a = proc.apvts.getRawParameterValue(id)) a->store(raw);
    }

    void block(int n, const Fill& fill, Row& row, bool bypassed = false)
    {
        buf.setSize(ch, n, false, false, true);
        for (int c = 0; c < ch; ++c)
        {
            auto* d = buf.getWritePointer(c);
            for (int i = 0; i < n; ++i) d[i] = fill(frame + i, c);
        }
        const auto t0 = Clock::now();
        {
            rt::Scope scope;
            if (bypassed) proc.processBlockBypassed(buf, midi);
            else          proc.processBlock(buf, midi);
        }
        const auto t1 = Clock::now();
        row.t.add(std::chrono::duration<double, std::micro>(t1 - t0).count(),
                  1.0e6 * static_cast<double>(n) / sr);
        for (int c = 0; c < ch; ++c) row.a.scan(buf.getReadPointer(c), n, c);
        frame += n;
        row.audioSeconds += static_cast<double>(n) / sr;
    }

    void seconds(double secs, const Fill& fill, Row& row, int n = 0)
    {
        const int blk = n > 0 ? n : maxBlock;
        const long long total = static_cast<long long>(secs * sr);
        for (long long done = 0; done < total; done += blk)
            block(static_cast<int>(std::min<long long>(blk, total - done)), fill, row);
    }
};

void crank(Amp& a)
{
    a.set(P::GAIN, 0.9f);
    a.set(P::TONE, 0.5f);
    a.set(P::VOLUME, 0.8f);
}

void allEffectsOn(Amp& a)
{
    a.set(P::GATE_THRESHOLD, 0.35f);
    a.set(P::BOOST_ON, 1.0f);
    a.set(P::BOOST_LEVEL, 0.8f);
    a.set(P::BOOST_TIGHT, 0.6f);
    a.set(P::BOOST_BRIGHT, 0.7f);
    a.set(P::DELAY_ON, 1.0f);
    a.set(P::DELAY_TIME, 0.5f);
    a.set(P::DELAY_FEEDBACK, 0.7f);
    a.set(P::DELAY_MIX, 0.5f);
    a.set(P::DELAY_TONE, 0.6f);
    a.set(P::DELAY_PINGPONG, 1.0f);
    a.set(P::REVERB_ON, 1.0f);
    a.set(P::REVERB_MIX, 0.5f);
    a.set(P::REVERB_SIZE, 0.8f);
    a.set(P::REVERB_TONE, 0.5f);
    a.set(P::WIDTH, 0.8f);
    a.set(P::INPUT_LEVEL, 0.75f);
    a.set(P::OUTPUT_LEVEL, 0.65f);
}

// ── Audio thread ────────────────────────────────────────────────
// Every scenario runs on a thread of its own, while the real JUCE message
// thread (main(), in runDispatchLoop()) pumps beside it — so AsyncUpdater
// callbacks, the IR loader and the convolution's background thread all
// behave as they do in a host, instead of being starved by the test.
template <typename Fn>
void onAudioThread(Fn&& fn)
{
    std::thread th([&]
    {
#if defined(__linux__)
        sched_param sp {};
        sp.sched_priority = 70;
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);   // best effort
#endif
        fn();
    });
    th.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));   // let the queue drain
}

// Runs `f` on the real message thread and waits for it, so nothing it
// captures can go out of scope underneath it.
bool onMessageThread(std::function<void()> f, int timeoutMs)
{
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        if (mm->isThisTheMessageThread()) { f(); return true; }

    std::atomic<bool> done { false };
    if (! juce::MessageManager::callAsync([&f, &done] { f(); done.store(true); }))
        return false;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (! done.load())
    {
        if (Clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}

// Runs `fn` on an audio thread and fills in the row's counters around it.
template <typename Fn>
Row measure(const std::string& name, const std::string& config, Fn&& fn)
{
    Row row;
    row.name = name;
    row.config = config;
    row.t.reserve(1 << 16);
    const rt::Counts before = rt::snapshot();
    double cpu = 0.0;
    onAudioThread([&]
    {
        const double c0 = threadCpuSeconds();
        fn(row);
        cpu = threadCpuSeconds() - c0;
    });
    row.c = rt::since(before);
    row.cpuSeconds = cpu;
    return row;
}

// ── Shared assertions ───────────────────────────────────────────
void checkRow(const Row& r)
{
    const std::string where = r.name + " [" + r.config + "]";
    if (r.a.nonFinite > 0)
        report("CRITICAL", r.name,
               where + ": " + std::to_string(r.a.nonFinite) + " non-finite output samples");
    if (r.a.overshoot > 0)
        report("HIGH", r.name,
               where + ": " + std::to_string(r.a.overshoot) + " samples past +/-1 (peak "
               + std::to_string(r.a.maxAbs) + ")");
    if (r.c.allocs > 0)
        report("CRITICAL", r.name,
               where + ": " + std::to_string(r.c.allocs) + " allocations on the audio thread ("
               + std::to_string(r.c.allocBytes) + " bytes)");
    if (r.c.frees > 0)
        report("CRITICAL", r.name,
               where + ": " + std::to_string(r.c.frees) + " frees on the audio thread");
    if (r.c.locks > 0)
        report("CRITICAL", r.name,
               where + ": " + std::to_string(r.c.locks) + " mutex locks on the audio thread ("
               + std::to_string(r.c.contended) + " contended)");
}

void printRow(const Row& r)
{
    char line[320];
    std::snprintf(line, sizeof line,
                  "    %-34s mean %6.1f%%  p99 %7.1f%%  worst %8.1f%%  over %5lld/%-6lld  peak %6.3f",
                  r.config.c_str(), r.t.meanLoadPct(), r.t.percentile(99.0), r.t.worstLoadPct,
                  r.t.overruns, r.t.blocks, r.a.maxAbs);
    std::cout << line << "\n";
}

// ── A user IR on disk, for the IR scenarios ─────────────────────
juce::File writeIR(juce::TemporaryFile& tmp, int numSamples, double rate, bool decaying)
{
    const juce::File f = tmp.getFile();
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = std::make_unique<juce::FileOutputStream>(f);
    std::unique_ptr<juce::AudioFormatWriter> w(
        wav.createWriterFor(stream.get(), rate, 1, 24, {}, 0));
    if (w == nullptr) return {};
    stream.release();
    juce::AudioBuffer<float> b(1, numSamples);
    std::mt19937 rng { 1234 };
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        const float env = decaying
            ? std::exp(-4.0f * static_cast<float>(i) / static_cast<float>(numSamples)) : 1.0f;
        b.setSample(0, i, (i == 0 ? 1.0f : 0.25f * d(rng)) * env);
    }
    w->writeFromAudioSampleBuffer(b, 0, numSamples);
    return f;
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// 4. Scenarios
// ═══════════════════════════════════════════════════════════════

namespace {

std::vector<Row> g_rows;

std::vector<std::string> g_traceDump;

void keep(Row r)
{
    const bool violated = r.c.allocs > 0 || r.c.frees > 0 || r.c.locks > 0;
    checkRow(r);
    printRow(r);
    if (violated)
    {
        const std::string tr = rt::formatTraces();
        if (! tr.empty())
            g_traceDump.push_back(r.name + " [" + r.config + "]\n" + tr);
    }
    rt::resetTraces();
    g_rows.push_back(std::move(r));
}

// ── 1. Load matrix: can it hold its deadline at all? ────────────
void scenarioLoad(double secs, bool quick)
{
    std::cout << "\n[load] sample rate x oversampling x block size x effects\n";
    const std::vector<double> srs = quick ? std::vector<double>{ 48000.0, 192000.0 }
                                          : std::vector<double>{ 44100.0, 48000.0, 88200.0,
                                                                 96000.0, 176400.0, 192000.0 };
    const std::vector<int> blocks = quick ? std::vector<int>{ 128 } : std::vector<int>{ 64, 512 };
    for (double sr : srs)
        for (int os = 0; os < 3; ++os)
            for (int blk : blocks)
                for (int fx = 0; fx < 2; ++fx)
                {
                    char cfg[96];
                    std::snprintf(cfg, sizeof cfg, "%6.0f Hz  %dx  blk %-5d  fx %s",
                                  sr, 1 << (os + 1), blk, fx ? "on " : "off");
                    keep(measure("load", cfg, [&](Row& row)
                    {
                        Amp a(sr, blk, 2);
                        a.set(P::OVERSAMPLE, static_cast<float>(os));
                        crank(a);
                        if (fx) allEffectsOn(a);
                        a.prepare();
                        SignalGen g { Sig::DI, sr, std::mt19937 { 7 } };
                        a.seconds(secs, std::ref(g), row);
                    }));
                }
}

// ── 2. Block-size churn, including 1-sample blocks ──────────────
void scenarioBlocks(double secs)
{
    std::cout << "\n[blocks] host block size changing every call\n";
    for (int maxBlk : { 64, 2048 })
    {
        char cfg[96];
        std::snprintf(cfg, sizeof cfg, "random 1..%d", maxBlk);
        keep(measure("blocks", cfg, [&](Row& row)
        {
            Amp a(48000.0, maxBlk, 2);
            crank(a);
            allEffectsOn(a);
            a.prepare();
            SignalGen g { Sig::DI, 48000.0, std::mt19937 { 11 } };
            std::mt19937 rng { 99 };
            std::uniform_int_distribution<int> d(1, maxBlk);
            const long long total = static_cast<long long>(secs * 48000.0);
            for (long long done = 0; done < total;)
            {
                const int n = std::min<long long>(d(rng), total - done);
                a.block(n, std::ref(g), row);
                done += n;
            }
        }));
    }
    keep(measure("blocks", "every block 1 sample", [&](Row& row)
    {
        Amp a(48000.0, 2048, 2);
        crank(a);
        allEffectsOn(a);
        a.prepare();
        SignalGen g { Sig::DI, 48000.0, std::mt19937 { 12 } };
        for (int i = 0; i < 48000; ++i) a.block(1, std::ref(g), row);
    }));
}

// ── 3. Automation storm, including host NaN/Inf ─────────────────
void scenarioParams(double secs)
{
    std::cout << "\n[params] every parameter re-randomised every block\n";
    const juce::String knobs[] = {
        P::GAIN, P::TONE, P::VOLUME, P::GATE_THRESHOLD, P::BOOST_LEVEL, P::BOOST_TIGHT,
        P::BOOST_BRIGHT, P::DELAY_TIME, P::DELAY_FEEDBACK, P::DELAY_MIX, P::DELAY_TONE,
        P::REVERB_MIX, P::REVERB_SIZE, P::REVERB_TONE, P::WIDTH, P::INPUT_LEVEL, P::OUTPUT_LEVEL
    };
    const juce::String flags[] = {
        P::HALF_POWER, P::CAB_SIM, P::BOOST_ON, P::DELAY_ON, P::DELAY_PINGPONG, P::REVERB_ON
    };

    for (int hostile = 0; hostile < 2; ++hostile)
    {
        keep(measure("params", hostile ? "random + NaN/Inf injection" : "random every block",
                     [&](Row& row)
        {
            Amp a(48000.0, 128, 2);
            crank(a);
            allEffectsOn(a);
            a.prepare();
            SignalGen g { Sig::DI, 48000.0, std::mt19937 { 13 } };
            std::mt19937 rng { 4242 };
            std::uniform_real_distribution<float> u(0.0f, 1.0f);
            const long long total = static_cast<long long>(secs * 48000.0);
            int n = 0;
            for (long long done = 0; done < total; done += 128, ++n)
            {
                for (const auto& id : knobs) a.set(id, u(rng));
                for (const auto& id : flags) a.set(id, u(rng) > 0.5f ? 1.0f : 0.0f);
                a.set(P::DELAY_SYNC_DIV, static_cast<float>(rng() % 9));
                if (hostile != 0 && (n % 7) == 0)
                {
                    const float poison = (n % 21) == 0 ? std::numeric_limits<float>::quiet_NaN()
                                       : (n % 14) == 0 ? std::numeric_limits<float>::infinity()
                                                       : -1.0e30f;
                    a.poke(P::GAIN, poison);
                    a.poke(P::VOLUME, poison);
                    a.poke(P::TONE, poison);
                    a.poke(P::DELAY_FEEDBACK, poison);
                    a.poke(P::REVERB_SIZE, poison);
                    a.poke(P::INPUT_LEVEL, poison);
                    a.poke(P::OUTPUT_LEVEL, poison);
                    a.poke(P::WIDTH, poison);
                    a.poke(P::GATE_THRESHOLD, poison);
                }
                a.block(128, std::ref(g), row);
            }
            // Recovery: sane settings must give sane audio again.
            for (const auto& id : knobs) a.set(id, 0.5f);
            crank(a);
            Row after;
            after.t.reserve(4096);
            SignalGen clean { Sig::Sine, 48000.0, std::mt19937 { 5 } };
            a.seconds(1.0, std::ref(clean), after, 128);
            if (after.a.nonFinite > 0 || after.a.rms() < 1.0e-6)
                report("CRITICAL", "params",
                       std::string("plugin did not recover after hostile automation: rms ")
                       + std::to_string(after.a.rms()) + ", non-finite "
                       + std::to_string(after.a.nonFinite));
        }));
    }
}

// ── 4. Hostile input signals ────────────────────────────────────
void scenarioHostile(double secs)
{
    std::cout << "\n[hostile] what a broken input chain can feed the plugin\n";
    for (Sig s : { Sig::Noise, Sig::Square, Sig::Hot, Sig::Denormal, Sig::NaN_, Sig::Inf_,
                   Sig::Huge, Sig::Nyquist, Sig::Impulse })
    {
        keep(measure("hostile", sigName(s), [&](Row& row)
        {
            Amp a(48000.0, 256, 2);
            crank(a);
            allEffectsOn(a);
            a.prepare();
            SignalGen g { s, 48000.0, std::mt19937 { 17 } };
            a.seconds(secs, std::ref(g), row);

            // Must come back cleanly on a normal signal.
            Row after;
            after.t.reserve(4096);
            SignalGen clean { Sig::Sine, 48000.0, std::mt19937 { 5 } };
            a.seconds(1.5, std::ref(clean), after, 256);
            if (after.a.nonFinite > 0)
                report("CRITICAL", "hostile",
                       std::string(sigName(s)) + ": output still non-finite 1.5 s later");
            else if (after.a.rms() < 1.0e-7)
                report("HIGH", "hostile",
                       std::string(sigName(s)) + ": plugin stayed silent 1.5 s later (rms "
                       + std::to_string(after.a.rms()) + ") - state poisoned");
        }));
    }
}

// ── 5. Mode churn: everything the user can flip, as fast as possible ──
void scenarioModes(double secs)
{
    std::cout << "\n[modes] power / mute / cab / 7W / oversample / bypass / reset churn\n";

    keep(measure("modes", "oversample switch every block", [&](Row& row)
    {
        Amp a(48000.0, 128, 2);
        crank(a);
        a.prepare();
        SignalGen g { Sig::DI, 48000.0, std::mt19937 { 21 } };
        const long long total = static_cast<long long>(secs * 48000.0);
        int n = 0;
        for (long long done = 0; done < total; done += 128, ++n)
        {
            a.set(P::OVERSAMPLE, static_cast<float>(n % 3));
            a.block(128, std::ref(g), row);
        }
    }));

    keep(measure("modes", "cab + 7W + power + mute churn", [&](Row& row)
    {
        Amp a(48000.0, 128, 2);
        crank(a);
        allEffectsOn(a);
        a.prepare();
        SignalGen g { Sig::DI, 48000.0, std::mt19937 { 22 } };
        const long long total = static_cast<long long>(secs * 48000.0);
        int n = 0;
        for (long long done = 0; done < total; done += 128, ++n)
        {
            a.set(P::CAB_SIM,    (n % 2) ? 1.0f : 0.0f);
            a.set(P::HALF_POWER, (n % 3) ? 1.0f : 0.0f);
            a.set(P::POWER,      (n % 11) ? 1.0f : 0.0f);
            a.set(P::TUNER_MUTE, (n % 17) == 0 ? 1.0f : 0.0f);
            a.block(128, std::ref(g), row);
        }
    }));

    keep(measure("modes", "host bypass + reset() every block", [&](Row& row)
    {
        Amp a(48000.0, 128, 2);
        crank(a);
        allEffectsOn(a);
        a.prepare();
        SignalGen g { Sig::DI, 48000.0, std::mt19937 { 23 } };
        const long long total = static_cast<long long>(secs * 48000.0);
        int n = 0;
        for (long long done = 0; done < total; done += 128, ++n)
        {
            if ((n % 5) == 0) a.proc.reset();
            a.block(128, std::ref(g), row, (n % 3) == 0);
        }
    }));
}

// ── 6. IR churn ─────────────────────────────────────────────────
void scenarioIR(double secs)
{
    std::cout << "\n[ir] IR loading and IR_INDEX automation\n";

    juce::TemporaryFile t1(".wav"), t2(".wav"), t3(".wav");
    std::vector<juce::File> irs {
        writeIR(t1, 2048, 48000.0, true),
        writeIR(t2, 16384, 48000.0, true),     // past the 8192 cap: truncated + faded
        writeIR(t3, 512, 96000.0, true),       // needs resampling
    };

    keep(measure("ir", "IR_INDEX automated every block", [&](Row& row)
    {
        Amp a(48000.0, 128, 2);
        crank(a);
        a.prepare();
        for (const auto& f : irs)
            if (f.existsAsFile()) a.proc.loadUserIR(f);
        // let the convolution install
        SignalGen warm { Sig::Sine, 48000.0, std::mt19937 { 3 } };
        Row settle; settle.t.reserve(2048);
        a.seconds(0.5, std::ref(warm), settle);

        SignalGen g { Sig::DI, 48000.0, std::mt19937 { 24 } };
        const long long total = static_cast<long long>(secs * 48000.0);
        int n = 0;
        for (long long done = 0; done < total; done += 128, ++n)
        {
            a.set(P::IR_INDEX, static_cast<float>(n % 6));   // 0..3 real, 4..5 past the list
            a.block(128, std::ref(g), row);
        }
    }));
}

// ── 7. Message thread working while the audio thread runs ───────
void scenarioThreads(double secs)
{
    std::cout << "\n[threads] state save/restore, presets and IR loads during playback\n";

    juce::TemporaryFile t1(".wav");
    const juce::File ir = writeIR(t1, 4096, 48000.0, true);

    Row row;
    row.name = "threads";
    row.config = "msg thread + automation thread";
    row.t.reserve(1 << 16);
    const rt::Counts before = rt::snapshot();

    Amp a(48000.0, 128, 2);
    crank(a);
    allEffectsOn(a);
    a.prepare();

    std::atomic<bool> stop { false };
    long long stateOps = 0;

    // A host's automation thread.
    std::thread automation([&]
    {
        std::mt19937 rng { 777 };
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        while (! stop.load())
        {
            a.set(P::GAIN, u(rng));
            a.set(P::VOLUME, u(rng));
            a.set(P::DELAY_FEEDBACK, u(rng));
            a.set(P::OVERSAMPLE, static_cast<float>(rng() % 3));
            a.set(P::IR_INDEX, static_cast<float>(rng() % 3));
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
    });

    // The editor's 30 Hz timer: meters, DSP readout, tuner window.
    std::thread editorish([&]
    {
        std::vector<float> scratch(2048);
        while (! stop.load())
        {
            const auto r = a.proc.getDSPReadout();
            (void) r.vBPlus;
            a.proc.readLatestTunerSamples(scratch.data(), static_cast<int>(scratch.size()));
            (void) a.proc.inputLevelForGUI.load();
            (void) a.proc.outputLevelForGUI.load();
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
    });

    std::atomic<bool> done { false };
    double cpu = 0.0;
    std::thread audio([&]
    {
        const double c0 = threadCpuSeconds();
        SignalGen g { Sig::DI, 48000.0, std::mt19937 { 25 } };
        const long long total = static_cast<long long>(secs * 48000.0);
        for (long long i = 0; i < total; i += 128) a.block(128, std::ref(g), row);
        cpu = threadCpuSeconds() - c0;
        done.store(true);
    });

    // Everything a user does through the editor, on the real message thread,
    // while the audio thread is mid-block.
    juce::MemoryBlock saved;
    std::mt19937 rng { 31337 };
    while (! done.load())
    {
        const unsigned pick = rng() % 5;
        const bool ok = onMessageThread([&]
        {
            switch (pick)
            {
                case 0: a.proc.getStateInformation(saved); break;
                case 1: if (saved.getSize() > 0)
                            a.proc.setStateInformation(saved.getData(),
                                                       static_cast<int>(saved.getSize()));
                        break;
                case 2: a.proc.applyPreset(static_cast<int>(rng() % 5)); break;
                case 3: if (ir.existsAsFile() && a.proc.getNumUserIRs() < 8)
                            a.proc.loadUserIR(ir);
                        break;
                default:
                    if (a.proc.getNumUserIRs() > 0)
                        a.proc.selectUserIR(static_cast<int>(
                            rng() % static_cast<unsigned>(a.proc.getNumUserIRs())));
                    else
                        a.proc.selectBuiltInCab();
                    break;
            }
        });
        if (! ok)
        {
            report("HIGH", "threads", "message thread stalled over 5 s during playback");
            break;
        }
        ++stateOps;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    stop.store(true);
    audio.join();
    automation.join();
    editorish.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    row.c = rt::since(before);
    row.cpuSeconds = cpu;
    std::cout << "    (" << stateOps << " message-thread operations during playback)\n";
    keep(std::move(row));
}

// ── 7b. Recovery from a poisoned input sample ───────────────────
// A plugin upstream, a glitching interface or a corrupt file can hand the
// amp a non-finite sample. processBlock scrubs its OUTPUT, so nothing
// non-finite reaches the host - but nothing scrubs the INPUT, so the
// question this answers is whether the amp still plays afterwards.
void scenarioRecover()
{
    std::cout << "\n[recover] does the amp survive a non-finite input sample?\n";

    struct Case { const char* what; float bad; int samples; };
    const Case cases[] = {
        { "1 NaN sample",      std::numeric_limits<float>::quiet_NaN(), 1 },
        { "64 NaN samples",    std::numeric_limits<float>::quiet_NaN(), 64 },
        { "1 +Inf sample",     std::numeric_limits<float>::infinity(),  1 },
        { "64 +Inf samples",   std::numeric_limits<float>::infinity(),  64 },
        { "1 sample of 1e30",  1.0e30f,                                 1 },
    };

    for (const Case& c : cases)
    {
        Row row;
        row.name = "recover";
        row.config = c.what;
        row.t.reserve(1 << 14);
        double before = 0.0, after = 0.0, afterReset = 0.0, afterPower = 0.0, afterPrepare = 0.0;
        bool readoutFinite = true;

        onAudioThread([&]
        {
            Amp a(48000.0, 128, 2);
            crank(a);
            a.prepare();
            SignalGen sine { Sig::Sine, 48000.0, std::mt19937 { 2 } };

            auto play = [&](double secs)
            {
                Row r; r.t.reserve(1 << 12);
                a.seconds(secs, std::ref(sine), r, 128);
                return r.a.rms();
            };

            before = play(1.0);

            // One block whose first `samples` samples are poisoned.
            const long long start = a.frame;
            const int n = c.samples;
            Fill poisoned = [&](long long i, int ch) -> float
            {
                if (i >= start && i < start + n) return c.bad;
                return sine(i, ch);
            };
            a.block(128, poisoned, row);

            after = play(2.0);

            a.proc.reset();
            afterReset = play(2.0);

            // What a player would try first: the PWR switch off and on again.
            a.set(P::POWER, 0.0f);
            play(0.5);
            a.set(P::POWER, 1.0f);
            afterPower = play(2.0);

            const auto d = a.proc.getDSPReadout();
            readoutFinite = std::isfinite(d.vBPlus) && std::isfinite(d.vScreen)
                         && std::isfinite(d.vPi) && std::isfinite(d.vPreamp)
                         && std::isfinite(d.biasV) && std::isfinite(d.sagPercent);

            a.prepare();                       // what a host does on a rate/size change
            afterPrepare = play(2.0);
        });

        char msg[320];
        std::snprintf(msg, sizeof msg,
                      "%-18s rms before %.5f -> after %.9f -> reset() %.9f"
                      " -> PWR off/on %.9f -> prepareToPlay() %.5f%s",
                      c.what, before, after, afterReset, afterPower, afterPrepare,
                      readoutFinite ? "" : "   [DSP readout went non-finite]");
        std::cout << "    " << msg << "\n";

        // Judge on the steady state four seconds later, not on the decaying
        // tail in the two seconds right after the event.
        const bool dead = afterReset < before * 1.0e-4;
        if (dead && afterPrepare > before * 0.1)
            report("CRITICAL", "recover",
                   std::string(c.what)
                   + " silences the amp for the rest of the session: output rms "
                   + std::to_string(after) + " vs " + std::to_string(before)
                   + " before; neither reset() (" + std::to_string(afterReset)
                   + ") nor a PWR off/on cycle (" + std::to_string(afterPower)
                   + ") clears it, only a fresh prepareToPlay() ("
                   + std::to_string(afterPrepare) + ")");
        else if (dead)
            report("CRITICAL", "recover",
                   std::string(c.what) + " silences the amp permanently (not even"
                   " prepareToPlay() recovers: " + std::to_string(afterPrepare) + ")");
        g_rows.push_back(std::move(row));
    }
}

// ── 7c. Fixed per-block overhead ────────────────────────────────
// What a block costs before a single sample is processed. processBlock
// opens with ~28 APVTS getRawParameterValue() calls, and APVTS keeps its
// adapters in a std::map keyed by string - so each one is a red-black tree
// walk with string compares, paid per block whatever the block size. With
// POWER off the block returns straight after those reads, which isolates
// the fixed cost from the DSP.
void scenarioOverhead()
{
    std::cout << "\n[overhead] fixed cost per block, before any DSP\n";

    auto meanUs = [](int blk, bool powerOn, int blocks)
    {
        Row row;
        row.t.reserve(static_cast<std::size_t>(blocks) + 16);
        onAudioThread([&]
        {
            Amp a(48000.0, blk, 2);
            crank(a);
            a.set(P::POWER, powerOn ? 1.0f : 0.0f);
            a.prepare();
            SignalGen g { Sig::Sine, 48000.0, std::mt19937 { 31 } };
            for (int i = 0; i < blocks; ++i) a.block(blk, std::ref(g), row);
        });
        return row.t.blocks > 0 ? row.t.totalUs / static_cast<double>(row.t.blocks) : 0.0;
    };

    const double idle1   = meanUs(1,   false, 60000);
    const double idle512 = meanUs(512, false, 2000);
    const double live1   = meanUs(1,   true,  20000);
    const double live64  = meanUs(64,  true,  8000);

    char msg[320];
    std::snprintf(msg, sizeof msg,
                  "power off: %.2f us/block at 1 sample, %.2f us/block at 512 - "
                  "the difference is the DSP, the rest is per-block overhead",
                  idle1, idle512);
    std::cout << "    " << msg << "\n";
    std::snprintf(msg, sizeof msg,
                  "power on:  %.2f us/block at 1 sample, %.2f us/block at 64 "
                  "(%.1f%% and %.1f%% of the deadline)",
                  live1, live64, 100.0 * live1 / (1.0e6 / 48000.0),
                  100.0 * live64 / (1.0e6 * 64.0 / 48000.0));
    std::cout << "    " << msg << "\n";

    // The fixed cost is what a small-buffer session pays on every block.
    const double deadline64 = 1.0e6 * 64.0 / 48000.0;
    const double sharePct = 100.0 * idle1 / deadline64;
    if (sharePct > 1.0)
    {
        std::snprintf(msg, sizeof msg,
                      "%.2f us of per-block overhead before any DSP runs - %.1f%% of a"
                      " 64-sample deadline at 48 kHz, paid again at every block size",
                      idle1, sharePct);
        report("MEDIUM", "overhead", msg);
    }
}

// ── 8. Denormal stall ───────────────────────────────────────────
void scenarioDenormals()
{
    std::cout << "\n[denormals] block time while a tail decays into denormal range\n";
    Row loud, quiet;
    loud.name = quiet.name = "denormals";
    loud.config = "loud";
    quiet.config = "silence after loud (tail decaying)";
    loud.t.reserve(1 << 14);
    quiet.t.reserve(1 << 16);

    const rt::Counts before = rt::snapshot();
    onAudioThread([&]
    {
        Amp a(48000.0, 128, 2);
        crank(a);
        allEffectsOn(a);
        a.prepare();
        SignalGen g { Sig::Noise, 48000.0, std::mt19937 { 26 } };
        a.seconds(3.0, std::ref(g), loud);
        SignalGen s { Sig::Silence, 48000.0, std::mt19937 { 27 } };
        a.seconds(20.0, std::ref(s), quiet);
    });
    quiet.c = rt::since(before);

    keep(loud);
    keep(quiet);

    const float ratio = loud.t.meanLoadPct() > 0.0f
                      ? quiet.t.meanLoadPct() / loud.t.meanLoadPct() : 0.0f;
    char msg[200];
    std::snprintf(msg, sizeof msg,
                  "silent-tail blocks cost %.2fx a loud block (%.1f%% vs %.1f%% of deadline)",
                  ratio, quiet.t.meanLoadPct(), loud.t.meanLoadPct());
    if (ratio > 1.15f) report("HIGH", "denormals", std::string("denormal stall: ") + msg);
    else               std::cout << "    ok: " << msg << "\n";
}

// ── 9. Long-run stability at the worst settings ─────────────────
void scenarioStability(double secs)
{
    std::cout << "\n[stability] everything maxed, long run\n";
    keep(measure("stability", "all knobs maxed", [&](Row& row)
    {
        Amp a(48000.0, 512, 2);
        a.set(P::GAIN, 1.0f);
        a.set(P::TONE, 1.0f);
        a.set(P::VOLUME, 1.0f);
        a.set(P::GATE_THRESHOLD, 0.0f);
        a.set(P::BOOST_ON, 1.0f);
        a.set(P::BOOST_LEVEL, 1.0f);
        a.set(P::BOOST_TIGHT, 1.0f);
        a.set(P::BOOST_BRIGHT, 1.0f);
        a.set(P::DELAY_ON, 1.0f);
        a.set(P::DELAY_FEEDBACK, 1.0f);
        a.set(P::DELAY_MIX, 1.0f);
        a.set(P::DELAY_TIME, 0.02f);
        a.set(P::REVERB_ON, 1.0f);
        a.set(P::REVERB_MIX, 1.0f);
        a.set(P::REVERB_SIZE, 1.0f);
        a.set(P::WIDTH, 1.0f);
        a.set(P::INPUT_LEVEL, 1.0f);
        a.set(P::OUTPUT_LEVEL, 1.0f);
        a.prepare();
        SignalGen g { Sig::Noise, 48000.0, std::mt19937 { 28 } };
        a.seconds(secs * 0.5, std::ref(g), row);
        SignalGen s { Sig::Silence, 48000.0, std::mt19937 { 29 } };
        a.seconds(secs * 0.5, std::ref(s), row);
    }));
}

// ── 10. Opt-in: blocks larger than the host promised ────────────
void scenarioOverBlock()
{
    std::cout << "\n[overblock] host sends a block larger than prepareToPlay's maximum\n";
    std::cout << "    (JUCE's oversampler sizes its buffers from prepareToPlay; this may abort)\n";
    keep(measure("overblock", "prepared 128, fed 2048", [&](Row& row)
    {
        Amp a(48000.0, 128, 2);
        crank(a);
        a.prepare();
        a.buf.setSize(2, 4096);
        SignalGen g { Sig::DI, 48000.0, std::mt19937 { 30 } };
        for (int i = 0; i < 20; ++i) a.block(2048, std::ref(g), row);
    }));
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// 5. main
// ═══════════════════════════════════════════════════════════════

int main(int argc, char* argv[])
{
    std::string only, mdPath;
    bool quick = false, overblock = false;
    double scale = 1.0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--help")
        {
            std::cout <<
                "rt_stress - real-time stress harness for SilentRiotProcessor\n"
                "  --quick            small matrix, short runs (~1 min)\n"
                "  --only=a,b,c       run only these scenarios\n"
                "  --scale=N          multiply every run length by N\n"
                "  --overblock        also run the opt-in over-sized-block scenario\n"
                "  --md=PATH          write a Markdown report\n"
                "scenarios: load blocks params hostile modes ir threads recover overhead denormals\n"
                "           stability\n";
            return 0;
        }
        if (arg == "--quick") quick = true;
        else if (arg == "--overblock") overblock = true;
        else if (arg.rfind("--only=", 0) == 0) only = arg.substr(7);
        else if (arg.rfind("--md=", 0) == 0) mdPath = arg.substr(5);
        else if (arg.rfind("--scale=", 0) == 0) scale = std::atof(arg.c_str() + 8);
    }
    auto wanted = [&](const char* name)
    {
        return only.empty() || only.find(name) != std::string::npos;
    };

    juce::ScopedJuceInitialiser_GUI juceInit;
    rt::warmUp();

    // Self-test: the probe must see an allocation and a lock inside a Scope,
    // or a clean report below would mean nothing.
    {
        const rt::Counts before = rt::snapshot();
        {
            rt::Scope s;
            volatile auto* p = new int[16];
            delete[] const_cast<int*>(p);
#if SR_RT_INTERPOSE_PTHREAD
            pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
            pthread_mutex_lock(&m);
            pthread_mutex_unlock(&m);
#endif
        }
        const rt::Counts d = rt::since(before);
        std::cout << "probe self-test: " << d.allocs << " alloc, " << d.frees << " free, "
                  << d.locks << " lock\n";
#if SR_RT_INTERPOSE_NEW
        if (d.allocs == 0 || d.frees == 0)
        {
            std::cerr << "probe self-test FAILED: allocation interposer is not active\n";
            return 2;
        }
#else
        std::cout << "  note: sanitizer build - allocation findings unavailable,"
                     " the sanitizer is the instrument here\n";
#endif
#if SR_RT_INTERPOSE_PTHREAD
        if (d.locks == 0)
            std::cout << "  note: pthread interposer inactive - lock findings unavailable\n";
#else
        std::cout << "  note: pthread interposer compiled out - lock findings unavailable\n";
#endif
        rt::g_allocs = 0; rt::g_allocBytes = 0; rt::g_frees = 0;
        rt::g_locks = 0; rt::g_contended = 0; rt::g_traceCount = 0;
    }

    const double s = (quick ? 0.25 : 1.0) * scale;
    const auto t0 = Clock::now();

    // The scenarios run on their own thread so that main() can be the real
    // message thread for the whole run (JUCE's runDispatchLoopUntil is
    // compiled out of the plugin's modules, and the harness must not be the
    // reason an AsyncUpdater never fires).
    std::thread driver([&]
    {
        if (wanted("load"))      scenarioLoad(0.5 * s, quick);
        if (wanted("blocks"))    scenarioBlocks(4.0 * s);
        if (wanted("params"))    scenarioParams(4.0 * s);
        if (wanted("hostile"))   scenarioHostile(1.0 * s);
        if (wanted("modes"))     scenarioModes(4.0 * s);
        if (wanted("ir"))        scenarioIR(4.0 * s);
        if (wanted("threads"))   scenarioThreads(8.0 * s);
        if (wanted("recover"))   scenarioRecover();
        if (wanted("overhead"))  scenarioOverhead();
        if (wanted("denormals")) scenarioDenormals();
        if (wanted("stability")) scenarioStability(20.0 * s);
        if (overblock)           scenarioOverBlock();
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    driver.join();

    const double elapsed = std::chrono::duration<double>(Clock::now() - t0).count();

    // ── Report ──
    std::cout << "\n═══ summary ═══\n";
    double audioTotal = 0.0;
    for (const auto& r : g_rows) audioTotal += r.audioSeconds;
    std::cout << g_rows.size() << " runs, " << audioTotal << " s of audio rendered in "
              << elapsed << " s wall\n";

    std::string traces;
    for (const auto& t : g_traceDump) traces += t + "\n";
    if (! traces.empty())
        std::cout << "\nreal-time violations, with backtraces:\n" << traces;

    if (g_findings.empty())
    {
        std::cout << "\nno findings\n";
    }
    else
    {
        std::cout << "\n" << g_findings.size() << " findings:\n";
        for (const auto& f : g_findings)
            std::cout << "  " << f.level << "  [" << f.scenario << "] " << f.text << "\n";
    }

    if (! mdPath.empty())
    {
        std::ostringstream md;
        md << "# RT stress run\n\n";
        md << "- runs: " << g_rows.size() << "\n- audio rendered: " << audioTotal
           << " s\n- wall: " << elapsed << " s\n\n";
        md << "## Measurements\n\n";
        md << "| scenario | config | mean load | p99 | worst | overruns | peak | max step |"
              " allocs | locks |\n";
        md << "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
        for (const auto& r : g_rows)
        {
            char line[400];
            std::snprintf(line, sizeof line,
                          "| %s | `%s` | %.1f%% | %.1f%% | %.1f%% | %lld/%lld | %.3f | %.3f"
                          " | %lld | %lld |\n",
                          r.name.c_str(), r.config.c_str(), r.t.meanLoadPct(),
                          r.t.percentile(99.0), r.t.worstLoadPct, r.t.overruns, r.t.blocks,
                          r.a.maxAbs, r.a.maxStep, r.c.allocs, r.c.locks);
            md << line;
        }
        md << "\n## Findings\n\n";
        if (g_findings.empty()) md << "None.\n";
        for (const auto& f : g_findings)
            md << "- **" << f.level << "** [" << f.scenario << "] " << f.text << "\n";
        if (! traces.empty()) md << "\n## Backtraces\n\n```\n" << traces << "```\n";
        juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(mdPath))
            .replaceWithText(md.str());
        std::cout << "\nwrote " << mdPath << "\n";
    }

    return g_anyCritical ? 1 : 0;
}
