#!/usr/bin/env bash
set -euo pipefail

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUN_DIR="${BASE_DIR}/run"
PID_FILE="${RUN_DIR}/go2_mic_bridge_sidecar.pid"
LOG_FILE="${RUN_DIR}/go2_mic_bridge_sidecar.log"
ENV_FILE="${RUN_DIR}/go2_sport_grpc.env"

if [[ -f "${ENV_FILE}" ]]; then
  set -a
  # shellcheck disable=SC1090
  . "${ENV_FILE}"
  set +a
fi

: "${GO2_MIC_BRIDGE_MODE:=}"
: "${GO2_MIC_BRIDGE_SIDECAR_ENABLE:=0}"
: "${GO2_MIC_BRIDGE_UDP_PORT:=39001}"
: "${GO2_MIC_BRIDGE_GO2_IP:=192.168.123.161}"
: "${GO2_MIC_BRIDGE_STREAM_ID:=bridge1}"
: "${GO2_MIC_BRIDGE_SIDECAR_PYTHON:=/home/unitree/workspace/go2_webrtc_connect/.venv_mic310/bin/python}"
: "${GO2_MIC_BRIDGE_SIDECAR_PYTHONPATH:=/home/unitree/workspace/go2_webrtc_connect}"
: "${GO2_MIC_BRIDGE_SIDECAR_SCRIPT:=/home/unitree/openclaw/go2_grpc/scripts/go2_grpc/mic_bridge_webrtc_sidecar.py}"

mkdir -p "${RUN_DIR}"

is_running() {
  [[ -f "${PID_FILE}" ]] || return 1
  local pid
  pid="$(cat "${PID_FILE}")"
  [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null
}

is_enabled() {
  [[ "${GO2_MIC_BRIDGE_MODE}" == "udp" ]] && [[ "${GO2_MIC_BRIDGE_SIDECAR_ENABLE}" == "1" ]]
}

case "${1:-}" in
  run)
    if ! is_enabled; then
      echo "mic sidecar disabled"
      exit 0
    fi
    if is_running; then
      echo "mic sidecar already running pid=$(cat "${PID_FILE}")"
      exit 0
    fi
    if [[ ! -x "${GO2_MIC_BRIDGE_SIDECAR_PYTHON}" ]]; then
      echo "mic sidecar python missing: ${GO2_MIC_BRIDGE_SIDECAR_PYTHON}" >&2
      exit 2
    fi
    if [[ ! -f "${GO2_MIC_BRIDGE_SIDECAR_SCRIPT}" ]]; then
      echo "mic sidecar script missing: ${GO2_MIC_BRIDGE_SIDECAR_SCRIPT}" >&2
      exit 2
    fi
    nohup env PYTHONPATH="${GO2_MIC_BRIDGE_SIDECAR_PYTHONPATH}" \
      "${GO2_MIC_BRIDGE_SIDECAR_PYTHON}" "${GO2_MIC_BRIDGE_SIDECAR_SCRIPT}" \
      --go2-ip "${GO2_MIC_BRIDGE_GO2_IP}" \
      --stream-id "${GO2_MIC_BRIDGE_STREAM_ID}" \
      --udp-host 127.0.0.1 \
      --udp-port "${GO2_MIC_BRIDGE_UDP_PORT}" >"${LOG_FILE}" 2>&1 &
    echo $! > "${PID_FILE}"
    echo "mic sidecar started pid=$(cat "${PID_FILE}")"
    ;;
  kill)
    if is_running; then
      kill "$(cat "${PID_FILE}")" || true
      rm -f "${PID_FILE}"
      echo "mic sidecar stopped"
    else
      rm -f "${PID_FILE}"
      echo "mic sidecar not running"
    fi
    ;;
  status)
    if is_running; then
      echo "mic sidecar running pid=$(cat "${PID_FILE}")"
    else
      echo "mic sidecar stopped"
      exit 1
    fi
    ;;
  *)
    echo "usage: $0 {run|kill|status}" >&2
    exit 2
    ;;
esac
