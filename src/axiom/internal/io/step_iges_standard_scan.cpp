#include "axiom/internal/io/step_iges_standard_scan.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace axiom::io_internal {
namespace {

std::size_t find_ci_substr(std::string_view hay, std::string_view needle) {
    if (needle.empty() || needle.size() > hay.size()) {
        return std::string_view::npos;
    }
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (std::tolower(static_cast<unsigned char>(hay[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) {
            return i;
        }
    }
    return std::string_view::npos;
}

std::string strip_c_style_block_comments(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    bool in_comment = false;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (!in_comment && i + 1 < in.size() && in[i] == '/' && in[i + 1] == '*') {
            in_comment = true;
            ++i;
            continue;
        }
        if (in_comment && i + 1 < in.size() && in[i] == '*' && in[i + 1] == '/') {
            in_comment = false;
            ++i;
            continue;
        }
        if (!in_comment) {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::optional<std::string> extract_step_data_stripped(std::string_view text) {
    const std::size_t data_pos = find_ci_substr(text, "DATA;");
    if (data_pos == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t search_from = data_pos + 5;
    const std::size_t endsec = find_ci_substr(text.substr(search_from), "ENDSEC;");
    if (endsec == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view data_chunk = text.substr(search_from, endsec);
    return strip_c_style_block_comments(data_chunk);
}

void skip_ws(const char*& p, const char* e) {
    while (p < e && std::isspace(static_cast<unsigned char>(*p)) != 0) {
        ++p;
    }
}

void skip_step_rhs(const char*& p, const char* e) {
    skip_ws(p, e);
    if (p >= e) {
        return;
    }
    if (*p == ';') {
        ++p;
        return;
    }
    int depth = 0;
    bool in_string = false;
    while (p < e) {
        const char c = *p;
        if (in_string) {
            if (c == '\'') {
                if (p + 1 < e && p[1] == '\'') {
                    p += 2;
                    continue;
                }
                in_string = false;
                ++p;
                continue;
            }
            ++p;
            continue;
        }
        if (c == '\'') {
            in_string = true;
            ++p;
            continue;
        }
        if (c == '(') {
            ++depth;
            ++p;
            continue;
        }
        if (c == ')') {
            if (depth > 0) {
                --depth;
            }
            ++p;
            continue;
        }
        if (c == ';' && depth == 0) {
            ++p;
            return;
        }
        ++p;
    }
}

/// 自 `p` 指向的 `#` 起尝试解析一个实例；成功则 `p` 移到分号之后。
bool parse_step_instance_at_hash(const char*& p, const char* e, std::string& type_out) {
    const char* const start = p;
    if (p >= e || *p != '#') {
        return false;
    }
    ++p;
    if (p >= e || std::isdigit(static_cast<unsigned char>(*p)) == 0) {
        p = start;
        return false;
    }
    while (p < e && std::isdigit(static_cast<unsigned char>(*p)) != 0) {
        ++p;
    }
    skip_ws(p, e);
    if (p >= e || *p != '=') {
        p = start;
        return false;
    }
    ++p;
    skip_ws(p, e);
    if (p >= e || (std::isalpha(static_cast<unsigned char>(*p)) == 0 && *p != '_')) {
        p = start;
        return false;
    }
    const char* tb = p;
    while (p < e && (std::isalnum(static_cast<unsigned char>(*p)) != 0 || *p == '_')) {
        ++p;
    }
    type_out.assign(tb, p);
    skip_step_rhs(p, e);
    return true;
}

bool iges_line_looks_like_directory_entry(std::string_view line) {
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0) {
        line.remove_suffix(1);
    }
    if (line.size() >= 73) {
        const char c = line[72];
        if (c == 'D' || c == 'd') {
            return true;
        }
    }
    if (line.size() == 80) {
        static const std::regex k_de_tail(R"([0-9]{1,9}D[0-9]{1,9}\s*$)");
        return std::regex_search(std::string(line), k_de_tail);
    }
    return false;
}

int parse_iges_int_field8(std::string_view line) {
    if (line.size() < 8) {
        return 0;
    }
    std::string_view field = line.substr(0, 8);
    while (!field.empty() && std::isspace(static_cast<unsigned char>(field.front())) != 0) {
        field.remove_prefix(1);
    }
    while (!field.empty() && std::isspace(static_cast<unsigned char>(field.back())) != 0) {
        field.remove_suffix(1);
    }
    if (field.empty()) {
        return 0;
    }
    int v = 0;
    for (char ch : field) {
        if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
            return 0;
        }
        v = v * 10 + (ch - '0');
        if (v > 10000000) {
            return 10000000;
        }
    }
    return v;
}

}  // namespace

bool summarize_step_standard_file_entities(std::string_view file_text, std::string& out_message,
                                           std::size_t& out_parsed_instance_count) {
    out_message.clear();
    out_parsed_instance_count = 0;
    const auto data_opt = extract_step_data_stripped(file_text);
    if (!data_opt.has_value()) {
        return false;
    }
    const std::string& data = *data_opt;
    const char* p = data.data();
    const char* const end = p + data.size();

    std::unordered_map<std::string, std::size_t> counts;
    constexpr std::size_t k_max_instances = 200000;
    bool truncated = false;

    while (p < end) {
        skip_ws(p, end);
        if (p >= end) {
            break;
        }
        if (*p != '#') {
            ++p;
            continue;
        }
        std::string type;
        if (!parse_step_instance_at_hash(p, end, type)) {
            ++p;
            continue;
        }
        ++counts[type];
        ++out_parsed_instance_count;
        if (out_parsed_instance_count >= k_max_instances) {
            truncated = true;
            break;
        }
    }

    using Pair = std::pair<std::string, std::size_t>;
    std::vector<Pair> sorted;
    sorted.reserve(counts.size());
    for (const auto& kv : counts) {
        sorted.push_back(kv);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Pair& a, const Pair& b) {
        if (a.second != b.second) {
            return a.second > b.second;
        }
        return a.first < b.first;
    });

    constexpr std::size_t k_top = 16;
    std::ostringstream oss;
    oss << "STEP 物理层扫描（未物化几何/拓扑）：解析实例 " << out_parsed_instance_count;
    if (truncated) {
        oss << "（已达扫描上限，可能截断）";
    }
    oss << "；EXPRESS 类型频度 Top";
    if (sorted.empty()) {
        oss << "：无（DATA 段未识别到 `#id=TYPE` 实例，可能为多段/非典型换行）";
    } else {
        oss << "：";
        const std::size_t n = std::min(sorted.size(), k_top);
        for (std::size_t i = 0; i < n; ++i) {
            if (i) {
                oss << ", ";
            }
            oss << sorted[i].first << "=" << sorted[i].second;
        }
        if (sorted.size() > k_top) {
            oss << ", ...";
        }
    }
    oss << "。完整 BRep 物化需集成 STEP 内核（如 STEPcode/Open CASCADE）。";
    out_message = oss.str();
    return true;
}

bool summarize_iges_standard_file_entities(std::string_view file_text, std::string& out_message,
                                           std::size_t& out_directory_entry_lines) {
    out_message.clear();
    out_directory_entry_lines = 0;
    std::unordered_map<int, std::size_t> type_counts;
    std::size_t line_start = 0;
    for (std::size_t i = 0; i <= file_text.size(); ++i) {
        if (i < file_text.size() && file_text[i] != '\n') {
            continue;
        }
        std::string_view line = file_text.substr(line_start, i - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        line_start = i + 1;
        if (!iges_line_looks_like_directory_entry(line)) {
            continue;
        }
        ++out_directory_entry_lines;
        const int et = parse_iges_int_field8(line);
        ++type_counts[et];
    }

    using Ipair = std::pair<int, std::size_t>;
    std::vector<Ipair> sorted;
    sorted.reserve(type_counts.size());
    for (const auto& kv : type_counts) {
        sorted.push_back(kv);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Ipair& a, const Ipair& b) {
        if (a.second != b.second) {
            return a.second > b.second;
        }
        return a.first < b.first;
    });

    constexpr std::size_t k_top = 12;
    std::ostringstream oss;
    oss << "IGES 物理层扫描（未物化几何/拓扑）：疑似 Directory Entry 行 " << out_directory_entry_lines
        << "；实体类型号(字段1)频度 Top";
    if (sorted.empty()) {
        oss << "：无";
    } else {
        oss << "：";
        const std::size_t n = std::min(sorted.size(), k_top);
        for (std::size_t i = 0; i < n; ++i) {
            if (i) {
                oss << ", ";
            }
            oss << sorted[i].first << "=" << sorted[i].second;
        }
        if (sorted.size() > k_top) {
            oss << ", ...";
        }
    }
    oss << "。完整实体交换需集成 IGES 内核。";
    out_message = oss.str();
    return true;
}

}  // namespace axiom::io_internal
