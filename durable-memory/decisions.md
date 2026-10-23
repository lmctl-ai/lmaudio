# Engineering Decisions & Findings

## 1. License Audit: Replacing KFR, FFTW, and libsndfile
- `info.txt` suggested `kfr`, `daisysp`, `soundpipe`, `fftw`, and `libsndfile`.
- License check:
  - `daisysp` (`Source/`): MIT (excluding `DaisySP-LGPL`).
  - `soundpipe`: MIT (when compiled with `-DNO_LIBSNDFILE`).
  - `kfr`: Dual GPL-2.0+ / commercial.
  - `fftw`: GPL-2.0+.
  - `libsndfile`: LGPL-2.1+.
- Because `lmaudio` links everything into a single standalone static executable under the MIT license, linking `kfr`, `fftw`, or `libsndfile` was prohibited. We implemented a clean-room MIT engine in `cpp/audio.{hpp,cpp}` covering radix-2 + Bluestein arbitrary-$N$ FFT/IFFT/STFT, RBJ biquads, windowed-sinc resampling, and PCM16/PCM24/FLOAT32 RIFF WAVE I/O.

## 2. Deterministic Physical Modeling & Drum Synthesis
- Several `DaisySP` physical modeling and drum modules (`KarplusString`, `StringVoice`, `AnalogSnareDrum`, `SyntheticSnareDrum`, `SyntheticBassDrum`, `HiHat`) call standard C `rand()` for excitation noise bursts, while `Soundpipe` uses `sp_rand(sp)`.
- To guarantee 100% deterministic, byte-identical output across runs and independent of call order, every wrapper resets `std::srand(12345)` (and `sp_srand(sp, 12345)`) prior to initializing and rendering each voice.

## 3. DaisySP `Fm2` Chowning Modulation Index Calibration
- `daisysp::Fm2::Process()` accumulates `modval * idx_` into the carrier phase on every sample (`car_.PhaseAdd(modval * idx_)`), where `SetIndex(x)` sets `idx_ = 0.2 * x`.
- Because phase is accumulated per sample, the effective modulation index in radians is $\beta = 2\pi \cdot \text{idx\_} \cdot \frac{f_s}{2\pi f_{\text{mod}}}$. Scaling the argument passed to `SetIndex` by `5.0 * f_mod / sr` calibrates `audio.fm(carrier, ratio, index, ...)` so `index` equals the true Chowning modulation index $\beta$ in radians, matching analytical Bessel sideband amplitudes $A |J_k(\beta)|$.

## 4. DaisySP `Oscillator` Initialization
- `daisysp::Oscillator::Init()` does not initialize its private `last_out_` member. All `daisysp::Oscillator` instances in `cpp/audio.cpp` are value-initialized (`daisysp::Oscillator osc{};`) before calling `Init()`.
