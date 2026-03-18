#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
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

#include <arpa/inet.h>
#include <grpcpp/grpcpp.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <unitree/robot/channel/channel_factory.hpp>
#include <unitree/robot/go2/obstacles_avoid/obstacles_avoid_client.hpp>
#include <unitree/robot/go2/sport/sport_client.hpp>

#include "go2_sport.grpc.pb.h"

using go2::sport::v1::Action;
using go2::sport::v1::CloseSessionRequest;
using go2::sport::v1::CloseSessionResponse;
using go2::sport::v1::ExecuteActionRequest;
using go2::sport::v1::ExecuteActionResponse;
using go2::sport::v1::ForceCloseOwnerSessionsRequest;
using go2::sport::v1::ForceCloseOwnerSessionsResponse;
using go2::sport::v1::GetServerStatusRequest;
using go2::sport::v1::GetServerStatusResponse;
using go2::sport::v1::Go2SportService;
using go2::sport::v1::HeartbeatRequest;
using go2::sport::v1::HeartbeatResponse;
using go2::sport::v1::OpenSessionRequest;
using go2::sport::v1::OpenSessionResponse;
using go2::sport::v1::SessionStatus;

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
        // grpc clients need zone for link-local IPv6 routes.
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
  Go2SportServiceImpl(const std::string& network_interface, bool enable_lease)
      : stop_gc_(false) {
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

    gc_thread_ = std::thread([this]() { SessionGcLoop(); });
  }

  ~Go2SportServiceImpl() override {
    stop_gc_.store(true);
    if (gc_thread_.joinable()) {
      gc_thread_.join();
    }
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
  std::unique_ptr<unitree::robot::go2::SportClient> sport_client_;
  std::unique_ptr<unitree::robot::go2::ObstaclesAvoidClient> obstacles_avoid_client_;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0]
              << " <network_interface> <listen_address> <port> [enable_lease:0|1]" << std::endl;
    return 1;
  }

  const std::string network_interface = argv[1];
  const std::string listen_address = argv[2];
  const std::string port = argv[3];
  const bool enable_lease = (argc > 4) ? (std::string(argv[4]) == "1") : false;
  const uint16_t grpc_port = static_cast<uint16_t>(std::stoi(port));
  const AdvertisedAddresses advertised_addrs =
      ResolveAdvertisedAddresses(network_interface, listen_address);

  Go2SportServiceImpl service(network_interface, enable_lease);

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
