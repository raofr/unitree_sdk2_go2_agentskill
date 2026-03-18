# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Unitree SDK2 is a C++ SDK for controlling Unitree robots (GO2, B2, H1, G1, etc.). It provides:
- A pre-built C++ library with headers in `include/unitree/`
- DDS (Data Distribution Service) for real-time robot state communication
- Optional gRPC service (`src/go2_grpc/`) for network API access
- Node.js and Python client toolkits in `go2_agent_tool/`

## Build Commands

```bash
# Build examples (default ON)
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON
make

# Build with gRPC server (requires protobuf, gRPC, TensorRT, OpenCV, GStreamer deps)
cmake .. -DBUILD_GO2_GRPC=ON
make

# Install SDK to system
sudo make install

# Install to custom prefix
cmake .. -DCMAKE_INSTALL_PREFIX=/opt/unitree_robotics
sudo make install
```

## Architecture

```
lib/{aarch64,x86_64}/      # Pre-built SDK libraries (libunitree_sdk2.a, libddsc.so, libddscxx.so)
include/unitree/            # SDK headers
proto/go2_sport.proto      # gRPC service definition for GO2 robot control
src/go2_grpc/              # C++ gRPC server implementation (build with -DBUILD_GO2_GRPC=ON)
go2_agent_tool/            # Submodule: Node.js + Python gRPC client CLIs
  node/                     # Node.js CLI (preferred for OpenClaw)
  go2_agent_tool/           # Python CLI + generated protobuf stubs
example/                   # Robot-specific examples (go2/, b2/, h1/, g1/, etc.)
openclaw/                  # OpenClaw agent skills and tools
  skills/                  # Skill definitions for agent integration
  tools/                   # Tool definitions
python/                    # Python SDK package (go2-grpc CLI)
```

## Key SDK Components

- **SportClient**: High-level motion control (StandUp, StandDown, Move, BalanceStand, etc.)
- **ChannelSubscriber**: DDS-based robot state subscription (e.g., `SportModeState_`)
- **Robot state topic**: `rt/sportmodestate` - provides position, IMU, gait state

## gRPC Service (GO2 Only)

The gRPC server in `src/go2_grpc/` exposes robot control over network with:
- Session management (open/close/heartbeat)
- Action execution (40+ actions including locomotion, posture, special moves)
- YOLO detection support (TensorRT)
- Audio playback (Opus format via GStreamer)

Build requires: Protobuf, gRPC, TensorRT, OpenCV, GStreamer, libcurl, OpenSSL

## Python CLI

```bash
cd python && pip install -e .
go2-grpc --endpoint 192.168.51.213:50051 status
```

## Node.js CLI (Preferred for OpenClaw)

```bash
cd go2_agent_tool/node && npm install
node src/cli.js discover-endpoint --discovery-port 50052 --discovery-timeout-ms 5000 --prefer-ipv6
node src/cli.js --endpoint <DISCOVERED_ENDPOINT> status
```

## Dependencies for Full Build

```bash
# Ubuntu 20.04
apt-get install cmake g++ build-essential libyaml-cpp-dev libeigen3-dev \
  libboost-all-dev libspdlog-dev libfmt-dev

# For BUILD_GO2_GRPC=ON
apt-get install libprotobuf-dev protobuf-compiler-grpc libgrpc++-dev libssl-dev \
  libcurl4-openssl-dev libopencv-dev libgstreamer1.0-dev libgstreamer-plugins-bad1.0-dev
```
