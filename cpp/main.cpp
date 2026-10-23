// lmaudio: DaisySP + Soundpipe + MIT DSP/FFT/STFT/WAV engine behind an lmscript `_dsl_` schema.
// Scripting only: the executable has no user-facing commands besides --dsl and schema/version queries.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "audio.hpp"
#include "dslvm.hpp"
#include "memory_budget.hpp"
#include "notices_embedded.hpp"
#include "schema_embedded.hpp"

namespace {
using dslvm::Args;
using dslvm::Value;
using lmaudio::Biquad;
using lmaudio::Buffer;
using lmaudio::Spectrogram;
using lmaudio::Spectrum;

struct Host {
  bool dry_run = false;
  std::vector<std::unique_ptr<Buffer>> buffers;
  std::vector<std::unique_ptr<Spectrum>> spectra;
  std::vector<std::unique_ptr<Spectrogram>> spectrograms;
  std::vector<std::unique_ptr<Biquad>> biquads;

  template <class T> Value hold(std::vector<std::unique_ptr<T>> &arena, const char *type, T v) {
    if (arena.size() >= 4000000) throw dslvm::Error(std::string(type) + " object limit exceeded");
    arena.push_back(std::make_unique<T>(std::move(v)));
    return Value::object(type, arena.back().get());
  }
  Value buf(Buffer b) { return hold(buffers, "Buffer", std::move(b)); }
  Value spec(Spectrum s) { return hold(spectra, "Spectrum", std::move(s)); }
  Value sgram(Spectrogram sg) { return hold(spectrograms, "Spectrogram", std::move(sg)); }
  Value bq(Biquad b) { return hold(biquads, "Biquad", std::move(b)); }
};

template <class F> Value translate(F &&fn) {
  try {
    return fn();
  } catch (const dslvm::Error &) {
    throw;
  } catch (const std::exception &e) {
    throw dslvm::Error(e.what());
  }
}

void check_args(const char *who, const std::vector<std::string> &params, Args &a) {
  for (size_t i = 0; i < params.size(); i++) {
    const auto &want = params[i];
    const auto &v = a[i];
    bool ok = want == "float"    ? v.kind == Value::FLOAT
              : want == "int"    ? v.kind == Value::INT
              : want == "string" ? v.kind == Value::STR
                                 : (v.kind == Value::OBJ && v.type == want && v.obj);
    if (!ok) throw dslvm::Error(std::string(who) + " argument " + std::to_string(i + 1) + " must be " + want);
  }
}

void method(dslvm::Registry &r, const char *type, const char *name, std::vector<std::string> params,
            std::string returns, dslvm::Method cb) {
  std::string who = std::string(type) + "." + name;
  r.method(type, name, params, returns, [who, params, cb](void *self, Args &a) {
    check_args(who.c_str(), params, a);
    return translate([&] { return cb(self, a); });
  });
}

Buffer &B(void *p) { return *static_cast<Buffer *>(p); }
Buffer &arg_buf(Value &v) { return *static_cast<Buffer *>(v.obj); }
Spectrum &S(void *p) { return *static_cast<Spectrum *>(p); }
Spectrogram &SG(void *p) { return *static_cast<Spectrogram *>(p); }
Biquad &BQ(void *p) { return *static_cast<Biquad *>(p); }

std::string fmt(double x) {
  char b[48];
  std::snprintf(b, sizeof b, "%.10g", x);
  return b;
}

void register_host(dslvm::Registry &r, Host &h) {
  r.context("audio", "Audio");
  // --- Audio module ---
  method(r, "Audio", "silence", {"int", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_silence(a[0].i, a[1].f)); });
  method(r, "Audio", "silence", {"float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_silence(a[1].i, a[0].f)); });
  method(r, "Audio", "silence", {"float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_silence(44100, a[0].f)); });

  method(r, "Audio", "sine", {"float", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_sine(a[0].f, a[1].f, a[2].f, a[3].i)); });
  method(r, "Audio", "sine", {"float", "float", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_sine(a[0].f, a[1].f, a[2].f, 44100)); });
  method(r, "Audio", "sine", {"float", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_sine(a[0].f, a[1].f, 0.8, 44100)); });

  method(r, "Audio", "cosine", {"float", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_cosine(a[0].f, a[1].f, a[2].f, a[3].i)); });
  method(r, "Audio", "cosine", {"float", "float", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_cosine(a[0].f, a[1].f, a[2].f, 44100)); });

  method(r, "Audio", "saw", {"float", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_saw(a[0].f, a[1].f, a[2].f, a[3].i)); });
  method(r, "Audio", "saw", {"float", "float", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_saw(a[0].f, a[1].f, a[2].f, 44100)); });

  method(r, "Audio", "square", {"float", "float", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_square(a[0].f, a[1].f, a[2].f, a[3].f, a[4].i)); });
  method(r, "Audio", "square", {"float", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_square(a[0].f, a[1].f, a[2].f, 0.5, a[3].i)); });
  method(r, "Audio", "square", {"float", "float", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_square(a[0].f, a[1].f, a[2].f, 0.5, 44100)); });

  method(r, "Audio", "triangle", {"float", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_triangle(a[0].f, a[1].f, a[2].f, a[3].i)); });
  method(r, "Audio", "triangle", {"float", "float", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_triangle(a[0].f, a[1].f, a[2].f, 44100)); });

  method(r, "Audio", "chirp", {"float", "float", "float", "float", "string", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_chirp(a[0].f, a[1].f, a[2].f, a[3].f, a[4].s, a[5].i)); });
  method(r, "Audio", "chirp", {"float", "float", "float", "float", "string"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_chirp(a[0].f, a[1].f, a[2].f, a[3].f, a[4].s, 44100)); });

  method(r, "Audio", "impulse", {"float", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_impulse(a[0].f, a[1].f, a[2].f, a[3].i)); });
  method(r, "Audio", "impulse", {"float", "float", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_impulse(a[0].f, a[1].f, a[2].f, 44100)); });

  method(r, "Audio", "noise", {"string", "float", "float", "int", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_noise(a[0].s, a[1].f, a[2].f, a[3].i, a[4].i)); });
  method(r, "Audio", "noise", {"string", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_noise(a[0].s, a[1].f, a[2].f, a[3].i, 44100)); });

  method(r, "Audio", "expr", {"string", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_expr(a[0].s, a[1].f, a[2].i)); });
  method(r, "Audio", "expr", {"string", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_expr(a[0].s, a[1].f, 44100)); });

  method(r, "Audio", "fm", {"float", "float", "float", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_fm(a[0].f, a[1].f, a[2].f, a[3].f, a[4].f, a[5].i)); });
  method(r, "Audio", "fm", {"float", "float", "float", "float", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_fm(a[0].f, a[1].f, a[2].f, a[3].f, a[4].f, 44100)); });

  method(r, "Audio", "pluck", {"float", "float", "float", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_pluck(a[0].f, a[1].f, a[2].f, a[3].f, a[4].f, a[5].i)); });
  method(r, "Audio", "pluck", {"float", "float", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_pluck(a[0].f, a[1].f, a[2].f, 0.7, 0.85, 44100)); });

  method(r, "Audio", "modal", {"float", "float", "float", "float", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_modal(a[0].f, a[1].f, a[2].f, a[3].f, a[4].f, a[5].f, a[6].i)); });
  method(r, "Audio", "modal", {"float", "float", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_modal(a[0].f, a[1].f, a[2].f, 0.5, 0.65, 0.75, 44100)); });

  method(r, "Audio", "bar", {"int", "int", "float", "float", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_bar(a[0].i, a[1].i, a[2].f, a[3].f, a[4].f, a[5].f, a[6].i)); });

  method(r, "Audio", "drum", {"string", "float", "float", "float", "float", "float", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_drum(a[0].s, a[1].f, a[2].f, a[3].f, a[4].f, a[5].f, a[6].i)); });
  method(r, "Audio", "drum", {"string", "float", "float", "float"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_drum(a[0].s, a[1].f, a[2].f, a[3].f, 0.5, 0.8, 44100)); });

  method(r, "Audio", "sequence", {"string", "float", "string", "int"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_sequence(a[0].s, a[1].f, a[2].s, a[3].i)); });
  method(r, "Audio", "sequence", {"string", "float", "string"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_sequence(a[0].s, a[1].f, a[2].s, 44100)); });

  method(r, "Audio", "note_freq", {"string"}, "float",
         [&](void *, Args &a) { return Value::real(lmaudio::note_to_freq(a[0].s)); });
  method(r, "Audio", "midi_freq", {"float"}, "float",
         [&](void *, Args &a) { return Value::real(lmaudio::midi_to_freq(a[0].f)); });

  method(r, "Audio", "load_wav", {"string"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::load_wav(a[0].s)); });
  method(r, "Audio", "stereo", {"Buffer", "Buffer"}, "Buffer",
         [&](void *, Args &a) { return h.buf(lmaudio::make_stereo(arg_buf(a[0]), arg_buf(a[1]))); });

  method(r, "Audio", "biquad_design", {"string", "float", "float", "float", "int"}, "Biquad",
         [&](void *, Args &a) { return h.bq(lmaudio::design_biquad(a[0].s, a[1].f, a[2].f, a[3].f, a[4].i)); });
  method(r, "Audio", "biquad_design", {"string", "float", "float", "int"}, "Biquad",
         [&](void *, Args &a) { return h.bq(lmaudio::design_biquad(a[0].s, a[1].f, a[2].f, 0.0, a[3].i)); });

  method(r, "Audio", "assert_close", {"float", "float", "float"}, "void", [&](void *, Args &a) {
    if (!std::isfinite(a[2].f) || a[2].f < 0.0) throw dslvm::Error("Audio.assert_close: tolerance must be >= 0");
    double d = std::abs(a[0].f - a[1].f);
    if (!std::isfinite(a[0].f) || !std::isfinite(a[1].f) || d > a[2].f)
      throw dslvm::Error("assert_close failed: got " + fmt(a[0].f) + ", expected " + fmt(a[1].f) +
                         " (|diff| = " + fmt(d) + " > " + fmt(a[2].f) + ")");
    return Value::none();
  });

  // --- Buffer methods ---
  method(r, "Buffer", "sr", {}, "int", [&](void *s, Args &) { return Value::integer(B(s).sr); });
  method(r, "Buffer", "channels", {}, "int", [&](void *s, Args &) { return Value::integer(B(s).channels); });
  method(r, "Buffer", "frames", {}, "int", [&](void *s, Args &) { return Value::integer(B(s).frames); });
  method(r, "Buffer", "duration", {}, "float", [&](void *s, Args &) { return Value::real(B(s).duration()); });

  method(r, "Buffer", "get", {"int"}, "float", [&](void *s, Args &a) {
    const auto &b = B(s);
    if (a[0].i < 0 || a[0].i >= b.frames)
      throw dslvm::Error("Buffer.get: frame " + std::to_string(a[0].i) + " outside [0, " + std::to_string(b.frames - 1) + "]");
    return Value::real(b.at(a[0].i, 0));
  });
  method(r, "Buffer", "get", {"int", "int"}, "float", [&](void *s, Args &a) {
    const auto &b = B(s);
    if (a[1].i < 0 || a[1].i >= b.channels)
      throw dslvm::Error("Buffer.get: channel " + std::to_string(a[1].i) + " outside [0, " + std::to_string(b.channels - 1) + "]");
    if (a[0].i < 0 || a[0].i >= b.frames)
      throw dslvm::Error("Buffer.get: frame " + std::to_string(a[0].i) + " outside [0, " + std::to_string(b.frames - 1) + "]");
    return Value::real(b.at(a[0].i, static_cast<int>(a[1].i)));
  });

  method(r, "Buffer", "channel", {"int"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::extract_channel(B(s), a[0].i)); });
  method(r, "Buffer", "to_mono", {}, "Buffer",
         [&](void *s, Args &) { return h.buf(lmaudio::to_mono(B(s))); });

  method(r, "Buffer", "gain", {"float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_gain(B(s), a[0].f)); });
  method(r, "Buffer", "gain_db", {"float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_gain_db(B(s), a[0].f)); });
  method(r, "Buffer", "normalize", {"float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_normalize(B(s), a[0].f)); });
  method(r, "Buffer", "normalize", {}, "Buffer",
         [&](void *s, Args &) { return h.buf(lmaudio::apply_normalize(B(s), 0.99)); });
  method(r, "Buffer", "normalize_rms", {"float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_normalize_rms(B(s), a[0].f)); });

  method(r, "Buffer", "fade", {"float", "float", "string"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_fade(B(s), a[0].f, a[1].f, a[2].s)); });
  method(r, "Buffer", "fade", {"float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_fade(B(s), a[0].f, a[1].f, "cosine")); });

  method(r, "Buffer", "adsr", {"float", "float", "float", "float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_adsr(B(s), a[0].f, a[1].f, a[2].f, a[3].f, a[4].f)); });
  method(r, "Buffer", "adsr", {"float", "float", "float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_adsr(B(s), a[0].f, a[1].f, a[2].f, a[3].f, 0.0)); });

  method(r, "Buffer", "pan", {"float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_pan(B(s), a[0].f)); });
  method(r, "Buffer", "slice", {"float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_slice(B(s), a[0].f, a[1].f)); });
  method(r, "Buffer", "pad", {"float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_pad(B(s), a[0].f, a[1].f)); });
  method(r, "Buffer", "concat", {"Buffer"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_concat(B(s), arg_buf(a[0]))); });
  method(r, "Buffer", "mix", {"Buffer", "float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_mix(B(s), arg_buf(a[0]), a[1].f, a[2].f)); });
  method(r, "Buffer", "mix", {"Buffer", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_mix(B(s), arg_buf(a[0]), a[1].f, 0.0)); });
  method(r, "Buffer", "mix", {"Buffer"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_mix(B(s), arg_buf(a[0]), 1.0, 0.0)); });
  method(r, "Buffer", "reverse", {}, "Buffer",
         [&](void *s, Args &) { return h.buf(lmaudio::apply_reverse(B(s))); });
  method(r, "Buffer", "resample", {"int"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_resample(B(s), a[0].i)); });

  // Filters & effects
  method(r, "Buffer", "biquad", {"string", "float", "float", "float"}, "Buffer", [&](void *s, Args &a) {
    auto bq = lmaudio::design_biquad(a[0].s, a[1].f, a[2].f, a[3].f, B(s).sr);
    return h.buf(lmaudio::apply_biquad(B(s), bq));
  });
  method(r, "Buffer", "biquad", {"string", "float", "float"}, "Buffer", [&](void *s, Args &a) {
    auto bq = lmaudio::design_biquad(a[0].s, a[1].f, a[2].f, 0.0, B(s).sr);
    return h.buf(lmaudio::apply_biquad(B(s), bq));
  });
  method(r, "Buffer", "svf", {"string", "float", "float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_svf(B(s), a[0].s, a[1].f, a[2].f, a[3].f)); });
  method(r, "Buffer", "svf", {"string", "float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_svf(B(s), a[0].s, a[1].f, a[2].f, 0.0)); });
  method(r, "Buffer", "ladder", {"float", "float", "float", "string"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_ladder(B(s), a[0].f, a[1].f, a[2].f, a[3].s)); });
  method(r, "Buffer", "ladder", {"float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_ladder(B(s), a[0].f, a[1].f, 1.0, "lp24")); });
  method(r, "Buffer", "lpf_sweep", {"float", "float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_lpf_sweep(B(s), a[0].f, a[1].f, a[2].f)); });

  method(r, "Buffer", "delay", {"float", "float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_delay(B(s), a[0].f, a[1].f, a[2].f)); });
  method(r, "Buffer", "reverb", {"float", "float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_reverb(B(s), a[0].f, a[1].f, a[2].f)); });
  method(r, "Buffer", "zitarev", {"float", "float", "float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_zitarev(B(s), a[0].f, a[1].f, a[2].f, a[3].f)); });
  method(r, "Buffer", "chorus", {"float", "float", "float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_chorus(B(s), a[0].f, a[1].f, a[2].f, a[3].f)); });
  method(r, "Buffer", "flanger", {"float", "float", "float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_flanger(B(s), a[0].f, a[1].f, a[2].f, a[3].f)); });
  method(r, "Buffer", "phaser", {"float", "float", "float", "int", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_phaser(B(s), a[0].f, a[1].f, a[2].f, a[3].i, a[4].f)); });
  method(r, "Buffer", "overdrive", {"float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_overdrive(B(s), a[0].f, a[1].f)); });
  method(r, "Buffer", "wavefold", {"float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_wavefold(B(s), a[0].f, a[1].f)); });
  method(r, "Buffer", "bitcrush", {"int", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_bitcrush(B(s), a[0].i, a[1].f)); });
  method(r, "Buffer", "compress", {"float", "float", "float", "float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_compress(B(s), a[0].f, a[1].f, a[2].f, a[3].f, a[4].f)); });
  method(r, "Buffer", "paulstretch", {"float", "float"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_paulstretch(B(s), a[0].f, a[1].f)); });

  // Analysis
  method(r, "Buffer", "rms", {}, "float",
         [&](void *s, Args &) { return Value::real(lmaudio::compute_rms(B(s), 0)); });
  method(r, "Buffer", "rms", {"int"}, "float",
         [&](void *s, Args &a) { return Value::real(lmaudio::compute_rms(B(s), a[0].i)); });
  method(r, "Buffer", "peak", {}, "float",
         [&](void *s, Args &) { return Value::real(lmaudio::compute_peak(B(s), 0)); });
  method(r, "Buffer", "peak", {"int"}, "float",
         [&](void *s, Args &a) { return Value::real(lmaudio::compute_peak(B(s), a[0].i)); });
  method(r, "Buffer", "dc_offset", {}, "float",
         [&](void *s, Args &) { return Value::real(lmaudio::compute_dc_offset(B(s), 0)); });
  method(r, "Buffer", "dc_offset", {"int"}, "float",
         [&](void *s, Args &a) { return Value::real(lmaudio::compute_dc_offset(B(s), a[0].i)); });
  method(r, "Buffer", "energy", {}, "float",
         [&](void *s, Args &) { return Value::real(lmaudio::compute_energy(B(s), 0)); });
  method(r, "Buffer", "energy", {"int"}, "float",
         [&](void *s, Args &a) { return Value::real(lmaudio::compute_energy(B(s), a[0].i)); });
  method(r, "Buffer", "zcr", {}, "float",
         [&](void *s, Args &) { return Value::real(lmaudio::compute_zcr(B(s), 0)); });
  method(r, "Buffer", "zcr", {"int"}, "float",
         [&](void *s, Args &a) { return Value::real(lmaudio::compute_zcr(B(s), a[0].i)); });

  method(r, "Buffer", "pitch", {}, "float",
         [&](void *s, Args &) { return Value::real(lmaudio::compute_pitch(B(s), 40.0, 4000.0, 0)); });
  method(r, "Buffer", "pitch", {"float", "float"}, "float",
         [&](void *s, Args &a) { return Value::real(lmaudio::compute_pitch(B(s), a[0].f, a[1].f, 0)); });
  method(r, "Buffer", "peak_freq", {}, "float",
         [&](void *s, Args &) { return Value::real(lmaudio::compute_peak_freq(B(s), "hann", 0)); });
  method(r, "Buffer", "peak_freq", {"string"}, "float",
         [&](void *s, Args &a) { return Value::real(lmaudio::compute_peak_freq(B(s), a[0].s, 0)); });
  method(r, "Buffer", "centroid", {}, "float",
         [&](void *s, Args &) { return Value::real(lmaudio::compute_centroid(B(s), "hann", 0)); });
  method(r, "Buffer", "centroid", {"string"}, "float",
         [&](void *s, Args &a) { return Value::real(lmaudio::compute_centroid(B(s), a[0].s, 0)); });
  method(r, "Buffer", "thd", {}, "float",
         [&](void *s, Args &) { return Value::real(lmaudio::compute_thd(B(s), 0.0, 5, 0)); });
  method(r, "Buffer", "thd", {"float", "int"}, "float",
         [&](void *s, Args &a) { return Value::real(lmaudio::compute_thd(B(s), a[0].f, a[1].i, 0)); });
  method(r, "Buffer", "snr_db", {"Buffer"}, "float",
         [&](void *s, Args &a) { return Value::real(lmaudio::compute_snr_db(B(s), arg_buf(a[0]), 0)); });

  method(r, "Buffer", "spectrum", {}, "Spectrum",
         [&](void *s, Args &) { return h.spec(lmaudio::compute_spectrum(B(s), "hann", 0, 0)); });
  method(r, "Buffer", "spectrum", {"string"}, "Spectrum",
         [&](void *s, Args &a) { return h.spec(lmaudio::compute_spectrum(B(s), a[0].s, 0, 0)); });
  method(r, "Buffer", "spectrum", {"string", "int"}, "Spectrum",
         [&](void *s, Args &a) { return h.spec(lmaudio::compute_spectrum(B(s), a[0].s, a[1].i, 0)); });

  method(r, "Buffer", "spectrogram", {"int", "int", "string", "float"}, "Spectrogram",
         [&](void *s, Args &a) { return h.sgram(lmaudio::compute_spectrogram(B(s), a[0].i, a[1].i, a[2].s, a[3].f, 0)); });
  method(r, "Buffer", "spectrogram", {"int", "int"}, "Spectrogram",
         [&](void *s, Args &a) { return h.sgram(lmaudio::compute_spectrogram(B(s), a[0].i, a[1].i, "hann", 0.0, 0)); });

  // I/O
  method(r, "Buffer", "save_wav", {"string"}, "Buffer", [&](void *s, Args &a) {
    lmaudio::save_wav(B(s), a[0].s, 16, h.dry_run);
    return Value::object("Buffer", s);
  });
  method(r, "Buffer", "save_wav", {"string", "int"}, "Buffer", [&](void *s, Args &a) {
    lmaudio::save_wav(B(s), a[0].s, a[1].i, h.dry_run);
    return Value::object("Buffer", s);
  });
  method(r, "Buffer", "save_csv", {"string"}, "Buffer", [&](void *s, Args &a) {
    lmaudio::save_buffer_csv(B(s), a[0].s, 0, 1, h.dry_run);
    return Value::object("Buffer", s);
  });
  method(r, "Buffer", "save_csv", {"string", "int"}, "Buffer", [&](void *s, Args &a) {
    lmaudio::save_buffer_csv(B(s), a[0].s, a[1].i, 1, h.dry_run);
    return Value::object("Buffer", s);
  });
  method(r, "Buffer", "save_csv", {"string", "int", "int"}, "Buffer", [&](void *s, Args &a) {
    lmaudio::save_buffer_csv(B(s), a[0].s, a[1].i, a[2].i, h.dry_run);
    return Value::object("Buffer", s);
  });

  // --- Spectrum methods ---
  method(r, "Spectrum", "bins", {}, "int", [&](void *s, Args &) { return Value::integer(S(s).bins()); });
  method(r, "Spectrum", "nfft", {}, "int", [&](void *s, Args &) { return Value::integer(S(s).nfft); });
  method(r, "Spectrum", "sr", {}, "int", [&](void *s, Args &) { return Value::integer(S(s).sr); });
  method(r, "Spectrum", "freq_at", {"int"}, "float",
         [&](void *s, Args &a) { return Value::real(S(s).freq_at(a[0].i)); });
  method(r, "Spectrum", "mag_at", {"int"}, "float",
         [&](void *s, Args &a) { return Value::real(S(s).mag_at(a[0].i)); });
  method(r, "Spectrum", "db_at", {"int"}, "float",
         [&](void *s, Args &a) { return Value::real(S(s).db_at(a[0].i)); });
  method(r, "Spectrum", "phase_at", {"int"}, "float",
         [&](void *s, Args &a) { return Value::real(S(s).phase_at(a[0].i)); });
  method(r, "Spectrum", "mag_at_freq", {"float"}, "float",
         [&](void *s, Args &a) { return Value::real(S(s).mag_at_freq(a[0].f)); });
  method(r, "Spectrum", "peak_bin", {}, "int",
         [&](void *s, Args &) { return Value::integer(S(s).peak_bin()); });
  method(r, "Spectrum", "peak_freq", {}, "float",
         [&](void *s, Args &) { return Value::real(S(s).peak_freq()); });
  method(r, "Spectrum", "peak_mag", {}, "float",
         [&](void *s, Args &) { return Value::real(S(s).peak_mag()); });
  method(r, "Spectrum", "centroid", {}, "float",
         [&](void *s, Args &) { return Value::real(S(s).centroid()); });
  method(r, "Spectrum", "parseval_energy", {}, "float",
         [&](void *s, Args &) { return Value::real(S(s).parseval_energy()); });
  method(r, "Spectrum", "ifft", {}, "Buffer",
         [&](void *s, Args &) { return h.buf(lmaudio::compute_ifft(S(s))); });
  method(r, "Spectrum", "save_csv", {"string"}, "Spectrum", [&](void *s, Args &a) {
    S(s).save_csv(a[0].s, 0.0, h.dry_run);
    return Value::object("Spectrum", s);
  });
  method(r, "Spectrum", "save_csv", {"string", "float"}, "Spectrum", [&](void *s, Args &a) {
    S(s).save_csv(a[0].s, a[1].f, h.dry_run);
    return Value::object("Spectrum", s);
  });

  // --- Spectrogram methods ---
  method(r, "Spectrogram", "frames", {}, "int", [&](void *s, Args &) { return Value::integer(SG(s).n_frames); });
  method(r, "Spectrogram", "bins", {}, "int", [&](void *s, Args &) { return Value::integer(SG(s).n_bins); });
  method(r, "Spectrogram", "sr", {}, "int", [&](void *s, Args &) { return Value::integer(SG(s).sr); });
  method(r, "Spectrogram", "time_at", {"int"}, "float",
         [&](void *s, Args &a) { return Value::real(SG(s).time_at(a[0].i)); });
  method(r, "Spectrogram", "freq_at", {"int"}, "float",
         [&](void *s, Args &a) { return Value::real(SG(s).freq_at(a[0].i)); });
  method(r, "Spectrogram", "mag_at", {"int", "int"}, "float",
         [&](void *s, Args &a) { return Value::real(SG(s).mag_at(a[0].i, a[1].i)); });
  method(r, "Spectrogram", "db_at", {"int", "int"}, "float",
         [&](void *s, Args &a) { return Value::real(SG(s).db_at(a[0].i, a[1].i)); });
  method(r, "Spectrogram", "ridge_freq", {"int"}, "float",
         [&](void *s, Args &a) { return Value::real(SG(s).ridge_freq(a[0].i)); });
  method(r, "Spectrogram", "save_csv", {"string"}, "Spectrogram", [&](void *s, Args &a) {
    SG(s).save_csv(a[0].s, -90.0, h.dry_run);
    return Value::object("Spectrogram", s);
  });
  method(r, "Spectrogram", "save_csv", {"string", "float"}, "Spectrogram", [&](void *s, Args &a) {
    SG(s).save_csv(a[0].s, a[1].f, h.dry_run);
    return Value::object("Spectrogram", s);
  });

  // --- Biquad methods ---
  method(r, "Biquad", "b0", {}, "float", [&](void *s, Args &) { return Value::real(BQ(s).b0); });
  method(r, "Biquad", "b1", {}, "float", [&](void *s, Args &) { return Value::real(BQ(s).b1); });
  method(r, "Biquad", "b2", {}, "float", [&](void *s, Args &) { return Value::real(BQ(s).b2); });
  method(r, "Biquad", "a1", {}, "float", [&](void *s, Args &) { return Value::real(BQ(s).a1); });
  method(r, "Biquad", "a2", {}, "float", [&](void *s, Args &) { return Value::real(BQ(s).a2); });
  method(r, "Biquad", "gain_at", {"float"}, "float",
         [&](void *s, Args &a) { return Value::real(BQ(s).gain_at(a[0].f)); });
  method(r, "Biquad", "db_at", {"float"}, "float",
         [&](void *s, Args &a) { return Value::real(BQ(s).db_at(a[0].f)); });
  method(r, "Biquad", "phase_deg_at", {"float"}, "float",
         [&](void *s, Args &a) { return Value::real(BQ(s).phase_deg_at(a[0].f)); });
  method(r, "Biquad", "apply", {"Buffer"}, "Buffer",
         [&](void *s, Args &a) { return h.buf(lmaudio::apply_biquad(arg_buf(a[0]), BQ(s))); });
  method(r, "Biquad", "save_response_csv", {"string"}, "Biquad", [&](void *s, Args &a) {
    BQ(s).save_response_csv(a[0].s, 512, 20.0, double(BQ(s).sr) * 0.5, h.dry_run);
    return Value::object("Biquad", s);
  });
  method(r, "Biquad", "save_response_csv", {"string", "int", "float", "float"}, "Biquad", [&](void *s, Args &a) {
    BQ(s).save_response_csv(a[0].s, a[1].i, a[2].f, a[3].f, h.dry_run);
    return Value::object("Biquad", s);
  });
}

const char *usage =
    "usage: lmaudio --version | --notices | --dsl-schema [--json] | --dsl-signatures | [--dry-run] --dsl FILE [FILE ...]\n"
    "environment: LMAUDIO_MAX_MEMORY_MB  total memory budget in MiB (default 4096); a program that needs more\n"
    "             stops with a run error (exit 3) instead of allocating\n";

void diagnostic(const std::string &text) {
  if (text.find("memory budget exceeded:") != std::string::npos) return;
  std::cerr << "lmaudio: ";
  for (char c : text) std::cerr << ((c == '\r' || c == '\n') ? ' ' : c);
  std::cerr << '\n';
}

int report_and_log(const char *phase, const dslvm::Error &e) {
  diagnostic(e.what());
  return dslvm::report_error(phase, e);
}
}  // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  if (const char *env = std::getenv("LMAUDIO_MAX_MEMORY_MB")) {
    bool digits_only = (*env != '\0');
    for (const char *p = env; *p; ++p) {
      if (*p < '0' || *p > '9') digits_only = false;
    }
    if (!digits_only) {
      std::cerr << "lmaudio: LMAUDIO_MAX_MEMORY_MB must be a positive whole number of MiB, got '" << env << "'\n";
      std::cerr << usage;
      return 2;
    }
  }
  if (!membudget::configure("lmaudio", "LMAUDIO_MAX_MEMORY_MB")) {
    std::cerr << usage;
    return 2;
  }
  if (argc == 2 && std::string(argv[1]) == "--version") {
    std::cout << "lmaudio " << LMAUDIO_VERSION << "\n";
    return 0;
  }
  if (argc == 2 && std::string(argv[1]) == "--notices") {
    std::cout.write(reinterpret_cast<const char *>(lmaudio::embedded_notices),
                    static_cast<std::streamsize>(lmaudio::embedded_notices_size));
    return 0;
  }
  if ((argc == 2 || (argc == 3 && std::string(argv[2]) == "--json")) && std::string(argv[1]) == "--dsl-schema") {
    std::cout.write(reinterpret_cast<const char *>(lmaudio::embedded_schema),
                    static_cast<std::streamsize>(lmaudio::embedded_schema_size));
    return 0;
  }
  Host host;
  dslvm::Registry reg;
  register_host(reg, host);
  if (argc == 2 && std::string(argv[1]) == "--dsl-signatures") {
    for (const auto &s : reg.signatures()) std::cout << s << "\n";
    return 0;
  }
  int first = 2;
  if (argc >= 3 && std::string(argv[1]) == "--dry-run") {
    host.dry_run = true;
    first = 3;
  }
  if (argc <= first || std::string(argv[first - 1]) != "--dsl") {
    std::cerr << usage;
    return 2;
  }
  for (int i = first; i < argc; i++) {
    if (std::string(argv[i]).empty() || std::string(argv[i]).rfind("--", 0) == 0) {
      std::cerr << usage;
      return 2;
    }
  }
  const std::string schema(reinterpret_cast<const char *>(lmaudio::embedded_schema), lmaudio::embedded_schema_size);
  dslvm::Context context;
  context.set("audio", &host);
  std::vector<dslvm::Program> programs;
  for (int i = first; i < argc; i++) {
    try {
      programs.push_back(dslvm::Program::load(dslvm::read_file(argv[i]), schema, reg));
    } catch (const dslvm::Error &e) {
      return report_and_log("load", e);
    } catch (const std::exception &e) {
      return report_and_log("load", dslvm::Error(e.what()));
    }
  }
  for (const auto &p : programs) {
    try {
      p.run(context, reg);
    } catch (const dslvm::Error &e) {
      return report_and_log("run", e);
    } catch (const std::exception &e) {
      return report_and_log("run", dslvm::Error(e.what()));
    }
    dslvm::report_ok();
  }
  return 0;
}
