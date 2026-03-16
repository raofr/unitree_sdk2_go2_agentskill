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

## Deployment Lifecycle Commands

Current stable native build host is `unitree@192.168.123.18`.

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
