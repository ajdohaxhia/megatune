// In-process render bench: drives the processor with a synthetic vocal and
// writes interleaved float32 stereo, so verify/bench.py can measure it.
// usage: MegaTuneBench <out.f32> [signal=vowel|sine|glide|notes|wav] [hz=210] [seconds=4]
//                      [rate=48000] [vibrato=35] [block=256] [mono=0] [<paramID>=<plain value> ...]
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
    const int block = (int) num ("block", 256), channels = num ("mono", 0) > 0.5 ? 1 : 2;
    const int n = (int) (sr * seconds);
    std::vector<float> in ((size_t) n);
    std::mt19937 rng (7); std::normal_distribution<float> noise (0.f, 1.f);
    double phase = 0, drift = 0;
    for (int i = 0; i < n; ++i)
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
        if (signal == "notes" && std::fmod (t, 0.5) > 0.42) s = 0.02f * noise (rng);             // unvoiced gap between notes
        in[(size_t) i] = s;
    }
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    for (auto* q : p->getParameters())
        if (auto* w = dynamic_cast<juce::RangedAudioParameter*> (q))
            if (args.count (w->paramID)) w->setValueNotifyingHost (w->convertTo0to1 ((float) args[w->paramID].getDoubleValue()));
    p->setPlayConfigDetails (channels, channels, sr, block);
    p->prepareToPlay (sr, block);
    juce::AudioBuffer<float> buf (channels, block); juce::MidiBuffer midi;
    std::ofstream out (argv[1], std::ios::binary); std::ofstream dry (std::string (argv[1]) + ".in", std::ios::binary);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos + block <= n; pos += block)
    {
        for (int c = 0; c < channels; ++c) for (int i = 0; i < block; ++i) buf.setSample (c, i, in[(size_t) (pos + i)]);
        p->processBlock (buf, midi);
        for (int i = 0; i < block; ++i) { const float l = buf.getSample (0, i), r = buf.getSample (channels - 1, i);
                                          out.write ((const char*) &l, 4); out.write ((const char*) &r, 4); dry.write ((const char*) &in[(size_t) (pos + i)], 4); }
    }
    const double ms = juce::Time::getMillisecondCounterHiRes() - start;
    std::cout << "latency " << p->getLatencySamples() << " realtime x" << (seconds * 1000.0 / ms) << "\n";
    return 0;
}
