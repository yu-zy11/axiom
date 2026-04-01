// 最小插件宿主示例：展示清单注册、宿主策略与能力发现（进程内，无 OS 级隔离）。
#include <iostream>
#include <string>

#include "axiom/plugin/plugin_sdk_version.h"
#include "axiom/sdk/kernel.h"

namespace {

class DemoCurvePlugin final : public axiom::ICurvePlugin {
public:
    std::string type_name() const override { return "demo_line"; }
    axiom::Result<axiom::CurveId> create(const axiom::PluginCurveDesc&) override {
        return axiom::error_result<axiom::CurveId>(axiom::StatusCode::NotImplemented);
    }
};

}  // namespace

int main() {
    axiom::Kernel kernel;

    axiom::PluginManifest manifest;
    manifest.name = "demo_curve_plugin";
    manifest.version = "0.1.0";
    manifest.vendor = "example";
    manifest.capabilities = {"curve", "io:demo"};
    manifest.plugin_api_version = std::string(axiom::kPluginSdkApiVersion);

    auto reg = kernel.plugins().register_curve_type(manifest, std::make_unique<DemoCurvePlugin>());
    if (reg.status != axiom::StatusCode::Ok) {
        std::cerr << "register_curve_type failed\n";
        return 1;
    }

    const auto caps = kernel.plugin_capabilities();
    const auto disc = kernel.plugin_discovery_report_lines();
    const auto api = kernel.plugin_sdk_api_version();
    if (caps.status != axiom::StatusCode::Ok || !caps.value.has_value() || caps.value->empty()) {
        std::cerr << "plugin_capabilities unexpected\n";
        return 1;
    }
    if (disc.status != axiom::StatusCode::Ok || !disc.value.has_value() || disc.value->size() < 4) {
        std::cerr << "plugin_discovery_report_lines unexpected\n";
        return 1;
    }
    if (api.status != axiom::StatusCode::Ok || !api.value.has_value() || api.value->empty()) {
        std::cerr << "plugin_sdk_api_version unexpected\n";
        return 1;
    }

    for (const auto& line : *disc.value) {
        std::cout << line << '\n';
    }
    return 0;
}
