#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-local-x64"

: "${GO2_LOCAL_GRPC_PREFIX:=${ROOT_DIR}/thirdparty/grpc_local/install}"
: "${CMAKE_BUILD_TYPE:=Debug}"
: "${BUILD_EXAMPLES:=OFF}"
: "${BUILD_TESTING:=ON}"

EXTRA_CMAKE_ARGS=()
if [[ -x "${GO2_LOCAL_GRPC_PREFIX}/bin/protoc" && -x "${GO2_LOCAL_GRPC_PREFIX}/bin/grpc_cpp_plugin" ]]; then
  echo "Using local grpc toolchain: ${GO2_LOCAL_GRPC_PREFIX}"
  EXTRA_CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${GO2_LOCAL_GRPC_PREFIX}")
  EXTRA_CMAKE_ARGS+=("-DProtobuf_DIR=${GO2_LOCAL_GRPC_PREFIX}/lib/cmake/protobuf")
  EXTRA_CMAKE_ARGS+=("-DgRPC_DIR=${GO2_LOCAL_GRPC_PREFIX}/lib/cmake/grpc")
  EXTRA_CMAKE_ARGS+=("-DProtobuf_PROTOC_EXECUTABLE=${GO2_LOCAL_GRPC_PREFIX}/bin/protoc")
  EXTRA_CMAKE_ARGS+=("-DGRPC_CPP_PLUGIN_EXECUTABLE=${GO2_LOCAL_GRPC_PREFIX}/bin/grpc_cpp_plugin")
else
  if ! command -v protoc >/dev/null 2>&1 || ! command -v grpc_cpp_plugin >/dev/null 2>&1; then
    echo "Neither local nor system grpc toolchain found." >&2
    echo "Install system packages (Ubuntu):" >&2
    echo "  sudo apt update && sudo apt install -y libprotobuf-dev protobuf-compiler libgrpc++-dev libgrpc-dev protobuf-compiler-grpc pkg-config" >&2
    echo "Or run ./scripts/go2_grpc/bootstrap_local_grpc.sh" >&2
    exit 2
  fi
  echo "Using system grpc/protobuf from PATH"
fi

if ! pkg-config --exists gstreamer-1.0 gstreamer-app-1.0 gstreamer-webrtc-1.0 gstreamer-sdp-1.0; then
  echo "Warning: GStreamer WebRTC dev libs not found." >&2
  echo "Install for audio WebRTC mode (Ubuntu):" >&2
  echo "  sudo apt install -y libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-good gstreamer1.0-plugins-bad" >&2
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
  -DBUILD_EXAMPLES="${BUILD_EXAMPLES}" \
  -DBUILD_GO2_GRPC=ON \
  -DBUILD_TESTING="${BUILD_TESTING}" \
  "${EXTRA_CMAKE_ARGS[@]}"

cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo
echo "Build complete: ${BUILD_DIR}"
echo "Run unit tests:"
echo "ctest --test-dir \"${BUILD_DIR}\" --output-on-failure"
