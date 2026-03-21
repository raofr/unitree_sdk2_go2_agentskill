#!/usr/bin/env python3
import argparse
import asyncio
import json
import os
import shutil
import subprocess
import tempfile

from go2_webrtc_driver.webrtc_audiohub import WebRTCAudioHub
from go2_webrtc_driver.webrtc_driver import Go2WebRTCConnection, WebRTCConnectionMethod


def _looks_like_wav(path: str) -> bool:
    return path.lower().endswith(".wav")


def _looks_like_mp3(path: str) -> bool:
    return path.lower().endswith(".mp3")


def _convert_to_wav(src: str) -> str:
    fd, out = tempfile.mkstemp(prefix="go2_audio_bridge_", suffix=".wav")
    os.close(fd)
    cmd = ["ffmpeg", "-y", "-i", src, "-ac", "1", "-ar", "16000", out]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out


async def _run_play(args: argparse.Namespace) -> int:
    upload_path = args.input
    temp_converted = None
    temp_named_copy = None
    try:
        if not (_looks_like_wav(upload_path) or _looks_like_mp3(upload_path)):
            temp_converted = _convert_to_wav(upload_path)
            upload_path = temp_converted
        # Give uploaded file a deterministic name so CUSTOM_NAME lookup is stable.
        if args.stream_id:
            ext = os.path.splitext(upload_path)[1] or ".wav"
            temp_named_copy = os.path.join(tempfile.gettempdir(), f"{args.stream_id}{ext}")
            shutil.copyfile(upload_path, temp_named_copy)
            upload_path = temp_named_copy

        conn = Go2WebRTCConnection(WebRTCConnectionMethod.LocalSTA, ip=args.go2_ip)
        await conn.connect()
        hub = WebRTCAudioHub(conn)
        await hub.set_play_mode("no_cycle")
        upload_resp = await hub.upload_audio_file(upload_path)
        print("upload_resp=" + json.dumps(upload_resp, ensure_ascii=False))

        audio_list_resp = await hub.get_audio_list()
        payload = ((audio_list_resp or {}).get("data") or {}).get("data", "{}")
        parsed = json.loads(payload) if isinstance(payload, str) else (payload or {})
        items = parsed.get("audio_list") or []
        wanted_name = (args.stream_id or os.path.splitext(os.path.basename(upload_path))[0]).lower()

        target = None
        for item in items:
            name = str(item.get("CUSTOM_NAME") or "").lower()
            if name != wanted_name:
                continue
            if target is None or int(item.get("ADD_TIME") or 0) > int(target.get("ADD_TIME") or 0):
                target = item
        if target is None and items:
            target = max(items, key=lambda x: int(x.get("ADD_TIME") or 0))

        target_uid = str((target or {}).get("UNIQUE_ID") or "")
        if not target_uid:
            print("play_uid_missing=true")
            await conn.disconnect()
            return 2
        await hub.play_by_uuid(target_uid)
        print(f"play_sent=true uid={target_uid} name={wanted_name}")
        await asyncio.sleep(max(0.0, float(args.hold_sec)))
        await conn.disconnect()
        return 0
    finally:
        if temp_converted and os.path.exists(temp_converted):
            os.remove(temp_converted)
        if temp_named_copy and os.path.exists(temp_named_copy):
            os.remove(temp_named_copy)
        if args.delete_input and os.path.exists(args.input):
            os.remove(args.input)


async def _run_stop(args: argparse.Namespace) -> int:
    conn = Go2WebRTCConnection(WebRTCConnectionMethod.LocalSTA, ip=args.go2_ip)
    await conn.connect()
    hub = WebRTCAudioHub(conn)
    await hub.pause()
    await conn.disconnect()
    print("stop_sent=true")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Go2 audio playback sidecar bridge.")
    parser.add_argument("--go2-ip", default="192.168.123.161")
    parser.add_argument("--input", default="")
    parser.add_argument("--mime", default="")
    parser.add_argument("--stream-id", default="")
    parser.add_argument("--request-id", default="")
    parser.add_argument("--volume", type=float, default=1.0)
    parser.add_argument("--loop", action="store_true")
    parser.add_argument("--stop", action="store_true")
    parser.add_argument("--delete-input", action="store_true")
    parser.add_argument("--hold-sec", type=float, default=4.0)
    args = parser.parse_args()

    if args.stop:
        return asyncio.run(_run_stop(args))
    if not args.input:
        print("missing --input")
        return 2
    return asyncio.run(_run_play(args))


if __name__ == "__main__":
    raise SystemExit(main())
