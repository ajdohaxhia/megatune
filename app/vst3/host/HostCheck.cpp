// Independent host check: loads the built .vst3 through JUCE's VST3 hosting
// path (a real module load, not a static link) and exercises audio + parameters.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <iostream>

static void fillSine (juce::AudioBuffer<float>& b, double rate, double hz, float amp)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (c, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz
                                                       * (double) i / rate));
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::cout << "usage: HostCheck <plugin.vst3>\n"; return 1; }

    juce::ScopedJuceInitialiser_GUI init;
    const juce::File pluginFile = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);

    if (! pluginFile.exists()) { std::cout << "MISSING " << pluginFile.getFullPathName() << "\n"; return 1; }
    std::cout << "bundle=" << pluginFile.getFullPathName() << "\n";

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    juce::OwnedArray<juce::PluginDescription> found;
    juce::AudioPluginFormat* used = nullptr;

    for (int f = 0; f < formats.getNumFormats() && found.isEmpty(); ++f)
    {
        used = formats.getFormat (f);
        used->findAllTypesForFile (found, pluginFile.getFullPathName());
    }

    if (found.isEmpty() || used == nullptr) { std::cout << "RESULT no plugin types discovered\n"; return 2; }
    std::cout << "format=" << used->getName()
              << " types=" << found.size()
              << " name=\"" << found[0]->name << "\""
              << " vendor=\"" << found[0]->manufacturerName << "\""
              << " version=\"" << found[0]->version << "\"\n";

    juce::String err;
    std::unique_ptr<juce::AudioPluginInstance> inst (
        used->createInstanceFromDescription (*found[0], 48000.0, 512, err));
    if (inst == nullptr) { std::cout << "RESULT instantiate failed: " << err << "\n"; return 3; }

    inst->setPlayConfigDetails (2, 2, 48000.0, 512);
    inst->setBusesLayout ({ juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo() });

    const int nParams = inst->getParameters().size();
    std::cout << "params=" << nParams << " latency=" << inst->getLatencySamples() << "\n";
    for (int i = 0; i < nParams; ++i)
    {
        auto* p = inst->getParameters()[i];
        juce::String id = "?";
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            id = withId->paramID;

        std::cout << "  [" << i << "] id=\"" << id << "\""
                  << " name=\"" << p->getName (64) << "\""
                  << " min=" << p->getText (0.0f, 16)
                  << " max=" << p->getText (1.0f, 16)
                  << " default=\"" << p->getText (p->getDefaultValue(), 16) << "\"\n";
    }

    std::cout << "programs=" << inst->getNumPrograms() << "\n";
    for (int i = 0; i < inst->getNumPrograms(); ++i)
        std::cout << "  program[" << i << "]=" << inst->getProgramName (i) << "\n";
    inst->prepareToPlay (48000.0, 512);
    std::cout << "latency_after_prepare=" << inst->getLatencySamples() << "\n";

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    double outEnergy = 0.0, inEnergy = 0.0;
    bool finite = true;

    for (int block = 0; block < 40; ++block)
    {
        fillSine (buffer, 48000.0, 220.0, 0.25f);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                inEnergy += std::abs (buffer.getSample (c, i));

        inst->processBlock (buffer, midi);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = buffer.getSample (c, i);
                if (! std::isfinite (v)) finite = false;
                outEnergy += std::abs (v);
            }
    }

    std::cout << "audio in=" << inEnergy << " out=" << outEnergy
              << " finite=" << (finite ? "yes" : "no") << "\n";

    // ---- effect probes measured through the hosted VST3 module ----
    auto byName = [&](const juce::String& name) -> juce::AudioProcessorParameter*
    { for (auto* p : inst->getParameters()) if (p->getName (64) == name) return p; return nullptr; };
    auto setText = [&](const juce::String& name, const juce::String& text)
    { if (auto* p = byName (name)) p->setValueNotifyingHost (p->getValueForText (text)); };
    auto setNorm = [&](const juce::String& name, float value)
    { if (auto* p = byName (name)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value)); };

    auto measure = [&](const juce::String& speedText, const juce::String& amountText,
                       const juce::String& doublerText, const juce::String& widthText,
                       double inHz, double expectedHz, int blocks, int settle, double& freqOut, double& stereoDiff) -> bool
    {
        inst->prepareToPlay (48000.0, 512);
        setText ("Speed", speedText); setText ("Amount", amountText);
        setText ("Doubler", doublerText); setText ("Width", widthText);
        setNorm ("Global Bypass", 0.0f);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer mb;
        std::vector<float> steady;
        stereoDiff = 0.0;
        double phase = 0.0;
        const double inc = 2.0 * juce::MathConstants<double>::pi * inHz / 48000.0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float v = 0.25f * (float) std::sin (phase);
                phase += inc;
                for (int c = 0; c < 2; ++c) buf.setSample (c, i, v);
            }
            inst->processBlock (buf, mb);
            if (b >= settle)
                for (int i = 0; i < 512; ++i)
                {
                    const float l = buf.getSample (0, i), r = buf.getSample (1, i);
                    if (! std::isfinite (l) || ! std::isfinite (r)) return false;
                    steady.push_back (l);
                    stereoDiff += std::abs (l - r);
                }
        }
        const int lagLo = std::max (8, (int) std::floor (48000.0 / (expectedHz * 1.5)));
        const int lagHi = (int) std::ceil (48000.0 / (expectedHz * 0.7));
        std::vector<double> score ((size_t) lagHi + 2, 0.0);
        int bestLag = 0; double best = -1;
        for (int lag = lagLo; lag <= lagHi; ++lag)
        {
            double cross = 0;
            for (size_t i = 0; i + (size_t) lag < steady.size(); ++i)
                cross += steady[i] * steady[i + (size_t) lag];
            score[(size_t) lag] = cross;
            if (cross > best) { best = cross; bestLag = lag; }
        }
        if (bestLag <= lagLo || bestLag >= lagHi) return false;
        const double denom = score[(size_t) bestLag - 1] - 2.0 * score[(size_t) bestLag] + score[(size_t) bestLag + 1];
        const double off = std::abs (denom) > 1e-12
                         ? 0.5 * (score[(size_t) bestLag - 1] - score[(size_t) bestLag + 1]) / denom : 0.0;
        freqOut = 48000.0 / (bestLag + juce::jlimit (-0.5, 0.5, off));
        return true;
    };

    auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (30); };
    auto flushOneBlock = [&] { juce::AudioBuffer<float> tmp (2, 512); tmp.clear(); juce::MidiBuffer mb; inst->processBlock (tmp, mb); pump(); };
    setNorm ("Low Latency", 1.0f); flushOneBlock(); inst->prepareToPlay (48000.0, 512); pump();
    auto* llParam = byName ("Low Latency");
    std::cout << "ll_value=" << (llParam ? llParam->getValue() : -1.0f) << "\n";
    const int latLow = inst->getLatencySamples();
    setNorm ("Low Latency", 0.0f); flushOneBlock(); inst->prepareToPlay (48000.0, 512); pump();
    const int latQuality = inst->getLatencySamples();
    std::cout << "latency_quality=" << latQuality << " latency_low=" << latLow << "\n";
    const bool latencyOk = latQuality == 2048 && latLow == 128;

    double fAuto=0,sdAuto=0,fZero=0,sdZero=0,fFast=0,sdFast=0,fSlow=0,sdSlow=0,fWide=0,sdWide=0;
    setNorm ("Key", 0.0f);
    for (auto* p : inst->getParameters()) { const auto n = p->getName (64); if (n == "A") setNorm (n, 1.0f); else if (n.length() <= 2 || n.contains ("#")) setNorm (n, 0.0f); }
    flushOneBlock();
    setNorm ("Low Latency", 1.0f); flushOneBlock();
    double fLowNote = 0, sdLowNote = 0;
    const bool okLow = measure ("40","100","0","0",200,220,200,160,fLowNote,sdLowNote);
    std::cout << "probe_low_latency_200Hz=" << fLowNote << "Hz ok=" << okLow << " target A3 220\n";
    setNorm ("Low Latency", 0.0f); flushOneBlock();
    double fLowNoteQ = 0, sdLowNoteQ = 0;
    const bool okLowQ = measure ("40","100","0","0",200,220,200,160,fLowNoteQ,sdLowNoteQ);
    std::cout << "probe_quality_200Hz=" << fLowNoteQ << "Hz ok=" << okLowQ << " target A3 220\n";
    const bool probes = measure("25","100","0","0",432,440,200,160,fAuto,sdAuto)
                     && measure("25","0","0","0",432,432,200,160,fZero,sdZero)
                     && measure("-3","100","0","0",432,440,200,160,fFast,sdFast)
                     && measure("120","100","0","0",432,440,200,160,fSlow,sdSlow)
                     && measure("25","100","65","100",432,440,200,160,fWide,sdWide);
    if (probes)
    {
        std::cout << "probe_autotune=" << fAuto << "Hz (432 in), amount0=" << fZero << "Hz\n";
        std::cout << "probe_negative_speed_steady=" << fFast << "Hz, slow_speed_steady=" << fSlow << "Hz\n";
        std::cout << "probe_doubler_stereo_diff=" << sdWide << ", dry_stereo_diff=" << sdZero << "\n";
    }
    const bool autotuneOk = probes && std::abs (fAuto - 440.0) < 4.0 && std::abs (fZero - 432.0) < 4.0;
    const bool speedOk    = probes && std::abs (fFast - 440.0) < 2.0 && std::abs (fSlow - 440.0) < 2.0;
    const bool doublerOk  = probes && sdWide > 10.0 * sdZero + 1.0;
    const bool ok = finite && outEnergy > 0.0 && nParams >= 8 && autotuneOk && speedOk && doublerOk && latencyOk;
    std::cout << "RESULT " << (ok ? "PASS" : "FAIL") << "\n";

    if (auto* ed = inst->createEditorIfNeeded())
    {
        // A hosted VST3 editor draws through its native IPlugView, so an
        // offscreen JUCE snapshot is blank; only its creation/size is verified.
        const bool sized = ed->getWidth() > 0 && ed->getHeight() > 0;
        std::cout << "editor=" << ed->getWidth() << "x" << ed->getHeight()
                  << " created=" << (sized ? "yes" : "no") << "\n";
        inst->editorBeingDeleted (ed);
        delete ed;
        if (! sized) { std::cout << "RESULT FAIL\n"; return 5; }
    }
    else { std::cout << "RESULT editor unavailable\n"; return 6; }

    inst->releaseResources();
    return ok ? 0 : 4;
}
