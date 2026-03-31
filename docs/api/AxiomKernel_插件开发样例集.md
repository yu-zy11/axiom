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

## 4. 导入器插件示例

```cpp
class CustomImporterPlugin : public IImporterPlugin {
public:
  Result<BodyId> import_file(std::string_view path) override {
    return Result<BodyId>{StatusCode::NotImplemented, std::nullopt, {}, DiagnosticId{0}};
  }
};
```

## 5. 推荐返回策略

建议：

- 成功时返回标准结果对象
- 失败时返回错误码和诊断
- 不要抛裸异常作为常规失败路径

## 6. 结论

插件样例的核心不是展示复杂逻辑，而是把边界定清楚：插件可以扩展能力，但不能破坏内核一致性和可诊断性。
