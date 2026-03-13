// Copyright (c) 2025 JDSherbert. All rights reserved.
 
#include "Limiter.h"
 
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
 
// ======================================================================= //
 
void Sherbert::Limiter::prepare(float sr, int channels)
{
    assert(sr > 0.0f);
    assert(channels > 0);
 
    sampleRate  = sr;
    numChannels = channels;
 
    reset();
}
 
// ======================================================================= //
 
void Sherbert::Limiter::reset()
{
    loudnessGain   = 1.0f;
    safetyGain     = 1.0f;
    lastPeakDb     = -100.0f;
    panicHoldSamples = 0;
 
    rawInputAmplitude.store(0.0f);
    preClipAmplitude.store(0.0f);
    outputAmplitude.store(0.0f);
}
 
// ======================================================================= //
 
void Sherbert::Limiter::processSamples(float** channels,
                                       int     numChans,
                                       int     numSamples)
{
    assert(channels   != nullptr);
    assert(numChans   > 0);
    assert(numSamples > 0);
 
    // Measure the raw input before any processing so the UI can display
    // what came in before the limiter touched it.
    measureRawInputPeak(channels, numChans, numSamples);
 
    if (bypassActive)
    {
        outputAmplitude.store(rawInputAmplitude.load());
        return;
    }
 
    // === Stage 1: Input Gain ===========================================
    // Apply a flat gain before any dynamic processing. This lets the user
    // drive the limiter harder or softer without touching the source signal.
    applyInputGain(channels, numChans, numSamples);
 
    // === Stage 2: Loudness Correction ==================================
    // Measure the RMS level of this block and apply a correction gain to
    // nudge it toward targetLUFS. The correction is smoothed with a
    // one-pole release filter so gain changes are gradual rather than
    // abrupt. This prevents the hard ceiling from being hit unnecessarily
    // on material that is simply louder than the target; the ceiling is
    // a last resort, not the primary gain control.
    applyLoudnessCorrection(channels, numChans, numSamples);
 
    // === Stage 3: Jump Detection + Safety Clamp ========================
    // Scan the lookahead window for a sudden peak that is either larger
    // than jumpThresholdDb relative to the previous block, or rising
    // faster than rateThresholdDbPerMs. If either condition is met, the
    // safety clamp drops the gain immediately to panicGain and holds it
    // there for panicHoldSeconds before slowly recovering to unity.
    //
    // This handles fast transients that the slower loudness correction
    // cannot react to in time (drum hits, clicks, plosives).
    handleJumpDetection(channels, numChans, numSamples);
    applySafetyGain(channels, numChans, numSamples);
 
    // === Stage 4: Hard Ceiling =========================================
    // After all the dynamic processing above, clamp any remaining samples
    // that still exceed the ceiling. This is a hard clip; it is the
    // final safety net and should rarely be doing heavy lifting if the
    // stages above are tuned correctly.
    measurePreClipAmplitude(channels, numChans, numSamples);
    const float outputPeak = applyCeiling(channels, numChans, numSamples);
    outputAmplitude.store(outputPeak);
}
 
// ======================================================================= //
// Stage 0 — Raw Input Metering
// ======================================================================= //
 
void Sherbert::Limiter::measureRawInputPeak(float** channels,
                                            int     numChans,
                                            int     numSamples)
{
    float peak = 0.0f;
 
    for (int ch = 0; ch < numChans; ++ch)
    {
        const float* data = channels[ch];
        for (int i = 0; i < numSamples; ++i)
            peak = std::max(peak, std::abs(data[i]));
    }
 
    rawInputAmplitude.store(peak);
}
 
// ======================================================================= //
// Stage 1 — Input Gain
// ======================================================================= //
 
void Sherbert::Limiter::applyInputGain(float** channels,
                                        int     numChans,
                                        int     numSamples)
{
    const float gain = decibelsToGain(inputGainDb);
 
    for (int ch = 0; ch < numChans; ++ch)
    {
        float* data = channels[ch];
        for (int i = 0; i < numSamples; ++i)
            data[i] *= gain;
    }
}
 
// ======================================================================= //
// Stage 2 — Loudness Correction
// ======================================================================= //
 
void Sherbert::Limiter::applyLoudnessCorrection(float** channels,
                                                 int     numChans,
                                                 int     numSamples)
{
    // === HOW LOUDNESS CORRECTION WORKS ==================================
    //
    // We measure the RMS (Root Mean Square) level of this block across all
    // channels. RMS is a better proxy for perceived loudness than peak
    // because it reflects average energy rather than instantaneous peaks.
    //
    // RMS = sqrt( (1/N) * sum(x[i]^2) )
    //
    // We then convert it to dBFS and compare against targetLUFS to get an
    // error signal in dB. This error is clamped to ±loudnessCorrectionMaxDb
    // to prevent overcorrection on silence or extreme material.
    //
    // Rather than applying the correction gain immediately (which would
    // cause audible clicks on block boundaries), we smooth it with a
    // one-pole IIR release filter:
    //
    //   loudnessGain = alpha * loudnessGain + (1 - alpha) * targetGain
    //
    // where alpha is derived from the release time and sample rate.
    // This makes gain changes gradual — the longer the release, the slower
    // the gain rides back up after a loud passage.
    // =====================================================================
 
    // Compute average RMS across all channels
    float rms = 0.0f;
    for (int ch = 0; ch < numChans; ++ch)
    {
        const float* data = channels[ch];
        float sumOfSquares = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            sumOfSquares += data[i] * data[i];
        rms += std::sqrt(sumOfSquares / static_cast<float>(numSamples));
    }
 
    rms = std::max(rms / static_cast<float>(numChans), rmsFloor);
 
    // Convert to dBFS and compute error relative to target
    const float currentDb    = gainToDecibels(rms);
    const float errorDb      = targetLUFS - currentDb;
    const float correctionDb = clamp(errorDb, -loudnessCorrectionMaxDb, loudnessCorrectionMaxDb);
    const float targetGain   = decibelsToGain(correctionDb);
 
    // Smooth toward the target gain using the release time
    applyReleaseSmoothing(targetGain, numSamples);
 
    // Apply the smoothed gain to the buffer
    for (int ch = 0; ch < numChans; ++ch)
    {
        float* data = channels[ch];
        for (int i = 0; i < numSamples; ++i)
            data[i] *= loudnessGain;
    }
}
 
// ======================================================================= //
 
void Sherbert::Limiter::applyReleaseSmoothing(float targetGain, int numSamples)
{
    // === ONE-POLE IIR RELEASE SMOOTHER =================================
    //
    // A one-pole IIR (Infinite Impulse Response) filter is the standard
    // tool for smoothing gain changes in dynamic processors. It works by
    // blending the current gain toward the target gain each block:
    //
    //   y[n] = alpha * y[n-1] + (1 - alpha) * target
    //
    // alpha is derived from the release time:
    //
    //   alpha = exp(-numSamples / (releaseSeconds * sampleRate))
    //
    // At alpha = 0.0: gain snaps instantly to target (no smoothing)
    // At alpha = 1.0: gain never moves (infinite release)
    //
    // The numSamples term in the exponent accounts for the fact that we're
    // updating once per block rather than once per sample; the filter
    // advances by numSamples steps each call.
    // ====================================================================
 
    const float releaseSeconds = releaseMs * 0.001f;
    const float alpha = std::exp(
        -static_cast<float>(numSamples) / (releaseSeconds * sampleRate));
 
    loudnessGain = alpha * loudnessGain + (1.0f - alpha) * targetGain;
}
 
// ======================================================================= //
// Stage 3 — Jump Detection + Safety Clamp
// ======================================================================= //
 
void Sherbert::Limiter::handleJumpDetection(float** channels,
                                             int     numChans,
                                             int     numSamples)
{
    // === HOW JUMP DETECTION WORKS =======================================
    //
    // The loudness correction above reacts over hundreds of milliseconds;
    // it is not fast enough to catch sudden loud transients like drum hits,
    // gunshots, or plosives. Jump detection watches for those events.
    //
    // We scan the first lookaheadSamples of the block (the "lookahead
    // window") for the peak value and convert it to dBFS. We then check
    // two conditions:
    //
    //   1. Threshold jump: has the peak risen by more than jumpThresholdDb
    //      since the last block? A 12dB jump in one block is a transient.
    //
    //   2. Rate of change: how fast is the peak rising in dB/ms? A steep
    //      ramp (> rateThresholdDbPerMs) can exceed the ceiling even if
    //      the absolute jump is modest. This catches fast fades-in and
    //      some synthesis artefacts.
    //
    // If either condition triggers, engageSafetyClamp() is called.
    // =====================================================================
 
    const float futurePeakDb = detectJump(channels, numChans, numSamples);
    const float deltaDb      = futurePeakDb - lastPeakDb;
 
    const float blockMs      = static_cast<float>(numSamples) * 1000.0f / sampleRate;
    const float rateDbPerMs  = deltaDb / std::max(blockMs, 0.001f);
 
    const bool jumpDetected =
        deltaDb      > jumpThresholdDb     ||
        rateDbPerMs  > rateThresholdDbPerMs;
 
    lastPeakDb = futurePeakDb;
 
    if (jumpDetected)
        engageSafetyClamp();
}
 
// ======================================================================= //
 
float Sherbert::Limiter::detectJump(float** channels,
                                     int     numChans,
                                     int     numSamples)
{
    float peak = 0.0f;
 
    // Only scan as far as the lookahead window (or the block length,
    // whichever is shorter)
    const int scanSamples = std::min(lookaheadSamples, numSamples);
 
    for (int ch = 0; ch < numChans; ++ch)
    {
        const float* data = channels[ch];
        for (int i = 0; i < scanSamples; ++i)
            peak = std::max(peak, std::abs(data[i]));
    }
 
    // Apply a small floor before the dB conversion to avoid log(0)
    peak = std::max(peak, peakFloor);
    return gainToDecibels(peak);
}
 
// ======================================================================= //
 
void Sherbert::Limiter::engageSafetyClamp()
{
    // Drop gain immediately to panicGain (~-14 dB) and hold for
    // panicHoldSeconds. Recovery is handled in applySafetyGain().
    safetyGain       = panicGain;
    panicHoldSamples = static_cast<int>(panicHoldSeconds * sampleRate);
}
 
// ======================================================================= //
 
void Sherbert::Limiter::applySafetyGain(float** channels,
                                         int     numChans,
                                         int     numSamples)
{
    // === SAFETY GAIN STATE MACHINE =====================================
    //
    // Three states:
    //
    //   PANIC HOLD  — panicHoldSamples > 0. safetyGain stays at panicGain.
    //                 The hold prevents the gain from recovering during the
    //                 transient itself.
    //
    //   RECOVERY    — panicHoldSamples <= 0. safetyGain increments by
    //                 safetyRecoveryIncrement each block until it reaches
    //                 1.0. The slow ramp prevents the recovery from sounding
    //                 like a pumping artefact.
    //
    //   IDLE        — safetyGain == 1.0, panicHoldSamples == 0.
    //                 No change, no cost.
    // ===================================================================
 
    if (panicHoldSamples > 0)
    {
        panicHoldSamples -= numSamples;
    }
    else
    {
        safetyGain = std::min(1.0f, safetyGain + safetyRecoveryIncrement);
    }
 
    for (int ch = 0; ch < numChans; ++ch)
    {
        float* data = channels[ch];
        for (int i = 0; i < numSamples; ++i)
            data[i] *= safetyGain;
    }
}
 
// ======================================================================= //
// Stage 4 — Pre-clip Metering + Hard Ceiling
// ======================================================================= //
 
void Sherbert::Limiter::measurePreClipAmplitude(float** channels,
                                                 int     numChans,
                                                 int     numSamples)
{
    float peak = 0.0f;
 
    for (int ch = 0; ch < numChans; ++ch)
    {
        const float* data = channels[ch];
        for (int i = 0; i < numSamples; ++i)
            peak = std::max(peak, std::abs(data[i]));
    }
 
    preClipAmplitude.store(peak);
}
 
// ======================================================================= //
 
float Sherbert::Limiter::applyCeiling(float** channels,
                                       int     numChans,
                                       int     numSamples)
{
    // === HARD CEILING ==================================================
    //
    // Any sample whose absolute value exceeds the ceiling gain is clamped
    // to ±ceilingGain. This is a hard clip; it introduces harmonic
    // distortion if it is doing heavy work, which is why the earlier stages
    // exist to ensure it rarely needs to.
    //
    // The ceiling is expressed in dBFS and converted to a linear gain:
    //
    //   ceilingGain = 10^(ceilingDb / 20)
    //
    // For ceilingDb = -1.0 dBFS, ceilingGain ≈ 0.8913
    // ===================================================================
 
    const float ceilingGain = decibelsToGain(ceilingDb);
    float peak = 0.0f;
 
    for (int ch = 0; ch < numChans; ++ch)
    {
        float* data = channels[ch];
        for (int i = 0; i < numSamples; ++i)
        {
            data[i] = clamp(data[i], -ceilingGain, ceilingGain);
            peak    = std::max(peak, std::abs(data[i]));
        }
    }
 
    return peak;
}
 
// ======================================================================= //
