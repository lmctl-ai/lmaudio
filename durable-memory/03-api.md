# 03 — DSL API Reference

All host calls live in the `audio` context (`cli="lmaudio"`). The authoritative list is
`lmaudio --dsl-signatures` (generated into `schema/lmaudio.dsl.json` by `scripts/gen-schema.py`
from the registrations in `cpp/main.cpp`; `test.sh` fails when they drift). README "What is in the
box" groups them for users.

## `audio.*`
- Signals: `silence`, `sine`, `cosine`, `saw`, `square`, `triangle`, `chirp`, `impulse`, `noise`, `expr`,
  `additive` (0.2: harmonic amplitudes or `ratio:amp` partials, band-limited).
- Models and music: `fm`, `pluck`, `modal`, `bar`, `drum`, `karplus` (0.2), `sequence` (instruments
  sine/saw/square/triangle/fm/pluck/karplus/modal/pad), `note_freq`, `midi_freq`, `cents` (0.2), `db` (0.2).
- I/O and checks: `load_wav`, `stereo`, `biquad_design`, `assert_close`, `print` (0.2, stdout),
  `save_text` (0.2, writes a CSV/table built in the script; respects --dry-run).

## `Buffer`
- Inspect: `sr`, `channels`, `frames`, `duration`, `get`, `channel`, `to_mono`.
- Levels/envelopes: `gain`, `gain_db`, `normalize`, `normalize_rms`, `normalize_lufs` (0.2), `fade`,
  `adsr`, `multiply` (0.2, same sr and frames), `pan`.
- Edit: `slice`, `pad`, `concat`, `mix`, `reverse`, `resample` (Kaiser sinc since 0.2),
  `pattern` (0.2 step sequencer), `granular` (0.2).
- Filters/effects: `biquad`, `svf`, `ladder`, `lpf_sweep`, `delay`, `reverb`, `zitarev`, `convolve`
  (0.2), `chorus`, `flanger`, `phaser`, `overdrive`, `wavefold`, `bitcrush`, `compress`, `paulstretch`.
- Analysis: `rms`, `peak`, `dc_offset`, `energy`, `zcr`, `pitch`, `peak_freq`, `centroid`, `thd`,
  `snr_db`, `spectrum`, `spectrogram`, `lufs` (0.2), `rt60` (0.2), `schroeder_db` (0.2),
  `pitch_track_csv` (0.2).
- Output: `save_wav` (16/24/32f), `save_csv`.

## `Spectrum`, `Spectrogram`, `Biquad`
- `Spectrum`: `bins`, `nfft`, `freq_at`, `mag_at`, `db_at`, `phase_at`, `mag_at_freq`, `peak_bin`,
  `peak_freq`, `peak_freq_in(lo, hi)` (0.2), `peak_mag`, `centroid`, `parseval_energy`, `ifft`, `save_csv`.
  Windows: rect, hann, hamming, blackman, flattop (0.2; amplitude-accurate).
- `Spectrogram`: `frames`, `bins`, `sr`, `time_at`, `freq_at`, `mag_at`, `db_at`, `ridge_freq`, `save_csv`.
- `Biquad`: `b0..a2`, `gain_at`, `db_at`, `phase_deg_at`, `apply` (fails when the buffer's sr differs
  from the design sr), `save_response_csv`.

## Conventions worth remembering
- DSL has no log/exp: use `audio.db`, `audio.cents`, or `audio.expr` for envelopes; no `print` at the
  lmscript level for host values: `audio.print`, then echo `r.stdout` lines that do not start with `{`.
- `pattern`: X 1.0, x 0.7, o 0.4, 1-9 = 0.1-0.9, `.`/`-`/`_` rest, spaces and `|` ignored; result lasts the
  grid or until the last hit's tail ends.
