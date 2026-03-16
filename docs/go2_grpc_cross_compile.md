# GO2 gRPC Cross-Compile and Deploy Guide

This project now includes an optional GO2 sport gRPC service.

## 1. Host prerequisites (x86_64 build machine)

Install base tools:

- cmake
- ninja-build or make
- aarch64-linux-gnu-gcc / aarch64-linux-gnu-g++
- protobuf compiler and gRPC C++ dev package for cross build environment

For Python client:

- python3
- pip
- grpcio
- grpcio-tools
- protobuf

## 2. Configure cross build

Use the provided toolchain file:

- cmake/toolchains/aarch64-ubuntu2004.cmake

Optional:

- set GO2_AARCH64_SYSROOT to Ubuntu 20.04 aarch64 sysroot path for ABI consistency.

## 3. Build server (aarch64)

Run:

- scripts/go2_grpc/cross_build_aarch64.sh

This creates:

- build-aarch64/bin/go2_sport_grpc_server

## 4. Generate Python gRPC stubs

Run:

- scripts/generate_python_stubs.sh

## 5. Deploy to dock

Set env vars as needed:

- GO2_DOCK_HOST (default 192.168.51.213)
- GO2_DOCK_USER (default unitree)
- GO2_DOCK_PASSWORD (default 123)
- GO2_DOCK_DIR (default /home/unitree/openclaw/go2_grpc)

Deploy:

- scripts/go2_grpc/deploy.sh

## 6. Manage remote service

- Start: scripts/go2_grpc/run.sh
- Stop: scripts/go2_grpc/kill.sh
- Status: scripts/go2_grpc/status.sh

## 7. Skill default policy

Unless parallel mode is explicitly required, new tasks should cleanup previous owner sessions before issuing new actions.

- scripts/go2_grpc/exec_action.sh enforces this default when GO2_PARALLEL is not true.
