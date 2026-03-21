#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

: "${GO2_DOCK_HOST:=192.168.51.213}"
: "${GO2_DOCK_USER:=unitree}"
: "${GO2_DOCK_PASSWORD:=123}"
: "${GO2_DOCK_DIR:=/home/unitree/openclaw/go2_grpc}"
: "${GO2_SERVER_BIN:=${ROOT_DIR}/build-aarch64/bin/go2_sport_grpc_server}"
: "${GO2_MODEL_FILE:=}"
: "${GO2_MODEL_OUT_NAME:=}"
: "${GO2_REMOTE_CONVERT_MODEL:=1}"
: "${GO2_REMOTE_YOLO_VENV:=/home/unitree/openclaw/go2_grpc/.venv-yolo-convert}"
: "${GO2_REMOTE_YOLO_INSTALL_APT:=1}"
: "${GO2_REMOTE_YOLO_SUDO:=1}"
: "${GO2_LABEL_FILE:=}"
: "${SSHPASS_BIN:=sshpass}"

if [[ ! -f "${GO2_SERVER_BIN}" ]]; then
  echo "Server binary not found: ${GO2_SERVER_BIN}" >&2
  exit 2
fi

if ! command -v "${SSHPASS_BIN}" >/dev/null 2>&1; then
  echo "sshpass is required for password-based deployment" >&2
  exit 3
fi

SSH=("${SSHPASS_BIN}" -p "${GO2_DOCK_PASSWORD}" ssh -o StrictHostKeyChecking=no)
SCP=("${SSHPASS_BIN}" -p "${GO2_DOCK_PASSWORD}" scp -o StrictHostKeyChecking=no)

"${SSH[@]}" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}" "mkdir -p '${GO2_DOCK_DIR}/bin' '${GO2_DOCK_DIR}/run' '${GO2_DOCK_DIR}/models'"
"${SCP[@]}" "${GO2_SERVER_BIN}" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}/bin/"
"${SCP[@]}" "${ROOT_DIR}/scripts/go2_grpc/remote_server_ctl.sh" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}/"
"${SCP[@]}" "${ROOT_DIR}/scripts/go2_grpc/convert_yolo_to_trt.sh" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}/"

if [[ -n "${GO2_MODEL_FILE}" ]]; then
  if [[ ! -f "${GO2_MODEL_FILE}" ]]; then
    echo "Model file not found: ${GO2_MODEL_FILE}" >&2
    exit 4
  fi
  model_name="$(basename "${GO2_MODEL_FILE}")"
  model_ext="${model_name##*.}"
  model_ext="${model_ext,,}"
  model_base="${model_name%.*}"
  remote_model_src="${GO2_DOCK_DIR}/models/${model_name}"
  "${SCP[@]}" "${GO2_MODEL_FILE}" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${remote_model_src}"

  if [[ -n "${GO2_MODEL_OUT_NAME}" ]]; then
    remote_engine_path="${GO2_DOCK_DIR}/models/${GO2_MODEL_OUT_NAME}"
  else
    remote_engine_path="${GO2_DOCK_DIR}/models/${model_base}.engine"
  fi

  if [[ "${model_ext}" == "pt" || "${model_ext}" == "onnx" ]]; then
    if [[ "${GO2_REMOTE_CONVERT_MODEL}" == "1" ]]; then
      "${SSH[@]}" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}" \
        "chmod +x '${GO2_DOCK_DIR}/convert_yolo_to_trt.sh' && \
         GO2_MODEL_FILE='${remote_model_src}' \
         GO2_MODEL_OUT='${remote_engine_path}' \
         GO2_YOLO_VENV='${GO2_REMOTE_YOLO_VENV}' \
         GO2_YOLO_INSTALL_APT='${GO2_REMOTE_YOLO_INSTALL_APT}' \
         GO2_YOLO_SUDO='${GO2_REMOTE_YOLO_SUDO}' \
         '${GO2_DOCK_DIR}/convert_yolo_to_trt.sh'"
      echo "Remote TensorRT engine: ${remote_engine_path}"
    else
      echo "Remote conversion disabled (GO2_REMOTE_CONVERT_MODEL=0)." >&2
      echo "Uploaded source model: ${remote_model_src}" >&2
      echo "Remember to convert manually on remote host." >&2
    fi
  elif [[ "${model_ext}" == "engine" ]]; then
    echo "Uploaded TensorRT engine directly: ${remote_model_src}"
  else
    echo "Unsupported model extension: .${model_ext}" >&2
    exit 6
  fi
fi

if [[ -n "${GO2_LABEL_FILE}" ]]; then
  if [[ ! -f "${GO2_LABEL_FILE}" ]]; then
    echo "Label file not found: ${GO2_LABEL_FILE}" >&2
    exit 5
  fi
  "${SCP[@]}" "${GO2_LABEL_FILE}" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}/models/"
fi

"${SSH[@]}" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}" "chmod +x '${GO2_DOCK_DIR}/remote_server_ctl.sh' '${GO2_DOCK_DIR}/convert_yolo_to_trt.sh' '${GO2_DOCK_DIR}/bin/go2_sport_grpc_server'"

echo "Deployed go2_sport_grpc_server to ${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}"
