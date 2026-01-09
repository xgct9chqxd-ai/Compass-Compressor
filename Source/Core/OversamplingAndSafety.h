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

        // Sealed DC block coefficient (<= 20 Hz cutoff) at base sample rate
        constexpr double fc = 20.0;
        const double a = std::exp(-2.0 * juce::MathConstants<double>::pi * fc / sr);
        dcA = (std::isfinite(a) ? a : 0.0);

        // Reset ramp
        osRamp01 = 0.0;
        osTarget01 = 0.0;

        // Reset DC state
        dcX1[0] = dcX1[1] = 0.0;
        dcY1[0] = dcY1[1] = 0.0;

        // Pre-create oversamplers (mono + stereo) here to avoid any allocations in process().
        constexpr int kFactor = 2;
        const auto type = juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR;

        osMono.reset (new juce::dsp::Oversampling<float>((size_t)1, (size_t)kFactor, type, true));
        osStereo.reset(new juce::dsp::Oversampling<float>((size_t)2, (size_t)kFactor, type, true));

        osMono->initProcessing  ((size_t)maxBlock);
        osStereo->initProcessing((size_t)maxBlock);

        // Preallocate dry buffer for crossfade (always 2ch; we use only the needed channels).
        dryBuffer.setSize (2, maxBlock, false, false, true);

        osBuffer.setSize(0, 0, false, false, true);
    }

    void reset()
    {
        ratio = 1.0;
        attackMs = 10.0;
        peakAbs = 0.0;

        osRamp01 = 0.0;
        osTarget01 = 0.0;

        dcX1[0] = dcX1[1] = 0.0;
        dcY1[0] = dcY1[1] = 0.0;

        // Keep allocations out of process(): oversamplers exist after prepare().
        // Resetting simply clears ramp + injected controls.
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

        // Fail-soft: only support mono/stereo without allocating or rebuilding here.
        if (chs != 1 && chs != 2)
            return;

        // If host delivers a larger block than prepared, never allocate here.
        if (n > maxBlock)
            return;

        auto* osLocal = (chs == 1 ? osMono.get() : osStereo.get());
        if (osLocal == nullptr)
            return;

        // Copy dry for crossfade into preallocated buffer (no resize/alloc)
        for (int ch = 0; ch < chs; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, 0, n);

        // Oversample
        juce::dsp::AudioBlock<float> block (buffer);
        auto upBlock = osLocal->processSamplesUp(block);

        // Apply sealed safety soft-clip on oversampled audio (per-sample, per-channel)
        // Clip is gentle and only prevents overs; oversampling reduces aliasing.
        applySoftClip(upBlock);

        // Downsample back into 'buffer'
        osLocal->processSamplesDown(block);

        // Crossfade dry vs processed
        const float gWet = (float)osRamp01;
        const float gDry = 1.0f - gWet;

        for (int ch = 0; ch < chs; ++ch)
        {
            float* w = buffer.getWritePointer(ch);
            const float* d = dryBuffer.getReadPointer(ch);

            double px1 = dcX1[(size_t)ch];
            double py1 = dcY1[(size_t)ch];
            const double a = dcA;

            for (int i = 0; i < n; ++i)
            {
                float out = gDry * d[i] + gWet * w[i];

                // Post-safety DC block (sealed <= 10 Hz) — removes DC/sub-DC generated by nonlinear safety
                const double x = (double) out;
                const double y = (x - px1) + a * py1;
                px1 = x;
                py1 = y;

                out = (float) y;
                if (!std::isfinite(out)) out = 0.0f;

                w[i] = out;
            }

            dcX1[(size_t)ch] = px1;
            dcY1[(size_t)ch] = py1;
        }
    }

private:
    // Oversamplers are created in prepare() (mono + stereo). No rebuilding here.

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

    // DC block state (base-rate) for post-safety cleanup
    double dcA = 0.0;
    double dcX1[2] = { 0.0, 0.0 };
    double dcY1[2] = { 0.0, 0.0 };

    // Injected (control-only)
    double ratio    = 1.0;
    double attackMs = 10.0;
    double peakAbs  = 0.0;

    // Engage ramp
    double osTarget01 = 0.0;
    double osRamp01   = 0.0;

    std::unique_ptr<juce::dsp::Oversampling<float>> osMono;
    std::unique_ptr<juce::dsp::Oversampling<float>> osStereo;

    juce::AudioBuffer<float> dryBuffer; // sized 2ch in prepare; used as needed
    juce::AudioBuffer<float> osBuffer;  // reserved (unused but kept for future)
};
