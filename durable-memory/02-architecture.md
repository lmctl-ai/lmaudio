# 02 — Architecture & DSP Core

## Source Layout

- `cpp/main.cpp`: `dsl::Registry` host entrypoint, CLI flag handling (`--version`, `--notices`, `--dsl-schema`, `--dsl-signatures`, `--dry-run`, `--dsl <file.dslb>`), early `LMAUDIO_MAX_MEMORY_MB` validation, and dual `stdout` (JSON) + `stderr` (`lmaudio: ...`) error reporting.
- `cpp/audio.hpp` & `cpp/audio.cpp`: Complete implementation of `Buffer`, `Spectrum`, `Spectrogram`, and `Biquad` handle types and all synthesis, filtering, effects,WAV/CSV I/O, and spectral analysis algorithms.
- `cpp/memory_budget.hpp` & `cpp/memory_budget.cpp`: Global C++ `operator new`/`operator delete` tracker plus explicit `membudget::charge()` / `membudget::release()` hooks for C `malloc` allocations inside Soundpipe (`sp_data`, `sp_ftbl`, `sp_paulstretch`, `sp_compressor`).
- `third_party/daisysp/`: Pinned MIT checkout of Electrosmith DaisySP.
- `third_party/soundpipe/`: Pinned checkout of Soundpipe; `CMakeLists.txt` compiles **only** the 6 clean non-Csound, non-GPL modules (`base.c`, `ftbl.c`, `tevent.c`, `randmt.c`, `compressor.c`, `paulstretch.c`) plus `kiss_fft.c`/`kiss_fftr.c`.

## Handle Types

1. **`Buffer`**: Planar `float` sample storage (`channels * frames`), sample rate `sr` (`8000..192000 Hz`), channels (`1..8`), frames (`1..200000000`).
2. **`Spectrum`**: Double-precision complex FFT bins `X[0..N-1]` with window coherent-gain compensation, `mag_at`, `db_at`, `phase_at`, `mag_at_freq`, `peak_freq`, `centroid`, `parseval_energy`, and `ifft()`.
3. **`Spectrogram`**: 2D STFT time-frequency grid (`n_frames x n_bins`) with `time_at`, `freq_at`, `mag_at`, `db_at`, `ridge_freq`, and CSV export (`save_csv`, `save_matrix_csv`).
4. **`Biquad`**: Normalized Direct-Form II Transposed coefficients (`b0, b1, b2, a1, a2`) with analytical evaluation (`gain_at`, `db_at`, `phase_deg_at`), `apply(Buffer)`, and `save_response_csv`.
