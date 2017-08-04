#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <sstream>

#include <franka/exception.h>

#include <Poco/Net/DatagramSocket.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/StreamSocket.h>

namespace franka {

class Network {
 public:
  Network(const std::string& franka_address,
          uint16_t franka_port,
          std::chrono::milliseconds tcp_timeout = std::chrono::seconds(60),
          std::chrono::milliseconds udp_timeout = std::chrono::seconds(1),
          std::tuple<bool, int, int, int> tcp_keepalive = std::make_tuple(true, 1, 3, 1));
  ~Network();

  uint16_t udpPort() const noexcept;

  template <typename T>
  T udpBlockingReceive();

  template <typename T>
  bool udpReceive(T* data);

  template <typename T>
  void udpSend(const T& data);

  void tcpThrowIfConnectionClosed();

  void tcpReceiveIntoBuffer(uint8_t* buffer, size_t read_size);

  template <typename T>
  typename T::Response tcpBlockingReceiveResponse(uint32_t command_id);

  template <typename T>
  bool tcpReceiveResponse(uint32_t command_id,
                          std::function<void(const typename T::Response&)> handler);

  template <typename T, typename... TArgs>
  uint32_t tcpSendRequest(TArgs&&... args);

  template <typename T>
  typename T::Response executeCommand(const typename T::Request& request);

  template <typename T, typename... TArgs>
  typename T::Response executeCommand(TArgs... args);

 private:
  template <typename T>
  T udpBlockingReceiveUnsafe();

  void tcpReceiveIntoBufferUnsafe(uint8_t* buffer, size_t read_size);

  template <typename T>
  bool tcpPeekHeaderUnsafe(T* header);

  Poco::Net::StreamSocket tcp_socket_;
  Poco::Net::DatagramSocket udp_socket_;
  Poco::Net::SocketAddress udp_server_address_;
  uint16_t udp_port_;

  std::mutex tcp_mutex_;
  std::mutex udp_mutex_;

  uint32_t command_id_{0};
};

template <typename T>
bool Network::udpReceive(T* data) {
  std::lock_guard<std::mutex> _(udp_mutex_);

  if (udp_socket_.available() >= static_cast<int>(sizeof(T))) {
    *data = udpBlockingReceiveUnsafe<T>();
    return true;
  }
  return false;
}

template <typename T>
T Network::udpBlockingReceive() {
  std::lock_guard<std::mutex> _(udp_mutex_);
  return udpBlockingReceiveUnsafe<T>();
}

template <typename T>
T Network::udpBlockingReceiveUnsafe() try {
  std::array<uint8_t, sizeof(T)> buffer;

  int bytes_received =
      udp_socket_.receiveFrom(buffer.data(), static_cast<int>(buffer.size()), udp_server_address_);

  if (bytes_received != buffer.size()) {
    throw ProtocolException("libfranka: incorrect object size");
  }

  return *reinterpret_cast<T*>(buffer.data());
} catch (const Poco::Exception& e) {
  using namespace std::string_literals;  // NOLINT (google-build-using-namespace)
  throw NetworkException("libfranka: UDP receive: "s + e.what());
}

template <typename T>
void Network::udpSend(const T& data) try {
  std::lock_guard<std::mutex> _(udp_mutex_);

  int bytes_sent = udp_socket_.sendTo(&data, sizeof(data), udp_server_address_);
  if (bytes_sent != sizeof(data)) {
    throw NetworkException("libfranka: could not send UDP data");
  }
} catch (const Poco::Exception& e) {
  using namespace std::string_literals;  // NOLINT (google-build-using-namespace)
  throw NetworkException("libfranka: UDP send: "s + e.what());
}

template <typename T, typename... TArgs>
uint32_t Network::tcpSendRequest(TArgs&&... args) try {
  std::lock_guard<std::mutex> _(tcp_mutex_);

  struct {
    typename T::Header header;
    typename T::Request payload;
  } request{
      .header = typename T::Header(T::kCommand, command_id_++),
      .payload = typename T::Request(std::forward<TArgs>(args)...),
  };
  tcp_socket_.sendBytes(&request, sizeof(request));

  return request.header.command_id;
} catch (const Poco::Exception& e) {
  using namespace std::string_literals;  // NOLINT (google-build-using-namespace)
  throw NetworkException("libfranka: TCP send bytes: "s + e.what());
}

template <typename T>
bool Network::tcpPeekHeaderUnsafe(T* header) try {
  if (tcp_socket_.poll(0, Poco::Net::Socket::SELECT_READ) &&
      tcp_socket_.available() >= static_cast<int>(sizeof(T))) {
    int rv = tcp_socket_.receiveBytes(header, sizeof(T), MSG_PEEK);
    if (rv != sizeof(T)) {
      throw NetworkException("libfranka: Could not read data.");
    }
    return true;
  }
  return false;
} catch (const Poco::Exception& e) {
  using namespace std::string_literals;  // NOLINT (google-build-using-namespace)
  throw NetworkException("libfranka: "s + e.what());
}

template <typename T>
bool Network::tcpReceiveResponse(uint32_t command_id,
                                 std::function<void(const typename T::Response&)> handler) {
  std::lock_guard<std::mutex> _(tcp_mutex_);

  struct {
    typename T::Header header;
    std::array<uint8_t, sizeof(typename T::Response)> payload;
  } response;

  if (!tcpPeekHeaderUnsafe(&response.header) || response.header.command != T::kCommand ||
      response.header.command_id != command_id) {
    return false;
  }

  // We peeked the correct header, so now we have to block and wait for the rest of the message.
  tcpReceiveIntoBufferUnsafe(reinterpret_cast<uint8_t*>(&response), sizeof(response));
  handler(*reinterpret_cast<typename T::Response*>(response.payload.data()));
  return true;
}

template <typename T>
typename T::Response Network::tcpBlockingReceiveResponse(uint32_t command_id) {
  struct {
    typename T::Header header;
    std::array<uint8_t, sizeof(typename T::Response)> payload;
  } response;

  // Wait until we receive a packet with the right header.
  std::unique_lock<std::mutex> lock(tcp_mutex_, std::defer_lock);
  while (true) {
    lock.lock();
    tcp_socket_.poll(Poco::Timespan(0, 1e4), Poco::Net::Socket::SELECT_READ);
    if (tcpPeekHeaderUnsafe(&response.header) && response.header.command == T::kCommand &&
        response.header.command_id == command_id) {
      break;
    }
    lock.unlock();
  }
  tcpReceiveIntoBufferUnsafe(reinterpret_cast<uint8_t*>(&response), sizeof(response));
  return *reinterpret_cast<typename T::Response*>(response.payload.data());
}

template <typename T, uint16_t kLibraryVersion>
void connect(Network& network, uint16_t* ri_version) {
  uint32_t command_id = network.tcpSendRequest<T>(network.udpPort());
  typename T::Response connect_response = network.tcpBlockingReceiveResponse<T>(command_id);
  switch (connect_response.status) {
    case (T::Status::kIncompatibleLibraryVersion): {
      std::stringstream message;
      message << "libfranka: incompatible library version. " << std::endl
              << "Server version: " << connect_response.version << std::endl
              << "Library version: " << kLibraryVersion;
      throw IncompatibleVersionException(message.str());
    }
    case (T::Status::kSuccess): {
      *ri_version = connect_response.version;
      break;
    }
    default:
      throw ProtocolException(
          "libfranka gripper: protocol error during gripper connection attempt");
  }
}

}  // namespace franka
