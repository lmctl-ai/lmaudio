# 07 — Architectural & License Decisions

1. **Why KFR, FFTW, and libsndfile were excluded**: KFR is dual-licensed GPL-2+/commercial, FFTW is GPL-2+, and libsndfile is LGPL-2.1+. To keep `lmaudio` 100% permissive (MIT/BSD-3) with static linking on all 5 platforms, we implemented our own Cooley-Tukey + Bluestein FFT, RBJ biquads, windowed-sinc resampler, and 16/24/32f RIFF/WAVE reader/writer in `cpp/audio.cpp`.
2. **Why Soundpipe is restricted to 6 clean modules**: Although Soundpipe v0.4 was relicensed to MIT by Paul Batchelor in 2015, ~44 files in `third_party/soundpipe/modules/` carry headers stating they were extracted from Csound (LGPL-2.1) and `zitarev.c` derives from Fons Adriaensen's GPL-2 `zita-rev1`. `CMakeLists.txt` therefore compiles **only** the 6 non-Csound, non-GPL Soundpipe modules (`base.c`, `ftbl.c`, `tevent.c`, `randmt.c`, `compressor.c`, `paulstretch.c`) plus `kiss_fft.c`/`kiss_fftr.c`.
3. **Clean-room MIT replacements in `cpp/audio.cpp`**:
   - `audio.bar`: 4-mode Euler-Bernoulli struck bar synthesizer with boundary-condition stiffness ratios and raised-cosine strike excitation.
   - `Buffer.reverb` & `Buffer.zitarev`: 8-line Hadamard feedback delay networks (FDN) with prime-length modulated delay lines, pre-delay, one-pole high-frequency damping, and RT60 feedback gain `10^(-3 * d / rt60)`.
   - `Buffer.bitcrush`: Mid-tread bit-depth quantizer with sample-and-hold decimation.
