#!/usr/bin/env python3
import argparse
import asyncio
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from go2_webrtc_driver.webrtc_audiohub import WebRTCAudioHub
from go2_webrtc_driver.webrtc_driver import Go2WebRTCConnection, WebRTCConnectionMethod

DEFAULT_SOCKET = "/tmp/go2_audio_play_bridge.sock"
DEFAULT_LOG = "/tmp/go2_audio_play_bridge.log"
DEFAULT_PRELOAD_DIR = "/home/unitree/openclaw/go2_grpc/assets/greetings"


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


class PersistentAudioPlayer:
    def __init__(self, go2_ip: str, preload_dir: str):
        self.go2_ip = go2_ip
        self.preload_dir = preload_dir
        self.conn = None
        self.hub = None
        self._connect_lock = asyncio.Lock()
        self._preloaded = False
        self._uuid_cache = {}

    async def _reset(self) -> None:
        if self.conn is not None:
            try:
                await self.conn.disconnect()
            except Exception:
                pass
        self.conn = None
        self.hub = None

    async def ensure_connected(self) -> None:
        if self.hub is not None:
            return
        async with self._connect_lock:
            if self.hub is not None:
                return
            await self._reset()
            self.conn = Go2WebRTCConnection(WebRTCConnectionMethod.LocalSTA, ip=self.go2_ip)
            await self.conn.connect()
            self.hub = WebRTCAudioHub(self.conn)
            await self.hub.set_play_mode("no_cycle")
            print(f"[audio-daemon] connected go2_ip={self.go2_ip}", flush=True)
            self._preloaded = False

    async def _resolve_latest_uid(self, custom_name: str) -> str:
        record = await self.hub.find_latest_audio_by_name(custom_name)
        return str((record or {}).get("UNIQUE_ID") or "")

    async def _preload_once(self) -> None:
        if self._preloaded:
            return
        preload_path = Path(self.preload_dir)
        if not preload_path.exists():
            self._preloaded = True
            print(f"[audio-daemon] preload skip (missing dir): {self.preload_dir}", flush=True)
            return

        files = [p for p in sorted(preload_path.iterdir()) if p.is_file() and p.suffix.lower() in {".wav", ".mp3"}]
        if not files:
            self._preloaded = True
            print(f"[audio-daemon] preload skip (no files): {self.preload_dir}", flush=True)
            return

        t0 = int(time.time() * 1000)
        hit = 0
        uploaded = 0
        for p in files:
            key = p.stem.lower()
            uid = await self._resolve_latest_uid(key)
            if uid:
                self._uuid_cache[key] = uid
                hit += 1
                continue
            try:
                await self.hub.upload_audio_file(str(p))
                uid = await self._resolve_latest_uid(key)
                if uid:
                    self._uuid_cache[key] = uid
                    uploaded += 1
            except Exception as exc:
                print(f"[audio-daemon] preload upload failed file={p.name} err={exc}", flush=True)
        self._preloaded = True
        t1 = int(time.time() * 1000)
        print(
            f"[audio-daemon] preload done files={len(files)} cache={len(self._uuid_cache)} "
            f"hit={hit} uploaded={uploaded} dt_ms={t1 - t0}",
            flush=True,
        )

    async def play(self, cmd: dict) -> None:
        upload_path = cmd.get("input", "")
        if not upload_path:
            return
        temp_converted = None
        temp_named_copy = None
        delete_input = bool(cmd.get("delete_input", False))
        try:
            await self.ensure_connected()
            await self._preload_once()

            stream_id = str(cmd.get("stream_id", "") or "")
            source_key = Path(upload_path).stem.lower()
            if stream_id:
                for token in stream_id.lower().split("-"):
                    if token.startswith("greeting_"):
                        source_key = token
                        break
            cached_uid = self._uuid_cache.get(source_key, "")
            if cached_uid:
                await self.hub.play_by_uuid(cached_uid)
                print(f"play_sent_cached=true uid={cached_uid} name={source_key}", flush=True)
                return

            if not (_looks_like_wav(upload_path) or _looks_like_mp3(upload_path)):
                temp_converted = _convert_to_wav(upload_path)
                upload_path = temp_converted

            if stream_id:
                ext = os.path.splitext(upload_path)[1] or ".wav"
                temp_named_copy = os.path.join(tempfile.gettempdir(), f"{stream_id}{ext}")
                shutil.copyfile(upload_path, temp_named_copy)
                upload_path = temp_named_copy

            upload_resp = await self.hub.upload_audio_file(upload_path)
            print("upload_resp=" + json.dumps(upload_resp, ensure_ascii=False), flush=True)

            wanted_name = (stream_id or os.path.splitext(os.path.basename(upload_path))[0]).lower()
            target_uid = await self._resolve_latest_uid(wanted_name)
            if not target_uid:
                items = await self.hub.get_audio_items()
                if items:
                    target_uid = str((max(items, key=lambda x: int(x.get("ADD_TIME") or 0)) or {}).get("UNIQUE_ID") or "")
            if not target_uid:
                print("play_uid_missing=true", flush=True)
                return
            await self.hub.play_by_uuid(target_uid)
            print(f"play_sent=true uid={target_uid} name={wanted_name}", flush=True)
        except Exception as exc:
            print(f"[audio-daemon] play error: {exc}", flush=True)
            await self._reset()
        finally:
            if temp_converted and os.path.exists(temp_converted):
                os.remove(temp_converted)
            if temp_named_copy and os.path.exists(temp_named_copy):
                os.remove(temp_named_copy)
            if delete_input:
                src = cmd.get("input", "")
                if src and os.path.exists(src):
                    os.remove(src)

    async def stop(self) -> None:
        try:
            await self.ensure_connected()
            await self.hub.pause()
            print("stop_sent=true", flush=True)
        except Exception as exc:
            print(f"[audio-daemon] stop error: {exc}", flush=True)
            await self._reset()


def _spawn_daemon_if_needed(args: argparse.Namespace) -> None:
    sock = Path(args.socket)
    if sock.exists():
        return
    log_fp = open(args.daemon_log, "a", buffering=1)
    cmd = [
        sys.executable,
        os.path.abspath(__file__),
        "--daemon",
        "--go2-ip",
        args.go2_ip,
        "--socket",
        args.socket,
        "--daemon-log",
        args.daemon_log,
    ]
    subprocess.Popen(
        cmd,
        stdout=log_fp,
        stderr=log_fp,
        stdin=subprocess.DEVNULL,
        close_fds=True,
        start_new_session=True,
    )
    deadline = time.time() + 2.5
    while time.time() < deadline:
        if sock.exists():
            return
        time.sleep(0.05)


def _send_command(sock_path: str, payload: dict) -> int:
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    cli = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    try:
        try:
            cli.connect(sock_path)
            cli.send(data)
        except OSError:
            # stale socket path or daemon race
            try:
                os.unlink(sock_path)
            except OSError:
                pass
            raise
    finally:
        cli.close()
    return 0


async def _run_daemon(args: argparse.Namespace) -> int:
    try:
        os.unlink(args.socket)
    except FileNotFoundError:
        pass
    except OSError:
        pass

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(args.socket)
    os.chmod(args.socket, 0o666)
    sock.setblocking(False)
    print(f"[audio-daemon] listening socket={args.socket}", flush=True)

    player = PersistentAudioPlayer(args.go2_ip, args.preload_dir)
    # Best-effort preconnect to reduce first-play latency.
    try:
        await player.ensure_connected()
        await player._preload_once()
    except Exception as exc:
        print(f"[audio-daemon] preconnect failed: {exc}", flush=True)
    queue: asyncio.Queue[dict] = asyncio.Queue()
    loop = asyncio.get_running_loop()

    async def consumer() -> None:
        while True:
            cmd = await queue.get()
            c = cmd.get("cmd")
            if c == "play":
                await player.play(cmd)
            elif c == "stop":
                await player.stop()
            queue.task_done()

    async def producer() -> None:
        while True:
            data = await loop.sock_recv(sock, 65535)
            if not data:
                continue
            try:
                cmd = json.loads(data.decode("utf-8"))
                if isinstance(cmd, dict):
                    await queue.put(cmd)
            except Exception:
                continue

    await asyncio.gather(producer(), consumer())
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
    parser.add_argument("--socket", default=DEFAULT_SOCKET)
    parser.add_argument("--daemon", action="store_true")
    parser.add_argument("--daemon-log", default=DEFAULT_LOG)
    parser.add_argument("--preload-dir", default=os.getenv("GO2_AUDIO_PRELOAD_DIR", DEFAULT_PRELOAD_DIR))
    args = parser.parse_args()

    if args.daemon:
        return asyncio.run(_run_daemon(args))

    payload = {"cmd": "stop"} if args.stop else {
        "cmd": "play",
        "input": args.input,
        "mime": args.mime,
        "stream_id": args.stream_id,
        "request_id": args.request_id,
        "volume": args.volume,
        "loop": bool(args.loop),
        "delete_input": bool(args.delete_input),
    }
    if payload.get("cmd") == "play" and not payload.get("input"):
        print("missing --input")
        return 2
    _spawn_daemon_if_needed(args)
    try:
        return _send_command(args.socket, payload)
    except OSError:
        _spawn_daemon_if_needed(args)
        return _send_command(args.socket, payload)


if __name__ == "__main__":
    raise SystemExit(main())
