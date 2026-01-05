#pragma once
#include <JuceHeader.h>

class AutoMakeupToggleComponent;

class CompassCompressorAudioProcessor;

class CompassCompressorAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit CompassCompressorAudioProcessorEditor (CompassCompressorAudioProcessor&);
    ~CompassCompressorAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;


    void mouseDown (const juce::MouseEvent&) override;
private:
    CompassCompressorAudioProcessor& processorRef;


    // Knob Look Pass v1 (threshold only): custom LookAndFeel lives in .cpp
    struct CompassKnobLookAndFeel;
    std::unique_ptr<CompassKnobLookAndFeel> knobLnf;

    // Phase 6 UI members (Surface Contract)
    juce::Slider thresholdKnob;
    juce::Slider ratioKnob;
    juce::Slider attackKnob;
    juce::Slider releaseKnob;
    juce::Slider mixKnob;
    juce::Slider outputGainKnob;

    // Toggle Pass v1: custom component owns internal ToggleButton + paints “AUTO”
    std::unique_ptr<AutoMakeupToggleComponent> autoMakeupToggleComp;


        // Meter Look Pass v1: custom GR meter component lives in .cpp
    struct GRMeterComponent;
    std::unique_ptr<GRMeterComponent> grMeter;


    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ratioAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoMakeupAttach;

        // UI_PLATE_PASS: Cached background plate (static surface)
    juce::Image backgroundPlate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassCompressorAudioProcessorEditor)
};
