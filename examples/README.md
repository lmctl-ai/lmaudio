# lmaudio examples

Each example synthesizes its own input, asserts a known answer, writes `audio/<name>.wav` and draws `images/<name>.png` (`.gif` for the animation) with lmplot (and lmdraw for diagrams) in the current directory. Run one from an empty directory:

| Example | What it shows | Run |
|---|---|---|
| [`synthesis_methods_compared.lms`](synthesis_methods_compared.lms) | Three roads to one note: subtractive, additive, FM | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/synthesis_methods_compared.lms` |
| [`polyblep_harmonics.lms`](polyblep_harmonics.lms) | Band-limited PolyBLEP oscillators | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/polyblep_harmonics.lms` |
| [`fm_bell_spectrum.lms`](fm_bell_spectrum.lms) | FM sidebands follow Bessel functions | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/fm_bell_spectrum.lms` |
| [`synth_lead_melody.lms`](synth_lead_melody.lms) | Melody, bassline and chords from note strings | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/synth_lead_melody.lms` |
| [`granular_texture.lms`](granular_texture.lms) | Granular time-stretch | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/granular_texture.lms` |
| [`harmonic_morph_anim.lms`](harmonic_morph_anim.lms) | Animated wavefolding and ladder-filter morph | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/harmonic_morph_anim.lms` |
| [`karplus_strong_tuning.lms`](karplus_strong_tuning.lms) | Karplus-Strong string: an allpass fixes the tuning | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/karplus_strong_tuning.lms` |
| [`xylophone_bar_modes.lms`](xylophone_bar_modes.lms) | Struck bars: the supports set the overtones | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/xylophone_bar_modes.lms` |
| [`karplus_strong_harp.lms`](karplus_strong_harp.lms) | Plucked-string harp | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/karplus_strong_harp.lms` |
| [`drum_machine_pattern.lms`](drum_machine_pattern.lms) | A drum machine from text | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/drum_machine_pattern.lms` |
| [`analog_drum_kit.lms`](analog_drum_kit.lms) | 808 drum voices | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/analog_drum_kit.lms` |
| [`biquad_filter_bank.lms`](biquad_filter_bank.lms) | RBJ biquad filter bank | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/biquad_filter_bank.lms` |
| [`eq_before_after.lms`](eq_before_after.lms) | Four-band mastering EQ, before and after | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/eq_before_after.lms` |
| [`moog_ladder_sweep.lms`](moog_ladder_sweep.lms) | Moog ladder filter sweep | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/moog_ladder_sweep.lms` |
| [`compressor_transfer_curve.lms`](compressor_transfer_curve.lms) | Compressor: transfer curve and attack/release | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/compressor_transfer_curve.lms` |
| [`reverb_and_wavefolder.lms`](reverb_and_wavefolder.lms) | Wavefolding into a reverb | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/reverb_and_wavefolder.lms` |
| [`reverb_impulse_response.lms`](reverb_impulse_response.lms) | Feedback-delay-network reverb, measured | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/reverb_impulse_response.lms` |
| [`loudness_normalization_lufs.lms`](loudness_normalization_lufs.lms) | Loudness normalization (LUFS) | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/loudness_normalization_lufs.lms` |
| [`sample_rate_conversion_quality.lms`](sample_rate_conversion_quality.lms) | Sample-rate conversion without aliasing | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/sample_rate_conversion_quality.lms` |
| [`vocal_pitch_tracking.lms`](vocal_pitch_tracking.lms) | YIN pitch tracking of a sung glide | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/vocal_pitch_tracking.lms` |
| [`chirp_spectrogram.lms`](chirp_spectrogram.lms) | Linear vs exponential chirps | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/chirp_spectrogram.lms` |

Pictures, sounds and longer descriptions: the [README gallery](../README.md#gallery). To regenerate every `docs/images/*` and `docs/audio/*` file:

```sh
examples/render-all.sh
```
