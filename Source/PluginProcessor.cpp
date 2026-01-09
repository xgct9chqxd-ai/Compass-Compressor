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
      .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
      .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
      .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)), apvts(*this, nullptr, "Parameters", createParameterLayout()) {
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
    // Main I/O: mono or stereo, input must match output
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainIn != mainOut) return false;

    const bool mainOk = (mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo());
    if (!mainOk) return false;

    // Sidechain (aux input, bus index 1): allow disabled/mono/stereo
    // If host doesn't provide the aux bus (or JUCE reports only one input bus), accept mainOk.
    if ((int) layouts.inputBuses.size() < 2)
        return true;

    const auto sc = layouts.getChannelSet(true, 1);
    if (sc.isDisabled())
        return true;

    const bool scOk = (sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo());
    return scOk;
}
#endif

void CompassCompressorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Phase 5: read APVTS params (raw)
    const float thrDb      = apvts.getRawParameterValue("threshold")->load();
    const float ratioVal   = apvts.getRawParameterValue("ratio")->load();
    const float attackMs   = apvts.getRawParameterValue("attack")->load();
    const float releaseMs  = apvts.getRawParameterValue("release")->load();
    const float mixPct     = apvts.getRawParameterValue("mix")->load();
    const float outGainDb  = apvts.getRawParameterValue("output_gain")->load();
    const bool  autoMakeup = (apvts.getRawParameterValue("auto_makeup")->load() >= 0.5f);

    // Mix: dry/wet crossfade in [0..1]
    const float mix01 = juce::jlimit(0.0f, 1.0f, mixPct * 0.01f);

    // Phase 5: capture dry pre-process only when needed (reduces realtime load)
    // Always operate on MAIN bus view (bus 0). When sidechain is enabled, 'buffer' may be stacked.
    auto mainAudio = getBusBuffer (buffer, true, 0);

    bool dryValid = false;
    if (mix01 < 0.999f)
    {
        const int nMain = mainAudio.getNumSamples();
        if (nMain <= dryBuffer.getNumSamples() && dryBuffer.getNumChannels() >= mainAudio.getNumChannels())
        {
            for (int ch = 0; ch < mainAudio.getNumChannels(); ++ch)
                dryBuffer.copyFrom (ch, 0, mainAudio, ch, 0, nMain);
            dryValid = true;
        }
        // Never resize/allocate here (audio thread). If block size exceeds prealloc, fail-soft.
    }

    // Feed pipeline targets (pipeline handles smoothing)
    pipeline.setControlTargets((double)thrDb, (double)ratioVal, (double)attackMs, (double)releaseMs);

    // Run core DSP (sidechain-capable)
    // Audio path is always 'mainAudio' (bus 0 view). Detector key uses sidechain bus (bus 1) only when enabled *and usable*.
    bool usedExternalSC = false;

    if (getBusCount (true) > 1)
    {
        if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
        {
            // getBusBuffer returns a lightweight view — take by value to avoid lifetime/reference issues.
            auto detectorIn = getBusBuffer (buffer, true, 1);

            // Some hosts can report SC enabled while providing an empty/invalid view.
            if (detectorIn.getNumChannels() > 0 && detectorIn.getNumSamples() == mainAudio.getNumSamples())
            {
                // If SC is enabled but effectively silent, fall back to internal detector
                // to avoid "dead GR" when user forgot to route SC.
                constexpr float scEps = 1.0e-5f;
                float scMag = detectorIn.getMagnitude (0, 0, detectorIn.getNumSamples());
                if (detectorIn.getNumChannels() > 1)
                    scMag = juce::jmax (scMag, detectorIn.getMagnitude (1, 0, detectorIn.getNumSamples()));

                if (scMag > scEps)
                {
                    pipeline.process (mainAudio, detectorIn);
                    usedExternalSC = true;
                }
            }
        }
    }

    if (! usedExternalSC)
        pipeline.process (mainAudio);

    // UI GR meter tap: pipeline GR is positive dB; UI meter expects negative dB (0..-24)
    const float grDbPos = (float) pipeline.getMeterGainReductionDb();
    const float grDbNeg = -juce::jlimit (0.0f, 60.0f, (std::isfinite(grDbPos) ? grDbPos : 0.0f));
    grMeterDb.store (grDbNeg, std::memory_order_relaxed);

    // Phase 5: post-pipeline controls (no topology change inside pipeline)

    // Output gain (dB) + optional conservative auto-makeup (sealed)
    float makeupDb = 0.0f;
    if (autoMakeup)
    {
        // Conservative sealed heuristic: more makeup as threshold lowers and ratio rises (bounded)
        const float thrPos = juce::jlimit(0.0f, 60.0f, -thrDb);                           // 0..60
        const float rNorm  = juce::jlimit(0.0f, 1.0f, (ratioVal - 1.5f) / (20.0f - 1.5f)); // 0..1
        makeupDb = juce::jlimit(0.0f, 12.0f, 0.12f * thrPos * (0.35f + 0.65f * rNorm));
    }

    const float totalOutDb = outGainDb + makeupDb;
    const float outLin = juce::Decibels::decibelsToGain(totalOutDb);

    // Apply Mix + Output gain sample-accurate (no allocations)
    const int chs = mainAudio.getNumChannels();
    const int nSamp = mainAudio.getNumSamples();

    // Fast paths to avoid unnecessary work + stale dry reads when mix is effectively 100% wet
    // If dry couldn't be captured safely (no allocations), fail-soft to fully-wet.
    if (mix01 >= 0.999f || ! dryValid)
    {
        mainAudio.applyGain(outLin);
        return;
    }

    for (int ch = 0; ch < chs; ++ch)
    {
        float* w = mainAudio.getWritePointer(ch);
        const float* d = dryBuffer.getReadPointer(ch);
        for (int i = 0; i < nSamp; ++i)
        {
            const float wet = w[i];
            const float dry = d[i];
            const float x = dry + mix01 * (wet - dry);
            w[i] = x * outLin;
        }
    }
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
