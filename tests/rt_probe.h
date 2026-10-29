// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Boris Miscenco
#pragma once
// ═══════════════════════════════════════════════════════════════
// rt_probe.h — instrumentation for the real-time stress harness
// (tests/rt_stress.cpp). Measures the three things a plugin must never do
// on the audio thread, instead of arguing about them:
//
//   * allocate — global operator new/delete are replaced; every allocation
//                made inside an rt::Scope is counted and the first few keep
//                a backtrace, so the culprit is named, not guessed
//   * block    — pthread_mutex_lock/trylock are interposed, so a lock taken
//                inside an rt::Scope is counted (contended ones separately)
//   * overrun  — every block is timed against its own deadline
//                (numSamples / sampleRate)
//
// Only the counting is here. The operator new/delete and pthread_mutex_*
// definitions live in rt_stress.cpp: exactly one translation unit may
// define them, and they must be in the executable to win symbol
// resolution against libc.
//
// Everything on the counting path is allocation-free and lock-free, and
// rt::warmUp() forces the one-off lazy work (unwinder tables, dlsym) to
// happen before the first measured block.
// ═══════════════════════════════════════════════════════════════

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
 #define SR_RT_HAVE_BACKTRACE 1
 #include <execinfo.h>
#else
 #define SR_RT_HAVE_BACKTRACE 0
#endif

namespace rt {

inline constexpr int kMaxTraces  = 16;   // keep a backtrace for this many events
inline constexpr int kTraceDepth = 20;

struct Trace
{
    const char* kind  = nullptr;
    std::size_t bytes = 0;
    int         depth = 0;
    void*       frames[kTraceDepth] {};
};

// ── Counters ────────────────────────────────────────────────────
// g_depth > 0 means "this thread is inside the region under test".
inline thread_local int  g_depth  = 0;
inline thread_local bool g_inNote = false;   // re-entry guard for backtrace()

inline std::atomic<long long> g_allocs     { 0 };
inline std::atomic<long long> g_allocBytes { 0 };
inline std::atomic<long long> g_frees      { 0 };
inline std::atomic<long long> g_locks      { 0 };
inline std::atomic<long long> g_contended  { 0 };
inline std::atomic<int>       g_traceCount { 0 };
inline Trace                  g_traces[kMaxTraces];

// Records one forbidden event. Called from operator new and from the
// pthread interposers, so it must not allocate or lock itself.
inline void note(const char* kind, std::size_t bytes)
{
    if (g_depth <= 0 || g_inNote) return;
    g_inNote = true;
    const int idx = g_traceCount.fetch_add(1, std::memory_order_relaxed);
    if (idx < kMaxTraces)
    {
        g_traces[idx].kind  = kind;
        g_traces[idx].bytes = bytes;
#if SR_RT_HAVE_BACKTRACE
        g_traces[idx].depth = backtrace(g_traces[idx].frames, kTraceDepth);
#endif
    }
    g_inNote = false;
}

// RAII: marks the calling thread as "under test" for the scope's lifetime.
struct Scope
{
    Scope()  { ++g_depth; }
    ~Scope() { --g_depth; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

struct Counts
{
    long long allocs = 0, allocBytes = 0, frees = 0, locks = 0, contended = 0;
    bool clean() const { return allocs == 0 && frees == 0 && locks == 0; }
};

inline Counts snapshot()
{
    Counts c;
    c.allocs     = g_allocs.load(std::memory_order_relaxed);
    c.allocBytes = g_allocBytes.load(std::memory_order_relaxed);
    c.frees      = g_frees.load(std::memory_order_relaxed);
    c.locks      = g_locks.load(std::memory_order_relaxed);
    c.contended  = g_contended.load(std::memory_order_relaxed);
    return c;
}

inline Counts since(const Counts& before)
{
    const Counts now = snapshot();
    Counts d;
    d.allocs     = now.allocs     - before.allocs;
    d.allocBytes = now.allocBytes - before.allocBytes;
    d.frees      = now.frees      - before.frees;
    d.locks      = now.locks      - before.locks;
    d.contended  = now.contended  - before.contended;
    return d;
}

// Symbolised backtraces of the recorded events. Message thread, after the run.
std::string formatTraces();

// Drops the kept backtraces so the next scenario's are attributed to it.
inline void resetTraces() { g_traceCount.store(0, std::memory_order_relaxed); }

// Forces the one-off lazy work on every counting path (unwinder tables,
// dlsym of the real pthread entry points) to happen before any measured
// block, so it is not charged to the plugin.
void warmUp();

// ── Per-block timing ────────────────────────────────────────────
struct Timing
{
    std::vector<float> loadPct;     // block time as a % of its own deadline
    double totalUs = 0.0, budgetUs = 0.0;
    float  worstUs = 0.0f, worstLoadPct = 0.0f;
    long long blocks = 0, overruns = 0;

    void reserve(std::size_t n) { loadPct.reserve(n); }

    void add(double us, double deadlineUs)
    {
        ++blocks;
        totalUs  += us;
        budgetUs += deadlineUs;
        const float load = deadlineUs > 0.0 ? static_cast<float>(100.0 * us / deadlineUs) : 0.0f;
        loadPct.push_back(load);
        worstUs      = std::max(worstUs, static_cast<float>(us));
        worstLoadPct = std::max(worstLoadPct, load);
        if (us > deadlineUs) ++overruns;
    }

    float meanLoadPct() const
    {
        return budgetUs > 0.0 ? static_cast<float>(100.0 * totalUs / budgetUs) : 0.0f;
    }

    // Percentile of the per-block load. Sorts a copy — off the audio thread.
    float percentile(double p) const
    {
        if (loadPct.empty()) return 0.0f;
        std::vector<float> s(loadPct);
        auto idx = static_cast<std::size_t>(p / 100.0 * static_cast<double>(s.size() - 1) + 0.5);
        idx = std::min(idx, s.size() - 1);
        std::nth_element(s.begin(), s.begin() + static_cast<long>(idx), s.end());
        return s[idx];
    }
};

// ── Output sanity ───────────────────────────────────────────────
// What the host receives. The plugin promises finite samples inside ±1
// (its own tanh/softClip safety nets), so anything else is a defect.
struct AudioCheck
{
    long long nonFinite = 0, overshoot = 0, samples = 0;
    float maxAbs = 0.0f;
    double sumSq = 0.0;
    // Largest sample-to-sample jump seen, per channel — a click detector.
    float maxStep = 0.0f;
    float lastSample[2] { 0.0f, 0.0f };
    bool  haveLast[2] { false, false };

    void scan(const float* data, int n, int channel)
    {
        const int ch = channel < 2 ? channel : 1;
        for (int i = 0; i < n; ++i)
        {
            const float s = data[i];
            ++samples;
            if (!std::isfinite(s)) { ++nonFinite; haveLast[ch] = false; continue; }
            const float a = std::fabs(s);
            if (a > maxAbs) maxAbs = a;
            if (a > 1.0f + 1.0e-4f) ++overshoot;
            sumSq += static_cast<double>(s) * s;
            if (haveLast[ch]) maxStep = std::max(maxStep, std::fabs(s - lastSample[ch]));
            lastSample[ch] = s;
            haveLast[ch]   = true;
        }
    }

    double rms() const { return samples > 0 ? std::sqrt(sumSq / static_cast<double>(samples)) : 0.0; }
};

} // namespace rt
