#include "CabinetSim.h"
#include "IRLibrary.h"

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
    bufferTransfer.set(BufferWithSampleRate(std::move(irBuffer),
                                           reader->sampleRate));
    useIR.store(true);
}

void CabinetSim::loadIRFromBuffer(juce::AudioBuffer<float>&& buffer, double sampleRate)
{
    // Called from message thread only.
    currentIRLengthSamples = buffer.getNumSamples();
    irNativeSampleRate = sampleRate;
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
    queueConvolutionLoad(std::move(buffer), sampleRate);

    // Convolution::prepare() first runs every queued load on this thread
    // (waiting out one the background loader may be running), then builds the
    // engine for the spec and installs it directly — no pass-through, and no
    // crossfade from the previous engine.
    convolution.prepare(spec);
    useIR.store(true);
}

void CabinetSim::queueConvolutionLoad(juce::AudioBuffer<float>&& buffer, double sampleRate)
{
    convolution.loadImpulseResponse(std::move(buffer),
                                    sampleRate,
                                    juce::dsp::Convolution::Stereo::yes,
                                    juce::dsp::Convolution::Trim::yes,
                                    juce::dsp::Convolution::Normalise::yes);
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
