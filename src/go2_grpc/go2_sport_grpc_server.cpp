#include <atomic>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <unistd.h>

#include <arpa/inet.h>
#include <grpcpp/grpcpp.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <unitree/robot/channel/channel_factory.hpp>
#include <unitree/robot/go2/obstacles_avoid/obstacles_avoid_client.hpp>
#include <unitree/robot/go2/sport/sport_client.hpp>
#include <unitree/robot/go2/video/video_client.hpp>

#if __has_include(<NvInfer.h>) && __has_include(<cuda_runtime_api.h>)
#define GO2_HAS_TENSORRT 1
#include <NvInfer.h>
#include <cuda_runtime_api.h>
#else
#define GO2_HAS_TENSORRT 0
#endif

#if __has_include(<opencv2/imgcodecs.hpp>) && __has_include(<opencv2/imgproc.hpp>) && defined(GO2_HAS_OPENCV_LINK) && GO2_HAS_OPENCV_LINK
#define GO2_HAS_OPENCV 1
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#else
#define GO2_HAS_OPENCV 0
#endif

#if __has_include(<gst/gst.h>)
#define GO2_HAS_GSTREAMER 1
#include <gst/gst.h>
#else
#define GO2_HAS_GSTREAMER 0
#endif

#if __has_include(<gst/webrtc/webrtc.h>) && __has_include(<gst/sdp/gstsdpmessage.h>)
#define GO2_HAS_GSTREAMER_WEBRTC 1
#include <gst/webrtc/webrtc.h>
#include <gst/sdp/gstsdpmessage.h>
#else
#define GO2_HAS_GSTREAMER_WEBRTC 0
#endif

#if __has_include(<curl/curl.h>)
#define GO2_HAS_CURL 1
#include <curl/curl.h>
#else
#define GO2_HAS_CURL 0
#endif

#if __has_include(<openssl/evp.h>) && __has_include(<openssl/pem.h>) && __has_include(<openssl/rand.h>) && defined(GO2_HAS_OPENSSL_LINK) && GO2_HAS_OPENSSL_LINK
#define GO2_HAS_OPENSSL 1
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#else
#define GO2_HAS_OPENSSL 0
#endif

#include "go2_sport.grpc.pb.h"

using go2::sport::v1::Action;
using go2::sport::v1::CloseSessionRequest;
using go2::sport::v1::CloseSessionResponse;
using go2::sport::v1::DetectObjectsRequest;
using go2::sport::v1::DetectObjectsResponse;
using go2::sport::v1::Detection;
using go2::sport::v1::DetectionEvent;
using go2::sport::v1::ExecuteActionRequest;
using go2::sport::v1::ExecuteActionResponse;
using go2::sport::v1::ForceCloseOwnerSessionsRequest;
using go2::sport::v1::ForceCloseOwnerSessionsResponse;
using go2::sport::v1::GetServerStatusRequest;
using go2::sport::v1::GetServerStatusResponse;
using go2::sport::v1::GetAudioStatusRequest;
using go2::sport::v1::GetAudioStatusResponse;
using go2::sport::v1::Go2SportService;
using go2::sport::v1::HeartbeatRequest;
using go2::sport::v1::HeartbeatResponse;
using go2::sport::v1::OpenSessionRequest;
using go2::sport::v1::OpenSessionResponse;
using go2::sport::v1::UploadAndPlayAudioRequest;
using go2::sport::v1::UploadAndPlayAudioResponse;
using go2::sport::v1::SessionStatus;
using go2::sport::v1::StartDetectionRequest;
using go2::sport::v1::StartDetectionResponse;
using go2::sport::v1::StopAudioPlaybackRequest;
using go2::sport::v1::StopAudioPlaybackResponse;
using go2::sport::v1::StopDetectionRequest;
using go2::sport::v1::StopMicrophoneRequest;
using go2::sport::v1::StopMicrophoneResponse;
using go2::sport::v1::StartMicrophoneRequest;
using go2::sport::v1::StartMicrophoneResponse;
using go2::sport::v1::MicrophoneControl;
using go2::sport::v1::MicrophoneAudio;
using go2::sport::v1::StopDetectionResponse;
using go2::sport::v1::SubscribeDetectionsRequest;

namespace {

constexpr uint32_t kDefaultTtlSec = 30;
constexpr uint32_t kMinTtlSec = 5;
constexpr uint32_t kMaxTtlSec = 300;
constexpr uint16_t kDiscoveryUdpPort = 50052;
constexpr auto kDiscoveryBroadcastInterval = std::chrono::seconds(1);

struct Session {
  std::string id;
  std::string owner;
  std::string name;
  bool parallel{false};
  std::chrono::steady_clock::time_point expires_at;
};

uint64_t NowUnixMs() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count());
}

std::string GenerateSessionId() {
  static thread_local std::mt19937_64 rng(std::random_device{}());
  std::ostringstream oss;
  oss << std::hex << rng() << rng();
  return oss.str();
}

bool IsIpv6LinkLocal(const struct in6_addr& addr) {
  // fe80::/10
  return addr.s6_addr[0] == 0xfe && (addr.s6_addr[1] & 0xc0) == 0x80;
}

std::string ResolveInterfaceIpv4(const std::string& interface_name) {
  struct ifaddrs* ifaddr = nullptr;
  if (getifaddrs(&ifaddr) != 0) {
    return "";
  }

  std::string out;
  for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    if (interface_name != ifa->ifa_name) {
      continue;
    }

    char ip[INET_ADDRSTRLEN] = {0};
    const auto* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
    if (inet_ntop(AF_INET, &(sin->sin_addr), ip, sizeof(ip)) != nullptr) {
      out = ip;
      break;
    }
  }

  freeifaddrs(ifaddr);
  return out;
}

std::string ResolveInterfaceIpv6(const std::string& interface_name) {
  struct ifaddrs* ifaddr = nullptr;
  if (getifaddrs(&ifaddr) != 0) {
    return "";
  }

  std::string global_or_ula_ip;
  std::string link_local_ip;
  for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET6) {
      continue;
    }
    if (interface_name != ifa->ifa_name) {
      continue;
    }

    char ip[INET6_ADDRSTRLEN] = {0};
    const auto* sin6 = reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr);
    if (inet_ntop(AF_INET6, &(sin6->sin6_addr), ip, sizeof(ip)) == nullptr) {
      continue;
    }

    if (IsIpv6LinkLocal(sin6->sin6_addr)) {
      if (link_local_ip.empty()) {
        link_local_ip = std::string(ip) + "%" + interface_name;
      }
    } else if (global_or_ula_ip.empty()) {
      global_or_ula_ip = ip;
    }
  }

  freeifaddrs(ifaddr);
  if (!global_or_ula_ip.empty()) {
    return global_or_ula_ip;
  }
  return link_local_ip;
}

std::string NormalizeListenAddress(const std::string& listen_address) {
  if (listen_address.empty()) {
    return "";
  }
  if (listen_address == "0.0.0.0" || listen_address == "::" || listen_address == "[::]") {
    return "";
  }
  if (listen_address.front() == '[' && listen_address.back() == ']') {
    return listen_address.substr(1, listen_address.size() - 2);
  }
  return listen_address;
}

struct AdvertisedAddresses {
  std::string ipv4;
  std::string ipv6;
};

AdvertisedAddresses ResolveAdvertisedAddresses(const std::string& network_interface,
                                               const std::string& listen_address) {
  AdvertisedAddresses out;
  out.ipv4 = ResolveInterfaceIpv4(network_interface);
  out.ipv6 = ResolveInterfaceIpv6(network_interface);

  const std::string normalized_listen = NormalizeListenAddress(listen_address);
  if (normalized_listen.empty()) {
    return out;
  }
  if (out.ipv4.empty() && normalized_listen.find(':') == std::string::npos) {
    out.ipv4 = normalized_listen;
  }
  if (out.ipv6.empty() && normalized_listen.find(':') != std::string::npos) {
    out.ipv6 = normalized_listen;
  }
  return out;
}

std::string ChooseOrGenerateStreamId(const std::string& stream_id) {
  if (!stream_id.empty()) {
    return stream_id;
  }
  return GenerateSessionId();
}

std::string SanitizeForFileName(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
        c == '-' || c == '.') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  if (out.empty()) {
    out = "frame";
  }
  return out;
}

std::string TrimCopy(const std::string& s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])) != 0) {
    ++b;
  }
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) {
    --e;
  }
  return s.substr(b, e - b);
}

std::vector<std::string> DefaultCoco80Labels() {
  return {
      "person",       "bicycle",     "car",          "motorcycle",   "airplane",   "bus",         "train",
      "truck",        "boat",        "traffic light","fire hydrant", "stop sign",  "parking meter","bench",
      "bird",         "cat",         "dog",          "horse",        "sheep",      "cow",         "elephant",
      "bear",         "zebra",       "giraffe",      "backpack",     "umbrella",   "handbag",     "tie",
      "suitcase",     "frisbee",     "skis",         "snowboard",    "sports ball","kite",        "baseball bat",
      "baseball glove","skateboard", "surfboard",    "tennis racket","bottle",     "wine glass",  "cup",
      "fork",         "knife",       "spoon",        "bowl",         "banana",     "apple",       "sandwich",
      "orange",       "broccoli",    "carrot",       "hot dog",      "pizza",      "donut",       "cake",
      "chair",        "couch",       "potted plant", "bed",          "dining table","toilet",     "tv",
      "laptop",       "mouse",       "remote",       "keyboard",     "cell phone", "microwave",   "oven",
      "toaster",      "sink",        "refrigerator", "book",         "clock",      "vase",        "scissors",
      "teddy bear",   "hair drier",  "toothbrush"};
}

#if GO2_HAS_TENSORRT
class TrtLogger final : public nvinfer1::ILogger {
 public:
  void log(Severity severity, const char* msg) noexcept override {
    if (severity <= Severity::kWARNING) {
      std::cerr << "[TensorRT] " << msg << std::endl;
    }
  }
};
#endif

class YoloTrtEngine {
 public:
  struct DetectConfig {
    float conf_thres{0.25f};
    float iou_thres{0.45f};
    uint32_t max_det{100};
  };

  bool EnsureLoaded(const std::string& model_path, std::string* err) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string resolved = model_path.empty() ? default_model_path_ : model_path;
    if (resolved.empty()) {
      *err = "model path is empty";
      return false;
    }
    if (loaded_ && loaded_model_path_ == resolved) {
      return true;
    }
#if GO2_HAS_TENSORRT
    std::ifstream ifs(resolved, std::ios::binary);
    if (!ifs) {
      *err = "model/engine file not found";
      return false;
    }
    ifs.seekg(0, std::ios::end);
    const std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    if (size <= 0) {
      *err = "invalid engine file size";
      return false;
    }

    std::vector<char> blob(static_cast<size_t>(size));
    if (!ifs.read(blob.data(), size)) {
      *err = "failed to read engine file";
      return false;
    }

    runtime_.reset(nvinfer1::createInferRuntime(logger_));
    if (!runtime_) {
      *err = "createInferRuntime failed";
      return false;
    }
    engine_.reset(runtime_->deserializeCudaEngine(blob.data(), blob.size()));
    if (!engine_) {
      *err = "deserializeCudaEngine failed; only .engine is supported in runtime path";
      return false;
    }
    context_.reset(engine_->createExecutionContext());
    if (!context_) {
      *err = "createExecutionContext failed";
      return false;
    }
    input_index_ = -1;
    output_index_ = -1;
    const int nb = engine_->getNbBindings();
    for (int i = 0; i < nb; ++i) {
      if (engine_->bindingIsInput(i)) {
        input_index_ = i;
      } else if (output_index_ < 0) {
        output_index_ = i;
      }
    }
    if (input_index_ < 0 || output_index_ < 0) {
      *err = "failed to resolve engine input/output bindings";
      return false;
    }
    nvinfer1::Dims in_dims = engine_->getBindingDimensions(input_index_);
    if (in_dims.nbDims != 4) {
      *err = "input tensor must be NCHW 4D";
      return false;
    }
    if (in_dims.d[0] == -1) {
      in_dims.d[0] = 1;
    }
    if (in_dims.d[2] <= 0 || in_dims.d[3] <= 0) {
      *err = "invalid input tensor shape";
      return false;
    }
    if (!context_->setBindingDimensions(input_index_, in_dims)) {
      *err = "setBindingDimensions failed";
      return false;
    }
    input_h_ = in_dims.d[2];
    input_w_ = in_dims.d[3];
    loaded_ = true;
    loaded_model_path_ = resolved;
    return true;
#else
    (void)resolved;
    *err = "TensorRT headers/libraries are unavailable at build time";
    return false;
#endif
  }

  void SetDefaultModelPath(const std::string& path) { default_model_path_ = path; }
  void SetLabels(std::vector<std::string> labels) {
    std::lock_guard<std::mutex> lock(infer_mu_);
    labels_ = std::move(labels);
  }

  std::vector<Detection> Infer(const std::vector<uint8_t>& image_bytes, const DetectConfig& cfg) {
    std::lock_guard<std::mutex> lock(infer_mu_);
#if GO2_HAS_TENSORRT && GO2_HAS_OPENCV
    if (!loaded_ || !engine_ || !context_ || image_bytes.empty()) {
      return {};
    }

    cv::Mat encoded(1, static_cast<int>(image_bytes.size()), CV_8UC1,
                    const_cast<unsigned char*>(image_bytes.data()));
    cv::Mat bgr = cv::imdecode(encoded, cv::IMREAD_COLOR);
    if (bgr.empty()) {
      return {};
    }

    const int src_w = bgr.cols;
    const int src_h = bgr.rows;
    if (src_w <= 0 || src_h <= 0 || input_w_ <= 0 || input_h_ <= 0) {
      return {};
    }

    const float scale = std::min(static_cast<float>(input_w_) / static_cast<float>(src_w),
                                 static_cast<float>(input_h_) / static_cast<float>(src_h));
    const int resized_w = std::max(1, static_cast<int>(std::round(src_w * scale)));
    const int resized_h = std::max(1, static_cast<int>(std::round(src_h * scale)));
    const int pad_x = (input_w_ - resized_w) / 2;
    const int pad_y = (input_h_ - resized_h) / 2;

    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(resized_w, resized_h), 0.0, 0.0, cv::INTER_LINEAR);
    cv::Mat letterbox(input_h_, input_w_, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(letterbox(cv::Rect(pad_x, pad_y, resized_w, resized_h)));
    cv::Mat rgb;
    cv::cvtColor(letterbox, rgb, cv::COLOR_BGR2RGB);

    std::vector<float> input_tensor(static_cast<size_t>(3 * input_h_ * input_w_));
    for (int y = 0; y < input_h_; ++y) {
      const cv::Vec3b* row = rgb.ptr<cv::Vec3b>(y);
      for (int x = 0; x < input_w_; ++x) {
        const cv::Vec3b& p = row[x];
        const size_t idx = static_cast<size_t>(y * input_w_ + x);
        input_tensor[idx] = static_cast<float>(p[0]) / 255.0f;
        input_tensor[static_cast<size_t>(input_h_ * input_w_) + idx] = static_cast<float>(p[1]) / 255.0f;
        input_tensor[static_cast<size_t>(2 * input_h_ * input_w_) + idx] = static_cast<float>(p[2]) / 255.0f;
      }
    }

    void* input_dev = nullptr;
    void* output_dev = nullptr;
    cudaStream_t stream{};
    const size_t input_bytes = input_tensor.size() * sizeof(float);
    if (cudaMalloc(&input_dev, input_bytes) != cudaSuccess) {
      return {};
    }

    nvinfer1::Dims out_dims = context_->getBindingDimensions(output_index_);
    if (out_dims.nbDims < 2) {
      cudaFree(input_dev);
      return {};
    }
    size_t out_count = 1;
    for (int i = 0; i < out_dims.nbDims; ++i) {
      if (out_dims.d[i] <= 0) {
        cudaFree(input_dev);
        return {};
      }
      out_count *= static_cast<size_t>(out_dims.d[i]);
    }
    const size_t out_bytes = out_count * sizeof(float);
    if (cudaMalloc(&output_dev, out_bytes) != cudaSuccess) {
      cudaFree(input_dev);
      return {};
    }
    if (cudaStreamCreate(&stream) != cudaSuccess) {
      cudaFree(input_dev);
      cudaFree(output_dev);
      return {};
    }

    std::vector<void*> bindings(static_cast<size_t>(engine_->getNbBindings()), nullptr);
    bindings[static_cast<size_t>(input_index_)] = input_dev;
    bindings[static_cast<size_t>(output_index_)] = output_dev;

    bool ok = cudaMemcpyAsync(input_dev, input_tensor.data(), input_bytes, cudaMemcpyHostToDevice, stream) ==
              cudaSuccess;
    if (ok) {
      ok = context_->enqueueV2(bindings.data(), stream, nullptr);
    }

    std::vector<float> output(out_count);
    if (ok) {
      ok = cudaMemcpyAsync(output.data(), output_dev, out_bytes, cudaMemcpyDeviceToHost, stream) == cudaSuccess;
    }
    if (ok) {
      ok = cudaStreamSynchronize(stream) == cudaSuccess;
    }

    cudaStreamDestroy(stream);
    cudaFree(input_dev);
    cudaFree(output_dev);
    if (!ok) {
      return {};
    }

    std::vector<Detection> dets;
    dets.reserve(cfg.max_det);
    size_t rows = 0;
    size_t cols = 0;
    if (out_dims.nbDims == 3 && out_dims.d[2] == 6) {
      rows = static_cast<size_t>(out_dims.d[1]);
      cols = 6;
    } else if (out_dims.nbDims == 2 && out_dims.d[1] == 6) {
      rows = static_cast<size_t>(out_dims.d[0]);
      cols = 6;
    } else {
      return {};
    }

    const float inv_scale = scale > 0.0f ? (1.0f / scale) : 1.0f;
    for (size_t i = 0; i < rows; ++i) {
      const size_t off = i * cols;
      const float x1 = output[off + 0];
      const float y1 = output[off + 1];
      const float x2 = output[off + 2];
      const float y2 = output[off + 3];
      const float score = output[off + 4];
      const int class_id = static_cast<int>(std::round(output[off + 5]));
      if (score < cfg.conf_thres) {
        continue;
      }

      float ox1 = (x1 - static_cast<float>(pad_x)) * inv_scale;
      float oy1 = (y1 - static_cast<float>(pad_y)) * inv_scale;
      float ox2 = (x2 - static_cast<float>(pad_x)) * inv_scale;
      float oy2 = (y2 - static_cast<float>(pad_y)) * inv_scale;
      ox1 = std::max(0.0f, std::min(ox1, static_cast<float>(src_w - 1)));
      oy1 = std::max(0.0f, std::min(oy1, static_cast<float>(src_h - 1)));
      ox2 = std::max(0.0f, std::min(ox2, static_cast<float>(src_w - 1)));
      oy2 = std::max(0.0f, std::min(oy2, static_cast<float>(src_h - 1)));
      const float w = std::max(0.0f, ox2 - ox1);
      const float h = std::max(0.0f, oy2 - oy1);
      if (w <= 1.0f || h <= 1.0f) {
        continue;
      }

      Detection d;
      d.set_class_id(class_id);
      if (class_id >= 0 && static_cast<size_t>(class_id) < labels_.size()) {
        d.set_label(labels_[static_cast<size_t>(class_id)]);
      } else {
        d.set_label(std::to_string(class_id));
      }
      d.set_score(score);
      d.mutable_bbox()->set_x(ox1);
      d.mutable_bbox()->set_y(oy1);
      d.mutable_bbox()->set_w(w);
      d.mutable_bbox()->set_h(h);
      dets.push_back(std::move(d));
      if (dets.size() >= cfg.max_det) {
        break;
      }
    }
    return dets;
#else
    (void)image_bytes;
    (void)cfg;
    return {};
#endif
  }

 private:
#if GO2_HAS_TENSORRT
  struct RuntimeDeleter {
    void operator()(nvinfer1::IRuntime* p) const {
      delete p;
    }
  };
  struct EngineDeleter {
    void operator()(nvinfer1::ICudaEngine* p) const {
      delete p;
    }
  };
  struct ContextDeleter {
    void operator()(nvinfer1::IExecutionContext* p) const {
      delete p;
    }
  };

  TrtLogger logger_;
  std::unique_ptr<nvinfer1::IRuntime, RuntimeDeleter> runtime_;
  std::unique_ptr<nvinfer1::ICudaEngine, EngineDeleter> engine_;
  std::unique_ptr<nvinfer1::IExecutionContext, ContextDeleter> context_;
  int input_index_{-1};
  int output_index_{-1};
  int input_w_{0};
  int input_h_{0};
#endif
  std::mutex mu_;
  std::mutex infer_mu_;
  bool loaded_{false};
  std::string loaded_model_path_;
  std::string default_model_path_;
  std::vector<std::string> labels_;
};

struct DetectionStreamState {
  std::string session_id;
  std::string stream_id;
  YoloTrtEngine::DetectConfig cfg;
  std::string model_path;
  uint32_t frame_skip{0};
  uint32_t fps_limit{5};
  std::atomic<bool> stop{false};
  std::mutex mu;
  std::condition_variable cv;
  std::deque<DetectionEvent> queue;
  std::thread worker;
};

class AudioWebRtcManager {
 public:
  struct Item {
    std::string stream_id;
    std::string request_id;
    std::string mime;
    uint32_t sample_rate{0};
    uint32_t channels{0};
    float volume{1.0f};
    bool loop{false};
    std::vector<uint8_t> audio_bytes;
  };

  explicit AudioWebRtcManager(const std::string& go2_host_ip) : go2_host_ip_(go2_host_ip) {
#if GO2_HAS_GSTREAMER
    int argc = 0;
    char** argv = nullptr;
    gst_init(&argc, &argv);
    gst_available_ = true;
#endif
    const char* offer_cmd = std::getenv("GO2_WEBRTC_SIGNAL_OFFER_CMD");
    const char* ice_cmd = std::getenv("GO2_WEBRTC_SIGNAL_ICE_CMD");
    const char* answer_cmd = std::getenv("GO2_WEBRTC_SIGNAL_ANSWER_CMD");
    if (offer_cmd != nullptr) {
      signal_offer_cmd_ = offer_cmd;
    }
    if (ice_cmd != nullptr) {
      signal_ice_cmd_ = ice_cmd;
    }
    if (answer_cmd != nullptr) {
      signal_answer_cmd_ = answer_cmd;
    }
    const char* signal_mode = std::getenv("GO2_WEBRTC_SIGNAL_MODE");
    if (signal_mode != nullptr) {
      signal_mode_ = signal_mode;
    }
  }

  ~AudioWebRtcManager() { Stop(); }

  void Start() {
    if (started_.exchange(true)) {
      return;
    }
    stop_.store(false);
    worker_ = std::thread([this]() { RunLoop(); });
  }

  void Stop() {
    stop_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
#if GO2_HAS_GSTREAMER
    if (pipeline_) {
      gst_element_set_state(pipeline_, GST_STATE_NULL);
      if (appsrc_ != nullptr) {
        gst_object_unref(appsrc_);
        appsrc_ = nullptr;
      }
      if (appsink_ != nullptr) {
        gst_object_unref(appsink_);
        appsink_ = nullptr;
      }
      if (rtpopusdepay_ != nullptr) {
        gst_object_unref(rtpopusdepay_);
        rtpopusdepay_ = nullptr;
      }
      if (webrtc_ != nullptr) {
        gst_object_unref(webrtc_);
        webrtc_ = nullptr;
      }
      gst_object_unref(pipeline_);
      pipeline_ = nullptr;
    }
#endif
  }

  bool Enqueue(Item item, std::string* err) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!gst_available_) {
      *err = "gstreamer is unavailable at build time";
      return false;
    }
    if (!connected_) {
      *err = "webrtc peer is not connected (waiting signaling/answer)";
      return false;
    }
    if (item.audio_bytes.empty()) {
      *err = "audio_bytes is empty";
      return false;
    }
    queue_.push_back(std::move(item));
    cv_.notify_all();
    return true;
  }

  bool StopPlayback(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(mu_);
    active_stream_id_ = stream_id;
    playing_ = false;
    queue_.clear();
    return true;
  }

  void FillStatus(GetAudioStatusResponse* resp) {
    std::lock_guard<std::mutex> lock(mu_);
    resp->set_connected(connected_);
    resp->set_playing(playing_);
    resp->set_stream_id(active_stream_id_);
    resp->set_queued_items(static_cast<uint32_t>(queue_.size()));
    resp->set_last_error_ts_ms(last_error_ts_ms_);
    resp->set_last_error(last_error_);
  }

  void SetAudioCaptureCallback(
      std::function<void(const std::string&, const std::vector<uint8_t>&, uint64_t, bool)> callback) {
    audio_capture_callback_ = std::move(callback);
  }

 private:
  void SetError(const std::string& err) {
    last_error_ = err;
    last_error_ts_ms_ = NowUnixMs();
    connected_ = false;
  }

  static void OnNegotiationNeededThunk(GstElement*, gpointer user_data) {
#if GO2_HAS_GSTREAMER_WEBRTC
    static_cast<AudioWebRtcManager*>(user_data)->OnNegotiationNeeded();
#else
    (void)user_data;
#endif
  }

  static void OnIceCandidateThunk(GstElement*, guint mline_index, gchar* candidate, gpointer user_data) {
#if GO2_HAS_GSTREAMER_WEBRTC
    static_cast<AudioWebRtcManager*>(user_data)->OnIceCandidate(mline_index, candidate);
#else
    (void)mline_index;
    (void)candidate;
    (void)user_data;
#endif
  }

#if GO2_HAS_GSTREAMER_WEBRTC
  static void OnCreateOfferPromiseResolvedThunk(GstPromise* promise, gpointer user_data) {
    static_cast<AudioWebRtcManager*>(user_data)->OnCreateOfferPromiseResolved(promise);
  }

  static GstFlowReturn OnAppsinkNewSampleThunk(GstElement* appsink, gpointer user_data) {
    return static_cast<AudioWebRtcManager*>(user_data)->OnAppsinkNewSample(appsink);
  }

  GstFlowReturn OnAppsinkNewSample(GstElement* appsink) {
    if (!audio_capture_callback_) {
      return GST_FLOW_OK;
    }
    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));
    if (!sample) {
      return GST_FLOW_OK;
    }
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (buffer) {
      GstMapInfo map;
      if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        std::vector<uint8_t> data(map.data, map.data + map.size);
        uint64_t timestamp_ms = static_cast<uint64_t>(GST_BUFFER_PTS(buffer)) / 1000000;
        audio_capture_callback_(active_stream_id_, data, timestamp_ms, false);
        gst_buffer_unmap(buffer, &map);
      }
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }

  static void OnPadAddedThunk(GstElement* src, GstPad* new_pad, gpointer user_data) {
    static_cast<AudioWebRtcManager*>(user_data)->OnPadAdded(src, new_pad);
  }

  static GstPadProbeReturn OnRtpopusdepaySrcProbe(GstPad*, GstPadProbeInfo*, gpointer user_data) {
    auto* self = static_cast<AudioWebRtcManager*>(user_data);
    self->LinkRtpopusdepayToQueueRecv();
    return GST_PAD_PROBE_REMOVE;
  }

  void OnPadAdded(GstElement* src, GstPad* new_pad) {
    (void)src;
    if (!rtpopusdepay_) {
      return;
    }
    GstPad* sink_pad = gst_element_get_static_pad(rtpopusdepay_, "sink");
    if (!sink_pad) {
      return;
    }
    // Check if pad is already linked
    if (gst_pad_is_linked(sink_pad)) {
      gst_object_unref(sink_pad);
      return;
    }
    // Check if this is an audio pad (check caps)
    GstCaps* caps = gst_pad_query_caps(new_pad, nullptr);
    if (!caps) {
      gst_object_unref(sink_pad);
      return;
    }
    // Check if it's audio
    bool is_audio = false;
    GstStructure* structure = gst_caps_get_structure(caps, 0);
    if (structure) {
      const gchar* media = gst_structure_get_string(structure, "media");
      if (media && strcmp(media, "audio") == 0) {
        is_audio = true;
      }
    }
    gst_caps_unref(caps);
    if (!is_audio) {
      gst_object_unref(sink_pad);
      return;
    }
    // Link the pads
    GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
    gst_object_unref(sink_pad);
    if (ret != GST_PAD_LINK_OK) {
      std::cerr << "Failed to link audio pad: " << gst_pad_link_return_get_name(ret) << std::endl;
      return;
    }
    std::cerr << "Audio pad linked to rtpopusdepay" << std::endl;

    // Now link rtpopusdepay src to queue_recv
    // Use a probe on the src pad to know when it's ready
    GstPad* rtpopusdepay_src = gst_element_get_static_pad(rtpopusdepay_, "src");
    if (rtpopusdepay_src) {
      // Check if src pad is already available and linked
      if (gst_pad_is_linked(rtpopusdepay_src)) {
        gst_object_unref(rtpopusdepay_src);
        return;
      }
      // Install a probe to complete the link when src pad becomes available
      gst_pad_add_probe(rtpopusdepay_src, GST_PAD_PROBE_TYPE_PAD_NEGOTIATION,
                        OnRtpopusdepaySrcProbe, this, nullptr);
      gst_object_unref(rtpopusdepay_src);
    }
  }

  void LinkRtpopusdepayToQueueRecv() {
    if (!rtpopusdepay_ || !pipeline_) {
      return;
    }
    GstElement* queue_recv = gst_bin_get_by_name(GST_BIN(pipeline_), "queue_recv");
    if (!queue_recv) {
      return;
    }
    GstPad* src = gst_element_get_static_pad(rtpopusdepay_, "src");
    GstPad* sink = gst_element_get_static_pad(queue_recv, "sink");
    if (src && sink) {
      if (!gst_pad_is_linked(src)) {
        GstPadLinkReturn ret = gst_pad_link(src, sink);
        if (ret == GST_PAD_LINK_OK) {
          std::cerr << "rtpopusdepay linked to queue_recv" << std::endl;
        }
      }
    }
    if (src) gst_object_unref(src);
    if (sink) gst_object_unref(sink);
    gst_object_unref(queue_recv);
  }
#endif

  std::string WritePayloadTemp(const std::string& payload) {
    char tmpl[] = "/tmp/go2_webrtc_payload_XXXXXX";
    const int fd = mkstemp(tmpl);
    if (fd < 0) {
      return "";
    }
    FILE* f = fdopen(fd, "wb");
    if (f == nullptr) {
      close(fd);
      return "";
    }
    if (!payload.empty()) {
      fwrite(payload.data(), 1, payload.size(), f);
    }
    fclose(f);
    return std::string(tmpl);
  }

  bool RunHook(const std::string& cmd, const std::string& event, const std::string& payload,
               std::string* out, std::string* err) {
    if (cmd.empty()) {
      *err = "signaling hook command is empty";
      return false;
    }
    const std::string payload_file = WritePayloadTemp(payload);
    if (payload_file.empty()) {
      *err = "failed to create temp payload file";
      return false;
    }
    std::string full =
        cmd + " " + event + " " + go2_host_ip_ + " " + payload_file + " 2>/tmp/go2_webrtc_hook.err";

    FILE* pipe = popen(full.c_str(), "r");
    if (pipe == nullptr) {
      unlink(payload_file.c_str());
      *err = "failed to start signaling hook";
      return false;
    }
    char buf[512];
    std::string output;
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
      output += buf;
    }
    const int rc = pclose(pipe);
    unlink(payload_file.c_str());
    if (rc != 0) {
      *err = "signaling hook failed";
      return false;
    }
    *out = output;
    return true;
  }

  static std::string JsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 32);
    for (char c : in) {
      switch (c) {
        case '"':
          out += "\\\"";
          break;
        case '\\':
          out += "\\\\";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          out.push_back(c);
          break;
      }
    }
    return out;
  }

  static bool ExtractJsonStringField(const std::string& json, const std::string& key, std::string* out) {
    const std::string marker = "\"" + key + "\"";
    const size_t k = json.find(marker);
    if (k == std::string::npos) {
      return false;
    }
    size_t colon = json.find(':', k + marker.size());
    if (colon == std::string::npos) {
      return false;
    }
    size_t first_quote = json.find('"', colon + 1);
    if (first_quote == std::string::npos) {
      return false;
    }
    std::string v;
    for (size_t i = first_quote + 1; i < json.size(); ++i) {
      const char c = json[i];
      if (c == '"' && json[i - 1] != '\\') {
        *out = v;
        return true;
      }
      v.push_back(c);
    }
    return false;
  }

  static std::string Base64Decode(const std::string& in) {
#if GO2_HAS_OPENSSL
    if (in.empty()) {
      return "";
    }
    std::string cleaned;
    cleaned.reserve(in.size());
    for (char c : in) {
      if (c != '\n' && c != '\r' && c != ' ' && c != '\t') {
        cleaned.push_back(c);
      }
    }
    std::string out((cleaned.size() * 3) / 4 + 4, '\0');
    int len = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                              reinterpret_cast<const unsigned char*>(cleaned.data()),
                              static_cast<int>(cleaned.size()));
    if (len < 0) {
      return "";
    }
    size_t pad = 0;
    if (!cleaned.empty() && cleaned.back() == '=') {
      pad++;
      if (cleaned.size() > 1 && cleaned[cleaned.size() - 2] == '=') {
        pad++;
      }
    }
    out.resize(static_cast<size_t>(len) - pad);
    return out;
#else
    (void)in;
    return "";
#endif
  }

  static std::string Base64Encode(const std::string& in) {
#if GO2_HAS_OPENSSL
    if (in.empty()) {
      return "";
    }
    std::string out(((in.size() + 2) / 3) * 4, '\0');
    int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                              reinterpret_cast<const unsigned char*>(in.data()),
                              static_cast<int>(in.size()));
    if (len < 0) {
      return "";
    }
    out.resize(static_cast<size_t>(len));
    return out;
#else
    (void)in;
    return "";
#endif
  }

  static std::string GenerateAesKeyHex32() {
#if GO2_HAS_OPENSSL
    unsigned char r[16];
    if (RAND_bytes(r, sizeof(r)) != 1) {
      return "";
    }
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (unsigned char b : r) {
      out.push_back(hex[(b >> 4) & 0xF]);
      out.push_back(hex[b & 0xF]);
    }
    return out;
#else
    return "";
#endif
  }

  static std::string AesEncryptEcbPkcs5Base64(const std::string& plain, const std::string& key32) {
#if GO2_HAS_OPENSSL
    if (key32.size() != 32) {
      return "";
    }
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
      return "";
    }
    std::string cipher(plain.size() + 32, '\0');
    int out1 = 0;
    int out2 = 0;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr,
                           reinterpret_cast<const unsigned char*>(key32.data()), nullptr) != 1 ||
        EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(cipher.data()), &out1,
                          reinterpret_cast<const unsigned char*>(plain.data()),
                          static_cast<int>(plain.size())) != 1 ||
        EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(cipher.data()) + out1, &out2) != 1) {
      EVP_CIPHER_CTX_free(ctx);
      return "";
    }
    EVP_CIPHER_CTX_free(ctx);
    cipher.resize(static_cast<size_t>(out1 + out2));
    return Base64Encode(cipher);
#else
    (void)plain;
    (void)key32;
    return "";
#endif
  }

  static std::string AesDecryptEcbPkcs5Base64(const std::string& b64, const std::string& key32) {
#if GO2_HAS_OPENSSL
    if (key32.size() != 32) {
      return "";
    }
    const std::string cipher = Base64Decode(b64);
    if (cipher.empty()) {
      return "";
    }
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
      return "";
    }
    std::string plain(cipher.size() + 16, '\0');
    int out1 = 0;
    int out2 = 0;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr,
                           reinterpret_cast<const unsigned char*>(key32.data()), nullptr) != 1 ||
        EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plain.data()), &out1,
                          reinterpret_cast<const unsigned char*>(cipher.data()),
                          static_cast<int>(cipher.size())) != 1 ||
        EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plain.data()) + out1, &out2) != 1) {
      EVP_CIPHER_CTX_free(ctx);
      return "";
    }
    EVP_CIPHER_CTX_free(ctx);
    plain.resize(static_cast<size_t>(out1 + out2));
    return plain;
#else
    (void)b64;
    (void)key32;
    return "";
#endif
  }

  static std::string RsaEncryptPkcs1v15Base64(const std::string& plain,
                                              const std::string& public_key_pem_base64) {
#if GO2_HAS_OPENSSL
    const std::string key_data = Base64Decode(public_key_pem_base64);
    if (key_data.empty()) {
      return "";
    }

    EVP_PKEY* pkey = nullptr;
    {
      BIO* bio = BIO_new_mem_buf(key_data.data(), static_cast<int>(key_data.size()));
      if (bio == nullptr) {
        return "";
      }
      pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
      if (pkey == nullptr) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(key_data.data());
        pkey = d2i_PUBKEY(nullptr, &p, static_cast<long>(key_data.size()));
      }
      BIO_free(bio);
    }
    if (pkey == nullptr) {
      return "";
    }

    RSA* rsa = EVP_PKEY_get1_RSA(pkey);
    EVP_PKEY_free(pkey);
    if (rsa == nullptr) {
      return "";
    }

    const int key_size = RSA_size(rsa);
    const int max_chunk = key_size - 11;
    std::string enc_all;
    for (size_t i = 0; i < plain.size(); i += static_cast<size_t>(max_chunk)) {
      const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(max_chunk), plain.size() - i));
      std::string enc_chunk(static_cast<size_t>(key_size), '\0');
      const int m = RSA_public_encrypt(
          n, reinterpret_cast<const unsigned char*>(plain.data() + i),
          reinterpret_cast<unsigned char*>(enc_chunk.data()), rsa, RSA_PKCS1_PADDING);
      if (m <= 0) {
        RSA_free(rsa);
        return "";
      }
      enc_chunk.resize(static_cast<size_t>(m));
      enc_all += enc_chunk;
    }
    RSA_free(rsa);
    return Base64Encode(enc_all);
#else
    (void)plain;
    (void)public_key_pem_base64;
    return "";
#endif
  }

  static std::string CalcLocalPathEnding(const std::string& data1) {
    if (data1.size() < 10) {
      return "";
    }
    const std::string str_arr = "ABCDEFGHIJ";
    const std::string last10 = data1.substr(data1.size() - 10);
    std::string out;
    for (size_t i = 0; i + 1 < last10.size(); i += 2) {
      const char c = last10[i + 1];
      const size_t pos = str_arr.find(c);
      if (pos != std::string::npos) {
        out += std::to_string(pos);
      }
    }
    return out;
  }

#if GO2_HAS_CURL
  static size_t CurlWriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    if (userdata == nullptr) {
      return 0;
    }
    std::string* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
  }

  bool HttpPostRaw(const std::string& url, const std::string& body, const std::string& content_type,
                   std::string* response, std::string* err) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
      *err = "curl_easy_init failed";
      return false;
    }
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AudioWebRtcManager::CurlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
      *err = std::string("curl error: ") + curl_easy_strerror(rc);
      return false;
    }
    if (status != 200) {
      *err = "http status " + std::to_string(status);
      return false;
    }
    return true;
  }

  bool HttpPostJson(const std::string& url, const std::string& body, std::string* response, std::string* err) {
    return HttpPostRaw(url, body, "application/json", response, err);
  }
#endif

  bool TryGetAnswerFromLocalPeerOldMethod(const std::string& offer_sdp, std::string* answer_sdp,
                                          std::string* answer_type, std::string* err) {
#if GO2_HAS_CURL
    const std::string url = "http://" + go2_host_ip_ + ":8081/offer";
    const std::string body =
        std::string("{\"id\":\"STA_localNetwork\",\"sdp\":\"") + JsonEscape(offer_sdp) +
        "\",\"type\":\"offer\",\"token\":\"\"}";
    std::string resp;
    if (!HttpPostJson(url, body, &resp, err)) {
      return false;
    }
    if (!ExtractJsonStringField(resp, "sdp", answer_sdp)) {
      *err = "local peer response missing sdp";
      return false;
    }
    if (!ExtractJsonStringField(resp, "type", answer_type)) {
      *answer_type = "answer";
    }
    return true;
#else
    (void)offer_sdp;
    (void)answer_sdp;
    (void)answer_type;
    *err = "libcurl is unavailable at build time";
    return false;
#endif
  }

  bool TryGetAnswerFromLocalPeerNewMethod(const std::string& offer_sdp_json, std::string* answer_json,
                                          std::string* err) {
#if GO2_HAS_CURL && GO2_HAS_OPENSSL
    const std::string notify_url = "http://" + go2_host_ip_ + ":9991/con_notify";
    std::string notify_resp;
    if (!HttpPostRaw(notify_url, "", "application/x-www-form-urlencoded", &notify_resp, err)) {
      return false;
    }
    const std::string decoded = Base64Decode(notify_resp);
    if (decoded.empty()) {
      *err = "con_notify base64 decode failed";
      return false;
    }

    std::string data1;
    if (!ExtractJsonStringField(decoded, "data1", &data1) || data1.size() <= 20) {
      *err = "con_notify missing data1";
      return false;
    }
    const std::string public_key_pem_b64 = data1.substr(10, data1.size() - 20);
    const std::string path_ending = CalcLocalPathEnding(data1);
    if (path_ending.empty()) {
      *err = "failed to calc con_ing path ending";
      return false;
    }

    const std::string aes_key = GenerateAesKeyHex32();
    if (aes_key.empty()) {
      *err = "failed to generate aes key";
      return false;
    }
    const std::string enc_data1 = AesEncryptEcbPkcs5Base64(offer_sdp_json, aes_key);
    const std::string enc_data2 = RsaEncryptPkcs1v15Base64(aes_key, public_key_pem_b64);
    if (enc_data1.empty() || enc_data2.empty()) {
      *err = "failed to encrypt local peer new method payload";
      return false;
    }

    const std::string ing_body =
        std::string("{\"data1\":\"") + JsonEscape(enc_data1) + "\",\"data2\":\"" + JsonEscape(enc_data2) + "\"}";
    const std::string ing_url = "http://" + go2_host_ip_ + ":9991/con_ing_" + path_ending;
    std::string ing_resp;
    if (!HttpPostRaw(ing_url, ing_body, "application/x-www-form-urlencoded", &ing_resp, err)) {
      return false;
    }
    const std::string dec_answer = AesDecryptEcbPkcs5Base64(ing_resp, aes_key);
    if (dec_answer.empty()) {
      *err = "failed to decrypt con_ing response";
      return false;
    }
    *answer_json = dec_answer;
    return true;
#else
    (void)offer_sdp_json;
    (void)answer_json;
    *err = "libcurl/openssl is unavailable for local peer new method";
    return false;
#endif
  }

  bool TryGetAnswerFromLocalPeer(const std::string& offer_sdp, std::string* answer_sdp,
                                 std::string* answer_type, std::string* err) {
    if (TryGetAnswerFromLocalPeerOldMethod(offer_sdp, answer_sdp, answer_type, err)) {
      return true;
    }
    std::string e_old = *err;

    const std::string offer_json =
        std::string("{\"id\":\"STA_localNetwork\",\"sdp\":\"") + JsonEscape(offer_sdp) +
        "\",\"type\":\"offer\",\"token\":\"\"}";
    std::string answer_json;
    if (!TryGetAnswerFromLocalPeerNewMethod(offer_json, &answer_json, err)) {
      *err = "old method failed: " + e_old + "; new method failed: " + *err;
      return false;
    }
    if (!ExtractJsonStringField(answer_json, "sdp", answer_sdp)) {
      *err = "new method response missing sdp";
      return false;
    }
    if (!ExtractJsonStringField(answer_json, "type", answer_type)) {
      *answer_type = "answer";
    }
    return true;
  }

  bool ResolveAnswerSdp(const std::string& offer_sdp, std::string* answer_sdp, std::string* answer_type,
                        std::string* err) {
    // local_peer: 1:1 align to go2_webrtc_driver LocalSTA core request.
    if (signal_mode_ == "local_peer" || signal_mode_ == "auto") {
      if (TryGetAnswerFromLocalPeer(offer_sdp, answer_sdp, answer_type, err)) {
        return true;
      }
      if (signal_mode_ == "local_peer") {
        return false;
      }
    }

    // hook mode or auto fallback.
    std::string output;
    if (!RunHook(signal_offer_cmd_, "offer", offer_sdp, &output, err)) {
      return false;
    }
    *answer_sdp = output;
    if (answer_sdp->empty() && !signal_answer_cmd_.empty()) {
      std::string dummy;
      RunHook(signal_answer_cmd_, "answer", "", answer_sdp, &dummy);
    }
    *answer_type = "answer";
    return !answer_sdp->empty();
  }

#if GO2_HAS_GSTREAMER_WEBRTC
  void OnNegotiationNeeded() {
    std::lock_guard<std::mutex> lock(mu_);
    if (webrtc_ == nullptr) {
      SetError("webrtc element is null in negotiation");
      return;
    }
    GstPromise* promise = gst_promise_new_with_change_func(
        AudioWebRtcManager::OnCreateOfferPromiseResolvedThunk, this, nullptr);
    g_signal_emit_by_name(webrtc_, "create-offer", nullptr, promise);
  }

  void OnCreateOfferPromiseResolved(GstPromise* promise) {
    const GstStructure* reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription* offer = nullptr;
    gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, nullptr);
    gst_promise_unref(promise);
    if (offer == nullptr) {
      std::lock_guard<std::mutex> lock(mu_);
      SetError("failed to create offer");
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mu_);
      GstPromise* local_desc_promise = gst_promise_new();
      g_signal_emit_by_name(webrtc_, "set-local-description", offer, local_desc_promise);
      gst_promise_interrupt(local_desc_promise);
      gst_promise_unref(local_desc_promise);
    }

    gchar* sdp_text = gst_sdp_message_as_text(offer->sdp);
    const std::string local_offer = sdp_text != nullptr ? sdp_text : "";
    std::string answer_sdp;
    std::string answer_type;
    std::string err;
    if (!ResolveAnswerSdp(local_offer, &answer_sdp, &answer_type, &err)) {
      std::lock_guard<std::mutex> lock(mu_);
      SetError("failed to resolve answer SDP: " + err);
      if (sdp_text != nullptr) {
        g_free(sdp_text);
      }
      gst_webrtc_session_description_free(offer);
      return;
    }
    if (sdp_text != nullptr) {
      g_free(sdp_text);
    }

    if (answer_sdp == "reject") {
      std::lock_guard<std::mutex> lock(mu_);
      SetError("remote peer rejected offer (already occupied)");
      gst_webrtc_session_description_free(offer);
      return;
    }
    if (!answer_sdp.empty()) {
      GstSDPMessage* sdp = nullptr;
      if (gst_sdp_message_new(&sdp) == GST_SDP_OK &&
          gst_sdp_message_parse_buffer(reinterpret_cast<const guint8*>(answer_sdp.data()),
                                       answer_sdp.size(), sdp) == GST_SDP_OK) {
        GstWebRTCSDPType sdp_type = GST_WEBRTC_SDP_TYPE_ANSWER;
        if (answer_type == "offer") {
          sdp_type = GST_WEBRTC_SDP_TYPE_OFFER;
        } else if (answer_type == "pranswer") {
          sdp_type = GST_WEBRTC_SDP_TYPE_PRANSWER;
        }
        GstWebRTCSessionDescription* answer = gst_webrtc_session_description_new(sdp_type, sdp);
        GstPromise* remote_promise = gst_promise_new();
        g_signal_emit_by_name(webrtc_, "set-remote-description", answer, remote_promise);
        gst_promise_interrupt(remote_promise);
        gst_promise_unref(remote_promise);
        gst_webrtc_session_description_free(answer);
        std::lock_guard<std::mutex> lock(mu_);
        connected_ = true;
      } else {
        std::lock_guard<std::mutex> lock(mu_);
        SetError("failed to parse answer SDP from signaling hook");
      }
    }
    gst_webrtc_session_description_free(offer);
  }

  void OnIceCandidate(guint mline_index, const gchar* candidate) {
    std::string payload = std::to_string(mline_index) + "\n";
    if (candidate != nullptr) {
      payload += candidate;
    }
    std::string out;
    std::string err;
    if (signal_mode_ == "hook" || signal_mode_ == "auto") {
      if (!signal_ice_cmd_.empty()) {
        RunHook(signal_ice_cmd_, "ice", payload, &out, &err);
      }
    } else {
      // local_peer mode mirrors go2_webrtc_driver LocalSTA core flow:
      // single offer->answer exchange, no explicit trickle-ice HTTP bridge.
    }
  }

  bool PushOpusBytes(const std::vector<uint8_t>& payload) {
    if (appsrc_ == nullptr) {
      return false;
    }
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, payload.size(), nullptr);
    if (buffer == nullptr) {
      return false;
    }
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
      gst_buffer_unref(buffer);
      return false;
    }
    memcpy(map.data, payload.data(), payload.size());
    gst_buffer_unmap(buffer, &map);

    GstFlowReturn ret = GST_FLOW_ERROR;
    g_signal_emit_by_name(appsrc_, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);
    return ret == GST_FLOW_OK;
  }
#endif

  void InitPipelineIfNeeded() {
#if GO2_HAS_GSTREAMER
    if (pipeline_) {
      return;
    }
#if GO2_HAS_GSTREAMER_WEBRTC
    // Create elements individually for proper dynamic pad handling
    GstElement* pipeline = gst_pipeline_new(nullptr);
    if (!pipeline) {
      SetError("failed to create pipeline");
      return;
    }

    appsrc_ = gst_element_factory_make("appsrc", "audiosrc");
    GstElement* queue1 = gst_element_factory_make("queue", "queue1");
    GstElement* opusparse_send = gst_element_factory_make("opusparse", "opusparse_send");
    GstElement* rtpopuspay = gst_element_factory_make("rtpopuspay", "rtpopuspay");
    GstElement* queue2 = gst_element_factory_make("queue", "queue2");
    GstElement* rtpopusdepay = gst_element_factory_make("rtpopusdepay", "rtpopusdepay");
    GstElement* queue_recv = gst_element_factory_make("queue", "queue_recv");
    GstElement* opusparse_recv = gst_element_factory_make("opusparse", "opusparse_recv");
    appsink_ = gst_element_factory_make("appsink", "audiosink");
    webrtc_ = gst_element_factory_make("webrtcbin", "webrtc");

    if (!appsrc_ || !queue1 || !opusparse_send || !rtpopuspay || !queue2 ||
        !rtpopusdepay || !queue_recv || !opusparse_recv || !appsink_ || !webrtc_) {
      SetError("failed to create gstreamer elements");
      gst_object_unref(pipeline);
      return;
    }

    // Store the receive path elements for dynamic linking
    rtpopusdepay_ = rtpopusdepay;

    // Configure appsrc for sending
    GstCaps* opus_caps = gst_caps_new_simple("audio/x-opus", "channel-mapping-family", G_TYPE_INT, 0,
                                             "rate", G_TYPE_INT, 48000, "channels", G_TYPE_INT, 1,
                                             nullptr);
    g_object_set(appsrc_, "caps", opus_caps, "format", GST_FORMAT_TIME, "is-live", TRUE,
                 "do-timestamp", TRUE, nullptr);
    gst_caps_unref(opus_caps);

    // Configure appsink for receiving
    g_object_set(appsink_, "emit-signals", TRUE, "sync", FALSE, nullptr);
    g_signal_connect(appsink_, "new-sample",
                     G_CALLBACK(AudioWebRtcManager::OnAppsinkNewSampleThunk), this);

    // Add all elements to pipeline
    gst_bin_add_many(GST_BIN(pipeline), appsrc_, queue1, opusparse_send, rtpopuspay,
                     queue2, rtpopusdepay, queue_recv, opusparse_recv, appsink_, webrtc_, nullptr);

    // Link send path: appsrc -> queue -> opusparse -> rtpopuspay -> queue -> webrtcbin
    if (!gst_element_link_many(appsrc_, queue1, opusparse_send, rtpopuspay, queue2, webrtc_, nullptr)) {
      SetError("failed to link send path");
      gst_object_unref(pipeline);
      pipeline = nullptr;
      return;
    }

    // Link receive path: queue_recv -> opusparse_recv -> appsink (queue has static pads)
    if (!gst_element_link_many(queue_recv, opusparse_recv, appsink_, nullptr)) {
      SetError("failed to link receive path");
      gst_object_unref(pipeline);
      pipeline = nullptr;
      return;
    }

    pipeline_ = pipeline;

    // Connect webrtcbin signals
    g_signal_connect(webrtc_, "on-negotiation-needed",
                     G_CALLBACK(AudioWebRtcManager::OnNegotiationNeededThunk), this);
    g_signal_connect(webrtc_, "on-ice-candidate",
                     G_CALLBACK(AudioWebRtcManager::OnIceCandidateThunk), this);
    // Connect to pad-added for dynamic audio pad linking
    g_signal_connect(webrtc_, "pad-added",
                     G_CALLBACK(AudioWebRtcManager::OnPadAddedThunk), this);
#else
    pipeline_ = gst_parse_launch("fakesrc num-buffers=1 ! fakesink", nullptr);
#endif
    if (!pipeline_) {
      SetError("failed to create gstreamer pipeline");
      return;
    }
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    connected_ = false;
#endif
  }

  void PushSilenceKeepalive() {
#if GO2_HAS_GSTREAMER_WEBRTC
    static const std::vector<uint8_t> kOpusSilence = {0xF8, 0xFF, 0xFE};
    PushOpusBytes(kOpusSilence);
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  void RunLoop() {
    InitPipelineIfNeeded();
    while (!stop_.load()) {
      Item item;
      bool has_item = false;
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait_for(lock, std::chrono::milliseconds(100), [&]() {
          return stop_.load() || !queue_.empty();
        });
        if (stop_.load()) {
          break;
        }
        if (!queue_.empty()) {
          item = std::move(queue_.front());
          queue_.pop_front();
          has_item = true;
          playing_ = true;
          active_stream_id_ = item.stream_id;
        }
      }

      if (!has_item) {
        PushSilenceKeepalive();
        continue;
      }

      bool pushed = false;
#if GO2_HAS_GSTREAMER_WEBRTC
      pushed = PushOpusBytes(item.audio_bytes);
#endif
      const auto bytes = item.audio_bytes.size();
      const auto ms = std::max<int64_t>(40, static_cast<int64_t>(bytes / 128));
      std::this_thread::sleep_for(std::chrono::milliseconds(ms));
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (!pushed) {
          SetError("failed to push audio payload to webrtc appsrc");
        }
        if (!item.loop) {
          playing_ = false;
        } else {
          queue_.push_back(item);
        }
      }
    }
  }

  std::string go2_host_ip_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Item> queue_;
  std::thread worker_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> started_{false};
  bool gst_available_{false};
  bool connected_{false};
  bool playing_{false};
  std::string active_stream_id_;
  uint64_t last_error_ts_ms_{0};
  std::string last_error_;
  std::string signal_offer_cmd_;
  std::string signal_ice_cmd_;
  std::string signal_answer_cmd_;
  std::string signal_mode_{"auto"};
#if GO2_HAS_GSTREAMER
  GstElement* pipeline_{nullptr};
  GstElement* appsrc_{nullptr};
  GstElement* appsink_{nullptr};
  GstElement* webrtc_{nullptr};
  GstElement* rtpopusdepay_{nullptr};
#endif
  std::function<void(const std::string&, const std::vector<uint8_t>&, uint64_t, bool)> audio_capture_callback_;
};

class AudioCaptureManager {
 public:
  struct MicrophoneStream {
    std::string stream_id;
    std::string session_id;
    std::atomic<bool> active{false};
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::pair<std::vector<uint8_t>, uint64_t>> queue;
  };

  AudioCaptureManager() = default;
  ~AudioCaptureManager() { StopAll(); }

  bool StartMicrophone(const std::string& session_id, const std::string& stream_id,
                       uint32_t sample_rate, uint32_t channels, std::string* err);

  bool StopMicrophone(const std::string& stream_id);

  void OnAudioFrame(const std::string& stream_id, const std::vector<uint8_t>& data,
                    uint64_t timestamp_ms, bool is_silence);

  bool ReadAudioData(const std::string& stream_id, std::vector<uint8_t>* out_data,
                     uint64_t* out_timestamp_ms, bool* out_is_silence, int timeout_ms);

  void StopAll();

 private:
  std::mutex streams_mu_;
  std::unordered_map<std::string, std::shared_ptr<MicrophoneStream>> active_streams_;
};

bool AudioCaptureManager::StartMicrophone(const std::string& session_id,
                                         const std::string& stream_id,
                                         uint32_t /*sample_rate*/,
                                         uint32_t /*channels*/,
                                         std::string* err) {
#if !GO2_HAS_GSTREAMER
  (void)session_id;
  (void)stream_id;
  *err = "gstreamer is not available";
  return false;
#else
  std::lock_guard<std::mutex> lock(streams_mu_);
  if (active_streams_.count(stream_id) > 0) {
    *err = "stream already exists";
    return false;
  }
  auto stream = std::make_shared<MicrophoneStream>();
  stream->stream_id = stream_id;
  stream->session_id = session_id;
  stream->active = true;
  active_streams_[stream_id] = stream;
  return true;
#endif
}

bool AudioCaptureManager::StopMicrophone(const std::string& stream_id) {
  std::lock_guard<std::mutex> lock(streams_mu_);
  auto it = active_streams_.find(stream_id);
  if (it == active_streams_.end()) {
    return false;
  }
  it->second->active = false;
  it->second->cv.notify_all();
  active_streams_.erase(it);
  return true;
}

void AudioCaptureManager::OnAudioFrame(const std::string& stream_id,
                                      const std::vector<uint8_t>& data,
                                      uint64_t timestamp_ms,
                                      bool is_silence) {
  std::lock_guard<std::mutex> lock(streams_mu_);
  auto it = active_streams_.find(stream_id);
  if (it == active_streams_.end()) {
    return;
  }
  auto& stream = it->second;
  std::lock_guard<std::mutex> stream_lock(stream->mu);
  if (stream->queue.size() > 100) {
    stream->queue.pop_front();
  }
  stream->queue.emplace_back(data, timestamp_ms);
  stream->cv.notify_one();
}

bool AudioCaptureManager::ReadAudioData(const std::string& stream_id,
                                       std::vector<uint8_t>* out_data,
                                       uint64_t* out_timestamp_ms,
                                       bool* out_is_silence,
                                       int timeout_ms) {
  std::shared_ptr<MicrophoneStream> stream;
  {
    std::lock_guard<std::mutex> lock(streams_mu_);
    auto it = active_streams_.find(stream_id);
    if (it == active_streams_.end()) {
      return false;
    }
    stream = it->second;
  }

  std::unique_lock<std::mutex> stream_lock(stream->mu);
  auto pred = [&]() { return !stream->active || !stream->queue.empty(); };
  if (!stream->cv.wait_for(stream_lock, std::chrono::milliseconds(timeout_ms), pred)) {
    return false;
  }

  if (!stream->active || stream->queue.empty()) {
    return false;
  }

  auto& [data, ts] = stream->queue.front();
  out_data->assign(data.begin(), data.end());
  *out_timestamp_ms = ts;
  *out_is_silence = false;
  stream->queue.pop_front();
  return true;
}

void AudioCaptureManager::StopAll() {
  std::lock_guard<std::mutex> lock(streams_mu_);
  for (auto& [id, stream] : active_streams_) {
    stream->active = false;
    stream->cv.notify_all();
  }
  active_streams_.clear();
}

class UdpDiscoveryBroadcaster {
 public:
  UdpDiscoveryBroadcaster(std::string service_name, AdvertisedAddresses addresses,
                          std::string interface_name, uint16_t grpc_port, uint16_t udp_port)
      : service_name_(std::move(service_name)),
        addresses_(std::move(addresses)),
        interface_name_(std::move(interface_name)),
        grpc_port_(grpc_port),
        udp_port_(udp_port),
        stop_(false) {}

  void Start() { worker_ = std::thread([this]() { BroadcastLoop(); }); }

  void Stop() {
    stop_.store(true);
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  ~UdpDiscoveryBroadcaster() { Stop(); }

 private:
  void BroadcastLoop() {
    int sock4 = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock4 >= 0) {
      int enable_broadcast = 1;
      if (setsockopt(sock4, SOL_SOCKET, SO_BROADCAST, &enable_broadcast,
                     sizeof(enable_broadcast)) != 0) {
        std::cerr << "Warning: unable to enable SO_BROADCAST on discovery socket" << std::endl;
        ::close(sock4);
        sock4 = -1;
      }
    }

    int sock6 = ::socket(AF_INET6, SOCK_DGRAM, 0);
    uint32_t ifindex = if_nametoindex(interface_name_.c_str());
    if (sock6 >= 0 && ifindex != 0) {
      if (setsockopt(sock6, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifindex, sizeof(ifindex)) != 0) {
        std::cerr << "Warning: unable to set IPV6_MULTICAST_IF for discovery socket" << std::endl;
      }
      int hop_limit = 1;
      if (setsockopt(sock6, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hop_limit, sizeof(hop_limit)) !=
          0) {
        std::cerr << "Warning: unable to set IPV6_MULTICAST_HOPS for discovery socket"
                  << std::endl;
      }
    } else if (sock6 >= 0 && ifindex == 0) {
      std::cerr << "Warning: unknown interface index for IPv6 discovery multicast: "
                << interface_name_ << std::endl;
    }

    if (sock4 < 0 && sock6 < 0) {
      std::cerr << "Warning: unable to create any UDP discovery socket" << std::endl;
      return;
    }

    struct sockaddr_in addr4 {};
    addr4.sin_family = AF_INET;
    addr4.sin_port = htons(udp_port_);
    addr4.sin_addr.s_addr = inet_addr("255.255.255.255");

    struct sockaddr_in6 addr6 {};
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons(udp_port_);
    inet_pton(AF_INET6, "ff02::1", &(addr6.sin6_addr));
    addr6.sin6_scope_id = ifindex;

    while (!stop_.load()) {
      const uint64_t now_ms = NowUnixMs();

      if (sock4 >= 0) {
        std::ostringstream payload4;
        payload4 << "{\"service\":\"" << service_name_ << "\",\"family\":\"ipv4\",\"ip\":\""
                 << addresses_.ipv4 << "\",\"port\":" << grpc_port_ << ",\"ts_ms\":" << now_ms
                 << "}";
        const std::string body4 = payload4.str();
        const ssize_t sent4 = sendto(sock4, body4.data(), body4.size(), 0,
                                     reinterpret_cast<struct sockaddr*>(&addr4), sizeof(addr4));
        if (sent4 < 0) {
          std::cerr << "Warning: IPv4 UDP discovery broadcast failed: " << std::strerror(errno)
                    << std::endl;
        }
      }

      if (sock6 >= 0) {
        std::ostringstream payload6;
        payload6 << "{\"service\":\"" << service_name_ << "\",\"family\":\"ipv6\",\"ip\":\""
                 << addresses_.ipv6 << "\",\"port\":" << grpc_port_ << ",\"ts_ms\":" << now_ms
                 << "}";
        const std::string body6 = payload6.str();
        const ssize_t sent6 = sendto(sock6, body6.data(), body6.size(), 0,
                                     reinterpret_cast<struct sockaddr*>(&addr6), sizeof(addr6));
        if (sent6 < 0) {
          std::cerr << "Warning: IPv6 UDP discovery multicast failed: " << std::strerror(errno)
                    << std::endl;
        }
      }

      std::this_thread::sleep_for(kDiscoveryBroadcastInterval);
    }

    if (sock4 >= 0) {
      ::close(sock4);
    }
    if (sock6 >= 0) {
      ::close(sock6);
    }
  }

  std::string service_name_;
  AdvertisedAddresses addresses_;
  std::string interface_name_;
  uint16_t grpc_port_;
  uint16_t udp_port_;
  std::atomic<bool> stop_;
  std::thread worker_;
};

class Go2SportServiceImpl final : public Go2SportService::Service {
 public:
  Go2SportServiceImpl(const std::string& network_interface, bool enable_lease,
                      const std::string& default_model_path, const std::string& go2_host_ip)
      : stop_gc_(false), audio_mgr_(go2_host_ip) {
    unitree::robot::ChannelFactory::Instance()->Init(0, network_interface);
    sport_client_ = std::make_unique<unitree::robot::go2::SportClient>(enable_lease);
    sport_client_->SetTimeout(10.0f);
    sport_client_->Init();
    obstacles_avoid_client_ = std::make_unique<unitree::robot::go2::ObstaclesAvoidClient>();
    obstacles_avoid_client_->SetTimeout(10.0f);
    obstacles_avoid_client_->Init();
    const int32_t avoid_ret = obstacles_avoid_client_->SwitchSet(true);
    if (avoid_ret != 0) {
      std::cerr << "Warning: failed to enable obstacle avoidance, sdk_code=" << avoid_ret
                << std::endl;
    }
    video_client_ = std::make_unique<unitree::robot::go2::VideoClient>();
    video_client_->SetTimeout(1.0f);
    video_client_->Init();
    engine_.SetDefaultModelPath(default_model_path);
    engine_.SetLabels(ResolveLabels());
    const char* debug_dir = std::getenv("GO2_DETECT_DEBUG_DIR");
    if (debug_dir != nullptr && std::strlen(debug_dir) > 0) {
      detect_debug_dir_ = debug_dir;
      std::error_code ec;
      std::filesystem::create_directories(detect_debug_dir_, ec);
      if (ec) {
        std::cerr << "failed to create GO2_DETECT_DEBUG_DIR=" << detect_debug_dir_ << ": " << ec.message()
                  << std::endl;
      } else {
        std::cerr << "detection debug frame dump enabled: " << detect_debug_dir_ << std::endl;
      }
    }
    const char* timing_log_path = std::getenv("GO2_DETECT_TIMING_LOG");
    if (timing_log_path != nullptr && std::strlen(timing_log_path) > 0) {
      detect_timing_log_.open(timing_log_path, std::ios::app);
      if (detect_timing_log_.is_open()) {
        std::cerr << "detection timing log enabled: " << timing_log_path << std::endl;
        detect_timing_log_ << "# Detection Timing Log\n";
        detect_timing_log_ << "# timestamp_ms,frame_id,stream_id,fps_limit,frame_skip,fetch_ms,infer_ms,total_ms,detections\n";
        detect_timing_log_.flush();
      } else {
        std::cerr << "failed to open GO2_DETECT_TIMING_LOG=" << timing_log_path << std::endl;
      }
    }
    audio_mgr_.SetAudioCaptureCallback([this](const std::string& stream_id, const std::vector<uint8_t>& data,
                                             uint64_t timestamp_ms, bool is_silence) {
      mic_mgr_.OnAudioFrame(stream_id, data, timestamp_ms, is_silence);
    });
    audio_mgr_.Start();

    gc_thread_ = std::thread([this]() { SessionGcLoop(); });
  }

  ~Go2SportServiceImpl() override {
    StopAllDetectionStreams();
    audio_mgr_.Stop();
    mic_mgr_.StopAll();
    stop_gc_.store(true);
    if (gc_thread_.joinable()) {
      gc_thread_.join();
    }
    if (detect_timing_log_.is_open()) {
      detect_timing_log_.close();
    }
  }

  static std::vector<std::string> ResolveLabels() {
    const char* envs[] = {"GO2_LABEL_PATH", "GO2_LABELS_PATH", "GO2_LABEL_FILE"};
    for (const char* key : envs) {
      const char* v = std::getenv(key);
      if (v == nullptr || std::strlen(v) == 0) {
        continue;
      }
      std::ifstream ifs(v);
      if (!ifs) {
        std::cerr << "failed to open labels file from " << key << "=" << v << ", fallback to COCO80 labels"
                  << std::endl;
        continue;
      }
      std::vector<std::string> labels;
      std::string line;
      while (std::getline(ifs, line)) {
        line = TrimCopy(line);
        if (line.empty() || line[0] == '#') {
          continue;
        }
        labels.push_back(line);
      }
      if (!labels.empty()) {
        std::cerr << "loaded " << labels.size() << " labels from " << key << "=" << v << std::endl;
        return labels;
      }
    }
    return DefaultCoco80Labels();
  }

  grpc::Status OpenSession(grpc::ServerContext*, const OpenSessionRequest* req,
                           OpenSessionResponse* resp) override {
    const auto ttl_sec = NormalizeTtl(req->ttl_sec());
    const auto now = std::chrono::steady_clock::now();

    Session session;
    session.id = GenerateSessionId();
    session.owner = req->owner().empty() ? "anonymous" : req->owner();
    session.name = req->session_name();
    session.parallel = req->parallel();
    session.expires_at = now + std::chrono::seconds(ttl_sec);

    {
      std::unique_lock<std::shared_mutex> lock(session_mu_);
      sessions_[session.id] = session;
    }

    resp->set_code(0);
    resp->set_message("ok");
    resp->set_session_id(session.id);
    resp->set_expires_at_ms(NowUnixMs() + static_cast<uint64_t>(ttl_sec) * 1000ULL);
    return grpc::Status::OK;
  }

  grpc::Status Heartbeat(grpc::ServerContext*, const HeartbeatRequest* req,
                         HeartbeatResponse* resp) override {
    std::unique_lock<std::shared_mutex> lock(session_mu_);
    auto it = sessions_.find(req->session_id());
    if (it == sessions_.end()) {
      resp->set_code(404);
      resp->set_message("session not found");
      return grpc::Status::OK;
    }

    const auto ttl_sec = kDefaultTtlSec;
    it->second.expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(ttl_sec);
    resp->set_code(0);
    resp->set_message("ok");
    resp->set_expires_at_ms(NowUnixMs() + static_cast<uint64_t>(ttl_sec) * 1000ULL);
    return grpc::Status::OK;
  }

  grpc::Status CloseSession(grpc::ServerContext*, const CloseSessionRequest* req,
                            CloseSessionResponse* resp) override {
    std::unique_lock<std::shared_mutex> lock(session_mu_);
    size_t erased = sessions_.erase(req->session_id());
    if (erased == 0) {
      resp->set_code(404);
      resp->set_message("session not found");
      return grpc::Status::OK;
    }

    resp->set_code(0);
    resp->set_message("ok");
    return grpc::Status::OK;
  }

  grpc::Status ForceCloseOwnerSessions(grpc::ServerContext*,
                                       const ForceCloseOwnerSessionsRequest* req,
                                       ForceCloseOwnerSessionsResponse* resp) override {
    std::unique_lock<std::shared_mutex> lock(session_mu_);

    uint32_t closed_count = 0;
    for (auto it = sessions_.begin(); it != sessions_.end();) {
      const bool owner_match = it->second.owner == req->owner();
      const bool keep_parallel = req->keep_parallel_sessions() && it->second.parallel;
      if (owner_match && !keep_parallel) {
        it = sessions_.erase(it);
        ++closed_count;
      } else {
        ++it;
      }
    }

    resp->set_code(0);
    resp->set_message("ok");
    resp->set_closed_count(closed_count);
    return grpc::Status::OK;
  }

  grpc::Status ExecuteAction(grpc::ServerContext*, const ExecuteActionRequest* req,
                             ExecuteActionResponse* resp) override {
    if (!SessionExists(req->session_id())) {
      resp->set_code(404);
      resp->set_message("session not found");
      return grpc::Status::OK;
    }

    // Serialize command execution across sessions to avoid control conflicts.
    std::lock_guard<std::mutex> lock(action_mu_);

    bool bool_value = false;
    const auto result = DispatchAction(*req, &bool_value);
    resp->set_sdk_code(result.sdk_code);
    resp->set_code(result.code);
    resp->set_message(result.message);
    resp->set_bool_value(bool_value);
    return grpc::Status::OK;
  }

  grpc::Status GetServerStatus(grpc::ServerContext*, const GetServerStatusRequest*,
                               GetServerStatusResponse* resp) override {
    resp->set_code(0);
    resp->set_message("ok");

    std::shared_lock<std::shared_mutex> lock(session_mu_);
    resp->set_active_sessions(static_cast<uint32_t>(sessions_.size()));

    for (const auto& kv : sessions_) {
      const auto& s = kv.second;
      SessionStatus* out = resp->add_sessions();
      out->set_session_id(s.id);
      out->set_owner(s.owner);
      out->set_session_name(s.name);
      out->set_parallel(s.parallel);

      auto remain_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           s.expires_at - std::chrono::steady_clock::now())
                           .count();
      if (remain_ms < 0) {
        remain_ms = 0;
      }
      out->set_expires_at_ms(NowUnixMs() + static_cast<uint64_t>(remain_ms));
    }

    return grpc::Status::OK;
  }

  grpc::Status DetectObjects(grpc::ServerContext*, const DetectObjectsRequest* req,
                             DetectObjectsResponse* resp) override {
    if (!SessionExists(req->session_id())) {
      resp->set_code(404);
      resp->set_message("session not found");
      return grpc::Status::OK;
    }

    std::string err;
    if (!engine_.EnsureLoaded(req->model_path(), &err)) {
      resp->set_code(500);
      resp->set_message(err);
      return grpc::Status::OK;
    }

    std::vector<uint8_t> image_sample;
    const int32_t ret = video_client_->GetImageSample(image_sample);
    if (ret != 0) {
      resp->set_code(500);
      resp->set_message("GetImageSample failed");
      return grpc::Status::OK;
    }

    YoloTrtEngine::DetectConfig cfg;
    cfg.conf_thres = req->conf_thres() > 0.0f ? req->conf_thres() : 0.25f;
    cfg.iou_thres = req->iou_thres() > 0.0f ? req->iou_thres() : 0.45f;
    cfg.max_det = req->max_det() > 0 ? req->max_det() : 100U;
    MaybeDumpDetectionInput("detect_once", image_sample);
    const auto dets = engine_.Infer(image_sample, cfg);

    resp->set_code(0);
    resp->set_message("ok");
    resp->set_frame_id(next_frame_id_.fetch_add(1));
    resp->set_timestamp_ms(NowUnixMs());
    for (const auto& d : dets) {
      *resp->add_detections() = d;
    }
    return grpc::Status::OK;
  }

  grpc::Status StartDetection(grpc::ServerContext*, const StartDetectionRequest* req,
                              StartDetectionResponse* resp) override {
    if (!SessionExists(req->session_id())) {
      resp->set_code(404);
      resp->set_message("session not found");
      return grpc::Status::OK;
    }

    std::string err;
    if (!engine_.EnsureLoaded(req->model_path(), &err)) {
      resp->set_code(500);
      resp->set_message(err);
      return grpc::Status::OK;
    }

    const std::string stream_id = ChooseOrGenerateStreamId(req->stream_id());
    {
      std::lock_guard<std::mutex> lock(streams_mu_);
      if (detection_streams_.find(stream_id) != detection_streams_.end()) {
        resp->set_code(409);
        resp->set_message("stream already exists");
        return grpc::Status::OK;
      }
    }

    auto stream = std::make_shared<DetectionStreamState>();
    stream->session_id = req->session_id();
    stream->stream_id = stream_id;
    stream->model_path = req->model_path();
    stream->frame_skip = req->frame_skip();
    stream->fps_limit = req->fps_limit() > 0 ? req->fps_limit() : 5;
    stream->cfg.conf_thres = req->conf_thres() > 0.0f ? req->conf_thres() : 0.25f;
    stream->cfg.iou_thres = req->iou_thres() > 0.0f ? req->iou_thres() : 0.45f;
    stream->cfg.max_det = req->max_det() > 0 ? req->max_det() : 100U;

    {
      std::lock_guard<std::mutex> lock(streams_mu_);
      detection_streams_[stream_id] = stream;
    }
    stream->worker = std::thread([this, stream]() { DetectionWorkerLoop(stream); });

    resp->set_code(0);
    resp->set_message("ok");
    resp->set_stream_id(stream_id);
    return grpc::Status::OK;
  }

  grpc::Status StopDetection(grpc::ServerContext*, const StopDetectionRequest* req,
                             StopDetectionResponse* resp) override {
    std::shared_ptr<DetectionStreamState> stream;
    {
      std::lock_guard<std::mutex> lock(streams_mu_);
      auto it = detection_streams_.find(req->stream_id());
      if (it == detection_streams_.end()) {
        resp->set_code(404);
        resp->set_message("stream not found");
        resp->set_stopped(false);
        return grpc::Status::OK;
      }
      stream = it->second;
      detection_streams_.erase(it);
    }

    stream->stop.store(true);
    stream->cv.notify_all();
    if (stream->worker.joinable()) {
      stream->worker.join();
    }
    resp->set_code(0);
    resp->set_message("ok");
    resp->set_stopped(true);
    return grpc::Status::OK;
  }

  grpc::Status SubscribeDetections(grpc::ServerContext* ctx, const SubscribeDetectionsRequest* req,
                                   grpc::ServerWriter<DetectionEvent>* writer) override {
    if (!SessionExists(req->session_id())) {
      return grpc::Status(grpc::StatusCode::NOT_FOUND, "session not found");
    }

    std::shared_ptr<DetectionStreamState> stream;
    {
      std::lock_guard<std::mutex> lock(streams_mu_);
      auto it = detection_streams_.find(req->stream_id());
      if (it == detection_streams_.end()) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "stream not found");
      }
      stream = it->second;
    }

    while (!ctx->IsCancelled()) {
      DetectionEvent event;
      bool has_event = false;
      {
        std::unique_lock<std::mutex> lock(stream->mu);
        stream->cv.wait_for(lock, std::chrono::milliseconds(300), [&]() {
          return stream->stop.load() || !stream->queue.empty() || ctx->IsCancelled();
        });
        if (!stream->queue.empty()) {
          event = stream->queue.front();
          stream->queue.pop_front();
          has_event = true;
        } else if (stream->stop.load()) {
          break;
        }
      }
      if (has_event && !writer->Write(event)) {
        break;
      }
    }

    return grpc::Status::OK;
  }

  grpc::Status UploadAndPlayAudio(grpc::ServerContext*, const UploadAndPlayAudioRequest* req,
                                  UploadAndPlayAudioResponse* resp) override {
    if (!SessionExists(req->session_id())) {
      resp->set_code(404);
      resp->set_message("session not found");
      resp->set_accepted(false);
      return grpc::Status::OK;
    }

    const std::string stream_id = ChooseOrGenerateStreamId(req->stream_id());
    AudioWebRtcManager::Item item;
    item.stream_id = stream_id;
    item.request_id = req->request_id();
    item.mime = req->mime();
    item.sample_rate = req->sample_rate();
    item.channels = req->channels();
    item.volume = req->volume() <= 0.0f ? 1.0f : req->volume();
    item.loop = req->loop();
    item.audio_bytes.assign(req->audio_bytes().begin(), req->audio_bytes().end());

    std::string err;
    if (!audio_mgr_.Enqueue(std::move(item), &err)) {
      resp->set_code(500);
      resp->set_message(err);
      resp->set_accepted(false);
      return grpc::Status::OK;
    }

    resp->set_code(0);
    resp->set_message("ok");
    resp->set_stream_id(stream_id);
    resp->set_request_id(req->request_id());
    resp->set_accepted(true);
    return grpc::Status::OK;
  }

  grpc::Status GetAudioStatus(grpc::ServerContext*, const GetAudioStatusRequest* req,
                              GetAudioStatusResponse* resp) override {
    if (!SessionExists(req->session_id())) {
      resp->set_code(404);
      resp->set_message("session not found");
      return grpc::Status::OK;
    }
    resp->set_code(0);
    resp->set_message("ok");
    audio_mgr_.FillStatus(resp);
    return grpc::Status::OK;
  }

  grpc::Status StopAudioPlayback(grpc::ServerContext*, const StopAudioPlaybackRequest* req,
                                 StopAudioPlaybackResponse* resp) override {
    if (!SessionExists(req->session_id())) {
      resp->set_code(404);
      resp->set_message("session not found");
      resp->set_stopped(false);
      return grpc::Status::OK;
    }
    const bool stopped = audio_mgr_.StopPlayback(req->stream_id());
    resp->set_code(0);
    resp->set_message("ok");
    resp->set_stopped(stopped);
    return grpc::Status::OK;
  }

  grpc::Status StartMicrophone(grpc::ServerContext*, const StartMicrophoneRequest* req,
                               StartMicrophoneResponse* resp) override {
    if (!SessionExists(req->session_id())) {
      resp->set_code(404);
      resp->set_message("session not found");
      return grpc::Status::OK;
    }
    std::string stream_id = req->stream_id().empty() ? GenerateStreamId() : req->stream_id();
    std::string err;
    if (!mic_mgr_.StartMicrophone(req->session_id(), stream_id, req->sample_rate(),
                                  req->channels(), &err)) {
      resp->set_code(500);
      resp->set_message(err);
      return grpc::Status::OK;
    }
    resp->set_code(0);
    resp->set_message("ok");
    resp->set_stream_id(stream_id);
    return grpc::Status::OK;
  }

  grpc::Status StopMicrophone(grpc::ServerContext*, const StopMicrophoneRequest* req,
                              StopMicrophoneResponse* resp) override {
    if (!SessionExists(req->session_id())) {
      resp->set_code(404);
      resp->set_message("session not found");
      resp->set_stopped(false);
      return grpc::Status::OK;
    }
    const bool stopped = mic_mgr_.StopMicrophone(req->stream_id());
    resp->set_code(0);
    resp->set_message("ok");
    resp->set_stopped(stopped);
    return grpc::Status::OK;
  }

  grpc::Status SubscribeMicrophone(grpc::ServerContext* ctx,
                                  grpc::ServerReaderWriter<MicrophoneAudio, MicrophoneControl>* stream) override {
    MicrophoneControl control;
    if (!stream->Read(&control)) {
      return grpc::Status::OK;
    }
    if (control.command() != MicrophoneControl::COMMAND_START) {
      return grpc::Status::OK;
    }
    const std::string& stream_id = control.stream_id();
    while (!ctx->IsCancelled()) {
      std::vector<uint8_t> data;
      uint64_t ts = 0;
      bool is_silence = false;
      if (mic_mgr_.ReadAudioData(stream_id, &data, &ts, &is_silence, 100)) {
        MicrophoneAudio audio;
        audio.set_stream_id(stream_id);
        audio.set_audio_data(data.data(), data.size());
        audio.set_timestamp_ms(ts);
        audio.set_is_silence(is_silence);
        if (!stream->Write(audio)) {
          break;
        }
      }
      if (stream->Read(&control) && control.command() == MicrophoneControl::COMMAND_STOP) {
        break;
      }
    }
    return grpc::Status::OK;
  }

 private:
  struct ActionResult {
    int32_t code;
    int32_t sdk_code;
    std::string message;
  };

  static uint32_t NormalizeTtl(uint32_t ttl_sec) {
    if (ttl_sec == 0) {
      return kDefaultTtlSec;
    }
    if (ttl_sec < kMinTtlSec) {
      return kMinTtlSec;
    }
    if (ttl_sec > kMaxTtlSec) {
      return kMaxTtlSec;
    }
    return ttl_sec;
  }

  bool SessionExists(const std::string& session_id) {
    std::shared_lock<std::shared_mutex> lock(session_mu_);
    return sessions_.find(session_id) != sessions_.end();
  }

  ActionResult MakeResult(int32_t code, int32_t sdk_code, const std::string& message) {
    return ActionResult{code, sdk_code, message};
  }

  ActionResult DispatchAction(const ExecuteActionRequest& req, bool* bool_value) {
    int32_t ret = -1;
    switch (req.action()) {
      case Action::ACTION_DAMP:
        ret = sport_client_->Damp();
        break;
      case Action::ACTION_BALANCE_STAND:
        ret = sport_client_->BalanceStand();
        break;
      case Action::ACTION_STOP_MOVE:
        ret = sport_client_->StopMove();
        break;
      case Action::ACTION_STAND_UP:
        ret = sport_client_->StandUp();
        break;
      case Action::ACTION_STAND_DOWN:
        ret = sport_client_->StandDown();
        break;
      case Action::ACTION_RECOVERY_STAND:
        ret = sport_client_->RecoveryStand();
        break;
      case Action::ACTION_EULER:
        ret = sport_client_->Euler(req.roll(), req.pitch(), req.yaw());
        break;
      case Action::ACTION_MOVE:
        ret = sport_client_->Move(req.vx(), req.vy(), req.vyaw());
        break;
      case Action::ACTION_SIT:
        ret = sport_client_->Sit();
        break;
      case Action::ACTION_RISE_SIT:
        ret = sport_client_->RiseSit();
        break;
      case Action::ACTION_SPEED_LEVEL:
        ret = sport_client_->SpeedLevel(req.level());
        break;
      case Action::ACTION_HELLO:
        ret = sport_client_->Hello();
        break;
      case Action::ACTION_STRETCH:
        ret = sport_client_->Stretch();
        break;
      case Action::ACTION_SWITCH_JOYSTICK:
        ret = sport_client_->SwitchJoystick(req.flag());
        break;
      case Action::ACTION_CONTENT:
        ret = sport_client_->Content();
        break;
      case Action::ACTION_HEART:
        ret = sport_client_->Heart();
        break;
      case Action::ACTION_POSE:
        ret = sport_client_->Pose(req.flag());
        break;
      case Action::ACTION_SCRAPE:
        ret = sport_client_->Scrape();
        break;
      case Action::ACTION_FRONT_FLIP:
        ret = sport_client_->FrontFlip();
        break;
      case Action::ACTION_FRONT_JUMP:
        ret = sport_client_->FrontJump();
        break;
      case Action::ACTION_FRONT_POUNCE:
        ret = sport_client_->FrontPounce();
        break;
      case Action::ACTION_DANCE1:
        ret = sport_client_->Dance1();
        break;
      case Action::ACTION_DANCE2:
        ret = sport_client_->Dance2();
        break;
      case Action::ACTION_LEFT_FLIP:
        ret = sport_client_->LeftFlip();
        break;
      case Action::ACTION_BACK_FLIP:
        ret = sport_client_->BackFlip();
        break;
      case Action::ACTION_HAND_STAND:
        ret = sport_client_->HandStand(req.flag());
        break;
      case Action::ACTION_FREE_WALK:
        ret = sport_client_->FreeWalk();
        break;
      case Action::ACTION_FREE_BOUND:
        ret = sport_client_->FreeBound(req.flag());
        break;
      case Action::ACTION_FREE_JUMP:
        ret = sport_client_->FreeJump(req.flag());
        break;
      case Action::ACTION_FREE_AVOID:
        ret = sport_client_->FreeAvoid(req.flag());
        break;
      case Action::ACTION_CLASSIC_WALK:
        ret = sport_client_->ClassicWalk(req.flag());
        break;
      case Action::ACTION_WALK_UPRIGHT:
        ret = sport_client_->WalkUpright(req.flag());
        break;
      case Action::ACTION_CROSS_STEP:
        ret = sport_client_->CrossStep(req.flag());
        break;
      case Action::ACTION_AUTO_RECOVER_SET:
        ret = sport_client_->AutoRecoverSet(req.flag());
        break;
      case Action::ACTION_AUTO_RECOVER_GET: {
        bool v = false;
        ret = sport_client_->AutoRecoverGet(v);
        *bool_value = v;
        break;
      }
      case Action::ACTION_STATIC_WALK:
        ret = sport_client_->StaticWalk();
        break;
      case Action::ACTION_TROT_RUN:
        ret = sport_client_->TrotRun();
        break;
      case Action::ACTION_ECONOMIC_GAIT:
        ret = sport_client_->EconomicGait();
        break;
      case Action::ACTION_SWITCH_AVOID_MODE:
        ret = sport_client_->SwitchAvoidMode();
        break;
      default:
        return MakeResult(400, -1, "unsupported action");
    }

    if (ret == 0) {
      return MakeResult(0, ret, "ok");
    }
    return MakeResult(500, ret, "sdk returned non-zero");
  }

  void PublishDetectionEvent(const std::shared_ptr<DetectionStreamState>& stream,
                             const std::vector<Detection>& detections, uint64_t frame_id) {
    DetectionEvent event;
    event.set_code(0);
    event.set_message("ok");
    event.set_stream_id(stream->stream_id);
    event.set_frame_id(frame_id);
    event.set_timestamp_ms(NowUnixMs());
    for (const auto& d : detections) {
      *event.add_detections() = d;
    }

    std::lock_guard<std::mutex> lock(stream->mu);
    if (stream->queue.size() >= 32) {
      stream->queue.pop_front();
    }
    stream->queue.push_back(std::move(event));
    stream->cv.notify_all();
  }

  void DetectionWorkerLoop(const std::shared_ptr<DetectionStreamState>& stream) {
    uint32_t frame_counter = 0;
    const auto min_interval = std::chrono::milliseconds(
        stream->fps_limit > 0 ? static_cast<int>(1000 / stream->fps_limit) : 200);
    auto last_run = std::chrono::steady_clock::now() - min_interval;

    while (!stream->stop.load()) {
      auto loop_start = std::chrono::steady_clock::now();
      auto now = loop_start;
      if (now - last_run < min_interval) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }
      last_run = now;
      if (stream->frame_skip > 0 && (frame_counter++ % (stream->frame_skip + 1)) != 0) {
        continue;
      }

      auto fetch_start = std::chrono::steady_clock::now();
      std::vector<uint8_t> image_sample;
      if (video_client_->GetImageSample(image_sample) != 0) {
        continue;
      }
      auto fetch_end = std::chrono::steady_clock::now();
      MaybeDumpDetectionInput(stream->stream_id, image_sample);

      std::string err;
      if (!engine_.EnsureLoaded(stream->model_path, &err)) {
        continue;
      }
      auto infer_start = std::chrono::steady_clock::now();
      const auto detections = engine_.Infer(image_sample, stream->cfg);
      auto infer_end = std::chrono::steady_clock::now();
      const uint64_t frame_id = next_frame_id_.fetch_add(1);
      PublishDetectionEvent(stream, detections, frame_id);

      auto loop_end = std::chrono::steady_clock::now();
      int64_t fetch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(fetch_end - fetch_start).count();
      int64_t infer_ms = std::chrono::duration_cast<std::chrono::milliseconds>(infer_end - infer_start).count();
      int64_t total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(loop_end - loop_start).count();
      LogDetectionTiming(stream->stream_id, frame_id, stream->fps_limit, stream->frame_skip,
                         fetch_ms, infer_ms, total_ms, detections.size());
    }
  }

  void StopAllDetectionStreams() {
    std::vector<std::shared_ptr<DetectionStreamState>> streams;
    {
      std::lock_guard<std::mutex> lock(streams_mu_);
      for (auto& kv : detection_streams_) {
        streams.push_back(kv.second);
      }
      detection_streams_.clear();
    }
    for (auto& s : streams) {
      s->stop.store(true);
      s->cv.notify_all();
      if (s->worker.joinable()) {
        s->worker.join();
      }
    }
  }

  void MaybeDumpDetectionInput(const std::string& stream_tag, const std::vector<uint8_t>& image_sample) {
    if (detect_debug_dir_.empty() || image_sample.empty()) {
      return;
    }
    std::lock_guard<std::mutex> lock(detect_debug_mu_);
    uint64_t seq = ++detect_debug_seq_[stream_tag];
    const std::string name = detect_debug_dir_ + "/" + SanitizeForFileName(stream_tag) + "_" +
                             std::to_string(NowUnixMs()) + "_" + std::to_string(seq) + ".jpg";
    std::ofstream ofs(name, std::ios::binary);
    if (!ofs) {
      return;
    }
    ofs.write(reinterpret_cast<const char*>(image_sample.data()), static_cast<std::streamsize>(image_sample.size()));
  }

  void LogDetectionTiming(const std::string& stream_id, uint64_t frame_id,
                          uint32_t fps_limit, uint32_t frame_skip,
                          int64_t fetch_ms, int64_t infer_ms, int64_t total_ms,
                          size_t detection_count) {
    if (!detect_timing_log_.is_open()) {
      return;
    }
    std::lock_guard<std::mutex> lock(detect_timing_mu_);
    detect_timing_log_ << NowUnixMs()
                       << "," << frame_id
                       << "," << stream_id
                       << "," << fps_limit
                       << "," << frame_skip
                       << "," << fetch_ms
                       << "," << infer_ms
                       << "," << total_ms
                       << "," << detection_count
                       << "\n";
    detect_timing_log_.flush();
  }

  void SessionGcLoop() {
    while (!stop_gc_.load()) {
      {
        std::unique_lock<std::shared_mutex> lock(session_mu_);
        const auto now = std::chrono::steady_clock::now();
        for (auto it = sessions_.begin(); it != sessions_.end();) {
          if (it->second.expires_at <= now) {
            it = sessions_.erase(it);
          } else {
            ++it;
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  std::atomic<bool> stop_gc_;
  std::thread gc_thread_;

  std::shared_mutex session_mu_;
  std::unordered_map<std::string, Session> sessions_;

  std::mutex action_mu_;
  std::mutex streams_mu_;
  std::unordered_map<std::string, std::shared_ptr<DetectionStreamState>> detection_streams_;
  std::string detect_debug_dir_;
  std::mutex detect_debug_mu_;
  std::unordered_map<std::string, uint64_t> detect_debug_seq_;
  std::ofstream detect_timing_log_;
  std::mutex detect_timing_mu_;
  std::atomic<uint64_t> next_frame_id_{1};
  std::unique_ptr<unitree::robot::go2::SportClient> sport_client_;
  std::unique_ptr<unitree::robot::go2::ObstaclesAvoidClient> obstacles_avoid_client_;
  std::unique_ptr<unitree::robot::go2::VideoClient> video_client_;
  YoloTrtEngine engine_;
  AudioWebRtcManager audio_mgr_;
  AudioCaptureManager mic_mgr_;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0]
              << " <network_interface> <listen_address> <port> [enable_lease:0|1] "
                 "[default_model_path] [go2_host_ip]"
              << std::endl;
    return 1;
  }

  const std::string network_interface = argv[1];
  const std::string listen_address = argv[2];
  const std::string port = argv[3];
  const bool enable_lease = (argc > 4) ? (std::string(argv[4]) == "1") : false;
  const std::string default_model_path = (argc > 5) ? argv[5] : "";
  const std::string go2_host_ip = (argc > 6) ? argv[6] : "192.168.123.161";
  const uint16_t grpc_port = static_cast<uint16_t>(std::stoi(port));
  const AdvertisedAddresses advertised_addrs =
      ResolveAdvertisedAddresses(network_interface, listen_address);

  Go2SportServiceImpl service(network_interface, enable_lease, default_model_path, go2_host_ip);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(listen_address + ":" + port, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server) {
    std::cerr << "Failed to start gRPC server" << std::endl;
    return 2;
  }

  std::cout << "go2_sport_grpc_server listening on " << listen_address << ":" << port
            << " using robot interface " << network_interface << std::endl;
  std::cout << "UDP discovery broadcasting go2_sport_grpc endpoint to port " << kDiscoveryUdpPort
            << " with advertised ipv4 "
            << (advertised_addrs.ipv4.empty() ? "<sender-ip>" : advertised_addrs.ipv4)
            << " and ipv6 "
            << (advertised_addrs.ipv6.empty() ? "<sender-ip>" : advertised_addrs.ipv6)
            << std::endl;

  UdpDiscoveryBroadcaster broadcaster("go2_sport_grpc", advertised_addrs, network_interface,
                                      grpc_port, kDiscoveryUdpPort);
  broadcaster.Start();

  server->Wait();
  broadcaster.Stop();
  return 0;
}
