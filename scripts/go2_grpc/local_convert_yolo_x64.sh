#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

: "${GO2_LOCAL_MODEL_FILE:=${ROOT_DIR}/models/yolo26/yolo26s.pt}"
: "${GO2_LOCAL_ENGINE_OUT:=${ROOT_DIR}/models/yolo26/yolo26s.engine}"
: "${GO2_LOCAL_VERIFY_IMAGE:=${ROOT_DIR}/test/cat.jpg}"
: "${GO2_LOCAL_YOLO_VENV:=${ROOT_DIR}/.venv-yolo-local}"
: "${GO2_LOCAL_INSTALL_APT:=1}"

if [[ ! -f "${GO2_LOCAL_MODEL_FILE}" ]]; then
  echo "Model file not found: ${GO2_LOCAL_MODEL_FILE}" >&2
  exit 2
fi

if [[ ! -f "${GO2_LOCAL_VERIFY_IMAGE}" ]]; then
  echo "Verify image not found: ${GO2_LOCAL_VERIFY_IMAGE}" >&2
  exit 3
fi

GO2_MODEL_FILE="${GO2_LOCAL_MODEL_FILE}" \
GO2_MODEL_OUT="${GO2_LOCAL_ENGINE_OUT}" \
GO2_YOLO_VENV="${GO2_LOCAL_YOLO_VENV}" \
GO2_YOLO_INSTALL_APT="${GO2_LOCAL_INSTALL_APT}" \
GO2_YOLO_SUDO=1 \
GO2_YOLO_VERIFY_IMAGE="${GO2_LOCAL_VERIFY_IMAGE}" \
"${ROOT_DIR}/scripts/go2_grpc/convert_yolo_to_trt.sh"

echo "Local model convert+verify done."
echo "Engine: ${GO2_LOCAL_ENGINE_OUT}"
