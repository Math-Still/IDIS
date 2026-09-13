#include "smart_factory/application.hpp"
#include "smart_factory/integration_factory.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <vector>
#include <thread>
#include <sstream>

namespace smart_factory {
namespace {
std::string serializeValue(const boost::json::value& value) { return boost::json::serialize(value); }

struct ScenarioDefinition { const char* type; const char* label; const char* description; };
constexpr ScenarioDefinition kScenarios[] = {
  {"temperature_humidity", "温湿度监控", "环境温湿度状态"},
  {"pir_lighting", "红外感应照明", "感应照明状态"},
  {"hazardous_gas", "危险气体监测", "危险气体安全状态"},
  {"agv_obstacle", "AGV 避障", "移动设备避障状态"},
  {"goods_counting", "货物计数", "计数状态"}
};

void copyIfPresent(const boost::json::object& from, boost::json::object& to, const char* key) {
  if (const auto* v = from.if_contains(key)) to[key] = *v;
}
}

ApplicationService::ApplicationService(BackendConfig config,
                                       std::unique_ptr<DeviceAdapter> deviceAdapter,
                                       EventSink eventSink,
                                       std::unique_ptr<OsAdapter> osAdapter,
                                       std::shared_ptr<const DeviceRegistry> deviceRegistry)
    : config_(std::move(config)),
      deviceAdapter_(std::move(deviceAdapter)),
      deviceRegistry_(std::move(deviceRegistry)),
      eventSink_(std::move(eventSink)),
      nonRealtime_(static_cast<std::size_t>(std::max(1, config_.workerThreads)), config_.nonRealtimeQueueCapacity),
      feedbackExecutor_(static_cast<std::size_t>(std::max(2, config_.workerThreads)), config_.feedbackQueueCapacity) {
  if (!deviceAdapter_) throw std::runtime_error("DeviceAdapter is required");
  if (deviceAdapter_->apiVersion() != kDeviceAdapterApiVersion) {
    throw std::runtime_error("DeviceAdapter API version mismatch; host requires v" + std::to_string(kDeviceAdapterApiVersion));
  }
  std::string adapterStartDetail;
  if (!deviceAdapter_->start(adapterStartDetail)) {
    throw std::runtime_error("DeviceAdapter start failed: " + adapterStartDetail);
  }
  if (config_.mode == "factory" && deviceAdapter_->simulated()) {
    deviceAdapter_->stop();
    throw std::runtime_error("factory mode refuses a DeviceAdapter that reports simulated=true");
  }

  auth_ = std::make_unique<AuthService>(config_.authEnabled, config_.authUsersFile);
  if (config_.auditEnabled) audit_ = std::make_unique<AuditStore>(config_.auditDirectory, config_.auditMaxRecords);
  if (config_.platformConfigEnabled && deviceRegistry_) configService_ = std::make_unique<ConfigService>(config_.platformConfigDirectory, config_.siteName, deviceRegistry_);
  qualityService_ = std::make_unique<QualityService>(deviceRegistry_);
  acquisitionService_ = std::make_unique<AcquisitionService>(deviceRegistry_, config_.siteName, deviceAdapter_->name(), deviceAdapter_->simulated());
  ruleService_ = std::make_unique<RuleService>();
  const auto platformStateDirectory = config_.historianDirectory.empty() ? config_.platformConfigDirectory : config_.historianDirectory;
  alarmService_ = std::make_unique<AlarmService>(platformStateDirectory);
  incidentService_ = std::make_unique<IncidentService>(platformStateDirectory);
  commandService_ = std::make_unique<CommandService>(deviceAdapter_.get());
  if (configService_) ruleService_->configure(configService_->activeSnapshot(), runningConfigRevision());

  if (config_.commandLedgerEnabled) {
    commandLedger_ = std::make_unique<CommandLedger>(config_.commandLedgerDirectory, config_.commandLedgerMaxRecords);
    restoreCommandLedger();
  }

  if (config_.historianEnabled) {
    HistorianOptions historianOptions;
    historianOptions.retentionDays = config_.historianRetentionDays;
    historianOptions.backupEnabled = config_.historianBackupEnabled;
    historianOptions.backupDirectory = config_.historianBackupDirectory;
    historianOptions.backupIntervalHours = config_.historianBackupIntervalHours;
    historianOptions.minFreeSpaceMb = config_.historianMinFreeSpaceMb;
    historian_ = std::make_unique<HistorianStore>(config_.historianDirectory, std::move(historianOptions));
    {
      auto recovered = historian_->replayAlarmStates();
      std::lock_guard lock(alarmMutex_);
      alarmStates_ = std::move(recovered);
    }
    {
      auto recoveredCursors = historian_->latestTelemetryCursors();
      std::lock_guard lock(telemetryCursorMutex_);
      for (const auto& [key, cursor] : recoveredCursors) telemetryCursors_[key] = {cursor.sampleTs, cursor.seq, cursor.sourceEpoch};
    }
  }

  reportService_ = std::make_unique<ReportService>(platformStateDirectory);
  agentClient_ = std::make_unique<AgentClient>(AgentClientOptions{config_.agentEnabled, config_.agentProvider, config_.agentBaseUrl, config_.agentModel, config_.agentApiKeyEnv, config_.agentTimeoutMs, config_.agentMaxTokens});
  if (historian_ && configService_) replayService_ = std::make_unique<ReplayService>(platformStateDirectory, historian_.get(), configService_.get());

  if (config_.enableRealtime) {
    RealtimeThreadOptions options;
    options.priority = config_.realtimePriority;
    options.cpuAffinity = config_.realtimeCpu;
    options.lockMemory = config_.realtimeLockMemory;
    options.threadName = "sf-realtime";
    if (!osAdapter) osAdapter = makeConfiguredOsAdapter(config_);
    if (osAdapter->apiVersion() != kOsAdapterApiVersion) {
      throw std::runtime_error("OsAdapter API version mismatch; host requires v" + std::to_string(kOsAdapterApiVersion));
    }
    realtime_ = std::make_unique<RealtimeExecutor>(std::move(osAdapter), options, config_.realtimeQueueCapacity);
    for (int i = 0; i < 50 && realtime_->realtimeDetail().empty(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (config_.requireRealtime && !realtime_->realtimeGranted()) {
      throw std::runtime_error("Realtime scheduling is required but was not granted: " + realtime_->realtimeDetail());
    }

    if (config_.enableEmergencyControl) {
      auto emergencyOs = makeConfiguredOsAdapter(config_);
      if (!emergencyOs || emergencyOs->apiVersion() != kOsAdapterApiVersion) {
        throw std::runtime_error("Emergency OsAdapter API version mismatch");
      }
      RealtimeThreadOptions emergencyOptions = options;
      emergencyOptions.priority = config_.emergencyRealtimePriority;
      emergencyOptions.threadName = "sf-emergency";
      emergency_ = std::make_unique<RealtimeExecutor>(std::move(emergencyOs), emergencyOptions, config_.emergencyQueueCapacity);
      for (int i = 0; i < 50 && emergency_->realtimeDetail().empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      if (config_.requireRealtime && !emergency_->realtimeGranted()) {
        throw std::runtime_error("Emergency realtime scheduling is required but was not granted: " + emergency_->realtimeDetail());
      }
    }
  }

  try {
    auto initialBatch = readAcquisitionBatch();
    const auto initialTs = initialBatch.snapshot.generatedAt > 0 ? initialBatch.snapshot.generatedAt : nowMs();
    if (initialBatch.failedDeviceIds.empty()) recordAcquisitionSuccess(initialTs);
    else {
      std::lock_guard lock(acquisitionMutex_);
      acquisitionState_ = "DEGRADED";
      acquisitionDetail_ = "partial device acquisition: " + std::to_string(initialBatch.failedDeviceIds.size()) + " device(s) failed";
      acquisitionLastSuccessTs_ = initialTs;
    }
    replaceLatestSnapshot(normalizeAcquisitionBatch(std::move(initialBatch), true));
    updateStorageAlarm();
  } catch (const std::exception& e) {
    DeviceDataSnapshot empty; empty.generatedAt = nowMs(); replaceLatestSnapshot(std::move(empty));
    recordAcquisitionFailure(std::string("initial snapshot failed: ") + e.what());
  } catch (...) {
    DeviceDataSnapshot empty; empty.generatedAt = nowMs(); replaceLatestSnapshot(std::move(empty));
    recordAcquisitionFailure("initial snapshot failed: unknown exception");
  }

  if (eventSink_) {
    publisherRunning_.store(true);
    publisherThread_ = std::thread([this] { runRealtimePublisher(); });
  }
}

ApplicationService::~ApplicationService() {
  publisherRunning_.store(false);
  if (publisherThread_.joinable()) publisherThread_.join();

  // Command tasks capture this ApplicationService and mutate command/audit state.
  // Drain producer executors while all state and the DeviceAdapter are still alive,
  // then drain feedback observers, and only then release the field adapter.
  if (emergency_) emergency_->shutdown();
  if (realtime_) realtime_->shutdown();
  nonRealtime_.shutdown();
  feedbackExecutor_.shutdown();
  if (deviceAdapter_) deviceAdapter_->stop();
}

std::int64_t ApplicationService::nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

DeviceDataSnapshot ApplicationService::latestSnapshot() const {
  std::lock_guard lock(snapshotMutex_);
  return latestSnapshot_;
}

void ApplicationService::replaceLatestSnapshot(DeviceDataSnapshot snapshot) {
  std::lock_guard lock(snapshotMutex_);
  latestSnapshot_ = std::move(snapshot);
}

boost::json::array ApplicationService::devices() const { return latestSnapshot().devices; }
boost::json::array ApplicationService::alarms() const { return latestSnapshot().alarms; }
boost::json::array ApplicationService::communicationHealth() const { return latestSnapshot().communication; }

boost::json::object ApplicationService::latestTelemetry(const std::string& deviceId, const std::string& pointId) const {
  const auto snapshot = latestSnapshot();
  boost::json::array items;
  for (const auto& value : snapshot.telemetry) {
    if (!value.is_object()) continue;
    const auto& point = value.as_object();
    if (!deviceId.empty() && jsonStringOr(point, "deviceId") != deviceId) continue;
    if (!pointId.empty() && jsonStringOr(point, "pointId") != pointId) continue;
    items.emplace_back(point);
  }
  return {{"items",std::move(items)},{"generatedAt",snapshot.generatedAt},{"contextId","live:"+config_.siteName},{"configRevision",runningConfigRevision()}};
}

boost::json::object ApplicationService::dashboardSnapshot() const {
  auto snapshot = latestSnapshot();
  boost::json::array scenarios;
  for (const auto& def : kScenarios) {
    int deviceCount = 0;
    int onlineCount = 0;
    int activeAlarmCount = 0;
    for (const auto& value : snapshot.devices) {
      const auto& device = value.as_object();
      if (jsonStringOr(device, "scenario") != def.type) continue;
      ++deviceCount;
      if (jsonStringOr(device, "status") != "OFFLINE") ++onlineCount;
      const auto id = jsonStringOr(device, "id");
      for (const auto& alarmValue : snapshot.alarms) {
        const auto& alarm = alarmValue.as_object();
        if (jsonStringOr(alarm, "deviceId") == id && jsonStringOr(alarm, "state") != "CLEARED") ++activeAlarmCount;
      }
    }
    scenarios.emplace_back(boost::json::object{{"type", def.type}, {"label", def.label}, {"description", def.description},
      {"deviceCount", deviceCount}, {"onlineCount", onlineCount}, {"activeAlarmCount", activeAlarmCount}});
  }

  boost::json::array commandArray;
  {
    std::vector<boost::json::object> orderedCommands;
    std::lock_guard lock(commandsMutex_);
    orderedCommands.reserve(commands_.size());
    for (const auto& [_, stored] : commands_) orderedCommands.push_back(stored.command);
    std::sort(orderedCommands.begin(), orderedCommands.end(), [](const auto& a, const auto& b) {
      return ApplicationService::jsonInt(a, "updatedAt").value_or(0) > ApplicationService::jsonInt(b, "updatedAt").value_or(0);
    });
    if (orderedCommands.size() > config_.commandDashboardLimit) orderedCommands.resize(config_.commandDashboardLimit);
    for (auto& command : orderedCommands) commandArray.emplace_back(std::move(command));
  }

  const auto originKind = deviceAdapter_ ? (deviceAdapter_->simulated() ? "SIMULATION" : "REAL_DEVICE") : "UNKNOWN";
  const auto sourceId = deviceAdapter_ ? deviceAdapter_->name() : "unverified";
  return {{"siteName", config_.siteName}, {"generatedAt", snapshot.generatedAt},
          {"provenance", boost::json::object{{"originKind", originKind}, {"sourceId", sourceId}, {"deliveryMode", "LIVE"}}},
          {"devices", std::move(snapshot.devices)}, {"telemetry", std::move(snapshot.telemetry)},
          {"alarms", std::move(snapshot.alarms)}, {"commands", std::move(commandArray)},
          {"communication", std::move(snapshot.communication)}, {"scenarios", std::move(scenarios)}};
}

RuntimeStatus ApplicationService::runtimeStatus() const {
  RuntimeStatus status;
  status.realtimeEnabled = static_cast<bool>(realtime_);
  status.realtimeGranted = realtime_ ? realtime_->realtimeGranted() : false;
  status.realtimeDetail = realtime_ ? realtime_->realtimeDetail() : "realtime domain disabled";
  status.osAdapter = realtime_ ? realtime_->osAdapterName() : "not-loaded";
  status.deviceAdapter = deviceAdapter_ ? deviceAdapter_->name() : "none";
  status.emergencyControlEnabled = config_.enableEmergencyControl;
  status.emergencyRealtimeGranted = emergency_ ? emergency_->realtimeGranted() : false;
  status.emergencyRealtimeDetail = emergency_ ? emergency_->realtimeDetail() : "emergency realtime domain disabled";
  if (deviceAdapter_) {
    const auto health = deviceAdapter_->health();
    status.deviceAdapterSimulated = deviceAdapter_->simulated();
    status.deviceAdapterReady = health.ready;
    status.deviceAdapterState = health.state;
    status.deviceAdapterDetail = health.detail;
  }
  status.deviceRegistryFile = config_.deviceRegistryFile;
  status.configuredDeviceCount = deviceRegistry_ ? deviceRegistry_->size() : latestSnapshot().devices.size();
  status.commandLedgerEnabled = static_cast<bool>(commandLedger_);
  status.commandLedgerDirectory = commandLedger_ ? commandLedger_->directory() : "disabled";
  status.commandLedgerRecords = commandLedger_ ? commandLedger_->recordCount() : 0;
  status.historianEnabled = static_cast<bool>(historian_);
  status.historianDirectory = historian_ ? historian_->directory() : "disabled";
  status.historianTelemetryRecords = historian_ ? historian_->telemetryRecordCount() : 0;
  status.historianAlarmEvents = historian_ ? historian_->alarmEventCount() : 0;
  status.historianAlarmOccurrences = historian_ ? historian_->alarmOccurrenceCount() : 0;
  if (historian_) {
    const auto storage = historian_->storageHealth();
    status.historianStorageHealthy = storage.healthy;
    status.historianDatabaseBytes = storage.databaseBytes;
    status.historianFreeBytes = storage.freeBytes;
    status.historianLastBackupAt = storage.lastBackupAt;
    status.historianLastBackupPath = storage.lastBackupPath;
  }
  {
    std::lock_guard lock(acquisitionMutex_);
    status.acquisitionState = acquisitionState_;
    status.acquisitionDetail = acquisitionDetail_;
    status.acquisitionLastSuccessTs = acquisitionLastSuccessTs_;
    status.acquisitionLastFailureTs = acquisitionLastFailureTs_;
    status.acquisitionConsecutiveFailures = acquisitionConsecutiveFailures_;
    status.acquisitionTotalFailures = acquisitionTotalFailures_;
  }
  status.acquisitionPollMs = acquisitionIntervalMs();
  status.hmiPublishMs = config_.telemetryPublishMs;
  status.authEnabled = auth_ ? auth_->enabled() : false;
  status.auditEnabled = static_cast<bool>(audit_);
  status.auditDirectory = audit_ ? audit_->directory() : "disabled";
  status.auditRecords = audit_ ? audit_->recordCount() : 0;
  return status;
}

boost::json::object ApplicationService::runtimeStatusJson() const {
  const auto status = runtimeStatus();
  const auto nrtStats = nonRealtime_.stats();
  const auto feedbackStats = feedbackExecutor_.stats();
  const auto rtStats = realtime_ ? realtime_->stats() : ExecutorStats{};
  const auto emergencyStats = emergency_ ? emergency_->stats() : ExecutorStats{};
  auto statsJson = [](const ExecutorStats& stats) {
    return boost::json::object{{"queueDepth", static_cast<std::int64_t>(stats.queueDepth)},
                               {"queueCapacity", static_cast<std::int64_t>(stats.queueCapacity)},
                               {"accepted", static_cast<std::int64_t>(stats.accepted)},
                               {"rejected", static_cast<std::int64_t>(stats.rejected)},
                               {"completed", static_cast<std::int64_t>(stats.completed)}};
  };
  return {{"backend", "cpp"}, {"backendVersion", "0.11.0"}, {"mode", config_.mode},
          {"realtimeEnabled", status.realtimeEnabled}, {"realtimeGranted", status.realtimeGranted},
          {"realtimeDetail", status.realtimeDetail}, {"emergencyRealtimeGranted", status.emergencyRealtimeGranted},
          {"emergencyRealtimeDetail", status.emergencyRealtimeDetail}, {"osAdapter", status.osAdapter},
          {"deviceAdapter", status.deviceAdapter}, {"deviceAdapterSimulated", status.deviceAdapterSimulated},
          {"deviceAdapterReady", status.deviceAdapterReady}, {"deviceAdapterState", status.deviceAdapterState},
          {"deviceAdapterDetail", status.deviceAdapterDetail}, {"deviceRegistryFile", status.deviceRegistryFile},
          {"configuredDeviceCount", static_cast<std::int64_t>(status.configuredDeviceCount)},
          {"emergencyControlEnabled", status.emergencyControlEnabled},
          {"commandLedgerEnabled", status.commandLedgerEnabled}, {"commandLedgerDirectory", status.commandLedgerDirectory},
          {"commandLedgerRecords", static_cast<std::int64_t>(status.commandLedgerRecords)},
          {"executors", boost::json::object{{"nonRealtime", statsJson(nrtStats)}, {"feedback", statsJson(feedbackStats)},
                                             {"realtime", statsJson(rtStats)}, {"emergency", statsJson(emergencyStats)}}},
          {"historianEnabled", status.historianEnabled}, {"historianDirectory", status.historianDirectory},
          {"historianTelemetryRecords", static_cast<std::int64_t>(status.historianTelemetryRecords)},
          {"historianAlarmEvents", static_cast<std::int64_t>(status.historianAlarmEvents)},
          {"historianAlarmOccurrences", static_cast<std::int64_t>(status.historianAlarmOccurrences)},
          {"historianStorageHealthy", status.historianStorageHealthy},
          {"historianDatabaseBytes", static_cast<std::int64_t>(status.historianDatabaseBytes)},
          {"historianFreeBytes", static_cast<std::int64_t>(status.historianFreeBytes)},
          {"historianLastBackupAt", status.historianLastBackupAt}, {"historianLastBackupPath", status.historianLastBackupPath},
          {"acquisition", boost::json::object{{"state",status.acquisitionState},{"detail",status.acquisitionDetail},
                                               {"lastSuccessTs",status.acquisitionLastSuccessTs},{"lastFailureTs",status.acquisitionLastFailureTs},
                                               {"consecutiveFailures",static_cast<std::int64_t>(status.acquisitionConsecutiveFailures)},
                                               {"totalFailures",static_cast<std::int64_t>(status.acquisitionTotalFailures)},
                                               {"pollMs",status.acquisitionPollMs},{"hmiPublishMs",status.hmiPublishMs}}},
          {"authEnabled", status.authEnabled}, {"auditEnabled", status.auditEnabled},
          {"auditDirectory", status.auditDirectory}, {"auditRecords", static_cast<std::int64_t>(status.auditRecords)}};
}


boost::json::object ApplicationService::platformConfigStatus() const {
  if (!configService_) return {{"enabled",false}};
  auto result = configService_->status(); result["enabled"] = true; return result;
}

boost::json::object ApplicationService::platformConfigSnapshot() const {
  if (!configService_) return {{"enabled",false}};
  auto snapshot = configService_->publishedSnapshot(); snapshot["enabled"] = true; return snapshot;
}

boost::json::array ApplicationService::platformAssets() const {
  return configService_ ? configService_->assets() : boost::json::array{};
}

boost::json::array ApplicationService::applicationInstances() const {
  return configService_ ? configService_->applicationInstances() : boost::json::array{};
}

std::optional<std::string> ApplicationService::currentDeviceStatus(const std::string& deviceId) const {
  const auto snapshot = latestSnapshot();
  for (const auto& value : snapshot.devices) {
    if (!value.is_object()) continue;
    const auto& device = value.as_object();
    if (jsonStringOr(device, "id") == deviceId) return jsonStringOr(device, "status", "UNKNOWN");
  }
  return std::nullopt;
}

ApplicationService::MutationResult ApplicationService::previewCommandV2(const boost::json::object& request,
                                                                        const SecurityContext& actor) {
  const auto deviceId = jsonStringOr(request, "deviceId");
  const auto action = jsonStringOr(request, "action");
  const auto reason = jsonStringOr(request, "reason");
  const auto contextId = jsonStringOr(request, "contextId", "live:" + config_.siteName);
  if (contextId.rfind("replay:",0)==0) return {409, {{"error","commands are disabled in replay context"},{"code","REPLAY_COMMAND_FORBIDDEN"}}};
  if (!authorize(actor.principal, Permission::CommandIssue))
    return {403, {{"error","forbidden: command.issue permission required"},{"code","COMMAND_PERMISSION_REQUIRED"}}};
  if (deviceId.empty() || action.empty())
    return {400, {{"error","deviceId and action are required"},{"code","INVALID_COMMAND_PREVIEW"}}};
  if (!deviceRegistry_ || !commandService_)
    return {503, {{"error","command registry/service unavailable"},{"code","COMMAND_SERVICE_UNAVAILABLE"}}};
  const auto* deviceDefinition = deviceRegistry_->find(deviceId);
  if (!deviceDefinition) return {404, {{"error","device not found in registry"},{"code","DEVICE_NOT_REGISTERED"}}};
  const auto* commandDefinition = deviceRegistry_->findCommand(deviceId, action);
  if (!commandDefinition) return {404, {{"error","command is not supported by device"},{"code","COMMAND_NOT_REGISTERED"}}};
  const auto requiredPermission = commandPermission(*commandDefinition);
  if (!requiredPermission || !authorize(actor.principal, *requiredPermission))
    return {403, {{"error","required command permission not granted"},{"code","COMMAND_PERMISSION_REQUIRED"},{"requiredPermission",commandDefinition->requiredPermission}}};
  if (commandDefinition->reasonRequired && reason.empty())
    return {400, {{"error","reason is required for this command"},{"code","COMMAND_REASON_REQUIRED"}}};
  std::string parameterError;
  if (!validateRequestedValue(request, *commandDefinition, parameterError))
    return {400, {{"error",parameterError},{"code","COMMAND_PARAMETER_INVALID"}}};
  const auto status = currentDeviceStatus(deviceId).value_or("UNKNOWN");
  if (status == "OFFLINE")
    return {409, {{"error","device is offline"},{"code","DEVICE_OFFLINE"},{"deviceStatus",status}}};
  if (!commandService_->adapterWriteReady())
    return {409, {{"error",commandService_->adapterWriteReadinessDetail()},{"code","DRIVER_ISOLATION_REQUIRED"}}};
  if (commandLedger_) {
    if (const auto blocking = commandLedger_->blockingUnknownFor(deviceId, action))
      return {409, {{"error","an unresolved prior command blocks the same device action until reconciliation explicitly allows retry"},
                    {"code","COMMAND_UNRESOLVED_BLOCK"},{"blockingCommandId",*blocking}}};
  }

  boost::json::value requestedValue = nullptr;
  if (const auto* value = request.if_contains("requestedValue")) requestedValue = *value;
  const auto revision = runningConfigRevision();
  const auto record = commandService_->createPreview(actor.principal.id, deviceId, action, requestedValue,
                                                      reason, revision, status, nowMs());
  boost::json::object parameterSchema = commandDefinition->parameterSchema;
  boost::json::object body{{"previewId",record.previewId},{"deviceId",deviceId},{"deviceName",deviceDefinition->name},
                           {"action",action},{"displayName",commandDefinition->displayName},
                           {"requestedValue",record.requestedValue},{"reason",reason},
                           {"requiredPermission",commandDefinition->requiredPermission},
                           {"executionClass",DeviceRegistry::executionClassName(commandDefinition->executionClass)},
                           {"parameterSchema",std::move(parameterSchema)},{"configRevision",revision},
                           {"deviceStatus",status},{"createdAt",record.createdAt},{"expiresAt",record.expiresAt},
                           {"adapterWriteReady",true},{"adapterDetail",commandService_->adapterWriteReadinessDetail()}};
  appendAudit(actor,"COMMAND_PREVIEW","COMMAND",record.previewId,"SUCCESS","command preview created",reason);
  return {200,std::move(body)};
}

ApplicationService::MutationResult ApplicationService::issueCommandV2(const boost::json::object& request,
                                                                      const SecurityContext& actor) {
  if (!commandLedger_)
    return {503, {{"error","v2 command idempotency requires command ledger"},{"code","COMMAND_LEDGER_REQUIRED"}}};
  const auto previewId = jsonStringOr(request,"previewId");
  const auto idempotencyKey = jsonStringOr(request,"idempotencyKey");
  const auto contextId = jsonStringOr(request,"contextId","live:" + config_.siteName);
  if(contextId.rfind("replay:",0)==0) return {409,{{"error","commands are disabled in replay context"},{"code","REPLAY_COMMAND_FORBIDDEN"}}};
  if (previewId.empty() || idempotencyKey.empty())
    return {400, {{"error","previewId and idempotencyKey are required"},{"code","COMMAND_PREVIEW_OR_IDEMPOTENCY_REQUIRED"}}};
  if (idempotencyKey.size() > 200)
    return {400, {{"error","idempotencyKey is too long"},{"code","COMMAND_IDEMPOTENCY_INVALID"}}};
  if (!commandService_) return {503, {{"error","command service unavailable"},{"code","COMMAND_SERVICE_UNAVAILABLE"}}};
  const auto preview = commandService_->preview(previewId, actor.principal.id, nowMs());
  if (!preview) return {409, {{"error","preview is missing, expired, or belongs to another operator"},{"code","COMMAND_PREVIEW_INVALID"}}};
  const auto currentRevision = runningConfigRevision();
  if (preview->configRevision != currentRevision)
    return {409, {{"error","configuration changed after preview"},{"code","COMMAND_PREVIEW_CONFIG_CHANGED"},
                  {"previewConfigRevision",preview->configRevision},{"currentConfigRevision",currentRevision}}};
  const auto deviceId = jsonStringOr(request,"deviceId");
  const auto action = jsonStringOr(request,"action");
  const auto reason = jsonStringOr(request,"reason");
  boost::json::value requestedValue = nullptr;
  if (const auto* value=request.if_contains("requestedValue")) requestedValue=*value;
  if (deviceId != preview->deviceId || action != preview->action || reason != preview->reason || requestedValue != preview->requestedValue)
    return {409, {{"error","submitted command does not match preview"},{"code","COMMAND_PREVIEW_MISMATCH"}}};
  const auto status = currentDeviceStatus(deviceId).value_or("UNKNOWN");
  if (status != preview->deviceStatus)
    return {409, {{"error","device state changed after preview"},{"code","COMMAND_PREVIEW_DEVICE_STATE_CHANGED"},
                  {"previewDeviceStatus",preview->deviceStatus},{"currentDeviceStatus",status}}};
  if (status == "OFFLINE") return {409, {{"error","device became offline after preview"},{"code","DEVICE_OFFLINE"}}};
  if (!authorize(actor.principal, Permission::CommandIssue))
    return {403, {{"error","command permission changed after preview"},{"code","COMMAND_PERMISSION_REQUIRED"}}};

  boost::json::object canonical = request;
  std::string commandId = jsonStringOr(canonical,"commandId");
  if (commandId.empty()) commandId = "cmd-v2-" + std::to_string(nowMs()) + "-" + std::to_string(std::hash<std::string>{}(idempotencyKey));
  canonical["commandId"] = commandId;
  canonical["configRevision"] = currentRevision;
  canonical["previewId"] = previewId;
  canonical["idempotencyKey"] = idempotencyKey;
  const auto dedupExpiresAt = nowMs() + 7LL * 24 * 60 * 60 * 1000;
  canonical["idempotencyExpiresAt"] = dedupExpiresAt;
  const auto requestHash = commandDedupHash(canonical, actor);
  const auto result = issueCommandInternal(canonical, actor, idempotencyKey, requestHash, dedupExpiresAt);
  if (result.httpStatus == 202 || result.httpStatus == 200) commandService_->consumePreview(previewId);
  return result;
}

boost::json::object ApplicationService::commandV2(const std::string& commandId, const SecurityContext& actor) const {
  if (!authorize(actor.principal, Permission::View)) return {{"error","forbidden"},{"code","VIEW_PERMISSION_REQUIRED"}};
  boost::json::object command;
  {
    std::lock_guard lock(commandsMutex_);
    if (const auto it=commands_.find(commandId); it!=commands_.end()) command=it->second.command;
  }
  if (command.empty() && commandLedger_) {
    if (const auto persisted=commandLedger_->find(commandId)) command=persisted->command;
  }
  if (command.empty()) return {{"error","command not found"},{"code","COMMAND_NOT_FOUND"}};
  boost::json::array reconciliations;
  if (commandLedger_) for (const auto& r : commandLedger_->reconciliations(commandId)) {
    reconciliations.emplace_back(boost::json::object{{"reconciliationId",r.reconciliationId},{"actorId",r.actorId},
      {"conclusion",r.conclusion},{"evidence",r.evidence},{"allowRetry",r.allowRetry},{"createdAt",r.createdAt}});
  }
  command["reconciliations"] = std::move(reconciliations);
  return command;
}

ApplicationService::MutationResult ApplicationService::reconcileCommandV2(const std::string& commandId,
                                                                           const boost::json::object& request,
                                                                           const SecurityContext& actor) {
  if (!authorize(actor.principal, Permission::CommandReconcile))
    return {403, {{"error","forbidden: command.reconcile permission required"},{"code","COMMAND_RECONCILE_FORBIDDEN"}}};
  if (!commandLedger_) return {503, {{"error","command ledger unavailable"},{"code","COMMAND_LEDGER_REQUIRED"}}};
  auto current = commandV2(commandId, actor);
  if (jsonStringOr(current,"code") == "COMMAND_NOT_FOUND") return {404,std::move(current)};
  if (jsonStringOr(current,"state") != "OUTCOME_UNKNOWN")
    return {409, {{"error","only OUTCOME_UNKNOWN commands require reconciliation"},{"code","COMMAND_RECONCILE_STATE_INVALID"}}};
  const auto conclusion = jsonStringOr(request,"conclusion");
  const auto evidence = jsonStringOr(request,"evidence");
  if (conclusion != "CONFIRMED_APPLIED" && conclusion != "CONFIRMED_NOT_APPLIED" && conclusion != "INCONCLUSIVE")
    return {400, {{"error","invalid reconciliation conclusion"},{"code","COMMAND_RECONCILIATION_INVALID"}}};
  if (evidence.empty()) return {400, {{"error","evidence is required"},{"code","COMMAND_RECONCILIATION_EVIDENCE_REQUIRED"}}};
  bool allowRetry = false;
  if (const auto* value=request.if_contains("allowRetry"); value && value->is_bool()) allowRetry=value->as_bool();
  if (conclusion == "INCONCLUSIVE" && allowRetry)
    return {409, {{"error","retry cannot be allowed while reconciliation remains inconclusive"},{"code","COMMAND_RETRY_REQUIRES_CONCLUSION"}}};
  const auto createdAt = nowMs();
  CommandReconciliationRecord record;
  record.reconciliationId = "recon-" + std::to_string(createdAt) + "-" + std::to_string(std::hash<std::string>{}(commandId+actor.principal.id+evidence));
  record.commandId = commandId;
  record.actorId = actor.principal.id;
  record.conclusion = conclusion;
  record.evidence = evidence;
  record.allowRetry = allowRetry;
  record.createdAt = createdAt;
  commandLedger_->appendReconciliation(record);
  appendCommandEvidence(commandId,"MANUAL_RECONCILIATION","manual reconciliation recorded without rewriting original command state",
                        boost::json::object{{"reconciliationId",record.reconciliationId},{"conclusion",conclusion},
                                            {"evidence",evidence},{"allowRetry",allowRetry},{"actorId",actor.principal.id}});
  appendAudit(actor,"COMMAND_RECONCILE","COMMAND",commandId,"SUCCESS",conclusion,evidence);
  return {200, commandV2(commandId, actor)};
}

boost::json::array ApplicationService::platformRules() const {
  return ruleService_ ? ruleService_->rules() : boost::json::array{};
}

boost::json::array ApplicationService::alarmOccurrencesV2(const std::string& deviceId, const std::string& sourceDomain) const {
  return alarmService_ ? alarmService_->occurrences(deviceId, sourceDomain) : boost::json::array{};
}

ApplicationService::MutationResult ApplicationService::acknowledgeAlarmOccurrenceV2(
    const std::string& occurrenceId, const boost::json::object& request, const SecurityContext& actor) {
  const auto comment = jsonStringOr(request, "comment");
  if (!authorize(actor.principal, Permission::AlarmAcknowledge))
    return {403, {{"error","forbidden: alarm.ack permission required"},{"code","ALARM_ACK_FORBIDDEN"}}};
  if (!alarmService_) return {503, {{"error","v2 alarm service unavailable"},{"code","ALARM_SERVICE_UNAVAILABLE"}}};
  if (occurrenceId.empty()) return {400, {{"error","occurrenceId is required"},{"code","OCCURRENCE_ID_REQUIRED"}}};
  try {
    auto updated = alarmService_->acknowledge(occurrenceId, actor, comment);
    appendAudit(actor,"ALARM_OCCURRENCE_ACK","ALARM_OCCURRENCE",occurrenceId,"SUCCESS","alarm occurrence acknowledged",comment);
    emitRealtimeEvent("alarm.v2.updated", updated);
    return {200,std::move(updated)};
  } catch (const std::exception& e) {
    appendAudit(actor,"ALARM_OCCURRENCE_ACK","ALARM_OCCURRENCE",occurrenceId,"FAILED",e.what(),comment);
    return {404,{{"error",e.what()},{"code","ALARM_OCCURRENCE_NOT_FOUND"}}};
  }
}

boost::json::array ApplicationService::incidentsV2() const {
  return incidentService_ ? incidentService_->list() : boost::json::array{};
}

ApplicationService::MutationResult ApplicationService::createIncidentV2(const boost::json::object& request, const SecurityContext& actor) {
  if (!authorize(actor.principal, Permission::IncidentAssign))
    return {403,{{"error","forbidden: incident.assign permission required"},{"code","INCIDENT_ASSIGN_FORBIDDEN"}}};
  if (!incidentService_ || !alarmService_) return {503,{{"error","incident service unavailable"},{"code","INCIDENT_SERVICE_UNAVAILABLE"}}};
  if (const auto* ids=request.if_contains("occurrenceIds"); !ids || !ids->is_array() || ids->as_array().empty())
    return {422,{{"error","occurrenceIds[] is required"},{"code","INCIDENT_OCCURRENCES_REQUIRED"}}};
  for (const auto& idValue : request.at("occurrenceIds").as_array()) {
    if (!idValue.is_string() || !alarmService_->occurrence(std::string(idValue.as_string())))
      return {422,{{"error","incident references unknown alarm occurrence"},{"code","INCIDENT_OCCURRENCE_NOT_FOUND"}}};
  }
  try {
    auto incident=incidentService_->create(request,actor);
    appendAudit(actor,"INCIDENT_CREATE","INCIDENT",jsonStringOr(incident,"incidentId"),"SUCCESS","incident created");
    emitRealtimeEvent("incident.updated",incident);
    return {201,std::move(incident)};
  } catch(const std::exception& e){return {422,{{"error",e.what()},{"code","INCIDENT_CREATE_FAILED"}}};}
}

ApplicationService::MutationResult ApplicationService::transitionIncidentV2(
    const std::string& incidentId, const boost::json::object& request, const SecurityContext& actor) {
  if (!incidentService_ || !alarmService_) return {503,{{"error","incident service unavailable"},{"code","INCIDENT_SERVICE_UNAVAILABLE"}}};
  const auto action=jsonStringOr(request,"action");
  Permission permission=Permission::IncidentResolve;
  if(action=="ASSIGN") permission=Permission::IncidentAssign;
  else if(action=="CLOSE") permission=Permission::IncidentClose;
  if(!authorize(actor.principal,permission)) return {403,{{"error","forbidden: incident permission required"},{"code","INCIDENT_FORBIDDEN"}}};
  if(action=="CLOSE") {
    auto existing=incidentService_->get(incidentId);
    if(!existing) return {404,{{"error","incident not found"},{"code","INCIDENT_NOT_FOUND"}}};
    if(const auto* ids=existing->if_contains("occurrenceIds"); ids&&ids->is_array()) {
      for(const auto& value:ids->as_array()) if(value.is_string()) {
        auto occurrence=alarmService_->occurrence(std::string(value.as_string()));
        if(!occurrence || jsonStringOr(*occurrence,"conditionState")!="NORMAL" || jsonStringOr(*occurrence,"ackState")!="ACKNOWLEDGED")
          return {409,{{"error","incident cannot close until linked alarm conditions are normal and acknowledged"},{"code","INCIDENT_CLOSE_CONDITION_UNMET"}}};
      }
    }
  }
  try {
    auto incident=incidentService_->transition(incidentId,request,actor);
    appendAudit(actor,"INCIDENT_"+action,"INCIDENT",incidentId,"SUCCESS","incident state changed",jsonStringOr(request,"comment"));
    emitRealtimeEvent("incident.updated",incident);
    return {200,std::move(incident)};
  } catch(const std::exception& e){const std::string m=e.what();return {m=="INCIDENT_REVISION_CONFLICT"?409:422,{{"error",m},{"code",m}}};}
}

boost::json::object ApplicationService::timelineV2(std::int64_t fromTs, std::int64_t toTs, const std::string& instanceId) const {
  boost::json::array items; std::set<std::string> deviceIds, occurrenceIds;
  if(!instanceId.empty() && configService_){
    for(const auto& iv:configService_->applicationInstances()) if(iv.is_object()&&jsonStringOr(iv.as_object(),"instanceId")==instanceId){
      if(const auto* b=iv.as_object().if_contains("bindings");b&&b->is_object())for(const auto&[_,rv]:b->as_object())if(rv.is_object())deviceIds.insert(jsonStringOr(rv.as_object(),"deviceId"));
      break;
    }
  }
  if(alarmService_) {
    if(!deviceIds.empty()) {
      for(const auto& ov: alarmService_->occurrences()) if(ov.is_object() && deviceIds.contains(jsonStringOr(ov.as_object(),"deviceId")))
        occurrenceIds.insert(jsonStringOr(ov.as_object(),"occurrenceId"));
    }
    for(auto& v: alarmService_->events(fromTs,toTs)) {
      if(!v.is_object())continue; auto& o=v.as_object();
      if(!deviceIds.empty() && !occurrenceIds.contains(jsonStringOr(o,"occurrenceId"))) continue;
      o["timelineDomain"]="ALARM"; items.emplace_back(std::move(v));
    }
  }
  if(incidentService_) for(auto& v: incidentService_->events(fromTs,toTs)) {
    if(!v.is_object())continue; auto& o=v.as_object();
    if(!deviceIds.empty()) { auto inc=incidentService_->get(jsonStringOr(o,"incidentId")); bool related=false; if(inc) if(const auto* ids=inc->if_contains("occurrenceIds");ids&&ids->is_array()) for(const auto& idv:ids->as_array()) if(idv.is_string()&&occurrenceIds.contains(std::string(idv.as_string()))) {related=true;break;} if(!related)continue; }
    o["timelineDomain"]="INCIDENT"; items.emplace_back(std::move(v));
  }
  if(commandLedger_) for(const auto& pc:commandLedger_->recent(500)) {
    const auto& c=pc.command; const auto ts=jsonInt(c,"updatedAt").value_or(jsonInt(c,"issuedAt").value_or(0));
    if(ts<fromTs||ts>toTs)continue; const auto did=jsonStringOr(c,"deviceId"); if(!deviceIds.empty()&&!deviceIds.contains(did))continue;
    boost::json::object e{{"eventId","command-"+pc.commandId+"-"+std::to_string(ts)},{"timelineDomain","COMMAND"},{"eventType","COMMAND_STATE"},{"timestamp",ts},{"commandId",pc.commandId},{"deviceId",did},{"action",jsonStringOr(c,"action")},{"state",jsonStringOr(c,"state")},{"revision",jsonInt(c,"revision").value_or(0)}}; items.emplace_back(std::move(e));
    for(const auto& r:commandLedger_->reconciliations(pc.commandId)) if(r.createdAt>=fromTs&&r.createdAt<=toTs) items.emplace_back(boost::json::object{{"eventId",r.reconciliationId},{"timelineDomain","COMMAND"},{"eventType","RECONCILIATION"},{"timestamp",r.createdAt},{"commandId",pc.commandId},{"actor",r.actorId},{"conclusion",r.conclusion},{"comment",r.evidence}});
  }
  if(configService_) for(auto& v:configService_->events(fromTs,toTs)){
    if(v.is_object()){
      v.as_object()["timelineDomain"]="CONFIG";
      if(!instanceId.empty()) v.as_object()["relation"]="SAME_WINDOW";
    }
    items.emplace_back(std::move(v));
  }
  std::sort(items.begin(),items.end(),[](const auto& a,const auto& b){return ApplicationService::jsonInt(a.as_object(),"timestamp").value_or(0)<ApplicationService::jsonInt(b.as_object(),"timestamp").value_or(0);});
  return {{"items",std::move(items)},{"from",fromTs},{"to",toTs},{"instanceId",instanceId}};
}

ApplicationService::MutationResult ApplicationService::createReplaySessionV2(const boost::json::object& request, const SecurityContext& actor) {
  if(!authorize(actor.principal,Permission::View))return {403,{{"error","forbidden"},{"code","VIEW_PERMISSION_REQUIRED"}}};
  if(!replayService_)return {503,{{"error","replay requires historian and active platform configuration"},{"code","REPLAY_UNAVAILABLE"}}};
  const auto instanceId=jsonStringOr(request,"instanceId"); const auto from=jsonInt(request,"from").value_or(-1); const auto to=jsonInt(request,"to").value_or(-1); const auto limit=static_cast<std::size_t>(jsonInt(request,"limit").value_or(5000)); const auto configRevision=jsonStringOr(request,"configRevision");
  if(instanceId.empty()||from<0||to<from||to-from>7LL*24*60*60*1000||limit<1||limit>10000)return {422,{{"error","invalid replay range/instance/limit"},{"code","REPLAY_REQUEST_INVALID"}}};
  try{auto result=replayService_->create(instanceId,from,to,limit,configRevision);appendAudit(actor,"REPLAY_CREATE","REPLAY",jsonStringOr(result,"sessionId"),"SUCCESS","isolated replay session created");return {201,std::move(result)};}catch(const std::exception&e){return {422,{{"error",e.what()},{"code","REPLAY_CREATE_FAILED"}}};}
}
boost::json::object ApplicationService::replaySessionV2(const std::string& sessionId,const SecurityContext& actor) const {if(!authorize(actor.principal,Permission::View))return {{"error","forbidden"},{"code","VIEW_PERMISSION_REQUIRED"}};try{return replayService_?replayService_->get(sessionId):boost::json::object{{"error","replay unavailable"},{"code","REPLAY_UNAVAILABLE"}};}catch(const std::exception&e){return {{"error",e.what()},{"code","REPLAY_NOT_FOUND"}};}}

ApplicationService::MutationResult ApplicationService::createReportJobV2(const boost::json::object& request,const SecurityContext& actor){
  if(!authorize(actor.principal,Permission::ReportExport))return {403,{{"error","forbidden: report.export permission required"},{"code","REPORT_EXPORT_FORBIDDEN"}}};
  if(!reportService_||!historian_||!configService_)return {503,{{"error","report service requires historian and platform configuration"},{"code","REPORT_UNAVAILABLE"}}};
  const auto instanceId=jsonStringOr(request,"instanceId"); const auto from=jsonInt(request,"from").value_or(-1),to=jsonInt(request,"to").value_or(-1);
  if(instanceId.empty()||from<0||to<from||to-from>31LL*24*60*60*1000)return {422,{{"error","invalid report range/instance"},{"code","REPORT_REQUEST_INVALID"}}};
  boost::json::object instance; bool found=false; for(const auto&iv:configService_->applicationInstances())if(iv.is_object()&&jsonStringOr(iv.as_object(),"instanceId")==instanceId){instance=iv.as_object();found=true;break;} if(!found)return {404,{{"error","application instance not found"},{"code","INSTANCE_NOT_FOUND"}}};
  boost::json::array pointsSummary; std::ostringstream csv; csv<<"role,deviceId,pointId,sampleTs,value,unit,quality,origin,configRevision\n"; std::set<std::string> deviceIds;
  const auto snapshot=configService_->activeSnapshot(); const auto* bindings=instance.if_contains("bindings");
  if(bindings&&bindings->is_object())for(const auto&[role,rv]:bindings->as_object())if(rv.is_object()){
    const auto did=jsonStringOr(rv.as_object(),"deviceId"),pid=jsonStringOr(rv.as_object(),"pointId");deviceIds.insert(did);TelemetryHistoryQuery q{did,pid,from,to,10000};auto hist=historian_->queryTelemetry(q);const auto& items=hist.at("items").as_array();
    std::int64_t period=1000; if(const auto* devs=snapshot.if_contains("devices");devs&&devs->is_array())for(const auto&dv:devs->as_array())if(dv.is_object()&&jsonStringOr(dv.as_object(),"id")==did)if(const auto*ps=dv.as_object().if_contains("points");ps&&ps->is_array())for(const auto&pv:ps->as_array())if(pv.is_object()&&jsonStringOr(pv.as_object(),"pointId")==pid)period=jsonInt(pv.as_object(),"samplePeriodMs").value_or(1000);
    auto cov=QueryService::coverage(items,from,to,period); boost::json::object ps{{"role",role},{"deviceId",did},{"pointId",pid},{"coverage",cov},{"samplePeriodMs",period}};
    if(instance.if_contains("templateId")&&jsonStringOr(instance,"templateId")=="goods_counting"&&std::string(role)=="totalCount")ps["counterSegments"]=QueryService::counterSegments(items,period*3);
    pointsSummary.emplace_back(std::move(ps));
    for(const auto&v:items)if(v.is_object()){const auto&o=v.as_object();std::string val=o.if_contains("value")?boost::json::serialize(o.at("value")):"";std::string unit=jsonStringOr(o,"unit");std::string origin="UNKNOWN";if(const auto*prov=o.if_contains("provenance");prov&&prov->is_object())origin=jsonStringOr(prov->as_object(),"originKind","UNKNOWN");csv<<ReportService::csvCell(std::string(role))<<','<<ReportService::csvCell(did)<<','<<ReportService::csvCell(pid)<<','<<jsonInt(o,"sampleTs").value_or(jsonInt(o,"receiveTs").value_or(0))<<','<<ReportService::csvCell(val)<<','<<ReportService::csvCell(unit)<<','<<ReportService::csvCell(jsonStringOr(o,"quality"))<<','<<ReportService::csvCell(origin)<<','<<ReportService::csvCell(jsonStringOr(o,"configRevision"))<<"\n";}
  }
  std::int64_t occurrenceCount=0,unacked=0; if(alarmService_)for(const auto&v:alarmService_->occurrences())if(v.is_object()&&deviceIds.contains(jsonStringOr(v.as_object(),"deviceId"))){const auto raised=jsonInt(v.as_object(),"raisedAt").value_or(0);if(raised>=from&&raised<=to){++occurrenceCount;if(jsonStringOr(v.as_object(),"ackState")!="ACKNOWLEDGED")++unacked;}}
  std::int64_t unresolvedCommands=0;if(commandLedger_)for(const auto&pc:commandLedger_->recent(500))if(deviceIds.contains(jsonStringOr(pc.command,"deviceId"))&&jsonStringOr(pc.command,"state")=="OUTCOME_UNKNOWN")++unresolvedCommands;
  boost::json::object summary{{"instanceId",instanceId},{"displayName",jsonStringOr(instance,"displayName")},{"from",from},{"to",to},{"dataCutoff",to},{"configRevision",runningConfigRevision()},{"points",pointsSummary},{"alarmOccurrences",occurrenceCount},{"unacknowledgedOccurrences",unacked},{"unresolvedCommands",unresolvedCommands}};
  auto esc=[](std::string v){std::string o;for(char c:v){if(c=='&')o+="&amp;";else if(c=='<')o+="&lt;";else if(c=='>')o+="&gt;";else if(c=='\"')o+="&quot;";else o+=c;}return o;};
  std::ostringstream html;html<<"<!doctype html><meta charset=\"utf-8\"><title>运行报告</title><h1>"<<esc(jsonStringOr(instance,"displayName"))<<"</h1><p>范围: "<<from<<" - "<<to<<"</p><p>配置版本: "<<esc(runningConfigRevision())<<"</p><p>报警发生: "<<occurrenceCount<<"；未确认: "<<unacked<<"；未决命令: "<<unresolvedCommands<<"</p><pre>"<<esc(boost::json::serialize(pointsSummary))<<"</pre>";
  boost::json::object params=request;params["dataCutoff"]=to;try{auto job=reportService_->saveCompleted(params,summary,html.str(),csv.str());appendAudit(actor,"REPORT_EXPORT","REPORT",jsonStringOr(job,"jobId"),"SUCCESS","bounded report generated");return {201,std::move(job)};}catch(const std::exception&e){return {422,{{"error",e.what()},{"code","REPORT_CREATE_FAILED"}}};}
}
boost::json::object ApplicationService::reportJobV2(const std::string&jobId,const SecurityContext& actor)const{if(!authorize(actor.principal,Permission::ReportExport))return {{"error","forbidden"},{"code","REPORT_EXPORT_FORBIDDEN"}};try{return reportService_?reportService_->get(jobId):boost::json::object{{"error","report unavailable"},{"code","REPORT_UNAVAILABLE"}};}catch(const std::exception&e){return {{"error",e.what()},{"code","REPORT_NOT_FOUND"}};}}


boost::json::object ApplicationService::agentStatusV2(const SecurityContext& actor) const {
  (void)actor;
  return boost::json::object{
    {"enabled",config_.agentEnabled},
    {"configured",agentClient_ && agentClient_->configured()},
    {"provider",agentClient_?agentClient_->provider():config_.agentProvider},
    {"model",agentClient_?agentClient_->model():config_.agentModel},
    {"apiKeyEnv",config_.agentApiKeyEnv},
    {"mode",agentClient_ && agentClient_->configured()?"MODEL":"LOCAL"}
  };
}

ApplicationService::MutationResult ApplicationService::agentChatV2(const boost::json::object& request, const SecurityContext& actor) {
  if (!authorize(actor.principal, Permission::View)) return {403,{{"error","forbidden"},{"code","AGENT_FORBIDDEN"}}};
  const auto message=jsonStringOr(request,"message");
  if(message.empty()||message.size()>4000) return {422,{{"error","message is required and must be <= 4000 characters"},{"code","AGENT_MESSAGE_INVALID"}}};
  std::vector<std::pair<std::string,std::string>> history;
  if (const auto* hv=request.if_contains("history"); hv && hv->is_array()) {
    const auto& items=hv->as_array();
    const std::size_t start=items.size()>12?items.size()-12:0;
    for(std::size_t i=start;i<items.size();++i){
      if(!items[i].is_object()) continue;
      const auto& o=items[i].as_object();
      const auto role=jsonStringOr(o,"role"), content=jsonStringOr(o,"content");
      if((role=="user"||role=="assistant")&&!content.empty()&&content.size()<=4000) history.emplace_back(role,content);
    }
  }

  const auto snapshot=dashboardSnapshot();
  boost::json::object context;
  context["siteName"]=config_.siteName;
  context["generatedAt"]=snapshot.at("generatedAt");
  context["provenance"]=snapshot.at("provenance");
  context["devices"]=snapshot.at("devices");
  context["telemetry"]=snapshot.at("telemetry");
  context["alarms"]=snapshot.at("alarms");
  context["commands"]=snapshot.at("commands");
  context["communication"]=snapshot.at("communication");
  if(configService_){context["applications"]=configService_->applicationInstances();const auto cfgSnapshot=configService_->activeSnapshot();if(const auto* rv=cfgSnapshot.if_contains("rules"))context["rules"]=*rv;context["configRevision"]=runningConfigRevision();}
  if(alarmService_) context["alarmOccurrences"]=alarmService_->occurrences();
  if(incidentService_) context["incidents"]=incidentService_->list();
  if(historian_ && snapshot.at("telemetry").is_array()) {
    boost::json::array recentTrends;
    const auto toTs=nowMs();
    const auto fromTs=toTs-60LL*60*1000;
    std::size_t included=0;
    for(const auto& item:snapshot.at("telemetry").as_array()) {
      if(!item.is_object() || included>=32) continue;
      const auto& point=item.as_object();
      const auto deviceId=jsonStringOr(point,"deviceId"), pointId=jsonStringOr(point,"pointId");
      if(deviceId.empty()||pointId.empty()) continue;
      try {
        const auto historyResult=historian_->queryTelemetry(TelemetryHistoryQuery{deviceId,pointId,fromTs,toTs,240});
        const auto* values=historyResult.if_contains("items");
        if(!values||!values->is_array()) continue;
        std::size_t numericCount=0,good=0,uncertain=0,bad=0,stale=0;
        double sum=0.0,minValue=0.0,maxValue=0.0;
        std::int64_t lastTs=0;
        for(const auto& sample:values->as_array()) {
          if(!sample.is_object()) continue;
          const auto& sampleObj=sample.as_object();
          const auto quality=jsonStringOr(sampleObj,"quality");
          if(quality=="GOOD")++good; else if(quality=="UNCERTAIN")++uncertain; else if(quality=="BAD")++bad; else if(quality=="STALE")++stale;
          const auto sampleTs=jsonInt(sampleObj,"sampleTs").value_or(jsonInt(sampleObj,"receiveTs").value_or(0));
          lastTs=std::max(lastTs,sampleTs);
          const auto* raw=sampleObj.if_contains("value");
          if(!raw) continue;
          double number=0.0; bool ok=false;
          if(raw->is_double()){number=raw->as_double();ok=std::isfinite(number);}
          else if(raw->is_int64()){number=static_cast<double>(raw->as_int64());ok=true;}
          else if(raw->is_uint64()){number=static_cast<double>(raw->as_uint64());ok=true;}
          if(!ok) continue;
          if(numericCount==0){minValue=maxValue=number;}else{minValue=std::min(minValue,number);maxValue=std::max(maxValue,number);}
          sum+=number;++numericCount;
        }
        boost::json::object summary{{"deviceId",deviceId},{"pointId",pointId},{"label",jsonStringOr(point,"label")},{"unit",jsonStringOr(point,"unit")},{"sampleCount",values->as_array().size()},{"lastTs",lastTs},{"quality",boost::json::object{{"good",good},{"uncertain",uncertain},{"bad",bad},{"stale",stale}}}};
        if(numericCount){summary["numericSamples"]=numericCount;summary["min"]=minValue;summary["max"]=maxValue;summary["avg"]=sum/static_cast<double>(numericCount);}
        recentTrends.emplace_back(std::move(summary));++included;
      } catch(...) {}
    }
    context["recentHour"]=std::move(recentTrends);
  }

  const auto activeAlarms = [&]() { std::size_t count=0; for(const auto& item:snapshot.at("alarms").as_array()) if(item.is_object()&&jsonStringOr(item.as_object(),"state")!="CLEARED") ++count; return count; }();
  const auto criticalAlarms = [&]() { std::size_t count=0; for(const auto& item:snapshot.at("alarms").as_array()) if(item.is_object()&&jsonStringOr(item.as_object(),"state")!="CLEARED"&&jsonStringOr(item.as_object(),"severity")=="CRITICAL") ++count; return count; }();
  const auto unresolvedCommands = [&]() { std::size_t count=0; for(const auto& item:snapshot.at("commands").as_array()) if(item.is_object()&&jsonStringOr(item.as_object(),"state")=="OUTCOME_UNKNOWN") ++count; return count; }();

  const auto localSummary = [&]() {
    std::ostringstream out;
    out << "当前运行概况：设备 " << snapshot.at("devices").as_array().size() << " 台，活动报警 " << activeAlarms
        << " 条，结果待确认操作 " << unresolvedCommands << " 项。";
    if(criticalAlarms>0) out << " 建议先处理严重报警，再检查相关设备和通信状态。";
    else if(activeAlarms>0) out << " 建议先处理当前活动报警，再检查相关设备和通信状态。";
    else out << " 当前没有需要优先处理的活动报警。";
    out << " 可继续查询设备状态、报警、通信、最近一小时趋势、处置记录或操作风险。";
    return out.str();
  };

  std::string answer;
  std::string mode="LOCAL";
  try {
    if(agentClient_ && agentClient_->configured()) {
      const std::string systemPrompt =
        "你是智慧工厂生产助手。只依据系统提供的当前数据、最近一小时趋势摘要、配置、报警、操作记录和处置记录回答。"
        "当 provenance.originKind 为 SIMULATION 时，在回答中称为内置数据源，不得描述为真实现场采集；REAL_DEVICE 才可称为现场采集。"
        "回答使用简洁、专业、直接的中文，先给结论，再给影响对象、依据和处理顺序。"
        "不要使用AI营销语言，不要使用数据资产、智能洞察、赋能、AI大脑等词。"
        "你可以查询、汇总、比较和提出处理建议，但不得声称已经执行设备控制；设备控制必须由操作人员在界面中确认并经过权限、预览、命令和反馈链。"
        "系统上下文JSON如下：\n" + boost::json::serialize(context);
      answer=agentClient_->complete(systemPrompt,history,message);
      mode="MODEL";
    } else {
      answer=localSummary();
    }
    appendAudit(actor,"AGENT_QUERY","AGENT","assistant","SUCCESS",mode=="MODEL"?"model response":"local summary");
    return {200,{{"answer",answer},{"mode",mode},{"provider",agentClient_?agentClient_->provider():config_.agentProvider},{"model",agentClient_?agentClient_->model():config_.agentModel},{"generatedAt",nowMs()},{"activeAlarms",activeAlarms},{"unresolvedCommands",unresolvedCommands}}};
  } catch(const std::exception& e) {
    appendAudit(actor,"AGENT_QUERY","AGENT","assistant","FAILED",e.what());
    answer=localSummary()+" 模型连接暂不可用，当前已使用系统运行数据完成基础分析。";
    appendAudit(actor,"AGENT_QUERY","AGENT","assistant","SUCCESS","local fallback after provider error");
    return {200,{{"answer",answer},{"mode","LOCAL_FALLBACK"},{"provider",agentClient_?agentClient_->provider():config_.agentProvider},{"model",agentClient_?agentClient_->model():config_.agentModel},{"generatedAt",nowMs()},{"activeAlarms",activeAlarms},{"unresolvedCommands",unresolvedCommands}}};
  }
}

ApplicationService::MutationResult ApplicationService::createConfigDraft(const boost::json::object& request, const SecurityContext& actor) {
  if (!configService_) return {503, {{"error","platform configuration is disabled"},{"code","CONFIG_DISABLED"}}};
  if (!authorize(actor.principal, Permission::ConfigEdit)) return {403, {{"error","forbidden: config.edit permission required"},{"code","CONFIG_EDIT_FORBIDDEN"}}};
  std::string baseRevision; if (const auto* value=request.if_contains("baseRevision"); value && value->is_string()) baseRevision=std::string(value->as_string());
  std::optional<boost::json::object> snapshot; if (const auto* value=request.if_contains("snapshot")) { if (!value->is_object()) return {422,{{"error","snapshot must be an object"},{"code","CONFIG_SNAPSHOT_INVALID"}}}; snapshot=value->as_object(); }
  try { auto result=configService_->createDraft(baseRevision,snapshot,actor.principal.id); appendAudit(actor,"CONFIG_DRAFT_CREATE","CONFIG",std::string(result.at("draftId").as_string()),"SUCCESS","configuration draft created"); return {201,std::move(result)}; }
  catch(const std::exception& e){ appendAudit(actor,"CONFIG_DRAFT_CREATE","CONFIG","unknown","FAILED",e.what()); return {400,{{"error",e.what()},{"code","CONFIG_DRAFT_CREATE_FAILED"}}}; }
}

ApplicationService::MutationResult ApplicationService::validateConfigDraft(const std::string& draftId, const SecurityContext& actor) {
  if (!configService_) return {503, {{"error","platform configuration is disabled"},{"code","CONFIG_DISABLED"}}};
  if (!authorize(actor.principal, Permission::ConfigEdit)) return {403, {{"error","forbidden: config.edit permission required"},{"code","CONFIG_EDIT_FORBIDDEN"}}};
  try { auto result=configService_->validateDraft(draftId); return {result.at("valid").as_bool()?200:422,std::move(result)}; }
  catch(const std::exception& e){ return {404,{{"error",e.what()},{"code","CONFIG_DRAFT_NOT_FOUND"}}}; }
}

ApplicationService::MutationResult ApplicationService::publishConfigDraft(const std::string& draftId, const boost::json::object& request, const SecurityContext& actor) {
  if (!configService_) return {503, {{"error","platform configuration is disabled"},{"code","CONFIG_DISABLED"}}};
  if (!authorize(actor.principal, Permission::ConfigPublish)) return {403, {{"error","forbidden: config.publish permission required"},{"code","CONFIG_PUBLISH_FORBIDDEN"}}};
  std::string reason; if (const auto* value=request.if_contains("reason"); value && value->is_string()) reason=std::string(value->as_string());
  if (reason.empty()) return {422,{{"error","reason is required"},{"code","CONFIG_REASON_REQUIRED"}}};
  try { auto result=configService_->publishDraft(draftId,actor.principal.id,reason); const bool ok=result.if_contains("ok")&&result.at("ok").as_bool(); if(!ok){const auto code=result.if_contains("code")&&result.at("code").is_string()?std::string(result.at("code").as_string()):"CONFIG_PUBLISH_FAILED";return {code=="CONFIG_REVISION_CONFLICT"?409:422,std::move(result)};} const auto revision=std::string(result.at("revision").as_string()); appendAudit(actor,"CONFIG_PUBLISH","CONFIG",revision,"SUCCESS","configuration published",reason); emitRealtimeEvent("config.published",result); return {200,std::move(result)}; }
  catch(const std::exception& e){ appendAudit(actor,"CONFIG_PUBLISH","CONFIG",draftId,"FAILED",e.what(),reason); return {400,{{"error",e.what()},{"code","CONFIG_PUBLISH_FAILED"}}}; }
}

ApplicationService::MutationResult ApplicationService::rollbackConfig(const std::string& revision, const boost::json::object& request, const SecurityContext& actor) {
  if (!configService_) return {503, {{"error","platform configuration is disabled"},{"code","CONFIG_DISABLED"}}};
  if (!authorize(actor.principal, Permission::ConfigPublish)) return {403, {{"error","forbidden: config.publish permission required"},{"code","CONFIG_PUBLISH_FORBIDDEN"}}};
  std::string reason; if (const auto* value=request.if_contains("reason"); value && value->is_string()) reason=std::string(value->as_string());
  if (reason.empty()) return {422,{{"error","reason is required"},{"code","CONFIG_REASON_REQUIRED"}}};
  try { auto result=configService_->rollback(revision,actor.principal.id,reason); const bool ok=result.if_contains("ok")&&result.at("ok").as_bool(); if(!ok)return {422,std::move(result)}; const auto newRevision=std::string(result.at("revision").as_string()); appendAudit(actor,"CONFIG_ROLLBACK","CONFIG",newRevision,"SUCCESS","configuration rollback published",reason); emitRealtimeEvent("config.published",result); return {200,std::move(result)}; }
  catch(const std::exception& e){ appendAudit(actor,"CONFIG_ROLLBACK","CONFIG",revision,"FAILED",e.what(),reason); return {404,{{"error",e.what()},{"code","CONFIG_REVISION_NOT_FOUND"}}}; }
}

boost::json::object ApplicationService::telemetryHistory(const TelemetryHistoryQuery& query) const {
  if (!historian_) return {{"enabled", false}, {"items", boost::json::array{}}, {"totalMatched", 0}, {"truncated", false}};
  auto result = historian_->queryTelemetry(query);
  result["enabled"] = true;
  return result;
}

boost::json::object ApplicationService::alarmHistory(const AlarmHistoryQuery& query) const {
  if (!historian_) return {{"enabled", false}, {"items", boost::json::array{}}, {"totalMatched", 0}, {"truncated", false}};
  auto result = historian_->queryAlarmEvents(query);
  result["enabled"] = true;
  return result;
}

bool ApplicationService::authEnabled() const { return auth_ && auth_->enabled(); }
std::optional<SecurityPrincipal> ApplicationService::authenticate(const std::string& userId, const std::string& token) const {
  return auth_ ? auth_->authenticate(userId, token) : std::optional<SecurityPrincipal>{SecurityPrincipal{"anonymous","Anonymous",UserRole::Administrator,false}};
}
std::optional<SecurityPrincipal> ApplicationService::authenticateToken(const std::string& token) const {
  return auth_ ? auth_->authenticateToken(token) : std::optional<SecurityPrincipal>{SecurityPrincipal{"anonymous","Anonymous",UserRole::Administrator,false}};
}
bool ApplicationService::authorize(const SecurityPrincipal& principal, Permission permission) const {
  return auth_ ? auth_->authorize(principal, permission) : true;
}
boost::json::object ApplicationService::principalJson(const SecurityPrincipal& principal) const {
  return auth_ ? auth_->principalJson(principal) : boost::json::object{{"id","anonymous"},{"displayName","Anonymous"},{"role","ADMINISTRATOR"},{"authenticated",false},{"permissions",boost::json::array{"view","alarm.ack","command.issue","command.emergency","audit.view","runtime.inspect","config.edit","config.publish","rule.manage","incident.assign","incident.resolve","incident.close","report.export"}}};
}
boost::json::object ApplicationService::auditHistory(const AuditQuery& query, const SecurityContext& actor) const {
  if (!audit_) return {{"enabled",false},{"items",boost::json::array{}},{"totalMatched",0},{"truncated",false}};
  if (!authorize(actor.principal, Permission::AuditView)) return {{"enabled",false},{"error","forbidden"},{"items",boost::json::array{}},{"totalMatched",0},{"truncated",false}};
  auto result = audit_->query(query); result["enabled"] = true; return result;
}

void ApplicationService::appendAudit(const SecurityContext& actor, const std::string& action,
                                     const std::string& resourceType, const std::string& resourceId,
                                     const std::string& outcome, const std::string& detail,
                                     const std::string& reason) {
  if (!audit_) return;
  const auto timestamp = nowMs();
  boost::json::object event{{"eventId",std::to_string(timestamp)+":"+action+":"+resourceId}, {"timestamp",timestamp},
    {"actorId",actor.principal.id.empty()?"unknown":actor.principal.id}, {"actorDisplayName",actor.principal.displayName},
    {"actorRole",AuthService::roleName(actor.principal.role)}, {"action",action}, {"resourceType",resourceType},
    {"resourceId",resourceId}, {"outcome",outcome}, {"detail",detail}};
  if (!actor.remoteAddress.empty()) event["remoteAddress"] = actor.remoteAddress;
  if (!reason.empty()) event["reason"] = reason;
  audit_->append(std::move(event));
}

void ApplicationService::recordAuthAudit(const std::string& action, const std::string& outcome,
                                         const std::string& actorId, const std::string& detail,
                                         const std::string& remoteAddress,
                                         const std::optional<SecurityPrincipal>& principal) {
  SecurityContext context;
  if (principal) {
    context.principal = *principal;
  } else {
    context.principal.id = actorId.empty() ? "unknown" : actorId;
    context.principal.displayName = actorId.empty() ? "Unknown" : actorId;
    context.principal.role = UserRole::Observer;
    context.principal.authenticated = false;
  }
  context.remoteAddress = remoteAddress;
  appendAudit(context, action, "AUTH", actorId.empty()?"session":actorId, outcome, detail);
}

std::string ApplicationService::jsonStringOr(const boost::json::object& obj, const char* key, const std::string& fallback) {
  if (const auto* v = obj.if_contains(key); v && v->is_string()) return std::string(v->as_string());
  return fallback;
}

std::optional<std::int64_t> ApplicationService::jsonInt(const boost::json::object& obj, const char* key) {
  const auto* v = obj.if_contains(key);
  if (!v) return std::nullopt;
  if (v->is_int64()) return v->as_int64();
  if (v->is_uint64() && v->as_uint64() <= static_cast<std::uint64_t>(INT64_MAX)) return static_cast<std::int64_t>(v->as_uint64());
  return std::nullopt;
}

std::string ApplicationService::fingerprintCommand(const boost::json::object& request) {
  boost::json::object copy = request;
  copy.erase("commandId");
  copy.erase("operator");
  copy.erase("operatorRole");
  copy.erase("remoteAddress");
  copy.erase("commandDisplayName");
  copy.erase("executionClass");
  copy.erase("requiredPermission");
  return boost::json::serialize(copy);
}

std::string ApplicationService::commandDedupHash(const boost::json::object& request, const SecurityContext& actor) {
  boost::json::object canonical;
  canonical["actorId"] = actor.principal.id;
  canonical["deviceId"] = jsonStringOr(request, "deviceId");
  canonical["action"] = jsonStringOr(request, "action");
  canonical["reason"] = jsonStringOr(request, "reason");
  if (const auto* value = request.if_contains("requestedValue")) canonical["requestedValue"] = *value;
  else canonical["requestedValue"] = nullptr;
  return boost::json::serialize(canonical);
}

std::optional<Permission> ApplicationService::commandPermission(const CommandDefinition& definition) {
  if (definition.requiredPermission == "command.issue") return Permission::CommandIssue;
  if (definition.requiredPermission == "command.emergency") return Permission::CommandEmergency;
  return std::nullopt;
}

bool ApplicationService::validateRequestedValue(const boost::json::object& request,
                                                const CommandDefinition& definition,
                                                std::string& error) {
  const auto& schema = definition.parameterSchema;
  const auto type = jsonStringOr(schema, "type");
  const bool required = schema.if_contains("required") && schema.at("required").is_bool() && schema.at("required").as_bool();
  const auto* value = request.if_contains("requestedValue");

  if (!value || value->is_null()) {
    if (required) { error = "requestedValue is required"; return false; }
    return true;
  }
  if (type == "none") { error = "requestedValue is not allowed for this command"; return false; }

  auto asNumber = [](const boost::json::value& v, double& out) {
    if (v.is_double()) out = v.as_double();
    else if (v.is_int64()) out = static_cast<double>(v.as_int64());
    else if (v.is_uint64()) out = static_cast<double>(v.as_uint64());
    else return false;
    return std::isfinite(out);
  };

  if (type == "number") {
    double number = 0.0;
    if (!asNumber(*value, number)) { error = "requestedValue must be a finite number"; return false; }
    if (const auto* min = schema.if_contains("minimum")) { double bound = 0.0; asNumber(*min, bound); if (number < bound) { error = "requestedValue is below minimum"; return false; } }
    if (const auto* max = schema.if_contains("maximum")) { double bound = 0.0; asNumber(*max, bound); if (number > bound) { error = "requestedValue exceeds maximum"; return false; } }
  } else if (type == "integer") {
    if (!(value->is_int64() || value->is_uint64())) { error = "requestedValue must be an integer"; return false; }
    double number = 0.0; asNumber(*value, number);
    if (const auto* min = schema.if_contains("minimum")) { double bound = 0.0; asNumber(*min, bound); if (number < bound) { error = "requestedValue is below minimum"; return false; } }
    if (const auto* max = schema.if_contains("maximum")) { double bound = 0.0; asNumber(*max, bound); if (number > bound) { error = "requestedValue exceeds maximum"; return false; } }
  } else if (type == "boolean") {
    if (!value->is_bool()) { error = "requestedValue must be boolean"; return false; }
  } else if (type == "string") {
    if (!value->is_string()) { error = "requestedValue must be string"; return false; }
    const auto length = value->as_string().size();
    if (const auto min = jsonInt(schema, "minLength"); min && length < static_cast<std::size_t>(*min)) { error = "requestedValue is shorter than minLength"; return false; }
    if (const auto max = jsonInt(schema, "maxLength"); max && length > static_cast<std::size_t>(*max)) { error = "requestedValue exceeds maxLength"; return false; }
  } else if (type == "object") {
    if (!value->is_object()) { error = "requestedValue must be object"; return false; }
  } else {
    error = "unsupported parameter schema type";
    return false;
  }

  if (const auto* allowed = schema.if_contains("enum"); allowed && allowed->is_array()) {
    bool matched = false;
    for (const auto& candidate : allowed->as_array()) {
      if (candidate == *value) { matched = true; break; }
    }
    if (!matched) { error = "requestedValue is not in the allowed enum"; return false; }
  }
  return true;
}

bool ApplicationService::alarmEquivalent(const boost::json::object& a, const boost::json::object& b) {
  return boost::json::serialize(a) == boost::json::serialize(b);
}

std::string ApplicationService::makeOccurrenceId(const std::string& alarmId, std::int64_t raisedAt) {
  static std::atomic<std::uint64_t> sequence{0};
  const auto serial = sequence.fetch_add(1, std::memory_order_relaxed);
  const auto digest = std::hash<std::string>{}(alarmId + ":" + std::to_string(raisedAt));
  return "occ-" + std::to_string(raisedAt) + "-" + std::to_string(digest) + "-" + std::to_string(serial);
}

bool ApplicationService::terminalCommandState(const std::string& state) {
  return state == "CONFIRMED" || state == "FAILED" || state == "REJECTED" || state == "EXPIRED" ||
         state == "OUTCOME_UNKNOWN";
}

void ApplicationService::persistCommand(const boost::json::object& command) {
  if (commandLedger_) commandLedger_->update(command);
}

void ApplicationService::pruneCommandCacheLocked() {
  const auto target = std::max<std::size_t>(config_.commandDashboardLimit * 2, 500);
  if (commands_.size() <= target) return;
  std::vector<std::pair<std::int64_t, std::string>> terminal;
  terminal.reserve(commands_.size());
  for (const auto& [id, stored] : commands_) {
    const auto state = jsonStringOr(stored.command, "state");
    if (terminalCommandState(state) && state != "OUTCOME_UNKNOWN") {
      terminal.emplace_back(jsonInt(stored.command, "updatedAt").value_or(0), id);
    }
  }
  std::sort(terminal.begin(), terminal.end());
  for (const auto& [_, id] : terminal) {
    if (commands_.size() <= target) break;
    commands_.erase(id);
  }
}

void ApplicationService::restoreCommandLedger() {
  if (!commandLedger_) return;
  {
    std::lock_guard lock(commandsMutex_);
    for (auto& persisted : commandLedger_->recent(std::max<std::size_t>(config_.commandDashboardLimit * 2, 500))) {
      commands_[persisted.commandId] = StoredCommand{std::move(persisted.command), std::move(persisted.fingerprint)};
    }
  }

  // Never replay a non-terminal physical command after a process restart. The
  // previous process may have reached the device even if the durable state was
  // only ISSUED/EXECUTING/APPLIED. Preserve idempotency and surface uncertainty.
  for (auto& persisted : commandLedger_->nonTerminal()) {
    auto command = std::move(persisted.command);
    const auto previousState = jsonStringOr(command, "state", "UNKNOWN");
    const auto timestamp = nowMs();
    command["state"] = "OUTCOME_UNKNOWN";
    command["updatedAt"] = timestamp;
    command["recoveredAt"] = timestamp;
    command["recoveredFromState"] = previousState;
    command["feedbackStatus"] = "UNKNOWN";
    command["detail"] = "BACKEND_RESTART: command outcome cannot be proven; automatic physical replay is forbidden";
    command["lastEventSeq"] = jsonInt(command, "lastEventSeq").value_or(0) + 1;
    command["revision"] = jsonInt(command, "revision").value_or(1) + 1;
    commandLedger_->update(command);
    std::lock_guard lock(commandsMutex_);
    commands_[persisted.commandId] = StoredCommand{std::move(command), std::move(persisted.fingerprint)};
  }
  std::lock_guard lock(commandsMutex_);
  pruneCommandCacheLocked();
}

boost::json::object ApplicationService::initialCommand(const boost::json::object& request,
                                                       const std::string& commandId,
                                                       std::int64_t now) const {
  const auto ttl = jsonInt(request, "ttlMs").value_or(30000);
  boost::json::object command{{"id", commandId}, {"deviceId", jsonStringOr(request, "deviceId")},
                              {"action", jsonStringOr(request, "action")}, {"state", "ISSUED"},
                              {"revision", 1},
                              {"issuedAt", now}, {"updatedAt", now}, {"expiresAt", now + ttl}};
  if (const auto* v = request.if_contains("requestedValue")) command["requestedValue"] = *v;
  const auto op = jsonStringOr(request, "operator");
  const auto opRole = jsonStringOr(request, "operatorRole");
  const auto reason = jsonStringOr(request, "reason");
  copyIfPresent(request, command, "commandDisplayName");
  copyIfPresent(request, command, "executionClass");
  copyIfPresent(request, command, "requiredPermission");
  copyIfPresent(request, command, "previewId");
  copyIfPresent(request, command, "idempotencyKey");
  copyIfPresent(request, command, "configRevision");
  copyIfPresent(request, command, "idempotencyExpiresAt");
  if (!op.empty()) command["operator"] = op;
  if (!opRole.empty()) command["operatorRole"] = opRole;
  if (!reason.empty()) command["reason"] = reason;
  return command;
}

ApplicationService::CommandIssueResult ApplicationService::issueCommand(const boost::json::object& request,
                                                                          const SecurityContext& actor) {
  return issueCommandInternal(request, actor);
}

ApplicationService::CommandIssueResult ApplicationService::issueCommandInternal(const boost::json::object& request,
                                                                                 const SecurityContext& actor,
                                                                                 const std::string& idempotencyKey,
                                                                                 const std::string& requestHash,
                                                                                 std::int64_t dedupExpiresAt) {
  const auto commandId = jsonStringOr(request, "commandId");
  const auto deviceId = jsonStringOr(request, "deviceId");
  const auto action = jsonStringOr(request, "action");
  const auto reason = jsonStringOr(request, "reason");

  if (!authorize(actor.principal, Permission::CommandIssue)) {
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "DENIED", "role is not allowed to issue commands", reason);
    return {403, {{"error", "forbidden: command.issue permission required"}, {"code", "COMMAND_PERMISSION_REQUIRED"}}};
  }
  if (commandId.empty() || deviceId.empty() || action.empty()) {
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "REJECTED", "missing commandId/deviceId/action", reason);
    return {400, {{"error", "commandId, deviceId and action are required"}, {"code", "INVALID_COMMAND_REQUEST"}}};
  }
  if (!deviceRegistry_) {
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "REJECTED", "command registry unavailable; default deny", reason);
    return {503, {{"error", "command registry unavailable"}, {"code", "COMMAND_REGISTRY_UNAVAILABLE"}}};
  }
  const auto* deviceDefinition = deviceRegistry_->find(deviceId);
  if (!deviceDefinition) {
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "REJECTED", "device is not registered", reason);
    return {404, {{"error", "device not found in registry"}, {"code", "DEVICE_NOT_REGISTERED"}}};
  }
  const auto* commandDefinition = deviceRegistry_->findCommand(deviceId, action);
  if (!commandDefinition) {
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "REJECTED", "command is not registered for device", reason);
    return {404, {{"error", "command is not supported by device"}, {"code", "COMMAND_NOT_REGISTERED"}}};
  }
  const auto requiredPermission = commandPermission(*commandDefinition);
  if (!requiredPermission || !authorize(actor.principal, *requiredPermission)) {
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "DENIED", "required command permission was not granted", reason);
    return {403, {{"error", "forbidden: required command permission not granted"}, {"code", "COMMAND_PERMISSION_REQUIRED"},
                  {"requiredPermission", commandDefinition->requiredPermission}}};
  }
  if (commandDefinition->reasonRequired && reason.empty()) {
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "REJECTED", "command requires reason", reason);
    return {400, {{"error", "reason is required for this command"}, {"code", "COMMAND_REASON_REQUIRED"}}};
  }
  std::string parameterError;
  if (!validateRequestedValue(request, *commandDefinition, parameterError)) {
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "REJECTED", "invalid requestedValue: " + parameterError, reason);
    return {400, {{"error", parameterError}, {"code", "COMMAND_PARAMETER_INVALID"}}};
  }
  if (const auto ttl = jsonInt(request, "ttlMs"); ttl && (*ttl <= 0 || *ttl > 24LL * 60 * 60 * 1000)) {
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "REJECTED", "invalid ttlMs", reason);
    return {400, {{"error", "ttlMs must be within (0, 86400000]"}, {"code", "COMMAND_TTL_INVALID"}}};
  }
  if (commandDefinition->executionClass == CommandExecutionClass::Emergency && !config_.enableEmergencyControl) {
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "REJECTED", "emergency control is disabled by backend configuration", reason);
    return {409, {{"error", "emergency control is disabled"}, {"code", "EMERGENCY_CONTROL_DISABLED"}}};
  }
  if (commandDefinition->executionClass == CommandExecutionClass::Realtime && !realtime_) {
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "REJECTED", "realtime domain is disabled", reason);
    return {503, {{"error", "realtime domain is disabled"}, {"code", "REALTIME_DOMAIN_DISABLED"}}};
  }
  if (commandDefinition->executionClass == CommandExecutionClass::Emergency && !emergency_) {
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "REJECTED", "emergency realtime domain is unavailable", reason);
    return {503, {{"error", "emergency realtime domain is unavailable"}, {"code", "EMERGENCY_DOMAIN_UNAVAILABLE"}}};
  }
  if (commandService_ && !commandService_->adapterWriteReady()) {
    const auto detail = commandService_->adapterWriteReadinessDetail();
    appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "REJECTED", detail, reason);
    return {409, {{"error", detail}, {"code", "DRIVER_ISOLATION_REQUIRED"}}};
  }
  if (!idempotencyKey.empty() && commandLedger_) {
    if (const auto blocking = commandLedger_->blockingUnknownFor(deviceId, action)) {
      appendAudit(actor,"COMMAND_REQUEST","COMMAND",commandId,"REJECTED","unresolved prior command blocks duplicate physical action",reason);
      return {409, {{"error","an unresolved prior command blocks the same device action until reconciliation explicitly allows retry"},
                    {"code","COMMAND_UNRESOLVED_BLOCK"},{"blockingCommandId",*blocking}}};
    }
  }

  boost::json::object canonical = request;
  canonical["operator"] = actor.principal.id;
  canonical["operatorRole"] = AuthService::roleName(actor.principal.role);
  canonical["commandDisplayName"] = commandDefinition->displayName;
  canonical["executionClass"] = DeviceRegistry::executionClassName(commandDefinition->executionClass);
  canonical["requiredPermission"] = commandDefinition->requiredPermission;
  if (!actor.remoteAddress.empty()) canonical["remoteAddress"] = actor.remoteAddress;
  const auto fp = fingerprintCommand(canonical);

  boost::json::object initial;
  {
    std::lock_guard lock(commandsMutex_);
    if (const auto it = commands_.find(commandId); it != commands_.end()) {
      if (it->second.fingerprint != fp) {
        appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "CONFLICT", "commandId already exists with different payload", reason);
        return {409, {{"error", "commandId already exists with different payload"}, {"code", "COMMAND_ID_CONFLICT"}}};
      }
      appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "IDEMPOTENT_REPLAY", "existing command returned without physical replay", reason);
      return {200, it->second.command};
    }

    if (commandLedger_) {
      if (auto persisted = commandLedger_->find(commandId)) {
        commands_[commandId] = StoredCommand{persisted->command, persisted->fingerprint};
        pruneCommandCacheLocked();
        if (persisted->fingerprint != fp) {
          appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "CONFLICT", "persistent commandId exists with different payload", reason);
          return {409, {{"error", "commandId already exists with different payload"}, {"code", "COMMAND_ID_CONFLICT"}}};
        }
        appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "IDEMPOTENT_REPLAY", "persistent command returned without physical replay", reason);
        return {200, persisted->command};
      }
    }

    initial = initialCommand(canonical, commandId, nowMs());
    if (commandLedger_ && !idempotencyKey.empty()) {
      const auto outcome = commandLedger_->insertWithDedup(commandId, fp, initial, idempotencyKey, requestHash, dedupExpiresAt);
      if (outcome == CommandInsertOutcome::DedupConflict) {
        appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "CONFLICT", "idempotencyKey already exists with different canonical request", reason);
        return {409, {{"error", "idempotencyKey already exists with different request"}, {"code", "COMMAND_IDEMPOTENCY_CONFLICT"}}};
      }
      if (outcome == CommandInsertOutcome::CapacityExhausted) {
        appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "REJECTED", "protected command ledger capacity exhausted", reason);
        return {507, {{"error", "command ledger capacity is exhausted by protected/unresolved records"}, {"code", "COMMAND_LEDGER_CAPACITY_EXHAUSTED"}}};
      }
      if (outcome == CommandInsertOutcome::Duplicate) {
        const auto dedup = commandLedger_->findDedup(idempotencyKey, nowMs());
        if (!dedup) throw std::runtime_error("dedup row disappeared after duplicate detection");
        const auto persisted = commandLedger_->find(dedup->commandId);
        if (!persisted) throw std::runtime_error("dedup row references missing command");
        commands_[persisted->commandId] = StoredCommand{persisted->command, persisted->fingerprint};
        appendAudit(actor, "COMMAND_REQUEST", "COMMAND", persisted->commandId, "IDEMPOTENT_REPLAY", "idempotencyKey returned existing command without physical replay", reason);
        return {200, persisted->command};
      }
    } else if (commandLedger_ && !commandLedger_->insert(commandId, fp, initial)) {
      auto persisted = commandLedger_->find(commandId);
      if (!persisted) throw std::runtime_error("command ledger rejected insert but row could not be recovered");
      commands_[commandId] = StoredCommand{persisted->command, persisted->fingerprint};
      if (persisted->fingerprint != fp) {
        return {409, {{"error", "commandId already exists with different payload"}, {"code", "COMMAND_ID_CONFLICT"}}};
      }
      return {200, persisted->command};
    }
    commands_.emplace(commandId, StoredCommand{initial, fp});
    pruneCommandCacheLocked();
  }

  appendAudit(actor, "COMMAND_REQUEST", "COMMAND", commandId, "ACCEPTED", "registered command durably accepted for execution", reason);
  const auto executionClass = commandDefinition->executionClass;
  auto task = [this, commandId, executionClass] { runCommandLifecycle(commandId, executionClass); };
  bool submitted = false;
  std::string queueName;
  if (executionClass == CommandExecutionClass::Emergency) {
    queueName = "emergency";
    submitted = emergency_ && emergency_->submit(std::move(task));
  } else if (executionClass == CommandExecutionClass::Realtime) {
    queueName = "realtime";
    submitted = realtime_ && realtime_->submit(std::move(task));
  } else {
    queueName = "non-realtime";
    submitted = nonRealtime_.submit(std::move(task));
  }

  if (!submitted) {
    transition(commandId, "REJECTED", 1, "QUEUE_CAPACITY_EXCEEDED: " + queueName + " executor refused admission");
    std::lock_guard lock(commandsMutex_);
    auto body = commands_.at(commandId).command;
    body["code"] = "COMMAND_QUEUE_FULL";
    return {503, std::move(body)};
  }

  std::lock_guard lock(commandsMutex_);
  return {202, commands_.at(commandId).command};
}

void ApplicationService::runCommandLifecycle(const std::string& commandId, CommandExecutionClass executionClass) {
  const bool realtimePath = executionClass != CommandExecutionClass::Normal;
  const bool emergencyPath = executionClass == CommandExecutionClass::Emergency;
  const std::string domain = emergencyPath ? "emergency" : (realtimePath ? "realtime" : "non-realtime");
  transition(commandId, "RECEIVED", 1, "received by " + domain + " queue");

  boost::json::object snapshot;
  {
    std::lock_guard lock(commandsMutex_);
    snapshot = commands_.at(commandId).command;
  }
  const auto action = jsonStringOr(snapshot, "action");
  const auto expiresAt = jsonInt(snapshot, "expiresAt").value_or(nowMs());
  if (nowMs() > expiresAt) { transition(commandId, "EXPIRED", 2, "command deadline expired before execution"); return; }
  if (emergencyPath && (!emergency_ || !config_.enableEmergencyControl)) { transition(commandId, "REJECTED", 2, "emergency control domain is unavailable"); return; }
  if (realtimePath && config_.requireRealtime) {
    const bool granted = emergencyPath ? (emergency_ && emergency_->realtimeGranted()) : (realtime_ && realtime_->realtimeGranted());
    if (!granted) { transition(commandId, "REJECTED", 2, "required realtime scheduling was not granted"); return; }
  }

  transition(commandId, "ACCEPTED", 2, "accepted by " + domain + " service");
  transition(commandId, "EXECUTING", 3, "executing in " + domain + " domain");

  DeviceCommand deviceCommand;
  deviceCommand.commandId = commandId;
  deviceCommand.deviceId = jsonStringOr(snapshot, "deviceId");
  deviceCommand.action = action;
  if (const auto* v = snapshot.if_contains("requestedValue")) deviceCommand.requestedValueJson = serializeValue(*v);

  const auto executionStartedAt = nowMs();
  const auto steadyStart = std::chrono::steady_clock::now();
  const auto executionDeadline = std::min<std::int64_t>(expiresAt, executionStartedAt + config_.commandExecutionTimeoutMs);
  const auto delivery = commandService_->execute(deviceCommand, executionDeadline);
  const auto executionFinishedAt = nowMs();
  const auto executionDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - steadyStart).count();
  boost::json::object timingSnapshot;
  {
    std::lock_guard lock(commandsMutex_);
    auto& command = commands_.at(commandId).command;
    command["executionStartedAt"] = executionStartedAt;
    command["executionFinishedAt"] = executionFinishedAt;
    command["executionDurationMs"] = executionDurationMs;
    command["deliveryCertainty"] = CommandService::certaintyName(delivery.certainty);
    command["executionSupervision"] = delivery.supervisionMode;
    command["adapterQuarantined"] = delivery.adapterQuarantined;
    timingSnapshot = command;
  }
  persistCommand(timingSnapshot);

  if (delivery.certainty == DeviceCommandDeliveryCertainty::NotSent ||
      delivery.certainty == DeviceCommandDeliveryCertainty::Rejected) {
    transition(commandId, "FAILED", 4, CommandService::certaintyName(delivery.certainty) + ": " + delivery.detail);
    return;
  }
  if (delivery.certainty == DeviceCommandDeliveryCertainty::PossiblyApplied) {
    transition(commandId, "OUTCOME_UNKNOWN", 4, "POSSIBLY_APPLIED: " + delivery.detail);
    return;
  }
  transition(commandId, "APPLIED", 4, delivery.detail + "; physical confirmation pending");

  // Feedback observation never occupies realtime/emergency workers. A full
  // feedback queue is uncertainty, not a proven physical failure.
  const bool feedbackSubmitted = feedbackExecutor_.submit(
      [this, commandId, deviceCommand = std::move(deviceCommand), expiresAt]() mutable {
        monitorCommandFeedback(commandId, std::move(deviceCommand), expiresAt);
      });
  if (!feedbackSubmitted) {
    transition(commandId, "OUTCOME_UNKNOWN", 5,
               "FEEDBACK_QUEUE_FULL: command was applied but field confirmation could not be scheduled");
  }
}

void ApplicationService::monitorCommandFeedback(const std::string& commandId, DeviceCommand deviceCommand,
                                                std::int64_t expiresAt) {
  const auto feedbackDeadline = std::min<std::int64_t>(expiresAt, nowMs() + config_.commandFeedbackTimeoutMs);
  while (nowMs() <= feedbackDeadline) {
    DeviceCommandFeedback feedback;
    try {
      feedback = deviceAdapter_->readCommandFeedback(deviceCommand);
    } catch (const std::exception& e) {
      transition(commandId, "OUTCOME_UNKNOWN", 5, std::string("FEEDBACK_EXCEPTION: ") + e.what());
      return;
    } catch (...) {
      transition(commandId, "OUTCOME_UNKNOWN", 5, "FEEDBACK_EXCEPTION: unknown exception");
      return;
    }
    if (feedback.state == DeviceFeedbackState::Confirmed) {
      transition(commandId, "CONFIRMED", 5, "FIELD_CONFIRMED: " + feedback.detail);
      return;
    }
    if (feedback.state == DeviceFeedbackState::Failed) {
      transition(commandId, "FAILED", 5, "FIELD_FAILED: " + feedback.detail);
      return;
    }
    if (feedback.state == DeviceFeedbackState::Unsupported) {
      transition(commandId, "OUTCOME_UNKNOWN", 5, "FEEDBACK_UNSUPPORTED: " + feedback.detail);
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(config_.commandFeedbackPollMs));
  }
  transition(commandId, "OUTCOME_UNKNOWN", 5, "FEEDBACK_TIMEOUT: command was applied but field/device confirmation was not observed before deadline");
}

void ApplicationService::transition(const std::string& commandId,
                                    const std::string& state,
                                    int seq,
                                    const std::string& detail) {
  boost::json::object snapshot;
  bool lateObservation = false;
  std::string previousState;
  {
    std::lock_guard lock(commandsMutex_);
    auto& command = commands_.at(commandId).command;
    previousState = jsonStringOr(command, "state");
    if (terminalCommandState(previousState) && previousState != state) {
      lateObservation = true;
    } else {
      const auto timestamp = nowMs();
      command["state"] = state;
      command["updatedAt"] = timestamp;
      command["revision"] = jsonInt(command, "revision").value_or(1) + 1;
      command["lastEventSeq"] = seq;
      command["detail"] = detail;
      if (state == "APPLIED") command["feedbackStatus"] = "PENDING";
      else if (state == "CONFIRMED") { command["feedbackStatus"] = "CONFIRMED"; command["feedbackAt"] = timestamp; }
      else if (state == "OUTCOME_UNKNOWN") { command["feedbackStatus"] = "UNKNOWN"; command["feedbackAt"] = timestamp; }
      else if (state == "FAILED" && previousState == "APPLIED") {
        command["feedbackStatus"] = "FAILED";
        command["feedbackAt"] = timestamp;
      } else if (state == "EXPIRED") {
        command["feedbackStatus"] = "NOT_APPLIED";
      }
      snapshot = command;
    }
  }
  if (lateObservation) {
    appendCommandEvidence(commandId, "LATE_STATE_OBSERVATION",
                          "ignored state transition " + previousState + " -> " + state + ": " + detail,
                          boost::json::object{{"observedState", state}, {"seq", seq}});
    return;
  }
  persistCommand(snapshot);
  emitCommandEvent(snapshot, seq, detail);
  if (terminalCommandState(state)) {
    SecurityContext actor;
    actor.principal.id = jsonStringOr(snapshot, "operator", "unknown");
    actor.principal.displayName = actor.principal.id;
    actor.principal.role = AuthService::parseRole(jsonStringOr(snapshot, "operatorRole", "OBSERVER")).value_or(UserRole::Observer);
    actor.principal.authenticated = true;
    actor.remoteAddress = jsonStringOr(snapshot, "remoteAddress");
    appendAudit(actor, "COMMAND_RESULT", "COMMAND", jsonStringOr(snapshot, "id"), state, detail, jsonStringOr(snapshot, "reason"));
    std::lock_guard lock(commandsMutex_);
    pruneCommandCacheLocked();
  }
}

void ApplicationService::appendCommandEvidence(const std::string& commandId,
                                               const std::string& evidenceType,
                                               const std::string& detail,
                                               const boost::json::object& extra) {
  boost::json::object snapshot;
  boost::json::object evidence{{"type", evidenceType}, {"detail", detail}, {"timestamp", nowMs()}};
  for (const auto& item : extra) evidence[item.key()] = item.value();
  {
    std::lock_guard lock(commandsMutex_);
    auto it = commands_.find(commandId);
    if (it == commands_.end()) return;
    auto& command = it->second.command;
    boost::json::array observations;
    if (const auto* existing = command.if_contains("observations"); existing && existing->is_array()) observations = existing->as_array();
    observations.emplace_back(evidence);
    command["observations"] = std::move(observations);
    command["revision"] = jsonInt(command, "revision").value_or(1) + 1;
    command["updatedAt"] = nowMs();
    snapshot = command;
  }
  persistCommand(snapshot);
  emitRealtimeEvent("command.evidence", boost::json::object{{"commandId", commandId}, {"evidence", evidence}, {"revision", snapshot.at("revision")}});
}

void ApplicationService::emitCommandEvent(const boost::json::object& command, int seq, const std::string& detail) {
  const auto timestamp = nowMs();
  boost::json::object eventData{{"commandId", jsonStringOr(command, "id")}, {"state", jsonStringOr(command, "state")},
                                {"timestamp", timestamp}, {"seq", seq}, {"detail", detail}};
  copyIfPresent(command, eventData, "revision");
  copyIfPresent(command, eventData, "feedbackStatus");
  copyIfPresent(command, eventData, "feedbackAt");
  copyIfPresent(command, eventData, "deliveryCertainty");
  copyIfPresent(command, eventData, "executionSupervision");
  copyIfPresent(command, eventData, "adapterQuarantined");
  emitRealtimeEvent("command.status", eventData, seq);
}

void ApplicationService::emitRealtimeEvent(const std::string& type, const boost::json::value& data,
                                           std::optional<std::int64_t> seq) {
  if (!eventSink_) return;
  const auto timestamp = nowMs();
  boost::json::object envelope{{"type", type}, {"data", data}, {"timestamp", timestamp}};
  if (seq) envelope["seq"] = *seq;
  eventSink_(boost::json::serialize(envelope));
}

void ApplicationService::recordAlarmEvent(const std::string& eventType, const boost::json::object& alarm,
                                          const std::string& source, const std::string& operatorId,
                                          const std::string& comment) {
  if (!historian_) return;
  const auto timestamp = nowMs();
  const auto alarmId = jsonStringOr(alarm, "id");
  const auto occurrenceId = jsonStringOr(alarm, "occurrenceId", "legacy:" + alarmId + ":" + std::to_string(jsonInt(alarm,"raisedAt").value_or(timestamp)));
  boost::json::object event{{"eventId", occurrenceId + ":" + eventType + ":" + std::to_string(timestamp)},
                            {"occurrenceId", occurrenceId}, {"alarmId", alarmId}, {"deviceId", jsonStringOr(alarm, "deviceId")},
                            {"eventType", eventType}, {"timestamp", timestamp}, {"source", source},
                            {"alarm", alarm}};
  if (!operatorId.empty()) event["operator"] = operatorId;
  if (!comment.empty()) event["comment"] = comment;
  historian_->appendAlarmEvent(event);
}

boost::json::array ApplicationService::currentAlarmArrayLocked() const {
  std::vector<boost::json::object> ordered;
  ordered.reserve(alarmStates_.size());
  for (const auto& [_, alarm] : alarmStates_) ordered.push_back(alarm);
  std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
    return ApplicationService::jsonInt(a, "raisedAt").value_or(0) > ApplicationService::jsonInt(b, "raisedAt").value_or(0);
  });
  boost::json::array out;
  for (auto& alarm : ordered) out.emplace_back(std::move(alarm));
  return out;
}

boost::json::array ApplicationService::reconcileAlarms(const boost::json::array& rawAlarms,
                                                       std::int64_t snapshotTs,
                                                       bool persistAndEmit,
                                                       const std::set<std::string>& completeDeviceIds) {
  std::lock_guard lock(alarmMutex_);
  std::set<std::string> seen;

  for (const auto& value : rawAlarms) {
    if (!value.is_object()) continue;
    auto raw = value.as_object();
    const auto id = jsonStringOr(raw, "id");
    if (id.empty()) continue;
    seen.insert(id);
    raw["sourceDomain"] = "FIELD";
    const auto rawState = jsonStringOr(raw, "state", "ACTIVE");
    const auto rawRaisedAt = jsonInt(raw, "raisedAt").value_or(snapshotTs);
    raw["raisedAt"] = rawRaisedAt;

    auto it = alarmStates_.find(id);
    bool newOccurrence = false;
    bool changed = false;
    boost::json::object next = raw;

    if (it == alarmStates_.end()) {
      next["occurrenceId"] = makeOccurrenceId(id, rawRaisedAt);
      changed = true;
      newOccurrence = rawState != "CLEARED";
      if (rawState == "CLEARED" && !next.if_contains("clearedAt")) next["clearedAt"] = snapshotTs;
    } else {
      const auto previous = it->second;
      const auto previousState = jsonStringOr(previous, "state");
      const auto previousRaisedAt = jsonInt(previous, "raisedAt").value_or(0);
      const auto previousClearedAt = jsonInt(previous, "clearedAt").value_or(0);
      const auto previousOccurrenceId = jsonStringOr(previous, "occurrenceId");
      if (!previousOccurrenceId.empty()) next["occurrenceId"] = previousOccurrenceId;

      if (previousState == "CLEARED" && rawState != "CLEARED" && rawRaisedAt > std::max(previousRaisedAt, previousClearedAt)) {
        next.erase("acknowledgedAt"); next.erase("acknowledgedBy"); next.erase("acknowledgementComment");
        next.erase("clearedAt"); next.erase("clearedBy"); next.erase("clearComment");
        next["state"] = "ACTIVE";
        next["occurrenceId"] = makeOccurrenceId(id, rawRaisedAt);
        newOccurrence = true;
      } else if (previousState == "ACKNOWLEDGED" && rawState != "CLEARED") {
        next["state"] = "ACKNOWLEDGED";
        copyIfPresent(previous, next, "acknowledgedAt");
        copyIfPresent(previous, next, "acknowledgedBy");
        copyIfPresent(previous, next, "acknowledgementComment");
      } else if (rawState == "CLEARED") {
        next["state"] = "CLEARED";
        if (!next.if_contains("clearedAt")) next["clearedAt"] = snapshotTs;
        copyIfPresent(previous, next, "acknowledgedAt");
        copyIfPresent(previous, next, "acknowledgedBy");
        copyIfPresent(previous, next, "acknowledgementComment");
      }
      changed = !alarmEquivalent(previous, next);
    }

    if (changed) {
      const auto previousState = it == alarmStates_.end() ? std::string{} : jsonStringOr(it->second, "state");
      alarmStates_[id] = next;
      if (persistAndEmit) {
        if (newOccurrence) recordAlarmEvent("RAISED", next, "FIELD");
        else if (jsonStringOr(next, "state") == "CLEARED" && previousState != "CLEARED") recordAlarmEvent("CLEARED", next, "FIELD_RETURN_TO_NORMAL");
        emitRealtimeEvent("alarm.updated", next);
      }
    }
  }

  // Alarm absence may imply field return-to-normal only for device domains
  // explicitly complete in this acquisition batch. A failed device never clears
  // its FIELD alarms merely because the adapter could not observe them.
  for (auto& [id, alarm] : alarmStates_) {
    if (seen.contains(id) || jsonStringOr(alarm, "state") == "CLEARED") continue;
    if (jsonStringOr(alarm, "sourceDomain", "FIELD") != "FIELD") continue;
    const auto deviceId = jsonStringOr(alarm, "deviceId");
    if (!completeDeviceIds.contains(deviceId)) continue;
    alarm["state"] = "CLEARED";
    alarm["clearedAt"] = snapshotTs;
    alarm["clearedBy"] = "FIELD";
    if (persistAndEmit) {
      recordAlarmEvent("CLEARED", alarm, "FIELD_RETURN_TO_NORMAL");
      emitRealtimeEvent("alarm.updated", alarm);
    }
  }

  return currentAlarmArrayLocked();
}

void ApplicationService::persistTelemetry(const boost::json::array& telemetry) {
  if (!historian_) return;
  std::lock_guard lock(telemetryCursorMutex_);
  for (const auto& value : telemetry) {
    if (!value.is_object()) continue;
    const auto& point = value.as_object();
    const auto deviceId = jsonStringOr(point, "deviceId");
    const auto pointId = jsonStringOr(point, "pointId");
    const auto key = deviceId + ":" + pointId;
    const auto sampleTs = jsonInt(point, "sampleTs").value_or(0);
    const auto seq = jsonInt(point, "seq").value_or(-1);
    const auto sourceEpoch = jsonStringOr(point, "sourceEpoch");
    auto& cursor = telemetryCursors_[key];
    if (!sourceEpoch.empty() && !cursor.sourceEpoch.empty() && sourceEpoch != cursor.sourceEpoch) {
      cursor = {}; // explicit source restart creates a new sequence domain.
    }
    if (sampleTs < cursor.sampleTs || (sampleTs == cursor.sampleTs && seq <= cursor.seq)) continue;

    // Device Registry samplePeriodMs is an acquisition/persistence contract,
    // not presentation metadata. Even if a snapshot adapter refreshes all
    // points on every poll, slow points are not over-sampled into Historian.
    if (deviceRegistry_) {
      if (const auto* definition = deviceRegistry_->findPoint(deviceId, pointId); definition && cursor.sampleTs > 0) {
        if (sampleTs - cursor.sampleTs < definition->samplePeriodMs) continue;
      }
    }

    historian_->appendTelemetry(point);
    cursor = {sampleTs, seq, sourceEpoch};
  }
}

void ApplicationService::enrichSnapshotCapabilities(DeviceDataSnapshot& snapshot) const {
  if (!deviceRegistry_) return;
  for (auto& value : snapshot.devices) {
    if (!value.is_object()) continue;
    auto& device = value.as_object();
    const auto deviceId = jsonStringOr(device, "id");
    const auto* definition = deviceRegistry_->find(deviceId);
    if (!definition) continue;

    boost::json::array pointDefinitions;
    for (const auto& point : definition->points) {
      boost::json::object exposed{{"pointId",point.pointId},{"label",point.label},{"unit",point.unit},
                                  {"valueType",point.valueType},{"samplePeriodMs",point.samplePeriodMs}};
      if (!point.limits.empty()) exposed["limits"] = point.limits;
      pointDefinitions.emplace_back(std::move(exposed));
    }
    device["pointDefinitions"] = std::move(pointDefinitions);

    boost::json::object commands;
    for (const auto& [actionId, command] : definition->commandDefinitions) {
      if (const auto* developmentOnly = command.metadata.if_contains("developmentOnly"); developmentOnly && developmentOnly->is_bool() && developmentOnly->as_bool()) continue;
      commands[actionId] = boost::json::object{{"actionId",actionId},{"displayName",command.displayName},
        {"requiredPermission",command.requiredPermission},{"executionClass",DeviceRegistry::executionClassName(command.executionClass)},
        {"reasonRequired",command.reasonRequired},{"parameterSchema",command.parameterSchema}};
    }
    device["commandCapabilities"] = std::move(commands);
  }
}

std::int64_t ApplicationService::acquisitionIntervalMs() const {
  if (config_.acquisitionPollMs > 0) return config_.acquisitionPollMs;
  if (deviceRegistry_) return std::max<std::int64_t>(10, deviceRegistry_->minimumSamplePeriodMs());
  return std::max<std::int64_t>(100, config_.telemetryPublishMs);
}

void ApplicationService::updateAcquisitionAlarm(bool unhealthy, const std::string& detail, const std::string& severity) {
  const std::string id = "system-acquisition-health";
  boost::json::object updated;
  bool changed = false;
  std::string eventType;
  {
    std::lock_guard lock(alarmMutex_);
    auto it = alarmStates_.find(id);
    if (unhealthy) {
      if (it == alarmStates_.end() || jsonStringOr(it->second, "state") == "CLEARED") {
        const auto raisedAt = nowMs();
        updated = {{"id",id},{"deviceId","platform"},{"title","数据采集链路异常"},
                   {"message",detail},{"severity",severity},{"state","ACTIVE"},{"raisedAt",raisedAt},
                   {"occurrenceId",makeOccurrenceId(id,raisedAt)},{"sourceDomain","PLATFORM_ACQUISITION"}};
        alarmStates_[id] = updated; changed = true; eventType = "RAISED";
      } else {
        updated = it->second;
        const auto before = boost::json::serialize(updated);
        updated["message"] = detail; updated["severity"] = severity; updated["state"] = jsonStringOr(updated,"state")=="ACKNOWLEDGED"?"ACKNOWLEDGED":"ACTIVE";
        alarmStates_[id] = updated;
        changed = before != boost::json::serialize(updated);
      }
    } else if (it != alarmStates_.end() && jsonStringOr(it->second, "state") != "CLEARED") {
      updated = it->second;
      updated["state"] = "CLEARED"; updated["clearedAt"] = nowMs(); updated["clearedBy"] = "PLATFORM";
      updated["message"] = "数据采集链路已恢复";
      alarmStates_[id] = updated; changed = true; eventType = "CLEARED";
    }
  }
  if (changed && !updated.empty()) {
    if (!eventType.empty()) {
      try { recordAlarmEvent(eventType, updated, eventType=="RAISED"?"PLATFORM_ACQUISITION":"PLATFORM_RECOVERY"); }
      catch (const std::exception& e) { emitRealtimeEvent("platform.storage_fault", boost::json::object{{"detail",e.what()},{"timestamp",nowMs()}}); }
      catch (...) { emitRealtimeEvent("platform.storage_fault", boost::json::object{{"detail","failed to persist acquisition alarm"},{"timestamp",nowMs()}}); }
    }
    emitRealtimeEvent("alarm.updated", updated);
  }
}

void ApplicationService::recordAcquisitionSuccess(std::int64_t snapshotTs) {
  bool recovered = false;
  {
    std::lock_guard lock(acquisitionMutex_);
    recovered = acquisitionState_ != "GOOD" && acquisitionConsecutiveFailures_ > 0;
    acquisitionState_ = "GOOD";
    acquisitionDetail_ = "complete DeviceAdapter snapshot received";
    acquisitionLastSuccessTs_ = snapshotTs > 0 ? snapshotTs : nowMs();
    acquisitionConsecutiveFailures_ = 0;
  }
  if (recovered) updateAcquisitionAlarm(false, "acquisition recovered", "INFO");
}

void ApplicationService::recordAcquisitionFailure(const std::string& detail) {
  std::string state;
  std::uint64_t consecutive = 0;
  {
    std::lock_guard lock(acquisitionMutex_);
    const auto now = nowMs();
    acquisitionLastFailureTs_ = now;
    ++acquisitionConsecutiveFailures_;
    ++acquisitionTotalFailures_;
    consecutive = acquisitionConsecutiveFailures_;
    const auto age = acquisitionLastSuccessTs_ > 0 ? now - acquisitionLastSuccessTs_ : config_.acquisitionOfflineAfterMs;
    if (age >= config_.acquisitionOfflineAfterMs || (acquisitionLastSuccessTs_ == 0 && consecutive >= static_cast<std::uint64_t>(config_.acquisitionFailureAlarmThreshold))) state = "OFFLINE";
    else if (age >= config_.acquisitionStaleAfterMs || consecutive >= static_cast<std::uint64_t>(config_.acquisitionFailureAlarmThreshold)) state = "STALE";
    else state = "DEGRADED";
    acquisitionState_ = state;
    acquisitionDetail_ = detail;
  }

  const bool alarm = state == "STALE" || state == "OFFLINE" || consecutive >= static_cast<std::uint64_t>(config_.acquisitionFailureAlarmThreshold);
  if (alarm) updateAcquisitionAlarm(true, detail, state == "OFFLINE" ? "CRITICAL" : "WARNING");

  auto degraded = degradedSnapshotForAcquisitionFailure(state, detail);
  replaceLatestSnapshot(degraded);
  publishSnapshotEvents(degraded);
}

DeviceDataSnapshot ApplicationService::degradedSnapshotForAcquisitionFailure(const std::string& state, const std::string& detail) {
  auto snapshot = latestSnapshot();
  const auto now = nowMs();
  snapshot.generatedAt = now;
  const auto quality = state == "OFFLINE" ? "BAD" : "STALE";
  for (auto& value : snapshot.telemetry) {
    if (!value.is_object()) continue;
    auto& point = value.as_object(); point["quality"] = quality; point["staleSince"] = now;
  }
  for (auto& value : snapshot.communication) {
    if (!value.is_object()) continue;
    auto& comm = value.as_object(); comm["quality"] = quality; comm["acquisitionState"] = state; comm["acquisitionError"] = detail;
    if (state == "OFFLINE") comm["online"] = false;
  }
  {
    std::lock_guard lock(alarmMutex_);
    snapshot.alarms = currentAlarmArrayLocked();
  }
  return snapshot;
}

void ApplicationService::updateStorageAlarm() {
  if (!historian_) return;
  const auto health = historian_->storageHealth();
  const std::string id = "system-historian-storage";
  boost::json::object updated;
  bool changed = false;
  std::string eventType;
  {
    std::lock_guard lock(alarmMutex_);
    auto it = alarmStates_.find(id);
    if (!health.healthy) {
      const std::string detail = health.detail + "; freeBytes=" + std::to_string(health.freeBytes);
      if (it == alarmStates_.end() || jsonStringOr(it->second,"state") == "CLEARED") {
        const auto raisedAt = nowMs();
        updated = {{"id",id},{"deviceId","platform"},{"title","历史数据存储异常"},{"message",detail},
                   {"severity","CRITICAL"},{"state","ACTIVE"},{"raisedAt",raisedAt},{"occurrenceId",makeOccurrenceId(id,raisedAt)},
                   {"sourceDomain","PLATFORM_STORAGE"}};
        alarmStates_[id] = updated; changed = true; eventType = "RAISED";
      }
    } else if (it != alarmStates_.end() && jsonStringOr(it->second,"state") != "CLEARED") {
      updated = it->second; updated["state"]="CLEARED"; updated["clearedAt"]=nowMs(); updated["clearedBy"]="PLATFORM";
      updated["message"]="历史数据存储空间已恢复"; alarmStates_[id]=updated; changed=true; eventType="CLEARED";
    }
  }
  if (changed) {
    try { recordAlarmEvent(eventType,updated,eventType=="RAISED"?"PLATFORM_STORAGE":"PLATFORM_RECOVERY"); }
    catch (...) { /* storage alarm remains authoritative in memory even if its own store is degraded */ }
    emitRealtimeEvent("alarm.updated",updated);
  }
}

void ApplicationService::validateCompleteSnapshotOrThrow(const DeviceDataSnapshot& snapshot) const {
  std::set<std::string> observedDevices;
  for (const auto& value : snapshot.devices) {
    if (!value.is_object()) continue;
    const auto id = jsonStringOr(value.as_object(), "id");
    if (!id.empty()) observedDevices.insert(id);
  }

  // Registry-backed deployments have an explicit inventory. A complete API-v1
  // snapshot must report every registered device, even when a device is
  // OFFLINE, so omission cannot be confused with healthy disappearance.
  if (deviceRegistry_) {
    for (const auto& device : deviceRegistry_->devices()) {
      if (!observedDevices.contains(device.id)) {
        throw std::runtime_error("incomplete DeviceAdapter snapshot: registered device missing: " + device.id);
      }
    }
  }

  // Independently protect any durable FIELD alarm that was recovered from
  // storage. If its source device disappears from a nominally successful
  // snapshot, the host refuses to use alarm absence as return-to-normal.
  std::lock_guard lock(alarmMutex_);
  for (const auto& [alarmId, alarm] : alarmStates_) {
    if (jsonStringOr(alarm, "state") == "CLEARED") continue;
    if (jsonStringOr(alarm, "sourceDomain", "FIELD") != "FIELD") continue;
    const auto deviceId = jsonStringOr(alarm, "deviceId");
    if (!deviceId.empty() && !observedDevices.contains(deviceId)) {
      throw std::runtime_error("incomplete DeviceAdapter snapshot: active alarm source device missing: " + deviceId + " (" + alarmId + ")");
    }
  }
}

std::string ApplicationService::runningConfigRevision() const {
  if (!configService_) return "legacy-registry";
  const auto status = configService_->status();
  return jsonStringOr(status, "runningRevision", "legacy-registry");
}

DeviceAcquisitionBatch ApplicationService::readAcquisitionBatch() {
  if (auto* extension = dynamic_cast<DeviceAdapterBatchExtension*>(deviceAdapter_.get())) {
    auto batch = extension->readBatch();
    if (batch.snapshot.generatedAt <= 0) batch.snapshot.generatedAt = nowMs();
    if (batch.sourceEpoch.empty() && acquisitionService_) batch.sourceEpoch = acquisitionService_->sourceEpoch();
    // The extension must declare ownership and may report a strict subset as complete.
    if (batch.ownedDeviceIds.empty() && deviceRegistry_) {
      for (const auto& d : deviceRegistry_->devices()) batch.ownedDeviceIds.insert(d.id);
    }
    for (const auto& id : batch.completeDeviceIds) {
      if (!batch.ownedDeviceIds.contains(id)) throw std::runtime_error("batch complete device is outside adapter ownership: " + id);
    }
    for (const auto& id : batch.failedDeviceIds) {
      if (!batch.ownedDeviceIds.contains(id)) throw std::runtime_error("batch failed device is outside adapter ownership: " + id);
      if (batch.completeDeviceIds.contains(id)) throw std::runtime_error("device cannot be both complete and failed in one acquisition batch: " + id);
    }
    for (const auto& id : batch.ownedDeviceIds) {
      if (!batch.completeDeviceIds.contains(id) && !batch.failedDeviceIds.contains(id)) {
        throw std::runtime_error("batch ownership is incomplete: device has neither complete nor failed status: " + id);
      }
    }
    return batch;
  }

  auto snapshot = deviceAdapter_->readSnapshot();
  if (snapshot.generatedAt <= 0) snapshot.generatedAt = nowMs();
  validateCompleteSnapshotOrThrow(snapshot);
  return acquisitionService_ ? acquisitionService_->wrapV1(std::move(snapshot)) : DeviceAcquisitionBatch{std::move(snapshot)};
}

DeviceDataSnapshot ApplicationService::normalizeAcquisitionBatch(DeviceAcquisitionBatch batch, bool persistAndEmit) {
  if (!acquisitionService_ || !qualityService_) {
    return normalizeSnapshot(std::move(batch.snapshot), persistAndEmit, batch.failedDeviceIds.empty());
  }
  auto normalized = acquisitionService_->normalize(std::move(batch), latestSnapshot(), runningConfigRevision(), *qualityService_);
  enrichSnapshotCapabilities(normalized.snapshot);
  if (persistAndEmit) persistTelemetry(normalized.snapshot.telemetry);
  normalized.snapshot.alarms = reconcileAlarms(normalized.snapshot.alarms, normalized.snapshot.generatedAt, persistAndEmit, normalized.completeDeviceIds);
  if (alarmService_) for (const auto& value : normalized.snapshot.alarms) if (value.is_object()) alarmService_->syncLegacyAlarm(value.as_object());
  if (ruleService_ && configService_) {
    ruleService_->configure(configService_->activeSnapshot(), runningConfigRevision());
    const auto transitions = ruleService_->evaluate(normalized.snapshot.telemetry, normalized.snapshot.generatedAt);
    if (alarmService_) for (const auto& value : transitions) if (value.is_object()) {
      auto occurrence = alarmService_->observe(value.as_object());
      if (persistAndEmit && !occurrence.empty()) emitRealtimeEvent("alarm.v2.updated", occurrence);
    }
  }
  if (alarmService_) {
    for (const auto& value : alarmService_->currentRuleProjections()) normalized.snapshot.alarms.emplace_back(value);
  }
  return std::move(normalized.snapshot);
}

DeviceDataSnapshot ApplicationService::normalizeSnapshot(DeviceDataSnapshot snapshot, bool persistAndEmit, bool completeSnapshot) {
  if (snapshot.generatedAt <= 0) snapshot.generatedAt = nowMs();
  std::set<std::string> completeDevices;
  if (completeSnapshot) {
    if (deviceRegistry_) for (const auto& d : deviceRegistry_->devices()) completeDevices.insert(d.id);
    else for (const auto& v : snapshot.devices) if (v.is_object()) { const auto id=jsonStringOr(v.as_object(),"id"); if(!id.empty()) completeDevices.insert(id); }
  }
  enrichSnapshotCapabilities(snapshot);
  if (persistAndEmit) persistTelemetry(snapshot.telemetry);
  snapshot.alarms = reconcileAlarms(snapshot.alarms, snapshot.generatedAt, persistAndEmit, completeDevices);
  if (alarmService_) for (const auto& value : snapshot.alarms) if (value.is_object()) alarmService_->syncLegacyAlarm(value.as_object());
  if (ruleService_ && configService_) {
    ruleService_->configure(configService_->activeSnapshot(), runningConfigRevision());
    const auto transitions = ruleService_->evaluate(snapshot.telemetry, snapshot.generatedAt);
    if (alarmService_) for (const auto& value : transitions) if (value.is_object()) {
      auto occurrence = alarmService_->observe(value.as_object());
      if (persistAndEmit && !occurrence.empty()) emitRealtimeEvent("alarm.v2.updated", occurrence);
    }
  }
  if (alarmService_) for (const auto& value : alarmService_->currentRuleProjections()) snapshot.alarms.emplace_back(value);
  return snapshot;
}

ApplicationService::MutationResult ApplicationService::acknowledgeAlarm(const std::string& alarmId,
                                                                        const boost::json::object& request,
                                                                        const SecurityContext& actor) {
  const auto comment = jsonStringOr(request, "comment");
  if (!authorize(actor.principal, Permission::AlarmAcknowledge)) {
    appendAudit(actor, "ALARM_ACK", "ALARM", alarmId, "DENIED", "role is not allowed to acknowledge alarms", comment);
    return {403, {{"error", "forbidden: alarm.ack permission required"}}};
  }
  const auto operatorId = actor.principal.id;
  if (alarmId.empty()) return {400, {{"error", "alarm id is required"}}};
  if (comment.size() > 1000) return {400, {{"error", "comment is too long"}}};

  boost::json::object updated;
  {
    std::lock_guard lock(alarmMutex_);
    const auto it = alarmStates_.find(alarmId);
    if (it == alarmStates_.end()) { appendAudit(actor,"ALARM_ACK","ALARM",alarmId,"NOT_FOUND","alarm not found",comment); return {404, {{"error", "alarm not found"}}}; }
    const auto state = jsonStringOr(it->second, "state");
    if (state == "CLEARED") { appendAudit(actor,"ALARM_ACK","ALARM",alarmId,"CONFLICT","cleared alarm cannot be acknowledged",comment); return {409, {{"error", "cleared alarm cannot be acknowledged"}, {"alarm", it->second}}}; }
    if (state == "ACKNOWLEDGED") { appendAudit(actor,"ALARM_ACK","ALARM",alarmId,"IDEMPOTENT_REPLAY","alarm already acknowledged",comment); return {200, it->second}; }
    auto& alarm = it->second;
    alarm["state"] = "ACKNOWLEDGED";
    alarm["acknowledgedAt"] = nowMs();
    alarm["acknowledgedBy"] = operatorId;
    if (!comment.empty()) alarm["acknowledgementComment"] = comment;
    updated = alarm;
  }

  recordAlarmEvent("ACKNOWLEDGED", updated, "OPERATOR", operatorId, comment);
  appendAudit(actor, "ALARM_ACK", "ALARM", alarmId, "SUCCESS", "alarm acknowledged", comment);
  emitRealtimeEvent("alarm.updated", updated);
  auto snapshot = latestSnapshot();
  bool replaced = false;
  for (auto& value : snapshot.alarms) {
    if (value.is_object() && jsonStringOr(value.as_object(), "id") == alarmId) { value = updated; replaced = true; break; }
  }
  if (!replaced) snapshot.alarms.emplace_back(updated);
  snapshot.generatedAt = nowMs();
  replaceLatestSnapshot(std::move(snapshot));
  return {200, updated};
}

ApplicationService::MutationResult ApplicationService::clearAlarm(const std::string& alarmId,
                                                                  const boost::json::object& request,
                                                                  const SecurityContext& actor) {
  const auto comment = jsonStringOr(request, "comment");
  if (!authorize(actor.principal, Permission::AlarmAcknowledge)) {
    appendAudit(actor, "ALARM_CLEAR_ATTEMPT", "ALARM", alarmId, "DENIED", "role is not allowed to mutate alarm state", comment);
    return {403, {{"error", "forbidden"}}};
  }
  if (alarmId.empty()) return {400, {{"error", "alarm id is required"}}};
  std::lock_guard lock(alarmMutex_);
  const auto it = alarmStates_.find(alarmId);
  if (it == alarmStates_.end()) { appendAudit(actor,"ALARM_CLEAR_ATTEMPT","ALARM",alarmId,"NOT_FOUND","alarm not found",comment); return {404, {{"error", "alarm not found"}}}; }
  if (jsonStringOr(it->second, "state") == "CLEARED") { appendAudit(actor,"ALARM_CLEAR_ATTEMPT","ALARM",alarmId,"NOOP","alarm already cleared by field return-to-normal",comment); return {200, it->second}; }
  appendAudit(actor,"ALARM_CLEAR_ATTEMPT","ALARM",alarmId,"REJECTED","manual clear is prohibited; field return-to-normal owns CLEARED",comment);
  return {409, {{"error", "alarm cannot be manually cleared; CLEARED is driven by field return-to-normal"}, {"alarm", it->second}}};
}

void ApplicationService::publishSnapshotEvents(const DeviceDataSnapshot& snapshot) {
  for (const auto& item : snapshot.devices) emitRealtimeEvent("device.updated", item);
  for (const auto& item : snapshot.telemetry) {
    std::optional<std::int64_t> seq;
    if (item.is_object()) {
      const auto& obj = item.as_object();
      if (const auto* v = obj.if_contains("seq"); v && v->is_int64()) seq = v->as_int64();
    }
    emitRealtimeEvent("telemetry.updated", item, seq);
  }
  // alarm.updated is emitted only when the lifecycle state changes. Repeating
  // an unchanged active alarm every polling interval would hide lifecycle bugs.
  for (const auto& item : snapshot.communication) emitRealtimeEvent("communication.updated", item);
}

void ApplicationService::runRealtimePublisher() {
  while (publisherRunning_.load()) {
    bool acquisitionSucceeded = false;
    DeviceDataSnapshot raw;
    DeviceAcquisitionBatch batch;
    try {
      batch = readAcquisitionBatch();
      raw = batch.snapshot;
      if (batch.failedDeviceIds.empty()) {
        recordAcquisitionSuccess(raw.generatedAt);
      } else {
        std::lock_guard lock(acquisitionMutex_);
        acquisitionState_ = "DEGRADED";
        acquisitionDetail_ = "partial device acquisition: " + std::to_string(batch.failedDeviceIds.size()) + " device(s) failed";
        acquisitionLastSuccessTs_ = raw.generatedAt;
        acquisitionLastFailureTs_ = nowMs();
        ++acquisitionTotalFailures_;
      }
      acquisitionSucceeded = true;
    } catch (const std::exception& e) {
      recordAcquisitionFailure(std::string("DeviceAdapter acquisition failed/incomplete: ") + e.what());
    } catch (...) {
      recordAcquisitionFailure("DeviceAdapter readSnapshot failed/incomplete: unknown exception");
    }

    if (acquisitionSucceeded) {
      try {
        batch.snapshot = std::move(raw);
        auto snapshot = normalizeAcquisitionBatch(std::move(batch), true);
        replaceLatestSnapshot(snapshot);
        const auto now = nowMs();
        const auto previousPublish = lastHmiPublishTs_.load();
        if (previousPublish == 0 || now - previousPublish >= config_.telemetryPublishMs) {
          publishSnapshotEvents(snapshot);
          lastHmiPublishTs_.store(now);
        }
        updateStorageAlarm();
      } catch (const std::exception& e) {
        // Storage/domain persistence failure is not an acquisition failure.
        // Keep the last field snapshot, surface a platform event, and allow the
        // next cycle to retry without falsely clearing field alarms.
        emitRealtimeEvent("platform.storage_fault", boost::json::object{{"detail",e.what()},{"timestamp",nowMs()}});
      } catch (...) {
        emitRealtimeEvent("platform.storage_fault", boost::json::object{{"detail","unknown persistence error"},{"timestamp",nowMs()}});
      }
    }

    const auto interval = std::chrono::milliseconds(acquisitionIntervalMs());
    const auto quantum = std::chrono::milliseconds(50);
    auto slept = std::chrono::milliseconds(0);
    while (publisherRunning_.load() && slept < interval) {
      const auto step = std::min(quantum, interval - slept);
      std::this_thread::sleep_for(step);
      slept += step;
    }
  }
}

}  // namespace smart_factory
