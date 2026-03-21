#!/usr/bin/env bash
set -euo pipefail

: "${GO2_DOCK_HOST:=192.168.51.213}"
: "${GO2_DOCK_USER:=unitree}"
: "${GO2_DOCK_PASSWORD:=123}"
: "${GO2_DOCK_DIR:=/home/unitree/openclaw/go2_grpc}"
: "${SSHPASS_BIN:=sshpass}"

if ! command -v "${SSHPASS_BIN}" >/dev/null 2>&1; then
  echo "sshpass is required" >&2
  exit 3
fi

"${SSHPASS_BIN}" -p "${GO2_DOCK_PASSWORD}" ssh -o StrictHostKeyChecking=no \
  "${GO2_DOCK_USER}@${GO2_DOCK_HOST}" \
  "GO2_INTERFACE='${GO2_INTERFACE:-eth0}' GO2_GRPC_LISTEN='${GO2_GRPC_LISTEN:-0.0.0.0}' GO2_GRPC_PORT='${GO2_GRPC_PORT:-50051}' GO2_ENABLE_LEASE='${GO2_ENABLE_LEASE:-0}' GO2_MODEL_PATH='${GO2_MODEL_PATH:-}' GO2_HOST_IP='${GO2_HOST_IP:-192.168.123.161}' '${GO2_DOCK_DIR}/remote_server_ctl.sh' run"
