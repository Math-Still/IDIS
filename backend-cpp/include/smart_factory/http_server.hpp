#pragma once

#include "smart_factory/application.hpp"
#include "smart_factory/config.hpp"

#include <memory>

namespace smart_factory {

class HttpServer {
 public:
  HttpServer(BackendConfig config, ApplicationService& application);
  ~HttpServer();
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  void run();
  void stop();
  void broadcast(const std::string& payload);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace smart_factory
