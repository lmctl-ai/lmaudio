# `lmaudio` — Durable Memory Index

This directory records the architecture, API design, DSP algorithms, visual verification, testing discipline, build/CI pipeline, key decisions, and boundaries of **`lmaudio`** (`lmctl-ai/lmaudio`).

## Chapters

1. [01 — Purpose & Scope](01-purpose.md): What `lmaudio` is, what problems it solves, and why it exists in the `lmscript` ecosystem.
2. [02 — Architecture & DSP Core](02-architecture.md): Source layout, handle table (`Buffer`, `Spectrum`, `Spectrogram`, `Biquad`), embedded C++ engines (DaisySP, clean Soundpipe subset, Bluestein/Cooley-Tukey FFT, RBJ biquads, YIN pitch, windowed-sinc resampler), and memory budgeting.
3. [03 — DSL API Reference](03-api.md): Complete `audio.*` module functions and method signatures across all four handle types.
4. [04 — Visuals & Gallery Pipeline](04-Visuals.md): How `lmaudio` pairs with `lmplot` to render spectrogram heatmaps, Bode plots, RT60 decay curves, pitch trajectories, and companion WAV files.
5. [05 — Verification & Testing](05-testing.md): Numerical/analytical DSP invariants (Parseval's theorem, RBJ transfer function vs time-domain steady state, YIN pitch, WAV SNR, determinism) and `test.sh` coverage.
6. [06 — Build, Static Linking & 5-Platform CI](06-build-and-ci.md): CMake build, dependency pinning (`deps.lock`, `sdk.lock`), static binary linking across Linux/macOS/Windows, and release workflow.
7. [07 — Architectural & License Decisions](07-decisions.md): Why KFR/FFTW/libsndfile and Csound/GPL Soundpipe modules were excluded, and how clean-room MIT replacements were designed.
8. [08 — Limitations & Resource Boundaries](08-limitations.md): Frame/sample-rate bounds, `LMAUDIO_MAX_MEMORY_MB` budget enforcement, offline batch execution model, and WAV codec scope.
