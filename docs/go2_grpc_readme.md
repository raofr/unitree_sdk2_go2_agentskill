# GO2 gRPC Service (YOLO + Audio) Build/Deploy Guide

This document covers:
- Local WSL x64 build/test when GO2 is offline.
- GO2 dock host compile/deploy workflow.
- YOLO detection and WebRTC audio upload/play commands for Python and Node clients.

## Service Scope

The single `go2_sport_grpc_server` now includes:
- Motion/session RPCs (existing).
- YOLO detection RPCs: `DetectObjects`, `StartDetection`, `StopDetection`, `SubscribeDetections`.
- Audio RPCs: `UploadAndPlayAudio`, `GetAudioStatus`, `StopAudioPlayback`.

## Network Notes

- GO2 dock host internal IP (audio peer target default): `192.168.123.161`
- Service host (wifi-facing, common): `192.168.51.213:50051`

Quick wifi NIC check on dock host:

```bash
ssh unitree@192.168.51.213 "ip -o -4 addr show | grep 192.168.51. || true"
```

## 1) Local WSL x64 Build/Test (apt-first)

Install toolchain and runtime dependencies:

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config \
  libprotobuf-dev protobuf-compiler \
  libgrpc++-dev libgrpc-dev protobuf-compiler-grpc \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libgstreamer-plugins-bad1.0-dev \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-bad
```

Build:

```bash
./scripts/go2_grpc/local_build_x64.sh
```

Convert YOLO `.pt/.onnx` to TensorRT `.engine` on local WSL (uses `uv + venv`, and runs a quick cat image verify):

```bash
GO2_LOCAL_MODEL_FILE=models/yolo26/yolo26s.pt \
GO2_LOCAL_ENGINE_OUT=models/yolo26/yolo26s.engine \
GO2_LOCAL_VERIFY_IMAGE=test/cat.jpg \
./scripts/go2_grpc/local_convert_yolo_x64.sh
```

Notes:
- Script auto-installs `python3-venv` and `uv` when missing.
- TensorRT conversion still requires `trtexec` on local machine.

Run unit tests:

```bash
./scripts/go2_grpc/local_test_x64.sh
```

Expected tests include:
- `go2_grpc_proto_smoke_test` (proto messages for motion/detection/audio)
- `go2_grpc_audio_mock_test` (offline audio queue + silence keepalive scheduling)

## 2) GO2 Dock Host Build/Deploy

### 2.0 apt dependencies on GO2 dock/build host

Install build/runtime dependencies on `aarch64` host first:

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config \
  libprotobuf-dev protobuf-compiler \
  libgrpc++-dev libgrpc-dev protobuf-compiler-grpc \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libgstreamer-plugins-bad1.0-dev \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
  gstreamer1.0-tools
```

Quick verify:

```bash
protoc --version
grpc_cpp_plugin --version || true
pkg-config --modversion gstreamer-webrtc-1.0
```

### 2.1 Build on dock/build host

```bash
ssh unitree@192.168.123.18 "cd /home/unitree/workspace/unitree_sdk2 && \
  cmake -S . -B build-go2-grpc -DBUILD_EXAMPLES=OFF -DBUILD_GO2_GRPC=ON -DCMAKE_BUILD_TYPE=Release && \
  cmake --build build-go2-grpc -j4 --target go2_sport_grpc_server"
```

### 2.2 Pull prebuilt binary to local workspace (optional)

```bash
mkdir -p artifacts/go2_grpc
scp unitree@192.168.123.18:/home/unitree/workspace/unitree_sdk2/build-go2-grpc/bin/go2_sport_grpc_server \
  artifacts/go2_grpc/go2_sport_grpc_server.aarch64
file artifacts/go2_grpc/go2_sport_grpc_server.aarch64
```

Expected architecture: `ARM aarch64`.

### 2.3 Deploy binary/model/scripts to service host

```bash
GO2_SERVER_BIN=build-aarch64/bin/go2_sport_grpc_server \
GO2_MODEL_FILE=/path/to/yolo26s.pt \
./scripts/go2_grpc/deploy.sh
```

Remote convert behavior:
- If `GO2_MODEL_FILE` is `.pt` or `.onnx`, deploy script uploads source model and converts to `${GO2_DOCK_DIR}/models/<name>.engine` on remote host.
- If `GO2_MODEL_FILE` is `.engine`, deploy script uploads it directly.
- Remote convert also uses `uv + venv` and can auto-install dependencies.

Useful env knobs:

```bash
GO2_REMOTE_CONVERT_MODEL=1             # default: 1
GO2_REMOTE_YOLO_VENV=/home/unitree/openclaw/go2_grpc/.venv-yolo-convert
GO2_REMOTE_YOLO_INSTALL_APT=1          # default: 1
GO2_REMOTE_YOLO_SUDO=1                 # default: 1
GO2_MODEL_OUT_NAME=yolo26s.engine      # optional custom engine name
```

Remote target defaults:
- Host: `192.168.51.213`
- Dir: `/home/unitree/openclaw/go2_grpc`

### 2.4 Start/stop/status

Start:

```bash
GO2_INTERFACE=eth0 \
GO2_GRPC_LISTEN=0.0.0.0 \
GO2_GRPC_PORT=50051 \
GO2_MODEL_PATH=/home/unitree/openclaw/go2_grpc/models/yolo26.engine \
GO2_HOST_IP=192.168.123.161 \
./scripts/go2_grpc/run.sh
```

Stop:

```bash
./scripts/go2_grpc/kill.sh
```

Status:

```bash
./scripts/go2_grpc/status.sh
```

### 2.5 Run as systemd service (auto start)

Install/update service on remote host:

```bash
# If local has aarch64 binary:
GO2_DOCK_HOST=192.168.123.18 \
GO2_DOCK_USER=unitree \
GO2_DOCK_DIR=/home/unitree/openclaw/go2_grpc \
GO2_SERVER_BIN=/path/to/go2_sport_grpc_server \
GO2_SUDO_PASSWORD=123 \
./scripts/go2_grpc/install_systemd_service.sh

# If binary is already built on remote host:
GO2_DOCK_HOST=192.168.123.18 \
GO2_DOCK_USER=unitree \
GO2_DOCK_DIR=/home/unitree/openclaw/go2_grpc \
GO2_SKIP_BIN_SYNC=1 \
GO2_REMOTE_BIN_SOURCE=/home/unitree/workspace/unitree_sdk2/build-aarch64/bin/go2_sport_grpc_server \
GO2_SUDO_PASSWORD=123 \
./scripts/go2_grpc/install_systemd_service.sh
```

Service env file (edit runtime params/model path/debug dump switch):

```bash
/home/unitree/openclaw/go2_grpc/run/go2_sport_grpc.env
```

Camera frame dump control:
- `GO2_DETECT_DEBUG_DIR=` (empty) => disabled (recommended default).
- `GO2_DETECT_DEBUG_DIR=/some/path` => enabled (for troubleshooting only).

Microphone bridge sidecar (optional fallback when C++ webrtcbin cannot receive mic):
- `GO2_MIC_BRIDGE_MODE=udp`
- `GO2_MIC_BRIDGE_UDP_PORT=39001`
- `GO2_MIC_BRIDGE_SIDECAR_ENABLE=1`
- `GO2_MIC_BRIDGE_GO2_IP=192.168.123.161`
- `GO2_MIC_BRIDGE_STREAM_ID=bridge1`
- `GO2_MIC_BRIDGE_SIDECAR_PYTHON=/home/unitree/workspace/go2_webrtc_connect/.venv_mic310/bin/python`
- `GO2_MIC_BRIDGE_SIDECAR_PYTHONPATH=/home/unitree/workspace/go2_webrtc_connect`
- `GO2_MIC_BRIDGE_SIDECAR_SCRIPT=/home/unitree/openclaw/go2_grpc/scripts/go2_grpc/mic_bridge_webrtc_sidecar.py`

With these set, `go2_sport_grpc.service` starts/stops sidecar automatically.

Detection timing log (for performance measurement):
- `GO2_DETECT_TIMING_LOG=` (empty) => disabled (recommended default).
- `GO2_DETECT_TIMING_LOG=/some/path/timing.csv` => enabled, logs per-frame timing.

Timing log format (CSV):
```
timestamp_ms,frame_id,stream_id,fps_limit,frame_skip,fetch_ms,infer_ms,total_ms,detections
```
- `fetch_ms`: time to fetch image from robot camera
- `infer_ms`: time for TensorRT GPU inference
- `total_ms`: total loop iteration time

After env change:

```bash
ssh unitree@192.168.123.18 "sudo systemctl restart go2_sport_grpc.service"
```

Check service:

```bash
ssh unitree@192.168.123.18 "sudo systemctl status go2_sport_grpc.service --no-pager -l"
ssh unitree@192.168.123.18 "sudo systemctl is-enabled go2_sport_grpc.service && sudo systemctl is-active go2_sport_grpc.service"
```

Stop service:

```bash
ssh unitree@192.168.123.18 "sudo systemctl stop go2_sport_grpc.service"
```

Disable boot auto-start (but can still be started manually):

```bash
ssh unitree@192.168.123.18 "sudo systemctl disable go2_sport_grpc.service"
```

Disable auto restart on crash (`Restart=always` -> `Restart=no`):

```bash
ssh unitree@192.168.123.18 "sudo systemctl edit go2_sport_grpc.service"
```

Add:

```ini
[Service]
Restart=no
```

Then apply:

```bash
ssh unitree@192.168.123.18 "sudo systemctl daemon-reload && sudo systemctl restart go2_sport_grpc.service"
```

Restore default auto restart:

```bash
ssh unitree@192.168.123.18 "sudo rm -f /etc/systemd/system/go2_sport_grpc.service.d/override.conf && sudo systemctl daemon-reload && sudo systemctl restart go2_sport_grpc.service"
```

Completely prevent startup (manual/auto):

```bash
ssh unitree@192.168.123.18 "sudo systemctl mask go2_sport_grpc.service"
```

Unmask:

```bash
ssh unitree@192.168.123.18 "sudo systemctl unmask go2_sport_grpc.service"
```

## 3) YOLO Detection Usage

### Python (`python/go2_grpc_tool`)

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 open-session --owner openclaw --session-name yolo
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 detect-once --session-id <id> --model-path /home/unitree/openclaw/go2_grpc/models/yolo26.engine
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 detect-start --session-id <id> --stream-id yolo-main --frame-skip 1 --fps-limit 10
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 detect-subscribe --session-id <id> --stream-id yolo-main
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 detect-stop --session-id <id> --stream-id yolo-main
```

### Node (`go2_agent_tool/node`)

```bash
cd go2_agent_tool/node
node src/cli.js --endpoint 192.168.51.213:50051 detect-once --session-id <id> --model-path /home/unitree/openclaw/go2_grpc/models/yolo26.engine
node src/cli.js --endpoint 192.168.51.213:50051 detect-start --session-id <id> --stream-id yolo-main --frame-skip 1 --fps-limit 10
node src/cli.js --endpoint 192.168.51.213:50051 detect-subscribe --session-id <id> --stream-id yolo-main
node src/cli.js --endpoint 192.168.51.213:50051 detect-stop --session-id <id> --stream-id yolo-main
```

## 4) Audio Upload/Play Usage (WebRTC path)

`AudioWebRtcManager` now includes webrtcbin negotiation/ICE signaling hooks.
To integrate with your actual GO2 signaling service, set these env vars on server start:

```bash
export GO2_WEBRTC_SIGNAL_OFFER_CMD="/path/to/signal_hook.sh"
export GO2_WEBRTC_SIGNAL_ICE_CMD="/path/to/signal_hook.sh"
export GO2_WEBRTC_SIGNAL_ANSWER_CMD="/path/to/signal_hook.sh"
```

Hook contract:
- Offer hook: `signal_hook.sh offer <go2_host_ip> <payload_file>`
- ICE hook: `signal_hook.sh ice <go2_host_ip> <payload_file>`
- Answer hook: `signal_hook.sh answer <go2_host_ip> <payload_file>`

`payload_file` contains raw SDP text for `offer`, and `mline_index + candidate` for `ice`.
For `answer`, hook should print SDP answer to stdout.

Signaling mode selection (`GO2_WEBRTC_SIGNAL_MODE`):
- `auto` (default): first try LocalSTA direct peer request (`http://<GO2_HOST_IP>:8081/offer`), fallback to hook.
- `local_peer`: only LocalSTA direct peer request, no hook fallback.
- `hook`: always use hook for offer/ice/answer.

The LocalSTA direct request is aligned with `go2_webrtc_connect` core local path:
- POST `http://<ip>:8081/offer`
- JSON body: `{"id":"STA_localNetwork","sdp":"<offer>","type":"offer","token":""}`
- Response JSON should contain `sdp` and `type` (or `sdp=reject` when occupied).

### 4.1 Recommended signaling hook skeleton

Example shell hook (`signal_hook.sh`) behavior:

```bash
#!/usr/bin/env bash
set -euo pipefail

event="$1"          # offer | ice | answer
go2_host_ip="$2"    # e.g. 192.168.123.161
payload_file="$3"   # input payload from server

case "${event}" in
  offer)
    # Read local SDP offer, send to your signaling service, and print answer SDP.
    # cat "${payload_file}" | curl ... > /tmp/answer.sdp
    # cat /tmp/answer.sdp
    ;;
  ice)
    # Forward candidate to signaling service.
    # cat "${payload_file}" | curl ...
    ;;
  answer)
    # Optional fallback pull mode: print latest answer SDP to stdout.
    # curl ...
    ;;
  *)
    echo "unknown event: ${event}" >&2
    exit 2
    ;;
esac
```

### 4.2 Start server with signaling hooks

```bash
export GO2_WEBRTC_SIGNAL_OFFER_CMD="/home/unitree/openclaw/go2_grpc/signal_hook.sh"
export GO2_WEBRTC_SIGNAL_ICE_CMD="/home/unitree/openclaw/go2_grpc/signal_hook.sh"
export GO2_WEBRTC_SIGNAL_ANSWER_CMD="/home/unitree/openclaw/go2_grpc/signal_hook.sh"
export GO2_WEBRTC_SIGNAL_MODE="auto"

GO2_INTERFACE=eth0 \
GO2_GRPC_LISTEN=0.0.0.0 \
GO2_GRPC_PORT=50051 \
GO2_MODEL_PATH=/home/unitree/openclaw/go2_grpc/models/yolo26.engine \
GO2_HOST_IP=192.168.123.161 \
./scripts/go2_grpc/run.sh
```

### Python

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 audio-upload-play --session-id <id> --stream-id audio-main --file /tmp/beep.opus --mime audio/opus --sample-rate 48000 --channels 1 --volume 1.0
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 audio-status --session-id <id>
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 audio-stop --session-id <id> --stream-id audio-main
```

### Node

```bash
cd go2_agent_tool/node
node src/cli.js --endpoint 192.168.51.213:50051 audio-upload-play --session-id <id> --stream-id audio-main --file /tmp/beep.opus --mime audio/opus --sample-rate 48000 --channels 1 --volume 1.0
node src/cli.js --endpoint 192.168.51.213:50051 audio-status --session-id <id>
node src/cli.js --endpoint 192.168.51.213:50051 audio-stop --session-id <id> --stream-id audio-main
```

### 4.3 Microphone bridge fallback (Python WebRTC -> C++ gRPC)

Use this when C++ `webrtcbin` receives no microphone packets but Python `go2_webrtc_connect` works.

1) Start C++ server with UDP microphone bridge enabled:

```bash
export GO2_MIC_BRIDGE_MODE=udp
export GO2_MIC_BRIDGE_UDP_PORT=39001
```

2) Prepare Python runtime on dock host (wheel-only install, no source compile):

```bash
cd /home/unitree/workspace/go2_webrtc_connect
/home/unitree/.local/bin/uv python install 3.10
/home/unitree/.local/bin/uv venv --python 3.10 .venv_mic310
. .venv_mic310/bin/activate
UV_INDEX_URL=https://pypi.org/simple /home/unitree/.local/bin/uv pip install --only-binary=:all: aiortc requests pycryptodome packaging numpy lz4 wasmtime
```

3) Start sidecar sender (robot microphone -> localhost UDP bridge):

```bash
cd /home/unitree/workspace/go2_webrtc_connect
. .venv_mic310/bin/activate
PYTHONPATH=/home/unitree/workspace/go2_webrtc_connect \
python /home/unitree/workspace/unitree_sdk2/scripts/go2_grpc/mic_bridge_webrtc_sidecar.py \
  --go2-ip 192.168.123.161 \
  --stream-id bridge1 \
  --udp-host 127.0.0.1 \
  --udp-port 39001
```

4) Subscribe from Node client as usual:

```bash
cd go2_agent_tool/node
node src/cli.js --endpoint 192.168.51.213:50051 mic-start --session-id <id> --stream-id bridge1 --sample-rate 48000 --channels 2
node src/cli.js --endpoint 192.168.51.213:50051 mic-subscribe --session-id <id> --stream-id bridge1
```

Bridge packet format (UDP payload):
- byte `0`: version (currently `1`)
- byte `1`: stream_id length
- bytes `2..9`: timestamp_ms (`uint64`, little-endian)
- bytes `10..`: `stream_id` + raw audio bytes

## 5) Offline Validation Notes

When GO2 is unreachable:
- Build and `ctest` still validate protobuf contracts and local queue/keepalive logic.
- Audio status may report unavailable/disconnected if `gstreamer-webrtc` runtime path is not fully connected to robot peer.

This is expected before remote GO2 WebRTC signaling is online.

## 6) Online Integration Debug Checklist

When GO2 is back online, use this order:

1. Confirm service process and port:

```bash
./scripts/go2_grpc/status.sh
```

2. Confirm GStreamer webrtc dev/runtime installed:

```bash
pkg-config --modversion gstreamer-webrtc-1.0
gst-inspect-1.0 webrtcbin | rg "Factory Details|Plugin Details"
```

3. Check server log for signaling and state transitions:

```bash
ssh unitree@192.168.51.213 "tail -n 200 /home/unitree/openclaw/go2_grpc/run/go2_sport_grpc_server.log"
```

4. Validate audio RPC path from client:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 audio-status --session-id <id>
```

5. Upload a tiny opus clip first, then check status:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 audio-upload-play --session-id <id> --stream-id audio-main --file /tmp/beep.opus --mime audio/opus --sample-rate 48000 --channels 1
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 audio-status --session-id <id>
```

If `connected=false`:
- Verify `GO2_WEBRTC_SIGNAL_*` commands are exported in the same environment as server startup.
- Run signaling hook manually to ensure it returns answer SDP.
- Check that hook can reach your signaling endpoint and that GO2 host ip (`192.168.123.161`) is correct.

If `accepted=false` on upload:
- Confirm `audio_bytes` is non-empty and `mime` matches payload codec.
- Check server log for `offer hook failed` or `failed to parse answer SDP`.
