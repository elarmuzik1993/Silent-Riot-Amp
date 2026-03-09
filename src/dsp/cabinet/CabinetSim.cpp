#include "CabinetSim.h"
#include "IRLibrary.h"

namespace tt {

void CabinetSim::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(maxBlockSize);
    spec.numChannels      = static_cast<juce::uint32>(numChannels);

    convolution.prepare(spec);

    // Reload current IR (if any) after prepare — convolution resets internal state
    if (useIR && currentIRIndex > 0)
    {
        if (currentIRIndex <= NUM_STOCK_IRS)
            loadStockIR(currentIRIndex - 1);
        // User IRs (index > NUM_STOCK_IRS) would need a stored file path to reload;
        // for now they simply reset and the host must re-set the IR.
    }
}

void CabinetSim::reset()
{
    convolution.reset();
}

void CabinetSim::processBlock(juce::dsp::AudioBlock<float>& block)
{
    // Pick up any pending IR from the message thread (never blocks)
    bufferTransfer.get([this](BufferWithSampleRate& buf)
    {
        convolution.loadImpulseResponse(std::move(buf.buffer),
                                        buf.sampleRate,
                                        juce::dsp::Convolution::Stereo::yes,
                                        juce::dsp::Convolution::Trim::yes,
                                        juce::dsp::Convolution::Normalise::yes);
    });

    juce::dsp::ProcessContextReplacing<float> context(block);
    convolution.process(context);
}

void CabinetSim::setIRIndex(int newIndex)
{
    currentIRIndex = newIndex;

    if (newIndex <= 0)
    {
        // IIR mode — disable convolution path
        useIR = false;
        currentIRLengthSamples = 0;
        convolution.reset();
        return;
    }

    useIR = true;

    if (newIndex <= NUM_STOCK_IRS)
    {
        loadStockIR(newIndex - 1); // stock indices are 0-based internally
    }
    // Indices > NUM_STOCK_IRS are user slots — loadIRFromFile() must be called separately
}

void CabinetSim::loadStockIR(int stockIndex)
{
    jassert(stockIndex >= 0 && stockIndex < NUM_STOCK_IRS);

    const auto& desc = getStockIRDescriptor(stockIndex);
    auto irBuffer = generateSyntheticIR(desc);
    currentIRLengthSamples = irBuffer.getNumSamples();

    bufferTransfer.set(BufferWithSampleRate(std::move(irBuffer),
                                           static_cast<double>(desc.sampleRate)));
}

void CabinetSim::loadIRFromFile(const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));

    if (reader == nullptr)
        return; // unsupported format or file not found

    auto numSamples = static_cast<int>(
        std::min(reader->lengthInSamples, static_cast<juce::int64>(MAX_USER_IR_SAMPLES)));

    juce::AudioBuffer<float> irBuffer(static_cast<int>(reader->numChannels), numSamples);
    reader->read(&irBuffer, 0, numSamples, 0, true, true);

    currentIRLengthSamples = numSamples;
    bufferTransfer.set(BufferWithSampleRate(std::move(irBuffer),
                                           reader->sampleRate));
}

double CabinetSim::getIRTailSeconds() const
{
    if (!useIR || currentIRLengthSamples <= 0 || currentSampleRate <= 0.0)
        return 0.0;

    return static_cast<double>(currentIRLengthSamples) / currentSampleRate;
}

} // namespace tt
