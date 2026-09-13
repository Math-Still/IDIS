#include "smart_factory/os_adapter.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(smart_factory_native, m) {
  m.doc() = "Optional Python bridge for target-OS C++ capabilities. Not required by the final C++ backend.";
  m.def("probe_realtime", [](int priority) {
    auto adapter = smart_factory::makeDefaultOsAdapter();
    smart_factory::RealtimeThreadOptions options;
    options.priority = priority;
    options.threadName = "sf-py-probe";
    const auto result = adapter->configureCurrentThreadRealtime(options);
    py::dict out;
    out["osAdapter"] = adapter->name();
    out["granted"] = result.granted;
    out["detail"] = result.detail;
    return out;
  });
}
