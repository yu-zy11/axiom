#pragma once

#include <string_view>

namespace axiom {

/// 进程内插件 SDK 与清单 `plugin_api_version` 对齐用的稳定版本串（与 `Kernel::plugin_sdk_api_version` 一致）。
inline constexpr std::string_view kPluginSdkApiVersion = "1.0";

}  // namespace axiom
