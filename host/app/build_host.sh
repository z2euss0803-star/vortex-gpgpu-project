#!/usr/bin/env bash
set -euo pipefail
base="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
pkg-config --exists json-c libdrm
json_prefix="$(pkg-config --variable=prefix json-c)"
drm_prefix="$(pkg-config --variable=prefix libdrm)"
out="${OUT:-$base/out-host}"
make -C "$base/runtime" -f Makefile.unified OUT="$out"  NN_JSON_PREFIX="$json_prefix" NN_JSON_FLAGS="$(pkg-config --cflags json-c)"  NN_JSON_LIBS="$(pkg-config --libs json-c) -lm" U18_DRM_PREFIX="$drm_prefix"  U18_DRM_FLAGS="$(pkg-config --cflags libdrm)" U18_DRM_LIBS="$(pkg-config --libs libdrm)"  -j"${JOBS:-2}" "$out/vxgpud"
