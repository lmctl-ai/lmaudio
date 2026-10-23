# lmaudio Examples

Every example in `examples/` is self-executing: running it via `lmctl autorun <url>` or `lmscript examples/<name>.lms` compiles and executes the `lmaudio` synthesis/analysis block, writes the playable `.wav` file to `audio/<name>.wav`, and invokes `lmplot` to render the companion visualization into `images/<name>.png` (or `.gif`).

| Example | Description | Run Command |
|---|---|---|
| `polyblep_harmonics.lms` | Band-limited PolyBLEP sine, triangle, square, and sawtooth waveforms and Fourier harmonic spectra at 440 Hz | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/polyblep_harmonics.lms` |
| `karplus_strong_harp.lms` | Karplus-Strong plucked string physical model, damping comparison, and Celtic harp arpeggio STFT spectrogram | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/karplus_strong_harp.lms` |
| `fm_bell_spectrum.lms` | Chowning 2-operator FM synthesis Bessel sideband evolution and tubular bell / brass synthesis | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/fm_bell_spectrum.lms` |
| `biquad_filter_bank.lms` | RBJ Audio-EQ-Cookbook biquad filter bank analytical Bode magnitude/phase curves and filtered chord showcase | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/biquad_filter_bank.lms` |
| `moog_ladder_sweep.lms` | Huovilainen 24 dB/oct nonlinear Moog ladder filter resonant cutoff sweep spectrogram and spectral snapshots | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/moog_ladder_sweep.lms` |
| `chirp_spectrogram.lms` | Linear vs exponential (octave-rate) swept-sine chirp STFT spectrograms and ridge frequency tracking | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/chirp_spectrogram.lms` |
| `analog_drum_kit.lms` | 808 analog bass drum, snare, and metallic hi-hat transients, spectral envelopes, and 2-bar breakbeat groove | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/analog_drum_kit.lms` |
| `reverb_and_wavefolder.lms` | West-Coast Buchla wavefolding harmonic distortion and Zita-Rev1 / FDN studio reverb impulse response | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/reverb_and_wavefolder.lms` |
| `harmonic_morph_anim.lms` | 24-frame animated GIF (`ffmpeg`) of simultaneous wavefolding and ladder filter cutoff morphing | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/harmonic_morph_anim.lms` |

To regenerate all `docs/images/*` and `docs/audio/*` assets at once:

```sh
./examples/render-all.sh
```
