#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"
BUILD_DIR="${BUILD_DIR:-build}"
HOST="$BUILD_DIR/lmaudio"
UNAME_S="$(uname -s)"
case "$UNAME_S" in MINGW*|MSYS*|CYGWIN*) HOST="$HOST.exe";; esac
WORK="$(mktemp -d "${TMPDIR:-/tmp}/lmaudio-test-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
ROOT="$HERE"
case "$UNAME_S" in MINGW*|MSYS*|CYGWIN*) WORK="$(cd "$WORK" && pwd -W | tr '\\' '/')"; ROOT="$(pwd -W | tr '\\' '/')";; esac
pass=0
ok(){ pass=$((pass+1)); echo "ok $pass - $1"; }
fail(){ echo "FAIL: $*" >&2; exit 1; }
sha(){ if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -c1-64; else shasum -a 256 "$1" | cut -c1-64; fi; }

# lmscript resolves cli="lmaudio" on PATH first, so programs bind to the binary under test.
export PATH="$(cd "$BUILD_DIR" 2>/dev/null && pwd || echo "$HERE/$BUILD_DIR"):$PATH"
if [ -d "$HERE/../lmplot/build" ]; then
  export PATH="$(cd "$HERE/../lmplot/build" && pwd):$PATH"
fi
LMSCRIPT="$(scripts/select-compiler.sh)"
scripts/sync-sdk.sh --verify >/dev/null || fail "SDK header pin mismatch"
ok "SDK header matches sdk.lock"
scripts/sync-deps.sh --verify >/dev/null || fail "third_party pin mismatch"
ok "third_party matches deps.lock"

GEN=""
command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
# shellcheck disable=SC2086
cmake -S . -B "$BUILD_DIR" $GEN -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" >"$WORK/build.log" 2>&1 || { tail -40 "$WORK/build.log" >&2; fail "build"; }
ok "build"

want_version="lmaudio $(sed -n 's/^project(lmaudio VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ "$("$HOST" --version)" = "$want_version" ] || fail "--version is not exactly '$want_version'"
ok "--version prints exactly '$want_version'"
cp schema/lmaudio.dsl.json "$WORK/schema.before"
python3 scripts/gen-schema.py >/dev/null && cmp -s "$WORK/schema.before" schema/lmaudio.dsl.json || fail "schema/lmaudio.dsl.json is stale: run scripts/gen-schema.py"
ok "schema matches the registrations in cpp/main.cpp"
"$HOST" --dsl-schema --json >"$WORK/schema.json" || fail "embedded schema print"
cmp -s "$WORK/schema.json" schema/lmaudio.dsl.json || fail "embedded schema differs from schema/lmaudio.dsl.json"
ok "--dsl-schema --json prints byte-identical embedded schema bytes"
diff -u <("$LMSCRIPT" --dsl-signatures schema/lmaudio.dsl.json) <("$HOST" --dsl-signatures) || fail "host/compiler signatures differ"
ok "host registry matches compiler signatures"
"$HOST" --notices >"$WORK/notices.txt" || fail "embedded notices print"
cmp -s "$WORK/notices.txt" THIRD_PARTY_NOTICES || fail "--notices differs from THIRD_PARTY_NOTICES"
grep -q "DaisySP" THIRD_PARTY_NOTICES && grep -q "Soundpipe" THIRD_PARTY_NOTICES && grep -q "Matsumoto" THIRD_PARTY_NOTICES && grep -q "GNU LESSER GENERAL PUBLIC LICENSE" THIRD_PARTY_NOTICES && grep -q "GCC RUNTIME LIBRARY EXCEPTION" THIRD_PARTY_NOTICES || fail "THIRD_PARTY_NOTICES missing required attributions"
ok "--notices prints byte-identical THIRD_PARTY_NOTICES with full attribution and runtime licenses"

for f in index.md 01-purpose.md 02-architecture.md 03-api.md 04-Visuals.md 05-testing.md 06-build-and-ci.md 07-decisions.md 08-limitations.md; do
  [ -s "durable-memory/$f" ] || fail "missing or empty durable-memory/$f"
done
ok "durable-memory/ contains all 9 architecture files"

case "$UNAME_S" in
  Linux) if ldd "$HOST" >/dev/null 2>&1; then fail "executable is dynamically linked"; fi; ok "executable is fully static (no dynamic loader)";;
  Darwin) bad="$(otool -L "$HOST" | tail -n +2 | grep -v -E '/usr/lib/(libSystem|libc\+\+|libresolv)' || true)"
          [ -z "$bad" ] || fail "non-system dylib dependencies: $bad"; ok "only system libraries linked";;
  MINGW*|MSYS*|CYGWIN*)
    if command -v objdump >/dev/null 2>&1; then
      bad="$(objdump -p "$HOST" | grep 'DLL Name' | grep -i -E 'libstdc|libgcc|winpthread|libgomp' || true)"
      [ -z "$bad" ] || fail "MinGW runtime DLLs are imported: $bad"
      ok "no MinGW runtime DLL imports"
    else ok "static-link check skipped (no objdump)"; fi;;
  *) ok "static-link check skipped on $UNAME_S";;
esac

# Compile and run every verification test suite in tests/*.lms.
mkdir -p build
nt=0
for src in tests/*.lms; do
  name="$(basename "$src" .lms)"
  "$LMSCRIPT" "$src" "build/$name.dslb" >"$WORK/$name.log" 2>&1 || { cat "$WORK/$name.log" >&2; fail "$name compile"; }
  "$HOST" --dsl "build/$name.dslb" >"$WORK/$name.out" 2>"$WORK/$name.err" || { cat "$WORK/$name.out" "$WORK/$name.err" >&2; fail "$name run"; }
  grep -q '"ok": true' "$WORK/$name.out" || { cat "$WORK/$name.out" >&2; fail "$name did not report ok"; }
  nt=$((nt+1))
done
ok "$nt verification test suites compile and pass all numerical/DSP assertions"

# Error paths: each snippet must fail the run with exit 3 and a message naming the problem on BOTH stdout (JSON) and stderr.
nerr=0
expect_error() { # name, snippet, expected message fragment
  nerr=$((nerr+1))
  cat >"$WORK/$1.lms" <<LMS
p = _dsl_ cli="lmaudio";
$2
end _dsl_
savedsl(p, "$WORK/$1.dslb");
LMS
  "$LMSCRIPT" "$WORK/$1.lms" >/dev/null 2>"$WORK/$1.cerr" || { cat "$WORK/$1.cerr" >&2; fail "$1 compile"; }
  set +e; out="$("$HOST" --dsl "$WORK/$1.dslb" 2>"$WORK/$1.rerr")"; code=$?; set -e
  [ "$code" = 3 ] || fail "$1: exit code $code, expected 3 ($out)"
  echo "$out" | grep -q "$3" || fail "$1: stdout JSON lacks '$3': $out"
  grep -q "$3" "$WORK/$1.rerr" || fail "$1: stderr lacks '$3': $(cat "$WORK/$1.rerr")"
}
expect_error badsr 'let b = audio.sine(440.0, 1.0, 0.8, 10);' "sample rate"
expect_error baddur 'let b = audio.sine(440.0, -1.0, 0.8, 44100);' "duration"
expect_error badfreq 'let b = audio.sine(30000.0, 1.0, 0.8, 44100);' "Nyquist"
expect_error badduty 'let b = audio.square(440.0, 1.0, 0.8, 1.5, 44100);' "duty"
expect_error badnoise 'let b = audio.noise("purple", 1.0, 0.5, 1, 44100);' "unknown noise"
expect_error badexpr 'let b = audio.expr("2 +* t", 0.1, 44100);' "expression"
expect_error nanexpr 'let b = audio.expr("1 / 0", 0.1, 44100);' "non-finite"
expect_error badnote 'let f = audio.note_freq("H4");' "invalid note"
expect_error badmidi 'let f = audio.midi_freq(200.0);' "MIDI"
expect_error badbiquad 'let bq = audio.biquad_design("lowpass", 25000.0, 0.707, 44100);' "cutoff"
expect_error badq 'let bq = audio.biquad_design("lowpass", 1000.0, 0.0, 44100);' "biquad Q"
expect_error badkind 'let bq = audio.biquad_design("warp", 1000.0, 0.707, 44100);' "unknown biquad"
expect_error badbqdb 'let bq = audio.biquad_design("peak", 1000.0, 1.0, 100000.0, 44100);' "db_gain"
expect_error badgaindb 'let b = audio.sine(440.0, 0.1).gain_db(500.0);' "gain_db"
expect_error badmixoff 'let a = audio.sine(440.0, 0.1); let c = a.mix(a, 1.0, 999999999999999.0);' "offset_sec"
expect_error badpad 'let a = audio.sine(440.0, 0.1).pad(999999999999999.0, 0.0);' "before_sec"
expect_error badpluckdamp 'let b = audio.pluck(440.0, 0.2, 0.8, 0.5, 1.5, 44100);' "damping"
expect_error badphasefreq 'let bq = audio.biquad_design("lowpass", 1000.0, 0.707, 44100); let p = bq.phase_deg_at(30000.0);' "outside"
expect_error thdsil 'let s = audio.silence(0.2, 44100); let t = s.thd(440.0, 5);' "fundamental power"
expect_error badchan 'let b = audio.sine(440.0, 0.2); let c = b.channel(1);' "channel index"
expect_error badget 'let b = audio.sine(440.0, 0.1, 0.8, 1000); let x = b.get(1000);' "outside"
expect_error badslice 'let b = audio.sine(440.0, 0.5); let s = b.slice(0.4, 0.2);' "slice range"
expect_error diffsr 'let a = audio.sine(440.0, 0.2, 0.8, 22050); let b = audio.sine(440.0, 0.2, 0.8, 44100); let c = a.concat(b);' "sample rates differ"
expect_error badpan 'let b = audio.sine(440.0, 0.2).pan(2.0);' "pan position"
expect_error badwin 'let s = audio.sine(440.0, 0.2).spectrum("kaiser");' "unknown window"
expect_error badwav 'let b = audio.load_wav("does_not_exist.wav");' "cannot open WAV"
expect_error badbits 'let b = audio.sine(440.0, 0.1).save_wav("build/bad.wav", 12);' "bits must be"
expect_error assertfail 'audio.assert_close(1.0, 2.0, 0.1);' "assert_close failed"
expect_error bqsrmismatch 'let bq = audio.biquad_design("lowpass", 1000.0, 0.707, 48000); let y = bq.apply(audio.sine(440.0, 0.1, 0.5, 8000));' "designed for sr=48000"
expect_error barunstable 'let b = audio.bar(1, 3, 5000.0, 2.0, 0.2, 0.8, 22050);' "too high"
python3 -c "import struct,sys; d=struct.pack('<4f',0.1,float('nan'),0.2,0.3); f=struct.pack('<HHIIHH',3,1,8000,32000,4,32); sys.stdout.buffer.write(b'RIFF'+struct.pack('<I',4+8+16+8+len(d))+b'WAVEfmt '+struct.pack('<I',16)+f+b'data'+struct.pack('<I',len(d))+d)" >"$WORK/nan.wav"
expect_error nanwav "let b = audio.load_wav(\"$WORK/nan.wav\");" "not finite"
expect_error mulframes 'let a = audio.sine(440.0, 0.1, 0.5, 8000); let b = audio.sine(440.0, 0.2, 0.5, 8000); let c = a.multiply(b);' "frame counts differ"
expect_error convsr 'let a = audio.sine(440.0, 0.1, 0.5, 8000); let b = audio.impulse(0.01, 0.0, 1.0, 16000); let c = a.convolve(b);' "convolve: sample rates differ"
expect_error badpattern 'let a = audio.sine(440.0, 0.05, 0.5, 8000).pattern("x..y", 120.0, 4);' "unknown step character"
expect_error badpartial 'let a = audio.additive(220.0, 0.1, 0.5, "1 0.5 abc", 8000);' "invalid partial"
expect_error lufssilent 'let l = audio.silence(1.0, 44100).lufs();' "absolute gate"
expect_error rt60short 'let r = audio.sine(440.0, 0.5, 0.5, 8000).rt60();' "does not fall"
expect_error ksfreq 'let k = audio.karplus(10000.0, 0.5, 0.5, 1.0, 1, 22050);' "karplus frequency"
expect_error grainpos 'let g = audio.sine(440.0, 0.5, 0.5, 8000).granular(1.0, 0.05, 20.0, 1.5, 0.0, 0.0, 1);' "position"
if [ "$UNAME_S" = Linux ] && [ -w /dev/full ]; then
  expect_error devfullwav 'let b = audio.sine(440.0, 0.1); b.save_wav("/dev/full", 16);' "write failed"
  expect_error devfullcsv 'let b = audio.sine(440.0, 0.1); b.save_csv("/dev/full");' "write failed"
fi
ok "errors stop the program with exit 3 and a clear message on stdout and stderr ($nerr cases)"

# WAVE_FORMAT_EXTENSIBLE (0xFFFE) 16-bit PCM load test.
python3 - "$WORK/ext.wav" <<'PY'
import math, struct, sys
sr = 22050
n = 4410
samples = [int(round(0.8 * math.sin(2 * math.pi * 440.0 * i / sr) * 32767)) for i in range(n)]
pcm = struct.pack("<" + "h" * n, *samples)
subformat = struct.pack("<IHH8s", 1, 0x0000, 0x0010, b"\x80\x00\x00\xaa\x00\x38\x9b\x71")
fmt_body = struct.pack("<HHIIHHHHI", 0xFFFE, 1, sr, sr * 2, 2, 16, 22, 16, 4) + subformat
riff_body = b"WAVEfmt " + struct.pack("<I", len(fmt_body)) + fmt_body + b"data" + struct.pack("<I", len(pcm)) + pcm
with open(sys.argv[1], "wb") as f:
    f.write(b"RIFF" + struct.pack("<I", len(riff_body)) + riff_body)
PY
cat >"$WORK/ext.lms" <<LMS
p = _dsl_ cli="lmaudio";
let w = audio.load_wav("$WORK/ext.wav");
audio.assert_close(w.pitch(), 440.0, 1.0);
audio.assert_close(w.peak(), 0.8, 0.002);
end _dsl_
savedsl(p, "$WORK/ext.dslb");
LMS
"$LMSCRIPT" "$WORK/ext.lms" >/dev/null && "$HOST" --dsl "$WORK/ext.dslb" >/dev/null || fail "WAVE_FORMAT_EXTENSIBLE load"
ok "WAVE_FORMAT_EXTENSIBLE (0xFFFE) 16-bit PCM WAV loads accurately"

# WAV writer/reader edge cases: odd-length 24-bit mono data chunk gets its RIFF pad byte,
# out-of-range samples are clipped with a stderr warning, a truncated data chunk still loads.
cat >"$WORK/wavedge.lms" <<LMS
p = _dsl_ cli="lmaudio";
let odd = audio.sine(440.0, 0.0001, 0.5, 10000);
odd.save_wav("$WORK/odd24.wav", 24);
let back = audio.load_wav("$WORK/odd24.wav");
audio.assert_close(float(back.frames()), 1.0, 0.0);
audio.sine(440.0, 0.1, 1.5, 8000).save_wav("$WORK/hot.wav", 16);
audio.sine(440.0, 0.5, 0.5, 8000).save_wav("$WORK/full.wav", 16);
end _dsl_
savedsl(p, "$WORK/wavedge.dslb");
LMS
"$LMSCRIPT" "$WORK/wavedge.lms" >/dev/null && "$HOST" --dsl "$WORK/wavedge.dslb" >/dev/null 2>"$WORK/wavedge.err" || fail "WAV edge-case program"
[ "$(wc -c <"$WORK/odd24.wav")" = 48 ] || fail "24-bit mono 1-frame WAV should be 44 + 3 + 1 pad bytes, got $(wc -c <"$WORK/odd24.wav")"
grep -q "clipped while writing $WORK/hot.wav" "$WORK/wavedge.err" || fail "no clipping warning on stderr: $(cat "$WORK/wavedge.err")"
head -c 4044 "$WORK/full.wav" >"$WORK/trunc.wav"
cat >"$WORK/trunc.lms" <<LMS
p = _dsl_ cli="lmaudio";
let t = audio.load_wav("$WORK/trunc.wav");
audio.assert_close(float(t.frames()), 2000.0, 0.0);
end _dsl_
savedsl(p, "$WORK/trunc.dslb");
LMS
"$LMSCRIPT" "$WORK/trunc.lms" >/dev/null && "$HOST" --dsl "$WORK/trunc.dslb" >/dev/null 2>"$WORK/trunc.err" || fail "truncated WAV load"
grep -q "only 4000 are present" "$WORK/trunc.err" || fail "no truncation warning: $(cat "$WORK/trunc.err")"
ok "WAV edge cases: RIFF pad byte, clipping warning, truncated data chunk"

# Memory budget: a runaway allocation loop stops with a run error (exit 3, message on stderr and in
# the JSON line) instead of exhausting the machine. Also verify native Soundpipe allocations are charged,
# and invalid LMAUDIO_MAX_MEMORY_MB values fail on every CLI path (--version, --notices, --dsl-schema, --dsl-signatures).
cat >"$WORK/runaway.lms" <<LMS
p = _dsl_ cli="lmaudio";
let i = 0;
while (i < 1000000) {
  let b = audio.sine(440.0, 2.0, 0.5, 44100);
  i = i + 1;
}
end _dsl_
savedsl(p, "$WORK/runaway.dslb");
LMS
"$LMSCRIPT" "$WORK/runaway.lms" >/dev/null || fail "runaway.lms compile"
set +e; out="$(LMAUDIO_MAX_MEMORY_MB=64 "$HOST" --dsl "$WORK/runaway.dslb" 2>"$WORK/runaway.err")"; code=$?; set -e
[ "$code" = 3 ] || fail "runaway loop under LMAUDIO_MAX_MEMORY_MB=64: exit $code, expected 3 ($out)"
echo "$out" | grep -q "memory budget exceeded" || fail "runaway loop: JSON lacks the budget message: $out"
grep -q "LMAUDIO_MAX_MEMORY_MB" "$WORK/runaway.err" || fail "runaway loop: no budget message on stderr"
cat >"$WORK/sp_budget.lms" <<LMS
p = _dsl_ cli="lmaudio";
let s = audio.sine(440.0, 0.5, 0.5, 44100).paulstretch(20.0, 1.5);
end _dsl_
savedsl(p, "$WORK/sp_budget.dslb");
LMS
"$LMSCRIPT" "$WORK/sp_budget.lms" >/dev/null || fail "sp_budget.lms compile"
set +e; out="$(LMAUDIO_MAX_MEMORY_MB=1 "$HOST" --dsl "$WORK/sp_budget.dslb" 2>"$WORK/sp_budget.err")"; code=$?; set -e
[ "$code" = 3 ] && echo "$out" | grep -q "memory budget exceeded" || fail "paulstretch under LMAUDIO_MAX_MEMORY_MB=1: exit $code ($out)"
for flag in --version --notices --dsl-schema --dsl-signatures; do
  set +e; LMAUDIO_MAX_MEMORY_MB=abc "$HOST" "$flag" >/dev/null 2>"$WORK/badenv.err"; code=$?; set -e
  [ "$code" = 2 ] && grep -q "positive whole number" "$WORK/badenv.err" || fail "bad LMAUDIO_MAX_MEMORY_MB on $flag: exit $code"
done
set +e; LMAUDIO_MAX_MEMORY_MB=-5 "$HOST" --dsl "$WORK/runaway.dslb" >/dev/null 2>"$WORK/badenv.err"; code=$?; set -e
[ "$code" = 2 ] && grep -q "positive whole number" "$WORK/badenv.err" || fail "bad LMAUDIO_MAX_MEMORY_MB: exit $code"
if [ "$UNAME_S" = Linux ]; then
  set +e; out="$(ulimit -v 6291456; "$HOST" --dsl "$WORK/runaway.dslb" 2>/dev/null)"; code=$?; set -e
  [ "$code" = 3 ] && echo "$out" | grep -q "limit 4096 MiB" || fail "runaway loop with the default budget under ulimit -v 6 GiB: exit $code ($out)"
  ok "memory budget: runaway loop stops at 64 MiB and at the default 4096 MiB (under ulimit -v 6 GiB)"
else
  ok "memory budget: runaway loop stops at LMAUDIO_MAX_MEMORY_MB=64 (default-budget run is Linux-only)"
fi

# README gallery: every linked image, WAV audio file, and example exists and obeys magic/size limits.
magic_ok() {
  case "$1" in
    *.png) [ "$(head -c 8 "$1" | od -An -tx1 | tr -d ' \n')" = "89504e470d0a1a0a" ];;
    *.gif) [ "$(head -c 4 "$1")" = "GIF8" ];;
    *.wav) [ "$(head -c 4 "$1")" = "RIFF" ];;
    *) return 1;;
  esac
}
nimg=0
for img in $(grep -oE 'docs/images/[A-Za-z0-9_.-]+\.(png|gif)' README.md | sort -u); do
  [ -f "$img" ] || fail "README references missing image $img"
  magic_ok "$img" || fail "$img is not a valid PNG/GIF"
  size=$(wc -c <"$img" | tr -d ' ')
  case "$img" in *.gif) limit=3000000;; *) limit=300000;; esac
  [ "$size" -le "$limit" ] || fail "$img is $size bytes (limit $limit)"
  nimg=$((nimg+1))
done
[ "$nimg" -ge 22 ] || fail "README gallery shows only $nimg images (expected >= 22)"
for src in examples/*.lms; do
  grep -q "main/$src" README.md || fail "README has no autorun caption for $src"
done
nwav=0
for wav in $(grep -oE 'docs/audio/[A-Za-z0-9_.-]+\.wav' README.md | sort -u); do
  [ -f "$wav" ] || fail "README references missing audio $wav"
  magic_ok "$wav" || fail "$wav is not a valid RIFF/WAVE file"
  size=$(wc -c <"$wav" | tr -d ' ')
  [ "$size" -le 200000 ] || fail "$wav is $size bytes (limit 200000)"
  nwav=$((nwav+1))
done
[ "$nwav" -ge 14 ] || fail "README links only $nwav audio files (expected >= 14)"
for ex in $(grep -oE 'examples/[A-Za-z0-9_.-]+\.lms' README.md | sort -u); do
  [ -f "$ex" ] || fail "README references missing example $ex"
done
ok "README gallery: $nimg images and $nwav WAV files valid and within size limits, every linked example exists"

# Showcase examples: lmaudio synthesizes WAV + CSV, lmplot renders PNG/GIF, and WAV/CSV outputs are byte-identical on rerun.
if command -v lmplot >/dev/null 2>&1 || [ "${LMAUDIO_TEST_NETWORK:-}" = 1 ]; then
  mkdir -p images audio build
  ng=0
  for src in examples/*.lms; do
    name="$(basename "$src" .lms)"
    if [ "$name" = harmonic_morph_anim ] && ! command -v ffmpeg >/dev/null 2>&1; then
      echo "note: $name skipped (no ffmpeg on PATH)"
      continue
    fi
    timeout 300 "$LMSCRIPT" "$src" >"$WORK/ex-$name.log" 2>&1 || { cat "$WORK/ex-$name.log" >&2; fail "example $name"; }
    ng=$((ng+1))
  done
  for img in images/*.png images/*.gif; do
    [ -s "$img" ] || continue
    magic_ok "$img" || fail "$img is not a valid PNG/GIF"
  done
  for wav in audio/*.wav; do
    [ -s "$wav" ] || continue
    magic_ok "$wav" || fail "$wav is not a valid WAV"
  done
  # Determinism check: re-running every example's lmaudio bytecode produces byte-identical WAV and CSV files.
  for f in audio/*.wav build/*.csv; do [ -f "$f" ] && echo "$(sha "$f") $f"; done >"$WORK/before.txt"
  for dslb in build/*.dslb; do
    case "$dslb" in *-*.dslb) continue;; esac  # lmplot/lmdraw programs (name-draw, name-plot, ...)
    "$HOST" --dsl "$dslb" >/dev/null 2>&1 || fail "rerun $dslb"
  done
  for f in audio/*.wav build/*.csv; do [ -f "$f" ] && echo "$(sha "$f") $f"; done >"$WORK/after.txt"
  cmp -s "$WORK/before.txt" "$WORK/after.txt" || { diff "$WORK/before.txt" "$WORK/after.txt" >&2; fail "WAV/CSV files differ on rerun"; }
  ok "$ng showcase examples run, write valid images/WAVs, and all $(wc -l <"$WORK/after.txt" | tr -d ' ') WAV/CSV files are byte-identical on rerun"
else
  ok "showcase examples skipped (no lmplot on PATH, LMAUDIO_TEST_NETWORK unset)"
fi

# --dry-run runs the program but writes no WAV or CSV files.
cat >"$WORK/dry.lms" <<LMS
p = _dsl_ cli="lmaudio";
let s = audio.sine(440.0, 0.1, 0.8, 22050);
s.save_wav("$WORK/dry.wav", 16);
s.save_csv("$WORK/dry.csv");
end _dsl_
savedsl(p, "$WORK/dry.dslb");
LMS
"$LMSCRIPT" "$WORK/dry.lms" >/dev/null || fail "dry.lms compile"
"$HOST" --dry-run --dsl "$WORK/dry.dslb" >/dev/null 2>"$WORK/dry.err" || fail "dry run"
[ ! -e "$WORK/dry.wav" ] && [ ! -e "$WORK/dry.csv" ] || fail "--dry-run wrote a file"
grep -q "would write" "$WORK/dry.err" || fail "--dry-run did not report the skipped write"
"$HOST" --dsl "$WORK/dry.dslb" >/dev/null || fail "real run"
[ -s "$WORK/dry.wav" ] && [ -s "$WORK/dry.csv" ] || fail "save_wav/save_csv wrote nothing"
ok "--dry-run reports save_wav/save_csv without writing; a real run writes"

set +e; "$HOST" --dsl "$WORK/does-not-exist.dslb" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 3 ] || fail "missing bytecode exit code $code, expected 3"
set +e; "$HOST" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 2 ] || fail "usage exit code $code, expected 2"
ok "usage and load errors use exit codes 2 and 3"

echo "all $pass checks passed"
