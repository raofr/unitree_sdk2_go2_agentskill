#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEPS_DIR="${ROOT_DIR}/thirdparty/grpc_local"
SRC_DIR="${DEPS_DIR}/src"
BUILD_DIR="${DEPS_DIR}/build"
INSTALL_DIR="${DEPS_DIR}/install"

mkdir -p "${SRC_DIR}" "${BUILD_DIR}" "${INSTALL_DIR}"

: "${PROTOBUF_TAG:=v25.3}"
: "${GRPC_TAG:=v1.62.2}"
: "${CMAKE_BUILD_TYPE:=Release}"

if [[ ! -d "${SRC_DIR}/protobuf/.git" ]]; then
  echo "[1/4] Cloning protobuf ${PROTOBUF_TAG}"
  git clone --branch "${PROTOBUF_TAG}" --depth 1 https://github.com/protocolbuffers/protobuf.git "${SRC_DIR}/protobuf"
fi

if [[ ! -d "${SRC_DIR}/protobuf/third_party/abseil-cpp" || ! -f "${SRC_DIR}/protobuf/third_party/abseil-cpp/CMakeLists.txt" ]]; then
  echo "[1.1/4] Initializing protobuf submodules"
  git -C "${SRC_DIR}/protobuf" submodule update --init --recursive
fi

if [[ ! -d "${SRC_DIR}/grpc/.git" ]]; then
  echo "[2/4] Cloning grpc ${GRPC_TAG}"
  git clone --branch "${GRPC_TAG}" --depth 1 https://github.com/grpc/grpc.git "${SRC_DIR}/grpc"
fi

if [[ ! -d "${SRC_DIR}/grpc/third_party/abseil-cpp" || ! -f "${SRC_DIR}/grpc/third_party/abseil-cpp/CMakeLists.txt" ]]; then
  echo "[2.1/4] Initializing grpc submodules"
  git -C "${SRC_DIR}/grpc" submodule update --init --recursive
fi

if [[ ! -x "${INSTALL_DIR}/bin/protoc" ]]; then
  echo "[3/4] Building host protobuf"
  cmake -S "${SRC_DIR}/protobuf" -B "${BUILD_DIR}/protobuf-host" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -Dprotobuf_BUILD_TESTS=OFF
  cmake --build "${BUILD_DIR}/protobuf-host" -j"$(nproc)"
  cmake --install "${BUILD_DIR}/protobuf-host"
fi

if [[ ! -x "${INSTALL_DIR}/bin/grpc_cpp_plugin" ]]; then
  echo "[4/4] Building host grpc (with grpc_cpp_plugin)"
  cmake -S "${SRC_DIR}/grpc" -B "${BUILD_DIR}/grpc-host" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -DCMAKE_PREFIX_PATH="${INSTALL_DIR}" \
    -DgRPC_INSTALL=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -DBUILD_TESTING=OFF \
    -DABSL_BUILD_TESTING=OFF \
    -DRE2_BUILD_TESTING=OFF \
    -DCARES_BUILD_TESTS=OFF \
    -DBENCHMARK_ENABLE_TESTING=OFF \
    -DgRPC_ABSL_PROVIDER=module \
    -DgRPC_CARES_PROVIDER=module \
    -DgRPC_RE2_PROVIDER=module \
    -DgRPC_SSL_PROVIDER=module \
    -DgRPC_ZLIB_PROVIDER=module \
    -DgRPC_PROTOBUF_PROVIDER=package \
    -DProtobuf_DIR="${INSTALL_DIR}/lib/cmake/protobuf" \
    -DProtobuf_PROTOC_EXECUTABLE="${INSTALL_DIR}/bin/protoc"
  cmake --build "${BUILD_DIR}/grpc-host" -j"$(nproc)"
  cmake --install "${BUILD_DIR}/grpc-host"
fi

cat <<EOF

Local gRPC/protobuf bootstrap complete.

Use these before local build:
export GO2_LOCAL_GRPC_PREFIX="${INSTALL_DIR}"
export CMAKE_PREFIX_PATH="\${GO2_LOCAL_GRPC_PREFIX}"
export Protobuf_DIR="\${GO2_LOCAL_GRPC_PREFIX}/lib/cmake/protobuf"
export gRPC_DIR="\${GO2_LOCAL_GRPC_PREFIX}/lib/cmake/grpc"
export Protobuf_PROTOC_EXECUTABLE="\${GO2_LOCAL_GRPC_PREFIX}/bin/protoc"
export GRPC_CPP_PLUGIN_EXECUTABLE="\${GO2_LOCAL_GRPC_PREFIX}/bin/grpc_cpp_plugin"

Then run:
./scripts/go2_grpc/local_build_x64.sh
EOF
