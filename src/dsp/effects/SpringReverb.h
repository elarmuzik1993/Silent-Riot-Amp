#pragma once
// ═══════════════════════════════════════════════════════════════
// SpringReverbCore — spring-tank reverb (pure C++, header-only)
//
// Models a guitar-amp spring tank. The defining feature of a spring is
// DISPERSION: frequencies travel the spring at different speeds, producing
// the characteristic chirp/"boing" and a bandlimited, darkened decay. This
// is reproduced with the published parametric-spring technique — a cascade
// of first-order all-pass filters (the dispersion network) inside a damped
// feedback delay loop. Original implementation; no third-party code.
//
// Pure C++ (no JUCE) so the core is unit-testable in the fast test suite.
// The juce-facing Reverb wrapper (Reverb.{h,cpp}) owns one core per channel.
// ═══════════════════════════════════════════════════════════════

#include "../../utils/Constants.h"
#include "../filters/OnePole.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace sr {

// Cascade of first-order all-passes: H(z) = (a + z⁻¹)/(1 + a·z⁻¹).
// Lossless (unit magnitude) but frequency-dependent group delay → the chirp.
struct DispersionChain {
    std::array<float, SPRING_DISP_STAGES> z{};
    float a = SPRING_DISP_COEFF;

    void reset() { z.fill(0.0f); }

    float process(float x) {
        for (int i = 0; i < SPRING_DISP_STAGES; ++i) {
            // Direct-form II first-order all-pass.
            float w = x - a * z[i];
            float y = a * w + z[i];
            z[i] = w + ANTI_DENORMAL;
            x = y;
        }
        return x;
    }
};

// One spring: a feedback delay loop with dispersion + damping in the path.
struct Spring {
    std::vector<float> buf;   // ring buffer (sized in prepare)
    int   bufSize  = 0;
    int   writePtr = 0;
    int   loopLen  = 1;       // current delay length (samples)
    float baseMs   = 28.0f;   // base loop length before SIZE scaling
    float feedback = 0.7f;

    DispersionChain dispersion;
    OnePoleLP       loopLP;
    OnePoleHP       loopHP;

    void prepare(float sampleRate, float baseMsIn, float detune) {
        baseMs = baseMsIn * detune;
        bufSize = static_cast<int>(std::ceil(SPRING_MAX_LOOP_MS * 0.001f * sampleRate)) + 2;
        buf.assign(static_cast<size_t>(bufSize), 0.0f);
        loopHP.setFreq(SPRING_LOOP_HP_HZ, sampleRate);
        reset();
    }

    void reset() {
        std::fill(buf.begin(), buf.end(), 0.0f);
        writePtr = 0;
        dispersion.reset();
        loopLP.reset();
        loopHP.reset();
    }

    void setLength(float sampleRate, float lenScale) {
        int len = static_cast<int>(std::lround(baseMs * 0.001f * sampleRate * lenScale));
        loopLen = std::clamp(len, 1, bufSize - 1);
    }

    void setTone(float sampleRate, float cutoffHz) {
        loopLP.setFreq(cutoffHz, sampleRate);
    }

    float process(float x) {
        int readPos = writePtr - loopLen;
        if (readPos < 0) readPos += bufSize;

        float read   = buf[static_cast<size_t>(readPos)];
        float disp   = dispersion.process(read);
        float damped = loopLP.process(disp);
        damped       = loopHP.process(damped);

        buf[static_cast<size_t>(writePtr)] = x + feedback * damped + ANTI_DENORMAL;
        if (++writePtr >= bufSize) writePtr = 0;

        return damped;
    }
};

struct SpringReverbCore {
    std::array<Spring, SPRING_NUM> springs;
    float sr = 44100.0f;
    float curFeedback = 0.7f;

    // detune decorrelates left vs right cores for genuine stereo width.
    void prepare(float sampleRate, float detune = 1.0f) {
        sr = sampleRate;
        const float baseMs[2] = { SPRING_LOOP1_MS, SPRING_LOOP2_MS };
        for (int i = 0; i < SPRING_NUM; ++i)
            springs[static_cast<size_t>(i)].prepare(sampleRate, baseMs[i % 2], detune);
        setSize(0.5f);
        setTone(0.5f);
    }

    void reset() {
        for (auto& s : springs) s.reset();
    }

    void setSize(float size01) {
        size01 = std::clamp(size01, 0.0f, 1.0f);
        const float lenScale = SPRING_LEN_SCALE_MIN
            + size01 * (SPRING_LEN_SCALE_MAX - SPRING_LEN_SCALE_MIN);
        curFeedback = SPRING_FB_MIN + size01 * (SPRING_FB_MAX - SPRING_FB_MIN);
        for (auto& s : springs) {
            s.setLength(sr, lenScale);
            s.feedback = curFeedback;
        }
    }

    void setTone(float tone01) {
        tone01 = std::clamp(tone01, 0.0f, 1.0f);
        const float cutoff = SPRING_TONE_LP_MIN
            + tone01 * (SPRING_TONE_LP_MAX - SPRING_TONE_LP_MIN);
        for (auto& s : springs) s.setTone(sr, cutoff);
    }

    // One mono sample in → wet sample out.
    float process(float x) {
        float wet = 0.0f;
        for (auto& s : springs) wet += s.process(x);
        return wet * (1.0f / static_cast<float>(SPRING_NUM));
    }

    float feedbackGain() const { return curFeedback; }

    float loopSeconds() const {
        int longest = 1;
        for (const auto& s : springs) longest = std::max(longest, s.loopLen);
        return static_cast<float>(longest) / sr;
    }
};

} // namespace sr
