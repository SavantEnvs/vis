#!/usr/bin/env bash
#
# mayhem/build.sh — build vis' fuzz harnesses (sanitized, DWARF 3) AND the project's own test suite
# (normal flags). Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem, offline-safe:
# every dependency (libtermkey, ncurses, lua, lpeg, acl, tre) is an apt package baked into the image.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# 1) The project's own test suite, built with vis' NORMAL flags (independent of the sanitized
#    harness builds below): the editor + helper programs, the C unit tests (test/core) and the
#    keystroke driver used by test/vim. mayhem/test.sh only RUNS these.
env -u CFLAGS -u LDFLAGS -u CPPFLAGS ./configure CFLAGS="-O2 $COVERAGE_FLAGS" LDFLAGS="$COVERAGE_FLAGS" >/dev/null
make -j"$MAYHEM_JOBS" CFLAGS_EXTRA="$COVERAGE_FLAGS"
make -C test/util keys
make -C test/core CFLAGS_EXTRA="$COVERAGE_FLAGS" buffer-test map-test text-test

# 2) Fuzz harnesses. The project sources are compiled straight into each harness with
#    $SANITIZER_FLAGS + $DEBUG_FLAGS (so the fuzzed code is instrumented and carries DWARF 3).
#    -DBUFFER_SIZE=4 -DBLOCK_SIZE=4 are the tiny block sizes upstream's own test/fuzz and test/core
#    use, so the piece-table / gap-buffer growth paths are reachable from small inputs.
HFLAGS=(-O1 -std=c99 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -DNDEBUG -DVERSION=\"mayhem\" -DVIS_API=0 -DBUFFER_SIZE=4 -DBLOCK_SIZE=4
        -Wno-initializer-overrides -Wno-override-init -Wno-unused-function -I"$SRC")

# LeakSanitizer off at build time (never at runtime).
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o /tmp/lsan_off.o
# Run-once driver as a C object, so the harness' LLVMFuzzerTestOneInput keeps C linkage.
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o /tmp/standalone_main.o

# vis is a unity build (main.c, test/core): each harness #includes the project .c files it fuzzes.
build_harness() {  # <name> <harness.c>
  local name="$1" harness="$2"
  $CC "${HFLAGS[@]}" $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE \
      "$harness" /tmp/lsan_off.o -o "/mayhem/$name"
  $CC "${HFLAGS[@]}" $SANITIZER_FLAGS $DEBUG_FLAGS \
      "$harness" /tmp/standalone_main.o /tmp/lsan_off.o -o "/mayhem/$name-standalone"
}

build_harness text-libfuzzer   mayhem/harnesses/text-libfuzzer.c
build_harness buffer-fuzzer    mayhem/harnesses/buffer-fuzzer.c
build_harness vis-digraph-fuzzer mayhem/harnesses/vis-digraph.c
