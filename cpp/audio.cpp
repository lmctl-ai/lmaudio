// lmaudio — audio DSP, synthesis, spectral analysis, and WAV I/O (MIT).
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "audio.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "daisysp.h"
#include "expr.hpp"
#include "memory_budget.hpp"

extern "C" {
#include "soundpipe.h"
}

namespace lmaudio {
namespace {

constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kTwoPi = 2.0 * kPi;

void fail(const std::string &msg) { throw std::runtime_error("lmaudio: " + msg); }

void check_finite(double v, const char *name) {
  if (!std::isfinite(v)) fail(std::string(name) + " must be finite");
}

void check_unit_range(double v, const char *name) {
  check_finite(v, name);
  if (v < 0.0 || v > 1.0)
    fail(std::string(name) + " must be in [0, 1] (got " + std::to_string(v) + ")");
}

void check_sr(int64_t sr) {
  if (sr < 100 || sr > 384000)
    fail("sample rate must be in [100, 384000] Hz (got " + std::to_string(sr) + ")");
}

int64_t sec_to_frames_checked(double sec, int64_t sr, const char *name, double min_sec = 0.0, double max_sec = 3600.0) {
  check_finite(sec, name);
  if (sec < min_sec || sec > max_sec)
    fail(std::string(name) + " must be in [" + std::to_string(min_sec) + ", " + std::to_string(max_sec) + "] seconds (got " + std::to_string(sec) + ")");
  return static_cast<int64_t>(std::llround(sec * double(sr)));
}

int64_t calc_frames(int64_t sr, double duration) {
  check_sr(sr);
  check_finite(duration, "duration");
  if (duration <= 0.0 || duration > 3600.0)
    fail("duration must be in (0, 3600] seconds (got " + std::to_string(duration) + ")");
  int64_t n = static_cast<int64_t>(std::llround(double(sr) * duration));
  if (n <= 0) n = 1;
  return n;
}

void check_channel(const Buffer &b, int64_t ch) {
  if (ch < 0 || ch >= b.channels)
    fail("channel index " + std::to_string(ch) + " outside [0, " + std::to_string(b.channels - 1) + "]");
  if (b.frames <= 0) fail("buffer is empty");
}

void ensure_parent_dir(const std::string &path) {
  std::filesystem::path p(path);
  if (p.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
  }
}

void finish_write(std::ofstream &out, const std::string &path) {
  out.flush();
  if (!out) fail("write failed for '" + path + "'");
  out.close();
  if (out.fail()) fail("close failed for '" + path + "'");
}

// Deterministic xorshift64* PRNG for repeatable synthesis.
struct Rng {
  uint64_t s;
  explicit Rng(int64_t seed) : s(static_cast<uint64_t>(seed) ^ 0x9E3779B97F4A7C15ULL) {
    if (s == 0) s = 0x9E3779B97F4A7C15ULL;
  }
  uint64_t next_u64() {
    uint64_t x = s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    s = x;
    return x * 0x2545F4914F6CDD1DULL;
  }
  // Uniform in [-1, 1].
  double uniform_bipolar() {
    return (double(next_u64() >> 11) * (1.0 / 9007199254740992.0)) * 2.0 - 1.0;
  }
};

struct SpGuard {
  sp_data *sp = nullptr;
  std::size_t charged = 0;
  explicit SpGuard(int sr, uint32_t seed = 12345) {
    membudget::charge(sizeof(sp_data));
    charged = sizeof(sp_data);
    sp_create(&sp);
    sp->sr = sr;
    sp_srand(sp, seed);
  }
  void charge_extra(std::size_t bytes) {
    membudget::charge(bytes);
    charged += bytes;
  }
  ~SpGuard() {
    if (sp) sp_destroy(&sp);
    if (charged > 0) membudget::release(charged);
  }
};

// Cooley-Tukey radix-2 complex FFT in-place (invert=false forward, invert=true inverse without 1/N scaling).
void fft_radix2(std::vector<std::complex<double>> &a, bool invert) {
  std::size_t n = a.size();
  for (std::size_t i = 1, j = 0; i < n; i++) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    double ang = kTwoPi / double(len) * (invert ? 1.0 : -1.0);
    std::complex<double> wlen(std::cos(ang), std::sin(ang));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0);
      std::size_t half = len >> 1;
      for (std::size_t j = 0; j < half; j++) {
        std::complex<double> u = a[i + j];
        std::complex<double> v = a[i + j + half] * w;
        a[i + j] = u + v;
        a[i + j + half] = u - v;
        w *= wlen;
      }
    }
  }
}

bool is_pow2(std::size_t n) { return n > 0 && (n & (n - 1)) == 0; }

// Arbitrary-length complex FFT via radix-2 or Bluestein chirp-z.
void fft_complex(std::vector<std::complex<double>> &a, bool invert) {
  std::size_t n = a.size();
  if (n <= 1) return;
  if (is_pow2(n)) {
    fft_radix2(a, invert);
    return;
  }
  std::size_t m = 1;
  while (m < 2 * n - 1) m <<= 1;
  membudget::require(m * sizeof(std::complex<double>) * 3);
  std::vector<std::complex<double>> w(n), x(m, {0.0, 0.0}), y(m, {0.0, 0.0});
  double sign = invert ? 1.0 : -1.0;
  for (std::size_t k = 0; k < n; k++) {
    uint64_t k2 = (uint64_t(k) * uint64_t(k)) % (2 * uint64_t(n));
    double angle = sign * kPi * double(k2) / double(n);
    w[k] = std::complex<double>(std::cos(angle), std::sin(angle));
    x[k] = a[k] * w[k];
    y[k] = std::conj(w[k]);
    if (k > 0) y[m - k] = std::conj(w[k]);
  }
  fft_radix2(x, false);
  fft_radix2(y, false);
  for (std::size_t i = 0; i < m; i++) x[i] *= y[i];
  fft_radix2(x, true);
  double inv_m = 1.0 / double(m);
  for (std::size_t k = 0; k < n; k++) {
    a[k] = x[k] * w[k] * inv_m;
  }
}

std::vector<double> make_window(const std::string &kind, std::size_t n, double &coherent_gain) {
  std::vector<double> w(n, 1.0);
  if (n <= 1 || kind == "rect" || kind == "rectangular" || kind == "none") {
    coherent_gain = 1.0;
    return w;
  }
  double sum = 0.0;
  double denom = double(n - 1);
  if (kind == "hann" || kind == "hanning") {
    for (std::size_t i = 0; i < n; i++) {
      w[i] = 0.5 * (1.0 - std::cos(kTwoPi * double(i) / denom));
      sum += w[i];
    }
  } else if (kind == "hamming") {
    for (std::size_t i = 0; i < n; i++) {
      w[i] = 0.54 - 0.46 * std::cos(kTwoPi * double(i) / denom);
      sum += w[i];
    }
  } else if (kind == "blackman") {
    for (std::size_t i = 0; i < n; i++) {
      double a = kTwoPi * double(i) / denom;
      w[i] = 0.42 - 0.5 * std::cos(a) + 0.08 * std::cos(2.0 * a);
      sum += w[i];
    }
  } else {
    fail("unknown window '" + kind + "' (expected rect, hann, hamming, or blackman)");
  }
  coherent_gain = sum / double(n);
  return w;
}

}  // namespace

Buffer::Buffer(int sr_, int channels_, int64_t frames_) : sr(sr_), channels(channels_), frames(frames_) {
  check_sr(sr_);
  if (channels_ < 1 || channels_ > 2)
    fail("channels must be 1 (mono) or 2 (stereo), got " + std::to_string(channels_));
  if (frames_ <= 0 || frames_ > 200000000)
    fail("frames out of range: " + std::to_string(frames_));
  std::size_t total = std::size_t(channels_) * std::size_t(frames_);
  membudget::require(total * sizeof(float));
  data.assign(total, 0.0f);
}

// --- Biquad ---

double Biquad::gain_at(double freq_hz) const {
  check_finite(freq_hz, "freq_hz");
  if (freq_hz < 0.0 || freq_hz > double(sr) * 0.5)
    fail("frequency " + std::to_string(freq_hz) + " Hz outside [0, Nyquist=" + std::to_string(sr / 2) + "]");
  double w = kTwoPi * freq_hz / double(sr);
  std::complex<double> z1(std::cos(-w), std::sin(-w));
  std::complex<double> z2(std::cos(-2.0 * w), std::sin(-2.0 * w));
  std::complex<double> num = b0 + b1 * z1 + b2 * z2;
  std::complex<double> den = 1.0 + a1 * z1 + a2 * z2;
  return std::abs(num / den);
}

double Biquad::db_at(double freq_hz) const {
  double g = std::max(gain_at(freq_hz), 1e-12);
  return 20.0 * std::log10(g);
}

double Biquad::phase_deg_at(double freq_hz) const {
  check_finite(freq_hz, "freq_hz");
  if (freq_hz < 0.0 || freq_hz > double(sr) * 0.5)
    fail("frequency " + std::to_string(freq_hz) + " Hz outside [0, Nyquist=" + std::to_string(sr / 2) + "]");
  double w = kTwoPi * freq_hz / double(sr);
  std::complex<double> z1(std::cos(-w), std::sin(-w));
  std::complex<double> z2(std::cos(-2.0 * w), std::sin(-2.0 * w));
  std::complex<double> num = b0 + b1 * z1 + b2 * z2;
  std::complex<double> den = 1.0 + a1 * z1 + a2 * z2;
  return std::arg(num / den) * (180.0 / kPi);
}

void Biquad::save_response_csv(const std::string &path, int64_t points, double fmin, double fmax, bool dry_run) const {
  if (path.empty()) fail("save_response_csv: empty path");
  if (points < 2 || points > 1000000) fail("save_response_csv: points must be in [2, 1000000]");
  check_finite(fmin, "fmin");
  check_finite(fmax, "fmax");
  double nyq = double(sr) * 0.5;
  if (fmax == 0.0) fmax = nyq;
  if (fmax <= 0.0 || fmax > nyq) fail("save_response_csv: fmax must be in (0, Nyquist]");
  if (fmin <= 0.0 || fmin >= fmax) fail("save_response_csv: invalid frequency range");
  if (dry_run) {
    std::cerr << "lmaudio: --dry-run: would write " << path << "\n";
    return;
  }
  ensure_parent_dir(path);
  std::ofstream out(path, std::ios::binary);
  if (!out) fail("cannot write '" + path + "'");
  out << "# freq_hz,gain,db,phase_deg\n";
  out << std::setprecision(10);
  double log_min = std::log(fmin), log_max = std::log(fmax);
  for (int64_t i = 0; i < points; i++) {
    double u = double(i) / double(points - 1);
    double f = std::exp(log_min + u * (log_max - log_min));
    if (f > nyq) f = nyq;
    out << f << "," << gain_at(f) << "," << db_at(f) << "," << phase_deg_at(f) << "\n";
  }
  finish_write(out, path);
}

Biquad design_biquad(const std::string &kind, double freq, double q, double db_gain, int64_t sr) {
  check_sr(sr);
  check_finite(freq, "freq");
  check_finite(q, "q");
  check_finite(db_gain, "db_gain");
  if (freq <= 0.0 || freq >= double(sr) * 0.5)
    fail("biquad cutoff frequency " + std::to_string(freq) + " Hz must be in (0, " + std::to_string(sr * 0.5) + ")");
  if (q <= 0.0 || q > 1000.0) fail("biquad Q must be in (0, 1000] (got " + std::to_string(q) + ")");
  if (db_gain < -120.0 || db_gain > 120.0)
    fail("biquad db_gain must be in [-120, 120] dB (got " + std::to_string(db_gain) + ")");

  double w0 = kTwoPi * freq / double(sr);
  double cosw = std::cos(w0);
  double sinw = std::sin(w0);
  double alpha = sinw / (2.0 * q);
  double A = std::pow(10.0, db_gain / 40.0);

  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
  if (kind == "lowpass" || kind == "lpf") {
    b0 = (1.0 - cosw) * 0.5;
    b1 = 1.0 - cosw;
    b2 = (1.0 - cosw) * 0.5;
    a0 = 1.0 + alpha;
    a1 = -2.0 * cosw;
    a2 = 1.0 - alpha;
  } else if (kind == "highpass" || kind == "hpf") {
    b0 = (1.0 + cosw) * 0.5;
    b1 = -(1.0 + cosw);
    b2 = (1.0 + cosw) * 0.5;
    a0 = 1.0 + alpha;
    a1 = -2.0 * cosw;
    a2 = 1.0 - alpha;
  } else if (kind == "bandpass" || kind == "bpf") {
    b0 = alpha;
    b1 = 0.0;
    b2 = -alpha;
    a0 = 1.0 + alpha;
    a1 = -2.0 * cosw;
    a2 = 1.0 - alpha;
  } else if (kind == "notch" || kind == "bandreject") {
    b0 = 1.0;
    b1 = -2.0 * cosw;
    b2 = 1.0;
    a0 = 1.0 + alpha;
    a1 = -2.0 * cosw;
    a2 = 1.0 - alpha;
  } else if (kind == "allpass" || kind == "apf") {
    b0 = 1.0 - alpha;
    b1 = -2.0 * cosw;
    b2 = 1.0 + alpha;
    a0 = 1.0 + alpha;
    a1 = -2.0 * cosw;
    a2 = 1.0 - alpha;
  } else if (kind == "peak" || kind == "peaking") {
    b0 = 1.0 + alpha * A;
    b1 = -2.0 * cosw;
    b2 = 1.0 - alpha * A;
    a0 = 1.0 + alpha / A;
    a1 = -2.0 * cosw;
    a2 = 1.0 - alpha / A;
  } else if (kind == "lowshelf") {
    double sq = 2.0 * std::sqrt(A) * alpha;
    b0 = A * ((A + 1.0) - (A - 1.0) * cosw + sq);
    b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw);
    b2 = A * ((A + 1.0) - (A - 1.0) * cosw - sq);
    a0 = (A + 1.0) + (A - 1.0) * cosw + sq;
    a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw);
    a2 = (A + 1.0) + (A - 1.0) * cosw - sq;
  } else if (kind == "highshelf") {
    double sq = 2.0 * std::sqrt(A) * alpha;
    b0 = A * ((A + 1.0) + (A - 1.0) * cosw + sq);
    b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw);
    b2 = A * ((A + 1.0) + (A - 1.0) * cosw - sq);
    a0 = (A + 1.0) - (A - 1.0) * cosw + sq;
    a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw);
    a2 = (A + 1.0) - (A - 1.0) * cosw - sq;
  } else {
    fail("unknown biquad filter kind '" + kind + "'");
  }

  Biquad res;
  res.b0 = b0 / a0;
  res.b1 = b1 / a0;
  res.b2 = b2 / a0;
  res.a1 = a1 / a0;
  res.a2 = a2 / a0;
  res.sr = static_cast<int>(sr);
  res.kind = kind;
  res.freq = freq;
  res.q = q;
  res.db_gain = db_gain;
  return res;
}

Buffer apply_biquad(const Buffer &buf, const Biquad &bq) {
  Buffer out(buf.sr, buf.channels, buf.frames);
  for (int c = 0; c < buf.channels; c++) {
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    double z1 = 0.0, z2 = 0.0;
    for (int64_t i = 0; i < buf.frames; i++) {
      double x = in_p[i];
      double y = bq.b0 * x + z1;
      z1 = bq.b1 * x - bq.a1 * y + z2;
      z2 = bq.b2 * x - bq.a2 * y;
      out_p[i] = static_cast<float>(y);
    }
  }
  return out;
}

// --- Spectrum ---

double Spectrum::freq_at(int64_t bin) const {
  if (bin < 0 || bin >= bins())
    fail("Spectrum.freq_at: bin " + std::to_string(bin) + " outside [0, " + std::to_string(bins() - 1) + "]");
  return double(bin) * double(sr) / double(nfft);
}

double Spectrum::mag_at(int64_t bin) const {
  if (bin < 0 || bin >= bins())
    fail("Spectrum.mag_at: bin " + std::to_string(bin) + " outside [0, " + std::to_string(bins() - 1) + "]");
  double raw = std::hypot(re[std::size_t(bin)], im[std::size_t(bin)]);
  double scale = (bin == 0 || (nfft % 2 == 0 && bin == nfft / 2)) ? (1.0 / double(nfft)) : (2.0 / double(nfft));
  return raw * scale / (coherent_gain > 0.0 ? coherent_gain : 1.0);
}

double Spectrum::db_at(int64_t bin) const {
  return 20.0 * std::log10(std::max(mag_at(bin), 1e-12));
}

double Spectrum::phase_at(int64_t bin) const {
  if (bin < 0 || bin >= bins())
    fail("Spectrum.phase_at: bin " + std::to_string(bin) + " outside [0, " + std::to_string(bins() - 1) + "]");
  return std::atan2(im[std::size_t(bin)], re[std::size_t(bin)]);
}

double Spectrum::mag_at_freq(double freq_hz) const {
  check_finite(freq_hz, "freq_hz");
  if (freq_hz < 0.0 || freq_hz > double(sr) * 0.5)
    fail("Spectrum.mag_at_freq: frequency outside [0, Nyquist]");
  double pos = freq_hz * double(nfft) / double(sr);
  int64_t i0 = static_cast<int64_t>(std::floor(pos));
  double frac = pos - double(i0);
  if (i0 >= bins() - 1) return mag_at(bins() - 1);
  return mag_at(i0) * (1.0 - frac) + mag_at(i0 + 1) * frac;
}

int64_t Spectrum::peak_bin() const {
  int64_t best = (bins() > 1) ? 1 : 0;
  double best_m = mag_at(best);
  for (int64_t k = best + 1; k < bins(); k++) {
    double m = mag_at(k);
    if (m > best_m) {
      best_m = m;
      best = k;
    }
  }
  return best;
}

double Spectrum::peak_freq() const {
  int64_t k = peak_bin();
  if (k <= 0 || k >= bins() - 1) return freq_at(k);
  double m0 = std::log(std::max(mag_at(k - 1), 1e-15));
  double m1 = std::log(std::max(mag_at(k), 1e-15));
  double m2 = std::log(std::max(mag_at(k + 1), 1e-15));
  double denom = m0 - 2.0 * m1 + m2;
  double delta = (std::abs(denom) > 1e-12) ? 0.5 * (m0 - m2) / denom : 0.0;
  delta = std::clamp(delta, -0.5, 0.5);
  return (double(k) + delta) * double(sr) / double(nfft);
}

double Spectrum::peak_mag() const { return mag_at(peak_bin()); }

double Spectrum::centroid() const {
  double num = 0.0, den = 0.0;
  for (int64_t k = 0; k < bins(); k++) {
    double m = mag_at(k);
    num += freq_at(k) * m;
    den += m;
  }
  return den > 1e-15 ? (num / den) : 0.0;
}

double Spectrum::parseval_energy() const {
  double sum = 0.0;
  for (int i = 0; i < nfft; i++) {
    sum += re[std::size_t(i)] * re[std::size_t(i)] + im[std::size_t(i)] * im[std::size_t(i)];
  }
  return sum / double(nfft);
}

void Spectrum::save_csv(const std::string &path, double max_freq, bool dry_run) const {
  if (path.empty()) fail("Spectrum.save_csv: empty path");
  check_finite(max_freq, "max_freq");
  if (max_freq < 0.0 || max_freq > double(sr) * 0.5)
    fail("Spectrum.save_csv: max_freq must be in [0, Nyquist]");
  double limit = (max_freq > 0.0) ? max_freq : (double(sr) * 0.5);
  if (dry_run) {
    std::cerr << "lmaudio: --dry-run: would write " << path << "\n";
    return;
  }
  ensure_parent_dir(path);
  std::ofstream out(path, std::ios::binary);
  if (!out) fail("cannot write '" + path + "'");
  out << "# freq_hz,magnitude,db,phase_rad\n";
  out << std::setprecision(9);
  for (int64_t k = 0; k < bins(); k++) {
    double f = freq_at(k);
    if (f > limit) break;
    out << f << "," << mag_at(k) << "," << db_at(k) << "," << phase_at(k) << "\n";
  }
  finish_write(out, path);
}

// --- Spectrogram ---

double Spectrogram::time_at(int64_t frame) const {
  if (frame < 0 || frame >= n_frames)
    fail("Spectrogram.time_at: frame " + std::to_string(frame) + " outside [0, " + std::to_string(n_frames - 1) + "]");
  return times[std::size_t(frame)];
}

double Spectrogram::freq_at(int64_t bin) const {
  if (bin < 0 || bin >= n_bins)
    fail("Spectrogram.freq_at: bin " + std::to_string(bin) + " outside [0, " + std::to_string(n_bins - 1) + "]");
  return freqs[std::size_t(bin)];
}

double Spectrogram::mag_at(int64_t frame, int64_t bin) const {
  if (frame < 0 || frame >= n_frames || bin < 0 || bin >= n_bins)
    fail("Spectrogram.mag_at: index (" + std::to_string(frame) + ", " + std::to_string(bin) + ") out of bounds");
  return mags[std::size_t(frame) * std::size_t(n_bins) + std::size_t(bin)];
}

double Spectrogram::db_at(int64_t frame, int64_t bin) const {
  return 20.0 * std::log10(std::max(mag_at(frame, bin), 1e-12));
}

double Spectrogram::ridge_freq(int64_t frame) const {
  if (frame < 0 || frame >= n_frames)
    fail("Spectrogram.ridge_freq: frame out of bounds");
  int64_t best = (n_bins > 1) ? 1 : 0;
  double best_m = mag_at(frame, best);
  for (int64_t b = best + 1; b < n_bins; b++) {
    double m = mag_at(frame, b);
    if (m > best_m) {
      best_m = m;
      best = b;
    }
  }
  if (best <= 0 || best >= n_bins - 1) return freq_at(best);
  double m0 = std::log(std::max(mag_at(frame, best - 1), 1e-15));
  double m1 = std::log(std::max(mag_at(frame, best), 1e-15));
  double m2 = std::log(std::max(mag_at(frame, best + 1), 1e-15));
  double denom = m0 - 2.0 * m1 + m2;
  double delta = (std::abs(denom) > 1e-12) ? 0.5 * (m0 - m2) / denom : 0.0;
  delta = std::clamp(delta, -0.5, 0.5);
  double df = (n_bins > 1) ? (freqs[1] - freqs[0]) : 0.0;
  return freqs[std::size_t(best)] + delta * df;
}

void Spectrogram::save_csv(const std::string &path, double floor_db, bool dry_run) const {
  if (path.empty()) fail("Spectrogram.save_csv: empty path");
  check_finite(floor_db, "floor_db");
  if (dry_run) {
    std::cerr << "lmaudio: --dry-run: would write " << path << "\n";
    return;
  }
  ensure_parent_dir(path);
  std::ofstream out(path, std::ios::binary);
  if (!out) fail("cannot write '" + path + "'");
  out << std::setprecision(7);
  for (int f = 0; f < n_frames; f++) {
    double t = times[std::size_t(f)];
    for (int b = 0; b < n_bins; b++) {
      double m = mags[std::size_t(f) * std::size_t(n_bins) + std::size_t(b)];
      double db = std::max(20.0 * std::log10(std::max(m, 1e-12)), floor_db);
      out << t << "," << freqs[std::size_t(b)] << "," << db << "," << m << "\n";
    }
    out << "\n";
  }
  finish_write(out, path);
}

// --- Synthesis ---

Buffer make_silence(int64_t sr, double duration) {
  return Buffer(static_cast<int>(sr), 1, calc_frames(sr, duration));
}

Buffer make_sine(double freq, double duration, double amp, int64_t sr) {
  check_sr(sr);
  check_finite(freq, "freq");
  check_finite(amp, "amp");
  if (freq < 0.0 || freq > double(sr) * 0.5)
    fail("sine frequency " + std::to_string(freq) + " Hz outside [0, Nyquist]");
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  float *p = b.chan_ptr(0);
  double w = kTwoPi * freq / double(sr);
  for (int64_t i = 0; i < b.frames; i++) {
    p[i] = static_cast<float>(amp * std::sin(w * double(i)));
  }
  return b;
}

Buffer make_cosine(double freq, double duration, double amp, int64_t sr) {
  check_sr(sr);
  check_finite(freq, "freq");
  check_finite(amp, "amp");
  if (freq < 0.0 || freq > double(sr) * 0.5)
    fail("cosine frequency " + std::to_string(freq) + " Hz outside [0, Nyquist]");
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  float *p = b.chan_ptr(0);
  double w = kTwoPi * freq / double(sr);
  for (int64_t i = 0; i < b.frames; i++) {
    p[i] = static_cast<float>(amp * std::cos(w * double(i)));
  }
  return b;
}

Buffer make_saw(double freq, double duration, double amp, int64_t sr) {
  check_sr(sr);
  check_finite(freq, "freq");
  check_finite(amp, "amp");
  if (freq <= 0.0 || freq >= double(sr) * 0.5)
    fail("saw frequency " + std::to_string(freq) + " Hz must be in (0, Nyquist)");
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  daisysp::Oscillator osc{};
  osc.Init(static_cast<float>(sr));
  osc.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
  osc.SetFreq(static_cast<float>(freq));
  osc.SetAmp(static_cast<float>(amp));
  float *p = b.chan_ptr(0);
  for (int64_t i = 0; i < b.frames; i++) p[i] = osc.Process();
  return b;
}

Buffer make_square(double freq, double duration, double amp, double duty, int64_t sr) {
  check_sr(sr);
  check_finite(freq, "freq");
  check_finite(amp, "amp");
  check_finite(duty, "duty");
  if (freq <= 0.0 || freq >= double(sr) * 0.5)
    fail("square frequency " + std::to_string(freq) + " Hz must be in (0, Nyquist)");
  if (duty <= 0.01 || duty >= 0.99)
    fail("square duty cycle must be in (0.01, 0.99)");
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  daisysp::Oscillator osc{};
  osc.Init(static_cast<float>(sr));
  osc.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SQUARE);
  osc.SetFreq(static_cast<float>(freq));
  osc.SetPw(static_cast<float>(duty));
  // Compensate for DaisySP's internal 0.707f scaling on WAVE_POLYBLEP_SQUARE.
  osc.SetAmp(static_cast<float>(amp / 0.707));
  float *p = b.chan_ptr(0);
  for (int64_t i = 0; i < b.frames; i++) p[i] = osc.Process();
  return b;
}

Buffer make_triangle(double freq, double duration, double amp, int64_t sr) {
  check_sr(sr);
  check_finite(freq, "freq");
  check_finite(amp, "amp");
  if (freq <= 0.0 || freq >= double(sr) * 0.5)
    fail("triangle frequency " + std::to_string(freq) + " Hz must be in (0, Nyquist)");
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  daisysp::Oscillator osc{};
  osc.Init(static_cast<float>(sr));
  osc.SetWaveform(daisysp::Oscillator::WAVE_TRI);
  osc.SetFreq(static_cast<float>(freq));
  osc.SetAmp(static_cast<float>(amp));
  float *p = b.chan_ptr(0);
  for (int64_t i = 0; i < b.frames; i++) p[i] = osc.Process();
  return b;
}

Buffer make_chirp(double f0, double f1, double duration, double amp, const std::string &mode, int64_t sr) {
  check_finite(f0, "f0");
  check_finite(f1, "f1");
  check_finite(amp, "amp");
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  if (f0 <= 0.0 || f1 <= 0.0 || f0 >= double(sr) * 0.5 || f1 >= double(sr) * 0.5)
    fail("chirp frequencies must be in (0, Nyquist)");
  float *p = b.chan_ptr(0);
  if (mode == "linear") {
    double k = (f1 - f0) / duration;
    for (int64_t i = 0; i < b.frames; i++) {
      double t = double(i) / double(sr);
      double phase = kTwoPi * (f0 * t + 0.5 * k * t * t);
      p[i] = static_cast<float>(amp * std::sin(phase));
    }
  } else if (mode == "log" || mode == "exponential") {
    if (std::abs(f1 - f0) < 1e-9) return make_sine(f0, duration, amp, sr);
    double r = std::log(f1 / f0) / duration;
    for (int64_t i = 0; i < b.frames; i++) {
      double t = double(i) / double(sr);
      double phase = kTwoPi * f0 * (std::exp(r * t) - 1.0) / r;
      p[i] = static_cast<float>(amp * std::sin(phase));
    }
  } else {
    fail("unknown chirp mode '" + mode + "' (expected linear or log)");
  }
  return b;
}

Buffer make_impulse(double duration, double offset_sec, double amp, int64_t sr) {
  check_sr(sr);
  check_finite(amp, "amp");
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  int64_t idx = sec_to_frames_checked(offset_sec, sr, "impulse offset_sec", 0.0, duration);
  if (idx < 0 || idx >= b.frames)
    fail("impulse offset_sec " + std::to_string(offset_sec) + " outside buffer duration");
  b.at(idx, 0) = static_cast<float>(amp);
  return b;
}

Buffer make_noise(const std::string &kind, double duration, double amp, int64_t seed, int64_t sr) {
  check_finite(amp, "amp");
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  Rng rng(seed);
  float *p = b.chan_ptr(0);
  if (kind == "white") {
    for (int64_t i = 0; i < b.frames; i++) p[i] = static_cast<float>(amp * rng.uniform_bipolar());
  } else if (kind == "pink") {
    // Paul Kellet's 7-pole pink noise filter.
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (int64_t i = 0; i < b.frames; i++) {
      double w = rng.uniform_bipolar();
      b0 = 0.99886 * b0 + w * 0.0555179;
      b1 = 0.99332 * b1 + w * 0.0750759;
      b2 = 0.96900 * b2 + w * 0.1538520;
      b3 = 0.86650 * b3 + w * 0.3104856;
      b4 = 0.55000 * b4 + w * 0.5329522;
      b5 = -0.7616 * b5 - w * 0.0168980;
      double pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.18;
      b6 = w * 0.115926;
      p[i] = static_cast<float>(amp * std::clamp(pink, -1.0, 1.0));
    }
  } else if (kind == "brown" || kind == "red") {
    double acc = 0.0;
    for (int64_t i = 0; i < b.frames; i++) {
      acc = (acc + 0.02 * rng.uniform_bipolar()) / 1.02;
      p[i] = static_cast<float>(amp * std::clamp(acc * 3.5, -1.0, 1.0));
    }
  } else {
    fail("unknown noise kind '" + kind + "' (expected white, pink, or brown)");
  }
  return b;
}

Buffer make_expr(const std::string &formula, double duration, int64_t sr) {
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  Expr prog(formula, {"t", "n", "sr", "dur"});
  double vars[4] = {0.0, 0.0, double(sr), duration};
  float *p = b.chan_ptr(0);
  for (int64_t i = 0; i < b.frames; i++) {
    vars[0] = double(i) / double(sr);
    vars[1] = double(i);
    double val = prog.eval(vars);
    if (!std::isfinite(val))
      fail("audio.expr produced non-finite value at sample " + std::to_string(i));
    p[i] = static_cast<float>(val);
  }
  return b;
}

Buffer make_fm(double carrier, double ratio, double index, double duration, double amp, int64_t sr) {
  check_sr(sr);
  check_finite(carrier, "carrier");
  check_finite(ratio, "ratio");
  check_finite(index, "index");
  check_finite(amp, "amp");
  if (carrier <= 0.0 || carrier >= double(sr) * 0.5)
    fail("FM carrier frequency must be in (0, Nyquist)");
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  daisysp::Fm2 fm;
  fm.Init(static_cast<float>(sr));
  fm.SetFrequency(static_cast<float>(carrier));
  fm.SetRatio(static_cast<float>(ratio));
  // DaisySP Fm2::Process() accumulates modval * idx_ per sample (idx_ = 0.2 * arg),
  // so scaling by 5 * f_mod / sr makes `index` the exact Chowning modulation index beta (radians),
  // yielding sideband amplitudes A * |J_k(index)|.
  double f_mod = carrier * ratio;
  fm.SetIndex(static_cast<float>(index * 5.0 * f_mod / double(sr)));
  float *p = b.chan_ptr(0);
  for (int64_t i = 0; i < b.frames; i++) {
    p[i] = static_cast<float>(amp) * fm.Process();
  }
  return b;
}

Buffer make_pluck(double freq, double duration, double amp, double brightness, double damping, int64_t sr) {
  check_sr(sr);
  check_finite(freq, "freq");
  check_finite(amp, "amp");
  check_unit_range(brightness, "pluck brightness");
  check_unit_range(damping, "pluck damping");
  if (freq < 20.0 || freq >= double(sr) * 0.45)
    fail("pluck frequency " + std::to_string(freq) + " Hz must be in [20, 0.45*sr)");
  std::srand(12345);
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  daisysp::StringVoice sv;
  sv.Init(static_cast<float>(sr));
  sv.SetFreq(static_cast<float>(freq));
  sv.SetStructure(0.25f);  // linear bridge (zero pitch shift nonlinearity)
  sv.SetBrightness(static_cast<float>(brightness));
  sv.SetDamping(static_cast<float>(damping));
  sv.SetAccent(0.85f);
  float *p = b.chan_ptr(0);
  float max_abs = 0.0f;
  for (int64_t i = 0; i < b.frames; i++) {
    p[i] = sv.Process(i == 0);
    max_abs = std::max(max_abs, std::abs(p[i]));
  }
  if (max_abs > 1e-7f) {
    float scale = static_cast<float>(amp) / max_abs;
    for (int64_t i = 0; i < b.frames; i++) p[i] *= scale;
  }
  return b;
}

Buffer make_modal(double freq, double duration, double amp, double structure, double brightness, double damping, int64_t sr) {
  check_sr(sr);
  check_finite(freq, "freq");
  check_finite(amp, "amp");
  check_unit_range(structure, "modal structure");
  check_unit_range(brightness, "modal brightness");
  check_unit_range(damping, "modal damping");
  if (freq < 20.0 || freq >= double(sr) * 0.45)
    fail("modal frequency " + std::to_string(freq) + " Hz must be in [20, 0.45*sr)");
  std::srand(12345);
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  daisysp::ModalVoice mv;
  mv.Init(static_cast<float>(sr));
  mv.SetFreq(static_cast<float>(freq));
  mv.SetStructure(static_cast<float>(structure));
  mv.SetBrightness(static_cast<float>(brightness));
  mv.SetDamping(static_cast<float>(damping));
  mv.SetAccent(0.8f);
  float *p = b.chan_ptr(0);
  float max_abs = 0.0f;
  for (int64_t i = 0; i < b.frames; i++) {
    p[i] = mv.Process(i == 0);
    max_abs = std::max(max_abs, std::abs(p[i]));
  }
  if (max_abs > 1e-7f) {
    float scale = static_cast<float>(amp) / max_abs;
    for (int64_t i = 0; i < b.frames; i++) p[i] *= scale;
  }
  return b;
}

// Clean-room MIT 4th-order Euler-Bernoulli finite-difference bar model
// (Bilbao 2009, Numerical Sound Synthesis, Ch. 7: u_tt = -kappa^2 u_xxxx - 2*s0*u_t + 2*s1*u_txx).
Buffer make_bar(int64_t bc_left, int64_t bc_right, double stiffness, double decay, double duration, double amp, int64_t sr) {
  check_sr(sr);
  check_finite(stiffness, "stiffness");
  check_finite(decay, "decay");
  check_finite(amp, "amp");
  if (bc_left < 1 || bc_left > 3 || bc_right < 1 || bc_right > 3)
    fail("bar boundary conditions must be 1 (clamped), 2 (pivoting), or 3 (free)");
  if (stiffness <= 0.0 || decay <= 0.0)
    fail("bar stiffness and decay must be > 0");
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  double dt = 1.0 / double(sr);
  double s0 = 3.0 * std::log(10.0) / decay;
  double s1 = 0.0005;
  double h_min = std::sqrt(2.0 * dt * (s1 + std::sqrt(s1 * s1 + stiffness * stiffness)));
  int N = std::clamp(static_cast<int>(std::floor(1.0 / h_min)), 8, 512);
  double h = 1.0 / double(N);
  double mu = (stiffness * dt) / (h * h);
  double mu2 = mu * mu;
  double d0 = s0 * dt;
  double d1 = (2.0 * s1 * dt) / (h * h);

  std::vector<double> u0(N + 1, 0.0), u1(N + 1, 0.0), u2(N + 1, 0.0);
  // Raised-cosine strike velocity profile around x = 0.28.
  int center = std::max(2, static_cast<int>(std::llround(0.28 * N)));
  int width = std::max(2, N / 10);
  for (int j = 1; j < N; j++) {
    if (std::abs(j - center) <= width) {
      double w = 0.5 * (1.0 + std::cos(kPi * double(j - center) / double(width)));
      u1[j] = dt * w;
    }
  }
  int readout = std::clamp(static_cast<int>(std::llround(0.71 * N)), 2, N - 2);
  float *p = b.chan_ptr(0);
  float max_abs = 0.0f;

  auto get_u = [&](const std::vector<double> &u, int idx) -> double {
    if (idx >= 0 && idx <= N) return u[idx];
    if (idx == -1) {
      if (bc_left == 1) return u[1];       // clamped: u_0 = 0, u_{-1} = u_1
      if (bc_left == 2) return -u[1];      // pivoting: u_0 = 0, u_{-1} = -u_1
      return 2.0 * u[0] - u[1];            // free
    }
    if (idx == N + 1) {
      if (bc_right == 1) return u[N - 1];
      if (bc_right == 2) return -u[N - 1];
      return 2.0 * u[N] - u[N - 1];
    }
    return 0.0;
  };

  for (int64_t n = 0; n < b.frames; n++) {
    int j_start = (bc_left == 3) ? 0 : 1;
    int j_end = (bc_right == 3) ? N : (N - 1);
    for (int j = j_start; j <= j_end; j++) {
      double u_jm2 = get_u(u1, j - 2);
      double u_jm1 = get_u(u1, j - 1);
      double u_j = get_u(u1, j);
      double u_jp1 = get_u(u1, j + 1);
      double u_jp2 = get_u(u1, j + 2);
      double dxxxx = u_jp2 - 4.0 * u_jp1 + 6.0 * u_j - 4.0 * u_jm1 + u_jm2;
      double dxx1 = u_jp1 - 2.0 * u_j + u_jm1;
      double dxx2 = get_u(u2, j + 1) - 2.0 * get_u(u2, j) + get_u(u2, j - 1);
      u0[j] = (2.0 * u_j - (1.0 - d0) * get_u(u2, j) - mu2 * dxxxx + d1 * (dxx1 - dxx2)) / (1.0 + d0);
    }
    if (bc_left != 3) u0[0] = 0.0;
    if (bc_right != 3) u0[N] = 0.0;
    float sample = static_cast<float>(u0[readout] - u1[readout]);
    p[n] = sample;
    max_abs = std::max(max_abs, std::abs(sample));
    std::swap(u2, u1);
    std::swap(u1, u0);
  }
  if (max_abs > 1e-12f) {
    float scale = static_cast<float>(amp) / max_abs;
    for (int64_t i = 0; i < b.frames; i++) p[i] *= scale;
  }
  return b;
}

Buffer make_drum(const std::string &kind, double freq, double duration, double decay, double tone, double accent, int64_t sr) {
  check_sr(sr);
  check_finite(freq, "freq");
  check_unit_range(decay, "drum decay");
  check_unit_range(tone, "drum tone");
  check_unit_range(accent, "drum accent");
  if (freq <= 10.0 || freq >= double(sr) * 0.45)
    fail("drum frequency must be in (10, 0.45*sr)");
  std::srand(12345);
  Buffer b(static_cast<int>(sr), 1, calc_frames(sr, duration));
  float *p = b.chan_ptr(0);
  if (kind == "kick" || kind == "bass") {
    daisysp::AnalogBassDrum d;
    d.Init(static_cast<float>(sr));
    d.SetFreq(static_cast<float>(freq));
    d.SetDecay(static_cast<float>(decay));
    d.SetTone(static_cast<float>(tone));
    d.SetAccent(static_cast<float>(accent));
    for (int64_t i = 0; i < b.frames; i++) p[i] = d.Process(i == 0);
  } else if (kind == "synth_kick") {
    daisysp::SyntheticBassDrum d;
    d.Init(static_cast<float>(sr));
    d.SetFreq(static_cast<float>(freq));
    d.SetDecay(static_cast<float>(decay));
    d.SetTone(static_cast<float>(tone));
    d.SetAccent(static_cast<float>(accent));
    for (int64_t i = 0; i < b.frames; i++) p[i] = d.Process(i == 0);
  } else if (kind == "snare") {
    daisysp::AnalogSnareDrum d;
    d.Init(static_cast<float>(sr));
    d.SetFreq(static_cast<float>(freq));
    d.SetDecay(static_cast<float>(decay));
    d.SetTone(static_cast<float>(tone));
    d.SetAccent(static_cast<float>(accent));
    for (int64_t i = 0; i < b.frames; i++) p[i] = d.Process(i == 0);
  } else if (kind == "synth_snare") {
    daisysp::SyntheticSnareDrum d;
    d.Init(static_cast<float>(sr));
    d.SetFreq(static_cast<float>(freq));
    d.SetDecay(static_cast<float>(decay));
    d.SetAccent(static_cast<float>(accent));
    for (int64_t i = 0; i < b.frames; i++) p[i] = d.Process(i == 0);
  } else if (kind == "hihat" || kind == "hat") {
    daisysp::HiHat<> d;
    d.Init(static_cast<float>(sr));
    d.SetFreq(static_cast<float>(freq));
    d.SetDecay(static_cast<float>(decay));
    d.SetTone(static_cast<float>(tone));
    d.SetAccent(static_cast<float>(accent));
    for (int64_t i = 0; i < b.frames; i++) p[i] = d.Process(i == 0);
  } else {
    fail("unknown drum kind '" + kind + "' (expected kick, synth_kick, snare, synth_snare, or hihat)");
  }
  return b;
}

double midi_to_freq(double midi) {
  check_finite(midi, "midi");
  if (midi < 0.0 || midi > 135.0)
    fail("MIDI note number must be in [0, 135] (got " + std::to_string(midi) + ")");
  return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

double note_to_freq(const std::string &note) {
  if (note.size() < 2) fail("invalid note '" + note + "'");
  char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(note[0])));
  int semitone = 0;
  switch (letter) {
    case 'C': semitone = 0; break;
    case 'D': semitone = 2; break;
    case 'E': semitone = 4; break;
    case 'F': semitone = 5; break;
    case 'G': semitone = 7; break;
    case 'A': semitone = 9; break;
    case 'B': semitone = 11; break;
    default: fail("invalid note letter in '" + note + "'");
  }
  std::size_t pos = 1;
  if (pos < note.size() && (note[pos] == '#' || note[pos] == 'b')) {
    semitone += (note[pos] == '#') ? 1 : -1;
    pos++;
  }
  if (pos >= note.size()) fail("missing octave in note '" + note + "'");
  int octave = 0;
  try {
    std::size_t idx = 0;
    octave = std::stoi(note.substr(pos), &idx);
    if (pos + idx != note.size()) fail("invalid note '" + note + "'");
  } catch (...) {
    fail("invalid octave in note '" + note + "'");
  }
  int midi = (octave + 1) * 12 + semitone;
  return midi_to_freq(double(midi));
}

namespace {
Buffer render_single_note(double freq, double dur, const std::string &inst, int64_t sr) {
  Buffer b;
  if (inst == "sine") {
    b = make_sine(freq, dur, 0.7, sr);
    b = apply_adsr(b, std::min(0.015, dur * 0.2), std::min(0.08, dur * 0.3), 0.7, std::min(0.08, dur * 0.3), 0.0);
  } else if (inst == "saw") {
    b = make_saw(freq, dur, 0.6, sr);
    b = apply_ladder(b, std::min(freq * 4.5, double(sr) * 0.42), 0.25, 1.0, "lp24");
    b = apply_adsr(b, std::min(0.01, dur * 0.15), std::min(0.1, dur * 0.3), 0.65, std::min(0.08, dur * 0.3), 0.0);
  } else if (inst == "square") {
    b = make_square(freq, dur, 0.6, 0.5, sr);
    b = apply_adsr(b, std::min(0.01, dur * 0.15), std::min(0.08, dur * 0.3), 0.65, std::min(0.06, dur * 0.25), 0.0);
  } else if (inst == "triangle") {
    b = make_triangle(freq, dur, 0.7, sr);
    b = apply_adsr(b, std::min(0.01, dur * 0.15), std::min(0.08, dur * 0.3), 0.75, std::min(0.06, dur * 0.25), 0.0);
  } else if (inst == "fm") {
    b = make_fm(freq, 2.0, 1.5, dur, 0.65, sr);
    b = apply_adsr(b, std::min(0.008, dur * 0.1), std::min(0.15, dur * 0.4), 0.4, std::min(0.1, dur * 0.3), 0.0);
  } else if (inst == "pluck") {
    b = make_pluck(freq, dur, 0.75, 0.65, 0.82, sr);
    b = apply_fade(b, 0.001, std::min(0.025, dur * 0.15), "cosine");
  } else if (inst == "modal" || inst == "bell") {
    b = make_modal(freq, dur, 0.75, 0.45, 0.65, 0.75, sr);
    b = apply_fade(b, 0.001, std::min(0.025, dur * 0.15), "cosine");
  } else if (inst == "pad") {
    Buffer s1 = make_saw(freq * 0.997, dur, 0.35, sr);
    Buffer s2 = make_saw(freq * 1.003, dur, 0.35, sr);
    b = apply_mix(s1, s2, 1.0, 0.0);
    b = apply_ladder(b, std::min(freq * 3.0, double(sr) * 0.42), 0.2, 1.0, "lp24");
    b = apply_adsr(b, std::min(0.08, dur * 0.3), std::min(0.15, dur * 0.3), 0.8, std::min(0.15, dur * 0.35), 0.0);
  } else {
    fail("unknown sequence instrument '" + inst + "' (expected sine, saw, square, triangle, fm, pluck, modal, or pad)");
  }
  return b;
}
}  // namespace

Buffer make_sequence(const std::string &notes, double bpm, const std::string &instrument, int64_t sr) {
  check_sr(sr);
  check_finite(bpm, "bpm");
  if (bpm <= 10.0 || bpm > 600.0) fail("sequence bpm must be in (10, 600]");
  double beat_sec = 60.0 / bpm;

  std::istringstream iss(notes);
  std::string tok;
  std::vector<Buffer> parts;
  while (iss >> tok) {
    double beats = 1.0;
    std::string pitch_part = tok;
    auto colon = tok.find(':');
    if (colon != std::string::npos) {
      pitch_part = tok.substr(0, colon);
      try {
        beats = std::stod(tok.substr(colon + 1));
      } catch (...) {
        fail("invalid beat length in token '" + tok + "'");
      }
      if (!std::isfinite(beats) || beats <= 0.0 || beats > 64.0)
        fail("beat length out of range in token '" + tok + "'");
    }
    double dur = beats * beat_sec;
    if (pitch_part == "R" || pitch_part == "r" || pitch_part == "_" || pitch_part == "rest") {
      parts.push_back(make_silence(sr, dur));
      continue;
    }
    // Split chord notes on '+'
    std::vector<std::string> chord_notes;
    std::size_t start = 0;
    while (start < pitch_part.size()) {
      auto plus = pitch_part.find('+', start);
      if (plus == std::string::npos) {
        chord_notes.push_back(pitch_part.substr(start));
        break;
      }
      chord_notes.push_back(pitch_part.substr(start, plus - start));
      start = plus + 1;
    }
    if (chord_notes.empty()) fail("empty note token '" + tok + "'");
    Buffer chord_buf = make_silence(sr, dur);
    double scale = 1.0 / std::sqrt(double(chord_notes.size()));
    for (const auto &n_str : chord_notes) {
      double f = note_to_freq(n_str);
      Buffer nb = render_single_note(f, dur, instrument, sr);
      chord_buf = apply_mix(chord_buf, nb, scale, 0.0);
    }
    parts.push_back(std::move(chord_buf));
  }
  if (parts.empty()) fail("sequence string is empty");
  int64_t total_frames = 0;
  for (const auto &p : parts) total_frames += p.frames;
  Buffer out(static_cast<int>(sr), 1, total_frames);
  float *dst = out.chan_ptr(0);
  int64_t cursor = 0;
  for (const auto &p : parts) {
    std::memcpy(dst + cursor, p.chan_ptr(0), std::size_t(p.frames) * sizeof(float));
    cursor += p.frames;
  }
  return out;
}

// --- WAV I/O ---

namespace {
uint16_t read_u16_le(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t read_u32_le(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void write_u16_le(std::ostream &os, uint16_t v) {
  char b[2] = {char(v & 0xFF), char((v >> 8) & 0xFF)};
  os.write(b, 2);
}
void write_u32_le(std::ostream &os, uint32_t v) {
  char b[4] = {char(v & 0xFF), char((v >> 8) & 0xFF), char((v >> 16) & 0xFF), char((v >> 24) & 0xFF)};
  os.write(b, 4);
}
}  // namespace

Buffer load_wav(const std::string &path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) fail("cannot open WAV file '" + path + "'");
  std::streamsize sz = in.tellg();
  if (sz < 44) fail("invalid or truncated WAV file '" + path + "'");
  membudget::require(std::size_t(sz));
  std::vector<uint8_t> bytes(static_cast<std::size_t>(sz));
  in.seekg(0);
  if (!in.read(reinterpret_cast<char *>(bytes.data()), sz))
    fail("failed reading WAV file '" + path + "'");

  if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
    fail("not a RIFF/WAVE file: '" + path + "'");

  uint16_t fmt_tag = 0, channels = 0, bits_per_sample = 0;
  uint32_t sample_rate = 0;
  const uint8_t *data_ptr = nullptr;
  uint32_t data_bytes = 0;

  std::size_t pos = 12;
  while (pos + 8 <= bytes.size()) {
    const uint8_t *hdr = bytes.data() + pos;
    uint32_t chunk_sz = read_u32_le(hdr + 4);
    pos += 8;
    if (pos + chunk_sz > bytes.size()) break;
    if (std::memcmp(hdr, "fmt ", 4) == 0) {
      if (chunk_sz < 16) fail("corrupt fmt chunk in '" + path + "'");
      fmt_tag = read_u16_le(bytes.data() + pos);
      channels = read_u16_le(bytes.data() + pos + 2);
      sample_rate = read_u32_le(bytes.data() + pos + 4);
      bits_per_sample = read_u16_le(bytes.data() + pos + 14);
      if (fmt_tag == 0xFFFE && chunk_sz >= 40) {
        // WAVE_FORMAT_EXTENSIBLE: SubFormat GUID starts at offset 24 in the fmt chunk.
        fmt_tag = read_u16_le(bytes.data() + pos + 24);
      }
    } else if (std::memcmp(hdr, "data", 4) == 0) {
      data_ptr = bytes.data() + pos;
      data_bytes = chunk_sz;
    }
    pos += chunk_sz + (chunk_sz & 1u);
  }

  if (!data_ptr || channels < 1 || channels > 2 || sample_rate == 0)
    fail("missing or unsupported fmt/data chunks in '" + path + "'");
  std::size_t bytes_per_sample = bits_per_sample / 8;
  if (!((fmt_tag == 1 && (bits_per_sample == 16 || bits_per_sample == 24)) ||
        (fmt_tag == 3 && bits_per_sample == 32)))
    fail("unsupported WAV encoding in '" + path + "' (expected PCM16, PCM24, or FLOAT32)");

  std::size_t frame_bytes = bytes_per_sample * channels;
  int64_t frames = static_cast<int64_t>(data_bytes / frame_bytes);
  Buffer out(static_cast<int>(sample_rate), static_cast<int>(channels), frames);

  for (int64_t f = 0; f < frames; f++) {
    for (int c = 0; c < int(channels); c++) {
      const uint8_t *p = data_ptr + (std::size_t(f) * channels + std::size_t(c)) * bytes_per_sample;
      float val = 0.0f;
      if (fmt_tag == 1 && bits_per_sample == 16) {
        int16_t s = static_cast<int16_t>(read_u16_le(p));
        val = float(s) / 32768.0f;
      } else if (fmt_tag == 1 && bits_per_sample == 24) {
        int32_t s = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
        if (s & 0x800000) s |= ~0xFFFFFF;
        val = float(s) / 8388608.0f;
      } else if (fmt_tag == 3 && bits_per_sample == 32) {
        uint32_t u = read_u32_le(p);
        std::memcpy(&val, &u, sizeof(float));
      }
      out.at(f, c) = val;
    }
  }
  return out;
}

void save_wav(const Buffer &buf, const std::string &path, int64_t bits, bool dry_run) {
  if (path.empty()) fail("save_wav: empty path");
  if (bits != 16 && bits != 24 && bits != 32)
    fail("save_wav: bits must be 16, 24, or 32 (got " + std::to_string(bits) + ")");
  if (dry_run) {
    std::cerr << "lmaudio: --dry-run: would write " << path << "\n";
    return;
  }
  ensure_parent_dir(path);
  std::ofstream out(path, std::ios::binary);
  if (!out) fail("cannot write WAV file '" + path + "'");

  uint16_t fmt_tag = (bits == 32) ? 3 : 1;
  uint16_t channels = static_cast<uint16_t>(buf.channels);
  uint32_t sr = static_cast<uint32_t>(buf.sr);
  uint16_t bytes_per_sample = static_cast<uint16_t>(bits / 8);
  uint16_t block_align = channels * bytes_per_sample;
  uint32_t byte_rate = sr * block_align;
  uint32_t data_bytes = static_cast<uint32_t>(buf.frames) * block_align;
  uint32_t riff_size = 36 + data_bytes;

  out.write("RIFF", 4);
  write_u32_le(out, riff_size);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  write_u32_le(out, 16);
  write_u16_le(out, fmt_tag);
  write_u16_le(out, channels);
  write_u32_le(out, sr);
  write_u32_le(out, byte_rate);
  write_u16_le(out, block_align);
  write_u16_le(out, static_cast<uint16_t>(bits));
  out.write("data", 4);
  write_u32_le(out, data_bytes);

  for (int64_t f = 0; f < buf.frames; f++) {
    for (int c = 0; c < buf.channels; c++) {
      float s = buf.at(f, c);
      if (bits == 16) {
        double clamped = std::clamp(double(s), -1.0, 32767.0 / 32768.0);
        int16_t q = static_cast<int16_t>(std::lrint(clamped * 32768.0));
        write_u16_le(out, static_cast<uint16_t>(q));
      } else if (bits == 24) {
        double clamped = std::clamp(double(s), -1.0, 8388607.0 / 8388608.0);
        int32_t q = static_cast<int32_t>(std::lrint(clamped * 8388608.0));
        char b[3] = {char(q & 0xFF), char((q >> 8) & 0xFF), char((q >> 16) & 0xFF)};
        out.write(b, 3);
      } else {
        uint32_t u = 0;
        std::memcpy(&u, &s, sizeof(float));
        write_u32_le(out, u);
      }
    }
  }
  finish_write(out, path);
}

void save_buffer_csv(const Buffer &buf, const std::string &path, int64_t max_points, int64_t stride, bool dry_run) {
  if (path.empty()) fail("Buffer.save_csv: empty path");
  if (stride < 1) fail("Buffer.save_csv: stride must be >= 1");
  if (dry_run) {
    std::cerr << "lmaudio: --dry-run: would write " << path << "\n";
    return;
  }
  ensure_parent_dir(path);
  std::ofstream out(path, std::ios::binary);
  if (!out) fail("cannot write '" + path + "'");
  if (buf.channels == 1) out << "# t,ch0\n";
  else out << "# t,ch0,ch1\n";
  out << std::setprecision(8);
  int64_t step = stride;
  if (max_points > 0 && buf.frames / step > max_points) {
    step = std::max<int64_t>(1, buf.frames / max_points);
  }
  for (int64_t f = 0; f < buf.frames; f += step) {
    double t = double(f) / double(buf.sr);
    if (buf.channels == 1) {
      out << t << "," << buf.at(f, 0) << "\n";
    } else {
      out << t << "," << buf.at(f, 0) << "," << buf.at(f, 1) << "\n";
    }
  }
  finish_write(out, path);
}

// --- Channel & Stereo helpers ---

Buffer make_stereo(const Buffer &left, const Buffer &right) {
  if (left.sr != right.sr) fail("audio.stereo: sample rates differ");
  if (left.channels != 1 || right.channels != 1) fail("audio.stereo: expected two mono buffers");
  int64_t n = std::max(left.frames, right.frames);
  Buffer out(left.sr, 2, n);
  std::memcpy(out.chan_ptr(0), left.chan_ptr(0), std::size_t(left.frames) * sizeof(float));
  std::memcpy(out.chan_ptr(1), right.chan_ptr(0), std::size_t(right.frames) * sizeof(float));
  return out;
}

Buffer extract_channel(const Buffer &buf, int64_t ch) {
  check_channel(buf, ch);
  Buffer out(buf.sr, 1, buf.frames);
  std::memcpy(out.chan_ptr(0), buf.chan_ptr(static_cast<int>(ch)), std::size_t(buf.frames) * sizeof(float));
  return out;
}

Buffer to_mono(const Buffer &buf) {
  if (buf.channels == 1) return buf;
  Buffer out(buf.sr, 1, buf.frames);
  const float *l = buf.chan_ptr(0);
  const float *r = buf.chan_ptr(1);
  float *dst = out.chan_ptr(0);
  for (int64_t i = 0; i < buf.frames; i++) dst[i] = 0.5f * (l[i] + r[i]);
  return out;
}

// --- Editing, Mixing & Envelopes ---

Buffer apply_gain(const Buffer &buf, double factor) {
  check_finite(factor, "factor");
  if (std::abs(factor) > 1e12) fail("gain factor magnitude must be <= 1e12");
  Buffer out = buf;
  float g = static_cast<float>(factor);
  for (float &x : out.data) x *= g;
  return out;
}

Buffer apply_gain_db(const Buffer &buf, double db) {
  check_finite(db, "db");
  if (db < -240.0 || db > 240.0) fail("gain_db must be in [-240, 240] dB");
  return apply_gain(buf, std::pow(10.0, db / 20.0));
}

Buffer apply_normalize(const Buffer &buf, double target_peak) {
  check_finite(target_peak, "target_peak");
  if (target_peak <= 0.0 || target_peak > 1e6) fail("normalize: target_peak must be in (0, 1e6]");
  float pk = 0.0f;
  for (float x : buf.data) pk = std::max(pk, std::abs(x));
  if (pk < 1e-12f) return buf;
  return apply_gain(buf, target_peak / double(pk));
}

Buffer apply_normalize_rms(const Buffer &buf, double target_rms) {
  check_finite(target_rms, "target_rms");
  if (target_rms <= 0.0 || target_rms > 1e6) fail("normalize_rms: target_rms must be in (0, 1e6]");
  if (buf.data.empty()) return buf;
  double sum = 0.0;
  for (float x : buf.data) sum += double(x) * double(x);
  double r = std::sqrt(sum / double(buf.data.size()));
  if (r < 1e-12) return buf;
  return apply_gain(buf, target_rms / r);
}

Buffer apply_fade(const Buffer &buf, double fade_in_sec, double fade_out_sec, const std::string &shape) {
  int64_t n_in = std::min(buf.frames, sec_to_frames_checked(fade_in_sec, buf.sr, "fade_in_sec", 0.0, 3600.0));
  int64_t n_out = std::min(buf.frames, sec_to_frames_checked(fade_out_sec, buf.sr, "fade_out_sec", 0.0, 3600.0));
  bool cosine = false;
  if (shape == "cosine" || shape == "hann") cosine = true;
  else if (shape != "linear") fail("unknown fade shape '" + shape + "' (expected linear or cosine)");

  Buffer out = buf;
  for (int c = 0; c < out.channels; c++) {
    float *p = out.chan_ptr(c);
    for (int64_t i = 0; i < n_in; i++) {
      double u = double(i) / double(std::max<int64_t>(1, n_in));
      double g = cosine ? (0.5 * (1.0 - std::cos(kPi * u))) : u;
      p[i] *= static_cast<float>(g);
    }
    for (int64_t i = 0; i < n_out; i++) {
      double u = double(i) / double(std::max<int64_t>(1, n_out));
      double g = cosine ? (0.5 * (1.0 - std::cos(kPi * u))) : u;
      p[buf.frames - 1 - i] *= static_cast<float>(g);
    }
  }
  return out;
}

Buffer apply_adsr(const Buffer &buf, double attack, double decay, double sustain, double release, double gate_sec) {
  check_finite(attack, "attack");
  check_finite(decay, "decay");
  check_finite(sustain, "sustain");
  check_finite(release, "release");
  check_finite(gate_sec, "gate_sec");
  if (attack < 0.0 || attack > 3600.0 || decay < 0.0 || decay > 3600.0 || release < 0.0 || release > 3600.0 ||
      gate_sec < 0.0 || gate_sec > 3600.0 || sustain < 0.0 || sustain > 1.0)
    fail("invalid ADSR parameters");

  Buffer out = buf;
  double dur = buf.duration();
  double g_sec = (gate_sec > 0.0) ? gate_sec : std::max(attack + decay, dur - release);
  int64_t gate_frames = sec_to_frames_checked(g_sec, buf.sr, "gate_sec", 0.0, 7200.0);

  daisysp::Adsr env;
  env.Init(static_cast<float>(buf.sr));
  env.SetTime(daisysp::ADSR_SEG_ATTACK, static_cast<float>( std::max(0.0005, attack) ));
  env.SetTime(daisysp::ADSR_SEG_DECAY, static_cast<float>( std::max(0.0005, decay) ));
  env.SetSustainLevel(static_cast<float>(sustain));
  env.SetTime(daisysp::ADSR_SEG_RELEASE, static_cast<float>( std::max(0.0005, release) ));

  for (int64_t i = 0; i < out.frames; i++) {
    float g = env.Process(i < gate_frames);
    for (int c = 0; c < out.channels; c++) out.at(i, c) *= g;
  }
  return out;
}

Buffer apply_pan(const Buffer &buf, double pos) {
  check_finite(pos, "pos");
  if (pos < -1.0 || pos > 1.0) fail("pan position must be in [-1, 1]");
  Buffer mono = to_mono(buf);
  Buffer out(buf.sr, 2, buf.frames);
  double angle = (pos + 1.0) * (kPi * 0.25);
  float gl = static_cast<float>(std::cos(angle));
  float gr = static_cast<float>(std::sin(angle));
  const float *src = mono.chan_ptr(0);
  float *l = out.chan_ptr(0);
  float *r = out.chan_ptr(1);
  for (int64_t i = 0; i < buf.frames; i++) {
    l[i] = src[i] * gl;
    r[i] = src[i] * gr;
  }
  return out;
}

Buffer apply_slice(const Buffer &buf, double start_sec, double end_sec) {
  int64_t s = sec_to_frames_checked(start_sec, buf.sr, "slice start_sec", 0.0, 3600.0);
  int64_t e = sec_to_frames_checked(end_sec, buf.sr, "slice end_sec", 0.0, 3600.0);
  if (s < 0 || e <= s || s >= buf.frames)
    fail("slice range [" + std::to_string(start_sec) + ", " + std::to_string(end_sec) + "] invalid");
  e = std::min(e, buf.frames);
  Buffer out(buf.sr, buf.channels, e - s);
  for (int c = 0; c < buf.channels; c++) {
    std::memcpy(out.chan_ptr(c), buf.chan_ptr(c) + s, std::size_t(e - s) * sizeof(float));
  }
  return out;
}

Buffer apply_pad(const Buffer &buf, double before_sec, double after_sec) {
  int64_t nb = sec_to_frames_checked(before_sec, buf.sr, "pad before_sec", 0.0, 3600.0);
  int64_t na = sec_to_frames_checked(after_sec, buf.sr, "pad after_sec", 0.0, 3600.0);
  Buffer out(buf.sr, buf.channels, nb + buf.frames + na);
  for (int c = 0; c < buf.channels; c++) {
    std::memcpy(out.chan_ptr(c) + nb, buf.chan_ptr(c), std::size_t(buf.frames) * sizeof(float));
  }
  return out;
}

Buffer apply_concat(const Buffer &a, const Buffer &b) {
  if (a.sr != b.sr) fail("concat: sample rates differ (" + std::to_string(a.sr) + " vs " + std::to_string(b.sr) + ")");
  if (a.channels != b.channels) fail("concat: channel counts differ");
  Buffer out(a.sr, a.channels, a.frames + b.frames);
  for (int c = 0; c < a.channels; c++) {
    std::memcpy(out.chan_ptr(c), a.chan_ptr(c), std::size_t(a.frames) * sizeof(float));
    std::memcpy(out.chan_ptr(c) + a.frames, b.chan_ptr(c), std::size_t(b.frames) * sizeof(float));
  }
  return out;
}

Buffer apply_mix(const Buffer &a, const Buffer &b, double b_gain, double offset_sec) {
  check_finite(b_gain, "b_gain");
  if (std::abs(b_gain) > 1e12) fail("mix: b_gain magnitude must be <= 1e12");
  if (a.sr != b.sr) fail("mix: sample rates differ");
  int64_t off = sec_to_frames_checked(offset_sec, a.sr, "mix offset_sec", 0.0, 3600.0);
  int out_ch = std::max(a.channels, b.channels);
  int64_t total = std::max(a.frames, off + b.frames);
  Buffer out(a.sr, out_ch, total);
  for (int c = 0; c < out_ch; c++) {
    int ac = std::min(c, a.channels - 1);
    std::memcpy(out.chan_ptr(c), a.chan_ptr(ac), std::size_t(a.frames) * sizeof(float));
    int bc = std::min(c, b.channels - 1);
    const float *bp = b.chan_ptr(bc);
    float *op = out.chan_ptr(c) + off;
    float g = static_cast<float>(b_gain);
    for (int64_t i = 0; i < b.frames; i++) op[i] += bp[i] * g;
  }
  return out;
}

Buffer apply_reverse(const Buffer &buf) {
  Buffer out = buf;
  for (int c = 0; c < out.channels; c++) {
    float *p = out.chan_ptr(c);
    std::reverse(p, p + out.frames);
  }
  return out;
}

Buffer apply_resample(const Buffer &buf, int64_t new_sr) {
  check_sr(new_sr);
  if (new_sr == buf.sr) return buf;
  double ratio = double(new_sr) / double(buf.sr);
  int64_t out_frames = std::max<int64_t>(1, static_cast<int64_t>(std::llround(double(buf.frames) * ratio)));
  Buffer out(static_cast<int>(new_sr), buf.channels, out_frames);
  constexpr int kRadius = 6;
  double scale = std::min(1.0, ratio);
  for (int c = 0; c < buf.channels; c++) {
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    for (int64_t i = 0; i < out_frames; i++) {
      double src_pos = double(i) / ratio;
      int64_t center = static_cast<int64_t>(std::floor(src_pos));
      double sum = 0.0, wsum = 0.0;
      int half_w = static_cast<int>( std::ceil(double(kRadius) / scale) );
      for (int k = -half_w; k <= half_w; k++) {
        int64_t idx = center + k;
        if (idx < 0 || idx >= buf.frames) continue;
        double x = (src_pos - double(idx)) * scale;
        double w = 1.0;
        if (std::abs(x) >= double(kRadius)) continue;
        if (std::abs(x) > 1e-9) {
          double pix = kPi * x;
          w = (std::sin(pix) / pix) * (std::sin(pix / double(kRadius)) / (pix / double(kRadius)));
        }
        sum += double(in_p[idx]) * w;
        wsum += w;
      }
      out_p[i] = static_cast<float>(wsum > 1e-12 ? (sum / wsum) : 0.0);
    }
  }
  return out;
}

// --- Filters & Effects ---

Buffer apply_svf(const Buffer &buf, const std::string &mode, double cutoff, double res, double drive) {
  check_finite(cutoff, "cutoff");
  check_finite(res, "res");
  check_finite(drive, "drive");
  if (cutoff <= 0.0 || cutoff >= double(buf.sr) * 0.49)
    fail("svf cutoff must be in (0, 0.49*sr)");
  if (res < 0.0 || res > 1.0) fail("svf resonance must be in [0, 1]");
  int m = 0;
  if (mode == "lowpass" || mode == "lpf") m = 0;
  else if (mode == "highpass" || mode == "hpf") m = 1;
  else if (mode == "bandpass" || mode == "bpf") m = 2;
  else if (mode == "notch") m = 3;
  else if (mode == "peak") m = 4;
  else fail("unknown svf mode '" + mode + "' (expected lowpass, highpass, bandpass, notch, or peak)");

  Buffer out(buf.sr, buf.channels, buf.frames);
  for (int c = 0; c < buf.channels; c++) {
    daisysp::Svf svf;
    svf.Init(static_cast<float>(buf.sr));
    svf.SetFreq(static_cast<float>(cutoff));
    svf.SetRes(static_cast<float>(res));
    svf.SetDrive(static_cast<float>(drive));
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    for (int64_t i = 0; i < buf.frames; i++) {
      svf.Process(in_p[i]);
      switch (m) {
        case 0: out_p[i] = svf.Low(); break;
        case 1: out_p[i] = svf.High(); break;
        case 2: out_p[i] = svf.Band(); break;
        case 3: out_p[i] = svf.Notch(); break;
        case 4: out_p[i] = svf.Peak(); break;
      }
    }
  }
  return out;
}

Buffer apply_ladder(const Buffer &buf, double cutoff, double res, double drive, const std::string &mode) {
  check_finite(cutoff, "cutoff");
  check_finite(res, "res");
  check_finite(drive, "drive");
  if (cutoff <= 5.0 || cutoff >= double(buf.sr) * 0.49)
    fail("ladder cutoff must be in (5, 0.49*sr)");
  daisysp::LadderFilter::FilterMode fm = daisysp::LadderFilter::FilterMode::LP24;
  if (mode == "lp24" || mode == "lowpass") fm = daisysp::LadderFilter::FilterMode::LP24;
  else if (mode == "lp12") fm = daisysp::LadderFilter::FilterMode::LP12;
  else if (mode == "bp24" || mode == "bandpass") fm = daisysp::LadderFilter::FilterMode::BP24;
  else if (mode == "bp12") fm = daisysp::LadderFilter::FilterMode::BP12;
  else if (mode == "hp24" || mode == "highpass") fm = daisysp::LadderFilter::FilterMode::HP24;
  else if (mode == "hp12") fm = daisysp::LadderFilter::FilterMode::HP12;
  else fail("unknown ladder filter mode '" + mode + "' (expected lp24, lp12, bp24, bp12, hp24, or hp12)");

  Buffer out(buf.sr, buf.channels, buf.frames);
  for (int c = 0; c < buf.channels; c++) {
    daisysp::LadderFilter lf;
    lf.Init(static_cast<float>(buf.sr));
    lf.SetFilterMode(fm);
    lf.SetFreq(static_cast<float>(cutoff));
    lf.SetRes(static_cast<float>(res));
    lf.SetInputDrive(static_cast<float>(drive));
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    for (int64_t i = 0; i < buf.frames; i++) out_p[i] = lf.Process(in_p[i]);
  }
  return out;
}

Buffer apply_lpf_sweep(const Buffer &buf, double f0, double f1, double res) {
  check_finite(f0, "f0");
  check_finite(f1, "f1");
  check_finite(res, "res");
  if (f0 <= 5.0 || f1 <= 5.0 || f0 >= double(buf.sr) * 0.48 || f1 >= double(buf.sr) * 0.48)
    fail("lpf_sweep frequencies must be in (5, 0.48*sr)");
  Buffer out(buf.sr, buf.channels, buf.frames);
  double log_f0 = std::log(f0), log_f1 = std::log(f1);
  for (int c = 0; c < buf.channels; c++) {
    daisysp::LadderFilter lf;
    lf.Init(static_cast<float>(buf.sr));
    lf.SetRes(static_cast<float>(res));
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    for (int64_t i = 0; i < buf.frames; i++) {
      double u = (buf.frames > 1) ? double(i) / double(buf.frames - 1) : 0.0;
      lf.SetFreq(static_cast<float>(std::exp(log_f0 + u * (log_f1 - log_f0))));
      out_p[i] = lf.Process(in_p[i]);
    }
  }
  return out;
}

Buffer apply_delay(const Buffer &buf, double delay_sec, double feedback, double wet) {
  check_finite(delay_sec, "delay_sec");
  check_finite(feedback, "feedback");
  check_finite(wet, "wet");
  if (delay_sec <= 0.0 || delay_sec > 10.0) fail("delay_sec must be in (0, 10]");
  if (std::abs(feedback) >= 0.995) fail("delay feedback magnitude must be < 0.995");
  if (wet < 0.0 || wet > 1.0) fail("wet mix must be in [0, 1]");
  int64_t d_samps = std::max<int64_t>(1, static_cast<int64_t>(std::llround(delay_sec * double(buf.sr))));
  membudget::require(std::size_t(d_samps) * sizeof(float));
  Buffer out(buf.sr, buf.channels, buf.frames);
  for (int c = 0; c < buf.channels; c++) {
    std::vector<float> line(static_cast<std::size_t>(d_samps), 0.0f);
    std::size_t wp = 0;
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    for (int64_t i = 0; i < buf.frames; i++) {
      float delayed = line[wp];
      float x = in_p[i];
      line[wp] = x + delayed * static_cast<float>(feedback);
      wp = (wp + 1) % line.size();
      out_p[i] = static_cast<float>((1.0 - wet) * x + wet * delayed);
    }
  }
  return out;
}

// Clean-room MIT 8-line Feedback Delay Network (FDN) stereo reverb with Householder
// unitary mixing matrix H = I - (2/8)*1*1^T and 1-pole lowpass damping per delay line.
Buffer apply_reverb(const Buffer &buf, double feedback, double lpf_freq, double wet) {
  check_finite(feedback, "feedback");
  check_finite(lpf_freq, "lpf_freq");
  check_unit_range(wet, "reverb wet");
  if (feedback < 0.0 || feedback >= 1.0) fail("reverb feedback must be in [0, 1)");
  if (lpf_freq < 20.0 || lpf_freq >= double(buf.sr) * 0.49)
    fail("reverb lpf_freq must be in [20, 0.49*sr)");

  static constexpr double kDelaysSec[8] = {0.0297, 0.0371, 0.0411, 0.0437, 0.0531, 0.0593, 0.0677, 0.0739};
  std::vector<float> lines[8];
  std::size_t pos[8] = {0};
  double lp_state[8] = {0.0};
  for (int k = 0; k < 8; k++) {
    std::size_t len = std::max<std::size_t>(8, static_cast<std::size_t>(std::llround(kDelaysSec[k] * double(buf.sr))));
    lines[k].assign(len, 0.0f);
  }
  double a_lp = std::exp(-kTwoPi * lpf_freq / double(buf.sr));
  double b_lp = 1.0 - a_lp;

  Buffer out(buf.sr, buf.channels, buf.frames);
  const float *inL = buf.chan_ptr(0);
  const float *inR = buf.chan_ptr(buf.channels > 1 ? 1 : 0);
  float *outL = out.chan_ptr(0);
  float *outR = out.chan_ptr(buf.channels > 1 ? 1 : 0);

  for (int64_t i = 0; i < buf.frames; i++) {
    double xL = inL[i], xR = inR[i];
    double rd[8];
    double sum = 0.0;
    for (int k = 0; k < 8; k++) {
      rd[k] = lines[k][pos[k]];
      sum += rd[k];
    }
    double hh = 0.25 * sum;
    double yL = 0.0, yR = 0.0;
    for (int k = 0; k < 8; k++) {
      double mixed = rd[k] - hh;
      lp_state[k] = b_lp * mixed + a_lp * lp_state[k];
      double exc = (k & 1) ? xR : xL;
      lines[k][pos[k]] = static_cast<float>(exc * 0.25 + feedback * lp_state[k]);
      pos[k] = (pos[k] + 1) % lines[k].size();
      if (k & 1) yR += rd[k];
      else yL += rd[k];
    }
    yL *= 0.5;
    yR *= 0.5;
    if (buf.channels == 1) {
      outL[i] = static_cast<float>((1.0 - wet) * xL + wet * 0.5 * (yL + yR));
    } else {
      outL[i] = static_cast<float>((1.0 - wet) * xL + wet * yL);
      outR[i] = static_cast<float>((1.0 - wet) * xR + wet * yR);
    }
  }
  return out;
}

// Clean-room MIT 8-line FDN reverb with independent low/mid RT60 2-band crossover damping
// and HF damping filter (no GPL-2 zita-rev1 code).
Buffer apply_zitarev(const Buffer &buf, double rt60_low, double rt60_mid, double damping, double wet) {
  check_finite(rt60_low, "rt60_low");
  check_finite(rt60_mid, "rt60_mid");
  check_finite(damping, "damping");
  check_unit_range(wet, "zitarev wet");
  if (rt60_low <= 0.1 || rt60_mid <= 0.1) fail("zitarev RT60 must be > 0.1 s");
  if (damping < 20.0 || damping >= double(buf.sr) * 0.49)
    fail("zitarev damping frequency must be in [20, 0.49*sr)");

  static constexpr double kDelaysSec[8] = {0.0243, 0.0317, 0.0389, 0.0461, 0.0523, 0.0607, 0.0683, 0.0761};
  std::vector<float> lines[8];
  std::size_t pos[8] = {0};
  double g_low[8], g_mid[8], lp_xo[8] = {0.0}, lp_hf[8] = {0.0};
  for (int k = 0; k < 8; k++) {
    std::size_t len = std::max<std::size_t>(8, static_cast<std::size_t>(std::llround(kDelaysSec[k] * double(buf.sr))));
    lines[k].assign(len, 0.0f);
    double tau = double(len) / double(buf.sr);
    g_low[k] = std::pow(10.0, -3.0 * tau / rt60_low);
    g_mid[k] = std::pow(10.0, -3.0 * tau / rt60_mid);
  }
  double a_xo = std::exp(-kTwoPi * 250.0 / double(buf.sr));
  double b_xo = 1.0 - a_xo;
  double a_hf = std::exp(-kTwoPi * damping / double(buf.sr));
  double b_hf = 1.0 - a_hf;

  Buffer out(buf.sr, buf.channels, buf.frames);
  const float *inL = buf.chan_ptr(0);
  const float *inR = buf.chan_ptr(buf.channels > 1 ? 1 : 0);
  float *outL = out.chan_ptr(0);
  float *outR = out.chan_ptr(buf.channels > 1 ? 1 : 0);

  for (int64_t i = 0; i < buf.frames; i++) {
    double xL = inL[i], xR = inR[i];
    double rd[8];
    double sum = 0.0;
    for (int k = 0; k < 8; k++) {
      rd[k] = lines[k][pos[k]];
      sum += rd[k];
    }
    double hh = 0.25 * sum;
    double yL = 0.0, yR = 0.0;
    for (int k = 0; k < 8; k++) {
      double mixed = rd[k] - hh;
      lp_xo[k] = b_xo * mixed + a_xo * lp_xo[k];
      double band = g_low[k] * lp_xo[k] + g_mid[k] * (mixed - lp_xo[k]);
      lp_hf[k] = b_hf * band + a_hf * lp_hf[k];
      double exc = (k & 1) ? xR : xL;
      lines[k][pos[k]] = static_cast<float>(exc * 0.25 + lp_hf[k]);
      pos[k] = (pos[k] + 1) % lines[k].size();
      if (k & 1) yR += rd[k];
      else yL += rd[k];
    }
    yL *= 0.5;
    yR *= 0.5;
    if (buf.channels == 1) {
      outL[i] = static_cast<float>((1.0 - wet) * xL + wet * 0.5 * (yL + yR));
    } else {
      outL[i] = static_cast<float>((1.0 - wet) * xL + wet * yL);
      outR[i] = static_cast<float>((1.0 - wet) * xR + wet * yR);
    }
  }
  return out;
}

Buffer apply_chorus(const Buffer &buf, double rate, double depth, double feedback, double wet) {
  check_finite(rate, "rate");
  check_unit_range(depth, "chorus depth");
  check_finite(feedback, "feedback");
  check_unit_range(wet, "chorus wet");
  if (rate <= 0.0 || rate > 100.0) fail("chorus rate must be in (0, 100] Hz");
  if (feedback < 0.0 || feedback > 0.99) fail("chorus feedback must be in [0, 0.99]");
  Buffer out(buf.sr, buf.channels, buf.frames);
  for (int c = 0; c < buf.channels; c++) {
    daisysp::Chorus ch;
    ch.Init(static_cast<float>(buf.sr));
    ch.SetLfoFreq(static_cast<float>(rate));
    ch.SetLfoDepth(static_cast<float>(depth));
    ch.SetFeedback(static_cast<float>(feedback));
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    for (int64_t i = 0; i < buf.frames; i++) {
      float y = ch.Process(in_p[i]);
      out_p[i] = static_cast<float>((1.0 - wet) * in_p[i] + wet * y);
    }
  }
  return out;
}

Buffer apply_flanger(const Buffer &buf, double rate, double depth, double feedback, double wet) {
  check_finite(rate, "rate");
  check_unit_range(depth, "flanger depth");
  check_finite(feedback, "feedback");
  check_unit_range(wet, "flanger wet");
  if (rate <= 0.0 || rate > 100.0) fail("flanger rate must be in (0, 100] Hz");
  if (feedback < 0.0 || feedback > 0.99) fail("flanger feedback must be in [0, 0.99]");
  Buffer out(buf.sr, buf.channels, buf.frames);
  for (int c = 0; c < buf.channels; c++) {
    daisysp::Flanger fl;
    fl.Init(static_cast<float>(buf.sr));
    fl.SetLfoFreq(static_cast<float>(rate));
    fl.SetLfoDepth(static_cast<float>(depth));
    fl.SetFeedback(static_cast<float>(feedback));
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    for (int64_t i = 0; i < buf.frames; i++) {
      float y = fl.Process(in_p[i]);
      out_p[i] = static_cast<float>((1.0 - wet) * in_p[i] + wet * y);
    }
  }
  return out;
}

Buffer apply_phaser(const Buffer &buf, double rate, double depth, double feedback, int64_t poles, double wet) {
  check_finite(rate, "rate");
  check_unit_range(depth, "phaser depth");
  check_finite(feedback, "feedback");
  check_unit_range(wet, "phaser wet");
  if (rate <= 0.0 || rate > 100.0) fail("phaser rate must be in (0, 100] Hz");
  if (feedback < 0.0 || feedback > 0.99) fail("phaser feedback must be in [0, 0.99]");
  if (poles < 1 || poles > 8) fail("phaser poles must be in [1, 8]");
  Buffer out(buf.sr, buf.channels, buf.frames);
  for (int c = 0; c < buf.channels; c++) {
    daisysp::Phaser ph;
    ph.Init(static_cast<float>(buf.sr));
    ph.SetPoles(static_cast<int>(poles));
    ph.SetLfoFreq(static_cast<float>(rate));
    ph.SetLfoDepth(static_cast<float>(depth));
    ph.SetFeedback(static_cast<float>(feedback));
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    for (int64_t i = 0; i < buf.frames; i++) {
      float y = ph.Process(in_p[i]);
      out_p[i] = static_cast<float>((1.0 - wet) * in_p[i] + wet * y);
    }
  }
  return out;
}

Buffer apply_overdrive(const Buffer &buf, double drive, double wet) {
  check_unit_range(drive, "overdrive drive");
  check_unit_range(wet, "overdrive wet");
  Buffer out(buf.sr, buf.channels, buf.frames);
  for (int c = 0; c < buf.channels; c++) {
    daisysp::Overdrive od;
    od.Init();
    od.SetDrive(static_cast<float>(drive));
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    for (int64_t i = 0; i < buf.frames; i++) {
      float y = od.Process(in_p[i]);
      out_p[i] = static_cast<float>((1.0 - wet) * in_p[i] + wet * y);
    }
  }
  return out;
}

Buffer apply_wavefold(const Buffer &buf, double gain, double offset) {
  check_finite(gain, "gain");
  check_finite(offset, "offset");
  if (gain <= 0.0 || gain > 1000.0) fail("wavefold gain must be in (0, 1000]");
  Buffer out(buf.sr, buf.channels, buf.frames);
  for (int c = 0; c < buf.channels; c++) {
    daisysp::Wavefolder wf;
    wf.Init();
    wf.SetGain(static_cast<float>(gain));
    wf.SetOffset(static_cast<float>(offset));
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    for (int64_t i = 0; i < buf.frames; i++) out_p[i] = wf.Process(in_p[i]);
  }
  return out;
}

Buffer apply_bitcrush(const Buffer &buf, int64_t bitdepth, double crush_sr) {
  check_finite(crush_sr, "crush_sr");
  if (bitdepth < 1 || bitdepth > 16) fail("bitcrush bitdepth must be in [1, 16]");
  if (crush_sr <= 50.0 || crush_sr > double(buf.sr)) fail("bitcrush crush_sr must be in (50, sr]");
  Buffer out(buf.sr, buf.channels, buf.frames);
  const double levels = std::pow(2.0, std::floor(bitdepth));
  const double step_samples = std::max(1.0, double(buf.sr) / crush_sr);
  for (int c = 0; c < buf.channels; c++) {
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    double next_trigger = 0.0;
    float held = 0.0f;
    for (int64_t i = 0; i < buf.frames; i++) {
      double q = std::floor((double(in_p[i]) * 0.5 + 0.5) * levels) / levels * 2.0 - 1.0;
      if (double(i) >= next_trigger) {
        next_trigger += step_samples;
        held = static_cast<float>(q);
      }
      out_p[i] = held;
    }
  }
  return out;
}

Buffer apply_compress(const Buffer &buf, double ratio, double thresh_db, double attack, double release, double makeup_db) {
  check_finite(ratio, "ratio");
  check_finite(thresh_db, "thresh_db");
  check_finite(attack, "attack");
  check_finite(release, "release");
  check_finite(makeup_db, "makeup_db");
  if (ratio < 1.0 || ratio > 1000.0) fail("compress ratio must be in [1, 1000]");
  if (thresh_db < -120.0 || thresh_db > 0.0) fail("compress thresh_db must be in [-120, 0] dB");
  if (attack <= 0.0 || attack > 10.0) fail("compress attack must be in (0, 10] s");
  if (release <= 0.0 || release > 10.0) fail("compress release must be in (0, 10] s");
  if (makeup_db < -120.0 || makeup_db > 120.0) fail("compress makeup_db must be in [-120, 120] dB");
  float makeup = static_cast<float>(std::pow(10.0, makeup_db / 20.0));
  Buffer out(buf.sr, buf.channels, buf.frames);
  for (int c = 0; c < buf.channels; c++) {
    SpGuard g(buf.sr);
    g.charge_extra(sizeof(sp_compressor) + 256);
    sp_compressor *comp = nullptr;
    sp_compressor_create(&comp);
    sp_compressor_init(g.sp, comp);
    *comp->ratio = static_cast<SPFLOAT>(ratio);
    *comp->thresh = static_cast<SPFLOAT>(thresh_db);
    *comp->atk = static_cast<SPFLOAT>(attack);
    *comp->rel = static_cast<SPFLOAT>(release);
    const float *in_p = buf.chan_ptr(c);
    float *out_p = out.chan_ptr(c);
    for (int64_t i = 0; i < buf.frames; i++) {
      SPFLOAT x = in_p[i], y = 0.0f;
      sp_compressor_compute(g.sp, comp, &x, &y);
      out_p[i] = y * makeup;
    }
    sp_compressor_destroy(&comp);
  }
  return out;
}

Buffer apply_paulstretch(const Buffer &buf, double stretch, double window_sec) {
  check_finite(stretch, "stretch");
  check_finite(window_sec, "window_sec");
  if (stretch < 0.5 || stretch > 100.0) fail("paulstretch stretch factor must be in [0.5, 100]");
  if (window_sec <= 0.01 || window_sec > 2.0) fail("paulstretch window_sec must be in (0.01, 2.0]");
  int64_t out_frames = std::max<int64_t>(1, static_cast<int64_t>(std::llround(double(buf.frames) * stretch)));
  Buffer out(buf.sr, buf.channels, out_frames);
  std::size_t win_samps = std::max<std::size_t>(16, static_cast<std::size_t>(buf.sr * window_sec));
  std::size_t sp_bytes = sizeof(sp_ftbl) + std::size_t(buf.frames) * sizeof(float) +
                         sizeof(sp_paulstretch) + win_samps * sizeof(float) * 8;
  for (int c = 0; c < buf.channels; c++) {
    SpGuard g(buf.sr);
    g.charge_extra(sp_bytes);
    sp_ftbl *ft = nullptr;
    sp_ftbl_create(g.sp, &ft, static_cast<size_t>(buf.frames));
    std::memcpy(ft->tbl, buf.chan_ptr(c), std::size_t(buf.frames) * sizeof(float));
    sp_paulstretch *ps = nullptr;
    sp_paulstretch_create(&ps);
    sp_paulstretch_init(g.sp, ps, ft, static_cast<SPFLOAT>(window_sec), static_cast<SPFLOAT>(stretch));
    float *out_p = out.chan_ptr(c);
    for (int64_t i = 0; i < out_frames; i++) {
      SPFLOAT dummy = 0.0f, y = 0.0f;
      sp_paulstretch_compute(g.sp, ps, &dummy, &y);
      out_p[i] = y;
    }
    sp_paulstretch_destroy(&ps);
    sp_ftbl_destroy(&ft);
  }
  return out;
}

// --- Analysis & Spectral Transforms ---

double compute_rms(const Buffer &buf, int64_t ch) {
  check_channel(buf, ch);
  const float *p = buf.chan_ptr(static_cast<int>(ch));
  double sum = 0.0;
  for (int64_t i = 0; i < buf.frames; i++) sum += double(p[i]) * double(p[i]);
  return std::sqrt(sum / double(buf.frames));
}

double compute_peak(const Buffer &buf, int64_t ch) {
  check_channel(buf, ch);
  const float *p = buf.chan_ptr(static_cast<int>(ch));
  double pk = 0.0;
  for (int64_t i = 0; i < buf.frames; i++) pk = std::max(pk, std::abs(double(p[i])));
  return pk;
}

double compute_dc_offset(const Buffer &buf, int64_t ch) {
  check_channel(buf, ch);
  const float *p = buf.chan_ptr(static_cast<int>(ch));
  double sum = 0.0;
  for (int64_t i = 0; i < buf.frames; i++) sum += double(p[i]);
  return sum / double(buf.frames);
}

double compute_energy(const Buffer &buf, int64_t ch) {
  check_channel(buf, ch);
  const float *p = buf.chan_ptr(static_cast<int>(ch));
  double sum = 0.0;
  for (int64_t i = 0; i < buf.frames; i++) sum += double(p[i]) * double(p[i]);
  return sum;
}

double compute_zcr(const Buffer &buf, int64_t ch) {
  check_channel(buf, ch);
  if (buf.frames < 2) return 0.0;
  const float *p = buf.chan_ptr(static_cast<int>(ch));
  int64_t crossings = 0;
  for (int64_t i = 1; i < buf.frames; i++) {
    if ((p[i - 1] >= 0.0f && p[i] < 0.0f) || (p[i - 1] < 0.0f && p[i] >= 0.0f)) crossings++;
  }
  return double(crossings) / buf.duration();
}

// YIN fundamental frequency estimator with parabolic lag refinement.
double compute_pitch(const Buffer &buf, double fmin, double fmax, int64_t ch) {
  check_channel(buf, ch);
  check_finite(fmin, "fmin");
  check_finite(fmax, "fmax");
  if (fmin <= 10.0 || fmax <= fmin || fmax >= double(buf.sr) * 0.5)
    fail("pitch frequency range [fmin, fmax] invalid");
  int64_t tau_min = std::max<int64_t>(2, static_cast<int64_t>(std::floor(double(buf.sr) / fmax)));
  int64_t tau_max = static_cast<int64_t>(std::ceil(double(buf.sr) / fmin));
  if (buf.frames < tau_max * 2 + 4) {
    tau_max = buf.frames / 2 - 2;
  }
  if (tau_max <= tau_min) fail("buffer too short for pitch detection at fmin=" + std::to_string(fmin));

  int64_t W = std::min<int64_t>(std::max<int64_t>(tau_max * 2, 2048), buf.frames - tau_max - 1);
  // Pick the most energetic window in the first 75% of the buffer (handles plucked/percussive attacks).
  const float *p = buf.chan_ptr(static_cast<int>(ch));
  int64_t best_start = 0;
  double best_e = -1.0;
  int64_t max_start = std::max<int64_t>(0, buf.frames - W - tau_max - 1);
  int64_t step = std::max<int64_t>(1, max_start / 8);
  for (int64_t s = 0; s <= max_start; s += step) {
    double e = 0.0;
    for (int64_t i = 0; i < W; i++) e += double(p[s + i]) * double(p[s + i]);
    if (e > best_e) {
      best_e = e;
      best_start = s;
    }
  }
  if (best_e < 1e-18) return 0.0;
  const float *x = p + best_start;

  std::vector<double> d(static_cast<std::size_t>(tau_max + 1), 0.0);
  std::vector<double> cmndf(static_cast<std::size_t>(tau_max + 1), 1.0);
  double running_sum = 0.0;
  for (int64_t tau = 1; tau <= tau_max; tau++) {
    double diff = 0.0;
    for (int64_t j = 0; j < W; j++) {
      double delta = double(x[j]) - double(x[j + tau]);
      diff += delta * delta;
    }
    d[std::size_t(tau)] = diff;
    running_sum += diff;
    if (running_sum > 1e-18) {
      cmndf[std::size_t(tau)] = diff * double(tau) / running_sum;
    }
  }
  if (running_sum < 1e-18) return 0.0;

  constexpr double kThreshold = 0.12;
  int64_t best_tau = -1;
  for (int64_t tau = tau_min; tau <= tau_max; tau++) {
    if (cmndf[std::size_t(tau)] < kThreshold) {
      while (tau + 1 <= tau_max && cmndf[std::size_t(tau + 1)] < cmndf[std::size_t(tau)]) {
        tau++;
      }
      best_tau = tau;
      break;
    }
  }
  if (best_tau < 0) {
    best_tau = tau_min;
    for (int64_t tau = tau_min + 1; tau <= tau_max; tau++) {
      if (cmndf[std::size_t(tau)] < cmndf[std::size_t(best_tau)]) best_tau = tau;
    }
    if (cmndf[std::size_t(best_tau)] > 0.85) return 0.0;
  }

  // Refine the CMNDF-selected lag best_tau using the symmetric difference function d(tau)
  // (avoids the tau/running_sum tilt bias of d'(tau) on pure sinusoids, de Cheveigne & Kawahara 2002 II.E).
  double refined_tau = double(best_tau);
  if (best_tau > 1 && best_tau < tau_max) {
    double y0 = d[std::size_t(best_tau - 1)];
    double y1 = d[std::size_t(best_tau)];
    double y2 = d[std::size_t(best_tau + 1)];
    double denom = 2.0 * (y0 - 2.0 * y1 + y2);
    if (std::abs(denom) > 1e-15) {
      double delta = (y0 - y2) / denom;
      refined_tau += std::clamp(delta, -0.5, 0.5);
    }
  }
  return std::clamp(double(buf.sr) / refined_tau, fmin, fmax);
}

Spectrum compute_spectrum(const Buffer &buf, const std::string &window, int64_t nfft, int64_t ch) {
  check_channel(buf, ch);
  if (nfft < 0) fail("Spectrum nfft must be >= 0 (0 selects buffer frame count)");
  if (nfft == 0 && buf.frames > 16777216)
    fail("buffer frames exceed maximum FFT size 16777216; pass an explicit nfft in [2, 16777216]");
  int64_t N = (nfft > 0) ? nfft : buf.frames;
  if (N < 2 || N > 16777216) fail("Spectrum nfft must be in [2, 16777216]");
  int64_t copy_n = std::min(buf.frames, N);
  double cg = 1.0;
  std::vector<double> win = make_window(window, static_cast<std::size_t>(copy_n), cg);
  // Adjust coherent gain if zero-padded.
  cg *= double(copy_n) / double(N);

  membudget::require(std::size_t(N) * sizeof(std::complex<double>) + std::size_t(N) * sizeof(double) * 2);
  std::vector<std::complex<double>> a(static_cast<std::size_t>(N), {0.0, 0.0});
  const float *p = buf.chan_ptr(static_cast<int>(ch));
  for (int64_t i = 0; i < copy_n; i++) {
    a[std::size_t(i)] = std::complex<double>(double(p[i]) * win[std::size_t(i)], 0.0);
  }
  fft_complex(a, false);

  Spectrum spec;
  spec.sr = buf.sr;
  spec.nfft = static_cast<int>(N);
  spec.coherent_gain = cg;
  spec.window = window;
  spec.re.resize(static_cast<std::size_t>(N));
  spec.im.resize(static_cast<std::size_t>(N));
  for (int64_t i = 0; i < N; i++) {
    spec.re[std::size_t(i)] = a[std::size_t(i)].real();
    spec.im[std::size_t(i)] = a[std::size_t(i)].imag();
  }
  return spec;
}

Buffer compute_ifft(const Spectrum &spec) {
  if (spec.nfft < 2) fail("Spectrum.ifft: empty spectrum");
  std::size_t N = static_cast<std::size_t>(spec.nfft);
  membudget::require(N * sizeof(std::complex<double>));
  std::vector<std::complex<double>> a(N);
  for (std::size_t i = 0; i < N; i++) a[i] = {spec.re[i], spec.im[i]};
  fft_complex(a, true);
  Buffer out(spec.sr, 1, spec.nfft);
  float *p = out.chan_ptr(0);
  double inv_n = 1.0 / double(N);
  for (std::size_t i = 0; i < N; i++) {
    p[i] = static_cast<float>(a[i].real() * inv_n);
  }
  return out;
}

double compute_peak_freq(const Buffer &buf, const std::string &window, int64_t ch) {
  return compute_spectrum(buf, window, 0, ch).peak_freq();
}

double compute_centroid(const Buffer &buf, const std::string &window, int64_t ch) {
  return compute_spectrum(buf, window, 0, ch).centroid();
}

double compute_thd(const Buffer &buf, double fundamental_hz, int64_t harmonics, int64_t ch) {
  check_channel(buf, ch);
  check_finite(fundamental_hz, "fundamental_hz");
  if (harmonics < 2 || harmonics > 32) fail("thd: harmonics must be in [2, 32]");
  Spectrum spec = compute_spectrum(buf, "hann", 0, ch);
  double f0 = (fundamental_hz > 0.0) ? fundamental_hz : spec.peak_freq();
  if (f0 <= 0.0 || f0 >= double(buf.sr) * 0.5) fail("thd: invalid fundamental frequency");
  auto band_power = [&](double f_center) {
    int64_t k_center = static_cast<int64_t>(std::llround(f_center * double(spec.nfft) / double(spec.sr)));
    double pwr = 0.0;
    for (int64_t k = std::max<int64_t>(1, k_center - 3); k <= std::min<int64_t>(spec.bins() - 1, k_center + 3); k++) {
      double m = spec.mag_at(k);
      pwr += m * m;
    }
    return pwr;
  };
  double p1 = band_power(f0);
  if (p1 < 1e-18) fail("thd: fundamental power is too low (silent or unvoiced signal)");
  double ph = 0.0;
  for (int64_t h = 2; h <= harmonics; h++) {
    double fh = f0 * double(h);
    if (fh >= double(buf.sr) * 0.5) break;
    ph += band_power(fh);
  }
  return std::sqrt(ph / p1);
}

double compute_snr_db(const Buffer &buf, const Buffer &ref, int64_t ch) {
  check_channel(buf, ch);
  check_channel(ref, ch);
  if (buf.frames != ref.frames || buf.sr != ref.sr)
    fail("snr_db: buffer and reference must have identical sample rate and frame count");
  const float *x = buf.chan_ptr(static_cast<int>(ch));
  const float *s = ref.chan_ptr(static_cast<int>(ch));
  double sig_e = 0.0, err_e = 0.0;
  for (int64_t i = 0; i < buf.frames; i++) {
    double sv = s[i];
    double ev = double(x[i]) - sv;
    sig_e += sv * sv;
    err_e += ev * ev;
  }
  if (sig_e < 1e-24) fail("snr_db: reference signal has zero energy");
  if (err_e < 1e-24) return 240.0;
  return 10.0 * std::log10(sig_e / err_e);
}

Spectrogram compute_spectrogram(const Buffer &buf, int64_t win_size, int64_t hop_size, const std::string &window, double max_freq, int64_t ch) {
  check_channel(buf, ch);
  check_finite(max_freq, "max_freq");
  if (max_freq < 0.0) fail("spectrogram max_freq must be >= 0");
  if (win_size < 16 || win_size > 65536) fail("spectrogram win_size must be in [16, 65536]");
  if (hop_size < 1 || hop_size > win_size) fail("spectrogram hop_size must be in [1, win_size]");
  if (buf.frames < win_size) fail("buffer shorter than spectrogram win_size");

  double cg = 1.0;
  std::vector<double> win = make_window(window, static_cast<std::size_t>(win_size), cg);
  int64_t n_frames = (buf.frames - win_size) / hop_size + 1;
  int64_t full_bins = win_size / 2 + 1;
  double nyq = double(buf.sr) * 0.5;
  double f_limit = (max_freq > 0.0 && max_freq < nyq) ? max_freq : nyq;
  int64_t keep_bins = std::clamp<int64_t>(static_cast<int64_t>(std::floor(f_limit * double(win_size) / double(buf.sr))) + 1, 2, full_bins);

  membudget::require(std::size_t(n_frames) * std::size_t(keep_bins) * sizeof(float));

  Spectrogram sg;
  sg.sr = buf.sr;
  sg.win_size = static_cast<int>(win_size);
  sg.hop_size = static_cast<int>(hop_size);
  sg.n_frames = static_cast<int>(n_frames);
  sg.n_bins = static_cast<int>(keep_bins);
  sg.times.resize(static_cast<std::size_t>(n_frames));
  sg.freqs.resize(static_cast<std::size_t>(keep_bins));
  sg.mags.resize(static_cast<std::size_t>(n_frames) * static_cast<std::size_t>(keep_bins));

  for (int64_t b = 0; b < keep_bins; b++) {
    sg.freqs[std::size_t(b)] = double(b) * double(buf.sr) / double(win_size);
  }

  const float *p = buf.chan_ptr(static_cast<int>(ch));
  std::vector<std::complex<double>> a(static_cast<std::size_t>(win_size));
  double scale_inner = 2.0 / (double(win_size) * (cg > 0.0 ? cg : 1.0));
  double scale_edge = 1.0 / (double(win_size) * (cg > 0.0 ? cg : 1.0));

  for (int64_t f = 0; f < n_frames; f++) {
    int64_t start = f * hop_size;
    sg.times[std::size_t(f)] = (double(start) + 0.5 * double(win_size)) / double(buf.sr);
    for (int64_t i = 0; i < win_size; i++) {
      a[std::size_t(i)] = {double(p[start + i]) * win[std::size_t(i)], 0.0};
    }
    fft_complex(a, false);
    for (int64_t b = 0; b < keep_bins; b++) {
      double m = std::abs(a[std::size_t(b)]) * ((b == 0 || (win_size % 2 == 0 && b == win_size / 2)) ? scale_edge : scale_inner);
      sg.mags[std::size_t(f) * std::size_t(keep_bins) + std::size_t(b)] = static_cast<float>(m);
    }
  }
  return sg;
}

}  // namespace lmaudio
