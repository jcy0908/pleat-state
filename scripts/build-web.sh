#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
DIST_DIR="${PROJECT_ROOT}/dist"
WEB_DIR="${PROJECT_ROOT}/web"

if ! command -v em++ >/dev/null 2>&1; then
  echo "error: em++ was not found. Activate the Emscripten SDK before building." >&2
  echo "see: https://emscripten.org/docs/getting_started/downloads.html" >&2
  exit 127
fi

required_files=(
  "${PROJECT_ROOT}/src/Cloth.cpp"
  "${PROJECT_ROOT}/src/wasm_bindings.cpp"
  "${PROJECT_ROOT}/include/pleat/Cloth.hpp"
  "${PROJECT_ROOT}/include/pleat/wasm_api.h"
  "${WEB_DIR}/index.html"
  "${WEB_DIR}/app.js"
  "${WEB_DIR}/styles.css"
  "${WEB_DIR}/favicon.svg"
  "${WEB_DIR}/og-image.png"
)

for required_file in "${required_files[@]}"; do
  if [[ ! -f "${required_file}" ]]; then
    echo "error: required file is missing: ${required_file}" >&2
    exit 1
  fi
done

if [[ -d "${DIST_DIR}" ]]; then
  find "${DIST_DIR}" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
else
  mkdir -p "${DIST_DIR}"
fi

cp -R "${WEB_DIR}/." "${DIST_DIR}/"

em++ \
  "${PROJECT_ROOT}/src/Cloth.cpp" \
  "${PROJECT_ROOT}/src/wasm_bindings.cpp" \
  -I"${PROJECT_ROOT}/include" \
  -std=c++20 \
  -O3 \
  -DNDEBUG \
  -fexceptions \
  --no-entry \
  -sWASM=1 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createPleatModule \
  -sENVIRONMENT=web \
  -sALLOW_MEMORY_GROWTH=1 \
  -sFILESYSTEM=0 \
  -sMALLOC=emmalloc \
  -sDISABLE_EXCEPTION_CATCHING=0 \
  -sASSERTIONS=0 \
  -sEXPORTED_FUNCTIONS='["_ps_create","_ps_destroy","_ps_reset","_ps_step","_ps_set_params","_ps_pointer_down","_ps_pointer_move","_ps_pointer_up","_ps_particle_count","_ps_positions_ptr","_ps_index_count","_ps_indices_ptr","_ps_energy"]' \
  -sEXPORTED_RUNTIME_METHODS='["cwrap","HEAPF32","HEAPU32"]' \
  -o "${DIST_DIR}/pleat.js"

touch "${DIST_DIR}/.nojekyll"

echo "Built ${DIST_DIR}/pleat.js and ${DIST_DIR}/pleat.wasm"
echo "Serve ${DIST_DIR} over HTTP; opening index.html directly will not load WebAssembly."
