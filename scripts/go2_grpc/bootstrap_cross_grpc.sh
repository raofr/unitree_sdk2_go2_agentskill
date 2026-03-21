#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEPS_DIR="${ROOT_DIR}/thirdparty/grpc_cross"
SRC_DIR="${DEPS_DIR}/src"
BUILD_DIR="${DEPS_DIR}/build"
INSTALL_DIR="${DEPS_DIR}/install"
TOOLS_DIR="${DEPS_DIR}/tools"

mkdir -p "${SRC_DIR}" "${BUILD_DIR}" "${INSTALL_DIR}" "${TOOLS_DIR}"

: "${ARM_GNU_TOOLCHAIN_URL:=https://developer.arm.com/-/media/Files/downloads/gnu/13.2.Rel1/binrel/arm-gnu-toolchain-13.2.Rel1-x86_64-aarch64-none-linux-gnu.tar.xz}"
: "${PROTOBUF_TAG:=v25.3}"
: "${GRPC_TAG:=v1.62.2}"
: "${SSHPASS_TAG:=1.10}"

TOOLCHAIN_TAR="${SRC_DIR}/$(basename "${ARM_GNU_TOOLCHAIN_URL}")"
TOOLCHAIN_EXTRACT_DIR="${TOOLS_DIR}/arm-gnu"
HOST_PREFIX="${INSTALL_DIR}/host"
AARCH64_PREFIX="${INSTALL_DIR}/aarch64"

fetch() {
  local url="$1"
  local out="$2"
  if [[ -f "${out}" ]]; then
    return 0
  fi
  if command -v curl >/dev/null 2>&1; then
    curl -L --fail -o "${out}" "${url}"
  else
    wget -O "${out}" "${url}"
  fi
}

if [[ ! -d "${TOOLCHAIN_EXTRACT_DIR}/bin" ]]; then
  echo "[1/7] Downloading ARM GNU toolchain"
  fetch "${ARM_GNU_TOOLCHAIN_URL}" "${TOOLCHAIN_TAR}"
  mkdir -p "${TOOLCHAIN_EXTRACT_DIR}"
  tar -xf "${TOOLCHAIN_TAR}" -C "${TOOLCHAIN_EXTRACT_DIR}" --strip-components=1
fi

CROSS_TRIPLE="aarch64-none-linux-gnu"
if [[ ! -x "${TOOLCHAIN_EXTRACT_DIR}/bin/${CROSS_TRIPLE}-gcc" ]]; then
  echo "Cross compiler not found in ${TOOLCHAIN_EXTRACT_DIR}/bin" >&2
  exit 10
fi

if [[ ! -x "${TOOLS_DIR}/sshpass/bin/sshpass" ]]; then
  echo "[2/7] Building local sshpass"
  SSHPASS_TAR="${SRC_DIR}/sshpass-${SSHPASS_TAG}.tar.gz"
  fetch "https://sourceforge.net/projects/sshpass/files/sshpass/${SSHPASS_TAG}/sshpass-${SSHPASS_TAG}.tar.gz/download" "${SSHPASS_TAR}"
  rm -rf "${BUILD_DIR}/sshpass"
  mkdir -p "${BUILD_DIR}/sshpass"
  tar -xf "${SSHPASS_TAR}" -C "${BUILD_DIR}/sshpass" --strip-components=1
  (
    cd "${BUILD_DIR}/sshpass"
    ./configure --prefix="${TOOLS_DIR}/sshpass"
    make -j"$(nproc)"
    make install
  )
fi

if [[ ! -d "${SRC_DIR}/protobuf/.git" ]]; then
  echo "[3/7] Cloning protobuf ${PROTOBUF_TAG}"
  git clone --branch "${PROTOBUF_TAG}" --depth 1 https://github.com/protocolbuffers/protobuf.git "${SRC_DIR}/protobuf"
fi

if [[ ! -d "${SRC_DIR}/protobuf/third_party/abseil-cpp" || ! -f "${SRC_DIR}/protobuf/third_party/abseil-cpp/CMakeLists.txt" ]]; then
  echo "[3.1/7] Initializing protobuf submodules"
  git -C "${SRC_DIR}/protobuf" submodule update --init --recursive
fi

if [[ ! -d "${SRC_DIR}/grpc/.git" ]]; then
  echo "[4/7] Cloning grpc ${GRPC_TAG}"
  git clone --branch "${GRPC_TAG}" --depth 1 https://github.com/grpc/grpc.git "${SRC_DIR}/grpc"
fi

if [[ ! -d "${SRC_DIR}/grpc/third_party/abseil-cpp" || ! -f "${SRC_DIR}/grpc/third_party/abseil-cpp/CMakeLists.txt" ]]; then
  echo "[4.1/7] Initializing grpc submodules"
  git -C "${SRC_DIR}/grpc" submodule update --init --recursive
fi

if [[ ! -x "${HOST_PREFIX}/bin/protoc" ]]; then
  echo "[5/7] Building host protobuf (protoc)"
  cmake -S "${SRC_DIR}/protobuf" -B "${BUILD_DIR}/protobuf-host" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${HOST_PREFIX}" \
    -Dprotobuf_BUILD_TESTS=OFF
  cmake --build "${BUILD_DIR}/protobuf-host" -j"$(nproc)"
  cmake --install "${BUILD_DIR}/protobuf-host"
fi

if [[ ! -x "${HOST_PREFIX}/bin/grpc_cpp_plugin" ]]; then
  echo "[6/7] Building host grpc toolchain (grpc_cpp_plugin)"
  cmake -S "${SRC_DIR}/grpc" -B "${BUILD_DIR}/grpc-host" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${HOST_PREFIX}" \
    -DCMAKE_PREFIX_PATH="${HOST_PREFIX}" \
    -DgRPC_INSTALL=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -DgRPC_ABSL_PROVIDER=module \
    -DgRPC_CARES_PROVIDER=module \
    -DgRPC_RE2_PROVIDER=module \
    -DgRPC_SSL_PROVIDER=module \
    -DgRPC_ZLIB_PROVIDER=module \
    -DgRPC_PROTOBUF_PROVIDER=module
  cmake --build "${BUILD_DIR}/grpc-host" --target protoc grpc_cpp_plugin -j"$(nproc)"
  install -d "${HOST_PREFIX}/bin"
  install -m 755 "${BUILD_DIR}/grpc-host/grpc_cpp_plugin" "${HOST_PREFIX}/bin/grpc_cpp_plugin"
  if [[ -f "${BUILD_DIR}/grpc-host/third_party/protobuf/protoc" ]]; then
    install -m 755 "${BUILD_DIR}/grpc-host/third_party/protobuf/protoc" "${HOST_PREFIX}/bin/protoc"
  fi
fi

if [[ ! -f "${AARCH64_PREFIX}/lib/cmake/grpc/gRPCConfig.cmake" ]]; then
  echo "[7/7] Cross-building aarch64 protobuf + grpc"
  SYSROOT="${TOOLCHAIN_EXTRACT_DIR}/${CROSS_TRIPLE}/libc"
  TOOLCHAIN_FILE="${BUILD_DIR}/cross-toolchain.cmake"
  cat > "${TOOLCHAIN_FILE}" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER ${TOOLCHAIN_EXTRACT_DIR}/bin/${CROSS_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_EXTRACT_DIR}/bin/${CROSS_TRIPLE}-g++)
set(CMAKE_SYSROOT ${SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
EOF

  cmake -S "${SRC_DIR}/protobuf" -B "${BUILD_DIR}/protobuf-aarch64" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${AARCH64_PREFIX}" \
    -DBUILD_SHARED_LIBS=ON \
    -Dprotobuf_BUILD_SHARED_LIBS=ON \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_BUILD_PROTOC_BINARIES=OFF
  cmake --build "${BUILD_DIR}/protobuf-aarch64" -j"$(nproc)"
  cmake --install "${BUILD_DIR}/protobuf-aarch64"

  cmake -S "${SRC_DIR}/grpc" -B "${BUILD_DIR}/grpc-aarch64" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${AARCH64_PREFIX}" \
    -DCMAKE_PREFIX_PATH="${AARCH64_PREFIX}" \
    -DgRPC_INSTALL=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -DgRPC_BUILD_CODEGEN=OFF \
    -DgRPC_ABSL_PROVIDER=module \
    -DgRPC_CARES_PROVIDER=module \
    -DgRPC_RE2_PROVIDER=module \
    -DgRPC_SSL_PROVIDER=module \
    -DgRPC_ZLIB_PROVIDER=module \
    -DgRPC_PROTOBUF_PROVIDER=package \
    -DProtobuf_DIR="${AARCH64_PREFIX}/lib/cmake/protobuf" \
    -DProtobuf_PROTOC_EXECUTABLE="${HOST_PREFIX}/bin/protoc"
  cmake --build "${BUILD_DIR}/grpc-aarch64" -j"$(nproc)"
  cmake --install "${BUILD_DIR}/grpc-aarch64"
fi

cat <<EOF

Bootstrap complete.

Use these exports before cross building project:
export GO2_AARCH64_TOOLCHAIN_ROOT="${TOOLCHAIN_EXTRACT_DIR}"
export GO2_AARCH64_CROSS_TRIPLE="${CROSS_TRIPLE}"
export GO2_AARCH64_SYSROOT="${TOOLCHAIN_EXTRACT_DIR}/${CROSS_TRIPLE}/libc"
export GO2_AARCH64_GRPC_PREFIX="${AARCH64_PREFIX}"
export GO2_HOST_GRPC_TOOLS_PREFIX="${HOST_PREFIX}"
export SSHPASS_BIN="${TOOLS_DIR}/sshpass/bin/sshpass"

Then run:
./scripts/go2_grpc/cross_build_aarch64.sh
EOF
