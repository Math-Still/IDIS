#include "smart_factory/http_server.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>


namespace smart_factory {
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace {
std::string mimeType(const std::filesystem::path& path) {
  const auto ext = path.extension().string();
  if (ext == ".html") return "text/html; charset=utf-8";
  if (ext == ".js") return "text/javascript; charset=utf-8";
  if (ext == ".css") return "text/css; charset=utf-8";
  if (ext == ".json") return "application/json; charset=utf-8";
  if (ext == ".svg") return "image/svg+xml";
  if (ext == ".png") return "image/png";
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".woff2") return "font/woff2";
  return "application/octet-stream";
}

template <class Body, class Allocator>
beast::error_code readHttpRequestWithDeadline(
    tcp::socket& socket,
    http::request_parser<Body, Allocator>& parser,
    std::chrono::milliseconds timeout) {
  beast::error_code ec;
  socket.non_blocking(true, ec);
  if (ec) return ec;

  const auto restoreBlocking = [&socket]() {
    beast::error_code ignored;
    socket.non_blocking(false, ignored);
  };
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::array<char, 8192> chunk{};

  while (!parser.is_done()) {
    ec.clear();
    const auto bytes = socket.read_some(net::buffer(chunk), ec);
    if (!ec) {
      std::size_t offset = 0;
      while (offset < bytes && !parser.is_done()) {
        beast::error_code parseEc;
        const auto consumed = parser.put(
            net::buffer(chunk.data() + offset, bytes - offset), parseEc);
        offset += consumed;
        if (parseEc && parseEc != http::error::need_more) {
          restoreBlocking();
          return parseEc;
        }
        // Beast may stop after parsing headers even when body bytes are already
        // present in the same TCP read. Feed every unconsumed byte before
        // waiting for more network data.
        if (consumed == 0) break;
      }
      continue;
    }

    if (ec == net::error::would_block || ec == net::error::try_again) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        restoreBlocking();
        return net::error::timed_out;
      }
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
      std::this_thread::sleep_for(std::min<std::chrono::milliseconds>(remaining, std::chrono::milliseconds(10)));
      continue;
    }

    if (ec == net::error::eof) {
      beast::error_code eofEc;
      parser.put_eof(eofEc);
      restoreBlocking();
      return eofEc;
    }

    restoreBlocking();
    return ec;
  }

  restoreBlocking();
  return {};
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string targetPath(const beast::string_view target) {
  auto value = std::string(target);
  const auto q = value.find('?');
  if (q != std::string::npos) value.resize(q);
  return value;
}

std::string urlDecode(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '+' ) { out.push_back(' '); continue; }
    if (input[i] == '%' && i + 2 < input.size()) {
      const auto hex = input.substr(i + 1, 2);
      try { out.push_back(static_cast<char>(std::stoi(hex, nullptr, 16))); i += 2; continue; } catch (...) {}
    }
    out.push_back(input[i]);
  }
  return out;
}

std::unordered_map<std::string, std::string> queryParams(const beast::string_view target) {
  std::unordered_map<std::string, std::string> result;
  const auto value = std::string(target);
  const auto q = value.find('?');
  if (q == std::string::npos || q + 1 >= value.size()) return result;
  std::stringstream ss(value.substr(q + 1));
  std::string pair;
  while (std::getline(ss, pair, '&')) {
    if (pair.empty()) continue;
    const auto eq = pair.find('=');
    const auto key = urlDecode(pair.substr(0, eq));
    const auto val = eq == std::string::npos ? std::string{} : urlDecode(pair.substr(eq + 1));
    result[key] = val;
  }
  return result;
}

std::optional<std::int64_t> parseIntParam(const std::unordered_map<std::string, std::string>& params, const char* key) {
  const auto it = params.find(key);
  if (it == params.end() || it->second.empty()) return std::nullopt;
  try { return std::stoll(it->second); } catch (...) { throw std::runtime_error(std::string("invalid query integer: ") + key); }
}

bool isVersionedApiPath(std::string_view path) {
  if (path.size() < 4 || path[0] != '/' || path[1] != 'v') return false;
  std::size_t i = 2;
  while (i < path.size() && path[i] >= '0' && path[i] <= '9') ++i;
  return i > 2 && i < path.size() && path[i] == '/';
}

bool isPublicApiRoute(std::string_view path, http::verb method) {
  return (path == "/v1/auth/status" && method == http::verb::get) ||
         (path == "/v1/auth/login" && method == http::verb::post) ||
         (path == "/v1/benchmark/ping" && method == http::verb::get);
}

std::optional<std::string> alarmActionId(const std::string& path, const std::string& suffix) {
  constexpr const char* prefix = "/v1/alarms/";
  if (path.rfind(prefix, 0) != 0 || path.size() <= std::char_traits<char>::length(prefix) + suffix.size()) return std::nullopt;
  if (path.substr(path.size() - suffix.size()) != suffix) return std::nullopt;
  const auto id = path.substr(std::char_traits<char>::length(prefix), path.size() - std::char_traits<char>::length(prefix) - suffix.size());
  if (id.empty() || id.find('/') != std::string::npos) return std::nullopt;
  return urlDecode(id);
}


std::optional<std::string> pathResourceId(const std::string& path, const std::string& prefix, const std::string& suffix) {
  if (path.rfind(prefix, 0) != 0 || path.size() <= prefix.size() + suffix.size()) return std::nullopt;
  if (!suffix.empty() && path.substr(path.size() - suffix.size()) != suffix) return std::nullopt;
  const auto end = suffix.empty() ? path.size() : path.size() - suffix.size();
  const auto id = path.substr(prefix.size(), end - prefix.size());
  if (id.empty() || id.find('/') != std::string::npos) return std::nullopt;
  return urlDecode(id);
}

http::response<http::string_body> jsonResponse(http::status status, boost::json::value value, int version,
                                               const std::string& origin) {
  http::response<http::string_body> res{status, version};
  res.set(http::field::content_type, "application/json; charset=utf-8");
  res.set(http::field::access_control_allow_origin, origin);
  res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
  res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
  res.body() = boost::json::serialize(value);
  res.prepare_payload();
  return res;
}

enum class WsMessageClass { Critical, State, Telemetry, Heartbeat };

WsMessageClass classifyWsPayload(const std::string& payload) {
  if (payload.find("\"type\":\"telemetry.updated\"") != std::string::npos) return WsMessageClass::Telemetry;
  if (payload.find("\"type\":\"device.updated\"") != std::string::npos ||
      payload.find("\"type\":\"communication.updated\"") != std::string::npos) return WsMessageClass::State;
  if (payload.find("\"type\":\"heartbeat\"") != std::string::npos) return WsMessageClass::Heartbeat;
  return WsMessageClass::Critical;
}

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
 public:
  WebSocketSession(beast::tcp_stream stream, int heartbeatMs, std::size_t queueCapacity)
      : ws_(std::move(stream)), heartbeatMs_(heartbeatMs), queueCapacity_(std::max<std::size_t>(8, queueCapacity)) {}

  ~WebSocketSession() {
    shutdown();
    if (writer_.joinable() && writer_.get_id() != std::this_thread::get_id()) writer_.join();
    finishSocketClose();
  }

  template <class Body, class Allocator>
  void run(http::request<Body, http::basic_fields<Allocator>> req) {
    try {
      ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
      ws_.set_option(websocket::stream_base::decorator([](websocket::response_type& res) {
        res.set(http::field::server, "smart-factory-cpp-backend");
      }));
      ws_.accept(req);
      ws_.text(true);
      writer_ = std::thread([this] { writerLoop(); });
      while (!closed_.load()) {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        boost::json::object envelope{{"type", "heartbeat"}, {"data", boost::json::object{{"backend", "cpp"}}},
                                     {"timestamp", now}};
        enqueue(boost::json::serialize(envelope));
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(heartbeatMs_);
        while (!closed_.load() && std::chrono::steady_clock::now() < until)
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    } catch (...) {
      close();
    }
    shutdown();
    if (writer_.joinable()) writer_.join();
    finishSocketClose();
  }

  void shutdown() {
    close();
    // A session writer may be blocked inside a synchronous socket write when
    // the peer stops reading. Shutting down the lowest layer interrupts that
    // write before join(), so server/session teardown cannot deadlock behind a
    // slow client.
    beast::error_code ec;
    ws_.next_layer().socket().shutdown(tcp::socket::shutdown_both, ec);
  }

  bool enqueue(const std::string& payload) {
    if (closed_.load()) return false;
    const auto messageClass = classifyWsPayload(payload);
    {
      std::lock_guard lock(queueMutex_);
      if (closed_.load()) return false;

      if (messageClass == WsMessageClass::Heartbeat) {
        // Heartbeats are replaceable. Never let a slow client accumulate a
        // backlog of transport liveness frames.
        for (auto it = queue_.rbegin(); it != queue_.rend(); ++it) {
          if (it->messageClass == WsMessageClass::Heartbeat) {
            it->payload = payload;
            return true;
          }
        }
      }

      if (queue_.size() >= queueCapacity_) {
        if (messageClass == WsMessageClass::Telemetry || messageClass == WsMessageClass::State ||
            messageClass == WsMessageClass::Heartbeat) {
          ++droppedLowPriority_;
          return true;
        }

        // Critical alarm/command/platform events may evict old presentation
        // traffic, but are never silently dropped. If the queue contains only
        // critical events, terminate the slow session; reconnect + dashboard
        // snapshot is safer than losing an authoritative critical event.
        for (auto it = queue_.begin(); it != queue_.end() && queue_.size() >= queueCapacity_;) {
          if (it->messageClass != WsMessageClass::Critical) {
            it = queue_.erase(it);
            ++droppedLowPriority_;
          } else {
            ++it;
          }
        }
        if (queue_.size() >= queueCapacity_) {
          closed_.store(true);
          queueCv_.notify_all();
          return false;
        }
      }
      queue_.push_back(QueuedMessage{payload, messageClass});
    }
    queueCv_.notify_one();
    return true;
  }

  bool closed() const { return closed_.load(); }

 private:
  struct QueuedMessage { std::string payload; WsMessageClass messageClass; };

  void writerLoop() {
    try {
      while (true) {
        QueuedMessage message;
        {
          std::unique_lock lock(queueMutex_);
          queueCv_.wait(lock, [this] { return closed_.load() || !queue_.empty(); });
          if (queue_.empty()) {
            if (closed_.load()) break;
            continue;
          }
          message = std::move(queue_.front());
          queue_.pop_front();
        }
        ws_.write(net::buffer(message.payload));
      }
    } catch (...) {
      closed_.store(true);
      queueCv_.notify_all();
    }
  }

  void close() {
    closed_.store(true);
    queueCv_.notify_all();
  }

  void finishSocketClose() {
    beast::error_code ec;
    try {
      if (ws_.is_open()) ws_.close(websocket::close_code::normal, ec);
    } catch (...) {}
  }

  websocket::stream<beast::tcp_stream> ws_;
  int heartbeatMs_;
  std::size_t queueCapacity_;
  std::mutex queueMutex_;
  std::condition_variable queueCv_;
  std::deque<QueuedMessage> queue_;
  std::thread writer_;
  std::atomic<bool> closed_{false};
  std::uint64_t droppedLowPriority_{0};
};

class WebSocketHub {
 public:
  explicit WebSocketHub(std::size_t maxSessions) : maxSessions_(std::max<std::size_t>(1, maxSessions)) {}

  bool tryAdd(const std::shared_ptr<WebSocketSession>& session) {
    std::lock_guard lock(mutex_);
    compactLocked();
    if (sessions_.size() >= maxSessions_) return false;
    sessions_.push_back(session);
    return true;
  }

  void broadcast(const std::string& payload) {
    std::vector<std::shared_ptr<WebSocketSession>> targets;
    {
      std::lock_guard lock(mutex_);
      auto out = sessions_.begin();
      for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (auto session = it->lock(); session && !session->closed()) {
          targets.push_back(session);
          *out++ = *it;
        }
      }
      sessions_.erase(out, sessions_.end());
    }
    // Never hold the hub mutex while enqueueing into an individual session.
    for (const auto& session : targets) session->enqueue(payload);
  }

  void shutdownAll() {
    std::vector<std::shared_ptr<WebSocketSession>> targets;
    {
      std::lock_guard lock(mutex_);
      for (auto& weak : sessions_) if (auto session = weak.lock()) targets.push_back(std::move(session));
      sessions_.clear();
    }
    for (const auto& session : targets) session->shutdown();
  }

 private:
  void compactLocked() {
    auto out = sessions_.begin();
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
      if (auto session = it->lock(); session && !session->closed()) *out++ = *it;
    }
    sessions_.erase(out, sessions_.end());
  }

  std::size_t maxSessions_;
  std::mutex mutex_;
  std::vector<std::weak_ptr<WebSocketSession>> sessions_;
};
}  // namespace

struct HttpServer::Impl {
  BackendConfig config;
  ApplicationService& app;
  net::io_context ioc{1};
  tcp::acceptor acceptor;
  std::atomic<bool> stopping{false};
  WebSocketHub hub;
  std::atomic<std::size_t> activeConnections{0};

  Impl(BackendConfig c, ApplicationService& a)
      : config(std::move(c)), app(a), acceptor(ioc), hub(config.maxWsSessions) {}

  static std::string bearerToken(const http::request<http::string_body>& req) {
    const auto it = req.find(http::field::authorization);
    if (it == req.end()) return {};
    const std::string value(it->value());
    constexpr const char* prefix = "Bearer ";
    if (value.rfind(prefix, 0) != 0) return {};
    return value.substr(std::char_traits<char>::length(prefix));
  }

  std::optional<SecurityContext> requestContext(const http::request<http::string_body>& req,
                                                const std::string& remoteAddress) {
    if (!app.authEnabled()) {
      SecurityContext ctx; ctx.principal = {"anonymous","Anonymous",UserRole::Administrator,false}; ctx.remoteAddress = remoteAddress; return ctx;
    }
    const auto token = bearerToken(req);
    if (token.empty()) return std::nullopt;
    const auto principal = app.authenticateToken(token);
    if (!principal) return std::nullopt;
    return SecurityContext{*principal, remoteAddress};
  }

  http::response<http::string_body> route(const http::request<http::string_body>& req,
                                          const std::string& remoteAddress) {
    const auto path = targetPath(req.target());
    const auto version = req.version();
    const auto origin = config.allowedOrigin;

    if (req.method() == http::verb::options) {
      auto res = jsonResponse(http::status::no_content, boost::json::object{}, version, origin);
      res.body().clear(); res.prepare_payload(); return res;
    }

    if (path == "/v1/auth/status" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, boost::json::object{{"authEnabled",app.authEnabled()},{"siteName",config.siteName}}, version, origin);


    // Public latency/availability probe. It exposes no operational data and is
    // intentionally kept outside the authenticated API boundary so the
    // benchmark harness can measure transport health before operator login.
    if (path == "/v1/benchmark/ping" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, boost::json::object{{"ok",true},{"backend","cpp"}}, version, origin);

    if (path == "/v1/auth/login" && req.method() == http::verb::post) {
      try {
        if (!app.authEnabled()) return jsonResponse(http::status::ok, boost::json::object{{"authEnabled",false},{"user",boost::json::object{{"id","anonymous"},{"displayName","Anonymous"},{"role","ADMINISTRATOR"},{"authenticated",false}}}}, version, origin);
        const auto parsed = boost::json::parse(req.body());
        if (!parsed.is_object()) return jsonResponse(http::status::bad_request, boost::json::object{{"error","JSON object required"}}, version, origin);
        const auto& body = parsed.as_object();
        const auto* idValue = body.if_contains("userId"); const auto* tokenValue = body.if_contains("token");
        if (!idValue || !idValue->is_string() || !tokenValue || !tokenValue->is_string()) return jsonResponse(http::status::bad_request, boost::json::object{{"error","userId and token are required"}}, version, origin);
        const std::string userId(idValue->as_string()); const std::string token(tokenValue->as_string());
        const auto principal = app.authenticate(userId, token);
        if (!principal) { app.recordAuthAudit("AUTH_LOGIN","DENIED",userId,"invalid credentials",remoteAddress); return jsonResponse(http::status::unauthorized, boost::json::object{{"error","invalid credentials"}}, version, origin); }
        app.recordAuthAudit("AUTH_LOGIN","SUCCESS",principal->id,"authenticated",remoteAddress,*principal);
        return jsonResponse(http::status::ok, boost::json::object{{"authEnabled",true},{"user",app.principalJson(*principal)}}, version, origin);
      } catch (const std::exception& e) {
        return jsonResponse(http::status::bad_request, boost::json::object{{"error",e.what()}}, version, origin);
      }
    }

    // Static web resources remain public so the login screen can load. Every
    // versioned API endpoint is authenticated by default. Public routes are
    // explicitly allow-listed so a future /v2+ prefix cannot bypass auth.
    std::optional<SecurityContext> actor;
    if (isVersionedApiPath(path) && !isPublicApiRoute(path, req.method())) {
      actor = requestContext(req, remoteAddress);
      if (!actor) {
        app.recordAuthAudit("AUTH_REQUEST","DENIED","unknown","missing or invalid bearer token",remoteAddress);
        return jsonResponse(http::status::unauthorized, boost::json::object{{"error","authentication required"}}, version, origin);
      }
      if (!app.authorize(actor->principal, Permission::View)) return jsonResponse(http::status::forbidden, boost::json::object{{"error","forbidden"}}, version, origin);
    }

    if (path == "/v1/auth/me" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, boost::json::object{{"authEnabled",app.authEnabled()},{"user",app.principalJson(actor->principal)}}, version, origin);
    if (path == "/v1/runtime/status" && req.method() == http::verb::get) {
      if (!app.authorize(actor->principal, Permission::RuntimeInspect))
        return jsonResponse(http::status::forbidden, boost::json::object{{"error","forbidden: runtime.inspect permission required"},{"code","RUNTIME_INSPECT_FORBIDDEN"}}, version, origin);
      return jsonResponse(http::status::ok, app.runtimeStatusJson(), version, origin);
    }
    if (path == "/v1/dashboard" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, app.dashboardSnapshot(), version, origin);
    if (path == "/v1/devices" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, app.devices(), version, origin);
    if (path == "/v1/alarms" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, app.alarms(), version, origin);
    if (path == "/v1/communication/health" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, app.communicationHealth(), version, origin);

    // Platform v2 configuration/asset contract. These routes intentionally live
    // beside v1 during migration; B1 does not change the meaning of existing v1 fields.
    if (path == "/v2/config/status" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, app.platformConfigStatus(), version, origin);
    if (path == "/v2/config/snapshot" && req.method() == http::verb::get) {
      if (!app.authorize(actor->principal, Permission::ConfigEdit))
        return jsonResponse(http::status::forbidden, boost::json::object{{"error","forbidden: config.edit permission required"},{"code","CONFIG_EDIT_FORBIDDEN"}}, version, origin);
      return jsonResponse(http::status::ok, app.platformConfigSnapshot(), version, origin);
    }
    if (path == "/v2/assets" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, app.platformAssets(), version, origin);
    if (path == "/v2/application-instances" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, app.applicationInstances(), version, origin);
    if (path == "/v2/rules" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, app.platformRules(), version, origin);
    if (path == "/v2/alarm-occurrences" && req.method() == http::verb::get) {
      const auto params=queryParams(req.target()); std::string deviceId, sourceDomain;
      if(const auto it=params.find("deviceId");it!=params.end())deviceId=it->second;
      if(const auto it=params.find("sourceDomain");it!=params.end())sourceDomain=it->second;
      return jsonResponse(http::status::ok, app.alarmOccurrencesV2(deviceId,sourceDomain), version, origin);
    }
    if (path == "/v2/incidents" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, app.incidentsV2(), version, origin);
    if (path == "/v2/agent/status" && req.method() == http::verb::get)
      return jsonResponse(http::status::ok, app.agentStatusV2(*actor), version, origin);
    if (path == "/v2/timeline" && req.method() == http::verb::get) {
      const auto params=queryParams(req.target()); const auto from=parseIntParam(params,"from").value_or(0); const auto to=parseIntParam(params,"to").value_or(INT64_MAX);
      if(from<0||to<from)return jsonResponse(http::status::bad_request,boost::json::object{{"error","invalid timeline range"},{"code","TIMELINE_RANGE_INVALID"}},version,origin);
      const auto it=params.find("instanceId"); const std::string instanceId=it==params.end()?std::string():it->second;
      return jsonResponse(http::status::ok,app.timelineV2(from,to,instanceId),version,origin);
    }
    if (const auto replayId=pathResourceId(path,"/v2/replay-sessions/","")) if(req.method()==http::verb::get)
      return jsonResponse(http::status::ok,app.replaySessionV2(*replayId,*actor),version,origin);
    if (const auto reportId=pathResourceId(path,"/v2/report-jobs/","")) if(req.method()==http::verb::get)
      return jsonResponse(http::status::ok,app.reportJobV2(*reportId,*actor),version,origin);
    if (req.method() == http::verb::get) {
      if (const auto commandId=pathResourceId(path,"/v2/commands/","")) {
        auto body=app.commandV2(*commandId,*actor);
        const auto code=body.if_contains("code")&&body.at("code").is_string()?std::string(body.at("code").as_string()):std::string{};
        return jsonResponse(code=="COMMAND_NOT_FOUND"?http::status::not_found:http::status::ok,std::move(body),version,origin);
      }
    }
    if (path == "/v2/telemetry/latest" && req.method() == http::verb::get) {
      const auto params=queryParams(req.target());
      std::string deviceId, pointId;
      if(const auto it=params.find("deviceId");it!=params.end()) deviceId=it->second;
      if(const auto it=params.find("pointId");it!=params.end()) pointId=it->second;
      return jsonResponse(http::status::ok, app.latestTelemetry(deviceId,pointId), version, origin);
    }
    if (path == "/v2/telemetry/history" && req.method() == http::verb::get) {
      try {
        const auto params=queryParams(req.target()); TelemetryHistoryQuery query;
        if(const auto it=params.find("deviceId");it!=params.end())query.deviceId=it->second;
        if(const auto it=params.find("pointId");it!=params.end())query.pointId=it->second;
        query.fromTs=parseIntParam(params,"from").value_or(0); query.toTs=parseIntParam(params,"to").value_or(INT64_MAX); query.limit=static_cast<std::size_t>(parseIntParam(params,"limit").value_or(2000));
        if(query.fromTs<0||query.toTs<query.fromTs||query.limit<1||query.limit>10000) return jsonResponse(http::status::bad_request,boost::json::object{{"error","invalid history query range/limit"},{"code","TELEMETRY_QUERY_INVALID"}},version,origin);
        auto result=app.telemetryHistory(query); result["contextId"]="live:"+config.siteName; result["configRevision"]=app.platformConfigStatus().if_contains("runningRevision")?app.platformConfigStatus().at("runningRevision"):boost::json::value("legacy-registry");
        return jsonResponse(http::status::ok,std::move(result),version,origin);
      } catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()},{"code","TELEMETRY_QUERY_FAILED"}},version,origin);}
    }
    if (path == "/v2/config-drafts" && req.method() == http::verb::post) {
      try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object()) return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.createConfigDraft(parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin); }
      catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
    }
    if (path == "/v2/commands/preview" && req.method() == http::verb::post) {
      try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object())return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.previewCommandV2(parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin);}
      catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
    }
    if (path == "/v2/commands" && req.method() == http::verb::post) {
      try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object())return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.issueCommandV2(parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin);}
      catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
    }
    if (path == "/v2/replay-sessions" && req.method() == http::verb::post) {
      try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object())return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.createReplaySessionV2(parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin);}
      catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()},{"code","REPLAY_REQUEST_FAILED"}},version,origin);}
    }
    if (path == "/v2/report-jobs" && req.method() == http::verb::post) {
      try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object())return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.createReportJobV2(parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin);}
      catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()},{"code","REPORT_REQUEST_FAILED"}},version,origin);}
    }
    if (req.method() == http::verb::post) {
      if (path == "/v2/agent/chat") {
        try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object())return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.agentChatV2(parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin);}
        catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
      }
      if (path == "/v2/incidents") {
        try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object())return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.createIncidentV2(parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin);}
        catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
      }
      if (const auto commandId=pathResourceId(path,"/v2/commands/","/reconciliations")) {
        try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object())return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.reconcileCommandV2(*commandId,parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin);}
        catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
      }
      if (const auto occurrenceId=pathResourceId(path,"/v2/alarm-occurrences/","/acknowledge")) {
        try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object())return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.acknowledgeAlarmOccurrenceV2(*occurrenceId,parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin);}
        catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
      }
      if (const auto incidentId=pathResourceId(path,"/v2/incidents/","/transitions")) {
        try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object())return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.transitionIncidentV2(*incidentId,parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin);}
        catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
      }
      if (const auto draftId=pathResourceId(path,"/v2/config-drafts/","/validate")) {
        auto result=app.validateConfigDraft(*draftId,*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin);
      }
      if (const auto draftId=pathResourceId(path,"/v2/config-drafts/","/publish")) {
        try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object()) return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.publishConfigDraft(*draftId,parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin); }
        catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
      }
      if (const auto revision=pathResourceId(path,"/v2/config-revisions/","/rollback")) {
        try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object()) return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.rollbackConfig(*revision,parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin); }
        catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
      }
    }

    if (path == "/v1/history/telemetry" && req.method() == http::verb::get) {
      try {
        const auto params = queryParams(req.target()); TelemetryHistoryQuery query;
        if (const auto it=params.find("deviceId");it!=params.end()) query.deviceId=it->second;
        if (const auto it=params.find("pointId");it!=params.end()) query.pointId=it->second;
        query.fromTs=parseIntParam(params,"from").value_or(0); query.toTs=parseIntParam(params,"to").value_or(INT64_MAX); query.limit=static_cast<std::size_t>(parseIntParam(params,"limit").value_or(2000));
        if(query.fromTs<0||query.toTs<query.fromTs||query.limit<1||query.limit>10000) return jsonResponse(http::status::bad_request,boost::json::object{{"error","invalid history query range/limit"}},version,origin);
        return jsonResponse(http::status::ok,app.telemetryHistory(query),version,origin);
      } catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
    }
    if (path == "/v1/history/alarms" && req.method() == http::verb::get) {
      try {
        const auto params=queryParams(req.target()); AlarmHistoryQuery query;
        if(const auto it=params.find("deviceId");it!=params.end())query.deviceId=it->second;
        if(const auto it=params.find("alarmId");it!=params.end())query.alarmId=it->second;
        query.fromTs=parseIntParam(params,"from").value_or(0); query.toTs=parseIntParam(params,"to").value_or(INT64_MAX); query.limit=static_cast<std::size_t>(parseIntParam(params,"limit").value_or(2000));
        if(query.fromTs<0||query.toTs<query.fromTs||query.limit<1||query.limit>10000)return jsonResponse(http::status::bad_request,boost::json::object{{"error","invalid alarm history query range/limit"}},version,origin);
        return jsonResponse(http::status::ok,app.alarmHistory(query),version,origin);
      }catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
    }
    if (path == "/v1/audit" && req.method() == http::verb::get) {
      if (!app.authorize(actor->principal, Permission::AuditView)) return jsonResponse(http::status::forbidden,boost::json::object{{"error","forbidden: audit.view permission required"}},version,origin);
      try {
        const auto params=queryParams(req.target()); AuditQuery query;
        if(const auto it=params.find("actorId");it!=params.end())query.actorId=it->second;
        if(const auto it=params.find("action");it!=params.end())query.action=it->second;
        if(const auto it=params.find("resourceType");it!=params.end())query.resourceType=it->second;
        query.fromTs=parseIntParam(params,"from").value_or(0); query.toTs=parseIntParam(params,"to").value_or(INT64_MAX); query.limit=static_cast<std::size_t>(parseIntParam(params,"limit").value_or(1000));
        if(query.fromTs<0||query.toTs<query.fromTs||query.limit<1||query.limit>10000)return jsonResponse(http::status::bad_request,boost::json::object{{"error","invalid audit query range/limit"}},version,origin);
        return jsonResponse(http::status::ok,app.auditHistory(query,*actor),version,origin);
      }catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
    }
    if (path == "/v1/commands" && req.method() == http::verb::post) {
      try {
        const auto parsed=boost::json::parse(req.body()); if(!parsed.is_object())return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin);
        auto result=app.issueCommand(parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin);
      }catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
    }

    if (req.method() == http::verb::post) {
      if (const auto alarmId=alarmActionId(path,"/ack")) {
        try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object())return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.acknowledgeAlarm(*alarmId,parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin); }
        catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
      }
      if (const auto alarmId=alarmActionId(path,"/clear")) {
        try { const auto parsed=req.body().empty()?boost::json::value(boost::json::object{}):boost::json::parse(req.body()); if(!parsed.is_object())return jsonResponse(http::status::bad_request,boost::json::object{{"error","JSON object required"}},version,origin); auto result=app.clearAlarm(*alarmId,parsed.as_object(),*actor); return jsonResponse(static_cast<http::status>(result.httpStatus),std::move(result.body),version,origin); }
        catch(const std::exception& e){return jsonResponse(http::status::bad_request,boost::json::object{{"error",e.what()}},version,origin);}
      }
    }

    if (isVersionedApiPath(path)) {
      if (req.method()!=http::verb::get) return jsonResponse(http::status::method_not_allowed,boost::json::object{{"error","method not allowed"}},version,origin);
      return jsonResponse(http::status::not_found,boost::json::object{{"error","API route not found"}},version,origin);
    }

    if (req.method() != http::verb::get) return jsonResponse(http::status::method_not_allowed,boost::json::object{{"error","method not allowed"}},version,origin);
    std::filesystem::path root=std::filesystem::weakly_canonical(config.webRoot);
    auto relative=path=="/"?std::filesystem::path("index.html"):std::filesystem::path(path.substr(1));
    if(relative.is_absolute()||relative.string().find("..")!=std::string::npos)return jsonResponse(http::status::bad_request,boost::json::object{{"error","invalid path"}},version,origin);
    auto file=std::filesystem::weakly_canonical(root/relative);
    if(file.string().rfind(root.string(),0)!=0||!std::filesystem::exists(file)||std::filesystem::is_directory(file))file=root/"index.html";
    const auto body=readFile(file); if(body.empty()&&!std::filesystem::exists(file))return jsonResponse(http::status::not_found,boost::json::object{{"error","not found"}},version,origin);
    http::response<http::string_body> res{http::status::ok,version}; res.set(http::field::content_type,mimeType(file)); res.set(http::field::access_control_allow_origin,origin); res.body()=body; res.prepare_payload(); return res;
  }

  void handle(tcp::socket socket) {
    try {
      beast::tcp_stream stream(std::move(socket));
      const auto remoteAddress = stream.socket().remote_endpoint().address().to_string();
      // The server intentionally handles one HTTP request per connection. Use a
      // bounded non-blocking parse loop so slow/idle clients cannot occupy a
      // connection thread indefinitely. This deadline covers the complete HTTP
      // request, including partial headers/bodies, not only the first byte.
      http::request_parser<http::string_body> parser;
      parser.body_limit(config.maxRequestBodyBytes);
      auto readEc = readHttpRequestWithDeadline(
          stream.socket(), parser, std::chrono::milliseconds(config.httpRequestTimeoutMs));
      if (readEc == http::error::body_limit) {
        auto res = jsonResponse(http::status::payload_too_large,
          boost::json::object{{"error","request body exceeds configured limit"},{"code","REQUEST_BODY_TOO_LARGE"}},
          11, config.allowedOrigin);
        http::write(stream, res);
        return;
      }
      if (readEc) throw boost::system::system_error(readEc);
      auto req = parser.release();
      if (websocket::is_upgrade(req) && targetPath(req.target()) == "/ws") {
        bool allowed = !app.authEnabled();
        if (app.authEnabled()) {
          const auto params = queryParams(req.target());
          const auto it = params.find("access_token");
          if (it != params.end() && !it->second.empty()) allowed = static_cast<bool>(app.authenticateToken(it->second));
        }
        if (!allowed) {
          app.recordAuthAudit("AUTH_WEBSOCKET","DENIED","unknown","invalid websocket token",remoteAddress);
          auto res = jsonResponse(http::status::unauthorized, boost::json::object{{"error","websocket authentication required"}}, req.version(), config.allowedOrigin);
          http::write(stream, res); return;
        }
        stream.expires_never();
        auto session = std::make_shared<WebSocketSession>(std::move(stream), config.heartbeatMs, config.wsOutboundQueueCapacity);
        if (!hub.tryAdd(session)) {
          app.recordAuthAudit("WEBSOCKET_CONNECT","REJECTED","unknown","websocket session limit reached",remoteAddress);
          return;
        }
        session->run(std::move(req));
        return;
      }
      auto res = route(req, remoteAddress);
      res.keep_alive(false);
      http::write(stream, res);
      beast::error_code ec;
      stream.socket().shutdown(tcp::socket::shutdown_send, ec);
    } catch (const std::exception& e) {
      std::cerr << "connection error: " << e.what() << '\n';
    }
  }

  void run() {
    const auto address = net::ip::make_address(config.listenHost);
    tcp::endpoint endpoint{address, config.listenPort};
    acceptor.open(endpoint.protocol());
    acceptor.set_option(net::socket_base::reuse_address(true));
    acceptor.bind(endpoint);
    acceptor.listen(net::socket_base::max_listen_connections);
    std::cout << "smart-factory C++ backend listening on " << config.listenHost << ':' << config.listenPort << '\n';
    while (!stopping.load()) {
      beast::error_code ec;
      tcp::socket socket{ioc};
      acceptor.accept(socket, ec);
      if (ec) {
        if (!stopping.load()) std::cerr << "accept error: " << ec.message() << '\n';
        continue;
      }
      if (activeConnections.fetch_add(1) >= config.maxHttpConnections) {
        activeConnections.fetch_sub(1);
        beast::error_code closeEc;
        socket.shutdown(tcp::socket::shutdown_both, closeEc);
        socket.close(closeEc);
        continue;
      }
      std::thread([this, s = std::move(socket)]() mutable {
        handle(std::move(s));
        activeConnections.fetch_sub(1);
      }).detach();
    }
  }

  void stop() {
    stopping.store(true);
    hub.shutdownAll();
    beast::error_code ec;
    acceptor.close(ec);
  }
};

HttpServer::HttpServer(BackendConfig config, ApplicationService& application)
    : impl_(std::make_unique<Impl>(std::move(config), application)) {}
HttpServer::~HttpServer() = default;
void HttpServer::run() { impl_->run(); }
void HttpServer::stop() { impl_->stop(); }
void HttpServer::broadcast(const std::string& payload) { impl_->hub.broadcast(payload); }

}  // namespace smart_factory
