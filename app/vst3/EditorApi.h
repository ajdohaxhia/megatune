#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Small read-only bridge so editor tests can observe the processor's tuning
// decisions without exposing the whole processor class across translation units.
namespace megatune {
struct DecisionView { float in; float target; int reason; };
int historySequence(const juce::AudioProcessor&);
DecisionView historyDecision(const juce::AudioProcessor&, int sequence);
void appendDecision(juce::AudioProcessor&, float in, float target, int reason);
int currentTargetNote(const juce::AudioProcessor&);
int smithBufferCapacity(const juce::AudioProcessor&);
void tickEditor(juce::AudioProcessorEditor&);
}
