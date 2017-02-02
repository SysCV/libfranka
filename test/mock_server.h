#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include <franka/robot_state.h>

#include "message_types.h"

class MockServer {
 public:
  using ConnectCallbackT = std::function<void(const message_types::ConnectRequest&, message_types::ConnectReply&)>;
  using SendRobotStateCallbackT = std::function<franka::RobotState()>;

  MockServer() = default;
  ~MockServer();

  MockServer& onConnect(ConnectCallbackT on_connect);
  MockServer& onSendRobotState(SendRobotStateCallbackT on_send_robot_state);
  void start();

 private:
  void serverThread();

  std::condition_variable cv_;
  std::mutex mutex_;
  std::thread server_thread_;

  ConnectCallbackT on_connect_;
  SendRobotStateCallbackT on_send_robot_state_;
};