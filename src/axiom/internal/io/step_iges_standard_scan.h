#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace axiom::io_internal {

/// 从 ISO-10303-21 文本的 DATA 段扫描 `#id=TYPE(...);` 实例（不含 EXPRESS 语义），生成简短摘要。
/// @return 是否在 DATA 段内至少完成一次扫描尝试（含解析到 0 个实例的情况）。
bool summarize_step_standard_file_entities(std::string_view file_text, std::string& out_message,
                                             std::size_t& out_parsed_instance_count);

/// 从 IGES 固定列文本中统计疑似 Directory Entry 行及实体类型号（字段 1，列 1–8）频度。
bool summarize_iges_standard_file_entities(std::string_view file_text, std::string& out_message,
                                           std::size_t& out_directory_entry_lines);

}  // namespace axiom::io_internal
