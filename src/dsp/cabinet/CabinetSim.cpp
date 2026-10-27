#include "CabinetSim.h"
#include "IRLibrary.h"
#include "IRLevel.h"

namespace sr {

void CabinetSim::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    currentSampleRate = sampleRate;

    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(maxBlockSize);
    spec.numChannels      = static_cast<juce::uint32>(numChannels);

    convolution.prepare(spec);
    prepared = true;

    // IR reload after prepare is handled by the processor (it holds user IR data).
}

void CabinetSim::reset()
{
    convolution.reset();
}

void CabinetSim::processBlock(juce::dsp::AudioBlock<float>& block)
{
    // Guard: skip convolution if cab sim is disabled
    if (!enabled)
        return;

    // Pick up any pending IR from the message thread (never blocks)
    bufferTransfer.get([this](BufferWithSampleRate& buf)
    {
        queueConvolutionLoad(std::move(buf.buffer), buf.sampleRate);
    });

    juce::dsp::ProcessContextReplacing<float> context(block);
    convolution.process(context);
}

void CabinetSim::setIRIndex(int newIndex)
{
    // Called from message thread only (via AsyncUpdater).
    // No allocation or lock acquisition happens on the audio thread.
    currentIRIndex = newIndex;

    if (newIndex <= 0)
    {
        // IIR mode — disable convolution path.
        // Don't call convolution.reset() here; the convolution just sits idle
        // when useIR is false. This avoids touching convolution state that could
        // race with the audio thread's convolution.process() call.
        useIR.store(false);
        currentIRLengthSamples = 0;
        irNativeSampleRate = 0.0;
        return;
    }

    if (newIndex <= NUM_STOCK_IRS)
    {
        loadStockIR(newIndex - 1); // stock indices are 0-based internally
        // Set useIR after the IR is pushed to bufferTransfer, so the audio thread
        // doesn't route through convolution before the IR is available.
        useIR.store(true);
    }
    // Indices > NUM_STOCK_IRS are user slots — useIR stays false until
    // loadIRFromFile() is called and actually loads an IR.
}

void CabinetSim::loadStockIR(int stockIndex)
{
    // Called from message thread (via setIRIndex) or from prepare().
    // Heap allocation here is safe — never called from processBlock.
    jassert(stockIndex >= 0 && stockIndex < NUM_STOCK_IRS);

    const auto& desc = getStockIRDescriptor(stockIndex);
    auto irBuffer = generateSyntheticIR(stockIndex, desc);
    currentIRLengthSamples = irBuffer.getNumSamples();
    irNativeSampleRate = static_cast<double>(desc.sampleRate);
    matchBuiltInCabLevel(irBuffer, irNativeSampleRate);

    bufferTransfer.set(BufferWithSampleRate(std::move(irBuffer),
                                           static_cast<double>(desc.sampleRate)));
}

void CabinetSim::loadIRFromFile(const juce::File& file)
{
    // Called from message thread only.
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));

    if (reader == nullptr)
        return; // unsupported format or file not found

    auto numSamples = static_cast<int>(
        std::min(reader->lengthInSamples, static_cast<juce::int64>(MAX_USER_IR_SAMPLES)));

    // Stereo::yes convolves with at most the first two channels, so only those
    // are read — and only those are checked, or a dead third channel of a
    // multichannel file would get it refused.
    const int numChannels = std::min(static_cast<int>(reader->numChannels), 2);
    juce::AudioBuffer<float> irBuffer(numChannels, numSamples);
    reader->read(&irBuffer, 0, numSamples, 0, true, true);

    if (!isLoadableIR(irBuffer))
        return; // NaN/Inf, overflowing or dead-channel IRs would silence IR mode (review S4)

    currentIRLengthSamples = numSamples;
    irNativeSampleRate = reader->sampleRate;
    matchBuiltInCabLevel(irBuffer, irNativeSampleRate);
    bufferTransfer.set(BufferWithSampleRate(std::move(irBuffer),
                                           reader->sampleRate));
    useIR.store(true);
}

void CabinetSim::loadIRFromBuffer(juce::AudioBuffer<float>&& buffer, double sampleRate)
{
    // Called from message thread only. Scaled HERE, before bufferTransfer.set():
    // processBlock only picks the buffer up and queues it.
    currentIRLengthSamples = buffer.getNumSamples();
    irNativeSampleRate = sampleRate;
    matchBuiltInCabLevel(buffer, sampleRate);
    bufferTransfer.set(BufferWithSampleRate(std::move(buffer), sampleRate));
    useIR.store(true);
}

void CabinetSim::installIRNow(juce::AudioBuffer<float>&& buffer, double sampleRate)
{
    // prepareToPlay only — see the header. A transfer still pending would be
    // picked up by the next processBlock and start a second, timing-dependent
    // load and crossfade back to the older IR, so it goes first.
    jassert(prepared);   // the convolution is prepared with the spec prepare() stored
    bufferTransfer.clear();

    currentIRLengthSamples = buffer.getNumSamples();
    irNativeSampleRate = sampleRate;
    matchBuiltInCabLevel(buffer, sampleRate);
    queueConvolutionLoad(std::move(buffer), sampleRate);

    // Convolution::prepare() first runs every queued load on this thread
    // (waiting out one the background loader may be running), then builds the
    // engine for the spec and installs it directly — no pass-through, and no
    // crossfade from the previous engine.
    convolution.prepare(spec);
    useIR.store(true);
}

void CabinetSim::matchBuiltInCabLevel(juce::AudioBuffer<float>& buffer, double sampleRate)
{
    // Message thread (or prepareToPlay): ~1M multiply-adds for an 8192-sample
    // IR. Never from processBlock — the buffer it scales is the one later
    // handed to bufferTransfer / JUCE, a copy: userIRs keeps the raw audio.
    //
    // Stereo::yes convolves with at most the first two channels, so only those
    // are measured (a file with more never gets here: loads read two).
    //
    // An IR JUCE's -80 dBFS trim would cut to nothing (restore accepts these:
    // see isAudibleOnEveryChannel) is left as it is, so it still plays the
    // silence it was saved as, instead of being raised into audibility. Any
    // other IR keeps its peak above the trim threshold after scaling. When the
    // 40 dB cap binds the gain is > 1, so the peak only rises. Otherwise, for
    // N samples, band power ≤ (N · peak)², so gain ≥ sqrt(ref) / (N · peak) and
    // the scaled peak ≥ sqrt(ref) / N: ≥ 0.90 / 8192 = 1.1e-4 at the length
    // cap (the clamped-band reference of an 8 kHz IR is larger, -0.72 dB).
    //
    // Channel-averaged, one gain: the stereo balance is kept, and a stereo
    // IR's AVERAGE meets the reference. On a mono bus JUCE convolves only IR
    // channel 0 (see isAudibleOnEveryChannel), so a stereo IR whose channels
    // differ in level plays there at channel 0's level, off the reference by
    // channel 0's distance from the average (+2.0 dB for a right channel 6 dB
    // down). A mono IR, or a stereo IR with equal channels, plays exactly on it.
    const int numChannels = std::min(buffer.getNumChannels(), 2);
    bool audible = false;
    for (int ch = 0; ch < numChannels && !audible; ++ch)
        audible = buffer.getMagnitude(ch, 0, buffer.getNumSamples()) >= IR_TRIM_THRESHOLD;
    if (!audible)
        return;

    const float* channels[2] {};
    for (int ch = 0; ch < numChannels; ++ch)
        channels[ch] = buffer.getReadPointer(ch);
    const double gain = irlevel::levelMatchGain(
        irlevel::bandPower(channels, numChannels, buffer.getNumSamples(), sampleRate), sampleRate);
    buffer.applyGain(static_cast<float>(gain));   // one factor: stereo balance kept
}

void CabinetSim::queueConvolutionLoad(juce::AudioBuffer<float>&& buffer, double sampleRate)
{
    // Normalise::no: the level is set by matchBuiltInCabLevel(), at the IR's
    // native rate, before the buffer gets here. Never pass Normalise::yes —
    // JUCE scales to 0.125/sqrt(energy) AFTER resampling to the session rate:
    // 18 dB under unit energy and +3 dB at 96 kHz vs 48 kHz. With no, JUCE
    // scales the resampled IR by nativeRate / sessionRate instead, which keeps
    // its response (and our level) the same at every rate.
    convolution.loadImpulseResponse(std::move(buffer),
                                    sampleRate,
                                    juce::dsp::Convolution::Stereo::yes,
                                    juce::dsp::Convolution::Trim::yes,
                                    juce::dsp::Convolution::Normalise::no);
}

double CabinetSim::getIRTailSeconds() const
{
    if (!useIR.load() || currentIRLengthSamples <= 0 || irNativeSampleRate <= 0.0)
        return 0.0;

    // Use the IR's native sample rate (not the plugin's session rate) because
    // currentIRLengthSamples is in the IR's native sample count.
    return static_cast<double>(currentIRLengthSamples) / irNativeSampleRate;
}

} // namespace sr
