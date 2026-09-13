#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace smart_factory {

struct BackendConfig {
  std::string mode{"development"};
  std::string listenHost{"0.0.0.0"};
  std::uint16_t listenPort{8000};
  std::string webRoot{"apps/web/dist"};
  std::string deviceAdapter{"simulated"};
  std::string deviceRegistryFile{"backend-cpp/config/devices.development.json"};
  std::string deviceAdapterLibrary;
  std::string deviceAdapterOptions{"{}"};
  std::string osAdapter{"posix"};
  std::string osAdapterLibrary;
  std::string osAdapterOptions{"{}"};
  bool allowSimulatedInFactory{false};
  bool enableRealtime{true};
  bool requireRealtime{false};
  int realtimePriority{80};
  int emergencyRealtimePriority{90};
  int realtimeCpu{-1};
  bool realtimeLockMemory{false};
  bool enableEmergencyControl{false};
  int workerThreads{4};
  std::size_t nonRealtimeQueueCapacity{1024};
  std::size_t feedbackQueueCapacity{1024};
  std::size_t realtimeQueueCapacity{256};
  std::size_t emergencyQueueCapacity{32};
  int heartbeatMs{5000};
  // 0 = derive from the minimum Device Registry samplePeriodMs.
  int acquisitionPollMs{0};
  int telemetryPublishMs{1000};
  std::size_t maxHttpConnections{128};
  int httpRequestTimeoutMs{10000};
  std::size_t maxWsSessions{32};
  std::size_t maxRequestBodyBytes{1024 * 1024};
  std::size_t wsOutboundQueueCapacity{256};
  int acquisitionStaleAfterMs{3000};
  int acquisitionOfflineAfterMs{10000};
  int acquisitionFailureAlarmThreshold{3};
  int commandExecutionTimeoutMs{2000};
  int commandFeedbackTimeoutMs{3000};
  int commandFeedbackPollMs{100};
  bool commandLedgerEnabled{false};
  std::string commandLedgerDirectory{"data/commands"};
  std::size_t commandLedgerMaxRecords{100000};
  std::size_t commandDashboardLimit{200};
  bool historianEnabled{true};
  std::string historianDirectory{"data/historian"};
  // Deprecated compatibility field retained so alpha2 config files continue
  // to parse. beta1 historian retention is time based, not row-count based.
  std::size_t historianMaxRecords{200000};
  int historianRetentionDays{30};
  bool historianBackupEnabled{true};
  std::string historianBackupDirectory{"data/backup/historian"};
  int historianBackupIntervalHours{24};
  std::uint64_t historianMinFreeSpaceMb{512};
  bool platformConfigEnabled{true};
  std::string platformConfigDirectory{"data/config"};
  bool authEnabled{false};
  std::string authUsersFile{"backend-cpp/config/auth.local.json"};
  bool auditEnabled{true};
  std::string auditDirectory{"data/audit"};
  std::size_t auditMaxRecords{100000};
  bool agentEnabled{true};
  std::string agentProvider{"deepseek"};
  std::string agentBaseUrl{"https://api.deepseek.com"};
  std::string agentModel{"deepseek-chat"};
  std::string agentApiKeyEnv{"DEEPSEEK_API_KEY"};
  int agentTimeoutMs{30000};
  int agentMaxTokens{1400};
  std::string allowedOrigin{"*"};
  std::string siteName{"国产操作系统智慧工厂"};
};

BackendConfig loadBackendConfig(const std::string& path);
void validateBackendConfig(const BackendConfig& config);

}  // namespace smart_factory
