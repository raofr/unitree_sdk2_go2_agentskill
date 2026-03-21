#!/usr/bin/env python3
import argparse
import asyncio
import socket
import struct
import time

from go2_webrtc_driver.webrtc_driver import Go2WebRTCConnection, WebRTCConnectionMethod


def build_packet(stream_id: str, timestamp_ms: int, payload: bytes) -> bytes:
    sid = stream_id.encode("utf-8")
    if len(sid) > 255:
        sid = sid[:255]
    header = bytearray()
    header.append(1)  # protocol version
    header.append(len(sid))
    header.extend(struct.pack("<Q", timestamp_ms))
    header.extend(sid)
    return bytes(header) + payload


async def run_sender(go2_ip: str, stream_id: str, udp_host: str, udp_port: int) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_target = (udp_host, udp_port)

    conn = Go2WebRTCConnection(WebRTCConnectionMethod.LocalSTA, ip=go2_ip)
    sent_frames = 0
    sent_bytes = 0
    last_log = time.time()
    last_send_err = 0.0

    async def on_frame(frame):
        nonlocal sent_frames, sent_bytes, last_log, last_send_err
        arr = frame.to_ndarray()
        payload = arr.tobytes()
        ts_ms = int(time.time() * 1000)
        pkt = build_packet(stream_id, ts_ms, payload)
        try:
            sock.sendto(pkt, udp_target)
        except OSError as exc:
            now = time.time()
            if now - last_send_err >= 2.0:
                print(f"[mic-bridge] udp send error: {exc}")
                last_send_err = now
            return
        sent_frames += 1
        sent_bytes += len(payload)
        now = time.time()
        if now - last_log >= 2.0:
            print(f"[mic-bridge] sent_frames={sent_frames} sent_bytes={sent_bytes}")
            last_log = now

    await conn.connect()
    conn.audio.add_track_callback(on_frame)
    conn.audio.switchAudioChannel(True)
    print(
        f"[mic-bridge] connected, forwarding to udp://{udp_host}:{udp_port}, stream_id={stream_id}"
    )
    while True:
        await asyncio.sleep(1)


def main() -> None:
    parser = argparse.ArgumentParser(description="Forward Go2 microphone audio to local UDP bridge.")
    parser.add_argument("--go2-ip", default="192.168.123.161")
    parser.add_argument("--stream-id", default="mic-default")
    parser.add_argument("--udp-host", default="127.0.0.1")
    parser.add_argument("--udp-port", type=int, default=39001)
    args = parser.parse_args()

    while True:
        try:
            asyncio.run(
                run_sender(
                    go2_ip=args.go2_ip,
                    stream_id=args.stream_id,
                    udp_host=args.udp_host,
                    udp_port=args.udp_port,
                )
            )
        except KeyboardInterrupt:
            print("[mic-bridge] interrupted, exiting")
            break
        except Exception as exc:
            print(f"[mic-bridge] error: {exc}, retry in 2s")
            time.sleep(2)


if __name__ == "__main__":
    main()
