// Copyright (c) 2025 JDSherbert. All rights reserved.
 
#pragma once
 
#include <atomic>
#include <cmath>
 
// ======================================================================= //
// Limiter
//
// A single-class hard limiter implementing a four-stage protection chain:
//
//   1. Input Gain          - scales the signal before any processing
//   2. Loudness Correction - RMS-based gain rider that nudges the signal
//                            toward a target loudness level with release
//                            smoothing, preventing the ceiling from being
//                            hit unnecessarily on quiet material
//   3. Safety Clamp        - lookahead jump detector that watches for sudden
//                            loud transients and applies a fast gain
//                            reduction with a short hold and slow recovery
//   4. Hard Ceiling        - sample-accurate hard clip at a configurable dBFS
//                            ceiling as the final safety net
//
// Signal flow:
//
//   Input
//     │
//     ▼
//   [Input Gain]
//     │
//     ▼
//   [Loudness Correction]  ◄── RMS measurement, release smoothing
//     │
//     ▼
//   [Jump Detection]       ◄── lookahead peak scan, rate-of-change check
//     │
//     ▼
//   [Safety Clamp]         ◄── panic gain with hold + recovery
//     │
//     ▼
//   [Hard Ceiling]         ◄── sample clamp at ceilingDb
//     │
//     ▼
//   Output
//
// Usage:
//   Sherbert::Limiter limiter;
//   limiter.prepare(44100.0f, 2);
//   limiter.setInputGainDb(0.0f);
//   limiter.setTargetLUFS(-14.0f);
//   limiter.setReleaseMs(50.0f);
//   limiter.setCeilingDb(-1.0f);
//
//   // In your audio callback:
//   limiter.processSamples(leftBuffer, rightBuffer, numSamples);
//
// ======================================================================= //
 
namespace Sherbert
{
    class Limiter
    {

    public:
 
        // ------------------------------------------------------------------
        // Constructor
        // ------------------------------------------------------------------
 
        Limiter() = default;
 
        // ------------------------------------------------------------------
        // prepare
        //
        // Call this before processing begins, whenever the sample rate
        // changes, or after reset(). Sets internal state derived from
        // the sample rate (lookahead window, hold time, etc).
        //
        // sampleRate  - Audio sample rate in Hz (e.g. 44100.0f).
        // numChannels - Number of audio channels (e.g. 2 for stereo).
        // ------------------------------------------------------------------
 
        void prepare(float sampleRate, int numChannels);
 
        // ------------------------------------------------------------------
        // reset
        //
        // Clears all internal state (gain smoothing, safety clamp state,
        // amplitude measurements). Call when playback stops to prevent
        // stale state from bleeding into the next session.
        // ------------------------------------------------------------------
 
        void reset();
 
        // ------------------------------------------------------------------
        // processSamples
        //
        // Process one block of audio through the full protection chain.
        // All channel pointers must point to arrays of at least numSamples
        // floats. The buffer is modified in place.
        //
        // channels    - Array of float pointers, one per channel.
        // numChannels - Number of channels in the array.
        // numSamples  - Number of samples per channel to process.
        // ------------------------------------------------------------------
 
        void processSamples(float** channels, int numChannels, int numSamples);
 
        // ------------------------------------------------------------------
        // Setters
        // ------------------------------------------------------------------
 
        void setBypass(bool value)          noexcept { bypassActive = value; }
        void setInputGainDb(float db)       noexcept { inputGainDb = db; }
        void setTargetLUFS(float lufs)      noexcept { targetLUFS = lufs; }
        void setReleaseMs(float ms)         noexcept { releaseMs = ms; }
        void setCeilingDb(float db)         noexcept { ceilingDb = db; }
 
        // ------------------------------------------------------------------
        // Getters
        //
        // Amplitude values are updated each block and safe to read from
        // any thread (stored as std::atomic<float>).
        //
        // getRawInputAmplitude()    - Peak amplitude of input before any
        //                             processing. Linear scale.
        // getPreClipAmplitude()     - Peak amplitude after the safety clamp,
        //                             before the hard ceiling. Linear scale.
        // getOutputAmplitude()      - Peak amplitude after the hard ceiling.
        //                             Linear scale.
        //
        // The Db variants convert these to dBFS.
        // ------------------------------------------------------------------
 
        bool  getBypassActive()       const noexcept { return bypassActive; }
        float getInputGainDb()        const noexcept { return inputGainDb; }
        float getTargetLUFS()         const noexcept { return targetLUFS; }
        float getReleaseMs()          const noexcept { return releaseMs; }
        float getCeilingDb()          const noexcept { return ceilingDb; }
 
        float getRawInputAmplitude()  const noexcept { return rawInputAmplitude.load(); }
        float getPreClipAmplitude()   const noexcept { return preClipAmplitude.load(); }
        float getOutputAmplitude()    const noexcept { return outputAmplitude.load(); }
 
        float getRawInputAmplitudeDb() const noexcept
        {
            return gainToDecibels(rawInputAmplitude.load());
        }
 
        float getPreClipAmplitudeDb() const noexcept
        {
            return gainToDecibels(preClipAmplitude.load());
        }
 
        float getOutputAmplitudeDb() const noexcept
        {
            return gainToDecibels(outputAmplitude.load());
        }
 
    private:
 
        // ------------------------------------------------------------------
        // Processing stages - called in order by processSamples()
        // ------------------------------------------------------------------
 
        // Stage 0: measure raw input peak before any gain is applied
        void measureRawInputPeak(float** channels, int numChannels, int numSamples);
 
        // Stage 1: apply flat input gain (dB)
        void applyInputGain(float** channels, int numChannels, int numSamples);
 
        // Stage 2: RMS-based loudness correction with release smoothing
        void applyLoudnessCorrection(float** channels, int numChannels, int numSamples);
 
        // Stage 2.1: one-pole release smoother for loudness gain
        void applyReleaseSmoothing(float targetGain, int numSamples);
 
        // Stage 3: lookahead jump detector
        void handleJumpDetection(float** channels, int numChannels, int numSamples);
 
        // Stage 3.1: scans lookahead window for peak, returns dBFS
        float detectJump(float** channels, int numChannels, int numSamples);
 
        // Stage 3.2: arms the safety clamp
        void engageSafetyClamp();
 
        // Stage 3.3: applies safety gain with hold and recovery
        void applySafetyGain(float** channels, int numChannels, int numSamples);
 
        // Stage 4: measure peak before hard ceiling
        void measurePreClipAmplitude(float** channels, int numChannels, int numSamples);
 
        // Stage 4: hard ceiling clamp, returns output peak
        float applyCeiling(float** channels, int numChannels, int numSamples);
 
        // ------------------------------------------------------------------
        // Utility
        // ------------------------------------------------------------------
 
        static float decibelsToGain(float db) noexcept
        {
            return std::pow(10.0f, db / 20.0f);
        }
 
        static float gainToDecibels(float gain) noexcept
        {
            return 20.0f * std::log10(std::max(gain, 1.0e-9f));
        }
 
        static float clamp(float value, float low, float high) noexcept
        {
            return value < low ? low : (value > high ? high : value);
        }
 
        // ------------------------------------------------------------------
        // State
        // ------------------------------------------------------------------
 
        float sampleRate  = 44100.0f;
        int   numChannels = 2;
 
        bool  bypassActive = false;
 
        // Input gain
        float inputGainDb = 0.0f;
 
        // Loudness correction
        float targetLUFS  = -14.0f;
        float releaseMs   = 50.0f;
        float loudnessGain = 1.0f;
 
        // Safety clamp
        float safetyGain     = 1.0f;
        float lastPeakDb     = -100.0f;
        int   panicHoldSamples = 0;
 
        // Hard ceiling
        float ceilingDb = -1.0f;
 
        // Amplitude metering (thread-safe reads from UI thread)
        std::atomic<float> rawInputAmplitude { 0.0f };
        std::atomic<float> preClipAmplitude  { 0.0f };
        std::atomic<float> outputAmplitude   { 0.0f };
 
        // ------------------------------------------------------------------
        // Constants
        // ------------------------------------------------------------------
 
        // Number of samples scanned ahead for jump detection.
        // ~1.5ms at 44.1kHz which is long enough to catch fast transients without
        // introducing perceptible latency.
        static constexpr int lookaheadSamples = 64;
 
        // A level jump larger than this in a single block triggers the
        // safety clamp.
        static constexpr float jumpThresholdDb = 12.0f;
 
        // If the peak is rising faster than this many dB per millisecond
        // the safety clamp is also triggered, catching fast ramps that
        // might not exceed the threshold in a single block.
        static constexpr float rateThresholdDbPerMs = 3.0f;
 
        // Safety gain applied immediately when a jump is detected (~-14dB).
        static constexpr float panicGain = 0.2f;
 
        // Duration of the safety gain hold after a jump is detected.
        // Expressed as a fraction of sampleRate in prepare().
        static constexpr float panicHoldSeconds = 0.1f;
 
        // Recovery increment applied to safetyGain per block when not in
        // panic hold. At 0.002 per block this is a ~500-block ramp at
        // 44.1kHz ~ roughly 0.7 seconds to recover from panic to unity.
        static constexpr float safetyRecoveryIncrement = 0.002f;
 
        // Floor applied to RMS before loudness correction to prevent
        // division by zero on silence.
        static constexpr float rmsFloor = 1.0e-6f;
 
        // Floor applied to peak before gainToDecibels in jump detection.
        static constexpr float peakFloor = 1.0e-9f;
 
        // Maximum loudness correction range in dB. Prevents the loudness
        // rider from overcorrecting on very quiet or very loud material.
        static constexpr float loudnessCorrectionMaxDb = 12.0f;
 
    };
 
}
 
// ======================================================================= //
