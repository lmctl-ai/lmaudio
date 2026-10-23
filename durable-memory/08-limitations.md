# 08 — Limitations & Resource Boundaries

- **Offline Batch Processing Only**: `lmaudio` processes finite audio buffers in memory and writes WAV/CSV files; it does not open real-time OS audio hardware devices (ALSA/CoreAudio/WASAPI) or MIDI hardware ports.
- **Sample Rate & Frame Bounds**: Sample rate `sr` must be in `[100, 384000] Hz`, mono or stereo only, buffer length in `[1, 200000000]` frames, and a single synthesis call lasts at most 3600 s.
- **FFT Size Bound**: `Spectrum` supports `nfft` in `[2, 16777216]`. Buffers longer than `16777216` frames must pass an explicit `nfft` (or `slice` first).
- **WAV Codec Scope**: `load_wav` and `save_wav` support uncompressed 16-bit PCM, 24-bit PCM, and 32-bit IEEE float RIFF/WAVE files (including `WAVE_FORMAT_EXTENSIBLE` `0xFFFE`). Compressed formats (MP3, FLAC, Ogg, AAC) are out of scope.
- **Memory Budget**: Bounded by `LMAUDIO_MAX_MEMORY_MB` (default `4096 MiB`), enforced across both C++ heap allocations and Soundpipe native allocations.
- **Pitch**: one YIN estimate per call; slightly inharmonic tones (plucked strings) bias it by a few cents in the top
  octave at 22.05 kHz. Short periods (< 64 samples) are refined on an upsampled copy. Use `peak_freq_in` for tuning work.
- **Bar**: stiffness is capped by the stable finite-difference grid (N >= 6 points); mode frequencies of very stiff bars
  carry a few percent of numerical dispersion.
- **LUFS**: channel weights 1.0 (no surround), sr >= 8 kHz; K-weighting drifts ~0.03 dB at 22.05 kHz.
