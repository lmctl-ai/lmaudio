// lmaudio — audio DSP, synthesis, spectral analysis, and WAV I/O (MIT).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lmaudio {

struct Biquad {
  double b0 = 1.0, b1 = 0.0, b2 = 0.0;
  double a1 = 0.0, a2 = 0.0;
  int sr = 44100;
  std::string kind = "lowpass";
  double freq = 1000.0;
  double q = 0.7071067811865475;
  double db_gain = 0.0;

  double gain_at(double freq_hz) const;
  double db_at(double freq_hz) const;
  double phase_deg_at(double freq_hz) const;
  void save_response_csv(const std::string &path, int64_t points, double fmin, double fmax, bool dry_run) const;
};

struct Spectrum {
  int sr = 44100;
  int nfft = 0;
  double coherent_gain = 1.0;
  std::string window = "hann";
  // Full complex spectrum (length nfft) for exact Parseval energy and IFFT reconstruction.
  std::vector<double> re;
  std::vector<double> im;

  int64_t bins() const { return nfft / 2 + 1; }
  double freq_at(int64_t bin) const;
  double mag_at(int64_t bin) const;
  double db_at(int64_t bin) const;
  double phase_at(int64_t bin) const;
  double mag_at_freq(double freq_hz) const;
  int64_t peak_bin() const;
  double peak_freq() const;
  double peak_mag() const;
  double centroid() const;
  double parseval_energy() const;
  void save_csv(const std::string &path, double max_freq, bool dry_run) const;
};

struct Spectrogram {
  int sr = 44100;
  int win_size = 1024;
  int hop_size = 256;
  int n_frames = 0;
  int n_bins = 0;
  std::vector<double> times;
  std::vector<double> freqs;
  // Row-major [frame * n_bins + bin] linear single-sided amplitude.
  std::vector<float> mags;

  double time_at(int64_t frame) const;
  double freq_at(int64_t bin) const;
  double mag_at(int64_t frame, int64_t bin) const;
  double db_at(int64_t frame, int64_t bin) const;
  double ridge_freq(int64_t frame) const;
  void save_csv(const std::string &path, double floor_db, bool dry_run) const;
};

struct Buffer {
  int sr = 44100;
  int channels = 1;
  // Planar channel storage: channel c at frame f is data[c * frames + f].
  int64_t frames = 0;
  std::vector<float> data;

  Buffer() = default;
  Buffer(int sr_, int channels_, int64_t frames_);

  double duration() const { return sr > 0 ? double(frames) / double(sr) : 0.0; }
  float at(int64_t f, int c = 0) const { return data[std::size_t(c) * std::size_t(frames) + std::size_t(f)]; }
  float &at(int64_t f, int c = 0) { return data[std::size_t(c) * std::size_t(frames) + std::size_t(f)]; }
  const float *chan_ptr(int c = 0) const { return data.data() + std::size_t(c) * std::size_t(frames); }
  float *chan_ptr(int c = 0) { return data.data() + std::size_t(c) * std::size_t(frames); }
};

// Synthesis & I/O
Buffer make_silence(int64_t sr, double duration);
Buffer make_sine(double freq, double duration, double amp, int64_t sr);
Buffer make_cosine(double freq, double duration, double amp, int64_t sr);
Buffer make_saw(double freq, double duration, double amp, int64_t sr);
Buffer make_square(double freq, double duration, double amp, double duty, int64_t sr);
Buffer make_triangle(double freq, double duration, double amp, int64_t sr);
Buffer make_chirp(double f0, double f1, double duration, double amp, const std::string &mode, int64_t sr);
Buffer make_impulse(double duration, double offset_sec, double amp, int64_t sr);
Buffer make_noise(const std::string &kind, double duration, double amp, int64_t seed, int64_t sr);
Buffer make_expr(const std::string &formula, double duration, int64_t sr);
Buffer make_fm(double carrier, double ratio, double index, double duration, double amp, int64_t sr);
Buffer make_pluck(double freq, double duration, double amp, double brightness, double damping, int64_t sr);
Buffer make_modal(double freq, double duration, double amp, double structure, double brightness, double damping, int64_t sr);
Buffer make_bar(int64_t bc_left, int64_t bc_right, double stiffness, double decay, double duration, double amp, int64_t sr);
Buffer make_drum(const std::string &kind, double freq, double duration, double decay, double tone, double accent, int64_t sr);
Buffer make_sequence(const std::string &notes, double bpm, const std::string &instrument, int64_t sr);

double note_to_freq(const std::string &note);
double midi_to_freq(double midi);

Buffer load_wav(const std::string &path);
void save_wav(const Buffer &buf, const std::string &path, int64_t bits, bool dry_run);
void save_buffer_csv(const Buffer &buf, const std::string &path, int64_t max_points, int64_t stride, bool dry_run);

Buffer make_stereo(const Buffer &left, const Buffer &right);
Buffer extract_channel(const Buffer &buf, int64_t ch);
Buffer to_mono(const Buffer &buf);

// Editing, mixing & envelopes
Buffer apply_gain(const Buffer &buf, double factor);
Buffer apply_gain_db(const Buffer &buf, double db);
Buffer apply_normalize(const Buffer &buf, double target_peak);
Buffer apply_normalize_rms(const Buffer &buf, double target_rms);
Buffer apply_fade(const Buffer &buf, double fade_in_sec, double fade_out_sec, const std::string &shape);
Buffer apply_adsr(const Buffer &buf, double attack, double decay, double sustain, double release, double gate_sec);
Buffer apply_pan(const Buffer &buf, double pos);
Buffer apply_slice(const Buffer &buf, double start_sec, double end_sec);
Buffer apply_pad(const Buffer &buf, double before_sec, double after_sec);
Buffer apply_concat(const Buffer &a, const Buffer &b);
Buffer apply_mix(const Buffer &a, const Buffer &b, double b_gain, double offset_sec);
Buffer apply_reverse(const Buffer &buf);
Buffer apply_resample(const Buffer &buf, int64_t new_sr);

// Filters & effects
Biquad design_biquad(const std::string &kind, double freq, double q, double db_gain, int64_t sr);
Buffer apply_biquad(const Buffer &buf, const Biquad &bq);
Buffer apply_svf(const Buffer &buf, const std::string &mode, double cutoff, double res, double drive);
Buffer apply_ladder(const Buffer &buf, double cutoff, double res, double drive, const std::string &mode);
Buffer apply_lpf_sweep(const Buffer &buf, double f0, double f1, double res);
Buffer apply_delay(const Buffer &buf, double delay_sec, double feedback, double wet);
Buffer apply_reverb(const Buffer &buf, double feedback, double lpf_freq, double wet);
Buffer apply_zitarev(const Buffer &buf, double rt60_low, double rt60_mid, double damping, double wet);
Buffer apply_chorus(const Buffer &buf, double rate, double depth, double feedback, double wet);
Buffer apply_flanger(const Buffer &buf, double rate, double depth, double feedback, double wet);
Buffer apply_phaser(const Buffer &buf, double rate, double depth, double feedback, int64_t poles, double wet);
Buffer apply_overdrive(const Buffer &buf, double drive, double wet);
Buffer apply_wavefold(const Buffer &buf, double gain, double offset);
Buffer apply_bitcrush(const Buffer &buf, int64_t bitdepth, double crush_sr);
Buffer apply_compress(const Buffer &buf, double ratio, double thresh_db, double attack, double release, double makeup_db);
Buffer apply_paulstretch(const Buffer &buf, double stretch, double window_sec);

// Analysis & spectral transforms
double compute_rms(const Buffer &buf, int64_t ch);
double compute_peak(const Buffer &buf, int64_t ch);
double compute_dc_offset(const Buffer &buf, int64_t ch);
double compute_energy(const Buffer &buf, int64_t ch);
double compute_zcr(const Buffer &buf, int64_t ch);
double compute_pitch(const Buffer &buf, double fmin, double fmax, int64_t ch);
double compute_peak_freq(const Buffer &buf, const std::string &window, int64_t ch);
double compute_centroid(const Buffer &buf, const std::string &window, int64_t ch);
double compute_thd(const Buffer &buf, double fundamental_hz, int64_t harmonics, int64_t ch);
double compute_snr_db(const Buffer &buf, const Buffer &ref, int64_t ch);

Spectrum compute_spectrum(const Buffer &buf, const std::string &window, int64_t nfft, int64_t ch);
Buffer compute_ifft(const Spectrum &spec);
Spectrogram compute_spectrogram(const Buffer &buf, int64_t win_size, int64_t hop_size, const std::string &window, double max_freq, int64_t ch);

}  // namespace lmaudio
