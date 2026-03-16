# GO2 gRPC Build and Service Commands

This file records the current compile/start/stop/status workflow.

## Build Host (stable)

- Host: unitree@192.168.123.18
- Source dir: /home/unitree/workspace/unitree_sdk2
- Service endpoint (wifi): 192.168.51.213:50051

## 1. Compile

```bash
ssh unitree@192.168.123.18 "cd /home/unitree/workspace/unitree_sdk2 && cmake -S . -B build-go2-grpc -DBUILD_EXAMPLES=OFF -DBUILD_GO2_GRPC=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build-go2-grpc -j4 --target go2_sport_grpc_server"
```

## 2. Start

```bash
ssh unitree@192.168.123.18 "cd /home/unitree/workspace/unitree_sdk2 && nohup ./build-go2-grpc/bin/go2_sport_grpc_server eth0 0.0.0.0 50051 > build-go2-grpc/go2_grpc_server.log 2>&1 &"
```

## 3. Stop

```bash
ssh unitree@192.168.123.18 "pkill -x go2_sport_grpc_server || true"
```

## 4. Status

```bash
ssh unitree@192.168.123.18 "pgrep -ax go2_sport_grpc_server; ss -ltn | grep 50051 || true"
```

## 5. Local PC Python smoke test (StandUp/StandDown only)

Generate stubs first if needed:

```bash
PYTHON_BIN=python3 ./scripts/generate_python_stubs.sh
```

Install Python deps (local):

```bash
python3 -m pip install -r python/requirements.txt
```

Open session:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 open-session --owner openclaw --session-name smoke
```

StandUp:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_STAND_UP
```

StandDown:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_STAND_DOWN
```

Close session:

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 close-session --session-id <id>
```

## Verified Smoke Test (2026-03-17)

- Endpoint: `192.168.51.213:50051`
- Actions tested: `ACTION_STAND_UP`, `ACTION_STAND_DOWN`
- Result: both returned `code=0`, `message=ok`, `sdk_code=0`
