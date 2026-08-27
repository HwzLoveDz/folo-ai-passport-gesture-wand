#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd "$(dirname "$0")" && pwd)"
repo_dir="$(cd "$test_dir/.." && pwd)"
out_bin="${TMPDIR:-/tmp}/gesture_model_test"

cc -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$repo_dir/main" \
    "$repo_dir/main/gesture_model.c" \
    "$test_dir/test_gesture_model.c" \
    -lm -o "$out_bin"
"$out_bin"
rm -f "$out_bin"
