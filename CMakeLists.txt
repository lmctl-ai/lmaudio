cmake_minimum_required(VERSION 3.24)
project(lmaudio VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
option(LMAUDIO_FULL_STATIC "Link the complete executable statically" ON)

foreach(dep daisysp soundpipe)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${dep}")
    message(FATAL_ERROR "third_party/${dep} missing: run scripts/sync-deps.sh")
  endif()
endforeach()

# Embed schema and notices.
function(embed_file var file ns name)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
  file(READ "${file}" hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
  set(${var} "inline constexpr unsigned char ${name}[] = {${hex}0}; inline constexpr std::size_t ${name}_size = sizeof(${name}) - 1;\n" PARENT_SCOPE)
endfunction()

set(GEN "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN}")
embed_file(SCHEMA_SRC "${CMAKE_CURRENT_SOURCE_DIR}/schema/lmaudio.dsl.json" lmaudio embedded_schema)
file(WRITE "${GEN}/schema_embedded.hpp" "#pragma once\n#include <cstddef>\nnamespace lmaudio { ${SCHEMA_SRC} }\n")
embed_file(NOTICES_SRC "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES" lmaudio embedded_notices)
file(WRITE "${GEN}/notices_embedded.hpp" "#pragma once\n#include <cstddef>\nnamespace lmaudio { ${NOTICES_SRC} }\n")

# Soundpipe (MIT): generate soundpipe.h from the selected pure-C DSP modules (no libsndfile).
set(SP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/soundpipe")
set(SP_MODULES
  base ftbl tevent randmt
  adsr allpass atone autowah bal bar biquad biscale fold bitcrush
  blsaw blsquare bltriangle brown butbp butbr buthp butlp
  clamp clip clock comb compressor count conv crossfade dcblock delay
  diode dist dmetro drip dtrig dust eqfil expon fof fog fofilt fosc
  gbuzz hilbert in incr jcrev jitter line lpf18 maygate metro mincer
  mode moogladder noise osc oscmorph pan2 panst pareq paulstretch
  pdhalf peaklim phaser phasor pinknoise pitchamdf pluck port posc3
  pshift ptrack randh randi random reverse reson revsc rms rpt rspline
  saturator samphold scale scrambler sdelay slice smoothdelay sndwarp
  streson switch tabread tadsr talkbox tblrec tbvcf tdiv tenv tenv2
  tenvx tgate thresh timer tin tone trand tseg tseq vdelay voc vocoder
  waveset wpkorg35 zitarev fftwrapper padsynth
)

set(SP_HEADER_TEXT "#ifndef SOUNDPIPE_H\n#define SOUNDPIPE_H\n")
foreach(mod ${SP_MODULES})
  file(READ "${SP_DIR}/h/${mod}.h" mod_h)
  string(APPEND SP_HEADER_TEXT "${mod_h}\n")
endforeach()
string(APPEND SP_HEADER_TEXT "#endif\n")
file(WRITE "${GEN}/soundpipe.h" "${SP_HEADER_TEXT}")

file(READ "${SP_DIR}/lib/fft/fftlib.c" FFTLIB_C)
file(READ "${SP_DIR}/lib/fft/sp_fft.c" SP_FFT_C)
file(WRITE "${GEN}/sp_fft_combined.c" "${FFTLIB_C}\n${SP_FFT_C}\n")

set(SP_SRCS
  "${GEN}/sp_fft_combined.c"
  "${SP_DIR}/lib/kissfft/kiss_fft.c"
  "${SP_DIR}/lib/kissfft/kiss_fftr.c"
)
foreach(mod ${SP_MODULES})
  list(APPEND SP_SRCS "${SP_DIR}/modules/${mod}.c")
endforeach()

add_library(soundpipe_core STATIC ${SP_SRCS})
target_compile_definitions(soundpipe_core PUBLIC SPFLOAT=float NO_LIBSNDFILE _USE_MATH_DEFINES)
target_include_directories(soundpipe_core SYSTEM PUBLIC
  "${GEN}"
  "${SP_DIR}/h"
  "${SP_DIR}/lib/faust"
  "${SP_DIR}/lib/kissfft"
  "${SP_DIR}/lib/fft"
)
if(NOT MSVC)
  target_compile_options(soundpipe_core PRIVATE -w)
endif()

# DaisySP (MIT): synthesis, physical modeling, analog/state-variable filters, drums, effects.
set(DAISY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/daisysp")
set(DAISY_SRCS
  "${DAISY_DIR}/Source/Control/adenv.cpp"
  "${DAISY_DIR}/Source/Control/adsr.cpp"
  "${DAISY_DIR}/Source/Control/phasor.cpp"
  "${DAISY_DIR}/Source/Drums/analogbassdrum.cpp"
  "${DAISY_DIR}/Source/Drums/analogsnaredrum.cpp"
  "${DAISY_DIR}/Source/Drums/hihat.cpp"
  "${DAISY_DIR}/Source/Drums/synthbassdrum.cpp"
  "${DAISY_DIR}/Source/Drums/synthsnaredrum.cpp"
  "${DAISY_DIR}/Source/Dynamics/crossfade.cpp"
  "${DAISY_DIR}/Source/Dynamics/limiter.cpp"
  "${DAISY_DIR}/Source/Effects/autowah.cpp"
  "${DAISY_DIR}/Source/Effects/chorus.cpp"
  "${DAISY_DIR}/Source/Effects/decimator.cpp"
  "${DAISY_DIR}/Source/Effects/flanger.cpp"
  "${DAISY_DIR}/Source/Effects/overdrive.cpp"
  "${DAISY_DIR}/Source/Effects/phaser.cpp"
  "${DAISY_DIR}/Source/Effects/sampleratereducer.cpp"
  "${DAISY_DIR}/Source/Effects/tremolo.cpp"
  "${DAISY_DIR}/Source/Effects/wavefolder.cpp"
  "${DAISY_DIR}/Source/Filters/ladder.cpp"
  "${DAISY_DIR}/Source/Filters/svf.cpp"
  "${DAISY_DIR}/Source/Filters/soap.cpp"
  "${DAISY_DIR}/Source/Noise/clockednoise.cpp"
  "${DAISY_DIR}/Source/Noise/grainlet.cpp"
  "${DAISY_DIR}/Source/Noise/particle.cpp"
  "${DAISY_DIR}/Source/PhysicalModeling/drip.cpp"
  "${DAISY_DIR}/Source/PhysicalModeling/modalvoice.cpp"
  "${DAISY_DIR}/Source/PhysicalModeling/resonator.cpp"
  "${DAISY_DIR}/Source/PhysicalModeling/KarplusString.cpp"
  "${DAISY_DIR}/Source/PhysicalModeling/stringvoice.cpp"
  "${DAISY_DIR}/Source/Sampling/granularplayer.cpp"
  "${DAISY_DIR}/Source/Synthesis/fm2.cpp"
  "${DAISY_DIR}/Source/Synthesis/formantosc.cpp"
  "${DAISY_DIR}/Source/Synthesis/oscillator.cpp"
  "${DAISY_DIR}/Source/Synthesis/oscillatorbank.cpp"
  "${DAISY_DIR}/Source/Synthesis/variablesawosc.cpp"
  "${DAISY_DIR}/Source/Synthesis/variableshapeosc.cpp"
  "${DAISY_DIR}/Source/Synthesis/vosim.cpp"
  "${DAISY_DIR}/Source/Synthesis/zoscillator.cpp"
  "${DAISY_DIR}/Source/Utility/dcblock.cpp"
  "${DAISY_DIR}/Source/Utility/metro.cpp"
)
add_library(daisysp_core STATIC ${DAISY_SRCS})
target_compile_definitions(daisysp_core PUBLIC _USE_MATH_DEFINES)
target_include_directories(daisysp_core SYSTEM PUBLIC
  "${DAISY_DIR}/Source"
  PRIVATE
  "${DAISY_DIR}/Source/Control"
  "${DAISY_DIR}/Source/Drums"
  "${DAISY_DIR}/Source/Dynamics"
  "${DAISY_DIR}/Source/Effects"
  "${DAISY_DIR}/Source/Filters"
  "${DAISY_DIR}/Source/Noise"
  "${DAISY_DIR}/Source/PhysicalModeling"
  "${DAISY_DIR}/Source/Sampling"
  "${DAISY_DIR}/Source/Synthesis"
  "${DAISY_DIR}/Source/Utility"
)
if(NOT MSVC)
  target_compile_options(daisysp_core PRIVATE -w)
endif()

add_executable(lmaudio cpp/main.cpp cpp/audio.cpp cpp/memory_budget.cpp)
target_compile_definitions(lmaudio PRIVATE LMAUDIO_VERSION="${PROJECT_VERSION}" _USE_MATH_DEFINES)
target_include_directories(lmaudio PRIVATE cpp "${GEN}")
target_link_libraries(lmaudio PRIVATE daisysp_core soundpipe_core)
if(NOT WIN32)
  target_link_libraries(lmaudio PRIVATE m)
endif()
if(NOT MSVC)
  target_compile_options(lmaudio PRIVATE -Wall -Wextra)
endif()
if((CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang") AND NOT APPLE)
  target_link_options(lmaudio PRIVATE -static-libstdc++ -static-libgcc)
  if(LMAUDIO_FULL_STATIC)
    target_link_options(lmaudio PRIVATE -static)
  endif()
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(lmaudio PRIVATE -s)
  endif()
endif()
