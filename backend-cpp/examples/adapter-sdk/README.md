# Adapter SDK Templates

这些模板只用于交接，不参与默认构建，也不声称已经实现任何现场协议或国产 OS 私有 API。

- `target_device_adapter_template.cpp`：实现现场设备/协议聚合边界，并导出 DeviceAdapter Plugin ABI v1。
- `target_os_adapter_template.cpp`：封装目标国产 OS 实时调度和特殊接口，并导出 OsAdapter Plugin ABI v1。
- `protocol_adapter_template.cpp`：用于具体 Modbus/CAN/OPC UA/厂商协议实现，可组合进 DeviceAdapter。

模板故意 `fail closed`。未替换 TODO 前不能用于 factory 部署。

## 编译约束

可直接使用本目录的 CMake 工程编译模板，确认其与当前 API v1 头文件仍兼容。DeviceAdapter/OsAdapter 插件返回 C++ 接口对象，因此目标插件必须与 Host 使用 ABI 兼容的编译器、C++ 标准库和 Boost 版本。

## Boost.JSON / shared-library requirement

API v1 的 C ABI 只负责 `create/destroy` 入口，实际对象仍是 C++ `DeviceAdapter/OsAdapter`，接口中包含 Boost.JSON 类型。因此插件不能只编译模板单文件后留下未解析的 Boost.JSON 符号。

本目录提供 `boost_json_plugin_impl.cpp` 和独立 `CMakeLists.txt`。建议目标插件项目使用相同做法，或显式链接与 Host ABI 兼容的 Boost.JSON 库。Host 与插件必须使用兼容的编译器、C++ 标准库和 Boost 版本。

基线构建：

```bash
cmake -S backend-cpp/examples/adapter-sdk -B build/adapter-sdk -G Ninja
cmake --build build/adapter-sdk
```

Linux 下示例开启 `-Wl,-z,defs`，用于在交接阶段直接发现未解析符号。
