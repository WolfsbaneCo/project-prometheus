#!/bin/bash
# The checks CI runs on every build, for running locally before a push (CI itself runs only when
# started by hand, to save Actions minutes):
#   tools/check-before-push.sh [build dir]
# 1. No game files, saves or save states are tracked (N64 and GBA).
# 2. The save converters' round-trip and golden tests pass (skipped if the build dir is missing).
set -euo pipefail
cd "$(dirname "$0")/.."
bad=$(git ls-files | grep -iE '\.(z64|n64|v64|ndd|d64|gba|agb|sra|srm|eep|fla|mpk|sav|sgm|pj[0-9]?|st[0-9]|ss[0-9]|state[0-9]*)(\.zip)?$' || true)
if [ -n "$bad" ]; then
  echo "These files must not be committed:"
  echo "$bad"
  exit 1
fi
echo "No game or save files tracked."
build=${1:-build}
if [ "${CHECK_FILES_ONLY:-}" = 1 ]; then exit 0; fi
if [ -d "$build" ]; then
  cmake --build "$build" --parallel
  ctest --test-dir "$build" -C Release --output-on-failure
else
  echo "No build in $build: tests skipped (cmake -S . -B $build first)."
fi
