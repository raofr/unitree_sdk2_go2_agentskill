from __future__ import annotations

import argparse
import json

from go2_grpc_tool.client import ActionName, AudioUploadConfig, DetectionConfig, Go2SportClient


def _add_common(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--endpoint", default="127.0.0.1:50051", help="gRPC endpoint, e.g. 192.168.51.213:50051")
    parser.add_argument("--timeout", type=float, default=5.0, help="RPC timeout seconds")


def main() -> None:
    parser = argparse.ArgumentParser(prog="go2-grpc", description="GO2 gRPC CLI tool")
    sub = parser.add_subparsers(dest="command", required=True)

    p_open = sub.add_parser("open-session")
    _add_common(p_open)
    p_open.add_argument("--owner", required=True)
    p_open.add_argument("--session-name", default="")
    p_open.add_argument("--ttl-sec", type=int, default=30)
    p_open.add_argument("--parallel", action="store_true")

    p_hb = sub.add_parser("heartbeat")
    _add_common(p_hb)
    p_hb.add_argument("--session-id", required=True)

    p_close = sub.add_parser("close-session")
    _add_common(p_close)
    p_close.add_argument("--session-id", required=True)

    p_force = sub.add_parser("force-close-owner")
    _add_common(p_force)
    p_force.add_argument("--owner", required=True)
    p_force.add_argument("--keep-parallel-sessions", action="store_true")

    p_action = sub.add_parser("action")
    _add_common(p_action)
    p_action.add_argument("--session-id", required=True)
    p_action.add_argument("--action", required=True, choices=[a.value for a in ActionName])
    p_action.add_argument("--vx", type=float, default=0.0)
    p_action.add_argument("--vy", type=float, default=0.0)
    p_action.add_argument("--vyaw", type=float, default=0.0)
    p_action.add_argument("--roll", type=float, default=0.0)
    p_action.add_argument("--pitch", type=float, default=0.0)
    p_action.add_argument("--yaw", type=float, default=0.0)
    p_action.add_argument("--level", type=int, default=0)
    p_action.add_argument("--flag", action="store_true")

    p_status = sub.add_parser("status")
    _add_common(p_status)

    p_detect_once = sub.add_parser("detect-once")
    _add_common(p_detect_once)
    p_detect_once.add_argument("--session-id", required=True)
    p_detect_once.add_argument("--model-path", default="")
    p_detect_once.add_argument("--conf-thres", type=float, default=0.25)
    p_detect_once.add_argument("--iou-thres", type=float, default=0.45)
    p_detect_once.add_argument("--max-det", type=int, default=100)

    p_detect_start = sub.add_parser("detect-start")
    _add_common(p_detect_start)
    p_detect_start.add_argument("--session-id", required=True)
    p_detect_start.add_argument("--stream-id", default="")
    p_detect_start.add_argument("--model-path", default="")
    p_detect_start.add_argument("--conf-thres", type=float, default=0.25)
    p_detect_start.add_argument("--iou-thres", type=float, default=0.45)
    p_detect_start.add_argument("--max-det", type=int, default=100)
    p_detect_start.add_argument("--frame-skip", type=int, default=0)
    p_detect_start.add_argument("--fps-limit", type=int, default=5)

    p_detect_stop = sub.add_parser("detect-stop")
    _add_common(p_detect_stop)
    p_detect_stop.add_argument("--session-id", required=True)
    p_detect_stop.add_argument("--stream-id", required=True)

    p_detect_sub = sub.add_parser("detect-subscribe")
    _add_common(p_detect_sub)
    p_detect_sub.add_argument("--session-id", required=True)
    p_detect_sub.add_argument("--stream-id", required=True)

    p_audio_up = sub.add_parser("audio-upload-play")
    _add_common(p_audio_up)
    p_audio_up.add_argument("--session-id", required=True)
    p_audio_up.add_argument("--stream-id", default="")
    p_audio_up.add_argument("--file", required=True)
    p_audio_up.add_argument("--mime", default="audio/opus")
    p_audio_up.add_argument("--sample-rate", type=int, default=48000)
    p_audio_up.add_argument("--channels", type=int, default=1)
    p_audio_up.add_argument("--volume", type=float, default=1.0)
    p_audio_up.add_argument("--loop", action="store_true")
    p_audio_up.add_argument("--request-id", default="")

    p_audio_status = sub.add_parser("audio-status")
    _add_common(p_audio_status)
    p_audio_status.add_argument("--session-id", required=True)

    p_audio_stop = sub.add_parser("audio-stop")
    _add_common(p_audio_stop)
    p_audio_stop.add_argument("--session-id", required=True)
    p_audio_stop.add_argument("--stream-id", default="")

    p_mic_start = sub.add_parser("mic-start")
    _add_common(p_mic_start)
    p_mic_start.add_argument("--session-id", required=True)
    p_mic_start.add_argument("--stream-id", default="")
    p_mic_start.add_argument("--sample-rate", type=int, default=48000)
    p_mic_start.add_argument("--channels", type=int, default=1)

    p_mic_stop = sub.add_parser("mic-stop")
    _add_common(p_mic_stop)
    p_mic_stop.add_argument("--session-id", required=True)
    p_mic_stop.add_argument("--stream-id", required=True)

    p_mic_subscribe = sub.add_parser("mic-subscribe")
    _add_common(p_mic_subscribe)
    p_mic_subscribe.add_argument("--session-id", required=True)
    p_mic_subscribe.add_argument("--stream-id", required=True)

    args = parser.parse_args()
    client = Go2SportClient(endpoint=args.endpoint, timeout_sec=args.timeout)

    if args.command == "open-session":
        sess = client.open_session(
            owner=args.owner,
            session_name=args.session_name,
            ttl_sec=args.ttl_sec,
            parallel=args.parallel,
        )
        print(json.dumps({"session_id": sess.session_id, "expires_at_ms": sess.expires_at_ms}))
        return

    if args.command == "heartbeat":
        expires = client.heartbeat(session_id=args.session_id)
        print(json.dumps({"expires_at_ms": expires}))
        return

    if args.command == "close-session":
        client.close_session(session_id=args.session_id)
        print(json.dumps({"ok": True}))
        return

    if args.command == "force-close-owner":
        cnt = client.force_close_owner_sessions(
            owner=args.owner,
            keep_parallel_sessions=args.keep_parallel_sessions,
        )
        print(json.dumps({"closed_count": cnt}))
        return

    if args.command == "action":
        resp = client.execute_action(
            session_id=args.session_id,
            action=ActionName(args.action),
            vx=args.vx,
            vy=args.vy,
            vyaw=args.vyaw,
            roll=args.roll,
            pitch=args.pitch,
            yaw=args.yaw,
            level=args.level,
            flag=args.flag,
        )
        print(
            json.dumps(
                {
                    "code": resp.code,
                    "message": resp.message,
                    "sdk_code": resp.sdk_code,
                    "bool_value": bool(resp.bool_value),
                }
            )
        )
        return

    if args.command == "status":
        status = client.get_server_status()
        sessions = []
        for s in status.sessions:
            sessions.append(
                {
                    "session_id": s.session_id,
                    "owner": s.owner,
                    "session_name": s.session_name,
                    "parallel": s.parallel,
                    "expires_at_ms": s.expires_at_ms,
                }
            )
        print(json.dumps({"active_sessions": status.active_sessions, "sessions": sessions}))
        return

    if args.command == "detect-once":
        resp = client.detect_once(
            session_id=args.session_id,
            config=DetectionConfig(
                model_path=args.model_path,
                conf_thres=args.conf_thres,
                iou_thres=args.iou_thres,
                max_det=args.max_det,
            ),
        )
        print(
            json.dumps(
                {
                    "frame_id": int(resp.frame_id),
                    "timestamp_ms": int(resp.timestamp_ms),
                    "detections": [
                        {
                            "class_id": int(d.class_id),
                            "label": d.label,
                            "score": float(d.score),
                            "bbox": {
                                "x": float(d.bbox.x),
                                "y": float(d.bbox.y),
                                "w": float(d.bbox.w),
                                "h": float(d.bbox.h),
                            },
                        }
                        for d in resp.detections
                    ],
                }
            )
        )
        return

    if args.command == "detect-start":
        resp = client.start_detection(
            session_id=args.session_id,
            stream_id=args.stream_id,
            frame_skip=args.frame_skip,
            fps_limit=args.fps_limit,
            config=DetectionConfig(
                model_path=args.model_path,
                conf_thres=args.conf_thres,
                iou_thres=args.iou_thres,
                max_det=args.max_det,
            ),
        )
        print(json.dumps({"stream_id": resp.stream_id}))
        return

    if args.command == "detect-stop":
        resp = client.stop_detection(session_id=args.session_id, stream_id=args.stream_id)
        print(json.dumps({"stopped": bool(resp.stopped)}))
        return

    if args.command == "detect-subscribe":
        for event in client.subscribe_detections(session_id=args.session_id, stream_id=args.stream_id):
            print(
                json.dumps(
                    {
                        "stream_id": event.stream_id,
                        "frame_id": int(event.frame_id),
                        "timestamp_ms": int(event.timestamp_ms),
                        "detections": [
                            {
                                "class_id": int(d.class_id),
                                "label": d.label,
                                "score": float(d.score),
                                "bbox": {
                                    "x": float(d.bbox.x),
                                    "y": float(d.bbox.y),
                                    "w": float(d.bbox.w),
                                    "h": float(d.bbox.h),
                                },
                            }
                            for d in event.detections
                        ],
                    }
                )
            )
        return

    if args.command == "audio-upload-play":
        with open(args.file, "rb") as f:
            payload = f.read()
        resp = client.upload_and_play_audio(
            session_id=args.session_id,
            stream_id=args.stream_id,
            audio_bytes=payload,
            config=AudioUploadConfig(
                mime=args.mime,
                sample_rate=args.sample_rate,
                channels=args.channels,
                volume=args.volume,
                loop=args.loop,
                request_id=args.request_id,
            ),
        )
        print(
            json.dumps(
                {
                    "stream_id": resp.stream_id,
                    "request_id": resp.request_id,
                    "accepted": bool(resp.accepted),
                }
            )
        )
        return

    if args.command == "audio-status":
        resp = client.get_audio_status(session_id=args.session_id)
        print(
            json.dumps(
                {
                    "connected": bool(resp.connected),
                    "playing": bool(resp.playing),
                    "stream_id": resp.stream_id,
                    "queued_items": int(resp.queued_items),
                    "last_error_ts_ms": int(resp.last_error_ts_ms),
                    "last_error": resp.last_error,
                }
            )
        )
        return

    if args.command == "audio-stop":
        resp = client.stop_audio_playback(session_id=args.session_id, stream_id=args.stream_id)
        print(json.dumps({"stopped": bool(resp.stopped)}))
        return

    if args.command == "mic-start":
        resp = client.start_microphone(
            session_id=args.session_id,
            stream_id=args.stream_id,
            sample_rate=args.sample_rate,
            channels=args.channels,
        )
        print(json.dumps({"stream_id": resp.stream_id}))
        return

    if args.command == "mic-stop":
        resp = client.stop_microphone(session_id=args.session_id, stream_id=args.stream_id)
        print(json.dumps({"stopped": bool(resp.stopped)}))
        return

    if args.command == "mic-subscribe":
        for audio in client.subscribe_microphone(session_id=args.session_id, stream_id=args.stream_id):
            print(
                json.dumps(
                    {
                        "stream_id": audio.stream_id,
                        "timestamp_ms": int(audio.timestamp_ms),
                        "is_silence": bool(audio.is_silence),
                        "audio_data_len": len(audio.audio_data),
                    }
                )
            )
        return


if __name__ == "__main__":
    main()
