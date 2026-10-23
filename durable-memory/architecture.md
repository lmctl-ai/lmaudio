# lmaudio Architecture

## Overview

`lmaudio` is a static C++17 scripting host for `lmscript` (`_dsl_`, bytecode v5) dedicated to audio synthesis, physical modeling, digital filtering, effects, spectral analysis (FFT/IFFT/STFT), pitch detection, and RIFF WAVE I/O.

## Component Stack

1. **`cpp/dslvm.hpp` (`cpp-dsl-sdk`)**:
   - Pinned in `sdk.lock` and verified by `scripts/sync-sdk.sh --verify`.
   - Provides the bytecode v5 loader, schema SHA-256 verification, and method dispatch registry.
2. **DaisySP (`third_party/daisysp`, MIT)**:
   - Pinned in `deps.lock` (`2c72eaf9eac5fc0dca1919d65d606da907832618`).
   - Compiled into `daisysp_core` (excluding the separate `DaisySP-LGPL` submodule).
   - Provides PolyBLEP oscillators (`Oscillator`), 2-operator Chowning FM (`Fm2`), Karplus-Strong physical modeling (`StringVoice`, `String`), struck modal resonator (`ModalVoice`), Huovilainen 24 dB/oct Moog ladder filter (`LadderFilter`), state-variable filter (`Svf`), `Adsr`, 808/synth drum voices (`AnalogBassDrum`, `SyntheticBassDrum`, `AnalogSnareDrum`, `SyntheticSnareDrum`, `HiHat`), and modulation/distortion effects (`Chorus`, `Flanger`, `Phaser`, `Overdrive`, `Wavefolder`).
3. **Soundpipe (`third_party/soundpipe`, MIT)**:
   - Pinned in `deps.lock` (`3efb43bdabd0ed23b17c694292b5a79f1692a3ea`).
   - `CMakeLists.txt` generates `build/generated/soundpipe.h` and `sp_fft_combined.c` from pure-C MIT modules with `-DSPFLOAT=float -DNO_LIBSNDFILE`.
   - Provides Sean Costello's 8-delay-line FDN reverb (`revsc`), Fons Adriaensen's `zitarev`, physical struck bar (`bar`), dynamic range `compressor`, `bitcrush`, and spectral `paulstretch`.
4. **Clean-Room MIT DSP / FFT / STFT / WAV Engine (`cpp/audio.{hpp,cpp}`)**:
   - Replaces `KFR` (GPL-2.0+/commercial), `FFTW` (GPL-2.0+), and `libsndfile` (LGPL-2.1+).
   - **FFT / IFFT / STFT**: Cooley-Tukey radix-2 in-place complex FFT for power-of-2 sizes and Bluestein chirp-z convolution FFT for arbitrary frame counts $N$, preserving exact Parseval energy identity and machine-precision IFFT reconstruction.
   - **RBJ Biquad Filter Bank**: Audio-EQ-Cookbook 2nd-order filters (`lowpass`, `highpass`, `bandpass`, `notch`, `peak`, `lowshelf`, `highshelf`, `allpass`) with exact analytical complex transfer function evaluation $H(e^{j\omega})$.
   - **YIN Fundamental Pitch Estimator**: Cumulative Mean Normalized Difference Function (CMNDF) with parabolic lag interpolation.
   - **RIFF WAVE I/O**: Reads and writes `PCM16`, `PCM24`, and `FLOAT32` (mono and stereo) `.wav` files and exports `.csv` waveforms, spectra, spectrograms, and Bode curves for `lmplot`.
5. **Memory Budget (`cpp/memory_budget.{hpp,cpp}`)**:
   - Enforces `LMAUDIO_MAX_MEMORY_MB` (default `4096` MiB) via global replacement `operator new`/`delete` and pre-allocation `membudget::require(bytes)` checks, stopping runaway scripts with exit code `3` and a clear JSON/stderr error message.
