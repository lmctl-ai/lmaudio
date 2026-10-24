# 01 — Purpose & Scope

`lmaudio` is the **audio synthesis, digital signal processing (DSP), spectral analysis, and musical sequence host** for `lmscript`. It compiles to a single self-contained native binary (`lmaudio` / `lmaudio.exe`) with zero external runtime library dependencies.

## Core Goals

1. **Deterministic, Offline Audio Synthesis & DSP**: Every oscillator, noise generator (seeded xorshift64\*), physical model (Karplus-Strong plucked string, modal resonator, 4-mode Euler-Bernoulli struck bar), drum synth, filter, reverb, and spectral analysis routine is 100% deterministic across Linux, macOS, and Windows on both `x86_64` and `arm64`.
2. **Mathematical & Analytical Precision**: Built-in double-precision complex Cooley-Tukey + Bluestein chirp-z FFT/IFFT satisfies Parseval's theorem for arbitrary buffer lengths `N` (including prime and non-power-of-two lengths), RBJ Audio-EQ-Cookbook biquads expose exact complex transfer function evaluation (`gain_at`, `db_at`, `phase_deg_at`) alongside Direct-Form II Transposed time-domain filtering, and YIN autocorrelation with cumulative mean normalized difference (`cmndf`) parabolic interpolation tracks fundamental pitch accurately while returning `0.0 Hz` on silence or unvoiced noise.
3. **Self-Contained WAV & CSV Export**: Writes and reads 16-bit PCM, 24-bit PCM, and 32-bit IEEE float RIFF/WAVE files (including `WAVE_FORMAT_EXTENSIBLE` `0xFFFE` subformats) and exports decimated waveforms, FFT spectra, 2D STFT spectrograms, and filter Bode responses to CSV for immediate plotting with `lmplot`.
4. **Permissive License Closure**: Links only MIT and BSD-3-Clause code (`DaisySP`, 6 clean non-Csound/non-GPL `Soundpipe` core/DSP modules, `KissFFT`, `MT19937`, and `lmcode-cpp`).
