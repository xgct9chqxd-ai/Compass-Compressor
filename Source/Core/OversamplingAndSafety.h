// Phase 4 Step 3 — Oversampling Safety (sealed)
// - Invisible safety system: conditional 2x oversampling ONLY when risk is detected
// - Smooth engage/disengage (block-rate one-pole ramp)
// - Oversampling is used ONLY to reduce aliasing of the safety soft-clip stage
// - No parameters. No UI. No topology changes.
//
// Trigger (sealed):
//   enable if (ratio > 8:1 AND attackMs < 3.0) OR (peakAbs > 0.98)
//
// Notes:
// - Uses JUCE dsp::Oversampling polyphase IIR mode (low/near-zero latency).
// - This module runs at the end of the chain as a safety clipper + alias guard.
// - It does not widen stereo; it processes channels independently.

#pragma once
#include <JuceHeader.h>

struct OversamplingAndSafety
{
    void prepare (double sampleRate, int maxBlockSize)
    {
        sr = (sampleRate > 1.0 ? sampleRate : 48000.0);
        maxBlock = (maxBlockSize > 0 ? maxBlockSize : 1024);

        // Reset ramp
        osRamp01 = 0.0;
        osTarget01 = 0.0;

        // Lazy-create oversampler in process() because channel count can vary.
        currentChans = 0;
        os.reset();
        osBuffer.setSize(0, 0, false, false, true);
    }

    void reset()
    {
        ratio = 1.0;
        attackMs = 10.0;
        peakAbs = 0.0;

        osRamp01 = 0.0;
        osTarget01 = 0.0;

        currentChans = 0;
        os.reset();
        osBuffer.setSize(0, 0, false, false, true);
    }

    // Injection slots (NOT parameters)
    void setRatio (double r)
    {
        if (!std::isfinite(r) or r < 1.0) r = 1.0;
        ratio = r;
    }

    void setAttackMs (double ms)
    {
        if (!std::isfinite(ms) or ms < 0.05) ms = 0.05;
        if (ms > 100.0) ms = 100.0;
        attackMs = ms;
    }

    void setPeakAbs (double p)
    {
        if (!std::isfinite(p) or p < 0.0) p = 0.0;
        if (p > 10.0) p = 10.0;
        peakAbs = p;
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int chs = buffer.getNumChannels();
        const int n   = buffer.getNumSamples();
        if (chs <= 0 || n <= 0)
            return;

        // Trigger (sealed)
        const bool condAggressive = (ratio > 8.0 && attackMs < 3.0);
        const bool condSatRisk    = (peakAbs > 0.98);
        osTarget01 = (condAggressive || condSatRisk) ? 1.0 : 0.0;

        // Smooth ramp (sealed tau)
        const double tau = 0.030; // 30 ms
        const double a = std::exp(-(double)n / (tau * (sr > 1.0 ? sr : 48000.0)));
        osRamp01 = a * osRamp01 + (1.0 - a) * osTarget01;
        if (!std::isfinite(osRamp01)) osRamp01 = osTarget01;
        if (osRamp01 < 0.0) osRamp01 = 0.0;
        if (osRamp01 > 1.0) osRamp01 = 1.0;

        // If not engaged, do nothing (hard bypass)
        if (osRamp01 <= 1e-6)
            return;

        // Ensure oversampler exists + matches channel count
        ensureOversampler(chs);

        // Copy dry for crossfade
        dryBuffer.makeCopyOf(buffer, true);

        // Oversample
        juce::dsp::AudioBlock<float> block (buffer);
        auto upBlock = os->processSamplesUp(block);

        // Apply sealed safety soft-clip on oversampled audio (per-sample, per-channel)
        // Clip is gentle and only prevents overs; oversampling reduces aliasing.
        applySoftClip(upBlock);

        // Downsample back into 'buffer'
        os->processSamplesDown(block);

        // Crossfade dry vs processed
        const float gWet = (float)osRamp01;
        const float gDry = 1.0f - gWet;

        for (int ch = 0; ch < chs; ++ch)
        {
            float* w = buffer.getWritePointer(ch);
            const float* d = dryBuffer.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
                w[i] = gDry * d[i] + gWet * w[i];
        }
    }

private:
    void ensureOversampler(int chs)
    {
        if (os && chs == currentChans)
            return;

        currentChans = chs;

        // 2x oversampling (sealed)
        constexpr int kFactor = 2;
        const auto type = juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR;

        os.reset(new juce::dsp::Oversampling<float>((size_t)chs, (size_t)kFactor, type, true));
        os->initProcessing((size_t)maxBlock);

        dryBuffer.setSize(chs, maxBlock, false, false, true);
    }

    static inline float softClip(float x)
    {
        // Sealed gentle curve: tanh-based with conservative drive
        // Ensures bounded output and avoids hard corners.
        const float drive = 1.20f;
        const float y = std::tanh(drive * x) / std::tanh(drive);
        return y;
    }

    static void applySoftClip(juce::dsp::AudioBlock<float>& blk)
    {
        const int chs = (int)blk.getNumChannels();
        const int n   = (int)blk.getNumSamples();

        for (int ch = 0; ch < chs; ++ch)
        {
            auto* p = blk.getChannelPointer((size_t)ch);
            for (int i = 0; i < n; ++i)
            {
                float x = p[i];
                if (!std::isfinite(x)) x = 0.0f;
                // Only act near risky levels (sealed)
                if (std::abs(x) > 0.90f)
                    p[i] = softClip(x);
            }
        }
    }

    double sr = 48000.0;
    int    maxBlock = 1024;

    // Injected (control-only)
    double ratio    = 1.0;
    double attackMs = 10.0;
    double peakAbs  = 0.0;

    // Engage ramp
    double osTarget01 = 0.0;
    double osRamp01   = 0.0;

    int currentChans = 0;

    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> osBuffer; // reserved (unused but kept for future)
};
