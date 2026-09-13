#include "smart_factory/config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace smart_factory {
namespace {
std::string trim(std::string s) {
  auto notSpace = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
  s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
  return s;
}

bool parseBool(const std::string& value) {
  const auto v = trim(value);
  if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
  if (v == "false" || v == "0" || v == "no" || v == "off") return false;
  throw std::runtime_error("Invalid boolean: " + value);
}
}  // namespace

BackendConfig loadBackendConfig(const std::string& path) {
  BackendConfig cfg;
  if (path.empty()) return cfg;
  std::ifstream in(path);
  if (!in) throw std::runtime_error("Cannot open backend config: " + path);

  std::string line;
  std::size_t lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    const auto pos = line.find('=');
    if (pos == std::string::npos) throw std::runtime_error("Invalid config line " + std::to_string(lineNo));
    const auto key = trim(line.substr(0, pos));
    const auto value = trim(line.substr(pos + 1));

    if (key == "mode") cfg.mode = value;
    else if (key == "listen_host") cfg.listenHost = value;
    else if (key == "listen_port") { const auto parsed = std::stoll(value); if (parsed < 1 || parsed > 65535) throw std::runtime_error("listen_port must be 1..65535"); cfg.listenPort = static_cast<std::uint16_t>(parsed); }
    else if (key == "web_root") cfg.webRoot = value;
    else if (key == "device_adapter") cfg.deviceAdapter = value;
    else if (key == "device_registry_file") cfg.deviceRegistryFile = value;
    else if (key == "device_adapter_library") cfg.deviceAdapterLibrary = value;
    else if (key == "device_adapter_options") cfg.deviceAdapterOptions = value;
    else if (key == "os_adapter") cfg.osAdapter = value;
    else if (key == "os_adapter_library") cfg.osAdapterLibrary = value;
    else if (key == "os_adapter_options") cfg.osAdapterOptions = value;
    else if (key == "allow_simulated_in_factory") cfg.allowSimulatedInFactory = parseBool(value);
    else if (key == "enable_realtime") cfg.enableRealtime = parseBool(value);
    else if (key == "require_realtime") cfg.requireRealtime = parseBool(value);
    else if (key == "realtime_priority") cfg.realtimePriority = std::stoi(value);
    else if (key == "emergency_realtime_priority") cfg.emergencyRealtimePriority = std::stoi(value);
    else if (key == "realtime_cpu") cfg.realtimeCpu = std::stoi(value);
    else if (key == "realtime_lock_memory") cfg.realtimeLockMemory = parseBool(value);
    else if (key == "enable_emergency_control") cfg.enableEmergencyControl = parseBool(value);
    else if (key == "worker_threads") cfg.workerThreads = std::stoi(value);
    else if (key == "non_realtime_queue_capacity") cfg.nonRealtimeQueueCapacity = static_cast<std::size_t>(std::stoull(value));
    else if (key == "feedback_queue_capacity") cfg.feedbackQueueCapacity = static_cast<std::size_t>(std::stoull(value));
    else if (key == "realtime_queue_capacity") cfg.realtimeQueueCapacity = static_cast<std::size_t>(std::stoull(value));
    else if (key == "emergency_queue_capacity") cfg.emergencyQueueCapacity = static_cast<std::size_t>(std::stoull(value));
    else if (key == "heartbeat_ms") cfg.heartbeatMs = std::stoi(value);
    else if (key == "acquisition_poll_ms") cfg.acquisitionPollMs = std::stoi(value);
    else if (key == "telemetry_publish_ms") cfg.telemetryPublishMs = std::stoi(value);
    else if (key == "max_http_connections") cfg.maxHttpConnections = static_cast<std::size_t>(std::stoull(value));
    else if (key == "http_request_timeout_ms") cfg.httpRequestTimeoutMs = std::stoi(value);
    else if (key == "max_ws_sessions") cfg.maxWsSessions = static_cast<std::size_t>(std::stoull(value));
    else if (key == "max_request_body_bytes") cfg.maxRequestBodyBytes = static_cast<std::size_t>(std::stoull(value));
    else if (key == "ws_outbound_queue_capacity") cfg.wsOutboundQueueCapacity = static_cast<std::size_t>(std::stoull(value));
    else if (key == "acquisition_stale_after_ms") cfg.acquisitionStaleAfterMs = std::stoi(value);
    else if (key == "acquisition_offline_after_ms") cfg.acquisitionOfflineAfterMs = std::stoi(value);
    else if (key == "acquisition_failure_alarm_threshold") cfg.acquisitionFailureAlarmThreshold = std::stoi(value);
    else if (key == "command_execution_timeout_ms") cfg.commandExecutionTimeoutMs = std::stoi(value);
    else if (key == "command_feedback_timeout_ms") cfg.commandFeedbackTimeoutMs = std::stoi(value);
    else if (key == "command_feedback_poll_ms") cfg.commandFeedbackPollMs = std::stoi(value);
    else if (key == "command_ledger_enabled") cfg.commandLedgerEnabled = parseBool(value);
    else if (key == "command_ledger_directory") cfg.commandLedgerDirectory = value;
    else if (key == "command_ledger_max_records") cfg.commandLedgerMaxRecords = static_cast<std::size_t>(std::stoull(value));
    else if (key == "command_dashboard_limit") cfg.commandDashboardLimit = static_cast<std::size_t>(std::stoull(value));
    else if (key == "historian_enabled") cfg.historianEnabled = parseBool(value);
    else if (key == "historian_directory") cfg.historianDirectory = value;
    else if (key == "historian_max_records") cfg.historianMaxRecords = static_cast<std::size_t>(std::stoull(value));
    else if (key == "historian_retention_days") cfg.historianRetentionDays = std::stoi(value);
    else if (key == "historian_backup_enabled") cfg.historianBackupEnabled = parseBool(value);
    else if (key == "historian_backup_directory") cfg.historianBackupDirectory = value;
    else if (key == "historian_backup_interval_hours") cfg.historianBackupIntervalHours = std::stoi(value);
    else if (key == "historian_min_free_space_mb") cfg.historianMinFreeSpaceMb = static_cast<std::uint64_t>(std::stoull(value));
    else if (key == "platform_config_enabled") cfg.platformConfigEnabled = parseBool(value);
    else if (key == "platform_config_directory") cfg.platformConfigDirectory = value;
    else if (key == "auth_enabled") cfg.authEnabled = parseBool(value);
    else if (key == "auth_users_file") cfg.authUsersFile = value;
    else if (key == "audit_enabled") cfg.auditEnabled = parseBool(value);
    else if (key == "audit_directory") cfg.auditDirectory = value;
    else if (key == "audit_max_records") cfg.auditMaxRecords = static_cast<std::size_t>(std::stoull(value));
    else if (key == "agent_enabled") cfg.agentEnabled = parseBool(value);
    else if (key == "agent_provider") cfg.agentProvider = value;
    else if (key == "agent_base_url") cfg.agentBaseUrl = value;
    else if (key == "agent_model") cfg.agentModel = value;
    else if (key == "agent_api_key_env") cfg.agentApiKeyEnv = value;
    else if (key == "agent_timeout_ms") cfg.agentTimeoutMs = std::stoi(value);
    else if (key == "agent_max_tokens") cfg.agentMaxTokens = std::stoi(value);
    else if (key == "allowed_origin") cfg.allowedOrigin = value;
    else if (key == "site_name") cfg.siteName = value;
    else throw std::runtime_error("Unknown backend config key: " + key);
  }
  validateBackendConfig(cfg);
  return cfg;
}

void validateBackendConfig(const BackendConfig& cfg) {
  if (cfg.listenPort == 0) throw std::runtime_error("listen_port must be > 0");
  if (cfg.deviceAdapter.empty()) throw std::runtime_error("device_adapter is required");
  if (cfg.deviceRegistryFile.empty()) throw std::runtime_error("device_registry_file is required");
  if (cfg.osAdapter.empty()) throw std::runtime_error("os_adapter is required");
  if (cfg.deviceAdapter == "plugin" && cfg.deviceAdapterLibrary.empty()) throw std::runtime_error("device_adapter_library is required when device_adapter=plugin");
  if (cfg.osAdapter == "plugin" && cfg.osAdapterLibrary.empty()) throw std::runtime_error("os_adapter_library is required when os_adapter=plugin");
  if (cfg.workerThreads < 1 || cfg.workerThreads > 128) throw std::runtime_error("worker_threads out of range");
  if (cfg.nonRealtimeQueueCapacity < 1 || cfg.nonRealtimeQueueCapacity > 1000000) throw std::runtime_error("non_realtime_queue_capacity out of range");
  if (cfg.feedbackQueueCapacity < 1 || cfg.feedbackQueueCapacity > 1000000) throw std::runtime_error("feedback_queue_capacity out of range");
  if (cfg.realtimeQueueCapacity < 1 || cfg.realtimeQueueCapacity > 1000000) throw std::runtime_error("realtime_queue_capacity out of range");
  if (cfg.emergencyQueueCapacity < 1 || cfg.emergencyQueueCapacity > 100000) throw std::runtime_error("emergency_queue_capacity out of range");
  if (cfg.realtimePriority < 1 || cfg.realtimePriority > 99) throw std::runtime_error("realtime_priority must be 1..99");
  if (cfg.emergencyRealtimePriority < 1 || cfg.emergencyRealtimePriority > 99) throw std::runtime_error("emergency_realtime_priority must be 1..99");
  if (cfg.enableEmergencyControl && !cfg.enableRealtime) throw std::runtime_error("enable_emergency_control requires enable_realtime=true");
  if (cfg.enableEmergencyControl && cfg.emergencyRealtimePriority < cfg.realtimePriority) throw std::runtime_error("emergency_realtime_priority must be >= realtime_priority");
  if (cfg.heartbeatMs < 500) throw std::runtime_error("heartbeat_ms must be >= 500");
  if (cfg.acquisitionPollMs < 0 || cfg.acquisitionPollMs > 60000) throw std::runtime_error("acquisition_poll_ms must be 0(auto)..60000");
  if (cfg.telemetryPublishMs < 100 || cfg.telemetryPublishMs > 60000) throw std::runtime_error("telemetry_publish_ms must be 100..60000");
  if (cfg.maxHttpConnections < 1 || cfg.maxHttpConnections > 10000) throw std::runtime_error("max_http_connections out of range");
  if (cfg.httpRequestTimeoutMs < 100 || cfg.httpRequestTimeoutMs > 120000) throw std::runtime_error("http_request_timeout_ms out of range");
  if (cfg.maxWsSessions < 1 || cfg.maxWsSessions > cfg.maxHttpConnections) throw std::runtime_error("max_ws_sessions must be 1..max_http_connections");
  if (cfg.maxRequestBodyBytes < 1024 || cfg.maxRequestBodyBytes > 64 * 1024 * 1024) throw std::runtime_error("max_request_body_bytes out of range");
  if (cfg.wsOutboundQueueCapacity < 8 || cfg.wsOutboundQueueCapacity > 100000) throw std::runtime_error("ws_outbound_queue_capacity out of range");
  if (cfg.acquisitionStaleAfterMs < cfg.telemetryPublishMs || cfg.acquisitionStaleAfterMs > 600000) throw std::runtime_error("acquisition_stale_after_ms must be >= telemetry_publish_ms and <= 600000");
  if (cfg.acquisitionOfflineAfterMs < cfg.acquisitionStaleAfterMs || cfg.acquisitionOfflineAfterMs > 3600000) throw std::runtime_error("acquisition_offline_after_ms must be >= acquisition_stale_after_ms and <= 3600000");
  if (cfg.acquisitionFailureAlarmThreshold < 1 || cfg.acquisitionFailureAlarmThreshold > 1000) throw std::runtime_error("acquisition_failure_alarm_threshold must be 1..1000");
  if (cfg.commandExecutionTimeoutMs < 50 || cfg.commandExecutionTimeoutMs > 60000) throw std::runtime_error("command_execution_timeout_ms must be 50..60000");
  if (cfg.commandFeedbackTimeoutMs < 100 || cfg.commandFeedbackTimeoutMs > 60000) throw std::runtime_error("command_feedback_timeout_ms must be 100..60000");
  if (cfg.commandFeedbackPollMs < 10 || cfg.commandFeedbackPollMs > cfg.commandFeedbackTimeoutMs) throw std::runtime_error("command_feedback_poll_ms must be 10..command_feedback_timeout_ms");
  if (cfg.commandLedgerEnabled && cfg.commandLedgerDirectory.empty()) throw std::runtime_error("command_ledger_directory is required when command ledger is enabled");
  if (cfg.commandLedgerMaxRecords < 1000 || cfg.commandLedgerMaxRecords > 5000000) throw std::runtime_error("command_ledger_max_records must be 1000..5000000");
  if (cfg.commandDashboardLimit < 1 || cfg.commandDashboardLimit > 5000) throw std::runtime_error("command_dashboard_limit must be 1..5000");
  if (cfg.historianEnabled && cfg.historianDirectory.empty()) throw std::runtime_error("historian_directory is required when historian is enabled");
  if (cfg.historianMaxRecords < 1000 || cfg.historianMaxRecords > 5000000) throw std::runtime_error("historian_max_records compatibility value must be 1000..5000000");
  if (cfg.historianRetentionDays < 1 || cfg.historianRetentionDays > 3650) throw std::runtime_error("historian_retention_days must be 1..3650");
  if (cfg.historianBackupEnabled && cfg.historianBackupDirectory.empty()) throw std::runtime_error("historian_backup_directory is required when backup is enabled");
  if (cfg.historianBackupIntervalHours < 1 || cfg.historianBackupIntervalHours > 168) throw std::runtime_error("historian_backup_interval_hours must be 1..168");
  if (cfg.historianMinFreeSpaceMb < 32 || cfg.historianMinFreeSpaceMb > 1048576) throw std::runtime_error("historian_min_free_space_mb must be 32..1048576");
  if (cfg.platformConfigEnabled && cfg.platformConfigDirectory.empty()) throw std::runtime_error("platform_config_directory is required when platform config is enabled");
  if (cfg.authEnabled && cfg.authUsersFile.empty()) throw std::runtime_error("auth_users_file is required when auth is enabled");
  if (cfg.auditEnabled && cfg.auditDirectory.empty()) throw std::runtime_error("audit_directory is required when audit is enabled");
  if (cfg.auditMaxRecords < 1000 || cfg.auditMaxRecords > 5000000) throw std::runtime_error("audit_max_records must be 1000..5000000");
  if (cfg.agentEnabled && (cfg.agentProvider.empty() || cfg.agentBaseUrl.empty() || cfg.agentModel.empty() || cfg.agentApiKeyEnv.empty())) throw std::runtime_error("agent provider/base_url/model/api_key_env are required when agent is enabled");
  if (cfg.agentTimeoutMs < 1000 || cfg.agentTimeoutMs > 120000) throw std::runtime_error("agent_timeout_ms must be 1000..120000");
  if (cfg.agentMaxTokens < 128 || cfg.agentMaxTokens > 8192) throw std::runtime_error("agent_max_tokens must be 128..8192");
  if (cfg.mode == "factory" && !cfg.authEnabled) throw std::runtime_error("factory mode requires auth_enabled=true");
  if (cfg.mode == "factory" && !cfg.auditEnabled) throw std::runtime_error("factory mode requires audit_enabled=true");
  if (cfg.mode == "factory" && !cfg.commandLedgerEnabled) throw std::runtime_error("factory mode requires command_ledger_enabled=true");
  if (cfg.mode == "factory" && cfg.historianEnabled && !cfg.historianBackupEnabled) throw std::runtime_error("factory mode requires historian_backup_enabled=true when historian is enabled");
  if (cfg.mode == "factory" && (cfg.allowedOrigin.empty() || cfg.allowedOrigin == "*")) throw std::runtime_error("factory mode requires an explicit allowed_origin");
  if (cfg.mode == "factory" && cfg.deviceAdapter == "simulated") {
    throw std::runtime_error("factory mode always refuses the simulated device adapter; build/use a target adapter or plugin");
  }
  if (cfg.mode == "factory" && cfg.allowSimulatedInFactory) {
    throw std::runtime_error("allow_simulated_in_factory is deprecated and cannot be enabled in factory mode");
  }
}

}  // namespace smart_factory
