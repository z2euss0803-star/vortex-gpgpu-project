#!/bin/bash
set -euo pipefail
test "$#" -eq 3 || { echo 'Usage: audit_image.sh CANDIDATE_BASE SNAPSHOT FRESH_RUN_ID' >&2; exit 2; }
test "$(cat "$1/build.exit")" = 0 || { echo 'Actual candidate build did not exit successfully' >&2; exit 1; }
script_dir=$(cd "$(dirname "$0")" && pwd)
python3 "$script_dir/image_audit.py" --base "$1" --snapshot "$2" --run-id "$3"
