// Copyright (c) 2025 JDSherbert. All rights reserved.
 
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>
 
#include "Limiter.h"
 
// ======================================================================= //
 
static constexpr float  kSampleRate   = 44100.0f;
static constexpr int    kNumChannels  = 2;
static constexpr int    kBlockSize    = 512;
static constexpr float  kPi           = 3.14159265358979323846f;
 
// ======================================================================= //
 
// Fills a stereo block with a sine wave at the given frequency and
// amplitude, advancing the phase each sample.
static void fillSineBlock
(
    float**     channels,
    int         numChans,
    int         numSamples,
    float       frequency,
    float       amplitude,
    float&      phase
)
{
    const float phaseIncrement = 2.0f * kPi * frequency / kSampleRate;
 
    for (int i = 0; i < numSamples; ++i)
    {
        const float sample = amplitude * std::sin(phase);
        phase += phaseIncrement;
        if (phase > 2.0f * kPi) phase -= 2.0f * kPi;
 
        for (int ch = 0; ch < numChans; ++ch)
            channels[ch][i] = sample;
    }
}
 
// ======================================================================= //
 
// Prints a single block's metering values to stdout.
static void printMetering(const Sherbert::Limiter& limiter, int blockIndex)
{
    std::printf("Block %3d  |  Raw In: %6.1f dBFS  |  Pre-clip: %6.1f dBFS  |  Output: %6.1f dBFS  |  Safety Gain: active=%s\n",
        blockIndex,
        limiter.getRawInputAmplitudeDb(),
        limiter.getPreClipAmplitudeDb(),
        limiter.getOutputAmplitudeDb(),
        limiter.getBypassActive() ? "BYPASS" : "on");
}
 
// ======================================================================= //
 
int main()
{
    // Allocate two channel buffers
    std::vector<float> leftBuffer (kBlockSize, 0.0f);
    std::vector<float> rightBuffer(kBlockSize, 0.0f);
    float* channels[kNumChannels] = { leftBuffer.data(), rightBuffer.data() };
 
    Sherbert::Limiter limiter;
    limiter.prepare(kSampleRate, kNumChannels);
 
    // == Demo 1: Normal level signal ====================================
    // A 440Hz sine at 0.5 amplitude (-6 dBFS). The loudness correction will
    // ride this up toward targetLUFS (-14 dBFS) and the ceiling should never
    // be touched since the signal is already below it.
    {
        limiter.reset();
        limiter.setInputGainDb(0.0f);
        limiter.setTargetLUFS(-14.0f);
        limiter.setReleaseMs(50.0f);
        limiter.setCeilingDb(-1.0f);
 
        std::cout << "\n--- Demo 1: Normal level (440Hz, 0.5 amplitude) ---\n\n";
 
        float phase = 0.0f;
        for (int block = 0; block < 5; ++block)
        {
            fillSineBlock(channels, kNumChannels, kBlockSize, 440.0f, 0.5f, phase);
            limiter.processSamples(channels, kNumChannels, kBlockSize);
            printMetering(limiter, block);
        }
    }
 
    // == Demo 2: Hot signal : loudness correction doing work =============
    // A sine at 0.95 amplitude (~-0.45 dBFS). The loudness correction will
    // apply a negative correction to pull it down toward -14 dBFS. Watch
    // the output dBFS drop over the first few blocks as the release smoother
    // converges, then stabilise.
    {
        limiter.reset();
        limiter.setInputGainDb(0.0f);
        limiter.setTargetLUFS(-14.0f);
        limiter.setReleaseMs(200.0f);  // Slower release to make the ramp visible
        limiter.setCeilingDb(-1.0f);
 
        std::cout << "\n--- Demo 2: Hot signal (440Hz, 0.95 amplitude, slow release) ---\n\n";
 
        float phase = 0.0f;
        for (int block = 0; block < 8; ++block)
        {
            fillSineBlock(channels, kNumChannels, kBlockSize, 440.0f, 0.95f, phase);
            limiter.processSamples(channels, kNumChannels, kBlockSize);
            printMetering(limiter, block);
        }
    }
 
    // == Demo 3: Sudden loud transient : jump detection triggering =========
    // Start with a quiet signal, then inject a very loud block to trigger
    // the jump detector. Watch the safety clamp engage (output drops sharply)
    // and then recover slowly over subsequent blocks.
    {
        limiter.reset();
        limiter.setInputGainDb(0.0f);
        limiter.setTargetLUFS(-14.0f);
        limiter.setReleaseMs(50.0f);
        limiter.setCeilingDb(-1.0f);
 
        std::cout << "\n--- Demo 3: Sudden transient — jump detection ---\n\n";
 
        float phase = 0.0f;
 
        // 3 quiet blocks
        for (int block = 0; block < 3; ++block)
        {
            fillSineBlock(channels, kNumChannels, kBlockSize, 440.0f, 0.1f, phase);
            limiter.processSamples(channels, kNumChannels, kBlockSize);
            printMetering(limiter, block);
        }
 
        // 1 very loud block (simulates a transient)
        std::cout << "  *** loud transient ***\n";
        fillSineBlock(channels, kNumChannels, kBlockSize, 440.0f, 2.0f, phase);
        limiter.processSamples(channels, kNumChannels, kBlockSize);
        printMetering(limiter, 3);
 
        // 6 recovery blocks - watch safety gain ramp back to unity
        for (int block = 4; block < 10; ++block)
        {
            fillSineBlock(channels, kNumChannels, kBlockSize, 440.0f, 0.1f, phase);
            limiter.processSamples(channels, kNumChannels, kBlockSize);
            printMetering(limiter, block);
        }
    }
 
    // == Demo 4: Bypass ==============================================
    // Same hot signal as Demo 2, but with bypass active. Output should
    // match raw input exactly; no processing applied.
    {
        limiter.reset();
        limiter.setBypass(true);
        limiter.setCeilingDb(-1.0f);
 
        std::cout << "\n--- Demo 4: Bypass active ---\n\n";
 
        float phase = 0.0f;
        for (int block = 0; block < 3; ++block)
        {
            fillSineBlock(channels, kNumChannels, kBlockSize, 440.0f, 0.95f, phase);
            limiter.processSamples(channels, kNumChannels, kBlockSize);
            printMetering(limiter, block);
        }
    }
 
    return 0;
}
 
// ======================================================================= //
