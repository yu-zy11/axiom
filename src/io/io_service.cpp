#include "axiom/io/io_service.h"

#include <filesystem>
#include <fstream>
#include <span>
#include <regex>
#include <sstream>
#include <type_traits>
#include <system_error>
#include <chrono>
#include <algorithm>
#include <unordered_map>

#include "axiom/heal/heal_services.h"
#include "axiom/rep/representation_conversion_service.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/core/kernel_state.h"

namespace axiom {

namespace {

std::string_view body_kind_name(detail::BodyKind kind) {
    switch (kind) {
        case detail::BodyKind::Box: return "Box";
        case detail::BodyKind::Sphere: return "Sphere";
        case detail::BodyKind::Cylinder: return "Cylinder";
        case detail::BodyKind::Cone: return "Cone";
        case detail::BodyKind::Torus: return "Torus";
        case detail::BodyKind::Wedge: return "Wedge";
        case detail::BodyKind::Sweep: return "Sweep";
        case detail::BodyKind::BooleanResult: return "BooleanResult";
        case detail::BodyKind::Modified: return "Modified";
        case detail::BodyKind::BlendResult: return "BlendResult";
        case detail::BodyKind::Imported: return "Imported";
        case detail::BodyKind::Generic:
        default:
            return "Generic";
    }
}

detail::BodyKind parse_body_kind(std::string_view value) {
    if (value == "Box") return detail::BodyKind::Box;
    if (value == "Sphere") return detail::BodyKind::Sphere;
    if (value == "Cylinder") return detail::BodyKind::Cylinder;
    if (value == "Cone") return detail::BodyKind::Cone;
    if (value == "Torus") return detail::BodyKind::Torus;
    if (value == "Wedge") return detail::BodyKind::Wedge;
    if (value == "Sweep") return detail::BodyKind::Sweep;
    if (value == "BooleanResult") return detail::BodyKind::BooleanResult;
    if (value == "Modified") return detail::BodyKind::Modified;
    if (value == "BlendResult") return detail::BodyKind::BlendResult;
    if (value == "Imported") return detail::BodyKind::Imported;
    return detail::BodyKind::Generic;
}

bool parse_bbox_line(std::string_view line, BoundingBox& bbox) {
    std::istringstream input {std::string(line)};
    std::string prefix;
    Point3 min {};
    Point3 max {};
    input >> prefix >> min.x >> min.y >> min.z >> max.x >> max.y >> max.z;
    if (!input || prefix != "AXIOM_BBOX") {
        return false;
    }
    bbox = detail::make_bbox(min, max);
    return true;
}

bool parse_triplet_line(std::string_view line, std::string_view expected_prefix,
                        Scalar& x, Scalar& y, Scalar& z) {
    std::istringstream input {std::string(line)};
    std::string prefix;
    Scalar tx {};
    Scalar ty {};
    Scalar tz {};
    input >> prefix >> tx >> ty >> tz;
    if (!input || prefix != expected_prefix) {
        return false;
    }
    x = tx;
    y = ty;
    z = tz;
    return true;
}

std::string trim_comment(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return line;
}

std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (const auto ch : in) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

bool extract_json_string(const std::string& content, std::string_view key, std::string& out) {
    const std::regex pattern("\"" + std::string(key) + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(content, match, pattern) || match.size() < 2) {
        return false;
    }
    out = match[1].str();
    return true;
}

bool extract_json_number(const std::string& content, std::string_view key, Scalar& out) {
    const std::regex pattern("\"" + std::string(key) + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(content, match, pattern) || match.size() < 2) {
        return false;
    }
    out = std::stod(match[1].str());
    return true;
}

std::string lower_copy(std::string value) {
    for (auto& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

std::string base64_encode(std::span<const std::uint8_t> data) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= data.size()) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(kTable[(v >> 18) & 0x3F]);
        out.push_back(kTable[(v >> 12) & 0x3F]);
        out.push_back(kTable[(v >> 6) & 0x3F]);
        out.push_back(kTable[v & 0x3F]);
        i += 3;
    }
    const std::size_t rem = data.size() - i;
    if (rem == 1) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16);
        out.push_back(kTable[(v >> 18) & 0x3F]);
        out.push_back(kTable[(v >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kTable[(v >> 18) & 0x3F]);
        out.push_back(kTable[(v >> 12) & 0x3F]);
        out.push_back(kTable[(v >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

template <typename T>
void append_pod(std::vector<std::uint8_t>& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

void append_padding_4(std::vector<std::uint8_t>& out) {
    while ((out.size() % 4) != 0) out.push_back(0);
}

}  // namespace

IOService::IOService(std::shared_ptr<detail::KernelState> state) : state_(std::move(state)) {}

Result<BodyId> IOService::import_step(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kIoImportFailure,
            "STEP 导入失败：输入路径为空", "STEP 导入失败");
    }
    if (!std::filesystem::exists(path)) {
        return detail::failed_result<BodyId>(
            *state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
            "STEP 导入失败：输入文件不存在", "STEP 导入失败");
    }

    std::ifstream in {std::string(path)};
    if (!in) {
        return detail::failed_result<BodyId>(
            *state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
            "STEP 导入失败：无法打开输入文件", "STEP 导入失败");
    }

    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::ExactBRep;
    record.label = std::string(path);
    record.bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});

    std::string line;
    while (std::getline(in, line)) {
        line = trim_comment(std::move(line));
        if (line.rfind("AXIOM_LABEL ", 0) == 0) {
            record.label = line.substr(std::string("AXIOM_LABEL ").size());
            continue;
        }
        if (line.rfind("AXIOM_BODY_KIND ", 0) == 0) {
            record.kind = parse_body_kind(line.substr(std::string("AXIOM_BODY_KIND ").size()));
            continue;
        }
        if (parse_triplet_line(line, "AXIOM_ORIGIN", record.origin.x, record.origin.y, record.origin.z)) {
            continue;
        }
        if (parse_triplet_line(line, "AXIOM_AXIS", record.axis.x, record.axis.y, record.axis.z)) {
            continue;
        }
        if (parse_triplet_line(line, "AXIOM_PARAMS", record.a, record.b, record.c)) {
            continue;
        }

        BoundingBox parsed {};
        if (parse_bbox_line(line, parsed)) {
            record.bbox = parsed;
        }
    }

    state_->bodies.emplace(body_id.value, record);

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    auto append_issues_from_diag = [this, &issues, &warnings](DiagnosticId diagnostic_id,
                                                              std::initializer_list<std::uint64_t> fallback_related_entities) {
        if (diagnostic_id.value == 0) {
            return;
        }
        const auto diag_it = state_->diagnostics.find(diagnostic_id.value);
        if (diag_it == state_->diagnostics.end()) {
            return;
        }
        for (const auto& issue : diag_it->second.issues) {
            auto tracked_issue = issue;
            if (tracked_issue.related_entities.empty()) {
                tracked_issue.related_entities.assign(fallback_related_entities.begin(), fallback_related_entities.end());
            }
            issues.push_back(tracked_issue);
            if (tracked_issue.severity == IssueSeverity::Warning ||
                tracked_issue.severity == IssueSeverity::Error ||
                tracked_issue.severity == IssueSeverity::Fatal) {
                warnings.push_back(Warning {tracked_issue.code, tracked_issue.message});
            }
        }
    };

    BodyId result_body_id = body_id;
    if (options.run_validation) {
        auto validation_issue = detail::make_info_issue(diag_codes::kIoPostImportValidation, "STEP 导入后已触发自动验证");
        validation_issue.related_entities = {body_id.value};
        validation_issue.stage = "io.post_import.validation";
        issues.push_back(std::move(validation_issue));

        ValidationService validation {state_};
        const auto validation_result = validation.validate_all(result_body_id, ValidationMode::Standard);
        if (validation_result.status != StatusCode::Ok) {
            append_issues_from_diag(validation_result.diagnostic_id, {result_body_id.value});
            if (validation_result.diagnostic_id.value == 0) {
                const auto message = std::string("STEP 导入后自动验证失败");
                auto fallback_issue = detail::make_warning_issue(diag_codes::kIoImportFailure, message);
                fallback_issue.related_entities = {result_body_id.value};
                issues.push_back(std::move(fallback_issue));
                warnings.push_back(Warning {std::string(diag_codes::kIoImportFailure), message});
            }

            if (options.auto_repair) {
                const auto repair_mode = options.repair_mode == RepairMode::ReportOnly
                                             ? RepairMode::Safe
                                             : options.repair_mode;
                auto repair_mode_issue = detail::make_info_issue(
                    diag_codes::kIoPostImportRepairMode,
                    "STEP 导入后自动修复策略已启用: mode=" +
                        std::to_string(static_cast<int>(repair_mode)));
                repair_mode_issue.related_entities = {result_body_id.value};
                repair_mode_issue.stage = "io.post_import.repair_mode";
                issues.push_back(std::move(repair_mode_issue));

                RepairService repair {state_};
                const auto repair_result = repair.auto_repair(result_body_id, repair_mode);
                if (repair_result.status == StatusCode::Ok && repair_result.value.has_value()) {
                    result_body_id = repair_result.value->output;
                    append_issues_from_diag(repair_result.value->diagnostic_id, {body_id.value, result_body_id.value});

                    const auto repaired_validation = validation.validate_all(result_body_id, ValidationMode::Standard);
                    if (repaired_validation.status != StatusCode::Ok) {
                        append_issues_from_diag(repaired_validation.diagnostic_id, {result_body_id.value});
                    }
                } else {
                    append_issues_from_diag(repair_result.diagnostic_id, {body_id.value});
                }
            }
        }
    }

    auto result = ok_result(result_body_id, state_->create_diagnostic("已导入STEP模型", std::move(issues)));
    result.warnings = std::move(warnings);
    return result;
}

Result<void> IOService::export_step(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "STEP 导出失败：目标实体不存在或输出路径为空", "STEP 导出失败");
    }

    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "STEP 导出失败：无法打开输出文件", "STEP 导出失败");
    }

    const auto body_it = state_->bodies.find(body_id.value);
    const auto& body = body_it->second;

    out << "ISO-10303-21;\n";
    out << "HEADER;\n";
    out << "FILE_DESCRIPTION(('AxiomKernel export'),'2;1');\n";
    if (options.embed_metadata) {
        out << "/* metadata embedded */\n";
        out << "AXIOM_LABEL " << body.label << "\n";
        out << "AXIOM_BODY_KIND " << body_kind_name(body.kind) << "\n";
        out << "AXIOM_ORIGIN " << body.origin.x << " " << body.origin.y << " " << body.origin.z << "\n";
        out << "AXIOM_AXIS " << body.axis.x << " " << body.axis.y << " " << body.axis.z << "\n";
        out << "AXIOM_PARAMS " << body.a << " " << body.b << " " << body.c << "\n";
        out << "AXIOM_BBOX "
            << body.bbox.min.x << " " << body.bbox.min.y << " " << body.bbox.min.z << " "
            << body.bbox.max.x << " " << body.bbox.max.y << " " << body.bbox.max.z << "\n";
    }
    out << "ENDSEC;\n";
    out << "DATA;\n";
    out << "/* body " << body_id.value << " */\n";
    out << "ENDSEC;\n";
    out << "END-ISO-10303-21;\n";

    return ok_void(state_->create_diagnostic("已导出STEP模型"));
}

Result<BodyId> IOService::import_axmjson(std::string_view path, const ImportOptions& options) {
    if (path.empty()) {
        return detail::invalid_input_result<BodyId>(
            *state_, diag_codes::kIoImportFailure,
            "AXMJSON 导入失败：输入路径为空", "AXMJSON 导入失败");
    }
    if (!std::filesystem::exists(path)) {
        return detail::failed_result<BodyId>(
            *state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
            "AXMJSON 导入失败：输入文件不存在", "AXMJSON 导入失败");
    }
    std::ifstream in {std::string(path)};
    if (!in) {
        return detail::failed_result<BodyId>(
            *state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
            "AXMJSON 导入失败：无法打开输入文件", "AXMJSON 导入失败");
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());

    const auto body_id = BodyId {state_->allocate_id()};
    detail::BodyRecord record;
    record.kind = detail::BodyKind::Imported;
    record.rep_kind = RepKind::ExactBRep;
    record.label = std::string(path);
    record.bbox = detail::make_bbox({0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});

    std::string kind;
    std::string label;
    if (extract_json_string(content, "body_kind", kind)) {
        record.kind = parse_body_kind(kind);
    }
    if (extract_json_string(content, "label", label)) {
        record.label = label;
    }
    extract_json_number(content, "origin_x", record.origin.x);
    extract_json_number(content, "origin_y", record.origin.y);
    extract_json_number(content, "origin_z", record.origin.z);
    extract_json_number(content, "axis_x", record.axis.x);
    extract_json_number(content, "axis_y", record.axis.y);
    extract_json_number(content, "axis_z", record.axis.z);
    extract_json_number(content, "param_a", record.a);
    extract_json_number(content, "param_b", record.b);
    extract_json_number(content, "param_c", record.c);
    Scalar min_x {}, min_y {}, min_z {}, max_x {}, max_y {}, max_z {};
    if (extract_json_number(content, "bbox_min_x", min_x) &&
        extract_json_number(content, "bbox_min_y", min_y) &&
        extract_json_number(content, "bbox_min_z", min_z) &&
        extract_json_number(content, "bbox_max_x", max_x) &&
        extract_json_number(content, "bbox_max_y", max_y) &&
        extract_json_number(content, "bbox_max_z", max_z)) {
        record.bbox = detail::make_bbox({min_x, min_y, min_z}, {max_x, max_y, max_z});
    }

    state_->bodies.emplace(body_id.value, record);

    std::vector<Issue> issues;
    std::vector<Warning> warnings;
    auto append_issues_from_diag = [this, &issues, &warnings](DiagnosticId diagnostic_id,
                                                              std::initializer_list<std::uint64_t> fallback_related_entities) {
        if (diagnostic_id.value == 0) {
            return;
        }
        const auto diag_it = state_->diagnostics.find(diagnostic_id.value);
        if (diag_it == state_->diagnostics.end()) {
            return;
        }
        for (const auto& issue : diag_it->second.issues) {
            auto tracked_issue = issue;
            if (tracked_issue.related_entities.empty()) {
                tracked_issue.related_entities.assign(fallback_related_entities.begin(), fallback_related_entities.end());
            }
            issues.push_back(tracked_issue);
            if (tracked_issue.severity == IssueSeverity::Warning ||
                tracked_issue.severity == IssueSeverity::Error ||
                tracked_issue.severity == IssueSeverity::Fatal) {
                warnings.push_back(Warning {tracked_issue.code, tracked_issue.message});
            }
        }
    };

    BodyId result_body_id = body_id;
    if (options.run_validation) {
        auto validation_issue = detail::make_info_issue(diag_codes::kIoPostImportValidation, "AXMJSON 导入后已触发自动验证");
        validation_issue.related_entities = {body_id.value};
        validation_issue.stage = "io.post_import.validation";
        issues.push_back(std::move(validation_issue));

        ValidationService validation {state_};
        const auto validation_result = validation.validate_all(result_body_id, ValidationMode::Standard);
        if (validation_result.status != StatusCode::Ok) {
            append_issues_from_diag(validation_result.diagnostic_id, {result_body_id.value});
            if (validation_result.diagnostic_id.value == 0) {
                const auto message = std::string("AXMJSON 导入后自动验证失败");
                auto fallback_issue = detail::make_warning_issue(diag_codes::kIoImportFailure, message);
                fallback_issue.related_entities = {result_body_id.value};
                issues.push_back(std::move(fallback_issue));
                warnings.push_back(Warning {std::string(diag_codes::kIoImportFailure), message});
            }

            if (options.auto_repair) {
                const auto repair_mode = options.repair_mode == RepairMode::ReportOnly
                                             ? RepairMode::Safe
                                             : options.repair_mode;
                auto repair_mode_issue = detail::make_info_issue(
                    diag_codes::kIoPostImportRepairMode,
                    "AXMJSON 导入后自动修复策略已启用: mode=" +
                        std::to_string(static_cast<int>(repair_mode)));
                repair_mode_issue.related_entities = {result_body_id.value};
                repair_mode_issue.stage = "io.post_import.repair_mode";
                issues.push_back(std::move(repair_mode_issue));

                RepairService repair {state_};
                const auto repair_result = repair.auto_repair(result_body_id, repair_mode);
                if (repair_result.status == StatusCode::Ok && repair_result.value.has_value()) {
                    result_body_id = repair_result.value->output;
                    append_issues_from_diag(repair_result.value->diagnostic_id, {body_id.value, result_body_id.value});

                    const auto repaired_validation = validation.validate_all(result_body_id, ValidationMode::Standard);
                    if (repaired_validation.status != StatusCode::Ok) {
                        append_issues_from_diag(repaired_validation.diagnostic_id, {result_body_id.value});
                    }
                } else {
                    append_issues_from_diag(repair_result.diagnostic_id, {body_id.value});
                }
            }
        }
    }

    auto result = ok_result(result_body_id, state_->create_diagnostic("已导入AXMJSON模型", std::move(issues)));
    result.warnings = std::move(warnings);
    return result;
}

Result<void> IOService::export_axmjson(BodyId body_id, std::string_view path, const ExportOptions& options) {
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "AXMJSON 导出失败：目标实体不存在或输出路径为空", "AXMJSON 导出失败");
    }
    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "AXMJSON 导出失败：无法打开输出文件", "AXMJSON 导出失败");
    }
    const auto body_it = state_->bodies.find(body_id.value);
    const auto& body = body_it->second;
    out << "{\n";
    out << "  \"format\": \"AXMJSON\",\n";
    out << "  \"label\": \"" << json_escape(body.label) << "\",\n";
    out << "  \"body_kind\": \"" << body_kind_name(body.kind) << "\",\n";
    out << "  \"origin_x\": " << body.origin.x << ",\n";
    out << "  \"origin_y\": " << body.origin.y << ",\n";
    out << "  \"origin_z\": " << body.origin.z << ",\n";
    out << "  \"axis_x\": " << body.axis.x << ",\n";
    out << "  \"axis_y\": " << body.axis.y << ",\n";
    out << "  \"axis_z\": " << body.axis.z << ",\n";
    out << "  \"param_a\": " << body.a << ",\n";
    out << "  \"param_b\": " << body.b << ",\n";
    out << "  \"param_c\": " << body.c << ",\n";
    if (options.embed_metadata) {
        out << "  \"bbox_min_x\": " << body.bbox.min.x << ",\n";
        out << "  \"bbox_min_y\": " << body.bbox.min.y << ",\n";
        out << "  \"bbox_min_z\": " << body.bbox.min.z << ",\n";
        out << "  \"bbox_max_x\": " << body.bbox.max.x << ",\n";
        out << "  \"bbox_max_y\": " << body.bbox.max.y << ",\n";
        out << "  \"bbox_max_z\": " << body.bbox.max.z << "\n";
    } else {
        out << "  \"bbox_min_x\": 0,\n";
        out << "  \"bbox_min_y\": 0,\n";
        out << "  \"bbox_min_z\": 0,\n";
        out << "  \"bbox_max_x\": 0,\n";
        out << "  \"bbox_max_y\": 0,\n";
        out << "  \"bbox_max_z\": 0\n";
    }
    out << "}\n";
    return ok_void(state_->create_diagnostic("已导出AXMJSON模型"));
}

Result<void> IOService::export_gltf(BodyId body_id, std::string_view path, const ExportOptions& options) {
    (void)options;
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "glTF 导出失败：目标实体不存在或输出路径为空", "glTF 导出失败");
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) return ensured;

    RepresentationConversionService convert {state_};
    const auto mesh_id = convert.brep_to_mesh(body_id, TessellationOptions{});
    if (mesh_id.status != StatusCode::Ok || !mesh_id.value.has_value()) {
        return error_void(mesh_id.status, mesh_id.diagnostic_id);
    }

    const auto mesh_it = state_->meshes.find(mesh_id.value->value);
    if (mesh_it == state_->meshes.end()) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                                   "glTF 导出失败：网格记录不存在", "glTF 导出失败");
    }
    const auto& mesh = mesh_it->second;
    if (mesh.vertices.empty()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure,
                                          "glTF 导出失败：网格顶点为空", "glTF 导出失败");
    }
    if (!mesh.indices.empty() && (mesh.indices.size() % 3) != 0) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure,
                                          "glTF 导出失败：网格索引数量非法", "glTF 导出失败");
    }

    std::vector<std::uint8_t> buffer;
    buffer.reserve(mesh.vertices.size() * sizeof(float) * 3 +
                   mesh.indices.size() * sizeof(std::uint32_t) + 64);

    const std::size_t pos_offset = buffer.size();
    float min_x = static_cast<float>(mesh.vertices.front().x);
    float min_y = static_cast<float>(mesh.vertices.front().y);
    float min_z = static_cast<float>(mesh.vertices.front().z);
    float max_x = min_x, max_y = min_y, max_z = min_z;
    for (const auto& v : mesh.vertices) {
        const float fx = static_cast<float>(v.x);
        const float fy = static_cast<float>(v.y);
        const float fz = static_cast<float>(v.z);
        append_pod(buffer, fx);
        append_pod(buffer, fy);
        append_pod(buffer, fz);
        min_x = std::min(min_x, fx); min_y = std::min(min_y, fy); min_z = std::min(min_z, fz);
        max_x = std::max(max_x, fx); max_y = std::max(max_y, fy); max_z = std::max(max_z, fz);
    }
    append_padding_4(buffer);
    const std::size_t pos_length = buffer.size() - pos_offset;

    const std::size_t idx_offset = buffer.size();
    for (const auto idx : mesh.indices) {
        const std::uint32_t v = static_cast<std::uint32_t>(idx);
        append_pod(buffer, v);
    }
    append_padding_4(buffer);
    const std::size_t idx_length = buffer.size() - idx_offset;

    const auto uri = std::string("data:application/octet-stream;base64,") + base64_encode(buffer);

    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "glTF 导出失败：无法打开输出文件", "glTF 导出失败");
    }

    const auto vertex_count = static_cast<std::uint64_t>(mesh.vertices.size());
    const auto index_count = static_cast<std::uint64_t>(mesh.indices.size());

    out << "{";
    out << "\"asset\":{\"version\":\"2.0\",\"generator\":\"AxiomKernel\"},";
    out << "\"scene\":0,";
    out << "\"scenes\":[{\"nodes\":[0]}],";
    out << "\"nodes\":[{\"mesh\":0,\"name\":\"" << json_escape(mesh.label) << "\"}],";
    out << "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],";
    out << "\"buffers\":[{\"uri\":\"" << uri << "\",\"byteLength\":" << buffer.size() << "}],";
    out << "\"bufferViews\":[";
    out << "{\"buffer\":0,\"byteOffset\":" << pos_offset << ",\"byteLength\":" << pos_length << ",\"target\":34962},";
    out << "{\"buffer\":0,\"byteOffset\":" << idx_offset << ",\"byteLength\":" << idx_length << ",\"target\":34963}";
    out << "],";
    out << "\"accessors\":[";
    out << "{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":" << vertex_count
        << ",\"type\":\"VEC3\",\"min\":[" << min_x << "," << min_y << "," << min_z << "],"
        << "\"max\":[" << max_x << "," << max_y << "," << max_z << "]},";
    out << "{\"bufferView\":1,\"byteOffset\":0,\"componentType\":5125,\"count\":" << index_count
        << ",\"type\":\"SCALAR\"}";
    out << "]";
    out << "}";

    return ok_void(state_->create_diagnostic("已导出glTF模型"));
}

Result<void> IOService::export_stl(BodyId body_id, std::string_view path, const ExportOptions& options) {
    (void)options;
    if (!detail::has_body(*state_, body_id) || path.empty()) {
        return detail::invalid_input_void(
            *state_, diag_codes::kIoExportFailure,
            "STL 导出失败：目标实体不存在或输出路径为空", "STL 导出失败");
    }
    const auto ensured = ensure_parent_directory(path);
    if (ensured.status != StatusCode::Ok) return ensured;

    RepresentationConversionService convert {state_};
    const auto mesh_id = convert.brep_to_mesh(body_id, TessellationOptions{});
    if (mesh_id.status != StatusCode::Ok || !mesh_id.value.has_value()) {
        return error_void(mesh_id.status, mesh_id.diagnostic_id);
    }
    const auto mesh_it = state_->meshes.find(mesh_id.value->value);
    if (mesh_it == state_->meshes.end()) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                                   "STL 导出失败：网格记录不存在", "STL 导出失败");
    }
    const auto& mesh = mesh_it->second;
    if (mesh.vertices.empty()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure,
                                          "STL 导出失败：网格顶点为空", "STL 导出失败");
    }
    if (mesh.indices.empty() || (mesh.indices.size() % 3) != 0) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure,
                                          "STL 导出失败：网格索引为空或数量非法", "STL 导出失败");
    }

    std::ofstream out {std::string(path)};
    if (!out) {
        return detail::failed_void(
            *state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
            "STL 导出失败：无法打开输出文件", "STL 导出失败");
    }

    out << "solid axiom\n";
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const auto i0 = static_cast<std::size_t>(mesh.indices[i]);
        const auto i1 = static_cast<std::size_t>(mesh.indices[i + 1]);
        const auto i2 = static_cast<std::size_t>(mesh.indices[i + 2]);
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure,
                                              "STL 导出失败：网格索引超出顶点范围", "STL 导出失败");
        }
        const auto& a = mesh.vertices[i0];
        const auto& b = mesh.vertices[i1];
        const auto& c = mesh.vertices[i2];
        const auto ab = detail::subtract(b, a);
        const auto ac = detail::subtract(c, a);
        const auto n = detail::normalize(detail::cross(ab, ac));

        out << "  facet normal " << n.x << " " << n.y << " " << n.z << "\n";
        out << "    outer loop\n";
        out << "      vertex " << a.x << " " << a.y << " " << a.z << "\n";
        out << "      vertex " << b.x << " " << b.y << " " << b.z << "\n";
        out << "      vertex " << c.x << " " << c.y << " " << c.z << "\n";
        out << "    endloop\n";
        out << "  endfacet\n";
    }
    out << "endsolid axiom\n";

    return ok_void(state_->create_diagnostic("已导出STL模型"));
}

Result<bool> IOService::file_exists(std::string_view path) const {
    if (path.empty()) {
        return ok_result(false, state_->create_diagnostic("已查询文件存在性"));
    }
    return ok_result(std::filesystem::exists(path), state_->create_diagnostic("已查询文件存在性"));
}

Result<bool> IOService::is_regular_file(std::string_view path) const {
    if (path.empty()) {
        return ok_result(false, state_->create_diagnostic("已查询常规文件属性"));
    }
    return ok_result(std::filesystem::is_regular_file(path), state_->create_diagnostic("已查询常规文件属性"));
}

Result<std::uint64_t> IOService::file_size_bytes(std::string_view path) const {
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return detail::invalid_input_result<std::uint64_t>(
            *state_, diag_codes::kIoImportFailure,
            "文件大小查询失败：文件不存在或不是常规文件", "文件大小查询失败");
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(std::filesystem::file_size(path)),
                                    state_->create_diagnostic("已查询文件大小"));
}

Result<bool> IOService::has_extension(std::string_view path, std::string_view ext) const {
    if (path.empty() || ext.empty()) {
        return ok_result(false, state_->create_diagnostic("已查询文件扩展名匹配"));
    }
    const auto got = lower_copy(std::filesystem::path(path).extension().string());
    auto expected = lower_copy(std::string(ext));
    if (!expected.empty() && expected.front() != '.') {
        expected = "." + expected;
    }
    return ok_result(got == expected, state_->create_diagnostic("已查询文件扩展名匹配"));
}

Result<std::string> IOService::detect_format(std::string_view path) const {
    if (path.empty()) {
        return detail::invalid_input_result<std::string>(
            *state_, diag_codes::kIoImportFailure, "格式识别失败：路径为空", "格式识别失败");
    }
    const auto ext = lower_copy(std::filesystem::path(path).extension().string());
    if (ext == ".step" || ext == ".stp") {
        return ok_result(std::string("step"), state_->create_diagnostic("已识别IO格式"));
    }
    if (ext == ".axmjson" || ext == ".json") {
        return ok_result(std::string("axmjson"), state_->create_diagnostic("已识别IO格式"));
    }
    if (ext == ".gltf") {
        return ok_result(std::string("gltf"), state_->create_diagnostic("已识别IO格式"));
    }
    if (ext == ".stl") {
        return ok_result(std::string("stl"), state_->create_diagnostic("已识别IO格式"));
    }
    return ok_result(std::string("unknown"), state_->create_diagnostic("已识别IO格式"));
}

Result<bool> IOService::is_step_path(std::string_view path) const {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<bool>(fmt.status, fmt.diagnostic_id);
    }
    return ok_result(*fmt.value == "step", state_->create_diagnostic("已识别STEP路径"));
}

Result<bool> IOService::is_axmjson_path(std::string_view path) const {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<bool>(fmt.status, fmt.diagnostic_id);
    }
    return ok_result(*fmt.value == "axmjson", state_->create_diagnostic("已识别AXMJSON路径"));
}

Result<bool> IOService::is_gltf_path(std::string_view path) const {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<bool>(fmt.status, fmt.diagnostic_id);
    }
    return ok_result(*fmt.value == "gltf", state_->create_diagnostic("已识别glTF路径"));
}

Result<bool> IOService::is_stl_path(std::string_view path) const {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<bool>(fmt.status, fmt.diagnostic_id);
    }
    return ok_result(*fmt.value == "stl", state_->create_diagnostic("已识别STL路径"));
}

Result<std::string> IOService::normalize_path(std::string_view path) const {
    if (path.empty()) {
        return detail::invalid_input_result<std::string>(
            *state_, diag_codes::kIoImportFailure, "路径规范化失败：路径为空", "路径规范化失败");
    }
    std::error_code ec;
    const auto normalized = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    if (ec) {
        return ok_result(std::filesystem::path(path).lexically_normal().string(),
                         state_->create_diagnostic("已规范化路径"));
    }
    return ok_result(normalized.string(), state_->create_diagnostic("已规范化路径"));
}

Result<BodyId> IOService::import_step_default(std::string_view path) {
    return import_step(path, ImportOptions{});
}

Result<BodyId> IOService::import_axmjson_default(std::string_view path) {
    return import_axmjson(path, ImportOptions{});
}

Result<void> IOService::export_step_default(BodyId body_id, std::string_view path) {
    return export_step(body_id, path, ExportOptions{});
}

Result<void> IOService::export_axmjson_default(BodyId body_id, std::string_view path) {
    return export_axmjson(body_id, path, ExportOptions{});
}

Result<void> IOService::export_gltf_default(BodyId body_id, std::string_view path) {
    return export_gltf(body_id, path, ExportOptions{});
}

Result<void> IOService::export_stl_default(BodyId body_id, std::string_view path) {
    return export_stl(body_id, path, ExportOptions{});
}

Result<BodyId> IOService::import_auto(std::string_view path, const ImportOptions& options) {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<BodyId>(fmt.status, fmt.diagnostic_id);
    }
    if (*fmt.value == "step") return import_step(path, options);
    if (*fmt.value == "axmjson") return import_axmjson(path, options);
    return detail::invalid_input_result<BodyId>(
        *state_, diag_codes::kIoImportFailure, "自动导入失败：不支持的文件格式", "自动导入失败");
}

Result<void> IOService::export_auto(BodyId body_id, std::string_view path, const ExportOptions& options) {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<void>(fmt.status, fmt.diagnostic_id);
    }
    if (*fmt.value == "step") return export_step(body_id, path, options);
    if (*fmt.value == "axmjson") return export_axmjson(body_id, path, options);
    if (*fmt.value == "gltf") return export_gltf(body_id, path, options);
    if (*fmt.value == "stl") return export_stl(body_id, path, options);
    return detail::invalid_input_void(
        *state_, diag_codes::kIoExportFailure, "自动导出失败：不支持的文件格式", "自动导出失败");
}

Result<std::vector<BodyId>> IOService::import_many_step(std::span<const std::string> paths, const ImportOptions& options) {
    if (paths.empty()) {
        return detail::invalid_input_result<std::vector<BodyId>>(
            *state_, diag_codes::kIoImportFailure, "批量STEP导入失败：路径列表为空", "批量STEP导入失败");
    }
    std::vector<BodyId> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto r = import_step(p, options);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::vector<BodyId>>(r.status, r.diagnostic_id);
        out.push_back(*r.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已完成批量STEP导入"));
}

Result<std::vector<BodyId>> IOService::import_many_axmjson(std::span<const std::string> paths, const ImportOptions& options) {
    if (paths.empty()) {
        return detail::invalid_input_result<std::vector<BodyId>>(
            *state_, diag_codes::kIoImportFailure, "批量AXMJSON导入失败：路径列表为空", "批量AXMJSON导入失败");
    }
    std::vector<BodyId> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto r = import_axmjson(p, options);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::vector<BodyId>>(r.status, r.diagnostic_id);
        out.push_back(*r.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已完成批量AXMJSON导入"));
}

Result<void> IOService::export_many_step(std::span<const BodyId> body_ids, std::span<const std::string> paths, const ExportOptions& options) {
    if (body_ids.empty() || body_ids.size() != paths.size()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "批量STEP导出失败：输入数量不一致", "批量STEP导出失败");
    }
    for (std::size_t i = 0; i < body_ids.size(); ++i) {
        const auto r = export_step(body_ids[i], paths[i], options);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已完成批量STEP导出"));
}

Result<void> IOService::export_many_axmjson(std::span<const BodyId> body_ids, std::span<const std::string> paths, const ExportOptions& options) {
    if (body_ids.empty() || body_ids.size() != paths.size()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "批量AXMJSON导出失败：输入数量不一致", "批量AXMJSON导出失败");
    }
    for (std::size_t i = 0; i < body_ids.size(); ++i) {
        const auto r = export_axmjson(body_ids[i], paths[i], options);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已完成批量AXMJSON导出"));
}

Result<std::vector<BodyId>> IOService::import_many_auto(std::span<const std::string> paths, const ImportOptions& options) {
    if (paths.empty()) {
        return detail::invalid_input_result<std::vector<BodyId>>(
            *state_, diag_codes::kIoImportFailure, "批量自动导入失败：路径列表为空", "批量自动导入失败");
    }
    std::vector<BodyId> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto r = import_auto(p, options);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::vector<BodyId>>(r.status, r.diagnostic_id);
        out.push_back(*r.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已完成批量自动导入"));
}

Result<void> IOService::export_many_auto(std::span<const BodyId> body_ids, std::span<const std::string> paths, const ExportOptions& options) {
    if (body_ids.empty() || body_ids.size() != paths.size()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "批量自动导出失败：输入数量不一致", "批量自动导出失败");
    }
    for (std::size_t i = 0; i < body_ids.size(); ++i) {
        const auto r = export_auto(body_ids[i], paths[i], options);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已完成批量自动导出"));
}

Result<std::uint64_t> IOService::count_lines(std::string_view path) const {
    std::ifstream in{std::string(path)};
    if (!in) {
        return detail::failed_result<std::uint64_t>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                                    "行数统计失败：无法打开文件", "行数统计失败");
    }
    std::uint64_t lines = 0;
    std::string tmp;
    while (std::getline(in, tmp)) ++lines;
    return ok_result<std::uint64_t>(lines, state_->create_diagnostic("已统计文件行数"));
}

Result<std::string> IOService::read_text_preview(std::string_view path, std::uint64_t max_chars) const {
    if (max_chars == 0) {
        return detail::invalid_input_result<std::string>(*state_, diag_codes::kIoImportFailure, "文本预览失败：长度上限为0", "文本预览失败");
    }
    std::ifstream in{std::string(path)};
    if (!in) {
        return detail::failed_result<std::string>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                                  "文本预览失败：无法打开文件", "文本预览失败");
    }
    std::string content;
    content.resize(static_cast<std::size_t>(max_chars));
    in.read(content.data(), static_cast<std::streamsize>(max_chars));
    content.resize(static_cast<std::size_t>(in.gcount()));
    return ok_result(std::move(content), state_->create_diagnostic("已读取文本预览"));
}

Result<void> IOService::write_text_snapshot(std::string_view path, std::string_view content) const {
    std::ofstream out{std::string(path)};
    if (!out) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure,
                                   "文本快照写出失败：无法打开输出文件", "文本快照写出失败");
    }
    out << content;
    return ok_void(state_->create_diagnostic("已写出文本快照"));
}

Result<void> IOService::export_body_summary_txt(BodyId body_id, std::string_view path) const {
    const auto body_it = state_->bodies.find(body_id.value);
    if (body_it == state_->bodies.end()) {
        return detail::invalid_input_void(*state_, diag_codes::kCoreInvalidHandle, "体摘要导出失败：目标体不存在", "体摘要导出失败");
    }
    std::ofstream out{std::string(path)};
    if (!out) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "体摘要导出失败：无法打开输出文件", "体摘要导出失败");
    }
    const auto& b = body_it->second;
    out << "BodyId: " << body_id.value << "\n";
    out << "Kind: " << body_kind_name(b.kind) << "\n";
    out << "Label: " << b.label << "\n";
    out << "BBox: " << b.bbox.min.x << " " << b.bbox.min.y << " " << b.bbox.min.z << " "
        << b.bbox.max.x << " " << b.bbox.max.y << " " << b.bbox.max.z << "\n";
    return ok_void(state_->create_diagnostic("已导出体摘要文本"));
}

Result<void> IOService::validate_import_path(std::string_view path) const {
    if (path.empty() || !std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return detail::invalid_input_void(*state_, diag_codes::kIoImportFailure, "导入路径校验失败：路径无效", "导入路径校验失败");
    }
    return ok_void(state_->create_diagnostic("导入路径校验通过"));
}

Result<void> IOService::validate_export_path(std::string_view path) const {
    if (path.empty()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "导出路径校验失败：路径为空", "导出路径校验失败");
    }
    const auto p = std::filesystem::path(path).parent_path();
    if (!p.empty() && !std::filesystem::exists(p)) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "导出路径校验失败：父目录不存在", "导出路径校验失败");
    }
    return ok_void(state_->create_diagnostic("导出路径校验通过"));
}

Result<std::string> IOService::temp_path_for(std::string_view stem, std::string_view ext) const {
    if (stem.empty()) {
        return detail::invalid_input_result<std::string>(*state_, diag_codes::kIoExportFailure, "临时路径生成失败：stem为空", "临时路径生成失败");
    }
    auto extension = std::string(ext);
    if (!extension.empty() && extension.front() != '.') {
        extension = "." + extension;
    }
    const auto path = std::filesystem::temp_directory_path() / (std::string(stem) + extension);
    return ok_result(path.string(), state_->create_diagnostic("已生成临时路径"));
}

Result<void> IOService::copy_file(std::string_view from, std::string_view to) const {
    std::error_code ec;
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "文件复制失败", "文件复制失败");
    }
    return ok_void(state_->create_diagnostic("已复制文件"));
}

Result<void> IOService::remove_file(std::string_view path) const {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "文件删除失败", "文件删除失败");
    }
    return ok_void(state_->create_diagnostic("已删除文件"));
}

Result<void> IOService::ensure_parent_directory(std::string_view path) const {
    if (path.empty()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "目录创建失败：路径为空", "目录创建失败");
    }
    const auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) {
        return ok_void(state_->create_diagnostic("已确认父目录可用"));
    }
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "目录创建失败", "目录创建失败");
    }
    return ok_void(state_->create_diagnostic("已确认父目录可用"));
}

Result<std::pair<BodyId, std::uint64_t>>
IOService::import_step_with_warnings_count(std::string_view path,
                                           const ImportOptions& options) {
    const auto imported = import_step(path, options);
    if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
        return error_result<std::pair<BodyId, std::uint64_t>>(imported.status, imported.diagnostic_id);
    }
    return ok_result(std::make_pair(*imported.value, static_cast<std::uint64_t>(imported.warnings.size())),
                     state_->create_diagnostic("已完成STEP导入并统计告警"));
}

Result<std::pair<BodyId, std::uint64_t>>
IOService::import_axmjson_with_warnings_count(std::string_view path,
                                              const ImportOptions& options) {
    const auto imported = import_axmjson(path, options);
    if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
        return error_result<std::pair<BodyId, std::uint64_t>>(imported.status, imported.diagnostic_id);
    }
    return ok_result(std::make_pair(*imported.value, static_cast<std::uint64_t>(imported.warnings.size())),
                     state_->create_diagnostic("已完成AXMJSON导入并统计告警"));
}

Result<std::pair<BodyId, std::uint64_t>>
IOService::import_auto_with_warnings_count(std::string_view path,
                                           const ImportOptions& options) {
    const auto fmt = detect_format(path);
    if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
        return error_result<std::pair<BodyId, std::uint64_t>>(fmt.status, fmt.diagnostic_id);
    }
    if (*fmt.value == "step") return import_step_with_warnings_count(path, options);
    if (*fmt.value == "axmjson") return import_axmjson_with_warnings_count(path, options);
    return detail::invalid_input_result<std::pair<BodyId, std::uint64_t>>(
        *state_, diag_codes::kIoImportFailure, "自动导入统计失败：不支持的文件格式", "自动导入统计失败");
}

Result<bool> IOService::export_step_checked(BodyId body_id, std::string_view path,
                                            const ExportOptions& options) {
    const auto exported = export_step(body_id, path, options);
    if (exported.status != StatusCode::Ok) {
        return error_result<bool>(exported.status, exported.diagnostic_id);
    }
    return ok_result(std::filesystem::exists(path), state_->create_diagnostic("已完成STEP导出并校验"));
}

Result<bool> IOService::export_axmjson_checked(BodyId body_id, std::string_view path,
                                               const ExportOptions& options) {
    const auto exported = export_axmjson(body_id, path, options);
    if (exported.status != StatusCode::Ok) {
        return error_result<bool>(exported.status, exported.diagnostic_id);
    }
    return ok_result(std::filesystem::exists(path), state_->create_diagnostic("已完成AXMJSON导出并校验"));
}

Result<bool> IOService::export_auto_checked(BodyId body_id, std::string_view path,
                                            const ExportOptions& options) {
    const auto exported = export_auto(body_id, path, options);
    if (exported.status != StatusCode::Ok) {
        return error_result<bool>(exported.status, exported.diagnostic_id);
    }
    return ok_result(std::filesystem::exists(path), state_->create_diagnostic("已完成自动导出并校验"));
}

Result<std::uint64_t>
IOService::import_many_step_count(std::span<const std::string> paths,
                                  const ImportOptions& options) {
    const auto imported = import_many_step(paths, options);
    if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
        return error_result<std::uint64_t>(imported.status, imported.diagnostic_id);
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(imported.value->size()),
                                    state_->create_diagnostic("已统计批量STEP导入数量"));
}

Result<std::uint64_t>
IOService::import_many_axmjson_count(std::span<const std::string> paths,
                                     const ImportOptions& options) {
    const auto imported = import_many_axmjson(paths, options);
    if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
        return error_result<std::uint64_t>(imported.status, imported.diagnostic_id);
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(imported.value->size()),
                                    state_->create_diagnostic("已统计批量AXMJSON导入数量"));
}

Result<std::uint64_t>
IOService::import_many_auto_count(std::span<const std::string> paths,
                                  const ImportOptions& options) {
    const auto imported = import_many_auto(paths, options);
    if (imported.status != StatusCode::Ok || !imported.value.has_value()) {
        return error_result<std::uint64_t>(imported.status, imported.diagnostic_id);
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(imported.value->size()),
                                    state_->create_diagnostic("已统计批量自动导入数量"));
}

Result<std::uint64_t>
IOService::export_many_step_checked(std::span<const BodyId> body_ids,
                                    std::span<const std::string> paths,
                                    const ExportOptions& options) {
    const auto exported = export_many_step(body_ids, paths, options);
    if (exported.status != StatusCode::Ok) {
        return error_result<std::uint64_t>(exported.status, exported.diagnostic_id);
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(paths.size()),
                                    state_->create_diagnostic("已统计批量STEP导出数量"));
}

Result<std::uint64_t>
IOService::export_many_axmjson_checked(std::span<const BodyId> body_ids,
                                       std::span<const std::string> paths,
                                       const ExportOptions& options) {
    const auto exported = export_many_axmjson(body_ids, paths, options);
    if (exported.status != StatusCode::Ok) {
        return error_result<std::uint64_t>(exported.status, exported.diagnostic_id);
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(paths.size()),
                                    state_->create_diagnostic("已统计批量AXMJSON导出数量"));
}

Result<std::uint64_t>
IOService::export_many_auto_checked(std::span<const BodyId> body_ids,
                                    std::span<const std::string> paths,
                                    const ExportOptions& options) {
    const auto exported = export_many_auto(body_ids, paths, options);
    if (exported.status != StatusCode::Ok) {
        return error_result<std::uint64_t>(exported.status, exported.diagnostic_id);
    }
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(paths.size()),
                                    state_->create_diagnostic("已统计批量自动导出数量"));
}

Result<std::vector<std::string>>
IOService::scan_formats(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) {
            return error_result<std::vector<std::string>>(fmt.status, fmt.diagnostic_id);
        }
        out.push_back(*fmt.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已完成格式扫描"));
}

Result<std::uint64_t>
IOService::count_existing_files(std::span<const std::string> paths) const {
    std::uint64_t count = 0;
    for (const auto& p : paths) {
        if (std::filesystem::exists(p)) ++count;
    }
    return ok_result<std::uint64_t>(count, state_->create_diagnostic("已统计存在文件数量"));
}

Result<std::vector<std::string>>
IOService::filter_existing_files(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    for (const auto& p : paths) {
        if (std::filesystem::exists(p)) out.push_back(p);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已筛选存在文件"));
}

Result<std::vector<std::string>>
IOService::filter_missing_files(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    for (const auto& p : paths) {
        if (!std::filesystem::exists(p)) out.push_back(p);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已筛选缺失文件"));
}

Result<std::string>
IOService::first_missing_file(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        if (!std::filesystem::exists(p)) {
            return ok_result(p, state_->create_diagnostic("已查询首个缺失文件"));
        }
    }
    return ok_result(std::string{}, state_->create_diagnostic("已查询首个缺失文件"));
}

Result<std::string>
IOService::first_existing_file(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        if (std::filesystem::exists(p)) {
            return ok_result(p, state_->create_diagnostic("已查询首个存在文件"));
        }
    }
    return ok_result(std::string{}, state_->create_diagnostic("已查询首个存在文件"));
}

Result<std::string> IOService::sanitize_export_stem(std::string_view stem) const {
    if (stem.empty()) {
        return detail::invalid_input_result<std::string>(
            *state_, diag_codes::kIoExportFailure, "导出名净化失败：stem为空", "导出名净化失败");
    }
    std::string out;
    out.reserve(stem.size());
    for (const auto ch : stem) {
        const bool valid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        out.push_back(valid ? ch : '_');
    }
    return ok_result(std::move(out), state_->create_diagnostic("已净化导出名"));
}

Result<std::string> IOService::compose_path(std::string_view dir, std::string_view name,
                                            std::string_view ext) const {
    if (name.empty()) {
        return detail::invalid_input_result<std::string>(
            *state_, diag_codes::kIoExportFailure, "路径拼装失败：name为空", "路径拼装失败");
    }
    auto extension = std::string(ext);
    if (!extension.empty() && extension.front() != '.') extension = "." + extension;
    const auto path = std::filesystem::path(dir) / (std::string(name) + extension);
    return ok_result(path.string(), state_->create_diagnostic("已拼装路径"));
}

Result<std::string> IOService::change_extension(std::string_view path,
                                                std::string_view ext) const {
    if (path.empty()) {
        return detail::invalid_input_result<std::string>(
            *state_, diag_codes::kIoExportFailure, "扩展名变更失败：路径为空", "扩展名变更失败");
    }
    auto p = std::filesystem::path(path);
    auto extension = std::string(ext);
    if (!extension.empty() && extension.front() != '.') extension = "." + extension;
    p.replace_extension(extension);
    return ok_result(p.string(), state_->create_diagnostic("已变更扩展名"));
}

Result<std::string> IOService::basename(std::string_view path) const {
    return ok_result(std::filesystem::path(path).filename().string(),
                     state_->create_diagnostic("已提取文件名"));
}

Result<std::string> IOService::dirname(std::string_view path) const {
    return ok_result(std::filesystem::path(path).parent_path().string(),
                     state_->create_diagnostic("已提取目录名"));
}

Result<std::uint64_t> IOService::file_mtime_unix(std::string_view path) const {
    if (!std::filesystem::exists(path)) {
        return detail::invalid_input_result<std::uint64_t>(
            *state_, diag_codes::kIoImportFailure, "文件时间查询失败：文件不存在", "文件时间查询失败");
    }
    const auto t = std::filesystem::last_write_time(path).time_since_epoch();
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(t).count();
    return ok_result<std::uint64_t>(secs < 0 ? 0 : static_cast<std::uint64_t>(secs),
                                    state_->create_diagnostic("已查询文件修改时间"));
}

Result<void> IOService::touch_empty_file(std::string_view path) const {
    std::ofstream out{std::string(path), std::ios::app};
    if (!out) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "touch失败：无法打开文件", "touch失败");
    }
    return ok_void(state_->create_diagnostic("已touch文件"));
}

Result<void> IOService::append_text(std::string_view path, std::string_view content) const {
    std::ofstream out{std::string(path), std::ios::app};
    if (!out) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "追加文本失败：无法打开文件", "追加文本失败");
    }
    out << content;
    return ok_void(state_->create_diagnostic("已追加文本"));
}

Result<std::string> IOService::read_all_text(std::string_view path) const {
    std::ifstream in{std::string(path)};
    if (!in) {
        return detail::failed_result<std::string>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                                  "读取全文失败：无法打开文件", "读取全文失败");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ok_result(ss.str(), state_->create_diagnostic("已读取全文"));
}

Result<bool> IOService::compare_file_text(std::string_view lhs,
                                          std::string_view rhs) const {
    const auto l = read_all_text(lhs);
    if (l.status != StatusCode::Ok || !l.value.has_value()) return error_result<bool>(l.status, l.diagnostic_id);
    const auto r = read_all_text(rhs);
    if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<bool>(r.status, r.diagnostic_id);
    return ok_result(*l.value == *r.value, state_->create_diagnostic("已比较文件文本"));
}

Result<void> IOService::export_bodies_summary_txt(std::span<const BodyId> body_ids,
                                                  std::string_view path) const {
    if (body_ids.empty()) {
        return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "批量摘要导出失败：体列表为空", "批量摘要导出失败");
    }
    std::ofstream out{std::string(path)};
    if (!out) {
        return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "批量摘要导出失败：无法打开输出文件", "批量摘要导出失败");
    }
    for (const auto body_id : body_ids) {
        const auto it = state_->bodies.find(body_id.value);
        if (it == state_->bodies.end()) continue;
        const auto& b = it->second;
        out << "BodyId: " << body_id.value << ", Kind: " << body_kind_name(b.kind) << ", Label: " << b.label << "\n";
    }
    return ok_void(state_->create_diagnostic("已导出批量体摘要文本"));
}

Result<BodyId>
IOService::import_auto_from_candidates(std::span<const std::string> paths,
                                       const ImportOptions& options) {
    if (paths.empty()) {
        return detail::invalid_input_result<BodyId>(*state_, diag_codes::kIoImportFailure, "候选导入失败：路径列表为空", "候选导入失败");
    }
    for (const auto& p : paths) {
        if (!std::filesystem::exists(p)) continue;
        return import_auto(p, options);
    }
    return detail::failed_result<BodyId>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure,
                                         "候选导入失败：没有可用文件", "候选导入失败");
}

Result<std::uint64_t> IOService::count_importable_paths(std::span<const std::string> paths) const {
    std::uint64_t n = 0;
    for (const auto& p : paths) {
        if (std::filesystem::exists(p) && detect_format(p).value.value_or("unknown") != "unknown") ++n;
    }
    return ok_result<std::uint64_t>(n, state_->create_diagnostic("已统计可导入路径数量"));
}

Result<std::uint64_t> IOService::count_exportable_paths(std::span<const std::string> paths) const {
    std::uint64_t n = 0;
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status == StatusCode::Ok && fmt.value.has_value() && *fmt.value != "unknown") ++n;
    }
    return ok_result<std::uint64_t>(n, state_->create_diagnostic("已统计可导出路径数量"));
}

Result<std::vector<std::string>> IOService::filter_step_paths(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    for (const auto& p : paths) if (is_step_path(p).value.value_or(false)) out.push_back(p);
    return ok_result(std::move(out), state_->create_diagnostic("已筛选STEP路径"));
}

Result<std::vector<std::string>> IOService::filter_axmjson_paths(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    for (const auto& p : paths) if (is_axmjson_path(p).value.value_or(false)) out.push_back(p);
    return ok_result(std::move(out), state_->create_diagnostic("已筛选AXMJSON路径"));
}

Result<std::vector<std::string>> IOService::filter_unknown_format_paths(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status == StatusCode::Ok && fmt.value.has_value() && *fmt.value == "unknown") out.push_back(p);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已筛选未知格式路径"));
}

Result<std::vector<std::string>> IOService::normalize_paths(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto n = normalize_path(p);
        if (n.status != StatusCode::Ok || !n.value.has_value()) return error_result<std::vector<std::string>>(n.status, n.diagnostic_id);
        out.push_back(*n.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已批量规范化路径"));
}

Result<std::vector<std::string>> IOService::compose_paths(std::string_view dir, std::span<const std::string> names, std::string_view ext) const {
    std::vector<std::string> out;
    out.reserve(names.size());
    for (const auto& n : names) {
        const auto p = compose_path(dir, n, ext);
        if (p.status != StatusCode::Ok || !p.value.has_value()) return error_result<std::vector<std::string>>(p.status, p.diagnostic_id);
        out.push_back(*p.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已批量拼装路径"));
}

Result<std::vector<std::string>> IOService::change_extensions(std::span<const std::string> paths, std::string_view ext) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto changed = change_extension(p, ext);
        if (changed.status != StatusCode::Ok || !changed.value.has_value()) return error_result<std::vector<std::string>>(changed.status, changed.diagnostic_id);
        out.push_back(*changed.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已批量修改扩展名"));
}

Result<std::vector<std::string>> IOService::basenames(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) out.push_back(std::filesystem::path(p).filename().string());
    return ok_result(std::move(out), state_->create_diagnostic("已批量提取文件名"));
}

Result<std::vector<std::string>> IOService::dirnames(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) out.push_back(std::filesystem::path(p).parent_path().string());
    return ok_result(std::move(out), state_->create_diagnostic("已批量提取目录名"));
}

Result<std::vector<std::uint64_t>> IOService::file_sizes(std::span<const std::string> paths) const {
    std::vector<std::uint64_t> out;
    out.reserve(paths.size());
    for (const auto& p : paths) out.push_back(file_size_bytes(p).value.value_or(0));
    return ok_result(std::move(out), state_->create_diagnostic("已批量查询文件大小"));
}

Result<std::vector<std::uint64_t>> IOService::file_mtimes(std::span<const std::string> paths) const {
    std::vector<std::uint64_t> out;
    out.reserve(paths.size());
    for (const auto& p : paths) out.push_back(file_mtime_unix(p).value.value_or(0));
    return ok_result(std::move(out), state_->create_diagnostic("已批量查询文件时间"));
}

Result<void> IOService::ensure_parent_directories(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        const auto r = ensure_parent_directory(p);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已批量确认父目录可用"));
}

Result<void> IOService::touch_empty_files(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        const auto r = touch_empty_file(p);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已批量touch文件"));
}

Result<void> IOService::remove_files(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        const auto r = remove_file(p);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已批量删除文件"));
}

Result<void> IOService::append_text_many(std::span<const std::string> paths, std::string_view content) const {
    for (const auto& p : paths) {
        const auto r = append_text(p, content);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已批量追加文本"));
}

Result<std::vector<std::string>> IOService::read_all_text_many(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto r = read_all_text(p);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::vector<std::string>>(r.status, r.diagnostic_id);
        out.push_back(*r.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已批量读取全文"));
}

Result<std::vector<std::uint64_t>> IOService::count_lines_many(std::span<const std::string> paths) const {
    std::vector<std::uint64_t> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto r = count_lines(p);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::vector<std::uint64_t>>(r.status, r.diagnostic_id);
        out.push_back(*r.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已批量统计行数"));
}

Result<std::vector<std::string>> IOService::read_text_preview_many(std::span<const std::string> paths, std::uint64_t max_chars) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto r = read_text_preview(p, max_chars);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::vector<std::string>>(r.status, r.diagnostic_id);
        out.push_back(*r.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已批量读取文本预览"));
}

Result<void> IOService::export_body_summaries_many(std::span<const BodyId> body_ids, std::span<const std::string> paths) const {
    if (body_ids.size() != paths.size()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "批量导出失败：输入数量不一致", "批量导出失败");
    for (std::size_t i = 0; i < body_ids.size(); ++i) {
        const auto r = export_body_summary_txt(body_ids[i], paths[i]);
        if (r.status != StatusCode::Ok) return r;
    }
    return ok_void(state_->create_diagnostic("已批量导出体摘要"));
}

Result<std::uint64_t> IOService::compare_file_text_many_equal(std::span<const std::string> lhs, std::span<const std::string> rhs) const {
    if (lhs.size() != rhs.size()) return detail::invalid_input_result<std::uint64_t>(*state_, diag_codes::kIoImportFailure, "批量比较失败：输入数量不一致", "批量比较失败");
    std::uint64_t eq = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const auto r = compare_file_text(lhs[i], rhs[i]);
        if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::uint64_t>(r.status, r.diagnostic_id);
        if (*r.value) ++eq;
    }
    return ok_result<std::uint64_t>(eq, state_->create_diagnostic("已批量比较文件文本"));
}

Result<std::vector<std::pair<std::string, std::string>>> IOService::detect_formats_with_paths(std::span<const std::string> paths) const {
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) return error_result<std::vector<std::pair<std::string, std::string>>>(fmt.status, fmt.diagnostic_id);
        out.emplace_back(p, *fmt.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已查询路径格式对"));
}

Result<std::string> IOService::first_importable_path(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        if (std::filesystem::exists(p) && detect_format(p).value.value_or("unknown") != "unknown") return ok_result(std::string(p), state_->create_diagnostic("已查询首个可导入路径"));
    }
    return ok_result(std::string{}, state_->create_diagnostic("已查询首个可导入路径"));
}

Result<std::string> IOService::first_exportable_path(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status == StatusCode::Ok && fmt.value.has_value() && *fmt.value != "unknown") return ok_result(std::string(p), state_->create_diagnostic("已查询首个可导出路径"));
    }
    return ok_result(std::string{}, state_->create_diagnostic("已查询首个可导出路径"));
}

Result<std::vector<BodyId>> IOService::import_existing_auto(std::span<const std::string> paths, const ImportOptions& options) {
    std::vector<std::string> existing;
    for (const auto& p : paths) if (std::filesystem::exists(p)) existing.push_back(p);
    return import_many_auto(std::span<const std::string>(existing), options);
}

Result<void> IOService::export_auto_to_directory(std::span<const BodyId> body_ids, std::string_view directory, std::string_view ext, const ExportOptions& options) {
    if (directory.empty()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "目录导出失败：目录为空", "目录导出失败");
    std::vector<std::string> paths;
    paths.reserve(body_ids.size());
    for (std::size_t i = 0; i < body_ids.size(); ++i) {
        auto p = compose_path(directory, "body_" + std::to_string(i), ext);
        if (p.status != StatusCode::Ok || !p.value.has_value()) return error_result<void>(p.status, p.diagnostic_id);
        paths.push_back(*p.value);
    }
    return export_many_auto(body_ids, std::span<const std::string>(paths), options);
}

Result<std::uint64_t> IOService::import_existing_auto_count(std::span<const std::string> paths, const ImportOptions& options) {
    const auto r = import_existing_auto(paths, options);
    if (r.status != StatusCode::Ok || !r.value.has_value()) return error_result<std::uint64_t>(r.status, r.diagnostic_id);
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(r.value->size()), state_->create_diagnostic("已统计现有文件自动导入数量"));
}

Result<std::uint64_t> IOService::export_auto_to_directory_count(std::span<const BodyId> body_ids, std::string_view directory, std::string_view ext, const ExportOptions& options) {
    const auto r = export_auto_to_directory(body_ids, directory, ext, options);
    if (r.status != StatusCode::Ok) return error_result<std::uint64_t>(r.status, r.diagnostic_id);
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(body_ids.size()), state_->create_diagnostic("已统计目录自动导出数量"));
}

Result<std::vector<std::string>> IOService::summarize_files_txt(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        const auto exists = std::filesystem::exists(p);
        const auto size = exists ? static_cast<std::uint64_t>(std::filesystem::file_size(p)) : 0;
        out.push_back(std::string(p) + " | exists=" + (exists ? "true" : "false") + " | size=" + std::to_string(size));
    }
    return ok_result(std::move(out), state_->create_diagnostic("已汇总文件文本信息"));
}

Result<void> IOService::export_files_summary_txt(std::span<const std::string> paths, std::string_view out_path) const {
    auto lines = summarize_files_txt(paths);
    if (lines.status != StatusCode::Ok || !lines.value.has_value()) return error_result<void>(lines.status, lines.diagnostic_id);
    std::ofstream out{std::string(out_path)};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "文件摘要导出失败：无法打开输出文件", "文件摘要导出失败");
    for (const auto& line : *lines.value) out << line << "\n";
    return ok_void(state_->create_diagnostic("已导出文件摘要文本"));
}

Result<std::string> IOService::canonical_or_normalized_path(std::string_view path) const {
    return normalize_path(path);
}

Result<std::string> IOService::relative_to(std::string_view path, std::string_view base) const {
    if (path.empty() || base.empty()) {
        return detail::invalid_input_result<std::string>(*state_, diag_codes::kIoImportFailure, "相对路径计算失败：输入为空", "相对路径计算失败");
    }
    std::error_code ec;
    const auto rel = std::filesystem::relative(std::filesystem::path(path), std::filesystem::path(base), ec);
    if (ec) {
        return ok_result(std::filesystem::path(path).lexically_relative(std::filesystem::path(base)).string(),
                         state_->create_diagnostic("已计算相对路径"));
    }
    return ok_result(rel.string(), state_->create_diagnostic("已计算相对路径"));
}

Result<std::string> IOService::common_parent_directory(std::span<const std::string> paths) const {
    if (paths.empty()) return ok_result(std::string{}, state_->create_diagnostic("已计算公共父目录"));
    std::filesystem::path common = std::filesystem::path(paths.front()).parent_path();
    for (std::size_t i = 1; i < paths.size() && !common.empty(); ++i) {
        auto p = std::filesystem::path(paths[i]).parent_path();
        while (!common.empty() && p.string().rfind(common.string(), 0) != 0) {
            common = common.parent_path();
        }
    }
    return ok_result(common.string(), state_->create_diagnostic("已计算公共父目录"));
}

Result<std::vector<std::string>> IOService::unique_paths(std::span<const std::string> paths) const {
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已去重路径"));
}

Result<std::vector<std::string>> IOService::sort_paths_lex(std::span<const std::string> paths) const {
    std::vector<std::string> out(paths.begin(), paths.end());
    std::sort(out.begin(), out.end());
    return ok_result(std::move(out), state_->create_diagnostic("已排序路径"));
}

Result<std::vector<std::pair<std::string, std::uint64_t>>> IOService::count_by_format(std::span<const std::string> paths) const {
    std::unordered_map<std::string, std::uint64_t> counts;
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) return error_result<std::vector<std::pair<std::string, std::uint64_t>>>(fmt.status, fmt.diagnostic_id);
        ++counts[*fmt.value];
    }
    std::vector<std::pair<std::string, std::uint64_t>> out;
    out.reserve(counts.size());
    for (const auto& kv : counts) out.push_back(kv);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b){ return a.first < b.first; });
    return ok_result(std::move(out), state_->create_diagnostic("已按格式计数"));
}

Result<std::vector<std::string>> IOService::paths_of_format(std::span<const std::string> paths, std::string_view format) const {
    std::vector<std::string> out;
    for (const auto& p : paths) {
        const auto fmt = detect_format(p);
        if (fmt.status != StatusCode::Ok || !fmt.value.has_value()) return error_result<std::vector<std::string>>(fmt.status, fmt.diagnostic_id);
        if (*fmt.value == format) out.push_back(p);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已按格式筛选路径"));
}

Result<void> IOService::rename_file(std::string_view from, std::string_view to) const {
    return move_file(from, to);
}

Result<void> IOService::move_file(std::string_view from, std::string_view to) const {
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(from), std::filesystem::path(to), ec);
    if (ec) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "移动文件失败", "移动文件失败");
    return ok_void(state_->create_diagnostic("已移动文件"));
}

Result<void> IOService::ensure_directory(std::string_view directory) const {
    if (directory.empty()) return detail::invalid_input_void(*state_, diag_codes::kIoExportFailure, "目录创建失败：目录为空", "目录创建失败");
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(directory), ec);
    if (ec) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "目录创建失败", "目录创建失败");
    return ok_void(state_->create_diagnostic("已确认目录可用"));
}

Result<bool> IOService::directory_exists(std::string_view directory) const {
    if (directory.empty()) return ok_result(false, state_->create_diagnostic("已查询目录存在性"));
    return ok_result(std::filesystem::is_directory(std::filesystem::path(directory)), state_->create_diagnostic("已查询目录存在性"));
}

Result<std::vector<std::string>> IOService::list_files_in_directory(std::string_view directory) const {
    if (!std::filesystem::is_directory(std::filesystem::path(directory))) {
        return detail::invalid_input_result<std::vector<std::string>>(*state_, diag_codes::kIoImportFailure, "目录扫描失败：目录无效", "目录扫描失败");
    }
    std::vector<std::string> out;
    for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(directory))) {
        if (e.is_regular_file()) out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end());
    return ok_result(std::move(out), state_->create_diagnostic("已扫描目录文件"));
}

Result<std::vector<std::string>> IOService::list_files_recursive(std::string_view directory) const {
    if (!std::filesystem::is_directory(std::filesystem::path(directory))) {
        return detail::invalid_input_result<std::vector<std::string>>(*state_, diag_codes::kIoImportFailure, "递归扫描失败：目录无效", "递归扫描失败");
    }
    std::vector<std::string> out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(std::filesystem::path(directory))) {
        if (e.is_regular_file()) out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end());
    return ok_result(std::move(out), state_->create_diagnostic("已递归扫描目录文件"));
}

Result<std::uint64_t> IOService::count_files_in_directory(std::string_view directory, bool recursive) const {
    const auto files = recursive ? list_files_recursive(directory) : list_files_in_directory(directory);
    if (files.status != StatusCode::Ok || !files.value.has_value()) return error_result<std::uint64_t>(files.status, files.diagnostic_id);
    return ok_result<std::uint64_t>(static_cast<std::uint64_t>(files.value->size()), state_->create_diagnostic("已统计目录文件数量"));
}

Result<void> IOService::write_lines(std::string_view path, std::span<const std::string> lines) const {
    std::ofstream out{std::string(path)};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "写入文本行失败：无法打开文件", "写入文本行失败");
    for (const auto& line : lines) out << line << "\n";
    return ok_void(state_->create_diagnostic("已写入文本行"));
}

Result<std::vector<std::string>> IOService::read_lines(std::string_view path) const {
    std::ifstream in{std::string(path)};
    if (!in) return detail::failed_result<std::vector<std::string>>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure, "读取文本行失败：无法打开文件", "读取文本行失败");
    std::vector<std::string> out;
    std::string line;
    while (std::getline(in, line)) out.push_back(line);
    return ok_result(std::move(out), state_->create_diagnostic("已读取文本行"));
}

Result<std::vector<std::string>> IOService::grep_lines_contains(std::string_view path, std::string_view token) const {
    const auto lines = read_lines(path);
    if (lines.status != StatusCode::Ok || !lines.value.has_value()) return error_result<std::vector<std::string>>(lines.status, lines.diagnostic_id);
    std::vector<std::string> out;
    for (const auto& line : *lines.value) if (line.find(token) != std::string::npos) out.push_back(line);
    return ok_result(std::move(out), state_->create_diagnostic("已按token筛选文本行"));
}

Result<std::uint64_t> IOService::replace_in_file_text(std::string_view path, std::string_view from, std::string_view to) const {
    if (from.empty()) return detail::invalid_input_result<std::uint64_t>(*state_, diag_codes::kIoExportFailure, "文本替换失败：from为空", "文本替换失败");
    const auto all = read_all_text(path);
    if (all.status != StatusCode::Ok || !all.value.has_value()) return error_result<std::uint64_t>(all.status, all.diagnostic_id);
    std::string text = *all.value;
    std::uint64_t replaced = 0;
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
        ++replaced;
    }
    const auto write = write_text_snapshot(path, text);
    if (write.status != StatusCode::Ok) return error_result<std::uint64_t>(write.status, write.diagnostic_id);
    return ok_result<std::uint64_t>(replaced, state_->create_diagnostic("已替换文件文本"));
}

Result<void> IOService::prepend_text(std::string_view path, std::string_view content) const {
    const auto all = read_all_text(path);
    if (all.status != StatusCode::Ok || !all.value.has_value()) return error_result<void>(all.status, all.diagnostic_id);
    return write_text_snapshot(path, std::string(content) + *all.value);
}

Result<void> IOService::truncate_file(std::string_view path) const {
    std::ofstream out{std::string(path), std::ios::trunc};
    if (!out) return detail::failed_void(*state_, StatusCode::OperationFailed, diag_codes::kIoExportFailure, "文件清空失败", "文件清空失败");
    return ok_void(state_->create_diagnostic("已清空文件"));
}

Result<std::string> IOService::file_stem(std::string_view path) const {
    return ok_result(std::filesystem::path(path).stem().string(), state_->create_diagnostic("已提取文件主名"));
}

Result<std::string> IOService::extension_of(std::string_view path) const {
    return ok_result(std::filesystem::path(path).extension().string(), state_->create_diagnostic("已提取扩展名"));
}

Result<std::string> IOService::with_stem(std::string_view path, std::string_view new_stem) const {
    if (new_stem.empty()) return detail::invalid_input_result<std::string>(*state_, diag_codes::kIoExportFailure, "路径主名替换失败：主名为空", "路径主名替换失败");
    auto p = std::filesystem::path(path);
    const auto ext = p.extension().string();
    p.replace_filename(std::string(new_stem) + ext);
    return ok_result(p.string(), state_->create_diagnostic("已替换路径主名"));
}

Result<std::string> IOService::append_suffix_before_ext(std::string_view path, std::string_view suffix) const {
    auto p = std::filesystem::path(path);
    const auto name = p.stem().string() + std::string(suffix) + p.extension().string();
    p.replace_filename(name);
    return ok_result(p.string(), state_->create_diagnostic("已在扩展名前追加后缀"));
}

Result<std::vector<std::string>> IOService::generate_sequential_paths(std::string_view directory, std::string_view prefix, std::string_view ext, std::uint64_t count) const {
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        const auto p = compose_path(directory, std::string(prefix) + std::to_string(i), ext);
        if (p.status != StatusCode::Ok || !p.value.has_value()) return error_result<std::vector<std::string>>(p.status, p.diagnostic_id);
        out.push_back(*p.value);
    }
    return ok_result(std::move(out), state_->create_diagnostic("已生成连续路径"));
}

Result<std::string> IOService::first_writable_path(std::span<const std::string> paths) const {
    for (const auto& p : paths) {
        const auto parent = std::filesystem::path(p).parent_path();
        if (parent.empty() || std::filesystem::exists(parent)) return ok_result(std::string(p), state_->create_diagnostic("已查询首个可写路径"));
    }
    return ok_result(std::string{}, state_->create_diagnostic("已查询首个可写路径"));
}

Result<std::uint64_t> IOService::export_auto_existing_only(std::span<const BodyId> body_ids, std::span<const std::string> paths, const ExportOptions& options) {
    if (body_ids.size() != paths.size()) return detail::invalid_input_result<std::uint64_t>(*state_, diag_codes::kIoExportFailure, "条件导出失败：输入数量不一致", "条件导出失败");
    std::uint64_t exported = 0;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (!std::filesystem::exists(std::filesystem::path(paths[i]).parent_path())) continue;
        const auto r = export_auto(body_ids[i], paths[i], options);
        if (r.status == StatusCode::Ok) ++exported;
    }
    return ok_result<std::uint64_t>(exported, state_->create_diagnostic("已完成条件自动导出"));
}

Result<std::vector<BodyId>> IOService::import_auto_existing_strict(std::span<const std::string> paths, const ImportOptions& options) {
    for (const auto& p : paths) {
        if (!std::filesystem::exists(p)) {
            return detail::failed_result<std::vector<BodyId>>(*state_, StatusCode::OperationFailed, diag_codes::kIoImportFailure, "严格导入失败：包含不存在路径", "严格导入失败");
        }
    }
    return import_many_auto(paths, options);
}

Result<std::vector<std::string>> IOService::summarize_format_histogram_txt(std::span<const std::string> paths) const {
    const auto counts = count_by_format(paths);
    if (counts.status != StatusCode::Ok || !counts.value.has_value()) return error_result<std::vector<std::string>>(counts.status, counts.diagnostic_id);
    std::vector<std::string> out;
    out.reserve(counts.value->size());
    for (const auto& kv : *counts.value) out.push_back(kv.first + ": " + std::to_string(kv.second));
    return ok_result(std::move(out), state_->create_diagnostic("已汇总格式直方文本"));
}

}  // namespace axiom
