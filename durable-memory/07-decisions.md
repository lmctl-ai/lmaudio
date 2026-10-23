# 07 — Architectural & License Decisions

1. **Why KFR, FFTW, and libsndfile were excluded**: KFR is dual-licensed GPL-2+/commercial, FFTW is GPL-2+, and libsndfile is LGPL-2.1+. To keep `lmaudio` 100% permissive (MIT/BSD-3) with static linking on all 5 platforms, we implemented our own Cooley-Tukey + Bluestein FFT, RBJ biquads, windowed-sinc resampler, and 16/24/32f RIFF/WAVE reader/writer in `cpp/audio.cpp`.
2. **Why Soundpipe is restricted to 6 clean modules**: Although Soundpipe v0.4 was relicensed to MIT by Paul Batchelor in 2015, ~44 files in `third_party/soundpipe/modules/` carry headers stating they were extracted from Csound (LGPL-2.1) and `zitarev.c` derives from Fons Adriaensen's GPL-2 `zita-rev1`. `CMakeLists.txt` therefore compiles **only** the 6 non-Csound, non-GPL Soundpipe modules (`base.c`, `ftbl.c`, `tevent.c`, `randmt.c`, `compressor.c`, `paulstretch.c`) plus `kiss_fft.c`/`kiss_fftr.c`.
3. **Clean-room MIT replacements in `cpp/audio.cpp`**:
   - `audio.bar`: finite-difference Euler-Bernoulli beam (Bilbao 2009) with clamped/pinned/free ends; free ends use u_xx = 0 and u_xxx = 0 ghost points (0.2 fix: free ends used to blow up to NaN), rigid-body motion is projected out of the strike and a 5 Hz DC blocker cleans the read-out.
   - `Buffer.reverb` & `Buffer.zitarev`: 8-line feedback delay networks with Householder mixing (I - 2/8 11^T), fixed delay lengths, one-pole high-frequency damping, and RT60 feedback gain `10^(-3 * d / rt60)`.
   - `Buffer.bitcrush`: Mid-tread bit-depth quantizer with sample-and-hold decimation.
4. **0.2 decisions**: resampler is Kaiser-windowed sinc (32 zero crossings, beta 9, cutoff 0.91 x Nyquist, tabulated
   kernel) instead of Lanczos-6 (which left -55 dB aliases); LUFS pre-filter uses the libebur128 design that reproduces
   the BS.1770 48 kHz table at any rate; the DaisySP analog kick gets attack-FM 1/50 and self-FM 0 to undo DaisySP's x50
   scaling (it played an octave high); Karplus-Strong allpass coefficient is solved for exact phase delay at f0.
