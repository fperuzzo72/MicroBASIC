#!/usr/bin/env bash
# Host-side tests for the parts of the firmware that are plain C++ with no
# hardware dependencies. Runs in about a second, versus a build+flash+serial
# cycle for the same coverage on device -- which is the whole point: the
# expensive part of this project has consistently been hardware debugging
# loops, so anything testable here should be tested here first.
#
#   ./test/run_tests.sh
set -euo pipefail

cd "$(dirname "$0")/.."
SRC=editor/src
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail=0

run() {
  local name=$1; shift
  echo "### $name"
  if c++ -std=c++17 -Wall -Wextra -I"$SRC" "$@" -o "$OUT/$name" && "$OUT/$name"; then
    echo
  else
    fail=1
    echo "### $name FAILED"
    echo
  fi
}

run test_program_store test/test_program_store.cpp "$SRC/program_store.cpp"

exit $fail
