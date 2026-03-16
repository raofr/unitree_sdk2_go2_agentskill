from __future__ import annotations

import argparse
import json

from go2_grpc_tool.client import ActionName, Go2SportClient


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


if __name__ == "__main__":
    main()
