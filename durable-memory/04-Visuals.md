# 04 — Visuals & Gallery Pipeline

Every example in `examples/*.lms` is a self-contained two-stage pipeline (`lmaudio` + `lmplot`):

1. **Stage 1 (`cli="lmaudio"`)**: Synthesizes or processes audio, saves a compact 16-bit PCM WAV (`<= 200 KB`, typically `sr = 16000` or `22050 Hz` and `1.5–4.0 s` duration) into `audio/<name>.wav`, and exports time-domain waveforms, FFT spectra, 2D STFT spectrogram matrices (`save_matrix_csv`), or biquad Bode responses into `build/*.csv`.
2. **Stage 2 (`cli="lmplot"`)**: Reads the CSV data from `build/*.csv` and renders multi-panel publication-grade charts (`images/<name>.png`, plus `images/harmonic_morph_anim.gif` via `ffmpeg`) with informative titles, axis labels, legends, and annotations.

`examples/render-all.sh` executes every `examples/*.lms` and copies the resulting PNG/GIF and WAV files into `docs/images/` and `docs/audio/` for the README gallery.
