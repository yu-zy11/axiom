#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "axiom/core/result.h"

namespace axiom {

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
    Result<void> register_curve_type(const PluginManifest& manifest, std::unique_ptr<ICurvePlugin> plugin);
    Result<void> register_repair_plugin(const PluginManifest& manifest, std::unique_ptr<IRepairPlugin> plugin);
    Result<void> register_importer(const PluginManifest& manifest, std::unique_ptr<IImporterPlugin> plugin);
    Result<void> register_exporter(const PluginManifest& manifest, std::unique_ptr<IExporterPlugin> plugin);
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
    std::vector<PluginManifest> manifests_;
    std::vector<std::unique_ptr<ICurvePlugin>> curve_plugins_;
    std::vector<std::unique_ptr<IRepairPlugin>> repair_plugins_;
    std::vector<std::unique_ptr<IImporterPlugin>> importer_plugins_;
    std::vector<std::unique_ptr<IExporterPlugin>> exporter_plugins_;
};

}  // namespace axiom
