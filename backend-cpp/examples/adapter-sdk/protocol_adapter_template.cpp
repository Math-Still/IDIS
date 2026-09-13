// TEMPLATE ONLY — ProtocolAdapter is normally composed inside a DeviceAdapter.
#include "smart_factory/protocol_adapter.hpp"

class TargetProtocolAdapter final : public smart_factory::ProtocolAdapter {
 public:
  std::string name() const override { return "REPLACE-protocol"; }
  bool start(std::string& detail) override { detail="NOT IMPLEMENTED"; return false; }
  void stop() noexcept override {}
  smart_factory::AdapterHealth health() const override { return {smart_factory::AdapterHealthState::Offline,"NOT IMPLEMENTED",0,0}; }
  smart_factory::ProtocolReadResult read(const smart_factory::ProtocolReadRequest&) override { return {false,nullptr,0,"BAD","NOT IMPLEMENTED"}; }
  smart_factory::ProtocolWriteResult write(const smart_factory::ProtocolWriteRequest&) override { return {false,"NOT IMPLEMENTED"}; }
};
