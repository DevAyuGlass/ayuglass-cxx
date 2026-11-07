#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "EventQueue.h"

namespace td { class ClientManager; }

namespace core {

struct Config {
  std::int32_t api_id = 0;
  std::string api_hash;
  std::string data_dir;        // куда TDLib кладёт базу
  std::string device_model = "Android";
  std::string app_version = "0.1";
  std::string lang = "ru";
};

class TdClient {
 public:
  explicit TdClient(Config cfg);
  ~TdClient();

  void start();
  void stop();

  EventQueue &events() { return events_; }

  void setPhone(const std::string &phone);
  void setCode(const std::string &code);
  void setPassword(const std::string &password);
  void sendText(std::int64_t chat_id, const std::string &text);
  // Ghost mode: прочтение НЕ отправляется автоматически. Вызывай только осознанно.
  void markRead(std::int64_t chat_id, std::int64_t msg_id);

 private:
  struct Impl;
  void loop();

  Config cfg_;
  EventQueue events_;
  std::unique_ptr<Impl> impl_;
  std::thread worker_;
  std::atomic<bool> closed_{false};
};

}  // namespace core
