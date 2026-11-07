#include "TdClient.h"

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

namespace td_api = td::td_api;

namespace core {
namespace {
template <class... Fs> struct overloaded : Fs... { using Fs::operator()...; };
template <class... Fs> overloaded(Fs...) -> overloaded<Fs...>;
}  // namespace

struct TdClient::Impl {
  td::ClientManager manager;
  std::int32_t client_id = 0;
  std::atomic<std::uint64_t> next_id{0};

  void send(td_api::object_ptr<td_api::Function> f) {
    manager.send(client_id, ++next_id, std::move(f));
  }
};

TdClient::TdClient(Config cfg) : cfg_(std::move(cfg)) {}
TdClient::~TdClient() { stop(); }

void TdClient::start() {
  if (worker_.joinable()) return;
  td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
  impl_ = std::make_unique<Impl>();
  impl_->client_id = impl_->manager.create_client_id();
  impl_->send(td_api::make_object<td_api::getOption>("version"));
  worker_ = std::thread([this] { loop(); });
}

void TdClient::stop() {
  if (!worker_.joinable()) return;
  impl_->send(td_api::make_object<td_api::close>());
  worker_.join();  // цикл выходит на authorizationStateClosed
  impl_.reset();
}

void TdClient::loop() {
  auto auth = [this](td_api::object_ptr<td_api::AuthorizationState> st) {
    td_api::downcast_call(
        *st,
        overloaded(
            [this](td_api::authorizationStateWaitTdlibParameters &) {
              auto p = td_api::make_object<td_api::setTdlibParameters>();
              p->database_directory_ = cfg_.data_dir;
              p->use_message_database_ = true;
              p->use_file_database_ = true;
              p->use_chat_info_database_ = true;
              p->use_secret_chats_ = false;
              p->api_id_ = cfg_.api_id;
              p->api_hash_ = cfg_.api_hash;
              p->system_language_code_ = cfg_.lang;
              p->device_model_ = cfg_.device_model;
              p->application_version_ = cfg_.app_version;
              impl_->send(std::move(p));
            },
            [this](td_api::authorizationStateWaitPhoneNumber &) { events_.push(EvAuth{AuthState::WaitPhone}); },
            [this](td_api::authorizationStateWaitCode &) { events_.push(EvAuth{AuthState::WaitCode}); },
            [this](td_api::authorizationStateWaitPassword &) { events_.push(EvAuth{AuthState::WaitPassword}); },
            [this](td_api::authorizationStateReady &) {
              events_.push(EvAuth{AuthState::Ready});
              auto f = td_api::make_object<td_api::loadChats>();
              f->limit_ = 50;
              impl_->send(std::move(f));
            },
            [this](td_api::authorizationStateClosed &) {
              events_.push(EvAuth{AuthState::Closed});
              closed_ = true;
            },
            [](auto &) {}));
  };

  while (!closed_) {
    auto r = impl_->manager.receive(1.0);
    if (!r.object) continue;

    if (r.object->get_id() == td_api::error::ID) {
      auto &e = static_cast<td_api::error &>(*r.object);
      if (e.code_ != 404) {  // 404 от loadChats значит "все чаты уже загружены"
        events_.push(EvError{e.code_, e.message_});
      }
      continue;
    }
    if (r.request_id != 0) continue;  // ответы на запросы пока не нужны

    td_api::downcast_call(
        *r.object,
        overloaded(
            [&](td_api::updateAuthorizationState &u) { auth(std::move(u.authorization_state_)); },
            [&](td_api::updateNewChat &u) { events_.push(EvChat{u.chat_->id_, u.chat_->title_}); },
            [&](td_api::updateChatTitle &u) { events_.push(EvChat{u.chat_id_, u.title_}); },
            [&](td_api::updateNewMessage &u) {
              auto &m = *u.message_;
              if (m.content_->get_id() != td_api::messageText::ID) return;
              auto &t = static_cast<td_api::messageText &>(*m.content_);
              events_.push(EvMessage{m.chat_id_, m.id_, m.is_outgoing_, m.date_, t.text_->text_});
            },
            [](auto &) {}));
  }
}

void TdClient::setPhone(const std::string &phone) {
  auto f = td_api::make_object<td_api::setAuthenticationPhoneNumber>();
  f->phone_number_ = phone;
  impl_->send(std::move(f));
}

void TdClient::setCode(const std::string &code) {
  auto f = td_api::make_object<td_api::checkAuthenticationCode>();
  f->code_ = code;
  impl_->send(std::move(f));
}

void TdClient::setPassword(const std::string &password) {
  auto f = td_api::make_object<td_api::checkAuthenticationPassword>();
  f->password_ = password;
  impl_->send(std::move(f));
}

void TdClient::sendText(std::int64_t chat_id, const std::string &text) {
  auto content = td_api::make_object<td_api::inputMessageText>();
  content->text_ = td_api::make_object<td_api::formattedText>();
  content->text_->text_ = text;
  auto f = td_api::make_object<td_api::sendMessage>();
  f->chat_id_ = chat_id;
  f->input_message_content_ = std::move(content);
  impl_->send(std::move(f));
}

void TdClient::markRead(std::int64_t chat_id, std::int64_t msg_id) {
  auto f = td_api::make_object<td_api::viewMessages>();
  f->chat_id_ = chat_id;
  f->message_ids_ = {msg_id};
  f->force_read_ = true;
  impl_->send(std::move(f));
}

}  // namespace core
