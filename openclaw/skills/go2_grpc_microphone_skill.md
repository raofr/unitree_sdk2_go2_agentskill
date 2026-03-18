# GO2 Microphone Recording Skill

## Scope

Use the GO2 gRPC microphone recording service to capture audio from the robot's microphone via WebRTC. This enables voice interaction capabilities.

## Prerequisites

- GO2 gRPC server must be running (see `go2_grpc_skill.md` for service lifecycle)
- WebRTC signaling must be configured (via `GO2_WEBRTC_SIGNAL_*` environment variables)
- Robot must have audio capability enabled

## Architecture

The microphone recording uses WebRTC bidirectional audio:
- The existing WebRTC infrastructure is extended with an `appsink` to receive robot's microphone audio
- Audio is captured as raw Opus packets
- Multiple concurrent microphone streams are supported

## Python Tool Commands

### Start Microphone Recording

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 \
  mic-start --session-id <session_id> --stream-id mic-main
```

Options:
- `--session-id`: Required. The session ID from `open-session`
- `--stream-id`: Stream identifier (default: auto-generated)
- `--sample-rate`: Audio sample rate (default: 48000)
- `--channels`: Number of audio channels (default: 1)

### Subscribe to Microphone Audio

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 \
  mic-subscribe --session-id <session_id> --stream-id mic-main
```

This streams audio data from the robot's microphone. Each audio packet contains:
- `stream_id`: The stream identifier
- `timestamp_ms`: Timestamp in milliseconds
- `is_silence`: Whether the audio is silence
- `audio_data_len`: Length of the audio data (audio_data is raw Opus)

### Stop Microphone Recording

```bash
PYTHONPATH=python python3 -m go2_grpc_tool.cli --endpoint 192.168.51.213:50051 \
  mic-stop --session-id <session_id> --stream-id mic-main
```

## Workflow Example

```bash
# 1. Open session
SESSION_ID=$(PYTHONPATH=python python3 -m go2_grpc_tool.cli \
  --endpoint 192.168.51.213:50051 open-session --owner openclaw | \
  python3 -c "import sys,json; print(json.load(sys.stdin)['session_id'])")

# 2. Start microphone recording
STREAM_ID=$(PYTHONPATH=python python3 -m go2_grpc_tool.cli \
  --endpoint 192.168.51.213:50051 mic-start \
  --session-id $SESSION_ID --stream-id mic-main | \
  python3 -c "import sys,json; print(json.load(sys.stdin)['stream_id'])")

# 3. Subscribe to audio stream (runs for a duration)
PYTHONPATH=python python3 -m go2_grpc_tool.cli \
  --endpoint 192.168.51.213:50051 mic-subscribe \
  --session-id $SESSION_ID --stream-id $STREAM_ID | \
  while read line; do
    echo "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(f\"Audio: {d['audio_data_len']} bytes at {d['timestamp_ms']}ms\")"
  done

# 4. Stop microphone
PYTHONPATH=python python3 -m go2_grpc_tool.cli \
  --endpoint 192.168.51.213:50051 mic-stop \
  --session-id $SESSION_ID --stream-id $STREAM_ID

# 5. Close session
PYTHONPATH=python python3 -m go2_grpc_tool.cli \
  --endpoint 192.168.51.213:50051 close-session --session-id $SESSION_ID
```

## Node.js Tool Commands

```bash
# Start microphone
node src/cli.js --endpoint 192.168.51.213:50051 mic-start \
  --session-id <session_id> --stream-id mic-main

# Subscribe to microphone
node src/cli.js --endpoint 192.168.51.213:50051 mic-subscribe \
  --session-id <session_id> --stream-id mic-main

# Stop microphone
node src/cli.js --endpoint 192.168.51.213:50051 mic-stop \
  --session-id <session_id> --stream-id mic-main
```

## Error Handling

- `404`: Session not found
- `500`: Microphone start failed (e.g., stream already exists, gstreamer error)
- The SubscribeMicrophone stream will terminate when:
  - Client disconnects
  - Server is shut down
  - Session expires
  - `COMMAND_STOP` is sent via the control stream

## Implementation Notes

- Audio is captured via WebRTC `appsink` element in the GStreamer pipeline
- The `AudioCaptureManager` class manages concurrent microphone streams
- Each stream has its own queue with a maximum depth of 100 packets
- Timestamp is extracted from the GStreamer buffer PTS (presentation time stamp)