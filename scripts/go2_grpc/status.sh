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
  "'${GO2_DOCK_DIR}/remote_server_ctl.sh' status"
