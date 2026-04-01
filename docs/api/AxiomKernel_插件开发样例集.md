# AxiomKernel 插件开发样例集

本文档提供 `AxiomKernel` 插件开发的样例和推荐边界，帮助后续扩展新的几何类型、修复器、导入导出器和分析器。

## 1. 文档目标

本文档用于说明：

- 插件如何声明能力
- 插件如何注册
- 插件如何返回结果与诊断
- 插件应该遵守哪些边界

## 2. 插件开发原则

- 插件只能通过公开 API 访问内核
- 插件不得依赖私有内存布局
- 插件失败必须返回结构化结果
- 插件输出建议经过验证器校验

## 3. 最小插件示例

### 3.1 修复器插件

```cpp
class RemoveTinyFeaturesPlugin : public IRepairPlugin {
public:
  std::string type_name() const override {
    return "remove_tiny_features";
  }

  Result<OpReport> run(BodyId body_id, RepairMode mode) override {
    OpReport report{};
    report.status = StatusCode::Ok;
    report.output = body_id;
    return Result<OpReport>{StatusCode::Ok, report, {}, DiagnosticId{0}};
  }
};
```

### 3.2 注册插件

```cpp
PluginManifest manifest;
manifest.name = "tiny-feature-repair";
manifest.version = "0.1.0";
manifest.vendor = "Axiom";
manifest.capabilities = {"repair"};

registry.register_repair_plugin(
  manifest,
  std::make_unique<RemoveTinyFeaturesPlugin>()
);
```

## 4. 宿主策略、能力发现与可诊断注册

### 4.1 `PluginHostPolicy`（`KernelConfig::plugin_host_policy`）

用于在**进程内**限制插件实例数量、约束清单名/`type_name` 唯一性、是否要求非空 `capabilities` 等。策略在 `Kernel` 构造时同步到 `PluginRegistry`；运行期可通过 `Kernel::set_plugin_host_policy` 更新。

**`PluginSandboxLevel`**（`PluginHostPolicy::sandbox_level`）：`None` 为默认；`Annotated` 仅影响能力发现/JSON 中的 `sandbox_level` 报告字段，**不提供 OS 级隔离**。未来若引入真实沙箱，可在此枚举上扩展并由宿主强制执行。

注册失败时，`PluginRegistry` 会在 `Result::warnings` 中附带稳定错误码字符串（如 `AXM-PLUGIN-E-0003` 等，见 `include/axiom/diag/error_codes.h` 与错误码字典）。`find_manifest` 未命中时带 **`AXM-PLUGIN-E-0001`**（`kPluginLoadFailure`）警告。

### 4.2 能力发现（CI / 工具）

- 文本行：`Kernel::plugin_discovery_report_lines()`（含 `AXM-PLUGIN-D-0001` 诊断快照语义）。
- JSON：`Kernel::plugin_discovery_report_json()`，字段与上述报告对齐，便于自动化解析。

### 4.3 需要 `DiagnosticService` 追溯时的注册方式

若希望注册/注销失败时**写入诊断存储**并得到非零 `diagnostic_id`（便于 `diagnostics().get` / 导出 JSON），请使用门面方法：

- `register_plugin_curve` / `register_plugin_repair` / `register_plugin_importer` / `register_plugin_exporter`
- `register_plugin_manifest_only`
- `unregister_plugin_curve` / `unregister_plugin_repair` / `unregister_plugin_importer` / `unregister_plugin_exporter` / `unregister_plugin_manifest`

若仅需内存内的 `Result`（含 `warnings`），可直接 `kernel.plugins().register_*` / `unregister_*`。

### 4.4 可诊断注销（`unregister_plugin_*`）

- 与 `register_plugin_*` 对称：`Kernel::unregister_plugin_curve` 等按 **`ICurvePlugin::type_name()`** 等实现侧类型名注销实例；`unregister_plugin_manifest` 按清单 **`name`** 删除条目（未删除任何条目则失败，码 **`AXM-PLUGIN-E-0008`**）。
- 需要仅 `Result::warnings` 时可调用 `plugins().unregister_curve_type` 等；空 `type_name` 或未命中实现时返回 **`InvalidInput`** 与稳定警告码。
- **`PluginManifest::implementation_type_name`**：带实现注册时若为空会写入插件的 `type_name`（去空白）；若已填写则必须与插件 `type_name` 一致，否则 **`AXM-PLUGIN-E-0003`**。按 `type_name` 注销曲线/修复/导入/导出实现时，会同时删除 **`implementation_type_name`** 与该 `type_name` 匹配的清单条目（`clear_plugins_keep_manifests` 仅删实现、不删清单，可能留下“孤儿”绑定字段，需宿主自行清理或再调 `unregister_plugin_manifest`）。

### 4.5 `plugin_api_version` 与验证入口

- 清单字段 **`PluginManifest::plugin_api_version`**：建议设为 `axiom::kPluginSdkApiVersion`（见 `include/axiom/plugin/plugin_sdk_version.h`），与 `Kernel::plugin_sdk_api_version()` 对齐。
- 当 **`PluginHostPolicy::require_plugin_api_version_match`** 为真时，注册前必须声明非空，并按 **`PluginHostPolicy::plugin_api_version_match_mode`** 与宿主 SDK API 比对，否则失败码 **`AXM-PLUGIN-E-0002`**（`kPluginVersionIncompatible`）。模式说明：`Exact` 为去空白后与 `kPluginSdkApiVersion` 完全一致；`SameMinor` 要求 `major.minor` 与宿主一致（允许 patch，如宿主 `1.0` 可接受 `1.0.3`）；`SameMajor` 仅要求 `major` 一致（更宽松，慎用）。
- **门禁/报告**：`Kernel::plugin_api_compatibility_report_lines()` 按清单输出 `state=ok|mismatch|unset`，便于 CI 检查未声明或过旧 API。
- **验证闭环**：插件修改或产出 `Body` 后，建议调用 **`Kernel::validate_after_plugin_mutation(body_id, ValidationMode)`**（语义同 `validate().validate_all`）。

### 4.6 仓库内可运行示例

构建示例目标 `axiom_minimal_plugin`（`examples/minimal_plugin.cpp`）：注册最小曲线插件、填写 `plugin_api_version` 并打印能力发现行。

## 5. 导入器插件示例

```cpp
class CustomImporterPlugin : public IImporterPlugin {
public:
  Result<BodyId> import_file(std::string_view path) override {
    return Result<BodyId>{StatusCode::NotImplemented, std::nullopt, {}, DiagnosticId{0}};
  }
};
```

## 6. 推荐返回策略

建议：

- 成功时返回标准结果对象
- 失败时返回错误码和诊断
- 不要抛裸异常作为常规失败路径

## 7. 结论

插件样例的核心不是展示复杂逻辑，而是把边界定清楚：插件可以扩展能力，但不能破坏内核一致性和可诊断性。
