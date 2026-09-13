#pragma once

#include <string>
#include <utility>
#include <vector>

namespace smart_factory {

struct AgentClientOptions {
  bool enabled{true};
  std::string provider{"deepseek"};
  std::string baseUrl{"https://api.deepseek.com"};
  std::string model{"deepseek-chat"};
  std::string apiKeyEnv{"DEEPSEEK_API_KEY"};
  int timeoutMs{30000};
  int maxTokens{1400};
};

class AgentClient {
 public:
  explicit AgentClient(AgentClientOptions options);
  bool configured() const;
  std::string provider() const { return options_.provider; }
  std::string model() const { return options_.model; }
  std::string baseUrl() const { return options_.baseUrl; }
  std::string apiKeyEnv() const { return options_.apiKeyEnv; }
  std::string complete(const std::string& systemPrompt, const std::vector<std::pair<std::string,std::string>>& history, const std::string& userPrompt) const;

 private:
  AgentClientOptions options_;
};

}  // namespace smart_factory
