# 贡献指南

IDIS 采用分层架构。提交代码时，应保持用户界面、领域逻辑、应用服务、设备协议适配和操作系统适配之间的职责边界，避免将设备厂商差异、操作系统差异或现场临时逻辑直接写入通用业务层。

## 开发原则

1. 一次提交应尽量聚焦一个明确问题或一个完整功能。
2. 公共接口、领域模型和配置结构发生变化时，应同步更新相关测试与文档。
3. 设备接入优先通过 `DeviceAdapter` 扩展。
4. 协议或厂商差异优先通过 `ProtocolAdapter` 扩展。
5. 操作系统相关能力优先通过 `OsAdapter` 扩展。
6. 前端业务层不直接依赖具体驱动、厂商 SDK 或操作系统私有接口。
7. 新增配置项应提供合理默认值或示例配置，不应写入真实生产凭据。

## 提交前检查

前端相关修改：

```bash
npm run check
```

C++ 后端相关修改：

```bash
cmake -S backend-cpp -B build/backend -G Ninja
cmake --build build/backend
ctest --test-dir build/backend --output-on-failure
```

涉及跨层接口时，应同时检查调用方、实现方、配置文件和测试用例的一致性。

## 提交内容限制

请勿向仓库提交：

- `node_modules/`、构建目录、缓存和自动生成产物；
- 本地数据库、运行日志和临时调试文件；
- 工厂现场原始数据或内部业务数据；
- 账号、密码、访问令牌、API Key、SSH 私钥等凭据；
- 内部网络地址、生产拓扑和未脱敏配置；
- 未取得再分发授权的厂商 SDK、驱动或二进制文件。

## 提交信息

建议使用清晰、可追溯的提交说明，例如：

```text
feat: add device health monitoring
fix: correct alarm acknowledgement state
refactor: isolate protocol adapter boundary
docs: update deployment guide
test: add command runtime coverage
```

提交说明应准确描述实际变更，不使用与内容无关的笼统表述。
