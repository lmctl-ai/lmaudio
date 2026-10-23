# 08 — Limitations & Resource Boundaries

- **Offline Batch Processing Only**: `lmaudio` processes finite audio buffers in memory and writes WAV/CSV files; it does not open real-time OS audio hardware devices (ALSA/CoreAudio/WASAPI) or MIDI hardware ports.
- **Sample Rate & Frame Bounds**: Sample rate `sr` must be in `[8000, 192000] Hz`, channel count in `[1, 8]`, and buffer length in `[1, 200000000]` frames (`<= 300 s` per oscillator call).
- **FFT Size Bound**: `Spectrum` supports `nfft` in `[2, 16777216]`. Buffers longer than `16777216` frames must pass an explicit `nfft` (or `slice` first).
- **WAV Codec Scope**: `load_wav` and `save_wav` support uncompressed 16-bit PCM, 24-bit PCM, and 32-bit IEEE float RIFF/WAVE files (including `WAVE_FORMAT_EXTENSIBLE` `0xFFFE`). Compressed formats (MP3, FLAC, Ogg, AAC) are out of scope.
- **Memory Budget**: Bounded by `LMAUDIO_MAX_MEMORY_MB` (default `4096 MiB`), enforced across both C++ heap allocations and Soundpipe native allocations.
