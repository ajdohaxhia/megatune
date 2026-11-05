// Renders a WAV through the packaged VST3, and measures real latency.
// usage: MegaTuneRender <plugin.vst3> <in.wav> <out.wav> <shift> <lowLatency 0|1>
//        MegaTuneRender --latency <plugin.vst3> <sampleRate>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <iostream>
#include <vector>

static juce::AudioPluginFormat* loadFormat (juce::AudioPluginFormatManager& fm,
                                            const juce::File& file,
                                            std::unique_ptr<juce::AudioPluginInstance>& out)
{
    juce::OwnedArray<juce::PluginDescription> found;
    juce::AudioPluginFormat* used = nullptr;
    for (int i = 0; i < fm.getNumFormats() && found.isEmpty(); ++i)
    {
        used = fm.getFormat (i);
        used->findAllTypesForFile (found, file.getFullPathName());
    }
    if (found.isEmpty() || used == nullptr) return nullptr;
    juce::String err;
    out = used->createInstanceFromDescription (*found[0], 48000.0, 512, err);
    return out != nullptr ? used : nullptr;
}

static juce::AudioProcessorParameter* byName (juce::AudioProcessor& p, const juce::String& n)
{
    for (auto* param : p.getParameters())
        if (param->getName (64) == n) return param;
    return nullptr;
}

static void setText (juce::AudioProcessor& p, const juce::String& n, const juce::String& v)
{
    if (auto* param = byName (p, n)) param->setValueNotifyingHost (param->getValueForText (v));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc >= 4 && juce::String (argv[1]) == "--latency")
    {
        const juce::File plugin (argv[2]);
        const double sr = juce::String (argv[3]).getDoubleValue();
        const bool lowMode = argc >= 5 && juce::String (argv[4]).getIntValue() != 0;
        const bool smithMode = argc >= 6 && juce::String (argv[5]).getIntValue() != 0;
        juce::AudioPluginFormatManager fm;
        juce::addDefaultFormatsToManager (fm);
        std::unique_ptr<juce::AudioPluginInstance> inst;
        if (loadFormat (fm, plugin, inst) == nullptr) { std::cout << "RESULT load_failed\n"; return 2; }

        inst->setPlayConfigDetails (2, 2, sr, 512);
        inst->setBusesLayout ({ juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo() });
        if (auto* p = byName (*inst, "Low Latency")) p->setValueNotifyingHost (lowMode ? 1.0f : 0.0f);
        if (auto* p = byName (*inst, "Signalsmith")) p->setValueNotifyingHost (smithMode ? 1.0f : 0.0f);
        { juce::AudioBuffer<float> flush (2, 512); flush.clear(); juce::MidiBuffer fm; inst->processBlock (flush, fm); }
        inst->prepareToPlay (sr, 512);
        const int reported = inst->getLatencySamples();

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        int firstEnergy = -1;
        for (int b = 0; b < 64 && firstEnergy < 0; ++b)
        {
            buf.clear();
            if (b == 0) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); }
            inst->processBlock (buf, midi);
            for (int i = 0; i < 512; ++i)
                if (std::abs (buf.getSample (0, i)) > 1.0e-4f) { firstEnergy = b * 512 + i; break; }
        }
        // capture the first block's raw samples for the impulse run
        juce::AudioBuffer<float> first (2, 512); first.clear();
        first.setSample (0, 0, 1.0f); first.setSample (1, 0, 1.0f);
        inst->processBlock (first, midi);
        std::cout << "impulse_first8:";
        for (int i = 0; i < 8; ++i) std::cout << " " << first.getSample (0, i);
        std::cout << "\n";

        // all-zero input run on the same instance
        juce::AudioBuffer<float> zero (2, 512);
        float zeroMax = 0.0f;
        for (int b = 0; b < 8; ++b)
        {
            zero.clear();
            inst->processBlock (zero, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i) zeroMax = juce::jmax (zeroMax, std::abs (zero.getSample (c, i)));
        }
        std::cout << "sample_rate=" << sr << " mode=" << (smithMode ? "signalsmith" : (lowMode ? "low" : "quality")) << " reported=" << reported << " after=" << inst->getLatencySamples()
                  << " onset=" << firstEnergy << " zero_run_max=" << zeroMax
                  << " ms_reported=" << (reported * 1000.0 / sr) << "\n";
        inst->releaseResources();
        return 0;
    }

    if (argc < 6) { std::cout << "usage: MegaTuneRender <plugin.vst3> <in.wav> <out.wav> <shift> <lowLatency 0|1>\n"; return 1; }

    const juce::File plugin (argv[1]), inFile (argv[2]), outFile (argv[3]);
    const float shift = juce::String (argv[4]).getFloatValue();
    const bool lowLatency = juce::String (argv[5]).getIntValue() != 0;
    const bool usePsola = argc >= 7 && juce::String (argv[6]).getIntValue() != 0;
    const bool useSmith = argc >= 9 && juce::String (argv[8]).getIntValue() != 0;
    const bool smithComp = argc < 10 || juce::String (argv[9]).getIntValue() != 0;
    const int muteNote = argc >= 11 ? juce::String (argv[10]).getIntValue() : -1;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr) { std::cout << "RESULT read_failed\n"; return 2; }

    const int numSamples = (int) reader->lengthInSamples;
    const int numChannels = juce::jmin (2, (int) reader->numChannels);
    const double sr = reader->sampleRate;
    juce::AudioBuffer<float> input (juce::jmax (1, numChannels), numSamples);
    reader->read (&input, 0, numSamples, 0, true, numChannels > 1);

    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager (fm);
    std::unique_ptr<juce::AudioPluginInstance> inst;
    if (loadFormat (fm, plugin, inst) == nullptr) { std::cout << "RESULT load_failed\n"; return 2; }

    inst->setPlayConfigDetails (numChannels, numChannels, sr, 512);
    inst->setBusesLayout ({ numChannels > 1 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono(),
                            numChannels > 1 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono() });

    const bool onlyA = argc >= 8 && juce::String (argv[7]) == "A";
    for (int n = 0; n < 12; ++n)
        if (auto* note = byName (*inst, juce::StringArray{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}[n]))
            note->setValueNotifyingHost (onlyA ? (n == 9 ? 1.0f : 0.0f) : (n == muteNote ? 0.0f : 1.0f));
    if (auto* k = byName (*inst, "Key")) k->setValueNotifyingHost (0.0f);
    setText (*inst, "Shift", juce::String (shift));
    setText (*inst, "Low Latency", lowLatency ? "On" : "Off");
    if (auto* e = byName (*inst, "TD-PSOLA")) e->setValueNotifyingHost (usePsola ? 1.0f : 0.0f);
    if (auto* e = byName (*inst, "Signalsmith")) e->setValueNotifyingHost (useSmith ? 1.0f : 0.0f);
    if (auto* e = byName (*inst, "Smith Comp")) e->setValueNotifyingHost (smithComp ? 1.0f : 0.0f);
    setText (*inst, "Amount", "100");
    setText (*inst, "Doubler", "0");
    setText (*inst, "Width", "0");

    { juce::AudioBuffer<float> flush (numChannels, 512); flush.clear(); juce::MidiBuffer fm; inst->processBlock (flush, fm); }
    inst->prepareToPlay (sr, 512);
    const int reported = inst->getLatencySamples();

    juce::AudioBuffer<float> output (numChannels, numSamples);
    juce::AudioBuffer<float> block (numChannels, 512);
    juce::MidiBuffer midi;
    int pos = 0;
    while (pos < numSamples)
    {
        const int n = juce::jmin (512, numSamples - pos);
        block.clear();
        for (int c = 0; c < numChannels; ++c)
            block.copyFrom (c, 0, input, c, pos, n);
        inst->processBlock (block, midi);
        for (int c = 0; c < numChannels; ++c)
            output.copyFrom (c, pos, block, c, 0, n);
        pos += n;
    }
    inst->releaseResources();

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (new juce::FileOutputStream (outFile), sr, (unsigned int) numChannels, 24, {}, 0));
    if (writer == nullptr) { std::cout << "RESULT write_failed\n"; return 3; }
    writer->writeFromAudioSampleBuffer (output, 0, numSamples);
    writer.reset();

    double peak = 0.0;
    for (int c = 0; c < numChannels; ++c) peak = juce::jmax (peak, (double) output.getMagnitude (c, 0, numSamples));
    std::cout << "rendered=" << outFile.getFullPathName() << " rate=" << sr
              << " samples=" << numSamples << " shift=" << shift
              << " low_latency=" << (lowLatency ? 1 : 0)
              << " reported_latency=" << reported << " peak=" << peak << "\n";
    return 0;
}
