#include <iostream>
#include <string>

#include "go2_sport.pb.h"

int main() {
  using go2::sport::v1::DetectObjectsRequest;
  using go2::sport::v1::DetectObjectsResponse;
  using go2::sport::v1::DetectionEvent;
  using go2::sport::v1::GetAudioStatusResponse;
  using go2::sport::v1::StartDetectionRequest;
  using go2::sport::v1::StopAudioPlaybackRequest;
  using go2::sport::v1::StopDetectionRequest;
  using go2::sport::v1::SubscribeDetectionsRequest;
  using go2::sport::v1::UploadAndPlayAudioRequest;

  DetectObjectsRequest detect_req;
  detect_req.set_session_id("session-local");
  detect_req.set_model_path("/tmp/yolo26.engine");
  detect_req.set_conf_thres(0.25f);
  detect_req.set_iou_thres(0.45f);
  detect_req.set_max_det(64);

  std::string wire;
  if (!detect_req.SerializeToString(&wire)) {
    std::cerr << "DetectObjectsRequest serialization failed" << std::endl;
    return 1;
  }

  DetectObjectsRequest detect_req_roundtrip;
  if (!detect_req_roundtrip.ParseFromString(wire)) {
    std::cerr << "DetectObjectsRequest parse failed" << std::endl;
    return 2;
  }
  if (detect_req_roundtrip.session_id() != "session-local" ||
      detect_req_roundtrip.model_path() != "/tmp/yolo26.engine") {
    std::cerr << "DetectObjectsRequest roundtrip mismatch" << std::endl;
    return 3;
  }

  StartDetectionRequest start_req;
  start_req.set_session_id("session-local");
  start_req.set_stream_id("stream-main");
  start_req.set_frame_skip(1);
  start_req.set_fps_limit(10);
  if (start_req.stream_id().empty()) {
    std::cerr << "StartDetectionRequest stream_id should not be empty" << std::endl;
    return 4;
  }

  StopDetectionRequest stop_req;
  stop_req.set_session_id("session-local");
  stop_req.set_stream_id("stream-main");

  SubscribeDetectionsRequest sub_req;
  sub_req.set_session_id("session-local");
  sub_req.set_stream_id("stream-main");

  DetectObjectsResponse detect_resp;
  detect_resp.set_code(0);
  detect_resp.set_message("ok");
  detect_resp.set_frame_id(7);
  detect_resp.set_timestamp_ms(123456);
  auto* det = detect_resp.add_detections();
  det->set_class_id(1);
  det->set_label("person");
  det->set_score(0.9f);
  det->mutable_bbox()->set_x(0.1f);
  det->mutable_bbox()->set_y(0.2f);
  det->mutable_bbox()->set_w(0.3f);
  det->mutable_bbox()->set_h(0.4f);

  DetectionEvent event;
  event.set_code(0);
  event.set_message("ok");
  event.set_stream_id("stream-main");
  event.set_frame_id(9);
  event.set_timestamp_ms(654321);
  *event.add_detections() = detect_resp.detections(0);

  if (event.detections_size() != 1 || event.detections(0).label() != "person") {
    std::cerr << "DetectionEvent message check failed" << std::endl;
    return 5;
  }

  UploadAndPlayAudioRequest audio_req;
  audio_req.set_session_id("session-local");
  audio_req.set_stream_id("audio-main");
  audio_req.set_mime("audio/opus");
  audio_req.set_sample_rate(48000);
  audio_req.set_channels(1);
  audio_req.set_volume(0.8f);
  audio_req.set_loop(false);
  audio_req.set_request_id("req-123");
  audio_req.set_audio_bytes("audio-bytes");
  if (!audio_req.SerializeToString(&wire)) {
    std::cerr << "UploadAndPlayAudioRequest serialization failed" << std::endl;
    return 6;
  }
  UploadAndPlayAudioRequest audio_req_roundtrip;
  if (!audio_req_roundtrip.ParseFromString(wire) || audio_req_roundtrip.audio_bytes().empty()) {
    std::cerr << "UploadAndPlayAudioRequest parse failed" << std::endl;
    return 7;
  }

  StopAudioPlaybackRequest audio_stop;
  audio_stop.set_session_id("session-local");
  audio_stop.set_stream_id("audio-main");

  GetAudioStatusResponse audio_status;
  audio_status.set_code(0);
  audio_status.set_message("ok");
  audio_status.set_connected(true);
  audio_status.set_playing(true);
  audio_status.set_stream_id("audio-main");
  audio_status.set_queued_items(1);
  if (!audio_status.connected() || audio_status.stream_id() != "audio-main") {
    std::cerr << "GetAudioStatusResponse check failed" << std::endl;
    return 8;
  }

  std::cout << "go2 grpc proto smoke test passed" << std::endl;
  return 0;
}
