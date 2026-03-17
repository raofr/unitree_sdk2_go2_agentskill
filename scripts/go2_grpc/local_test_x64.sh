#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-local-x64"

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "Build directory not found: ${BUILD_DIR}" >&2
  echo "Run ./scripts/go2_grpc/local_build_x64.sh first." >&2
  exit 2
fi

ctest --test-dir "${BUILD_DIR}" --output-on-failure "$@"
