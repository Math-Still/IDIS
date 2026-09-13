#include "smart_factory/agent_client.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/json.hpp>
#include <openssl/ssl.h>

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace smart_factory {
namespace {
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

struct ParsedUrl { std::string host; std::string port{"443"}; std::string basePath; };

ParsedUrl parseHttpsUrl(const std::string& url) {
  constexpr const char* prefix = "https://";
  if (url.rfind(prefix, 0) != 0) throw std::runtime_error("agent_base_url must use https://");
  std::string rest = url.substr(std::char_traits<char>::length(prefix));
  const auto slash = rest.find('/');
  std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
  std::string path = slash == std::string::npos ? "" : rest.substr(slash);
  const auto colon = authority.rfind(':');
  ParsedUrl result;
  if (colon != std::string::npos && authority.find(']') == std::string::npos) {
    result.host = authority.substr(0, colon);
    result.port = authority.substr(colon + 1);
  } else result.host = authority;
  result.basePath = path;
  while (!result.basePath.empty() && result.basePath.back() == '/') result.basePath.pop_back();
  if (result.host.empty()) throw std::runtime_error("agent_base_url host is empty");
  return result;
}

std::string apiKey(const AgentClientOptions& options) {
  if (options.apiKeyEnv.empty()) return {};
  const char* value = std::getenv(options.apiKeyEnv.c_str());
  return value ? std::string(value) : std::string();
}
}  // namespace

AgentClient::AgentClient(AgentClientOptions options) : options_(std::move(options)) {
  if (const char* value = std::getenv("DEEPSEEK_BASE_URL"); value && *value) options_.baseUrl = value;
  if (const char* value = std::getenv("DEEPSEEK_MODEL"); value && *value) options_.model = value;
}

bool AgentClient::configured() const {
  return options_.enabled && !apiKey(options_).empty() && !options_.model.empty() && !options_.baseUrl.empty();
}

std::string AgentClient::complete(const std::string& systemPrompt, const std::vector<std::pair<std::string,std::string>>& history, const std::string& userPrompt) const {
  if (!configured()) throw std::runtime_error("DeepSeek API key is not configured");
  const auto parsed = parseHttpsUrl(options_.baseUrl);
  const std::string key = apiKey(options_);

  net::io_context ioc;
  ssl::context ctx(ssl::context::tls_client);
  ctx.set_default_verify_paths();
  ctx.set_verify_mode(ssl::verify_peer);

  tcp::resolver resolver(ioc);
  beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
  if (!SSL_set_tlsext_host_name(stream.native_handle(), parsed.host.c_str()))
    throw std::runtime_error("cannot set TLS SNI for agent provider");
  stream.set_verify_callback(ssl::host_name_verification(parsed.host));
  beast::get_lowest_layer(stream).expires_after(std::chrono::milliseconds(options_.timeoutMs));
  const auto endpoints = resolver.resolve(parsed.host, parsed.port);
  beast::get_lowest_layer(stream).connect(endpoints);
  stream.handshake(ssl::stream_base::client);

  boost::json::array messages;
  messages.emplace_back(boost::json::object{{"role","system"},{"content",systemPrompt}});
  for (const auto& [role, content] : history) {
    if ((role == "user" || role == "assistant") && !content.empty())
      messages.emplace_back(boost::json::object{{"role",role},{"content",content}});
  }
  messages.emplace_back(boost::json::object{{"role","user"},{"content",userPrompt}});
  boost::json::object body{{"model",options_.model},{"messages",std::move(messages)},{"stream",false},{"temperature",0.2},{"max_tokens",options_.maxTokens}};

  const std::string target = (parsed.basePath.empty() ? std::string() : parsed.basePath) + "/chat/completions";
  http::request<http::string_body> req{http::verb::post, target, 11};
  req.set(http::field::host, parsed.host);
  req.set(http::field::user_agent, "smart-factory-agent/1.0");
  req.set(http::field::content_type, "application/json");
  req.set(http::field::authorization, "Bearer " + key);
  req.body() = boost::json::serialize(body);
  req.prepare_payload();
  http::write(stream, req);

  beast::flat_buffer buffer;
  http::response<http::string_body> res;
  http::read(stream, buffer, res);
  beast::error_code ec;
  stream.shutdown(ec);
  if (res.result_int() < 200 || res.result_int() >= 300)
    throw std::runtime_error("agent provider returned HTTP " + std::to_string(res.result_int()));

  const auto parsedBody = boost::json::parse(res.body());
  if (!parsedBody.is_object()) throw std::runtime_error("agent provider returned invalid JSON");
  const auto& root = parsedBody.as_object();
  const auto* choices = root.if_contains("choices");
  if (!choices || !choices->is_array() || choices->as_array().empty() || !choices->as_array().front().is_object())
    throw std::runtime_error("agent provider response has no choices");
  const auto& choice = choices->as_array().front().as_object();
  const auto* message = choice.if_contains("message");
  if (!message || !message->is_object()) throw std::runtime_error("agent provider response has no message");
  const auto* content = message->as_object().if_contains("content");
  if (!content || !content->is_string()) throw std::runtime_error("agent provider response has no text content");
  return std::string(content->as_string());
}

}  // namespace smart_factory
