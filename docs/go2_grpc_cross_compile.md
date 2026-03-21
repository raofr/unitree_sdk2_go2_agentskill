# GO2 gRPC Cross-Compile and GO2 Dock Deploy Guide

This document focuses on GO2 dock deployment, including model conversion for GO2 aarch64 runtime.

Default target used below:

- Dock host: `unitree@192.168.123.18`
- Repo dir on dock: `/home/unitree/workspace/unitree_sdk2`

## 1) Cross-build server binary (optional)

On x86_64 host:

```bash
./scripts/go2_grpc/cross_build_aarch64.sh
```

Output:

- `build-aarch64/bin/go2_sport_grpc_server`

If you already build directly on dock host, you can skip cross-build.

## 2) Sync code to GO2 dock host

```bash
rsync -az --delete \
  --exclude '.git/' \
  --exclude 'build/' --exclude 'build-*/' --exclude 'cmake-build-*/' \
  --exclude 'thirdparty/grpc_local/' --exclude 'thirdparty/grpc_cross/' \
  --exclude '.venv*/' \
  /home/raofr/unitree_sdk2/ \
  unitree@192.168.123.18:/home/unitree/workspace/unitree_sdk2/
```

## 3) Build server on GO2 dock host

```bash
ssh unitree@192.168.123.18 "cd /home/unitree/workspace/unitree_sdk2 && \
  cmake -S . -B build-aarch64 \
    -DBUILD_GO2_GRPC=ON -DBUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS='-I/usr/local/cuda/include -DGO2_HAS_TENSORRT_LINK=1' && \
  cmake --build build-aarch64 --target go2_sport_grpc_server -j\$(nproc)"
```

## 4) Deploy GO2 model for dock runtime

### 4.1 Copy model source files to dock

Recommended: copy both `.pt` (archive/source) and `.onnx` (for binary conversion):

```bash
rsync -az models/yolo26/yolo26s.pt \
  unitree@192.168.123.18:/home/unitree/workspace/unitree_sdk2/models/yolo26/yolo26s.pt

rsync -az models/yolo26/yolo26s.onnx \
  unitree@192.168.123.18:/home/unitree/workspace/unitree_sdk2/models/yolo26/yolo26s.onnx
```

### 4.2 Convert to TensorRT engine on dock (binary-only path)

No Python conversion required in this path:

```bash
ssh unitree@192.168.123.18 "mkdir -p /home/unitree/workspace/unitree_sdk2/models/yolo26/aarch64 && \
  /usr/src/tensorrt/bin/trtexec \
    --onnx=/home/unitree/workspace/unitree_sdk2/models/yolo26/yolo26s.onnx \
    --saveEngine=/home/unitree/workspace/unitree_sdk2/models/yolo26/aarch64/yolo26s.engine \
    --fp16 --buildOnly"
```

Expected final line:

- `PASSED TensorRT.trtexec`

### 4.3 Verify engine exists

```bash
ssh unitree@192.168.123.18 "ls -lh /home/unitree/workspace/unitree_sdk2/models/yolo26/aarch64/yolo26s.engine"
```

## 5) Start service with GO2 dock model

If using systemd service:

```bash
ssh unitree@192.168.123.18 "printf '123\n' | sudo -S systemctl restart go2_sport_grpc.service && \
  printf '123\n' | sudo -S systemctl is-active go2_sport_grpc.service"
```

Model path in service env:

- `/home/unitree/workspace/unitree_sdk2/models/yolo26/aarch64/yolo26s.engine`

If using script control:

```bash
GO2_DOCK_HOST=192.168.123.18 \
GO2_MODEL_PATH=/home/unitree/workspace/unitree_sdk2/models/yolo26/aarch64/yolo26s.engine \
GO2_HOST_IP=192.168.123.161 \
./scripts/go2_grpc/run.sh
```

## 6) Quick detect-once smoke test

```bash
cd go2_agent_tool/node
OPEN_JSON=$(node src/cli.js --endpoint 192.168.123.18:50051 open-session --owner openclaw --ttl-sec 120)
SID=$(printf '%s' "$OPEN_JSON" | python3 -c 'import sys,json; print(json.load(sys.stdin)["session_id"])')
node src/cli.js --endpoint 192.168.123.18:50051 detect-once \
  --session-id "$SID" \
  --model-path /home/unitree/workspace/unitree_sdk2/models/yolo26/aarch64/yolo26s.engine \
  --conf-thres 0.1 --iou-thres 0.45 --max-det 20
node src/cli.js --endpoint 192.168.123.18:50051 close-session --session-id "$SID"
```
