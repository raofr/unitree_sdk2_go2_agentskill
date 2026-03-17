# GO2 gRPC Tool Guide

## Purpose

This tool wraps `python/go2_grpc_tool/cli.py` so openclaw can execute GO2 actions only via gRPC service.

## Runtime Preconditions

- gRPC service endpoint must be reachable: `192.168.51.213:50051`
- Dock host must have external USB Wi-Fi adapter active on `192.168.51.x`
- Python command must run from repo root with `PYTHONPATH=python`

## Canonical Command Prefix

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051
```

## Tool Contract for Agent

Default non-parallel execution flow:

1. `force-close-owner --owner openclaw`
2. `open-session --owner openclaw --session-name <task_name>`
3. `action --session-id <id> --action <ACTION_...>`
4. optional `heartbeat --session-id <id>` for long tasks
5. `close-session --session-id <id>`

Parallel mode:

- Open with `open-session ... --parallel`
- Do not force-close owner sessions

## Subcommands

Status:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 status
```

Force close owner sessions:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 force-close-owner --owner openclaw
```

Open session:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 open-session --owner openclaw --session-name default
```

Action examples:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_STAND_UP
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_STAND_DOWN
```

Close session:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 close-session --session-id <id>
```

Detection once:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 detect-once --session-id <id> --model-path /home/unitree/openclaw/go2_grpc/models/yolo26.engine
```

Start/subscribe/stop detection:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 detect-start --session-id <id> --stream-id yolo-main --model-path /home/unitree/openclaw/go2_grpc/models/yolo26.engine --frame-skip 1 --fps-limit 10
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 detect-subscribe --session-id <id> --stream-id yolo-main
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 detect-stop --session-id <id> --stream-id yolo-main
```

## Binary Deployment (Prebuilt)

Pull from build host (`192.168.123.18`):

```bash
mkdir -p artifacts/go2_grpc
scp unitree@192.168.123.18:/home/unitree/workspace/unitree_sdk2/build-go2-grpc/bin/go2_sport_grpc_server \
  artifacts/go2_grpc/go2_sport_grpc_server.aarch64
```

Deploy to service host (`192.168.51.213`):

```bash
ssh unitree@192.168.51.213 "mkdir -p /home/unitree/openclaw/go2_grpc/bin"
scp artifacts/go2_grpc/go2_sport_grpc_server.aarch64 \
  unitree@192.168.51.213:/home/unitree/openclaw/go2_grpc/bin/go2_sport_grpc_server
ssh unitree@192.168.51.213 "chmod +x /home/unitree/openclaw/go2_grpc/bin/go2_sport_grpc_server"
```

Start service with detected `192.168.51.x` interface:

```bash
ssh unitree@192.168.51.213 "IFACE=\$(ip -o -4 addr show | awk '/192\\.168\\.51\\./ {print \$2; exit}'); \
  test -n \"\$IFACE\" && nohup /home/unitree/openclaw/go2_grpc/bin/go2_sport_grpc_server \"\$IFACE\" 0.0.0.0 50051 \
  > /home/unitree/openclaw/go2_grpc/go2_grpc_server.log 2>&1 &"
```
