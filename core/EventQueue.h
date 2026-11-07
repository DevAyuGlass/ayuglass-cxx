#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

namespace core {

enum class AuthState { Init, WaitPhone, WaitCode, WaitPassword, Ready, Closed };

struct EvAuth    { AuthState state; };
struct EvChat    { std::int64_t id; std::string title; };
struct EvMessage { std::int64_t chat_id; std::int64_t msg_id; bool outgoing; std::int32_t date; std::string text; };
struct EvError   { std::int32_t code; std::string message; };

using Event = std::variant<EvAuth, EvChat, EvMessage, EvError>;

// Рабочий поток TDLib кладёт события, UI-поток (и позже плагины) забирает раз в кадр.
class EventQueue {
 public:
  void push(Event e) {
    std::lock_guard<std::mutex> l(m_);
    q_.push_back(std::move(e));
  }
  std::vector<Event> drain() {
    std::vector<Event> out;
    std::lock_guard<std::mutex> l(m_);
    out.swap(q_);
    return out;
  }

 private:
  std::mutex m_;
  std::vector<Event> q_;
};

}  // namespace core
