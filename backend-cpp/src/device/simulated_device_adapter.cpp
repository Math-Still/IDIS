#include "smart_factory/device_adapter.hpp"
#include "smart_factory/device_registry.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace smart_factory {
namespace {
std::int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string stringOr(const boost::json::object& o, const char* key, const std::string& fallback = {}) {
  if (const auto* v = o.if_contains(key); v && v->is_string()) return std::string(v->as_string());
  return fallback;
}

double numberOr(const boost::json::object& o, const char* key, double fallback = 0.0) {
  const auto* v = o.if_contains(key);
  if (!v) return fallback;
  if (v->is_double()) return v->as_double();
  if (v->is_int64()) return static_cast<double>(v->as_int64());
  if (v->is_uint64()) return static_cast<double>(v->as_uint64());
  return fallback;
}

boost::json::value simulatedValue(const TelemetryPointDefinition& point, std::int64_t seq, std::int64_t now) {
  const auto kind = stringOr(point.simulation, "kind", point.valueType == "boolean" ? "toggle" : "constant");
  if (kind == "process" || kind == "processTotal") {
    const double t = static_cast<double>(now) / 1000.0;
    const double w = 6.283185307179586 / std::max(1.0, numberOr(point.simulation,"periodSeconds",900));
    const double phase = numberOr(point.simulation,"phase",0);
    const double base = numberOr(point.simulation,"base",0), amplitude = numberOr(point.simulation,"amplitude",0);
    if (kind == "processTotal") {
      const double elapsed = std::fmod(t + 8*3600, 8*3600);
      const auto integral = [w,phase](double x) { return -std::cos(w*x+phase)/w - .22*std::cos(w*2.7*x+phase+.8)/(w*2.7); };
      return base*elapsed/3600 + amplitude*(integral(t)-integral(t-elapsed))/3600;
    }
    return base + amplitude*(std::sin(w*t+phase) + .22*std::sin(w*2.7*t+phase+.8));
  }
  if (kind == "toggle") {
    const auto period = std::max(1.0, numberOr(point.simulation, "periodSteps", 30.0));
    return static_cast<bool>((seq / static_cast<std::int64_t>(period)) % 2 == 0);
  }
  if (kind == "counter") {
    const auto base = numberOr(point.simulation, "base", 0.0);
    const auto step = numberOr(point.simulation, "step", 1.0);
    return static_cast<std::int64_t>(std::llround(base + static_cast<double>(seq) * step));
  }
  if (kind == "sine") {
    const auto base = numberOr(point.simulation, "base", 0.0);
    const auto amplitude = numberOr(point.simulation, "amplitude", 1.0);
    const auto period = std::max(1.0, numberOr(point.simulation, "periodSteps", 10.0));
    return base + amplitude * std::sin(static_cast<double>(seq) / period);
  }
  if (const auto* v = point.simulation.if_contains("value")) return *v;
  if (point.valueType == "boolean") return false;
  return numberOr(point.simulation, "base", 0.0);
}

boost::json::object telemetry(const DeviceDefinition& device, const TelemetryPointDefinition& point,
                              boost::json::value value, std::int64_t now, std::int64_t seq) {
  boost::json::object o{{"deviceId",device.id},{"pointId",point.pointId},{"label",point.label},{"value",std::move(value)},
    {"quality","GOOD"},{"sampleTs",now-15},{"receiveTs",now-8},{"seq",seq}};
  if (!point.unit.empty()) o["unit"] = point.unit;
  return o;
}

boost::json::object communication(const DeviceDefinition& d, std::int64_t now) {
  const auto comm = d.metadata.if_contains("simulationCommunication");
  const auto* c = comm && comm->is_object() ? &comm->as_object() : nullptr;
  const auto metric = [c](const char* key, int fallback) {
    if (!c) return fallback;
    const auto* v = c->if_contains(key);
    if (!v) return fallback;
    if (v->is_int64()) return static_cast<int>(v->as_int64());
    if (v->is_uint64()) return static_cast<int>(v->as_uint64());
    return fallback;
  };
  const auto text = [c](const char* key, const char* fallback) {
    if (!c) return std::string(fallback);
    const auto* v = c->if_contains(key);
    return v && v->is_string() ? std::string(v->as_string()) : std::string(fallback);
  };
  return {{"deviceId",d.id},{"online",true},{"quality",text("quality","GOOD")},
    {"latencyMsP50",metric("latencyMsP50",6)},{"latencyMsP95",metric("latencyMsP95",12)},
    {"latencyMsP99",metric("latencyMsP99",18)},{"jitterMs",metric("jitterMs",3)},
    {"messageRatePerMin",metric("messageRatePerMin",60)},{"reconnectCount",metric("reconnectCount",0)},
    {"errorCount",metric("errorCount",0)},{"lastSeen",now-8}};
}

class SimulatedDeviceAdapter final : public DeviceAdapter, public DeviceCommandExecutionExtension {
 public:
  explicit SimulatedDeviceAdapter(std::shared_ptr<const DeviceRegistry> registry)
      : registry_(std::move(registry)), bootTs_(nowMs()) {
    if (!registry_) throw std::runtime_error("SimulatedDeviceAdapter requires DeviceRegistry");
  }

  std::string name() const override { return "simulated-registry"; }
  bool simulated() const override { return true; }
  bool start(std::string& detail) override {
    running_ = true;
    detail = "simulated adapter started with " + std::to_string(registry_->size()) + " registry devices";
    return true;
  }
  void stop() noexcept override { running_ = false; }
  DeviceAdapterHealth health() const override {
    return {running_, running_ ? "READY" : "OFFLINE", running_ ? "simulated data source active" : "adapter stopped", lastSuccessTs_, 0};
  }

  DeviceDataSnapshot readSnapshot() override {
    std::lock_guard lock(mutex_);
    const auto now = nowMs();
    const auto seq = ++seq_;
    DeviceDataSnapshot snapshot;
    snapshot.generatedAt = now;

    for (const auto& d : registry_->devices()) {
      boost::json::array tags;
      for (const auto& tag : d.tags) tags.emplace_back(tag);
      snapshot.devices.emplace_back(boost::json::object{
        {"id",d.id},{"name",d.name},{"scenario",d.scenario},{"status","ONLINE"},{"location",d.location},
        {"protocol",d.connection.legacyLabel()},{"connection",d.connection.toJson(true)},{"osNode",d.osNode},
        {"lastSeen",now-8},{"tags",std::move(tags)},{"adapterId",d.adapter}
      });
      for (const auto& point : d.points) {
        const auto key = d.id + ":" + point.pointId;
        auto value = simulatedValue(point, seq, now);
        if (const auto it = pointOverrides_.find(key); it != pointOverrides_.end()) value = it->second;
        if (point.simulation.if_contains("warningHigh") && value.is_number()) {
          const double reading = value.is_double() ? value.as_double() : numberOr(point.simulation,"base");
          const double threshold = numberOr(point.simulation,"warningHigh");
          const auto alarmId = "process-warning-" + d.id + "-" + point.pointId;
          bool active = processAlarmActive_[alarmId];
          const double hysteresis = numberOr(point.simulation,"warningDeadband",0);
          const bool next = active ? reading > threshold-hysteresis : reading > threshold;
          if (next && !active) processAlarmRaised_[alarmId] = now;
          if (!next && active) processAlarmCleared_[alarmId] = now;
          processAlarmActive_[alarmId] = next;
          if (processAlarmRaised_.contains(alarmId)) {
            boost::json::object alarm{{"id",alarmId},{"deviceId",d.id},{"title",point.label+"偏高"},
              {"message",point.label+"超过运行关注值"},{"severity","WARNING"},{"state",next?"ACTIVE":"CLEARED"},
              {"raisedAt",processAlarmRaised_[alarmId]}};
            if (!next) alarm["clearedAt"] = processAlarmCleared_[alarmId];
            snapshot.alarms.emplace_back(std::move(alarm));
          }
        }
        snapshot.telemetry.emplace_back(telemetry(d, point, std::move(value), now, seq));
      }
      snapshot.communication.emplace_back(communication(d, now));
    }

    if (!registry_->devices().empty()) {
      const auto& d = registry_->devices().front();
      snapshot.alarms.emplace_back(boost::json::object{{"id","startup-selfcheck-cleared"},{"deviceId",d.id},
        {"title","启动自检"},{"message","设备启动自检已完成"},{"severity","INFO"},{"state","CLEARED"},
        {"raisedAt",bootTs_-2000},{"clearedAt",bootTs_-1000}});
    }
    bool gasHigh = false;
    for (const auto& v : snapshot.telemetry) {
      const auto& p = v.as_object();
      if (stringOr(p,"deviceId")=="safety-02" && stringOr(p,"pointId")=="level") gasHigh = numberOr(p,"value") > 25;
    }
    if (gasHigh) {
      snapshot.alarms.emplace_back(boost::json::object{{"id","gas-attention-safety-02"},{"deviceId","safety-02"},
        {"title","精炼区气体浓度偏高"},{"message","当前浓度处于关注区间，请检查通风状态。"},{"severity","WARNING"},{"state","ACTIVE"},
        {"raisedAt",bootTs_-90'000}});
    }
    lastSuccessTs_ = now;
    return snapshot;
  }


  DeviceCommandExecutionIsolation executionIsolation() const noexcept override {
    return DeviceCommandExecutionIsolation::BoundedInProcess;
  }

  DeviceCommandDeliveryResult executeWithDelivery(const DeviceCommand& command, std::int64_t deadlineTs) override {
    if (!registry_->find(command.deviceId)) {
      return {DeviceCommandDeliveryCertainty::NotSent, "unknown deviceId in registry: " + command.deviceId};
    }
    if (!registry_->findCommand(command.deviceId, command.action)) {
      return {DeviceCommandDeliveryCertainty::Rejected, "command is not registered for device: " + command.action};
    }
    if (command.action.find("delivery-not-sent") != std::string::npos) {
      return {DeviceCommandDeliveryCertainty::NotSent, "simulated transport rejected before send"};
    }
    if (command.action.find("delivery-rejected") != std::string::npos) {
      return {DeviceCommandDeliveryCertainty::Rejected, "simulated device rejected request"};
    }
    const auto workMs = command.action.find("emergency") != std::string::npos ? 5 : 40;
    if (nowMs() + workMs > deadlineTs) {
      return {DeviceCommandDeliveryCertainty::PossiblyApplied, "simulated delivery deadline expired before a definitive acknowledgement"};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(workMs));
    {
      std::lock_guard lock(mutex_);
      feedbackReadyAt_[command.commandId] = nowMs() + 120;
      if (command.action == "device.start") pointOverrides_[command.deviceId + ":fanRunning"] = true;
      if (command.action == "device.stop" || command.action == "device.emergencyStop") pointOverrides_[command.deviceId + ":fanRunning"] = false;
    }
    if (command.action.find("delivery-unknown") != std::string::npos) {
      return {DeviceCommandDeliveryCertainty::PossiblyApplied, "simulated request may have crossed the device boundary; acknowledgement lost"};
    }
    return {DeviceCommandDeliveryCertainty::Applied, "simulated write accepted; awaiting independent feedback"};
  }

  DeviceCommandResult execute(const DeviceCommand& command) override {
    if (!registry_->find(command.deviceId)) return {false, "unknown deviceId in registry: " + command.deviceId};
    if (!registry_->findCommand(command.deviceId, command.action)) return {false, "command is not registered for device: " + command.action};
    std::this_thread::sleep_for(std::chrono::milliseconds(command.action.find("emergency") != std::string::npos ? 5 : 40));
    std::lock_guard lock(mutex_);
    feedbackReadyAt_[command.commandId] = nowMs() + 120;
    return {true, "simulated write accepted; awaiting independent feedback"};
  }

  DeviceCommandFeedback readCommandFeedback(const DeviceCommand& command) override {
    std::lock_guard lock(mutex_);
    if (command.action.find("feedback-fail") != std::string::npos) return {DeviceFeedbackState::Failed,"simulated field feedback reports command failure","false"};
    if (command.action.find("feedback-timeout") != std::string::npos) return {DeviceFeedbackState::Pending,"simulated field feedback intentionally withheld","null"};
    const auto it = feedbackReadyAt_.find(command.commandId);
    if (it == feedbackReadyAt_.end()) return {DeviceFeedbackState::Unsupported,"no feedback tracking entry","null"};
    if (nowMs() < it->second) return {DeviceFeedbackState::Pending,"waiting for simulated field state","null"};
    feedbackReadyAt_.erase(it);
    return {DeviceFeedbackState::Confirmed,"simulated field feedback confirmed requested state","true"};
  }

 private:
  std::shared_ptr<const DeviceRegistry> registry_;
  const std::int64_t bootTs_;
  mutable std::mutex mutex_;
  mutable std::int64_t seq_{0};
  mutable std::int64_t lastSuccessTs_{0};
  mutable bool running_{false};
  std::unordered_map<std::string,std::int64_t> feedbackReadyAt_;
  std::unordered_map<std::string,boost::json::value> pointOverrides_;
  std::unordered_map<std::string,bool> processAlarmActive_;
  std::unordered_map<std::string,std::int64_t> processAlarmRaised_, processAlarmCleared_;
};
}  // namespace

std::unique_ptr<DeviceAdapter> makeSimulatedDeviceAdapter(std::shared_ptr<const DeviceRegistry> registry) {
  return std::make_unique<SimulatedDeviceAdapter>(std::move(registry));
}

}  // namespace smart_factory
