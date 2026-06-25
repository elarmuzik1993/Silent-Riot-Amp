// ═══════════════════════════════════════════════════════════════════════════
// sr_render — headless offline render for the tone sign-off harness.
//
// Drives the REAL SilentRiotProcessor::processBlock (not a reconstructed chain —
// the audit flagged the reconstructed harness as omitting PSU/sag) over a DI or
// generated test signal at a chosen parameter set, and writes:
//
//   <out>.f32   interleaved float32 raw  (exact, for sr_analyze.py)
//   <out>.json  sidecar { sampleRate, numChannels, numFrames }
//   <out>.wav   24-bit PCM               (for listening / the ABX panel)
//
// Built only when -DSR_BUILD_TOOLS=ON (OFF by default — never affects the
// plugin build or CI). See tools/README.md.
//
//   sr_render --out OUT [--in FILE | --signal SPEC] [params...]
//
// Inputs:
//   --in FILE              WAV/AIFF, or raw float32 mono with --in-sr / --in-raw
//   --signal SPEC          generated; one of:
//                            sine:FREQ:AMP:SECONDS
//                            sweep:F0:F1:SECONDS      (log chirp, amp 0.5)
//                            pink:AMP:SECONDS
//                            silence:SECONDS
//                            step:AMP:SECONDS         (silence, then DC-ish tone burst)
//                            note:FREQ:AMP:SECONDS    (sine with 5ms attack, for sag)
// Params (displayed 0..10 for knobs, mapped to internal 0..1 here as 0..1):
//   --gain 0..1  --shape 0..1  --volume 0..1
//   --7w 0|1  --cab 0|1  --power 0|1  --os 0|1|2 (2x/4x/8x)
//   --ir FILE              load a user IR (enables IR convolution path)
//   --sr 44100  --block 512  --warmup-ms 250
// ═══════════════════════════════════════════════════════════════════════════

#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace
{
struct Args
{
    std::map<juce::String, juce::String> kv;

    juce::String str(const char* k, const juce::String& def = {}) const
    {
        auto it = kv.find(k);
        return it == kv.end() ? def : it->second;
    }
    float num(const char* k, float def) const
    {
        auto it = kv.find(k);
        return it == kv.end() ? def : it->second.getFloatValue();
    }
    bool has(const char* k) const { return kv.count(k) > 0; }
};

Args parseArgs(int argc, char** argv)
{
    Args a;
    for (int i = 1; i < argc; ++i)
    {
        juce::String tok(argv[i]);
        if (tok.startsWith("--") && i + 1 < argc)
            a.kv[tok.substring(2)] = juce::String(argv[++i]);
        else if (tok.startsWith("--"))
            a.kv[tok.substring(2)] = "1";
    }
    return a;
}

// Split "sine:220:0.5:1.0" -> ["sine","220","0.5","1.0"]
juce::StringArray spec(const juce::String& s)
{
    juce::StringArray parts;
    parts.addTokens(s, ":", "");
    return parts;
}

std::vector<float> genSignal(const juce::String& s, double sr)
{
    auto p = spec(s);
    auto kind = p[0];
    auto secs = [&](int idx, float def) { return p.size() > idx ? p[idx].getFloatValue() : def; };
    auto frames = [&](float seconds) { return static_cast<int>(seconds * sr); };
    std::vector<float> out;

    if (kind == "sine")
    {
        float f = secs(1, 220.0f), amp = secs(2, 0.5f), dur = secs(3, 1.0f);
        out.resize(frames(dur));
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = amp * std::sin(2.0 * juce::MathConstants<double>::pi * f * i / sr);
    }
    else if (kind == "note")
    {
        float f = secs(1, 220.0f), amp = secs(2, 0.5f), dur = secs(3, 1.5f);
        out.resize(frames(dur));
        int atk = static_cast<int>(0.005 * sr);
        for (size_t i = 0; i < out.size(); ++i)
        {
            float env = i < (size_t) atk ? (float) i / atk : 1.0f;
            out[i] = amp * env * std::sin(2.0 * juce::MathConstants<double>::pi * f * i / sr);
        }
    }
    else if (kind == "sweep")
    {
        float f0 = secs(1, 20.0f), f1 = secs(2, 20000.0f), dur = secs(3, 2.0f);
        out.resize(frames(dur));
        double T = dur, k = std::log(f1 / f0);
        for (size_t i = 0; i < out.size(); ++i)
        {
            double t = i / sr;
            double phase = 2.0 * juce::MathConstants<double>::pi * f0 * T / k * (std::exp(t / T * k) - 1.0);
            out[i] = 0.5f * std::sin(phase);
        }
    }
    else if (kind == "pink")
    {
        float amp = secs(1, 0.5f), dur = secs(2, 2.0f);
        out.resize(frames(dur));
        // Voss-McCartney-ish: white through a -3dB/oct filter (Paul Kellet)
        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (size_t i = 0; i < out.size(); ++i)
        {
            float w = uni(rng);
            b0 = 0.99886f * b0 + w * 0.0555179f;
            b1 = 0.99332f * b1 + w * 0.0750759f;
            b2 = 0.96900f * b2 + w * 0.1538520f;
            b3 = 0.86650f * b3 + w * 0.3104856f;
            b4 = 0.55000f * b4 + w * 0.5329522f;
            b5 = -0.7616f * b5 - w * 0.0168980f;
            float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
            b6 = w * 0.115926f;
            out[i] = amp * pink * 0.11f;
        }
    }
    else if (kind == "silence")
    {
        out.assign(frames(secs(1, 5.0f)), 0.0f);
    }
    else if (kind == "step")
    {
        float amp = secs(1, 0.5f), dur = secs(2, 1.0f);
        out.resize(frames(dur), 0.0f);
        for (size_t i = out.size() / 4; i < out.size(); ++i)
            out[i] = amp * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr);
    }
    else
    {
        std::fprintf(stderr, "unknown --signal kind '%s'\n", kind.toRawUTF8());
        std::exit(2);
    }
    return out;
}

std::vector<float> loadInput(const Args& a, double sr)
{
    auto inFile = a.str("in");
    if (a.has("in-raw"))
    {
        juce::File f(inFile);
        juce::MemoryBlock mb;
        f.loadFileAsData(mb);
        auto n = mb.getSize() / sizeof(float);
        std::vector<float> out(n);
        std::memcpy(out.data(), mb.getData(), n * sizeof(float));
        return out;
    }
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(juce::File(inFile)));
    if (!reader)
    {
        std::fprintf(stderr, "could not read input '%s'\n", inFile.toRawUTF8());
        std::exit(2);
    }
    juce::AudioBuffer<float> buf((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read(&buf, 0, (int) reader->lengthInSamples, 0, true, true);
    std::vector<float> out((size_t) buf.getNumSamples());
    for (int i = 0; i < buf.getNumSamples(); ++i)  // mono down-mix from ch0
        out[(size_t) i] = buf.getSample(0, i);
    return out;
}

void setParam(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float natural)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(natural));
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;  // MessageManager + JUCE statics
    auto a = parseArgs(argc, argv);

    if (a.str("out").isEmpty() || (a.str("in").isEmpty() && a.str("signal").isEmpty()))
    {
        std::fprintf(stderr, "usage: sr_render --out OUT (--in FILE | --signal SPEC) [params]\n");
        return 1;
    }

    const double sr = a.num("sr", 44100.0f);
    const int block = (int) a.num("block", 512.0f);

    std::vector<float> input = a.str("signal").isNotEmpty()
                                   ? genSignal(a.str("signal"), sr)
                                   : loadInput(a, sr);

    SilentRiotProcessor proc;
    proc.setPlayConfigDetails(2, 2, sr, block);
    proc.prepareToPlay(sr, block);

    // Parameters (knobs are already 0..1 internal; bools/choice mapped via convertTo0to1)
    setParam(proc.apvts, sr::Params::IDs::GAIN, a.num("gain", 0.6f));
    setParam(proc.apvts, sr::Params::IDs::TONE, a.num("shape", 0.5f));
    setParam(proc.apvts, sr::Params::IDs::VOLUME, a.num("volume", 0.5f));
    setParam(proc.apvts, sr::Params::IDs::HALF_POWER, a.num("7w", 0.0f));
    setParam(proc.apvts, sr::Params::IDs::CAB_SIM, a.num("cab", 1.0f));
    setParam(proc.apvts, sr::Params::IDs::POWER, a.num("power", 1.0f));
    setParam(proc.apvts, sr::Params::IDs::OVERSAMPLE, a.num("os", 1.0f));  // 0=2x 1=4x 2=8x

    if (a.str("ir").isNotEmpty())
        proc.loadUserIR(juce::File(a.str("ir")));  // synchronous; enables IR path

    juce::MidiBuffer midi;

    // Warm-up: let smoothers, PSU, and any oversample switch settle before the
    // measured region. The analyzer also skips a leading fraction.
    int warmup = (int) (a.num("warmup-ms", 250.0f) / 1000.0f * sr);
    {
        juce::AudioBuffer<float> wb(2, block);
        for (int done = 0; done < warmup; done += block)
        {
            wb.clear();
            proc.processBlock(wb, midi);  // process silence to settle state
        }
    }

    // Render
    const int n = (int) input.size();
    juce::AudioBuffer<float> out(2, n);
    juce::AudioBuffer<float> chunk(2, block);
    for (int pos = 0; pos < n; pos += block)
    {
        int len = std::min(block, n - pos);
        chunk.clear();
        for (int i = 0; i < len; ++i)
        {
            chunk.setSample(0, i, input[(size_t) (pos + i)]);
            chunk.setSample(1, i, input[(size_t) (pos + i)]);
        }
        juce::AudioBuffer<float> view(chunk.getArrayOfWritePointers(), 2, 0, len);
        proc.processBlock(view, midi);
        for (int i = 0; i < len; ++i)
        {
            out.setSample(0, pos + i, chunk.getSample(0, i));
            out.setSample(1, pos + i, chunk.getSample(1, i));
        }
    }

    const juce::String outBase = a.str("out");
    const int numCh = 2;

    // .f32 interleaved + .json sidecar (exact, for the analyzer)
    {
        juce::File f(outBase + ".f32");
        f.deleteFile();
        juce::FileOutputStream os(f);
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < numCh; ++c)
            {
                float s = out.getSample(c, i);
                os.write(&s, sizeof(float));
            }
    }
    {
        juce::File f(outBase + ".json");
        f.replaceWithText(juce::String("{\"sampleRate\": ") + juce::String((int) sr)
                          + ", \"numChannels\": " + juce::String(numCh)
                          + ", \"numFrames\": " + juce::String(n) + "}\n");
    }

    // .wav 24-bit for listening / ABX
    {
        juce::File f(outBase + ".wav");
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> fos(f.createOutputStream());
        if (fos)
        {
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(fos.release(), sr, (unsigned) numCh, 24, {}, 0));
            if (writer)
                writer->writeFromAudioSampleBuffer(out, 0, n);
        }
    }

    std::printf("rendered %d frames @ %g Hz -> %s.{f32,json,wav}\n", n, sr, outBase.toRawUTF8());
    return 0;
}
