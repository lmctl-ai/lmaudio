# 05 — Verification & Testing

`./test.sh` enforces:

1. **Pin & Schema Verification**: `scripts/sync-sdk.sh --verify`, `scripts/sync-deps.sh --verify`, `scripts/gen-schema.py` diff check, byte-exact `--dsl-schema --json` vs `schema/lmaudio.dsl.json`, host vs compiler `--dsl-signatures`, and byte-exact `--notices` vs `THIRD_PARTY_NOTICES`.
2. **Linkage Verification**: `readelf -d` (fallback `ldd`) check on Linux verifying the glibc NEEDED allowlist (`libc.so.6`, `libm.so.6`, `libpthread.so.0`, `libdl.so.2`, `librt.so.1`, `ld-linux*`, and nothing else), `otool -L` on macOS, `objdump -p` on Windows.
3. **Mathematical & Analytical DSP Suites (`tests/*.lms`)**:
   - `synthesis_and_pitch.lms`: Oscillator peak/RMS/DC/ZCR, YIN pitch accuracy on sine/cosine/saw/square/triangle, silence pitch returning `0.0 Hz`, expression waveform RMS, note/MIDI frequencies, and seeded noise determinism + spectral centroid ordering.
   - `filters_and_biquad.lms`: RBJ Butterworth lowpass, notch, and peaking EQ analytical `gain_at`/`db_at`/`phase_deg_at` matched against steady-state time-domain filtered sine RMS; DaisySP SVF and Moog ladder filtering.
   - `fft_and_parseval.lms`: Exact FFT bin magnitudes, Parseval's theorem (`sp.parseval_energy() == x.energy()`), IFFT reconstruction (`SNR > 120 dB`) for both power-of-2 (`N = 4096`) and non-power-of-2 Bluestein (`N = 1000`), and STFT ridge tracking.
   - `physical_effects_and_seq.lms`: Karplus-Strong pluck, modal resonator, 4-mode Euler-Bernoulli bar, 2-op FM sidebands, drum voices, wavefolder/overdrive/bitcrush THD increase, delay/FDN reverbs, compressor, PaulStretch, and musical sequence pitch tracking.
   - `wav_and_editing.lms`: 16/24/32f WAV round-trip SNR, constant-power stereo panning energy conservation, multi-channel `normalize_rms`, and windowed-sinc resampling.
4. **Error & Resource Boundary Checks**: 28+ `expect_error` cases verifying exit code `3` and diagnostic messages on both `stdout` (JSON) and `stderr`, `WAVE_FORMAT_EXTENSIBLE` (`0xFFFE`) loading, `/dev/full` write-failure detection, and `LMAUDIO_MAX_MEMORY_MB` enforcement across both C++ and Soundpipe native allocations.

## 0.2 additions
- `tests/loudness_convolution_and_synthesis.lms`: BS.1770 (-3.01 LUFS sine, 0 LUFS stereo, relative gate
  -23.176 for a loud/quiet step), Kaiser resampler (1 kHz kept, 5 kHz alias < 3e-5, DC exact at edges),
  convolution identity and delay, Schroeder RT60 of a 1.2 s exponential (2.5 %), Karplus-Strong tuning
  (< 1 cent), additive square THD 0.4288, flat-top amplitude, pattern grid/gains, granular determinism,
  multiply, kick at 55 Hz, Euler-Bernoulli bar modes (free-free 4.730^2, cantilever 1.875^2).
- New error cases: biquad sr mismatch, unstable bar stiffness, non-finite WAV samples, multiply frame
  mismatch, convolve sr mismatch, bad pattern char, bad partial, silent LUFS, short RT60, karplus range,
  granular position. WAV edge block: RIFF pad byte for odd 24-bit chunks, clip warning, truncated data.
- Showcase determinism skips lmplot/lmdraw programs by name (`build/*-*.dslb`).
- Example runs need `GITHUB_TOKEN` (or a warm cache): `install=` queries the GitHub API and hits 403
  without a token.
