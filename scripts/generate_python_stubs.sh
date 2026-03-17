#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROTO_FILE="${ROOT_DIR}/proto/go2_sport.proto"
OUT_DIR="${ROOT_DIR}/python/go2_grpc_tool/generated"
PYTHON_BIN="${PYTHON_BIN:-python3}"

if ! "${PYTHON_BIN}" -c "import grpc_tools.protoc" >/dev/null 2>&1; then
  echo "grpc_tools is missing for ${PYTHON_BIN}; install grpcio-tools first" >&2
  exit 4
fi

"${PYTHON_BIN}" -m grpc_tools.protoc \
  -I"${ROOT_DIR}/proto" \
  --python_out="${OUT_DIR}" \
  --grpc_python_out="${OUT_DIR}" \
  "${PROTO_FILE}"

# keep generated files importable as package modules
if [[ -f "${OUT_DIR}/go2_sport_pb2_grpc.py" ]]; then
  sed -i 's/^import go2_sport_pb2 as go2__sport__pb2/from . import go2_sport_pb2 as go2__sport__pb2/' "${OUT_DIR}/go2_sport_pb2_grpc.py"
fi

echo "Generated Python stubs at ${OUT_DIR}"
