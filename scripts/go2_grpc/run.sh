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
  "GO2_INTERFACE='${GO2_INTERFACE:-eth0}' GO2_GRPC_LISTEN='${GO2_GRPC_LISTEN:-0.0.0.0}' GO2_GRPC_PORT='${GO2_GRPC_PORT:-50051}' GO2_ENABLE_LEASE='${GO2_ENABLE_LEASE:-0}' GO2_MODEL_PATH='${GO2_MODEL_PATH:-}' GO2_HOST_IP='${GO2_HOST_IP:-192.168.123.161}' GO2_WEBRTC_SIGNAL_MODE='${GO2_WEBRTC_SIGNAL_MODE:-local_peer}' GO2_AUDIO_PLAY_BRIDGE_ENABLE='${GO2_AUDIO_PLAY_BRIDGE_ENABLE:-1}' GO2_AUDIO_PLAY_BRIDGE_PYTHON='${GO2_AUDIO_PLAY_BRIDGE_PYTHON:-/home/unitree/workspace/go2_webrtc_connect/.venv_mic310/bin/python}' GO2_AUDIO_PLAY_BRIDGE_PYTHONPATH='${GO2_AUDIO_PLAY_BRIDGE_PYTHONPATH:-/home/unitree/workspace/go2_webrtc_connect}' GO2_AUDIO_PLAY_BRIDGE_SCRIPT='${GO2_AUDIO_PLAY_BRIDGE_SCRIPT:-/home/unitree/openclaw/go2_grpc/scripts/go2_grpc/audio_play_webrtc_sidecar.py}' GO2_AUDIO_PLAY_BRIDGE_GO2_IP='${GO2_AUDIO_PLAY_BRIDGE_GO2_IP:-192.168.123.161}' GO2_MIC_BRIDGE_MODE='${GO2_MIC_BRIDGE_MODE:-udp}' GO2_MIC_BRIDGE_UDP_PORT='${GO2_MIC_BRIDGE_UDP_PORT:-39001}' GO2_MIC_BRIDGE_SIDECAR_ENABLE='${GO2_MIC_BRIDGE_SIDECAR_ENABLE:-1}' GO2_MIC_BRIDGE_GO2_IP='${GO2_MIC_BRIDGE_GO2_IP:-192.168.123.161}' GO2_MIC_BRIDGE_STREAM_ID='${GO2_MIC_BRIDGE_STREAM_ID:-bridge1}' GO2_MIC_BRIDGE_SIDECAR_PYTHON='${GO2_MIC_BRIDGE_SIDECAR_PYTHON:-/home/unitree/workspace/go2_webrtc_connect/.venv_mic310/bin/python}' GO2_MIC_BRIDGE_SIDECAR_PYTHONPATH='${GO2_MIC_BRIDGE_SIDECAR_PYTHONPATH:-/home/unitree/workspace/go2_webrtc_connect}' GO2_MIC_BRIDGE_SIDECAR_SCRIPT='${GO2_MIC_BRIDGE_SIDECAR_SCRIPT:-/home/unitree/openclaw/go2_grpc/scripts/go2_grpc/mic_bridge_webrtc_sidecar.py}' '${GO2_DOCK_DIR}/remote_server_ctl.sh' run"
