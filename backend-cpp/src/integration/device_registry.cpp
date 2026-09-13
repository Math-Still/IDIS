#include "smart_factory/device_registry.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace smart_factory {
namespace {
std::string str(const boost::json::object& o, const char* key, bool required = false, const std::string& fallback = {}) {
  if (const auto* v = o.if_contains(key); v && v->is_string()) return std::string(v->as_string());
  if (required) throw std::runtime_error(std::string("device registry missing string field: ") + key);
  return fallback;
}

std::int64_t i64(const boost::json::object& o, const char* key, std::int64_t fallback) {
  const auto* v = o.if_contains(key);
  if (!v) return fallback;
  if (v->is_int64()) return v->as_int64();
  if (v->is_uint64()) return static_cast<std::int64_t>(v->as_uint64());
  throw std::runtime_error(std::string("device registry field must be integer: ") + key);
}

boost::json::object objectOrEmpty(const boost::json::object& o, const char* key) {
  if (const auto* v = o.if_contains(key); v && v->is_object()) return v->as_object();
  return {};
}

bool boolean(const boost::json::object& o, const char* key, bool fallback) {
  const auto* v = o.if_contains(key);
  if (!v) return fallback;
  if (!v->is_bool()) throw std::runtime_error(std::string("device registry field must be boolean: ") + key);
  return v->as_bool();
}

CommandExecutionClass parseExecutionClass(const std::string& value, const std::string& context) {
  if (value == "NORMAL") return CommandExecutionClass::Normal;
  if (value == "REALTIME") return CommandExecutionClass::Realtime;
  if (value == "EMERGENCY") return CommandExecutionClass::Emergency;
  throw std::runtime_error("unsupported command executionClass for " + context + ": " + value);
}

void validateParameterSchema(const boost::json::object& schema, const std::string& context) {
  const auto type = str(schema, "type", true);
  static const std::set<std::string> supported{"none", "number", "integer", "boolean", "string", "object"};
  if (!supported.contains(type)) throw std::runtime_error("unsupported parameterSchema.type for " + context + ": " + type);
  if (const auto* required = schema.if_contains("required"); required && !required->is_bool())
    throw std::runtime_error("parameterSchema.required must be boolean for " + context);
  if (const auto* values = schema.if_contains("enum"); values && !values->is_array())
    throw std::runtime_error("parameterSchema.enum must be an array for " + context);
  for (const auto* key : {"minimum", "maximum"}) {
    if (const auto* value = schema.if_contains(key); value && !(value->is_int64() || value->is_uint64() || value->is_double()))
      throw std::runtime_error(std::string("parameterSchema.") + key + " must be numeric for " + context);
  }
  for (const auto* key : {"minLength", "maxLength"}) {
    if (const auto* value = schema.if_contains(key); value && !(value->is_int64() || value->is_uint64()))
      throw std::runtime_error(std::string("parameterSchema.") + key + " must be an integer for " + context);
  }
  if (type == "none" && boolean(schema, "required", false))
    throw std::runtime_error("parameterSchema type none cannot be required for " + context);
}


void validatePointLimits(const boost::json::object& limits, const std::string& context) {
  for (const auto* key : {"warningLow", "warningHigh", "alarmLow", "alarmHigh", "setpoint", "deadband"}) {
    if (const auto* value = limits.if_contains(key); value && !(value->is_int64() || value->is_uint64() || value->is_double()))
      throw std::runtime_error(std::string("point limits.") + key + " must be numeric for " + context);
  }
}

const std::set<std::string> kScenarios{
  "temperature_humidity", "pir_lighting", "hazardous_gas", "agv_obstacle", "goods_counting"
};
}  // namespace

std::string DeviceRegistry::executionClassName(CommandExecutionClass executionClass) {
  switch (executionClass) {
    case CommandExecutionClass::Normal: return "NORMAL";
    case CommandExecutionClass::Realtime: return "REALTIME";
    case CommandExecutionClass::Emergency: return "EMERGENCY";
  }
  return "NORMAL";
}

boost::json::object CommandDefinition::toJson() const {
  boost::json::object out{{"displayName", displayName}, {"requiredPermission", requiredPermission},
                          {"executionClass", DeviceRegistry::executionClassName(executionClass)},
                          {"reasonRequired", reasonRequired}, {"parameterSchema", parameterSchema}};
  if (!mapping.empty()) out["mapping"] = mapping;
  if (!metadata.empty()) out["metadata"] = metadata;
  return out;
}

boost::json::object DeviceConnectionDefinition::toJson(bool includeParameters) const {
  boost::json::object out{{"interfaceType", interfaceType}, {"protocol", protocol}, {"endpoint", endpoint}, {"driver", driver}};
  if (includeParameters) out["parameters"] = parameters;
  return out;
}

std::string DeviceConnectionDefinition::legacyLabel() const {
  if (protocol.empty() || protocol == "UNSPECIFIED") return interfaceType;
  if (interfaceType.empty() || interfaceType == "UNSPECIFIED" || protocol.find(interfaceType) != std::string::npos) return protocol;
  return protocol + " / " + interfaceType;
}

std::shared_ptr<DeviceRegistry> DeviceRegistry::load(const std::string& path) {
  if (path.empty()) throw std::runtime_error("device_registry_file is required");
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("Cannot open device registry: " + path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  boost::json::error_code ec;
  auto parsed = boost::json::parse(text, ec);
  if (ec || !parsed.is_object()) throw std::runtime_error("Invalid device registry JSON: " + path + ": " + ec.message());
  const auto& root = parsed.as_object();
  auto registry = std::shared_ptr<DeviceRegistry>(new DeviceRegistry());
  registry->sourcePath_ = path;
  registry->schemaVersion_ = str(root, "schemaVersion", true);
  registry->site_ = str(root, "site", false, "UNSPECIFIED");
  if (registry->schemaVersion_ != "1.0" && registry->schemaVersion_ != "1.1")
    throw std::runtime_error("Unsupported device registry schemaVersion: " + registry->schemaVersion_);
  const auto* devicesValue = root.if_contains("devices");
  if (!devicesValue || !devicesValue->is_array()) throw std::runtime_error("device registry requires devices[]");

  std::set<std::string> ids;
  for (const auto& value : devicesValue->as_array()) {
    if (!value.is_object()) throw std::runtime_error("device registry devices[] entries must be objects");
    const auto& o = value.as_object();
    DeviceDefinition d;
    d.id = str(o, "id", true);
    d.name = str(o, "name", true);
    d.scenario = str(o, "scenario", true);
    d.location = str(o, "location", true);
    d.osNode = str(o, "osNode", false, "UNASSIGNED");
    d.adapter = str(o, "adapter", false, "default");
    if (!ids.insert(d.id).second) throw std::runtime_error("duplicate device id in registry: " + d.id);
    if (!kScenarios.contains(d.scenario)) throw std::runtime_error("unsupported scenario in registry for " + d.id + ": " + d.scenario);

    const auto* connectionValue = o.if_contains("connection");
    if (!connectionValue || !connectionValue->is_object()) throw std::runtime_error("device " + d.id + " requires connection object");
    const auto& c = connectionValue->as_object();
    d.connection.interfaceType = str(c, "interfaceType", true);
    d.connection.protocol = str(c, "protocol", true);
    d.connection.endpoint = str(c, "endpoint", false);
    d.connection.driver = str(c, "driver", true);
    d.connection.parameters = objectOrEmpty(c, "parameters");

    if (const auto* tags = o.if_contains("tags"); tags && tags->is_array()) {
      for (const auto& t : tags->as_array()) if (t.is_string()) d.tags.emplace_back(t.as_string());
    }

    const auto* points = o.if_contains("points");
    if (!points || !points->is_array() || points->as_array().empty()) throw std::runtime_error("device " + d.id + " requires at least one telemetry point");
    std::set<std::string> pointIds;
    for (const auto& pointValue : points->as_array()) {
      if (!pointValue.is_object()) throw std::runtime_error("device " + d.id + " points[] entries must be objects");
      const auto& p = pointValue.as_object();
      TelemetryPointDefinition point;
      point.pointId = str(p, "pointId", true);
      point.label = str(p, "label", true);
      point.unit = str(p, "unit", false);
      point.valueType = str(p, "valueType", false, "number");
      point.samplePeriodMs = i64(p, "samplePeriodMs", 1000);
      if (point.samplePeriodMs < 10 || point.samplePeriodMs > 3600000) throw std::runtime_error("invalid samplePeriodMs for " + d.id + ":" + point.pointId);
      if (!pointIds.insert(point.pointId).second) throw std::runtime_error("duplicate pointId for " + d.id + ": " + point.pointId);
      point.source = objectOrEmpty(p, "source");
      point.limits = objectOrEmpty(p, "limits");
      validatePointLimits(point.limits, d.id + ":" + point.pointId);
      point.simulation = objectOrEmpty(p, "simulation");
      d.points.push_back(std::move(point));
    }
    d.commandMap = objectOrEmpty(o, "commands");
    if (registry->schemaVersion_ == "1.0" && !d.commandMap.empty())
      throw std::runtime_error("device registry schemaVersion 1.0 supports monitoring only; migrate command definitions to schemaVersion 1.1");
    for (const auto& [actionKey, commandValue] : d.commandMap) {
      const std::string actionId(actionKey);
      const std::string context = d.id + ":" + actionId;
      if (actionId.empty()) throw std::runtime_error("device command action id cannot be empty for " + d.id);
      if (!commandValue.is_object()) throw std::runtime_error("device command definition must be an object for " + context);
      const auto& commandObject = commandValue.as_object();
      CommandDefinition definition;
      definition.actionId = actionId;
      definition.displayName = str(commandObject, "displayName", true);
      definition.requiredPermission = str(commandObject, "requiredPermission", true);
      if (definition.requiredPermission != "command.issue" && definition.requiredPermission != "command.emergency")
        throw std::runtime_error("unsupported requiredPermission for " + context + ": " + definition.requiredPermission);
      definition.executionClass = parseExecutionClass(str(commandObject, "executionClass", true), context);
      definition.reasonRequired = boolean(commandObject, "reasonRequired", false);
      const auto* schemaValue = commandObject.if_contains("parameterSchema");
      if (!schemaValue || !schemaValue->is_object()) throw std::runtime_error("command " + context + " requires parameterSchema object");
      definition.parameterSchema = schemaValue->as_object();
      validateParameterSchema(definition.parameterSchema, context);
      definition.mapping = objectOrEmpty(commandObject, "mapping");
      definition.metadata = objectOrEmpty(commandObject, "metadata");
      if (definition.executionClass == CommandExecutionClass::Emergency) {
        if (definition.requiredPermission != "command.emergency")
          throw std::runtime_error("EMERGENCY command must require command.emergency permission for " + context);
        if (!definition.reasonRequired)
          throw std::runtime_error("EMERGENCY command must set reasonRequired=true for " + context);
      }
      d.commandDefinitions.emplace(actionId, std::move(definition));
    }
    d.metadata = objectOrEmpty(o, "metadata");
    registry->index_[d.id] = registry->devices_.size();
    registry->devices_.push_back(std::move(d));
  }
  if (registry->devices_.empty()) throw std::runtime_error("device registry must contain at least one device");
  return registry;
}

const DeviceDefinition* DeviceRegistry::find(const std::string& deviceId) const noexcept {
  const auto it = index_.find(deviceId);
  return it == index_.end() ? nullptr : &devices_[it->second];
}

const CommandDefinition* DeviceRegistry::findCommand(const std::string& deviceId, const std::string& actionId) const noexcept {
  const auto* device = find(deviceId);
  if (!device) return nullptr;
  const auto it = device->commandDefinitions.find(actionId);
  return it == device->commandDefinitions.end() ? nullptr : &it->second;
}

const TelemetryPointDefinition* DeviceRegistry::findPoint(const std::string& deviceId, const std::string& pointId) const noexcept {
  const auto* device = find(deviceId);
  if (!device) return nullptr;
  for (const auto& point : device->points) if (point.pointId == pointId) return &point;
  return nullptr;
}

std::int64_t DeviceRegistry::minimumSamplePeriodMs() const noexcept {
  std::int64_t minimum = 0;
  for (const auto& device : devices_) {
    for (const auto& point : device.points) {
      if (point.samplePeriodMs <= 0) continue;
      minimum = minimum == 0 ? point.samplePeriodMs : std::min(minimum, point.samplePeriodMs);
    }
  }
  return minimum > 0 ? minimum : 1000;
}

boost::json::object DeviceRegistry::toJson(bool includeParameters) const {
  boost::json::array devices;
  for (const auto& d : devices_) {
    boost::json::array tags;
    for (const auto& tag : d.tags) tags.emplace_back(tag);
    boost::json::array points;
    for (const auto& p : d.points) {
      boost::json::object point{{"pointId",p.pointId},{"label",p.label},{"unit",p.unit},{"valueType",p.valueType},{"samplePeriodMs",p.samplePeriodMs},{"source",p.source}};
      if (!p.limits.empty()) point["limits"] = p.limits;
      points.emplace_back(std::move(point));
    }
    devices.emplace_back(boost::json::object{{"id",d.id},{"name",d.name},{"scenario",d.scenario},{"location",d.location},{"osNode",d.osNode},{"adapter",d.adapter},{"connection",d.connection.toJson(includeParameters)},{"tags",std::move(tags)},{"points",std::move(points)},{"commands",d.commandMap},{"metadata",d.metadata}});
  }
  return {{"schemaVersion",schemaVersion_},{"site",site_},{"sourcePath",sourcePath_},{"devices",std::move(devices)}};
}

}  // namespace smart_factory
