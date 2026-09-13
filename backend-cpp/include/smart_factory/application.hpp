#pragma once

#include "smart_factory/config.hpp"
#include "smart_factory/command_ledger.hpp"
#include "smart_factory/device_adapter.hpp"
#include "smart_factory/device_registry.hpp"
#include "smart_factory/executor.hpp"
#include "smart_factory/historian.hpp"
#include "smart_factory/security.hpp"
#include "smart_factory/platform/config_service.hpp"
#include "smart_factory/platform/acquisition_service.hpp"
#include "smart_factory/platform/quality_service.hpp"
#include "smart_factory/platform/rule_service.hpp"
#include "smart_factory/platform/alarm_service.hpp"
#include "smart_factory/platform/incident_service.hpp"
#include "smart_factory/platform/command_service.hpp"
#include "smart_factory/platform/query_service.hpp"
#include "smart_factory/platform/replay_service.hpp"
#include "smart_factory/platform/report_service.hpp"
#include "smart_factory/agent_client.hpp"

#include <boost/json.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

namespace smart_factory {

using Json = boost::json::value;
using EventSink = std::function<void(const std::string&)>;

struct RuntimeStatus {
  bool realtimeEnabled{false};
  bool realtimeGranted{false};
  std::string realtimeDetail;
  std::string osAdapter;
  std::string deviceAdapter;
  bool emergencyControlEnabled{false};
  bool emergencyRealtimeGranted{false};
  std::string emergencyRealtimeDetail;
  bool deviceAdapterSimulated{false};
  bool deviceAdapterReady{false};
  std::string deviceAdapterState;
  std::string deviceAdapterDetail;
  std::string deviceRegistryFile;
  std::size_t configuredDeviceCount{0};
  bool commandLedgerEnabled{false};
  std::string commandLedgerDirectory;
  std::size_t commandLedgerRecords{0};
  bool historianEnabled{false};
  std::string historianDirectory;
  std::size_t historianTelemetryRecords{0};
  std::size_t historianAlarmEvents{0};
  std::size_t historianAlarmOccurrences{0};
  bool historianStorageHealthy{true};
  std::uint64_t historianDatabaseBytes{0};
  std::uint64_t historianFreeBytes{0};
  std::int64_t historianLastBackupAt{0};
  std::string historianLastBackupPath;
  std::string acquisitionState{"INIT"};
  std::string acquisitionDetail;
  std::int64_t acquisitionLastSuccessTs{0};
  std::int64_t acquisitionLastFailureTs{0};
  std::uint64_t acquisitionConsecutiveFailures{0};
  std::uint64_t acquisitionTotalFailures{0};
  std::int64_t acquisitionPollMs{1000};
  std::int64_t hmiPublishMs{1000};
  bool authEnabled{false};
  bool auditEnabled{false};
  std::string auditDirectory;
  std::size_t auditRecords{0};
};

class ApplicationService {
 public:
  ApplicationService(BackendConfig config,
                     std::unique_ptr<DeviceAdapter> deviceAdapter,
                     EventSink eventSink = {},
                     std::unique_ptr<OsAdapter> osAdapter = nullptr,
                     std::shared_ptr<const DeviceRegistry> deviceRegistry = nullptr);
  ~ApplicationService();

  boost::json::object dashboardSnapshot() const;
  boost::json::array devices() const;
  boost::json::array alarms() const;
  boost::json::array communicationHealth() const;
  boost::json::object runtimeStatusJson() const;
  RuntimeStatus runtimeStatus() const;

  boost::json::object telemetryHistory(const TelemetryHistoryQuery& query) const;
  boost::json::object latestTelemetry(const std::string& deviceId = {}, const std::string& pointId = {}) const;
  boost::json::object alarmHistory(const AlarmHistoryQuery& query) const;
  boost::json::object auditHistory(const AuditQuery& query, const SecurityContext& actor) const;

  bool authEnabled() const;
  std::optional<SecurityPrincipal> authenticate(const std::string& userId, const std::string& token) const;
  std::optional<SecurityPrincipal> authenticateToken(const std::string& token) const;
  bool authorize(const SecurityPrincipal& principal, Permission permission) const;
  boost::json::object principalJson(const SecurityPrincipal& principal) const;

  struct MutationResult {
    int httpStatus{200};
    boost::json::object body;
  };

  using CommandIssueResult = MutationResult;
  CommandIssueResult issueCommand(const boost::json::object& request, const SecurityContext& actor);
  MutationResult previewCommandV2(const boost::json::object& request, const SecurityContext& actor);
  MutationResult issueCommandV2(const boost::json::object& request, const SecurityContext& actor);
  boost::json::object commandV2(const std::string& commandId, const SecurityContext& actor) const;
  MutationResult reconcileCommandV2(const std::string& commandId, const boost::json::object& request, const SecurityContext& actor);
  MutationResult acknowledgeAlarm(const std::string& alarmId, const boost::json::object& request, const SecurityContext& actor);
  MutationResult clearAlarm(const std::string& alarmId, const boost::json::object& request, const SecurityContext& actor);

  boost::json::object platformConfigStatus() const;
  boost::json::object platformConfigSnapshot() const;
  boost::json::array platformAssets() const;
  boost::json::array applicationInstances() const;
  boost::json::array platformRules() const;
  boost::json::array alarmOccurrencesV2(const std::string& deviceId = {}, const std::string& sourceDomain = {}) const;
  MutationResult acknowledgeAlarmOccurrenceV2(const std::string& occurrenceId, const boost::json::object& request, const SecurityContext& actor);
  boost::json::array incidentsV2() const;
  MutationResult createIncidentV2(const boost::json::object& request, const SecurityContext& actor);
  MutationResult transitionIncidentV2(const std::string& incidentId, const boost::json::object& request, const SecurityContext& actor);
  boost::json::object timelineV2(std::int64_t fromTs, std::int64_t toTs, const std::string& instanceId = {}) const;
  MutationResult createReplaySessionV2(const boost::json::object& request, const SecurityContext& actor);
  boost::json::object replaySessionV2(const std::string& sessionId, const SecurityContext& actor) const;
  MutationResult createReportJobV2(const boost::json::object& request, const SecurityContext& actor);
  boost::json::object reportJobV2(const std::string& jobId, const SecurityContext& actor) const;
  boost::json::object agentStatusV2(const SecurityContext& actor) const;
  MutationResult agentChatV2(const boost::json::object& request, const SecurityContext& actor);
  MutationResult createConfigDraft(const boost::json::object& request, const SecurityContext& actor);
  MutationResult validateConfigDraft(const std::string& draftId, const SecurityContext& actor);
  MutationResult publishConfigDraft(const std::string& draftId, const boost::json::object& request, const SecurityContext& actor);
  MutationResult rollbackConfig(const std::string& revision, const boost::json::object& request, const SecurityContext& actor);
  void recordAuthAudit(const std::string& action, const std::string& outcome, const std::string& actorId,
                       const std::string& detail, const std::string& remoteAddress,
                       const std::optional<SecurityPrincipal>& principal = std::nullopt);

 private:
  struct StoredCommand {
    boost::json::object command;
    std::string fingerprint;
  };

  struct TelemetryCursor {
    std::int64_t sampleTs{0};
    std::int64_t seq{-1};
    std::string sourceEpoch;
  };

  static std::int64_t nowMs();
  static std::string jsonStringOr(const boost::json::object& obj, const char* key, const std::string& fallback = {});
  static std::optional<std::int64_t> jsonInt(const boost::json::object& obj, const char* key);
  static std::string fingerprintCommand(const boost::json::object& request);
  static std::string commandDedupHash(const boost::json::object& request, const SecurityContext& actor);
  static bool validateRequestedValue(const boost::json::object& request, const CommandDefinition& definition, std::string& error);
  static std::optional<Permission> commandPermission(const CommandDefinition& definition);
  static bool alarmEquivalent(const boost::json::object& a, const boost::json::object& b);
  static bool terminalCommandState(const std::string& state);
  static std::string makeOccurrenceId(const std::string& alarmId, std::int64_t raisedAt);

  boost::json::object initialCommand(const boost::json::object& request, const std::string& commandId, std::int64_t now) const;
  CommandIssueResult issueCommandInternal(const boost::json::object& request, const SecurityContext& actor,
                                          const std::string& idempotencyKey = {},
                                          const std::string& requestHash = {},
                                          std::int64_t dedupExpiresAt = 0);
  std::optional<std::string> currentDeviceStatus(const std::string& deviceId) const;
  void appendCommandEvidence(const std::string& commandId, const std::string& evidenceType,
                             const std::string& detail, const boost::json::object& extra = {});
  void runCommandLifecycle(const std::string& commandId, CommandExecutionClass executionClass);
  void restoreCommandLedger();
  void persistCommand(const boost::json::object& command);
  void pruneCommandCacheLocked();
  void monitorCommandFeedback(const std::string& commandId, DeviceCommand deviceCommand, std::int64_t expiresAt);
  void transition(const std::string& commandId, const std::string& state, int seq, const std::string& detail);
  void emitCommandEvent(const boost::json::object& command, int seq, const std::string& detail);
  void emitRealtimeEvent(const std::string& type, const boost::json::value& data, std::optional<std::int64_t> seq = std::nullopt);
  void appendAudit(const SecurityContext& actor, const std::string& action, const std::string& resourceType,
                   const std::string& resourceId, const std::string& outcome, const std::string& detail,
                   const std::string& reason = {});

  void validateCompleteSnapshotOrThrow(const DeviceDataSnapshot& snapshot) const;
  DeviceDataSnapshot normalizeSnapshot(DeviceDataSnapshot snapshot, bool persistAndEmit, bool completeSnapshot = true);
  DeviceAcquisitionBatch readAcquisitionBatch();
  DeviceDataSnapshot normalizeAcquisitionBatch(DeviceAcquisitionBatch batch, bool persistAndEmit);
  std::string runningConfigRevision() const;
  boost::json::array reconcileAlarms(const boost::json::array& rawAlarms, std::int64_t snapshotTs, bool persistAndEmit, const std::set<std::string>& completeDeviceIds);
  void recordAlarmEvent(const std::string& eventType, const boost::json::object& alarm,
                        const std::string& source, const std::string& operatorId = {}, const std::string& comment = {});
  void persistTelemetry(const boost::json::array& telemetry);
  void enrichSnapshotCapabilities(DeviceDataSnapshot& snapshot) const;
  std::int64_t acquisitionIntervalMs() const;
  void recordAcquisitionSuccess(std::int64_t snapshotTs);
  void recordAcquisitionFailure(const std::string& detail);
  void updateAcquisitionAlarm(bool unhealthy, const std::string& detail, const std::string& severity);
  void updateStorageAlarm();
  DeviceDataSnapshot degradedSnapshotForAcquisitionFailure(const std::string& state, const std::string& detail);
  boost::json::array currentAlarmArrayLocked() const;
  void replaceLatestSnapshot(DeviceDataSnapshot snapshot);
  DeviceDataSnapshot latestSnapshot() const;
  void publishSnapshotEvents(const DeviceDataSnapshot& snapshot);
  void runRealtimePublisher();

  BackendConfig config_;
  std::unique_ptr<DeviceAdapter> deviceAdapter_;
  std::shared_ptr<const DeviceRegistry> deviceRegistry_;
  EventSink eventSink_;
  NonRealtimeExecutor nonRealtime_;
  NonRealtimeExecutor feedbackExecutor_;
  std::unique_ptr<RealtimeExecutor> realtime_;
  std::unique_ptr<RealtimeExecutor> emergency_;
  std::unique_ptr<CommandLedger> commandLedger_;
  std::unique_ptr<HistorianStore> historian_;
  std::unique_ptr<AuthService> auth_;
  std::unique_ptr<AuditStore> audit_;
  std::unique_ptr<ConfigService> configService_;
  std::unique_ptr<QualityService> qualityService_;
  std::unique_ptr<AcquisitionService> acquisitionService_;
  std::unique_ptr<RuleService> ruleService_;
  std::unique_ptr<AlarmService> alarmService_;
  std::unique_ptr<IncidentService> incidentService_;
  std::unique_ptr<CommandService> commandService_;
  std::unique_ptr<ReplayService> replayService_;
  std::unique_ptr<ReportService> reportService_;
  std::unique_ptr<AgentClient> agentClient_;

  mutable std::mutex commandsMutex_;
  std::unordered_map<std::string, StoredCommand> commands_;

  mutable std::mutex snapshotMutex_;
  DeviceDataSnapshot latestSnapshot_;

  mutable std::mutex alarmMutex_;
  std::unordered_map<std::string, boost::json::object> alarmStates_;

  mutable std::mutex telemetryCursorMutex_;
  std::unordered_map<std::string, TelemetryCursor> telemetryCursors_;

  mutable std::mutex acquisitionMutex_;
  std::string acquisitionState_{"INIT"};
  std::string acquisitionDetail_{"no successful snapshot yet"};
  std::int64_t acquisitionLastSuccessTs_{0};
  std::int64_t acquisitionLastFailureTs_{0};
  std::uint64_t acquisitionConsecutiveFailures_{0};
  std::uint64_t acquisitionTotalFailures_{0};
  std::atomic<std::int64_t> lastHmiPublishTs_{0};

  std::atomic<bool> publisherRunning_{false};
  std::thread publisherThread_;
};

}  // namespace smart_factory
