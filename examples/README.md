# lmaudio Examples

Every example in `examples/` is self-executing: running it via `lmctl autorun <url>` or `lmscript examples/<name>.lms` compiles and executes the `lmaudio` synthesis/analysis block, writes the playable `.wav` file to `audio/<name>.wav`, and invokes `lmplot` to render the companion visualization into `images/<name>.png` (or `.gif`).

| Example | Description | Run Command |
|---|---|---|
| `polyblep_harmonics.lms` | Band-limited PolyBLEP sine, triangle, square, and sawtooth waveforms and Fourier harmonic spectra at 440 Hz | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/polyblep_harmonics.lms` |
| `karplus_strong_harp.lms` | Karplus-Strong plucked string physical model, damping comparison, and Celtic harp arpeggio STFT spectrogram | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/karplus_strong_harp.lms` |
| `fm_bell_spectrum.lms` | Chowning 2-operator FM synthesis Bessel sideband evolution and tubular bell / brass synthesis | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/fm_bell_spectrum.lms` |
| `synth_lead_melody.lms` | Synthwave lead melody, arpeggiated Moog bassline, and chord progression from `audio.sequence` note strings | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/synth_lead_melody.lms` |
| `biquad_filter_bank.lms` | RBJ Audio-EQ-Cookbook biquad filter bank analytical Bode magnitude/phase curves and filtered chord showcase | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/biquad_filter_bank.lms` |
| `eq_before_after.lms` | 4-band parametric mastering EQ before vs after STFT spectrograms, transfer curves, and FFT spectrum | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/eq_before_after.lms` |
| `moog_ladder_sweep.lms` | Huovilainen 24 dB/oct nonlinear Moog ladder filter resonant cutoff sweep spectrogram and spectral snapshots | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/moog_ladder_sweep.lms` |
| `chirp_spectrogram.lms` | Linear vs exponential (octave-rate) swept-sine chirp STFT spectrograms and ridge frequency tracking | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/chirp_spectrogram.lms` |
| `analog_drum_kit.lms` | 808 analog bass drum, snare, and metallic hi-hat transients, spectral envelopes, and 2-bar breakbeat groove | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/analog_drum_kit.lms` |
| `reverb_impulse_response.lms` | Studio 8-line Hadamard FDN reverb impulse response, RT60 log-decay slopes, and early vs late spectra | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/reverb_impulse_response.lms` |
| `vocal_pitch_tracking.lms` | YIN fundamental pitch tracking of a sung vibrato formant chirp and melody with unvoiced silence gaps | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/vocal_pitch_tracking.lms` |
| `loudness_and_resampling.lms` | Peak vs RMS loudness normalization and Kaiser-windowed sinc sample-rate conversion quality | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/loudness_and_resampling.lms` |
| `reverb_and_wavefolder.lms` | West-Coast Buchla wavefolding harmonic distortion and studio FDN reverb | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/reverb_and_wavefolder.lms` |
| `harmonic_morph_anim.lms` | 24-frame animated GIF (`ffmpeg`) of simultaneous wavefolding and ladder filter cutoff morphing | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/harmonic_morph_anim.lms` |

To regenerate all `docs/images/*` and `docs/audio/*` assets at once:

```sh
./examples/render-all.sh
```
