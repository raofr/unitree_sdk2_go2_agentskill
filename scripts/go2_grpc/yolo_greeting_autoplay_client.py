#!/usr/bin/env python3
from __future__ import annotations

import os
import random
import signal
import sys
import threading
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
        self.Go2SportClient = Go2SportClient
        self.endpoint = _env_str("GO2_AUTOPLAY_ENDPOINT", "127.0.0.1:50051")
        self.timeout_sec = _env_float("GO2_AUTOPLAY_TIMEOUT_SEC", 20.0)
        self.client = self._new_client()

        self.owner = _env_str("GO2_AUTOPLAY_OWNER", "go2_yolo_autoplay")
        self.session_name = _env_str("GO2_AUTOPLAY_SESSION_NAME", "yolo_greeting_autoplay")
        self.ttl_sec = _env_int("GO2_AUTOPLAY_TTL_SEC", 120)
        self.heartbeat_interval_sec = _env_float("GO2_AUTOPLAY_HEARTBEAT_INTERVAL_SEC", 20.0)
        self.detect_interval_sec = _env_float("GO2_AUTOPLAY_DETECT_INTERVAL_SEC", 1.0)
        self.cooldown_sec = _env_float("GO2_AUTOPLAY_COOLDOWN_SEC", 10.0)
        self.person_conf_min = _env_float("GO2_AUTOPLAY_PERSON_CONF_MIN", 0.7)
        self.person_width_ratio_min = _env_float("GO2_AUTOPLAY_PERSON_WIDTH_RATIO_MIN", 0.15)
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
        self._audio_seq = 0
        self._stop = False

        files = self._load_greetings()
        if not files:
            raise RuntimeError(f"no audio files in {self.greetings_dir}")
        self._greetings = ShuffleCycle(files)

    def _new_client(self):
        return self.Go2SportClient(endpoint=self.endpoint, timeout_sec=self.timeout_sec)

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
    def _has_person(resp, min_conf: float, min_width_ratio: float) -> bool:
        for d in resp.detections:
            label = (d.label or "").lower()
            if d.class_id != 0 and label != "person":
                continue
            score = float(d.score)
            w_ratio = float(getattr(d, "bbox_w_ratio", 0.0))
            h_ratio = float(getattr(d, "bbox_h_ratio", 0.0))
            width_ok = w_ratio >= min_width_ratio
            print(
                "[autoplay] person candidate: "
                f"score={score:.3f}, bbox_w_ratio={w_ratio:.3f}, bbox_h_ratio={h_ratio:.3f}, "
                f"conf_ok={score >= min_conf}, width_ok={width_ok}",
                flush=True,
            )
            if score >= min_conf and width_ok:
                return True
        return False

    def _play_and_act(self) -> None:
        assert self._session_id is not None
        audio_path = self._greetings.next()
        mime = self._infer_mime(audio_path)
        with audio_path.open("rb") as f:
            payload = f.read()

        session_id = self._session_id
        audio_err: List[str] = []
        self._audio_seq += 1
        stream_id = f"{self.audio_stream_id}-{audio_path.stem}-{int(time.time() * 1000)}-{self._audio_seq}"

        def _play_audio_task() -> None:
            try:
                audio_client = self._new_client()
                audio_resp = audio_client.upload_and_play_audio(
                    session_id=session_id,
                    stream_id=stream_id,
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
                print(
                    f"[autoplay] played: {audio_path.name}, stream_id={stream_id}, accepted={bool(audio_resp.accepted)}",
                    flush=True,
                )
            except Exception as exc:  # pylint: disable=broad-except
                audio_err.append(str(exc))

        # Run audio request and HEART action in parallel.
        audio_thread = threading.Thread(target=_play_audio_task, daemon=True)
        audio_thread.start()
        # Temporarily disable action sequence:
        # self.client.execute_action(session_id=session_id, action=self.ActionName.HEART)
        # self.client.execute_action(session_id=session_id, action=self.ActionName.RECOVERY_STAND)

        # Turn in place toward the detected person (yaw only, no x/y motion).
        try:
            det = self.client.detect_once(
                session_id=session_id,
                config=self.DetectionConfig(
                    model_path=self.model_path,
                    conf_thres=self.conf_thres,
                    iou_thres=self.iou_thres,
                    max_det=self.max_det,
                ),
            )
            best = None
            best_score = -1.0
            for d in det.detections:
                label = (d.label or "").lower()
                if d.class_id != 0 and label != "person":
                    continue
                score = float(d.score)
                if score > best_score:
                    best = d
                    best_score = score

            if best is not None:
                w = float(best.bbox.w)
                x = float(best.bbox.x)
                w_ratio = float(getattr(best, "bbox_w_ratio", 0.0))
                if w > 1e-6 and w_ratio > 1e-6:
                    img_w = w / w_ratio
                    if img_w > 1e-6:
                        person_cx = x + 0.5 * w
                        center_err = (person_cx - 0.5 * img_w) / (0.5 * img_w)
                        if abs(center_err) >= 0.08:
                            # Invert sign so robot turns toward person.
                            # Apply distance-aware gain by bbox width ratio:
                            # around w_ratio=0.25 -> yaw gain is reduced to about half.
                            size_scale = min(1.0, 0.125 / max(w_ratio, 1e-6))
                            vyaw_cmd = -center_err * 3.2 * size_scale
                            vyaw = max(-3.2, min(3.2, vyaw_cmd))
                            if 0.0 < abs(vyaw) < 0.35:
                                vyaw = 0.35 if vyaw > 0.0 else -0.35
                            self.client.execute_action(
                                session_id=session_id,
                                action=self.ActionName.MOVE,
                                vx=0.0,
                                vy=0.0,
                                vyaw=vyaw,
                            )
                            time.sleep(0.45)
                            self.client.execute_action(session_id=session_id, action=self.ActionName.STOP_MOVE)
                            print(
                                f"[autoplay] turn-to-person: center_err={center_err:.3f}, "
                                f"bbox_w_ratio={w_ratio:.3f}, size_scale={size_scale:.3f}, vyaw={vyaw:.3f}",
                                flush=True,
                            )
        except Exception as exc:  # pylint: disable=broad-except
            print(f"[autoplay] turn-to-person skipped: {exc}", flush=True)

        audio_thread.join(timeout=max(0.0, self.timeout_sec))
        if audio_err:
            raise RuntimeError(f"audio playback failed: {audio_err[0]}")
        if audio_thread.is_alive():
            print("[autoplay] audio request still in flight after action sequence", flush=True)
        print("[autoplay] action sequence disabled; audio+turn executed", flush=True)

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
                if now >= self._next_trigger_ts and self._has_person(
                    det, self.person_conf_min, self.person_width_ratio_min
                ):
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
