# GO2 gRPC Skill

## Scope

Use the GO2 gRPC service through Python tooling only. Do not call robot action binaries directly in normal workflow.

## Required Rule

For each new task, clean previous owner sessions before issuing a new action unless user explicitly asks for parallel execution.

- Default mode (non-parallel): run force-close-owner first.
- Parallel mode: keep existing sessions and create a named parallel session.

## Service Endpoint

- Default endpoint: 192.168.51.213:50051
- Transport: insecure gRPC over internal LAN (phase 1)

## Network Requirement (Important)

To run service on `192.168.51.213`, the dock host must have an external USB Wi-Fi adapter connected and online.

- Build/control network (stable): `192.168.123.18` (usually wired)
- Service/control network (robot side): `192.168.51.213` (usually external Wi-Fi NIC)

Quick check on dock host:

- `ip -o -4 addr show | grep 192.168.51.` must return at least one interface.
- If no output, connect and enable the external Wi-Fi adapter first.

## Python Tool Commands

- Open session:
  python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 open-session --owner openclaw --session-name default

- Close session:
  python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 close-session --session-id <id>

- Action call:
  python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_STAND_UP

- Status:
  python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 status

## Common Action Examples

- Move:
  python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_MOVE --vx 0.2 --vy 0.0 --vyaw 0.1

- Euler:
  python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_EULER --roll 0.0 --pitch 0.1 --yaw 0.0

- Bool flag action:
  python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_HAND_STAND --flag

## YOLO26 Detection Commands

- Detect once:
  PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 detect-once --session-id <id> --model-path /home/unitree/openclaw/go2_grpc/models/yolo26.engine --conf-thres 0.25 --iou-thres 0.45 --max-det 100

- Start continuous detection:
  PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 detect-start --session-id <id> --stream-id yolo-main --model-path /home/unitree/openclaw/go2_grpc/models/yolo26.engine --frame-skip 1 --fps-limit 10

- Subscribe callback stream:
  PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 detect-subscribe --session-id <id> --stream-id yolo-main

- Stop continuous detection:
  PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 detect-stop --session-id <id> --stream-id yolo-main

## Deployment Lifecycle Commands

Current stable native build host is `unitree@192.168.123.18`.

## Service Lifecycle For OpenClaw

OpenClaw should always perform service health check before robot actions.

### 1) Check service status

```bash
ssh unitree@192.168.51.213 "pgrep -ax go2_sport_grpc_server; ss -ltn | grep 50051 || true"
```

### 2) Start service (normal path)

```bash
ssh unitree@192.168.51.213 "IFACE=\$(ip -o -4 addr show | awk '/192\\.168\\.51\\./ {print \$2; exit}'); \
  test -n \"\$IFACE\" && nohup /home/unitree/openclaw/go2_grpc/bin/go2_sport_grpc_server \"\$IFACE\" 0.0.0.0 50051 \
  > /home/unitree/openclaw/go2_grpc/go2_grpc_server.log 2>&1 &"
```

### 3) Stop service

```bash
ssh unitree@192.168.51.213 "pkill -x go2_sport_grpc_server || true"
```

### 4) Restart service

```bash
ssh unitree@192.168.51.213 "pkill -x go2_sport_grpc_server || true"
ssh unitree@192.168.51.213 "IFACE=\$(ip -o -4 addr show | awk '/192\\.168\\.51\\./ {print \$2; exit}'); \
  test -n \"\$IFACE\" && nohup /home/unitree/openclaw/go2_grpc/bin/go2_sport_grpc_server \"\$IFACE\" 0.0.0.0 50051 \
  > /home/unitree/openclaw/go2_grpc/go2_grpc_server.log 2>&1 &"
```

### 5) If service is not running, auto-recover before action

OpenClaw should execute this policy:

1. Run status check.
2. If no `go2_sport_grpc_server` process or port `50051` not listening, run start command.
3. Retry status check once.
4. If still down, stop current task and report: `service_unavailable`.

Recommended one-shot check-and-start command:

```bash
ssh unitree@192.168.51.213 "pgrep -x go2_sport_grpc_server >/dev/null && ss -ltn | grep -q ':50051 ' && exit 0; \
  IFACE=\$(ip -o -4 addr show | awk '/192\\.168\\.51\\./ {print \$2; exit}'); \
  test -n \"\$IFACE\" && nohup /home/unitree/openclaw/go2_grpc/bin/go2_sport_grpc_server \"\$IFACE\" 0.0.0.0 50051 \
  > /home/unitree/openclaw/go2_grpc/go2_grpc_server.log 2>&1 &"
```

### Pull prebuilt binary from build host to local workspace

```bash
mkdir -p artifacts/go2_grpc
scp unitree@192.168.123.18:/home/unitree/workspace/unitree_sdk2/build-go2-grpc/bin/go2_sport_grpc_server \
  artifacts/go2_grpc/go2_sport_grpc_server.aarch64
file artifacts/go2_grpc/go2_sport_grpc_server.aarch64
```

Expected `file` output should contain `ELF 64-bit ... ARM aarch64`.

### Deploy prebuilt binary to service host (no remote compile)

```bash
ssh unitree@192.168.51.213 "mkdir -p /home/unitree/openclaw/go2_grpc/bin"
scp artifacts/go2_grpc/go2_sport_grpc_server.aarch64 \
  unitree@192.168.51.213:/home/unitree/openclaw/go2_grpc/bin/go2_sport_grpc_server
ssh unitree@192.168.51.213 "chmod +x /home/unitree/openclaw/go2_grpc/bin/go2_sport_grpc_server"
```

### Start/stop/status using deployed binary

Detect interface bound to `192.168.51.x` and start server:

```bash
ssh unitree@192.168.51.213 "IFACE=\$(ip -o -4 addr show | awk '/192\\.168\\.51\\./ {print \$2; exit}'); \
  test -n \"\$IFACE\" && nohup /home/unitree/openclaw/go2_grpc/bin/go2_sport_grpc_server \"\$IFACE\" 0.0.0.0 50051 \
  > /home/unitree/openclaw/go2_grpc/go2_grpc_server.log 2>&1 &"
```

Stop:

```bash
ssh unitree@192.168.51.213 "pkill -x go2_sport_grpc_server || true"
```

Status:

```bash
ssh unitree@192.168.51.213 "pgrep -ax go2_sport_grpc_server; ss -ltn | grep 50051 || true"
```

- Compile on dock host (native aarch64):
  ssh unitree@192.168.123.18 "cd /home/unitree/workspace/unitree_sdk2 && cmake -S . -B build-go2-grpc -DBUILD_EXAMPLES=OFF -DBUILD_GO2_GRPC=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build-go2-grpc -j4 --target go2_sport_grpc_server"

- Start service:
  ssh unitree@192.168.123.18 "cd /home/unitree/workspace/unitree_sdk2 && nohup ./build-go2-grpc/bin/go2_sport_grpc_server eth0 0.0.0.0 50051 > build-go2-grpc/go2_grpc_server.log 2>&1 &"

- Stop service:
  ssh unitree@192.168.123.18 "pkill -x go2_sport_grpc_server || true"

- Service status:
  ssh unitree@192.168.123.18 "pgrep -ax go2_sport_grpc_server; ss -ltn | grep 50051 || true"

For service-phase control via wifi, use endpoint `192.168.51.213:50051`.

## Minimal Local Test (PC)

Use only these two actions for smoke test:

1. Open session:
   PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 open-session --owner openclaw --session-name smoke
2. Stand up:
   PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_STAND_UP
3. Stand down:
   PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_STAND_DOWN
4. Close session:
   PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 close-session --session-id <id>
