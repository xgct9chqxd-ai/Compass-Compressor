#include "PluginEditor.h"
#include "PluginProcessor.h"

CompassCompressorAudioProcessorEditor::CompassCompressorAudioProcessorEditor (CompassCompressorAudioProcessor& p)
: juce::AudioProcessorEditor (&p), processorRef (p)
{
    setSize (480, 240); // minimal UI for Phase 0
}

void CompassCompressorAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    g.drawFittedText ("Compass Compressor (Phase 0 Skeleton)", getLocalBounds(), juce::Justification::centred, 1);
}

void CompassCompressorAudioProcessorEditor::resized()
{
}
