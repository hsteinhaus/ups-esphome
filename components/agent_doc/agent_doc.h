// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome::agent_doc {

// One static file, compiled in, served on GET at a fixed URL.
class AgentDoc : public AsyncWebHandler, public Component {
 public:
  AgentDoc(web_server_base::WebServerBase *base, const uint8_t *data, size_t size, const char *url,
           const char *content_type)
      : base_(base), data_(data), size_(size), url_(url), content_type_(content_type) {}

  bool canHandle(AsyncWebServerRequest *request) const override {
    if (request->method() != HTTP_GET)
      return false;
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->url_to(url_buf) == this->url_;
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    request->send(request->beginResponse(200, this->content_type_, this->data_, this->size_));
  }

  void setup() override {
    this->base_->init();
    this->base_->add_handler(this);
  }
  // After WiFi, as web_server's own handlers.
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

 protected:
  web_server_base::WebServerBase *base_;
  const uint8_t *data_;
  size_t size_;
  const char *url_;
  const char *content_type_;
};

}  // namespace esphome::agent_doc
