#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-aarch64"
TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/aarch64-ubuntu2004.cmake"

: "${GO2_AARCH64_SYSROOT:=}"
: "${GO2_AARCH64_TOOLCHAIN_ROOT:=}"
: "${GO2_AARCH64_CROSS_TRIPLE:=aarch64-linux-gnu}"
: "${GO2_AARCH64_GRPC_PREFIX:=}"
: "${GO2_HOST_GRPC_TOOLS_PREFIX:=}"
: "${CMAKE_BUILD_TYPE:=Release}"
: "${BUILD_EXAMPLES:=OFF}"

EXTRA_CMAKE_ARGS=()
if [[ -n "${GO2_AARCH64_GRPC_PREFIX}" ]]; then
  EXTRA_CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${GO2_AARCH64_GRPC_PREFIX}")
  EXTRA_CMAKE_ARGS+=("-DProtobuf_DIR=${GO2_AARCH64_GRPC_PREFIX}/lib/cmake/protobuf")
  EXTRA_CMAKE_ARGS+=("-DgRPC_DIR=${GO2_AARCH64_GRPC_PREFIX}/lib/cmake/grpc")
fi

if [[ -n "${GO2_HOST_GRPC_TOOLS_PREFIX}" ]]; then
  EXTRA_CMAKE_ARGS+=("-DProtobuf_PROTOC_EXECUTABLE=${GO2_HOST_GRPC_TOOLS_PREFIX}/bin/protoc")
  EXTRA_CMAKE_ARGS+=("-DGRPC_CPP_PLUGIN_EXECUTABLE=${GO2_HOST_GRPC_TOOLS_PREFIX}/bin/grpc_cpp_plugin")
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
  -DGO2_AARCH64_SYSROOT="${GO2_AARCH64_SYSROOT}" \
  -DGO2_AARCH64_TOOLCHAIN_ROOT="${GO2_AARCH64_TOOLCHAIN_ROOT}" \
  -DGO2_AARCH64_CROSS_TRIPLE="${GO2_AARCH64_CROSS_TRIPLE}" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
  -DBUILD_EXAMPLES="${BUILD_EXAMPLES}" \
  -DBUILD_GO2_GRPC=ON \
  "${EXTRA_CMAKE_ARGS[@]}"

cmake --build "${BUILD_DIR}" -j"$(nproc)"

BIN="${BUILD_DIR}/bin/go2_sport_grpc_server"
if [[ -f "${BIN}" ]]; then
  file "${BIN}"
else
  echo "Expected binary not found: ${BIN}" >&2
  exit 2
fi
