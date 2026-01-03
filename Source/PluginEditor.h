#pragma once
#include <JuceHeader.h>

class CompassCompressorAudioProcessor;

class CompassCompressorAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit CompassCompressorAudioProcessorEditor (CompassCompressorAudioProcessor&);
    ~CompassCompressorAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    CompassCompressorAudioProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassCompressorAudioProcessorEditor)
};
