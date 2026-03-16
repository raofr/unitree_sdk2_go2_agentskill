#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

: "${GO2_DOCK_HOST:=192.168.51.213}"
: "${GO2_DOCK_USER:=unitree}"
: "${GO2_DOCK_PASSWORD:=123}"
: "${GO2_DOCK_DIR:=/home/unitree/openclaw/go2_grpc}"
: "${GO2_SERVER_BIN:=${ROOT_DIR}/build-aarch64/bin/go2_sport_grpc_server}"
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

"${SSH[@]}" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}" "mkdir -p '${GO2_DOCK_DIR}/bin' '${GO2_DOCK_DIR}/run'"
"${SCP[@]}" "${GO2_SERVER_BIN}" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}/bin/"
"${SCP[@]}" "${ROOT_DIR}/scripts/go2_grpc/remote_server_ctl.sh" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}/"

"${SSH[@]}" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}" "chmod +x '${GO2_DOCK_DIR}/remote_server_ctl.sh' '${GO2_DOCK_DIR}/bin/go2_sport_grpc_server'"

echo "Deployed go2_sport_grpc_server to ${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}"
