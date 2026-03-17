#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct AudioItem {
  std::vector<uint8_t> payload;
  bool loop{false};
};

class MockAudioTransport {
 public:
  void SendSilence() { ++silence_frames_; }
  void SendAudio(const std::vector<uint8_t>& payload) { sent_audio_bytes_ += payload.size(); }

  uint64_t silence_frames() const { return silence_frames_.load(); }
  uint64_t sent_audio_bytes() const { return sent_audio_bytes_.load(); }

 private:
  std::atomic<uint64_t> silence_frames_{0};
  std::atomic<uint64_t> sent_audio_bytes_{0};
};

class AudioPlaybackQueue {
 public:
  explicit AudioPlaybackQueue(MockAudioTransport* transport) : transport_(transport) {}

  ~AudioPlaybackQueue() { Stop(); }

  void Start() {
    if (started_.exchange(true)) return;
    stop_.store(false);
    worker_ = std::thread([this]() { RunLoop(); });
  }

  void Stop() {
    stop_.store(true);
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  void Enqueue(AudioItem item) {
    std::lock_guard<std::mutex> lock(mu_);
    queue_.push_back(std::move(item));
  }

 private:
  void RunLoop() {
    while (!stop_.load()) {
      AudioItem item;
      bool has_item = false;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (!queue_.empty()) {
          item = std::move(queue_.front());
          queue_.pop_front();
          has_item = true;
        }
      }

      if (!has_item) {
        transport_->SendSilence();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }

      transport_->SendAudio(item.payload);
      if (item.loop) {
        Enqueue(item);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  MockAudioTransport* transport_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> started_{false};
  std::mutex mu_;
  std::deque<AudioItem> queue_;
  std::thread worker_;
};

}  // namespace

int main() {
  MockAudioTransport transport;
  AudioPlaybackQueue q(&transport);
  q.Start();

  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  if (transport.silence_frames() == 0) {
    std::cerr << "expected silence keepalive frames before audio enqueue" << std::endl;
    return 1;
  }

  AudioItem one;
  one.payload.assign(400, 0x42);
  q.Enqueue(one);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  if (transport.sent_audio_bytes() < 400) {
    std::cerr << "expected audio payload to be sent" << std::endl;
    return 2;
  }

  q.Stop();
  std::cout << "go2 grpc audio mock test passed" << std::endl;
  return 0;
}
