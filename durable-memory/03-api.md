# 03 — DSL API Reference

All host calls live in the `audio` module (`cli="lmaudio"`).

## `audio.*` Module Functions

- **Oscillators & Noise**: `sine`, `cosine`, `saw`, `square`, `triangle`, `chirp`, `impulse`, `silence`, `noise`, `expr`, `samples`
- **Physical Models, FM & Drums**: `pluck`, `modal`, `bar`, `fm`, `drum`, `sequence`
- **Pitch & Musical Helpers**: `note_freq`, `midi_freq`
- **Filters & I/O**: `biquad`, `biquad_design`, `load_wav`, `assert_close`

## `Buffer` Methods

- **Metadata & Metrics**: `sr`, `channels`, `frames`, `duration`, `get`, `peak`, `peak_db`, `rms`, `rms_db`, `energy`, `dc_offset`, `zcr`
- **Gain & Editing**: `gain`, `gain_db`, `normalize`, `normalize_rms`, `fade`, `adsr`, `slice`, `pad`, `concat`, `mix`, `reverse`, `pan`, `mono`, `channel`, `resample`
- **Filtering & Effects**: `svf`, `ladder`, `delay`, `reverb`, `zitarev`, `chorus`, `flanger`, `phaser`, `overdrive`, `wavefold`, `bitcrush`, `compress`, `paulstretch`
- **Analysis & Export**: `pitch`, `peak_freq`, `centroid`, `thd`, `snr_db`, `spectrum`, `spectrogram`, `save_wav`, `save_csv`

## `Biquad`, `Spectrum`, and `Spectrogram` Methods

- **`Biquad`**: `apply`, `gain_at`, `db_at`, `phase_deg_at`, `save_response_csv`
- **`Spectrum`**: `bins`, `nfft`, `sr`, `freq_at`, `mag_at`, `db_at`, `phase_at`, `mag_at_freq`, `peak_freq`, `centroid`, `parseval_energy`, `ifft`, `save_csv`
- **`Spectrogram`**: `frames`, `bins`, `time_at`, `freq_at`, `mag_at`, `db_at`, `ridge_freq`, `save_csv`, `save_matrix_csv`
