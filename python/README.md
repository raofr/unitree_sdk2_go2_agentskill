# go2-grpc-tool

Python SDK and CLI for GO2 gRPC sport service.

## Install (editable)

```bash
cd python
pip install -e .
```

## Generate protobuf stubs

```bash
./scripts/generate_python_stubs.sh
```

## Example

```bash
go2-grpc --endpoint 192.168.51.213:50051 open-session --owner openclaw --session-name default
```
