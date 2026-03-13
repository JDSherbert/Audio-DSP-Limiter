![image](https://github.com/JDSherbert/Audio-Delay/assets/43964243/6a7b530e-8740-423b-a20b-defd88ea625b)

# Audio DSP: Limiter
<!-- Header Start -->
<img align="right" alt="Stars Badge" src="https://img.shields.io/github/stars/jdsherbert/Audio-DSP-Limiter?label=%E2%AD%90"/>
<img align="right" alt="Forks Badge" src="https://img.shields.io/github/forks/jdsherbert/Audio-DSP-Limiter?label=%F0%9F%8D%B4"/>
<img align="right" alt="Watchers Badge" src="https://img.shields.io/github/watchers/jdsherbert/Audio-DSP-Limiter?label=%F0%9F%91%81%EF%B8%8F"/>
<img align="right" alt="Issues Badge" src="https://img.shields.io/github/issues/jdsherbert/Audio-DSP-Limiter?label=%E2%9A%A0%EF%B8%8F"/>
  <img height="40" width="40" src="https://cdn.simpleicons.org/cplusplus">
</a>
<!-- Header End -->
 
-----------------------------------------------------------------------
 
<a href="">
  <img align="left" alt="Audio Processing" src="https://img.shields.io/badge/Audio%20Processing-black?style=for-the-badge&logo=audacity&logoColor=white&color=black&labelColor=black">
</a>
 
<a href="https://choosealicense.com/licenses/mit/">
  <img align="right" alt="License" src="https://img.shields.io/badge/License%20:%20MIT-black?style=for-the-badge&logo=mit&logoColor=white&color=black&labelColor=black">
</a>
 
<br></br>
 
-----------------------------------------------------------------------
 
## Overview
 
A hard limiter implemented in plain C++, intended as a learning resource for understanding production-grade dynamic range protection. Unlike most DSP learning examples that implement only a ceiling clamp, this limiter implements a four-stage protection chain that reflects how real limiters work in shipped audio software.
 
A limiter's job is to prevent a signal from exceeding a defined ceiling under all conditions, including sudden loud transients that a compressor or simple gain stage cannot react to in time. This implementation approaches that problem in layers, with each stage handling a different class of loudness event.
 
-----------------------------------------------------------------------
 
## Files
 
| File | Description |
|---|---|
| `Limiter.h / .cpp` | Four-stage hard limiter: input gain, loudness correction, jump detection + safety clamp, hard ceiling |
| `main.cpp` | Four demonstrations: normal level, hot signal with visible loudness riding, sudden transient triggering the jump detector, and bypass |
 
-----------------------------------------------------------------------
 
## How It Works
 
The signal passes through four stages in series on every block:
 
```
Input
  │
  ▼
[1. Input Gain]          — flat dB gain applied before any dynamics
  │
  ▼
[2. Loudness Correction] — RMS-based gain rider nudges signal toward targetLUFS
  │                        with one-pole release smoothing
  ▼
[3. Jump Detection]      — lookahead peak scan + rate-of-change check
  │                        triggers safety clamp on sudden transients
  ▼
[3. Safety Clamp]        — panic gain (~-14 dB) with hold + slow recovery
  │
  ▼
[4. Hard Ceiling]        — sample-accurate clamp at ceilingDb as final net
  │
  ▼
Output
```
 
### Stage 1: Input Gain
 
A flat gain (in dB) applied before any dynamic processing. This lets the signal be driven harder or softer into the limiter without touching the source.
 
### Stage 2: Loudness Correction
 
The RMS level of the block is measured across all channels and compared against `targetLUFS`. The error is converted to a correction gain and smoothed with a one-pole IIR release filter before being applied:
 
```
y[n] = alpha * y[n-1] + (1 - alpha) * targetGain
 
where: alpha = exp(-numSamples / (releaseSeconds * sampleRate))
```
 
This prevents the ceiling from doing heavy lifting on material that is simply louder than the target; the ceiling is a last resort, not the primary gain control.
 
### Stage 3: Jump Detection + Safety Clamp
 
The loudness correction operates over hundreds of milliseconds and cannot react to fast transients. The jump detector watches for two conditions each block:
 
| Condition | Trigger |
|---|---|
| Threshold jump | Peak has risen by more than `jumpThresholdDb` (12 dB) since last block |
| Rate of change | Peak is rising faster than `rateThresholdDbPerMs` (3 dB/ms) |
 
If either fires, the safety clamp drops gain immediately to `panicGain` (~-14 dB) and holds it for `panicHoldSeconds` (100ms) before recovering at `safetyRecoveryIncrement` (0.002 per block — roughly 0.7 seconds to unity at 44.1kHz).
 
```
         Transient detected
               │
         safetyGain = 0.2  ◄── immediate drop (~-14 dB)
               │
        [100ms hold]        ◄── panicHoldSamples counts down
               │
        [slow recovery]     ◄── +0.002 per block until 1.0
               │
         safetyGain = 1.0
```
 
### Stage 4: Hard Ceiling
 
Any sample whose absolute value still exceeds the ceiling after the three stages above is clamped to `±ceilingGain`. This is a hard clip — it introduces harmonic distortion if it is doing heavy work, which is why the earlier stages exist to ensure it rarely needs to.
 
```
ceilingGain = 10^(ceilingDb / 20)
 
For ceilingDb = -1.0 dBFS:  ceilingGain ≈ 0.8913
```
 
-----------------------------------------------------------------------
 
## Parameters
 
| Parameter | Type | Default | Description |
|---|---|---|---|
| `inputGainDb` | `float` | `0.0` | Flat input gain in dB applied before all processing. |
| `targetLUFS` | `float` | `-14.0` | Target loudness level in dBFS for the RMS-based loudness correction. |
| `releaseMs` | `float` | `50.0` | Release time in milliseconds for the loudness correction smoother. Longer = slower gain riding. |
| `ceilingDb` | `float` | `-1.0` | Hard ceiling in dBFS. No sample will exceed this value after processing. |
| `bypass` | `bool` | `false` | Bypasses all processing. Raw input passes through unmodified. |
 
**Release time and perceived behaviour:**
 
| Release Time | Character |
|---|---|
| 10 - 30ms | Very fast — audible pumping on sustained material, good for percussive sources |
| 50 - 100ms | Transparent on most material, fast enough to catch most transients |
| 100 - 300ms | Gentle riding — noticeable on dynamic material, musical on full mixes |
| 300ms+ | Very slow — minimal gain movement, ceiling bears more load |
 
-----------------------------------------------------------------------
 
## Usage
 
### Basic
 
```cpp
Sherbert::Limiter limiter;
limiter.prepare(44100.0f, 2);
 
limiter.setInputGainDb(0.0f);
limiter.setTargetLUFS(-14.0f);
limiter.setReleaseMs(50.0f);
limiter.setCeilingDb(-1.0f);
 
// In your audio callback:
float* channels[2] = { leftBuffer, rightBuffer };
limiter.processSamples(channels, 2, blockSize);
```
 
### Reading Metering Values
 
```cpp
// All amplitude values are linear — convert to dBFS with the Db variants
const float inputDb    = limiter.getRawInputAmplitudeDb();
const float preClipDb  = limiter.getPreClipAmplitudeDb();
const float outputDb   = limiter.getOutputAmplitudeDb();
 
// The three metering points let you see how much work each stage is doing:
// inputDb vs preClipDb  — how much the loudness correction + safety clamp reduced
// preClipDb vs outputDb — how much the hard ceiling is clipping
```
 
### Resetting State
 
```cpp
// Call when playback stops to prevent stale gain state from the previous
// session bleeding into the next one
limiter.reset();
```
 
-----------------------------------------------------------------------
 
## API Reference
 
| Method | Description |
|---|---|
| `prepare(sampleRate, numChannels)` | Initialise with sample rate and channel count. Must be called before processing. |
| `reset()` | Clears all internal gain state and metering values. |
| `processSamples(channels, numChannels, numSamples)` | Process one block in place. |
| `setBypass(value)` | Enable or disable bypass. |
| `setInputGainDb(db)` | Set flat input gain in dB. |
| `setTargetLUFS(lufs)` | Set target loudness for the RMS correction stage. |
| `setReleaseMs(ms)` | Set release time for the loudness correction smoother. |
| `setCeilingDb(db)` | Set the hard ceiling in dBFS. |
| `getRawInputAmplitude()` | Peak input amplitude before any processing. Linear. |
| `getPreClipAmplitude()` | Peak amplitude after safety clamp, before hard ceiling. Linear. |
| `getOutputAmplitude()` | Peak output amplitude after the hard ceiling. Linear. |
| `getRawInputAmplitudeDb()` | As above, in dBFS. |
| `getPreClipAmplitudeDb()` | As above, in dBFS. |
| `getOutputAmplitudeDb()` | As above, in dBFS. |
 
-----------------------------------------------------------------------
 
## Limitations & Next Steps
 
This implementation is intentionally focused on the core protection chain as a learning resource. Production limiters typically extend this foundation with:
 
**No lookahead delay compensation**: the lookahead window scans the current block rather than a true delay buffer, so the output is not latency-compensated. A production limiter introduces a delay equal to the lookahead window and processes the delayed signal, so the gain reduction is always applied before the transient reaches the output. This implementation is zero-latency but means the ceiling can occasionally be exceeded on the very first samples of a sudden transient.
 
**No soft knee**: the ceiling is a hard clip. A soft knee would apply gentle compression as the signal approaches the ceiling, transitioning to hard limiting only at the ceiling itself. This significantly reduces the harmonic distortion introduced by the clipper on hot material.
 
**No inter-sample peak detection**: digital clipping can occur between samples during D/A conversion even if no individual sample exceeds the ceiling. True peak limiters oversample the signal to detect these inter-sample peaks. This is required for broadcast and streaming deliverables (EBU R128, ATSC A/85).
 
**Single-stage smoothing**: the loudness correction uses a single release time for both gain reduction and gain recovery. A more sophisticated implementation uses separate attack and release times, with a fast attack to catch transients quickly and a slow release to avoid pumping.
 
Natural next steps from here would be:
- Adding a true lookahead delay buffer for latency-compensated transient handling
- Implementing separate attack and release times for the loudness correction
- Oversampling the ceiling stage for inter-sample peak detection
- Adding a soft knee around the ceiling threshold
 
-----------------------------------------------------------------------
