#include "PluginProcessor.h"
#include "PluginEditor.h"


juce::AudioProcessorValueTreeState::ParameterLayout CompassCompressorAudioProcessor::createParameterLayout()
{
    using APVTS = juce::AudioProcessorValueTreeState;
    APVTS::ParameterLayout layout;

    auto thresholdRange = juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f);
    thresholdRange.setSkewForCentre (-18.0f);

    auto ratioRange = juce::NormalisableRange<float> (1.5f, 20.0f, 0.001f);
    ratioRange.setSkewForCentre (4.0f);

    auto attackRange = juce::NormalisableRange<float> (0.1f, 100.0f, 0.001f);
    attackRange.setSkewForCentre (10.0f);

    auto releaseRange = juce::NormalisableRange<float> (10.0f, 1000.0f, 0.01f);
    releaseRange.setSkewForCentre (100.0f);

    auto mixRange = juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f);

    auto outGainRange = juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f);

    layout.add (std::make_unique<juce::AudioParameterFloat> ("threshold",    "Threshold",   thresholdRange, -18.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("ratio",        "Ratio",       ratioRange,      4.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("attack",       "Attack",      attackRange,    10.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("release",      "Release",     releaseRange,  100.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("mix",          "Mix",         mixRange,     100.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("output_gain",  "Output Gain", outGainRange,   0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool>  ("auto_makeup",  "Auto-Makeup", false));

    return layout;
}

CompassCompressorAudioProcessor::CompassCompressorAudioProcessor()
: juce::AudioProcessor (
      BusesProperties()
      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)), apvts(*this, nullptr, "Parameters", createParameterLayout()) {
}

const juce::String CompassCompressorAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool CompassCompressorAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool CompassCompressorAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool CompassCompressorAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double CompassCompressorAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int CompassCompressorAudioProcessor::getNumPrograms() { return 1; }
int CompassCompressorAudioProcessor::getCurrentProgram() { return 0; }
void CompassCompressorAudioProcessor::setCurrentProgram (int) {}
const juce::String CompassCompressorAudioProcessor::getProgramName (int) { return {}; }
void CompassCompressorAudioProcessor::changeProgramName (int, const juce::String&) {}

void CompassCompressorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    pipeline.prepare(sampleRate, samplesPerBlock);
    // Phase 5: preallocate dry buffer for Mix (no allocations on audio thread)
    dryBuffer.setSize (getTotalNumOutputChannels(), samplesPerBlock, false, false, true);
}

void CompassCompressorAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool CompassCompressorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Minimal Phase 0: allow mono/stereo in==out only
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainIn != mainOut) return false;
    return (mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo());
}
#endif

void CompassCompressorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Phase 5: capture dry for    // Phase 5: post-pipeline controls (no topology change inside pipeline)
    // Mix: dry/wet crossfade in [0..1]
    const float mix01 = juce::jlimit(0.0f, 1.0f, mixPct * 0.01f);

    // Output gain (dB) + optional conservative auto-makeup (sealed)
    float makeupDb = 0.0f;
    if (autoMakeup)
    {
        // Conservative sealed heuristic: more makeup as threshold lowers and ratio rises (bounded)
        const float thrPos = juce::jlimit(0.0f, 60.0f, -thrDb);                 // 0..60
        const float rNorm  = juce::jlimit(0.0f, 1.0f, (ratio - 1.5f) / (20.0f - 1.5f)); // 0..1
        makeupDb = juce::jlimit(0.0f, 12.0f, 0.12f * thrPos * (0.35f + 0.65f * rNorm));
    }

    const float totalOutDb = outGainDb + makeupDb;
    const float outLin = juce::Decibels::decibelsToGain(totalOutDb);

    // Apply Mix + Output gain sample-accurate (no allocations)
    const int chs = buffer.getNumChannels();
    const int nSamp = buffer.getNumSamples();
    for (int ch = 0; ch < chs; ++ch)
    {
        float* w = buffer.getWritePointer(ch);
        const float* d = dryBuffer.getReadPointer(ch);
        for (int i = 0; i < nSamp; ++i)
        {
            const float wet = w[i];
            const float dry = d[i];
            const float x = dry + mix01 * (wet - dry);
            w[i] = x * outLin;
        }
    }
 
    dryBuffer.makeCopyOf (buffer, true);

    // Phase 5: read APVTS params (raw) and feed pipeline targets (pipeline handles smoothing)
    
    const float thrDb      = apvts.getRawParameterValue("threshold")->load();
    const float ratioVal   = apvts.getRawParameterValue("ratio")->load();
    const float attackMs   = apvts.getRawParameterValue("attack")->load();
    const float releaseMs  = apvts.getRawParameterValue("release")->load();
    const float mixPct     = apvts.getRawParameterValue("mix")->load();
    const float outGainDb  = apvts.getRawParameterValue("output_gain")->load();
    const bool  autoMakeup = (apvts.getRawParameterValue("auto_makeup")->load() >= 0.5f);

    pipeline.setControlTargets((double)thrDb, (double)ratioVal, (double)attackMs, (double)releaseMs);

    pipeline.process(buffer);

    juce::ScopedNoDenormals noDenormals;
    // Phase 0 pass-through: do nothing
    (void) buffer;
}

bool CompassCompressorAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* CompassCompressorAudioProcessor::createEditor()
{
    return new CompassCompressorAudioProcessorEditor (*this);
}

void CompassCompressorAudioProcessor::getStateInformation (juce::MemoryBlock&) {}
void CompassCompressorAudioProcessor::setStateInformation (const void*, int) {}

// This factory must exist for AU/VST3 builds
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CompassCompressorAudioProcessor();
}
