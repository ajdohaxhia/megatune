// In-process render bench: drives the processor with a synthetic vocal and
// writes interleaved float32 stereo, so verify/bench.py can measure it.
// usage: MegaTuneBench <out.f32> [signal=vowel|sine|glide|notes|noise|breathy] [hz=210] [seconds=4]
//                      [rate=48000] [vibrato=35] [block=256] [varblock=0] [mono=0] [file=<mono float32 recording>]
//                      [<paramID>=<plain value> ...]
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <vector>
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
int main (int argc, char** argv)
{
    if (argc < 2) return 2;
    juce::ScopedJuceInitialiser_GUI gui;
    std::map<juce::String, juce::String> args;
    for (int i = 2; i < argc; ++i) { const juce::String a (argv[i]); args[a.upToFirstOccurrenceOf ("=", false, false)] = a.fromFirstOccurrenceOf ("=", false, false); }
    auto num = [&] (const char* k, double d) { return args.count (k) ? args[k].getDoubleValue() : d; };
    const juce::String signal = args.count ("signal") ? args["signal"] : juce::String ("vowel");
    const double sr = num ("rate", 48000), hz = num ("hz", 210), seconds = num ("seconds", 4), vib = num ("vibrato", 35);
    const int maxBlock = (int) num ("block", 256), channels = num ("mono", 0) > 0.5 ? 1 : 2;
    // file=<path>: use a mono float32 recording instead of a synthetic signal (rate=<its sample rate>)
    std::vector<float> recording;
    if (args.count ("file"))
    {
        std::ifstream f (args["file"].toStdString(), std::ios::binary | std::ios::ate);
        const auto bytes = (size_t) f.tellg(); f.seekg (0);
        recording.resize (bytes / 4); f.read ((char*) recording.data(), (std::streamsize) (recording.size() * 4));
    }
    const int n = recording.empty() ? (int) (sr * seconds) : (int) recording.size();
    std::vector<float> in ((size_t) n);
    std::mt19937 rng (7); std::normal_distribution<float> noise (0.f, 1.f);
    double phase = 0, drift = 0;
    if (! recording.empty()) in = recording;
    else for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        double f0 = hz;
        if (signal == "glide") f0 = hz * std::pow (2.0, (t / seconds) * 4.0 / 12.0);            // +4 st portamento
        if (signal == "notes") { static const double st[] = { -0.3, 2.25, 3.8, 7.3, 5.2, 1.7 };  // off-pitch steps
                                 f0 = hz * std::pow (2.0, st[(int) (t / 0.5) % 6] / 12.0); }
        drift = 0.9995 * drift + 0.0005 * noise (rng) * 40.0;                                    // slow random drift (cents)
        const double cents = (signal == "sine" ? 0.0 : vib * std::sin (2 * juce::MathConstants<double>::pi * 5.5 * t) + drift);
        f0 *= std::pow (2.0, cents / 1200.0);
        phase += 2 * juce::MathConstants<double>::pi * f0 / sr;
        double v = 0;
        if (signal == "sine") v = 2.0 * std::sin (phase);
        else for (int k = 1; k <= 40; ++k)
        {
            const double fk = f0 * k; if (fk > 9000) break;
            const double a = 1.0 / (1.0 + std::pow ((fk - 700.0) / 130.0, 2)) + 0.7 / (1.0 + std::pow ((fk - 1200.0) / 180.0, 2))
                           + 0.25 / (1.0 + std::pow ((fk - 2600.0) / 250.0, 2)) + 0.02;
            v += a * std::sin (phase * k + 0.7 * k);
        }
        float s = (float) (0.12 * v) + 0.002f * noise (rng);
        if (signal == "noise") s = 0.1f * noise (rng);                                           // unvoiced: must pass through untouched
        if (signal == "breathy") s = (float) (0.12 * v) + 0.03f * noise (rng);                   // voiced with heavy aspiration
        if (signal == "notes" && std::fmod (t, 0.5) > 0.42) s = 0.02f * noise (rng);             // unvoiced gap between notes
        in[(size_t) i] = s;
    }
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    for (auto* q : p->getParameters())
        if (auto* w = dynamic_cast<juce::RangedAudioParameter*> (q))
            if (args.count (w->paramID)) w->setValueNotifyingHost (w->convertTo0to1 ((float) args[w->paramID].getDoubleValue()));
    // varblock=1: every call gets a different size up to `block`, as FL Studio and some other hosts do
    const bool varying = num ("varblock", 0) > 0.5; std::mt19937 sizes (3);
    p->setPlayConfigDetails (channels, channels, sr, maxBlock);
    p->prepareToPlay (sr, maxBlock);
    juce::MidiBuffer midi;
    std::ofstream out (argv[1], std::ios::binary); std::ofstream dry (std::string (argv[1]) + ".in", std::ios::binary);
    double worstBlock = 0;   // slowest single call, as a fraction of the time that block represents
    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0, block = maxBlock; pos + maxBlock <= n; pos += block)
    {
        block = varying ? 1 + (int) (sizes() % (unsigned) maxBlock) : maxBlock;
        juce::AudioBuffer<float> buf (channels, block);
        for (int c = 0; c < channels; ++c) for (int i = 0; i < block; ++i) buf.setSample (c, i, in[(size_t) (pos + i)]);
        const auto blockStart = juce::Time::getMillisecondCounterHiRes();
        p->processBlock (buf, midi);
        worstBlock = std::max (worstBlock, (juce::Time::getMillisecondCounterHiRes() - blockStart) / (block * 1000.0 / sr));
        for (int i = 0; i < block; ++i) { const float l = buf.getSample (0, i), r = buf.getSample (channels - 1, i);
                                          out.write ((const char*) &l, 4); out.write ((const char*) &r, 4); dry.write ((const char*) &in[(size_t) (pos + i)], 4); }
    }
    const double ms = juce::Time::getMillisecondCounterHiRes() - start;
    std::cout << "latency " << p->getLatencySamples() << " realtime x" << (n / sr * 1000.0 / ms) << " worst_block_load " << worstBlock << "\n";
    return 0;
}
