#include "UserIRState.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace sr {

bool isValidUserIRFormat(int numChannels, int numSamples, double sampleRate)
{
    return numChannels >= 1 && numChannels <= 2
        && numSamples >= 1 && numSamples <= MAX_USER_IR_SAMPLES
        && std::isfinite(sampleRate)
        && sampleRate >= MIN_USER_IR_SAMPLE_RATE
        && sampleRate <= MAX_USER_IR_SAMPLE_RATE;
}

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

        // The writer emits only "IR" children, so anything else is corruption —
        // most plausibly an IR node whose type was mangled. Skipping it would
        // shift every later IR down one slot, and IR_INDEX would play the wrong
        // IR with no sign of it; stopping leaves those indices past the list,
        // which applyUserIRIndex() turns into IIR mode. Same rule as below.
        auto irNode = irsNode.getChild(i);
        if (!irNode.hasType("IR"))
            break;

        UserIREntry entry;
        entry.name = irNode.getProperty("name").toString();
        entry.sampleRate = static_cast<double>(irNode.getProperty("sampleRate"));
        entry.numChannels = static_cast<int>(irNode.getProperty("numChannels"));
        entry.numSamples = static_cast<int>(irNode.getProperty("numSamples"));

        // All fields are untrusted (state comes from DAW project files).
        // isValidUserIRFormat() is the same gate loadUserIR() applies, so every
        // restorable entry's FORMAT could also have been loaded fresh, and vice
        // versa. (The sample checks differ on purpose — see below.)
        const bool dimsValid =
            isValidUserIRFormat(entry.numChannels, entry.numSamples, entry.sampleRate);

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

        // A NaN/Inf, or samples whose energy overflows JUCE's normalisation,
        // would poison or silence IR mode and be re-saved with the project
        // (review S4). Same stop-at-first-invalid rule as above.
        //
        // Only the SAFETY check, not the load-time audibility one: a silent
        // IR (or one with a dead channel) is harmless — it plays silence, as
        // it did when it was saved — and refusing it here would drop it and
        // every later slot, which the next save then deletes for good.
        if (!isSafeToNormaliseIR(entry.audioData))
            break;

        result.push_back(std::move(entry));
    }

    return result;
}

bool readUserIRFromFile(const juce::File& file, UserIREntry& out)
{
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(file));
    if (!reader) return false;

    const int numSamples = static_cast<int>(
        std::min(reader->lengthInSamples, static_cast<juce::int64>(MAX_USER_IR_SAMPLES)));

    // Convolution uses at most 2 channels (Stereo::yes); reading more would
    // only bloat the saved state — and the restore path rejects >2 channels.
    const int numChannels = std::min(static_cast<int>(reader->numChannels), 2);

    // Reject formats the restore path can't accept (e.g. an out-of-band sample
    // rate) up front — otherwise the IR would load now but be silently dropped
    // on the next project reload, taking every user IR after it with it.
    if (!isValidUserIRFormat(numChannels, numSamples, reader->sampleRate))
        return false;

    out.name = file.getFileNameWithoutExtension();
    out.sampleRate = reader->sampleRate;
    out.numChannels = numChannels;
    out.numSamples = numSamples;
    out.audioData.setSize(numChannels, numSamples);
    if (!reader->read(&out.audioData, 0, numSamples, 0, true, true))
        return false;

    // Float WAVs can carry NaN/Inf or absurdly large samples (the restore path
    // would drop such an entry anyway), and any file can be silent, or silent
    // in one channel, below JUCE's -80 dBFS trim — which plays silence on that
    // channel's output (on a mono bus, a dead channel 0 silences the amp).
    // Stricter than restore on purpose: refusing a load loses nothing (review S4).
    return isLoadableIR(out.audioData);
}

bool applyUserIRIndex(CabinetSim& cab, const std::vector<UserIREntry>& irs, int irIndex,
                      IRLoad load)
{
    // IR_INDEX maps directly onto user IR slots. The stock-IR table in
    // CabinetSim is intentionally NOT reachable from this parameter today —
    // the loader UI only populates user IRs, and the stock entries are
    // synthetic placeholders (see ROADMAP.md "Ship real cabinet IRs").
    const int userIdx = irIndex - 1;
    if (userIdx < 0 || userIdx >= static_cast<int>(irs.size()))
    {
        cab.setIRIndex(0);
        return false;
    }

    const auto& ir = irs[static_cast<size_t>(userIdx)];
    juce::AudioBuffer<float> bufCopy(ir.audioData);
    if (load == IRLoad::Now)
        cab.installIRNow(std::move(bufCopy), ir.sampleRate);
    else
        cab.loadIRFromBuffer(std::move(bufCopy), ir.sampleRate);
    return true;
}

} // namespace sr
