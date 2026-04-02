#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
}

/// 判断已 trim 的声明版本是否与 `expected_sdk_api` 在给定模式下兼容（注册门禁与 `plugin_api_compatibility_report_lines` 共用语义）。
/// `Exact`：整串相等；`SameMajor`/`SameMinor`：按 SemVer 惯例忽略 `-` 预发布与 `+` 构建元数据后，再解析 `X[.Y[.Z]]` 数值比较。
bool plugin_api_version_declared_compatible(std::string_view declared_trimmed,
                                            std::string_view expected_sdk_api,
                                            PluginApiVersionMatchMode mode);

class ICurvePlugin {
public:
    virtual ~ICurvePlugin() = default;
    virtual std::string type_name() const = 0;
    virtual Result<CurveId> create(const PluginCurveDesc& desc) = 0;
};

class IRepairPlugin {
public:
    virtual ~IRepairPlugin() = default;
    virtual std::string type_name() const = 0;
    virtual Result<OpReport> run(BodyId body_id, RepairMode mode) = 0;
};

class IImporterPlugin {
public:
    virtual ~IImporterPlugin() = default;
    virtual std::string type_name() const = 0;
    virtual Result<BodyId> import_file(std::string_view path) = 0;
};

class IExporterPlugin {
public:
    virtual ~IExporterPlugin() = default;
    virtual std::string type_name() const = 0;
    virtual Result<void> export_file(BodyId body_id, std::string_view path) = 0;
};

class PluginRegistry {
public:
    void set_host_policy(const PluginHostPolicy& policy);
    Result<PluginHostPolicy> host_policy() const;

    /// 绑定宿主内核后，`invoke_registered_*` 可按 `PluginHostPolicy` 自动做体/曲线一致性校验（与 `Kernel::plugin_*` 门面语义对齐）。
    /// 未绑定或 `weak_ptr` 已失效时，策略开关不生效（与历史「仅调用插件实现」行为一致）。
    void bind_host_kernel_for_plugin_invocation(std::weak_ptr<detail::KernelState> kernel);
    void clear_host_kernel_binding();

    /// 按当前宿主策略校验清单（供预检或工具使用）。
    Result<void> validate_manifest(const PluginManifest& manifest) const;
    /// 对当前注册表内**全部**清单逐条调用 `validate_manifest`；若有失败返回**首次**失败的 `Result`，`failure_details`（若非空）按顺序追加 `"<name>: <message>"`。
    Result<void> validate_all_manifests(std::vector<std::string>* failure_details = nullptr) const;

    Result<void> register_curve_type(const PluginManifest& manifest, std::unique_ptr<ICurvePlugin> plugin);
    /// 按已注册实现的 `type_name()`（去空白）调用 `ICurvePlugin::create`；`desc.type_name` 若为空则填为 `implementation_type_name`，若非空则须与之一致（去空白后）；未找到实现时 `InvalidInput` + `kPluginNotRegistered`。
    Result<CurveId> invoke_registered_curve(std::string_view implementation_type_name, PluginCurveDesc desc);
    Result<void> register_repair_plugin(const PluginManifest& manifest, std::unique_ptr<IRepairPlugin> plugin);
    Result<void> register_importer(const PluginManifest& manifest, std::unique_ptr<IImporterPlugin> plugin);
    /// 按已注册实现的 `type_name()`（去空白）调用 `IImporterPlugin::import_file`；未找到实现时 `InvalidInput` + `kPluginNotRegistered`。
    Result<BodyId> invoke_registered_importer(std::string_view implementation_type_name, std::string_view path,
                                              ValidationMode validation_mode = ValidationMode::Standard);
    Result<void> register_exporter(const PluginManifest& manifest, std::unique_ptr<IExporterPlugin> plugin);
    /// 按已注册实现的 `type_name()`（去空白）调用 `IExporterPlugin::export_file`；未找到实现时 `InvalidInput` + `kPluginNotRegistered`。
    Result<void> invoke_registered_exporter(std::string_view implementation_type_name, BodyId body_id, std::string_view path,
                                              ValidationMode validation_mode = ValidationMode::Standard);
    /// 按已注册实现的 `type_name()`（去空白）调用 `IRepairPlugin::run`；未找到实现时 `InvalidInput` + `kPluginNotRegistered`。
    Result<OpReport> invoke_registered_repair(std::string_view implementation_type_name, BodyId body_id, RepairMode mode,
                                              ValidationMode validation_mode = ValidationMode::Standard);
    Result<std::uint64_t> curve_plugin_count() const;
    Result<std::uint64_t> repair_plugin_count() const;
    Result<std::uint64_t> importer_count() const;
    Result<std::uint64_t> exporter_count() const;
    Result<std::uint64_t> manifest_count() const;
    Result<bool> empty() const;
    Result<void> clear();
    Result<bool> has_manifest(std::string_view name) const;
    Result<PluginManifest> find_manifest(std::string_view name) const;
    Result<std::vector<std::string>> all_manifest_names() const;
    Result<std::vector<std::string>> all_capabilities() const;
    Result<std::vector<PluginManifest>> find_by_capability(std::string_view capability) const;
    Result<bool> has_curve_type(std::string_view type_name) const;
    Result<bool> has_repair_type(std::string_view type_name) const;
    Result<bool> has_importer_type(std::string_view type_name) const;
    Result<bool> has_exporter_type(std::string_view type_name) const;
    /// `type_name` 去空白后须非空，且须命中已注册实现；否则返回 `InvalidInput` 与稳定 `warnings`。
    Result<void> unregister_curve_type(std::string_view type_name);
    Result<void> unregister_repair_plugin(std::string_view type_name);
    Result<void> unregister_importer(std::string_view type_name);
    Result<void> unregister_exporter(std::string_view type_name);
    Result<std::unordered_map<std::string, std::uint64_t>> plugin_type_counts() const;
    Result<std::unordered_map<std::string, std::uint64_t>> capabilities_histogram() const;
    Result<void> register_manifest_only(const PluginManifest& manifest);
    Result<std::uint64_t> remove_manifest_by_name(std::string_view name);
    Result<std::uint64_t> deduplicate_manifests_by_name();
    Result<std::vector<std::string>> manifest_names_sorted() const;
    Result<bool> contains_vendor(std::string_view vendor) const;
    Result<std::vector<PluginManifest>> find_by_vendor(std::string_view vendor) const;
    Result<PluginManifest> latest_manifest() const;
    Result<std::uint64_t> total_plugin_slots() const;
    Result<std::vector<std::string>> manifest_names_unique() const;
    Result<std::vector<std::string>> vendor_names() const;
    Result<std::unordered_map<std::string, std::uint64_t>> vendor_histogram() const;
    Result<std::uint64_t> capability_count_total() const;
    Result<std::vector<PluginManifest>> manifests_sorted_by_name() const;
    Result<std::vector<PluginManifest>> manifests_sorted_by_vendor() const;
    Result<std::vector<std::pair<std::string, std::uint64_t>>>
    top_capabilities(std::uint64_t limit) const;
    Result<std::vector<PluginManifest>>
    manifests_with_any_capability(std::span<const std::string> capabilities) const;
    Result<std::vector<PluginManifest>>
    manifests_without_capability(std::string_view capability) const;
    Result<bool>
    manifest_name_exists_case_insensitive(std::string_view name) const;
    Result<bool> capability_exists(std::string_view capability) const;
    Result<std::vector<std::string>> plugin_types_present() const;
    /// 全部已注册实现 `type_name()`（去空白、非空）的**有序去重**列表，供 `invoke_registered_*` 与运维发现对齐。
    Result<std::vector<std::string>> registered_implementation_type_names_sorted() const;
    Result<std::vector<std::string>> infer_supported_io_formats() const;
    Result<bool> supports_io_format(std::string_view format) const;
    Result<std::string> manifest_to_text(std::string_view name) const;
    Result<std::vector<std::string>> all_manifests_to_text_lines() const;
    Result<void> export_manifests_txt(std::string_view path) const;
    Result<void> export_capabilities_txt(std::string_view path) const;
    Result<void> export_summary_txt(std::string_view path) const;
    Result<void> clear_plugins_keep_manifests();
    Result<void> clear_manifests_keep_plugins();
    Result<std::uint64_t> remove_manifests_by_vendor(std::string_view vendor);
    Result<std::uint64_t> remove_manifests_without_capabilities();
    Result<std::uint64_t> count_manifests_with_capability(std::string_view capability) const;
    Result<std::uint64_t> count_manifests_by_vendor(std::string_view vendor) const;
    Result<std::string> first_manifest_name() const;
    Result<std::string> last_manifest_name() const;
    Result<std::vector<PluginManifest>> manifests_paginated(std::uint64_t offset,
                                                            std::uint64_t limit) const;
    Result<std::string> registry_summary_line() const;

private:
    Result<void> preflight_register_with_plugin(const PluginManifest& manifest, std::string_view impl_type_name,
                                                Result<bool> (PluginRegistry::*has_impl)(std::string_view) const) const;
    Result<void> preflight_register_manifest_only(const PluginManifest& manifest) const;

    std::weak_ptr<detail::KernelState> host_kernel_{};
    PluginHostPolicy host_policy_{};
    std::vector<PluginManifest> manifests_;
    std::vector<std::unique_ptr<ICurvePlugin>> curve_plugins_;
    std::vector<std::unique_ptr<IRepairPlugin>> repair_plugins_;
    std::vector<std::unique_ptr<IImporterPlugin>> importer_plugins_;
    std::vector<std::unique_ptr<IExporterPlugin>> exporter_plugins_;
};

}  // namespace axiom
