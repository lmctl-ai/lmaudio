# lmaudio

Self-contained static C++17 audio synthesis, physical modeling, filtering, effects, spectral analysis (FFT/STFT), and WAV scripting host for `lmscript` (`_dsl_`), combining **DaisySP** (MIT), **Soundpipe** (MIT), and an original MIT DSP/FFT/STFT/WAV/YIN engine.

## Visual and Audio Gallery

Every image and `.wav` file below is generated deterministically by an `.lms` script in `examples/` (`lmaudio` synthesizes and analyzes the signal, exports `.wav` audio and `.csv` data, and `lmplot` renders the companion plots). Run any example directly from an empty directory with `lmctl autorun`:

### Band-Limited PolyBLEP Oscillators and Fourier Harmonics

![Band-Limited PolyBLEP Oscillators at 440 Hz](docs/images/polyblep_harmonics.png)

Audio: [`docs/audio/polyblep_harmonics.wav`](docs/audio/polyblep_harmonics.wav) (22.05 kHz, 16-bit mono, 113 KB)

```sh
lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/polyblep_harmonics.lms
```

### Karplus-Strong Plucked String Physical Modeling and Harp Arpeggio

![Karplus-Strong Plucked String and Harp Spectrogram](docs/images/karplus_strong_harp.png)

Audio: [`docs/audio/karplus_strong_harp.wav`](docs/audio/karplus_strong_harp.wav) (22.05 kHz, 16-bit mono, 130 KB)

```sh
lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/karplus_strong_harp.lms
```

### Chowning 2-Operator FM Synthesis and Bessel Sideband Evolution

![2-Operator FM Synthesis Bessel Sidebands](docs/images/fm_bell_spectrum.png)

Audio: [`docs/audio/fm_bell_spectrum.wav`](docs/audio/fm_bell_spectrum.wav) (22.05 kHz, 16-bit mono, 117 KB)

```sh
lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/fm_bell_spectrum.lms
```

### RBJ Audio-EQ-Cookbook Biquad Filter Bank (Analytical Bode Magnitude and Phase)

![RBJ Biquad Filter Bank Bode Response](docs/images/biquad_filter_bank.png)

Audio: [`docs/audio/biquad_filter_bank.wav`](docs/audio/biquad_filter_bank.wav) (22.05 kHz, 16-bit mono, 121 KB)

```sh
lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/biquad_filter_bank.lms
```

### Huovilainen 24 dB/oct Moog Ladder Filter Resonance Sweep

![Moog Ladder Filter Sweep Spectrogram and Spectra](docs/images/moog_ladder_sweep.png)

Audio: [`docs/audio/moog_ladder_sweep.wav`](docs/audio/moog_ladder_sweep.wav) (22.05 kHz, 16-bit mono, 121 KB)

```sh
lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/moog_ladder_sweep.lms
```

### Linear vs Exponential Swept-Sine Chirp STFT Spectrograms

![Linear vs Exponential Chirp STFT Spectrograms](docs/images/chirp_spectrogram.png)

Audio: [`docs/audio/chirp_spectrogram.wav`](docs/audio/chirp_spectrogram.wav) (22.05 kHz, 16-bit mono, 113 KB)

```sh
lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/chirp_spectrogram.lms
```

### 808 Analog and Synthetic Drum Synthesis and Breakbeat Groove

![808 Analog Drum Transients, Spectra, and Breakbeat Groove](docs/images/analog_drum_kit.png)

Audio: [`docs/audio/analog_drum_kit.wav`](docs/audio/analog_drum_kit.wav) (22.05 kHz, 16-bit mono, 134 KB)

```sh
lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/analog_drum_kit.lms
```

### West-Coast Buchla Wavefolding and Zita-Rev1 Studio FDN Reverb

![Buchla Wavefolding and Zita-Rev1 FDN Reverb](docs/images/reverb_and_wavefolder.png)

Audio: [`docs/audio/reverb_and_wavefolder.wav`](docs/audio/reverb_and_wavefolder.wav) (22.05 kHz, 16-bit mono, 121 KB)

```sh
lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/reverb_and_wavefolder.lms
```

### Animated Wavefolding and Resonant Ladder Filter Morphing

![Animated Wavefolding and Ladder Filter Morph](docs/images/harmonic_morph_anim.gif)

Audio: [`docs/audio/harmonic_morph_anim.wav`](docs/audio/harmonic_morph_anim.wav) (22.05 kHz, 16-bit mono, 125 KB)

```sh
lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmaudio/main/examples/harmonic_morph_anim.lms
```

---

## Quick Start

```js
mkdir("audio");
a = _dsl_ cli="lmaudio" install="github:lmctl-ai/lmaudio@latest";
let arp = audio.sequence("C4:0.5 E4:0.5 G4:0.5 C5:1.5", 120.0, "pluck", 22050);
let out = arp.reverb(0.82, 6500.0, 0.28).normalize(0.92);
audio.assert_close(out.peak(), 0.92, 0.001);
out.save_wav("audio/quickstart.wav", 16);
end _dsl_
savedsl(a, "build/quickstart.dslb");
r = system ${dsl_cli(a)} --dsl build/quickstart.dslb;
assert(r.code == 0);
```

## Embedded Libraries and License Compliance

`lmaudio` compiles into a single self-contained executable with zero runtime shared-library dependencies:

| Component | Upstream / Origin | License | Role in `lmaudio` |
|---|---|---|---|
| **DaisySP** | `electro-smith/DaisySP` (`2c72eaf`) | MIT | Band-limited PolyBLEP oscillators, 2-op FM (`Fm2`), Karplus-Strong `StringVoice`, `ModalVoice`, Huovilainen 24 dB/oct `LadderFilter`, `Svf`, `Adsr`, 808/synth drums (`AnalogBassDrum`, `SyntheticBassDrum`, `AnalogSnareDrum`, `SyntheticSnareDrum`, `HiHat`), `Chorus`, `Flanger`, `Phaser`, `Overdrive`, `Wavefolder` |
| **Soundpipe** | `PaulBatchelor/Soundpipe` (`3efb43b`) | MIT | Sean Costello 8-line FDN reverb (`revsc`), Fons Adriaensen studio FDN reverb (`zitarev`), struck bar (`bar`), dynamic range `compressor`, `bitcrush`, spectral `paulstretch` |
| **DSP / FFT / STFT / WAV Engine** | `cpp/audio.{hpp,cpp}` | MIT | Cooley-Tukey radix-2 + Bluestein chirp-z arbitrary-length FFT/IFFT, 2D STFT spectrogram, RBJ Audio-EQ-Cookbook biquads with exact analytical transfer function evaluation, YIN fundamental pitch estimator, windowed-sinc resampling, scientific-pitch sequence/chord renderer, and PCM16/PCM24/FLOAT32 RIFF WAVE I/O |

> **Why KFR, FFTW, and libsndfile were replaced:** `info.txt` listed `kfr` (dual GPL-2.0+ / commercial), `fftw` (GPL-2.0+), and `libsndfile` (LGPL-2.1+). Static linking against GPL/LGPL libraries is incompatible with an MIT-licensed single-file binary distribution, so `lmaudio` implements its own clean-room MIT radix-2 + Bluestein FFT/IFFT/STFT engine, RBJ biquad filter bank, windowed-sinc resampler, and PCM16/PCM24/FLOAT32 WAV reader/writer alongside MIT-licensed **DaisySP** and **Soundpipe** (`-DNO_LIBSNDFILE`). Also note that DaisySP's optional `DaisySP-LGPL` submodule is excluded.

---

## DSL Reference (`context audio: Audio`)

### `Audio` Synthesis, Design, and I/O

- `audio.silence(sr: int, duration: float) -> Buffer` / `audio.silence(duration: float) -> Buffer`
- `audio.sine(freq: float, duration: float, amp: float, sr: int) -> Buffer` (also 2- and 3-arg overloads at `sr = 44100`)
- `audio.cosine(freq: float, duration: float, amp: float, sr: int) -> Buffer`
- `audio.saw(freq: float, duration: float, amp: float, sr: int) -> Buffer` — PolyBLEP band-limited sawtooth
- `audio.square(freq: float, duration: float, amp: float, duty: float, sr: int) -> Buffer` — PolyBLEP band-limited pulse/square
- `audio.triangle(freq: float, duration: float, amp: float, sr: int) -> Buffer` — triangle oscillator
- `audio.chirp(f0: float, f1: float, duration: float, amp: float, mode: string, sr: int) -> Buffer` — `"linear"` or `"log"` swept sine
- `audio.impulse(duration: float, offset_sec: float, amp: float, sr: int) -> Buffer` — unit impulse at `offset_sec`
- `audio.noise(kind: string, duration: float, amp: float, seed: int, sr: int) -> Buffer` — deterministic `"white"`, `"pink"`, or `"brown"` noise
- `audio.expr(formula: string, duration: float, sr: int) -> Buffer` — sample-by-sample math expression in `t`, `n`, `sr`, `dur`
- `audio.fm(carrier: float, ratio: float, index: float, duration: float, amp: float, sr: int) -> Buffer` — Chowning 2-operator FM with modulation index $\beta$ in radians
- `audio.pluck(freq: float, duration: float, amp: float, brightness: float, damping: float, sr: int) -> Buffer` — Karplus-Strong plucked string
- `audio.modal(freq: float, duration: float, amp: float, structure: float, brightness: float, damping: float, sr: int) -> Buffer` — struck modal resonator
- `audio.bar(bc_left: int, bc_right: int, stiffness: float, decay: float, duration: float, amp: float, sr: int) -> Buffer` — Soundpipe struck physical bar
- `audio.drum(kind: string, freq: float, duration: float, decay: float, tone: float, accent: float, sr: int) -> Buffer` — `"kick"`, `"synth_kick"`, `"snare"`, `"synth_snare"`, or `"hihat"`
- `audio.sequence(notes: string, bpm: float, instrument: string, sr: int) -> Buffer` — renders space-separated tokens `"C4:1 E4:0.5 R:0.5 C4+E4+G4:2"` with instrument `"sine"`, `"saw"`, `"square"`, `"triangle"`, `"fm"`, `"pluck"`, `"modal"`, or `"pad"`
- `audio.note_freq(note: string) -> float` / `audio.midi_freq(midi: float) -> float`
- `audio.load_wav(path: string) -> Buffer` / `audio.stereo(left: Buffer, right: Buffer) -> Buffer`
- `audio.biquad_design(kind: string, freq: float, q: float, db_gain: float, sr: int) -> Biquad` — `"lowpass"`, `"highpass"`, `"bandpass"`, `"notch"`, `"peak"`, `"lowshelf"`, `"highshelf"`, `"allpass"`
- `audio.assert_close(actual: float, expected: float, tol: float) -> void`

### `Buffer` Processing, Effects, Analysis, and Export

- **Inspection**: `.sr()`, `.channels()`, `.frames()`, `.duration()`, `.get(frame[, channel])`, `.channel(ch)`, `.to_mono()`
- **Gain & Envelopes**: `.gain(factor)`, `.gain_db(db)`, `.normalize([target_peak])`, `.normalize_rms(target_rms)`, `.fade(in_sec, out_sec[, shape])`, `.adsr(attack, decay, sustain, release[, gate_sec])`, `.pan(pos)`
- **Editing**: `.slice(start_sec, end_sec)`, `.pad(before_sec, after_sec)`, `.concat(other)`, `.mix(other[, gain[, offset_sec]])`, `.reverse()`, `.resample(new_sr)`
- **Filters & Effects**: `.biquad(kind, freq, q[, db_gain])`, `.svf(mode, cutoff, res[, drive])`, `.ladder(cutoff, res[, drive, mode])`, `.lpf_sweep(f0, f1, res)`, `.delay(delay_sec, feedback, wet)`, `.reverb(feedback, lpf_freq, wet)`, `.zitarev(rt60_low, rt60_mid, damping, wet)`, `.chorus(rate, depth, feedback, wet)`, `.flanger(rate, depth, feedback, wet)`, `.phaser(rate, depth, feedback, poles, wet)`, `.overdrive(drive, wet)`, `.wavefold(gain, offset)`, `.bitcrush(bitdepth, crush_sr)`, `.compress(ratio, thresh_db, attack, release, makeup_db)`, `.paulstretch(stretch, window_sec)`
- **Analysis**: `.rms([ch])`, `.peak([ch])`, `.dc_offset([ch])`, `.energy([ch])`, `.zcr([ch])`, `.pitch([fmin, fmax])`, `.peak_freq([window])`, `.centroid([window])`, `.thd([fundamental_hz, harmonics])`, `.snr_db(reference)`, `.spectrum([window[, nfft]])`, `.spectrogram(win_size, hop_size[, window, max_freq])`
- **Export**: `.save_wav(path[, bits])` (`bits = 16`, `24`, or `32`), `.save_csv(path[, max_points[, stride]])`

### `Spectrum`, `Spectrogram`, and `Biquad`

- **`Spectrum`**: `.bins()`, `.nfft()`, `.sr()`, `.freq_at(bin)`, `.mag_at(bin)`, `.db_at(bin)`, `.phase_at(bin)`, `.mag_at_freq(freq_hz)`, `.peak_bin()`, `.peak_freq()`, `.peak_mag()`, `.centroid()`, `.parseval_energy()`, `.ifft()`, `.save_csv(path[, max_freq])`
- **`Spectrogram`**: `.frames()`, `.bins()`, `.sr()`, `.time_at(frame)`, `.freq_at(bin)`, `.mag_at(frame, bin)`, `.db_at(frame, bin)`, `.ridge_freq(frame)`, `.save_csv(path[, floor_db])`
- **`Biquad`**: `.b0()`, `.b1()`, `.b2()`, `.a1()`, `.a2()`, `.gain_at(freq_hz)`, `.db_at(freq_hz)`, `.phase_deg_at(freq_hz)`, `.apply(buf)`, `.save_response_csv(path[, points, fmin, fmax])`

---

## Command-Line Interface and Memory Budget

```
usage: lmaudio --version | --notices | --dsl-schema [--json] | --dsl-signatures | [--dry-run] --dsl FILE [FILE ...]
environment: LMAUDIO_MAX_MEMORY_MB  total memory budget in MiB (default 4096); a program that needs more
             stops with a run error (exit 3) instead of allocating
```

## Building and Testing

```sh
scripts/sync-deps.sh
scripts/sync-sdk.sh --verify
./test.sh
```
