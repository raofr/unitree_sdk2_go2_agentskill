#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

: "${GO2_DOCK_HOST:=192.168.123.18}"
: "${GO2_DOCK_USER:=unitree}"
: "${GO2_DOCK_DIR:=/home/unitree/openclaw/go2_grpc}"
: "${GO2_SERVER_BIN:=${ROOT_DIR}/build-aarch64/bin/go2_sport_grpc_server}"
: "${GO2_SKIP_BIN_SYNC:=0}"
: "${GO2_REMOTE_BIN_SOURCE:=/home/unitree/workspace/unitree_sdk2/build-aarch64/bin/go2_sport_grpc_server}"
: "${GO2_SYSTEMD_UNIT_NAME:=go2_sport_grpc.service}"
: "${GO2_SUDO_PASSWORD:=}"

if [[ "${GO2_SKIP_BIN_SYNC}" != "1" ]] && [[ ! -f "${GO2_SERVER_BIN}" ]]; then
  echo "Server binary not found: ${GO2_SERVER_BIN}" >&2
  echo "Hint: set GO2_SKIP_BIN_SYNC=1 to use GO2_REMOTE_BIN_SOURCE on remote host." >&2
  exit 2
fi

if ! command -v rsync >/dev/null 2>&1; then
  echo "rsync is required" >&2
  exit 3
fi

remote_unit_path="/etc/systemd/system/${GO2_SYSTEMD_UNIT_NAME}"

ssh "${GO2_DOCK_USER}@${GO2_DOCK_HOST}" "mkdir -p '${GO2_DOCK_DIR}/bin' '${GO2_DOCK_DIR}/run' '${GO2_DOCK_DIR}/scripts/go2_grpc'"
if [[ "${GO2_SKIP_BIN_SYNC}" == "1" ]]; then
  ssh "${GO2_DOCK_USER}@${GO2_DOCK_HOST}" \
    "cp '${GO2_REMOTE_BIN_SOURCE}' '${GO2_DOCK_DIR}/bin/go2_sport_grpc_server'"
else
  rsync -az "${GO2_SERVER_BIN}" "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}/bin/go2_sport_grpc_server"
fi
rsync -az "${ROOT_DIR}/scripts/go2_grpc/systemd/go2_sport_grpc.env" \
  "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}/run/go2_sport_grpc.env"
rsync -az "${ROOT_DIR}/scripts/go2_grpc/systemd/go2_sport_grpc.service" \
  "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:/tmp/${GO2_SYSTEMD_UNIT_NAME}"
rsync -az "${ROOT_DIR}/scripts/go2_grpc/mic_bridge_sidecar_ctl.sh" \
  "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}/scripts/go2_grpc/mic_bridge_sidecar_ctl.sh"
rsync -az "${ROOT_DIR}/scripts/go2_grpc/mic_bridge_webrtc_sidecar.py" \
  "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}/scripts/go2_grpc/mic_bridge_webrtc_sidecar.py"
rsync -az "${ROOT_DIR}/scripts/go2_grpc/audio_play_webrtc_sidecar.py" \
  "${GO2_DOCK_USER}@${GO2_DOCK_HOST}:${GO2_DOCK_DIR}/scripts/go2_grpc/audio_play_webrtc_sidecar.py"

ssh "${GO2_DOCK_USER}@${GO2_DOCK_HOST}" \
  "set -e; \
   if [[ -n '${GO2_SUDO_PASSWORD}' ]]; then SUDO=\"printf '%s\\n' '${GO2_SUDO_PASSWORD}' | sudo -S\"; else SUDO='sudo'; fi; \
   chmod +x '${GO2_DOCK_DIR}/bin/go2_sport_grpc_server' '${GO2_DOCK_DIR}/scripts/go2_grpc/mic_bridge_sidecar_ctl.sh' '${GO2_DOCK_DIR}/scripts/go2_grpc/audio_play_webrtc_sidecar.py' && \
   eval \"\${SUDO} cp '/tmp/${GO2_SYSTEMD_UNIT_NAME}' '${remote_unit_path}'\" && \
   eval \"\${SUDO} systemctl daemon-reload\" && \
   eval \"\${SUDO} systemctl enable --now '${GO2_SYSTEMD_UNIT_NAME}'\" && \
   eval \"\${SUDO} systemctl status '${GO2_SYSTEMD_UNIT_NAME}' --no-pager -l | sed -n '1,12p'\" && \
   eval \"\${SUDO} systemctl is-enabled '${GO2_SYSTEMD_UNIT_NAME}'\" && \
   eval \"\${SUDO} systemctl is-active '${GO2_SYSTEMD_UNIT_NAME}'\""

echo "Installed and started systemd service: ${GO2_SYSTEMD_UNIT_NAME}"
