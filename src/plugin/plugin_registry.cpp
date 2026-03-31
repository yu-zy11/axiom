#include "axiom/plugin/plugin_registry.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace axiom {
namespace {
std::string lower_copy(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}
}

Result<void> PluginRegistry::register_curve_type(const PluginManifest& manifest, std::unique_ptr<ICurvePlugin> plugin) {
    if (!plugin) {
        return error_void(StatusCode::InvalidInput);
    }
    manifests_.push_back(manifest);
    curve_plugins_.push_back(std::move(plugin));
    return ok_void();
}

Result<void> PluginRegistry::register_repair_plugin(const PluginManifest& manifest, std::unique_ptr<IRepairPlugin> plugin) {
    if (!plugin) {
        return error_void(StatusCode::InvalidInput);
    }
    manifests_.push_back(manifest);
    repair_plugins_.push_back(std::move(plugin));
    return ok_void();
}

Result<void> PluginRegistry::register_importer(const PluginManifest& manifest, std::unique_ptr<IImporterPlugin> plugin) {
    if (!plugin) {
        return error_void(StatusCode::InvalidInput);
    }
    manifests_.push_back(manifest);
    importer_plugins_.push_back(std::move(plugin));
    return ok_void();
}

Result<void> PluginRegistry::register_exporter(const PluginManifest& manifest, std::unique_ptr<IExporterPlugin> plugin) {
    if (!plugin) {
        return error_void(StatusCode::InvalidInput);
    }
    manifests_.push_back(manifest);
    exporter_plugins_.push_back(std::move(plugin));
    return ok_void();
}

Result<std::uint64_t> PluginRegistry::curve_plugin_count() const {
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(curve_plugins_.size()));
}

Result<std::uint64_t> PluginRegistry::repair_plugin_count() const {
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(repair_plugins_.size()));
}

Result<std::uint64_t> PluginRegistry::importer_count() const {
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(importer_plugins_.size()));
}

Result<std::uint64_t> PluginRegistry::exporter_count() const {
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(exporter_plugins_.size()));
}

Result<std::uint64_t> PluginRegistry::manifest_count() const {
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(manifests_.size()));
}

Result<bool> PluginRegistry::empty() const {
    return ok_result(manifests_.empty() && curve_plugins_.empty() && repair_plugins_.empty() &&
                     importer_plugins_.empty() && exporter_plugins_.empty());
}

Result<void> PluginRegistry::clear() {
    manifests_.clear();
    curve_plugins_.clear();
    repair_plugins_.clear();
    importer_plugins_.clear();
    exporter_plugins_.clear();
    return ok_void();
}

Result<bool> PluginRegistry::has_manifest(std::string_view name) const {
    return ok_result(std::any_of(manifests_.begin(), manifests_.end(),
                                 [name](const PluginManifest& m){ return m.name == name; }));
}

Result<PluginManifest> PluginRegistry::find_manifest(std::string_view name) const {
    const auto it = std::find_if(manifests_.begin(), manifests_.end(),
                                 [name](const PluginManifest& m){ return m.name == name; });
    if (it == manifests_.end()) return error_result<PluginManifest>(StatusCode::InvalidInput);
    return ok_result(*it);
}

Result<std::vector<std::string>> PluginRegistry::all_manifest_names() const {
    std::vector<std::string> out;
    out.reserve(manifests_.size());
    for (const auto& m : manifests_) out.push_back(m.name);
    return ok_result(std::move(out));
}

Result<std::vector<std::string>> PluginRegistry::all_capabilities() const {
    std::set<std::string> caps;
    for (const auto& m : manifests_) for (const auto& c : m.capabilities) caps.insert(c);
    return ok_result(std::vector<std::string>(caps.begin(), caps.end()));
}

Result<std::vector<PluginManifest>> PluginRegistry::find_by_capability(std::string_view capability) const {
    std::vector<PluginManifest> out;
    for (const auto& m : manifests_) {
        if (std::any_of(m.capabilities.begin(), m.capabilities.end(),
                        [capability](const std::string& c){ return c == capability; })) out.push_back(m);
    }
    return ok_result(std::move(out));
}

Result<bool> PluginRegistry::has_curve_type(std::string_view type_name) const {
    return ok_result(std::any_of(curve_plugins_.begin(), curve_plugins_.end(),
                                 [type_name](const auto& p){ return p && p->type_name() == type_name; }));
}

Result<bool> PluginRegistry::has_repair_type(std::string_view type_name) const {
    return ok_result(std::any_of(repair_plugins_.begin(), repair_plugins_.end(),
                                 [type_name](const auto& p){ return p && p->type_name() == type_name; }));
}

Result<bool> PluginRegistry::has_importer_type(std::string_view type_name) const {
    return ok_result(std::any_of(importer_plugins_.begin(), importer_plugins_.end(),
                                 [type_name](const auto& p){ return p && p->type_name() == type_name; }));
}

Result<bool> PluginRegistry::has_exporter_type(std::string_view type_name) const {
    return ok_result(std::any_of(exporter_plugins_.begin(), exporter_plugins_.end(),
                                 [type_name](const auto& p){ return p && p->type_name() == type_name; }));
}

Result<void> PluginRegistry::unregister_curve_type(std::string_view type_name) {
    curve_plugins_.erase(std::remove_if(curve_plugins_.begin(), curve_plugins_.end(),
                                        [type_name](const auto& p){ return p && p->type_name() == type_name; }),
                         curve_plugins_.end());
    return ok_void();
}

Result<void> PluginRegistry::unregister_repair_plugin(std::string_view type_name) {
    repair_plugins_.erase(std::remove_if(repair_plugins_.begin(), repair_plugins_.end(),
                                         [type_name](const auto& p){ return p && p->type_name() == type_name; }),
                          repair_plugins_.end());
    return ok_void();
}

Result<void> PluginRegistry::unregister_importer(std::string_view type_name) {
    importer_plugins_.erase(std::remove_if(importer_plugins_.begin(), importer_plugins_.end(),
                                           [type_name](const auto& p){ return p && p->type_name() == type_name; }),
                            importer_plugins_.end());
    return ok_void();
}

Result<void> PluginRegistry::unregister_exporter(std::string_view type_name) {
    exporter_plugins_.erase(std::remove_if(exporter_plugins_.begin(), exporter_plugins_.end(),
                                           [type_name](const auto& p){ return p && p->type_name() == type_name; }),
                            exporter_plugins_.end());
    return ok_void();
}

Result<std::unordered_map<std::string, std::uint64_t>> PluginRegistry::plugin_type_counts() const {
    std::unordered_map<std::string, std::uint64_t> out;
    out["curve"] = static_cast<std::uint64_t>(curve_plugins_.size());
    out["repair"] = static_cast<std::uint64_t>(repair_plugins_.size());
    out["importer"] = static_cast<std::uint64_t>(importer_plugins_.size());
    out["exporter"] = static_cast<std::uint64_t>(exporter_plugins_.size());
    return ok_result(std::move(out));
}

Result<std::unordered_map<std::string, std::uint64_t>> PluginRegistry::capabilities_histogram() const {
    std::unordered_map<std::string, std::uint64_t> out;
    for (const auto& m : manifests_) for (const auto& c : m.capabilities) ++out[c];
    return ok_result(std::move(out));
}

Result<void> PluginRegistry::register_manifest_only(const PluginManifest& manifest) {
    manifests_.push_back(manifest);
    return ok_void();
}

Result<std::uint64_t> PluginRegistry::remove_manifest_by_name(std::string_view name) {
    const auto before = manifests_.size();
    manifests_.erase(std::remove_if(manifests_.begin(), manifests_.end(),
                                    [name](const PluginManifest& m){ return m.name == name; }),
                     manifests_.end());
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(before - manifests_.size()));
}

Result<std::uint64_t> PluginRegistry::deduplicate_manifests_by_name() {
    std::set<std::string> seen;
    const auto before = manifests_.size();
    manifests_.erase(std::remove_if(manifests_.begin(), manifests_.end(),
                                    [&seen](const PluginManifest& m){ return !seen.insert(m.name).second; }),
                     manifests_.end());
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(before - manifests_.size()));
}

Result<std::vector<std::string>> PluginRegistry::manifest_names_sorted() const {
    std::vector<std::string> out;
    for (const auto& m : manifests_) out.push_back(m.name);
    std::sort(out.begin(), out.end());
    return ok_result(std::move(out));
}

Result<bool> PluginRegistry::contains_vendor(std::string_view vendor) const {
    return ok_result(std::any_of(manifests_.begin(), manifests_.end(),
                                 [vendor](const PluginManifest& m){ return m.vendor == vendor; }));
}

Result<std::vector<PluginManifest>> PluginRegistry::find_by_vendor(std::string_view vendor) const {
    std::vector<PluginManifest> out;
    for (const auto& m : manifests_) if (m.vendor == vendor) out.push_back(m);
    return ok_result(std::move(out));
}

Result<PluginManifest> PluginRegistry::latest_manifest() const {
    if (manifests_.empty()) return error_result<PluginManifest>(StatusCode::InvalidInput);
    return ok_result(manifests_.back());
}

Result<std::uint64_t> PluginRegistry::total_plugin_slots() const {
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(
        curve_plugins_.size() + repair_plugins_.size() + importer_plugins_.size() + exporter_plugins_.size()));
}

Result<std::vector<std::string>> PluginRegistry::manifest_names_unique() const {
    std::set<std::string> uniq;
    for (const auto& m : manifests_) uniq.insert(m.name);
    return ok_result(std::vector<std::string>(uniq.begin(), uniq.end()));
}

Result<std::vector<std::string>> PluginRegistry::vendor_names() const {
    std::set<std::string> uniq;
    for (const auto& m : manifests_) uniq.insert(m.vendor);
    return ok_result(std::vector<std::string>(uniq.begin(), uniq.end()));
}

Result<std::unordered_map<std::string, std::uint64_t>> PluginRegistry::vendor_histogram() const {
    std::unordered_map<std::string, std::uint64_t> out;
    for (const auto& m : manifests_) ++out[m.vendor];
    return ok_result(std::move(out));
}

Result<std::uint64_t> PluginRegistry::capability_count_total() const {
    std::uint64_t total = 0;
    for (const auto& m : manifests_) total += static_cast<std::uint64_t>(m.capabilities.size());
    return ok_result<std::uint64_t>(total);
}

Result<std::vector<PluginManifest>> PluginRegistry::manifests_sorted_by_name() const {
    auto out = manifests_;
    std::sort(out.begin(), out.end(), [](const PluginManifest& a, const PluginManifest& b) { return a.name < b.name; });
    return ok_result(std::move(out));
}

Result<std::vector<PluginManifest>> PluginRegistry::manifests_sorted_by_vendor() const {
    auto out = manifests_;
    std::sort(out.begin(), out.end(), [](const PluginManifest& a, const PluginManifest& b) {
        return a.vendor == b.vendor ? a.name < b.name : a.vendor < b.vendor;
    });
    return ok_result(std::move(out));
}

Result<std::vector<std::pair<std::string, std::uint64_t>>> PluginRegistry::top_capabilities(std::uint64_t limit) const {
    std::unordered_map<std::string, std::uint64_t> hist;
    for (const auto& m : manifests_) for (const auto& c : m.capabilities) ++hist[c];
    std::vector<std::pair<std::string, std::uint64_t>> out(hist.begin(), hist.end());
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.second == b.second ? a.first < b.first : a.second > b.second;
    });
    if (limit < out.size()) out.resize(static_cast<std::size_t>(limit));
    return ok_result(std::move(out));
}

Result<std::vector<PluginManifest>>
PluginRegistry::manifests_with_any_capability(std::span<const std::string> capabilities) const {
    std::vector<PluginManifest> out;
    for (const auto& m : manifests_) {
        if (std::any_of(m.capabilities.begin(), m.capabilities.end(), [&capabilities](const std::string& c) {
                return std::find(capabilities.begin(), capabilities.end(), c) != capabilities.end();
            })) {
            out.push_back(m);
        }
    }
    return ok_result(std::move(out));
}

Result<std::vector<PluginManifest>> PluginRegistry::manifests_without_capability(std::string_view capability) const {
    std::vector<PluginManifest> out;
    for (const auto& m : manifests_) {
        if (std::find(m.capabilities.begin(), m.capabilities.end(), capability) == m.capabilities.end()) out.push_back(m);
    }
    return ok_result(std::move(out));
}

Result<bool> PluginRegistry::manifest_name_exists_case_insensitive(std::string_view name) const {
    const auto target = lower_copy(std::string(name));
    return ok_result(std::any_of(manifests_.begin(), manifests_.end(), [&target](const PluginManifest& m) {
        return lower_copy(m.name) == target;
    }));
}

Result<bool> PluginRegistry::capability_exists(std::string_view capability) const {
    for (const auto& m : manifests_) {
        if (std::find(m.capabilities.begin(), m.capabilities.end(), capability) != m.capabilities.end()) return ok_result(true);
    }
    return ok_result(false);
}

Result<std::vector<std::string>> PluginRegistry::plugin_types_present() const {
    std::vector<std::string> out;
    if (!curve_plugins_.empty()) out.push_back("curve");
    if (!repair_plugins_.empty()) out.push_back("repair");
    if (!importer_plugins_.empty()) out.push_back("importer");
    if (!exporter_plugins_.empty()) out.push_back("exporter");
    return ok_result(std::move(out));
}

Result<std::vector<std::string>> PluginRegistry::infer_supported_io_formats() const {
    std::set<std::string> out;
    for (const auto& m : manifests_) {
        for (const auto& c : m.capabilities) {
            const std::string prefix = "io:";
            if (c.rfind(prefix, 0) == 0 && c.size() > prefix.size()) out.insert(c.substr(prefix.size()));
        }
    }
    return ok_result(std::vector<std::string>(out.begin(), out.end()));
}

Result<bool> PluginRegistry::supports_io_format(std::string_view format) const {
    const auto fmts = infer_supported_io_formats();
    if (fmts.status != StatusCode::Ok || !fmts.value.has_value()) return error_result<bool>(fmts.status, fmts.diagnostic_id);
    return ok_result(std::find(fmts.value->begin(), fmts.value->end(), format) != fmts.value->end());
}

Result<std::string> PluginRegistry::manifest_to_text(std::string_view name) const {
    const auto manifest = find_manifest(name);
    if (manifest.status != StatusCode::Ok || !manifest.value.has_value()) return error_result<std::string>(manifest.status, manifest.diagnostic_id);
    std::string out = manifest.value->name + "@" + manifest.value->version + " by " + manifest.value->vendor + " [";
    for (std::size_t i = 0; i < manifest.value->capabilities.size(); ++i) {
        out += manifest.value->capabilities[i];
        if (i + 1 < manifest.value->capabilities.size()) out += ",";
    }
    out += "]";
    return ok_result(std::move(out));
}

Result<std::vector<std::string>> PluginRegistry::all_manifests_to_text_lines() const {
    std::vector<std::string> out;
    out.reserve(manifests_.size());
    for (const auto& m : manifests_) {
        std::string line = m.name + "@" + m.version + " by " + m.vendor + " [";
        for (std::size_t i = 0; i < m.capabilities.size(); ++i) {
            line += m.capabilities[i];
            if (i + 1 < m.capabilities.size()) line += ",";
        }
        line += "]";
        out.push_back(std::move(line));
    }
    return ok_result(std::move(out));
}

Result<void> PluginRegistry::export_manifests_txt(std::string_view path) const {
    const auto lines = all_manifests_to_text_lines();
    if (lines.status != StatusCode::Ok || !lines.value.has_value()) return error_result<void>(lines.status, lines.diagnostic_id);
    std::ofstream out{std::string(path)};
    if (!out) return error_void(StatusCode::OperationFailed);
    for (const auto& line : *lines.value) out << line << "\n";
    return ok_void();
}

Result<void> PluginRegistry::export_capabilities_txt(std::string_view path) const {
    const auto hist = capabilities_histogram();
    if (hist.status != StatusCode::Ok || !hist.value.has_value()) return error_result<void>(hist.status, hist.diagnostic_id);
    std::vector<std::pair<std::string, std::uint64_t>> entries(hist.value->begin(), hist.value->end());
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b){ return a.first < b.first; });
    std::ofstream out{std::string(path)};
    if (!out) return error_void(StatusCode::OperationFailed);
    for (const auto& e : entries) out << e.first << ": " << e.second << "\n";
    return ok_void();
}

Result<void> PluginRegistry::export_summary_txt(std::string_view path) const {
    std::ofstream out{std::string(path)};
    if (!out) return error_void(StatusCode::OperationFailed);
    const auto total = total_plugin_slots();
    const auto manifests = manifest_count();
    out << "plugins=" << total.value.value_or(0) << "\n";
    out << "manifests=" << manifests.value.value_or(0) << "\n";
    const auto types = plugin_type_counts();
    if (types.status == StatusCode::Ok && types.value.has_value()) {
        for (const auto& kv : *types.value) out << kv.first << "=" << kv.second << "\n";
    }
    return ok_void();
}

Result<void> PluginRegistry::clear_plugins_keep_manifests() {
    curve_plugins_.clear();
    repair_plugins_.clear();
    importer_plugins_.clear();
    exporter_plugins_.clear();
    return ok_void();
}

Result<void> PluginRegistry::clear_manifests_keep_plugins() {
    manifests_.clear();
    return ok_void();
}

Result<std::uint64_t> PluginRegistry::remove_manifests_by_vendor(std::string_view vendor) {
    const auto before = manifests_.size();
    manifests_.erase(std::remove_if(manifests_.begin(), manifests_.end(), [vendor](const PluginManifest& m) {
        return m.vendor == vendor;
    }), manifests_.end());
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(before - manifests_.size()));
}

Result<std::uint64_t> PluginRegistry::remove_manifests_without_capabilities() {
    const auto before = manifests_.size();
    manifests_.erase(std::remove_if(manifests_.begin(), manifests_.end(), [](const PluginManifest& m) {
        return m.capabilities.empty();
    }), manifests_.end());
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(before - manifests_.size()));
}

Result<std::uint64_t> PluginRegistry::count_manifests_with_capability(std::string_view capability) const {
    std::uint64_t count = 0;
    for (const auto& m : manifests_) {
        if (std::find(m.capabilities.begin(), m.capabilities.end(), capability) != m.capabilities.end()) ++count;
    }
    return ok_result<std::uint64_t>(count);
}

Result<std::uint64_t> PluginRegistry::count_manifests_by_vendor(std::string_view vendor) const {
    std::uint64_t count = 0;
    for (const auto& m : manifests_) if (m.vendor == vendor) ++count;
    return ok_result<std::uint64_t>(count);
}

Result<std::string> PluginRegistry::first_manifest_name() const {
    if (manifests_.empty()) return ok_result(std::string{});
    return ok_result(manifests_.front().name);
}

Result<std::string> PluginRegistry::last_manifest_name() const {
    if (manifests_.empty()) return ok_result(std::string{});
    return ok_result(manifests_.back().name);
}

Result<std::vector<PluginManifest>> PluginRegistry::manifests_paginated(std::uint64_t offset, std::uint64_t limit) const {
    std::vector<PluginManifest> out;
    if (offset >= manifests_.size() || limit == 0) return ok_result(out);
    const auto end = std::min<std::uint64_t>(static_cast<std::uint64_t>(manifests_.size()), offset + limit);
    for (std::uint64_t i = offset; i < end; ++i) out.push_back(manifests_[static_cast<std::size_t>(i)]);
    return ok_result(std::move(out));
}

Result<std::string> PluginRegistry::registry_summary_line() const {
    const auto m = manifest_count().value.value_or(0);
    const auto p = total_plugin_slots().value.value_or(0);
    const auto c = capability_count_total().value.value_or(0);
    return ok_result("manifests=" + std::to_string(m) + ", plugins=" + std::to_string(p) + ", capabilities=" + std::to_string(c));
}

}  // namespace axiom
