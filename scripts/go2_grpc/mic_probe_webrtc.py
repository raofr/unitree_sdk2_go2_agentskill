#!/usr/bin/env python3
import argparse
import asyncio
import time

from go2_webrtc_driver.webrtc_driver import Go2WebRTCConnection, WebRTCConnectionMethod


async def run_probe(ip: str, duration_sec: int) -> None:
    stats = {"frames": 0, "bytes": 0, "first_ts": None, "last_ts": None}

    async def on_frame(frame):
        arr = frame.to_ndarray()
        payload = arr.tobytes()
        now = time.time()
        if stats["first_ts"] is None:
            stats["first_ts"] = now
        stats["last_ts"] = now
        stats["frames"] += 1
        stats["bytes"] += len(payload)

    conn = Go2WebRTCConnection(WebRTCConnectionMethod.LocalSTA, ip=ip)
    await conn.connect()
    conn.audio.add_track_callback(on_frame)
    conn.audio.switchAudioChannel(True)

    started = time.time()
    while time.time() - started < duration_sec:
        await asyncio.sleep(1)
        print(f"tick frames={stats['frames']} bytes={stats['bytes']}")

    await conn.disconnect()
    print(f"final frames={stats['frames']} bytes={stats['bytes']}")
    print(
        f"final first_ts={stats['first_ts']} last_ts={stats['last_ts']}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Probe Go2 microphone via go2_webrtc_connect.")
    parser.add_argument("--ip", default="192.168.123.161")
    parser.add_argument("--duration-sec", type=int, default=20)
    args = parser.parse_args()
    asyncio.run(run_probe(args.ip, args.duration_sec))


if __name__ == "__main__":
    main()
