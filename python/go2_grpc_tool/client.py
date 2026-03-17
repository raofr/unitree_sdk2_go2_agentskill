from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Iterator

import grpc

from go2_grpc_tool.generated import go2_sport_pb2
from go2_grpc_tool.generated import go2_sport_pb2_grpc


class ActionName(str, Enum):
    DAMP = "ACTION_DAMP"
    BALANCE_STAND = "ACTION_BALANCE_STAND"
    STOP_MOVE = "ACTION_STOP_MOVE"
    STAND_UP = "ACTION_STAND_UP"
    STAND_DOWN = "ACTION_STAND_DOWN"
    RECOVERY_STAND = "ACTION_RECOVERY_STAND"
    EULER = "ACTION_EULER"
    MOVE = "ACTION_MOVE"
    SIT = "ACTION_SIT"
    RISE_SIT = "ACTION_RISE_SIT"
    SPEED_LEVEL = "ACTION_SPEED_LEVEL"
    HELLO = "ACTION_HELLO"
    STRETCH = "ACTION_STRETCH"
    SWITCH_JOYSTICK = "ACTION_SWITCH_JOYSTICK"
    CONTENT = "ACTION_CONTENT"
    HEART = "ACTION_HEART"
    POSE = "ACTION_POSE"
    SCRAPE = "ACTION_SCRAPE"
    FRONT_FLIP = "ACTION_FRONT_FLIP"
    FRONT_JUMP = "ACTION_FRONT_JUMP"
    FRONT_POUNCE = "ACTION_FRONT_POUNCE"
    DANCE1 = "ACTION_DANCE1"
    DANCE2 = "ACTION_DANCE2"
    LEFT_FLIP = "ACTION_LEFT_FLIP"
    BACK_FLIP = "ACTION_BACK_FLIP"
    HAND_STAND = "ACTION_HAND_STAND"
    FREE_WALK = "ACTION_FREE_WALK"
    FREE_BOUND = "ACTION_FREE_BOUND"
    FREE_JUMP = "ACTION_FREE_JUMP"
    FREE_AVOID = "ACTION_FREE_AVOID"
    CLASSIC_WALK = "ACTION_CLASSIC_WALK"
    WALK_UPRIGHT = "ACTION_WALK_UPRIGHT"
    CROSS_STEP = "ACTION_CROSS_STEP"
    AUTO_RECOVER_SET = "ACTION_AUTO_RECOVER_SET"
    AUTO_RECOVER_GET = "ACTION_AUTO_RECOVER_GET"
    STATIC_WALK = "ACTION_STATIC_WALK"
    TROT_RUN = "ACTION_TROT_RUN"
    ECONOMIC_GAIT = "ACTION_ECONOMIC_GAIT"
    SWITCH_AVOID_MODE = "ACTION_SWITCH_AVOID_MODE"


@dataclass
class SessionInfo:
    session_id: str
    expires_at_ms: int


@dataclass
class DetectionConfig:
    model_path: str = ""
    conf_thres: float = 0.25
    iou_thres: float = 0.45
    max_det: int = 100


@dataclass
class AudioUploadConfig:
    mime: str = "audio/opus"
    sample_rate: int = 48000
    channels: int = 1
    volume: float = 1.0
    loop: bool = False
    request_id: str = ""


class Go2SportClient:
    def __init__(self, endpoint: str = "127.0.0.1:50051", timeout_sec: float = 5.0):
        self._endpoint = endpoint
        self._timeout = timeout_sec
        self._channel = grpc.insecure_channel(endpoint)
        self._stub = go2_sport_pb2_grpc.Go2SportServiceStub(self._channel)

        self._detect_once_rpc = self._channel.unary_unary(
            "/go2.sport.v1.Go2SportService/DetectObjects",
            request_serializer=go2_sport_pb2.DetectObjectsRequest.SerializeToString,
            response_deserializer=go2_sport_pb2.DetectObjectsResponse.FromString,
        )
        self._start_detection_rpc = self._channel.unary_unary(
            "/go2.sport.v1.Go2SportService/StartDetection",
            request_serializer=go2_sport_pb2.StartDetectionRequest.SerializeToString,
            response_deserializer=go2_sport_pb2.StartDetectionResponse.FromString,
        )
        self._stop_detection_rpc = self._channel.unary_unary(
            "/go2.sport.v1.Go2SportService/StopDetection",
            request_serializer=go2_sport_pb2.StopDetectionRequest.SerializeToString,
            response_deserializer=go2_sport_pb2.StopDetectionResponse.FromString,
        )
        self._subscribe_detections_rpc = self._channel.unary_stream(
            "/go2.sport.v1.Go2SportService/SubscribeDetections",
            request_serializer=go2_sport_pb2.SubscribeDetectionsRequest.SerializeToString,
            response_deserializer=go2_sport_pb2.DetectionEvent.FromString,
        )
        self._upload_and_play_audio_rpc = self._channel.unary_unary(
            "/go2.sport.v1.Go2SportService/UploadAndPlayAudio",
            request_serializer=go2_sport_pb2.UploadAndPlayAudioRequest.SerializeToString,
            response_deserializer=go2_sport_pb2.UploadAndPlayAudioResponse.FromString,
        )
        self._get_audio_status_rpc = self._channel.unary_unary(
            "/go2.sport.v1.Go2SportService/GetAudioStatus",
            request_serializer=go2_sport_pb2.GetAudioStatusRequest.SerializeToString,
            response_deserializer=go2_sport_pb2.GetAudioStatusResponse.FromString,
        )
        self._stop_audio_playback_rpc = self._channel.unary_unary(
            "/go2.sport.v1.Go2SportService/StopAudioPlayback",
            request_serializer=go2_sport_pb2.StopAudioPlaybackRequest.SerializeToString,
            response_deserializer=go2_sport_pb2.StopAudioPlaybackResponse.FromString,
        )

    def open_session(
        self,
        owner: str,
        session_name: str = "",
        ttl_sec: int = 30,
        parallel: bool = False,
    ) -> SessionInfo:
        req = go2_sport_pb2.OpenSessionRequest(
            owner=owner,
            session_name=session_name,
            ttl_sec=ttl_sec,
            parallel=parallel,
        )
        resp = self._stub.OpenSession(req, timeout=self._timeout)
        self._ensure_ok(resp.code, resp.message)
        return SessionInfo(session_id=resp.session_id, expires_at_ms=resp.expires_at_ms)

    def heartbeat(self, session_id: str) -> int:
        resp = self._stub.Heartbeat(
            go2_sport_pb2.HeartbeatRequest(session_id=session_id),
            timeout=self._timeout,
        )
        self._ensure_ok(resp.code, resp.message)
        return resp.expires_at_ms

    def close_session(self, session_id: str) -> None:
        resp = self._stub.CloseSession(
            go2_sport_pb2.CloseSessionRequest(session_id=session_id),
            timeout=self._timeout,
        )
        self._ensure_ok(resp.code, resp.message)

    def force_close_owner_sessions(self, owner: str, keep_parallel_sessions: bool = True) -> int:
        resp = self._stub.ForceCloseOwnerSessions(
            go2_sport_pb2.ForceCloseOwnerSessionsRequest(
                owner=owner,
                keep_parallel_sessions=keep_parallel_sessions,
            ),
            timeout=self._timeout,
        )
        self._ensure_ok(resp.code, resp.message)
        return int(resp.closed_count)

    def execute_action(
        self,
        session_id: str,
        action: ActionName,
        *,
        vx: float = 0.0,
        vy: float = 0.0,
        vyaw: float = 0.0,
        roll: float = 0.0,
        pitch: float = 0.0,
        yaw: float = 0.0,
        level: int = 0,
        flag: bool = False,
    ) -> go2_sport_pb2.ExecuteActionResponse:
        req = go2_sport_pb2.ExecuteActionRequest(
            session_id=session_id,
            action=go2_sport_pb2.Action.Value(action.value),
            vx=vx,
            vy=vy,
            vyaw=vyaw,
            roll=roll,
            pitch=pitch,
            yaw=yaw,
            level=level,
            flag=flag,
        )
        resp = self._stub.ExecuteAction(req, timeout=self._timeout)
        self._ensure_ok(resp.code, resp.message)
        return resp

    def get_server_status(self) -> go2_sport_pb2.GetServerStatusResponse:
        resp = self._stub.GetServerStatus(
            go2_sport_pb2.GetServerStatusRequest(),
            timeout=self._timeout,
        )
        self._ensure_ok(resp.code, resp.message)
        return resp

    def detect_once(
        self,
        *,
        session_id: str,
        config: DetectionConfig,
    ) -> go2_sport_pb2.DetectObjectsResponse:
        req = go2_sport_pb2.DetectObjectsRequest(
            session_id=session_id,
            model_path=config.model_path,
            conf_thres=float(config.conf_thres),
            iou_thres=float(config.iou_thres),
            max_det=int(config.max_det),
        )
        resp = self._detect_once_rpc(req, timeout=self._timeout)
        self._ensure_ok(resp.code, resp.message)
        return resp

    def start_detection(
        self,
        *,
        session_id: str,
        stream_id: str,
        frame_skip: int,
        fps_limit: int,
        config: DetectionConfig,
    ) -> go2_sport_pb2.StartDetectionResponse:
        req = go2_sport_pb2.StartDetectionRequest(
            session_id=session_id,
            stream_id=stream_id,
            model_path=config.model_path,
            conf_thres=float(config.conf_thres),
            iou_thres=float(config.iou_thres),
            max_det=int(config.max_det),
            frame_skip=int(frame_skip),
            fps_limit=int(fps_limit),
        )
        resp = self._start_detection_rpc(req, timeout=self._timeout)
        self._ensure_ok(resp.code, resp.message)
        return resp

    def stop_detection(self, *, session_id: str, stream_id: str) -> go2_sport_pb2.StopDetectionResponse:
        req = go2_sport_pb2.StopDetectionRequest(session_id=session_id, stream_id=stream_id)
        resp = self._stop_detection_rpc(req, timeout=self._timeout)
        self._ensure_ok(resp.code, resp.message)
        return resp

    def subscribe_detections(self, *, session_id: str, stream_id: str) -> Iterator[go2_sport_pb2.DetectionEvent]:
        req = go2_sport_pb2.SubscribeDetectionsRequest(session_id=session_id, stream_id=stream_id)
        return self._subscribe_detections_rpc(req, timeout=self._timeout)

    def upload_and_play_audio(
        self,
        *,
        session_id: str,
        stream_id: str,
        audio_bytes: bytes,
        config: AudioUploadConfig,
    ) -> go2_sport_pb2.UploadAndPlayAudioResponse:
        req = go2_sport_pb2.UploadAndPlayAudioRequest(
            session_id=session_id,
            stream_id=stream_id,
            audio_bytes=audio_bytes,
            mime=config.mime,
            sample_rate=int(config.sample_rate),
            channels=int(config.channels),
            volume=float(config.volume),
            loop=bool(config.loop),
            request_id=config.request_id,
        )
        resp = self._upload_and_play_audio_rpc(req, timeout=self._timeout)
        self._ensure_ok(resp.code, resp.message)
        return resp

    def get_audio_status(self, *, session_id: str) -> go2_sport_pb2.GetAudioStatusResponse:
        req = go2_sport_pb2.GetAudioStatusRequest(session_id=session_id)
        resp = self._get_audio_status_rpc(req, timeout=self._timeout)
        self._ensure_ok(resp.code, resp.message)
        return resp

    def stop_audio_playback(self, *, session_id: str, stream_id: str = "") -> go2_sport_pb2.StopAudioPlaybackResponse:
        req = go2_sport_pb2.StopAudioPlaybackRequest(session_id=session_id, stream_id=stream_id)
        resp = self._stop_audio_playback_rpc(req, timeout=self._timeout)
        self._ensure_ok(resp.code, resp.message)
        return resp

    @staticmethod
    def _ensure_ok(code: int, message: str) -> None:
        if code != 0:
            raise RuntimeError(f"gRPC request failed code={code}: {message}")
