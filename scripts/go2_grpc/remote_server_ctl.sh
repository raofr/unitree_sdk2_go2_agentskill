#!/usr/bin/env bash
set -euo pipefail

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${BASE_DIR}/bin/go2_sport_grpc_server"
RUN_DIR="${BASE_DIR}/run"
PID_FILE="${RUN_DIR}/go2_sport_grpc_server.pid"
LOG_FILE="${RUN_DIR}/go2_sport_grpc_server.log"
SIDECAR_CTL="${BASE_DIR}/mic_bridge_sidecar_ctl.sh"

: "${GO2_INTERFACE:=eth0}"
: "${GO2_GRPC_LISTEN:=0.0.0.0}"
: "${GO2_GRPC_PORT:=50051}"
: "${GO2_ENABLE_LEASE:=0}"
: "${GO2_MODEL_PATH:=}"
: "${GO2_HOST_IP:=192.168.123.161}"

mkdir -p "${RUN_DIR}"

is_running() {
  [[ -f "${PID_FILE}" ]] || return 1
  local pid
  pid="$(cat "${PID_FILE}")"
  [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null
}

case "${1:-}" in
  run)
    if is_running; then
      echo "already running pid=$(cat "${PID_FILE}")"
      exit 0
    fi
    nohup "${BIN}" "${GO2_INTERFACE}" "${GO2_GRPC_LISTEN}" "${GO2_GRPC_PORT}" "${GO2_ENABLE_LEASE}" "${GO2_MODEL_PATH}" "${GO2_HOST_IP}" >"${LOG_FILE}" 2>&1 &
    echo $! > "${PID_FILE}"
    if [[ -x "${SIDECAR_CTL}" ]]; then
      sleep 1
      "${SIDECAR_CTL}" run || true
    fi
    echo "started pid=$(cat "${PID_FILE}")"
    ;;
  kill)
    if [[ -x "${SIDECAR_CTL}" ]]; then
      "${SIDECAR_CTL}" kill || true
    fi
    if is_running; then
      kill "$(cat "${PID_FILE}")"
      rm -f "${PID_FILE}"
      echo "stopped"
    else
      rm -f "${PID_FILE}"
      echo "not running"
    fi
    ;;
  status)
    if is_running; then
      echo "running pid=$(cat "${PID_FILE}")"
      if [[ -x "${SIDECAR_CTL}" ]]; then
        "${SIDECAR_CTL}" status || true
      fi
    else
      echo "stopped"
      exit 1
    fi
    ;;
  *)
    echo "usage: $0 {run|kill|status}" >&2
    exit 2
    ;;
esac
