#pragma once

#include <boost/json.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace smart_factory {

struct DeviceConnectionDefinition {
  std::string interfaceType{"UNSPECIFIED"};
  std::string protocol{"UNSPECIFIED"};
  std::string endpoint;
  std::string driver;
  boost::json::object parameters;

  boost::json::object toJson(bool includeParameters = true) const;
  std::string legacyLabel() const;
};

struct TelemetryPointDefinition {
  std::string pointId;
  std::string label;
  std::string unit;
  std::string valueType{"number"};
  std::int64_t samplePeriodMs{1000};
  boost::json::object source;
  boost::json::object limits;
  boost::json::object simulation;
};

enum class CommandExecutionClass { Normal, Realtime, Emergency };

struct CommandDefinition {
  std::string actionId;
  std::string displayName;
  std::string requiredPermission{"command.issue"};
  CommandExecutionClass executionClass{CommandExecutionClass::Normal};
  bool reasonRequired{false};
  boost::json::object parameterSchema;
  boost::json::object mapping;
  boost::json::object metadata;

  boost::json::object toJson() const;
};

struct DeviceDefinition {
  std::string id;
  std::string name;
  std::string scenario;
  std::string location;
  std::string osNode;
  std::string adapter;
  DeviceConnectionDefinition connection;
  std::vector<std::string> tags;
  std::vector<TelemetryPointDefinition> points;
  boost::json::object commandMap;
  std::unordered_map<std::string, CommandDefinition> commandDefinitions;
  boost::json::object metadata;
};

class DeviceRegistry {
 public:
  static std::shared_ptr<DeviceRegistry> load(const std::string& path);

  const std::string& sourcePath() const noexcept { return sourcePath_; }
  const std::string& schemaVersion() const noexcept { return schemaVersion_; }
  const std::string& site() const noexcept { return site_; }
  const std::vector<DeviceDefinition>& devices() const noexcept { return devices_; }
  const DeviceDefinition* find(const std::string& deviceId) const noexcept;
  const CommandDefinition* findCommand(const std::string& deviceId, const std::string& actionId) const noexcept;
  const TelemetryPointDefinition* findPoint(const std::string& deviceId, const std::string& pointId) const noexcept;
  std::int64_t minimumSamplePeriodMs() const noexcept;
  static std::string executionClassName(CommandExecutionClass executionClass);
  std::size_t size() const noexcept { return devices_.size(); }
  boost::json::object toJson(bool includeParameters = true) const;

 private:
  std::string sourcePath_;
  std::string schemaVersion_;
  std::string site_;
  std::vector<DeviceDefinition> devices_;
  std::unordered_map<std::string, std::size_t> index_;
};

}  // namespace smart_factory
