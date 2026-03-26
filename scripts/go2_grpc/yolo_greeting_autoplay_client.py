#!/usr/bin/env python3
from __future__ import annotations

import os
import random
import signal
import sys
import time
from pathlib import Path
from typing import List, Optional


def _env_str(key: str, default: str) -> str:
    v = os.getenv(key)
    return v if v is not None and v != "" else default


def _env_float(key: str, default: float) -> float:
    v = os.getenv(key)
    if v is None or v == "":
        return default
    try:
        return float(v)
    except ValueError:
        return default


def _env_int(key: str, default: int) -> int:
    v = os.getenv(key)
    if v is None or v == "":
        return default
    try:
        return int(v)
    except ValueError:
        return default


class ShuffleCycle:
    def __init__(self, files: List[Path]) -> None:
        self._base = list(files)
        self._queue: List[Path] = []
        self._refill()

    @staticmethod
    def _fisher_yates(items: List[Path]) -> None:
        for i in range(len(items) - 1, 0, -1):
            j = random.randint(0, i)
            items[i], items[j] = items[j], items[i]

    def _refill(self) -> None:
        self._queue = list(self._base)
        self._fisher_yates(self._queue)

    def next(self) -> Path:
        if not self._queue:
            self._refill()
        return self._queue.pop(0)


class YoloGreetingAutoplay:
    def __init__(self) -> None:
        repo_pkg_root = _env_str("GO2_AUTOPLAY_GO2_AGENT_TOOL_ROOT", "/home/unitree/openclaw/go2_grpc/go2_agent_tool")
        if repo_pkg_root not in sys.path:
            sys.path.insert(0, repo_pkg_root)

        from go2_agent_tool.client import (  # pylint: disable=import-error
            ActionName,
            AudioUploadConfig,
            DetectionConfig,
            Go2SportClient,
        )

        self.ActionName = ActionName
        self.AudioUploadConfig = AudioUploadConfig
        self.DetectionConfig = DetectionConfig
        self.client = Go2SportClient(
            endpoint=_env_str("GO2_AUTOPLAY_ENDPOINT", "127.0.0.1:50051"),
            timeout_sec=_env_float("GO2_AUTOPLAY_TIMEOUT_SEC", 20.0),
        )

        self.owner = _env_str("GO2_AUTOPLAY_OWNER", "go2_yolo_autoplay")
        self.session_name = _env_str("GO2_AUTOPLAY_SESSION_NAME", "yolo_greeting_autoplay")
        self.ttl_sec = _env_int("GO2_AUTOPLAY_TTL_SEC", 120)
        self.heartbeat_interval_sec = _env_float("GO2_AUTOPLAY_HEARTBEAT_INTERVAL_SEC", 20.0)
        self.detect_interval_sec = _env_float("GO2_AUTOPLAY_DETECT_INTERVAL_SEC", 1.0)
        self.cooldown_sec = _env_float("GO2_AUTOPLAY_COOLDOWN_SEC", 10.0)
        self.person_conf_min = _env_float("GO2_AUTOPLAY_PERSON_CONF_MIN", 0.45)
        self.model_path = _env_str(
            "GO2_AUTOPLAY_MODEL_PATH",
            "/home/unitree/workspace/unitree_sdk2/models/yolo26/aarch64/yolo26s.engine",
        )
        self.max_det = _env_int("GO2_AUTOPLAY_MAX_DET", 32)
        self.conf_thres = _env_float("GO2_AUTOPLAY_CONF_THRES", 0.25)
        self.iou_thres = _env_float("GO2_AUTOPLAY_IOU_THRES", 0.45)

        self.greetings_dir = Path(_env_str("GO2_AUTOPLAY_GREETINGS_DIR", "/home/unitree/openclaw/go2_grpc/assets/greetings"))
        self.audio_stream_id = _env_str("GO2_AUTOPLAY_AUDIO_STREAM_ID", "yolo-greeting")
        self.audio_volume = _env_float("GO2_AUTOPLAY_AUDIO_VOLUME", 1.0)
        self.audio_sample_rate = _env_int("GO2_AUTOPLAY_AUDIO_SAMPLE_RATE", 24000)
        self.audio_channels = _env_int("GO2_AUTOPLAY_AUDIO_CHANNELS", 1)

        self._session_id: Optional[str] = None
        self._last_hb_ts = 0.0
        self._next_trigger_ts = 0.0
        self._stop = False

        files = self._load_greetings()
        if not files:
            raise RuntimeError(f"no audio files in {self.greetings_dir}")
        self._greetings = ShuffleCycle(files)

    def _load_greetings(self) -> List[Path]:
        exts = {".wav", ".mp3"}
        out: List[Path] = []
        if not self.greetings_dir.exists():
            return out
        for p in sorted(self.greetings_dir.iterdir()):
            if p.is_file() and p.suffix.lower() in exts:
                out.append(p)
        return out

    def _infer_mime(self, path: Path) -> str:
        ext = path.suffix.lower()
        if ext == ".mp3":
            return "audio/mpeg"
        if ext == ".wav":
            return "audio/wav"
        return "audio/wav"

    def _ensure_session(self) -> None:
        now = time.time()
        if self._session_id is None:
            self.client.force_close_owner_sessions(self.owner, keep_parallel_sessions=False)
            sess = self.client.open_session(
                owner=self.owner,
                session_name=self.session_name,
                ttl_sec=self.ttl_sec,
                parallel=False,
            )
            self._session_id = sess.session_id
            self._last_hb_ts = now
            print(f"[autoplay] session opened: {self._session_id}", flush=True)
            return
        if now - self._last_hb_ts >= self.heartbeat_interval_sec:
            self.client.heartbeat(self._session_id)
            self._last_hb_ts = now

    @staticmethod
    def _has_person(resp, min_conf: float) -> bool:
        for d in resp.detections:
            label = (d.label or "").lower()
            if (d.class_id == 0 or label == "person") and float(d.score) >= min_conf:
                return True
        return False

    def _play_and_act(self) -> None:
        assert self._session_id is not None
        audio_path = self._greetings.next()
        mime = self._infer_mime(audio_path)
        with audio_path.open("rb") as f:
            payload = f.read()

        audio_resp = self.client.upload_and_play_audio(
            session_id=self._session_id,
            stream_id=self.audio_stream_id,
            audio_bytes=payload,
            config=self.AudioUploadConfig(
                mime=mime,
                sample_rate=self.audio_sample_rate,
                channels=self.audio_channels,
                volume=self.audio_volume,
                loop=False,
                request_id="",
            ),
        )
        print(f"[autoplay] played: {audio_path.name}, accepted={bool(audio_resp.accepted)}", flush=True)
        self.client.execute_action(session_id=self._session_id, action=self.ActionName.HEART)
        self.client.execute_action(session_id=self._session_id, action=self.ActionName.RECOVERY_STAND)
        print("[autoplay] action sequence: HEART -> RECOVERY_STAND", flush=True)

    def run(self) -> None:
        print("[autoplay] started", flush=True)
        while not self._stop:
            try:
                self._ensure_session()
                assert self._session_id is not None
                det = self.client.detect_once(
                    session_id=self._session_id,
                    config=self.DetectionConfig(
                        model_path=self.model_path,
                        conf_thres=self.conf_thres,
                        iou_thres=self.iou_thres,
                        max_det=self.max_det,
                    ),
                )
                now = time.time()
                if now >= self._next_trigger_ts and self._has_person(det, self.person_conf_min):
                    self._play_and_act()
                    self._next_trigger_ts = time.time() + self.cooldown_sec
                time.sleep(self.detect_interval_sec)
            except Exception as exc:  # pylint: disable=broad-except
                print(f"[autoplay] loop error: {exc}", flush=True)
                time.sleep(1.5)
                self._session_id = None

        self._shutdown()

    def stop(self) -> None:
        self._stop = True

    def _shutdown(self) -> None:
        if self._session_id:
            try:
                self.client.close_session(self._session_id)
            except Exception:  # pylint: disable=broad-except
                pass
            self._session_id = None
        print("[autoplay] stopped", flush=True)


def main() -> int:
    app = YoloGreetingAutoplay()

    def _handle(_sig, _frm):
        app.stop()

    signal.signal(signal.SIGINT, _handle)
    signal.signal(signal.SIGTERM, _handle)

    app.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
