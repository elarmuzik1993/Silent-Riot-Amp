#include "UserIRState.h"

#include <cmath>

namespace sr {

juce::ValueTree userIRsToValueTree(const std::vector<UserIREntry>& userIRs)
{
    juce::ValueTree irsNode("UserIRs");

    for (auto& ir : userIRs)
    {
        juce::ValueTree irNode("IR");
        irNode.setProperty("name", ir.name, nullptr);
        irNode.setProperty("sampleRate", ir.sampleRate, nullptr);
        irNode.setProperty("numChannels", ir.numChannels, nullptr);
        irNode.setProperty("numSamples", ir.numSamples, nullptr);

        // Store audio data as raw floats per channel
        juce::MemoryBlock audioBlock;
        for (int ch = 0; ch < ir.numChannels; ++ch)
            audioBlock.append(ir.audioData.getReadPointer(ch),
                              static_cast<size_t>(ir.numSamples) * sizeof(float));

        irNode.setProperty("audioData", juce::var(audioBlock), nullptr);
        irsNode.addChild(irNode, -1, nullptr);
    }

    return irsNode;
}

std::vector<UserIREntry> userIRsFromValueTree(const juce::ValueTree& irsNode)
{
    std::vector<UserIREntry> result;

    if (!irsNode.isValid() || !irsNode.hasType("UserIRs"))
        return result;

    for (int i = 0; i < irsNode.getNumChildren(); ++i)
    {
        if (static_cast<int>(result.size()) >= MAX_USER_IRS)
            break;

        auto irNode = irsNode.getChild(i);
        if (!irNode.hasType("IR"))
            continue;

        UserIREntry entry;
        entry.name = irNode.getProperty("name").toString();
        entry.sampleRate = static_cast<double>(irNode.getProperty("sampleRate"));
        entry.numChannels = static_cast<int>(irNode.getProperty("numChannels"));
        entry.numSamples = static_cast<int>(irNode.getProperty("numSamples"));

        // All fields are untrusted (state comes from DAW project files).
        // Bounds match what loadUserIR() can produce: 1–2 channels, at most
        // MAX_USER_IR_SAMPLES samples, an audio-typical sample rate.
        const bool dimsValid =
            entry.numChannels >= 1 && entry.numChannels <= 2
            && entry.numSamples >= 1 && entry.numSamples <= MAX_USER_IR_SAMPLES
            && std::isfinite(entry.sampleRate)
            && entry.sampleRate >= 8000.0 && entry.sampleRate <= 768000.0;

        const auto* audioBlob = irNode.getProperty("audioData").getBinaryData();
        const size_t needed = dimsValid
            ? static_cast<size_t>(entry.numChannels)
              * static_cast<size_t>(entry.numSamples) * sizeof(float)
            : 0;

        if (!dimsValid || audioBlob == nullptr || audioBlob->getSize() < needed)
            break;  // stop at first invalid entry — preserves IR_INDEX mapping

        entry.audioData.setSize(entry.numChannels, entry.numSamples);
        const auto* rawData = static_cast<const float*>(audioBlob->getData());
        for (int ch = 0; ch < entry.numChannels; ++ch)
            entry.audioData.copyFrom(ch, 0, rawData + ch * entry.numSamples,
                                     entry.numSamples);

        result.push_back(std::move(entry));
    }

    return result;
}

} // namespace sr
