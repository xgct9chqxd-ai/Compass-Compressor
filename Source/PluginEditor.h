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


    void mouseDown (const juce::MouseEvent&) override;
private:
    CompassCompressorAudioProcessor& processorRef;

    // Phase 6 UI members (Surface Contract)
    juce::Slider thresholdKnob;
    juce::Slider ratioKnob;
    juce::Slider attackKnob;
    juce::Slider releaseKnob;
    juce::Slider mixKnob;
    juce::Slider outputGainKnob;

    juce::ToggleButton autoMakeupToggle;

    juce::Slider gainReductionMeter; // configured as meter in next patch

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ratioAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoMakeupAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassCompressorAudioProcessorEditor)
};
