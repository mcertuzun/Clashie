#!/bin/bash
# Build MiniClash for the web using Emscripten
#
# Prerequisites:
#   - Emscripten SDK installed and activated (source emsdk_env.sh)
#   - emcmake and emmake available on PATH
#
# Usage:
#   ./build_web.sh [Debug|Release]
#
# Output:
#   build_web/game/miniclash.html  — open in browser (or use the custom shell)
#   build_web/game/miniclash.js
#   build_web/game/miniclash.wasm

set -euo pipefail

BUILD_TYPE="${1:-Release}"

echo "=== Building MiniClash for Web (${BUILD_TYPE}) ==="

# Check Emscripten is available
if ! command -v emcmake &>/dev/null; then
    echo "Error: emcmake not found. Please install and activate the Emscripten SDK first."
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk && ./emsdk install latest && ./emsdk activate latest"
    echo "  source emsdk_env.sh"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_web"

mkdir -p "${BUILD_DIR}"

echo "--- Configuring CMake ---"
emcmake cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DPEBBLE_BUILD_TESTS=OFF

echo "--- Building ---"
emmake cmake --build "${BUILD_DIR}" -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

# Copy custom HTML shell next to the output
if [ -f "${BUILD_DIR}/game/miniclash.js" ]; then
    cp "${SCRIPT_DIR}/web/index.html" "${BUILD_DIR}/game/index.html"
    echo ""
    echo "=== Build complete ==="
    echo "Serve the build directory and open in a browser:"
    echo "  cd ${BUILD_DIR}/game && python3 -m http.server 8080"
    echo "  Open http://localhost:8080/index.html"
else
    echo ""
    echo "=== Build complete ==="
    echo "Output: ${BUILD_DIR}/game/"
fi
