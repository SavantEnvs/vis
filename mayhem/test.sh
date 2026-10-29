#!/usr/bin/env bash
#
# mayhem/test.sh — RUN vis' OWN functional test suites (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# vis' own suites, all pre-built by mayhem/build.sh with vis' normal flags:
#   test/core  C unit tests (TAP: buffer, map, text)
#   test/vis   editor scenarios: keystrokes/commands -> compared against .ref files
#   test/lua   Lua API scenarios (need a vis built +lua)
#   test/vim   keystroke scenarios replayed through test/util/keys, compared against .ref files
# Every runner must report a positive number of passes: a runner that prints nothing (or that a
# patch turned into `exit 0`) fails the check instead of silently counting zero.
export LC_ALL=C.UTF-8
export VIM=mayhem-no-such-vim   # force vis-only runs; never depend on a host vim

passed=0 failed=0 skipped=0
broken() { echo "TEST-SETUP-ERROR: $*" >&2; failed=$((failed + 1)); }

[ -x ./vis ] || { echo "./vis missing: mayhem/build.sh should have built it" >&2; exit 1; }
# the Lua suites silently skip (exit 0) when vis lacks lua; that must be a failure here
./vis -v 2>&1 | grep -q '+lua' || broken "vis was built without +lua; test/vis and test/lua would silently skip"

# --- test/core: TAP ---------------------------------------------------------------------------
for t in buffer-test map-test text-test; do
  [ -x "test/core/$t" ] || { broken "test/core/$t missing"; continue; }
  out=$(cd test/core && "./$t" 2>&1); rc=$?
  ok=$(printf '%s\n' "$out" | grep -c '^ok ' || true)
  nok=$(printf '%s\n' "$out" | grep -c '^not ok ' || true)
  echo "test/core/$t: ok=$ok not_ok=$nok rc=$rc"
  [ "$ok" -gt 0 ] || broken "test/core/$t reported no passing tests"
  passed=$((passed + ok)); failed=$((failed + nok))
  [ "$rc" -eq 0 ] || [ "$nok" -gt 0 ] || broken "test/core/$t exited $rc without a failing test line"
done

# --- test/vis, test/lua, test/vim: "Tests ok N/M[, skipped K]" ----------------------------------
run_suite() {  # <dir> [env...]
  local dir="$1" out rc ok run skip
  out=$(cd "test/$dir" && "${@:2}" ./test.sh 2>&1); rc=$?
  line=$(printf '%s\n' "$out" | grep -E '^Tests ok [0-9]+/[0-9]+' | tail -1)
  ok=$(printf '%s' "$line" | sed -E 's|^Tests ok ([0-9]+)/([0-9]+).*|\1|')
  run=$(printf '%s' "$line" | sed -E 's|^Tests ok ([0-9]+)/([0-9]+).*|\2|')
  skip=$(printf '%s' "$line" | sed -nE 's|.*skipped ([0-9]+).*|\1|p')
  echo "test/$dir: ${line:-<no summary line>} rc=$rc"
  if [ -z "$line" ] || [ "${ok:-0}" -le 0 ]; then
    printf '%s\n' "$out" | tail -20 >&2
    broken "test/$dir produced no passing scenarios"
    return
  fi
  passed=$((passed + ok)); failed=$((failed + run - ok)); skipped=$((skipped + ${skip:-0}))
  [ "$rc" -eq 0 ] || [ "$run" -gt "$ok" ] || broken "test/$dir exited $rc although all scenarios passed"
}
run_suite vis
run_suite lua
run_suite vim

# --- vis-digraph: known-answer lookups through the real binary ---------------------------------
kat() {  # <expected> <digraph>...
  local want="$1" got; shift
  got=$(./vis-digraph "$@" 2>/dev/null)
  if [ "$got" = "$want" ]; then passed=$((passed + 1)); else
    echo "vis-digraph $*: want [$want] got [$got]" >&2; failed=$((failed + 1)); fi
}
kat '#'  Nb
kat 'ä'  'a:'
kat 'é'  "e'"
kat 'É'  "E'"
kat 'ß'  ss
kat '#$' Nb DO
kat ' '  SP
./vis-digraph 'Zz-not-a-digraph' >/dev/null 2>&1 && broken "unknown digraph was accepted" || passed=$((passed + 1))

emit_ctrf "vis-tests" "$passed" "$failed" "$skipped"
