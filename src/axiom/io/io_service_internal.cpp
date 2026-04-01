#include "axiom/internal/io/io_service_internal.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <system_error>

#include "axiom/heal/heal_services.h"
#include "axiom/internal/core/diagnostic_helpers.h"
#include "axiom/internal/rep/representation_internal_utils.h"

namespace axiom {
namespace io_internal {


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

void append_padding_4(std::vector<std::uint8_t>& out) {
    while ((out.size() % 4) != 0) out.push_back(0);
}

void parse_axiom_interchange_metadata_lines(std::istream& in, detail::BodyRecord& record) {
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
}

std::string strip_leading_hash_lines(const std::string& in) {
    std::istringstream sin(in);
    std::ostringstream sout;
    std::string line;
    while (std::getline(sin, line)) {
        std::size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) != 0) {
            ++i;
        }
        if (i < line.size() && line[i] == '#') {
            continue;
        }
        sout << line << '\n';
    }
    return sout.str();
}

void fill_body_record_from_axmjson_content(const std::string& content, detail::BodyRecord& record) {
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
    if (extract_json_number(content, "bbox_min_x", min_x) && extract_json_number(content, "bbox_min_y", min_y) &&
        extract_json_number(content, "bbox_min_z", min_z) && extract_json_number(content, "bbox_max_x", max_x) &&
        extract_json_number(content, "bbox_max_y", max_y) && extract_json_number(content, "bbox_max_z", max_z)) {
        record.bbox = detail::make_bbox({min_x, min_y, min_z}, {max_x, max_y, max_z});
    }
}

void write_axmjson_payload(std::ostream& out, const detail::BodyRecord& body, const ExportOptions& options,
                           std::string_view format_json_value) {
    out << "{\n";
    out << "  \"format\": \"" << format_json_value << "\",\n";
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
}

std::uint32_t crc32_ieee_update(std::uint32_t crc, const std::uint8_t* p, std::size_t n) {
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

std::uint32_t crc32_ieee(const std::vector<std::uint8_t>& d) {
    return crc32_ieee_update(0, d.data(), d.size());
}

void append_le_u16(std::vector<std::uint8_t>& z, std::uint16_t v) {
    z.push_back(static_cast<std::uint8_t>(v & 0xFF));
    z.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void append_le_u32(std::vector<std::uint8_t>& z, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        z.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

std::vector<std::uint8_t> build_zip_store_archive(
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files) {
    std::vector<std::uint8_t> out;
    std::vector<std::uint32_t> local_offsets;
    local_offsets.reserve(files.size());
    for (const auto& entry : files) {
        local_offsets.push_back(static_cast<std::uint32_t>(out.size()));
        const std::string& name = entry.first;
        const std::vector<std::uint8_t>& data = entry.second;
        const std::uint32_t crc = crc32_ieee(data);
        append_le_u32(out, 0x04034b50u);
        append_le_u16(out, 20);
        append_le_u16(out, 0);
        append_le_u16(out, 0);
        append_le_u16(out, 0);
        append_le_u16(out, 0);
        append_le_u32(out, crc);
        append_le_u32(out, static_cast<std::uint32_t>(data.size()));
        append_le_u32(out, static_cast<std::uint32_t>(data.size()));
        append_le_u16(out, static_cast<std::uint16_t>(name.size()));
        append_le_u16(out, 0);
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(), data.begin(), data.end());
    }
    const std::uint32_t cd_start = static_cast<std::uint32_t>(out.size());
    std::uint32_t cd_entry = 0;
    for (std::size_t fi = 0; fi < files.size(); ++fi) {
        const std::string& name = files[fi].first;
        const std::vector<std::uint8_t>& data = files[fi].second;
        const std::uint32_t crc = crc32_ieee(data);
        append_le_u32(out, 0x02014b50u);
        append_le_u16(out, 20);
        append_le_u16(out, 20);
        append_le_u16(out, 0);
        append_le_u16(out, 0);
        append_le_u16(out, 0);
        append_le_u16(out, 0);
        append_le_u32(out, crc);
        append_le_u32(out, static_cast<std::uint32_t>(data.size()));
        append_le_u32(out, static_cast<std::uint32_t>(data.size()));
        append_le_u16(out, static_cast<std::uint16_t>(name.size()));
        append_le_u16(out, 0);
        append_le_u16(out, 0);
        append_le_u16(out, 0);
        append_le_u16(out, 0);
        append_le_u32(out, 0);
        append_le_u32(out, local_offsets[fi]);
        out.insert(out.end(), name.begin(), name.end());
        ++cd_entry;
    }
    const std::uint32_t cd_size = static_cast<std::uint32_t>(out.size() - cd_start);
    append_le_u32(out, 0x06054b50u);
    append_le_u16(out, 0);
    append_le_u16(out, 0);
    append_le_u16(out, static_cast<std::uint16_t>(files.size()));
    append_le_u16(out, static_cast<std::uint16_t>(files.size()));
    append_le_u32(out, cd_size);
    append_le_u32(out, cd_start);
    append_le_u16(out, 0);
    return out;
}

bool zip_parse_stored_locals(const std::vector<std::uint8_t>& z,
                             std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& out_files) {
    out_files.clear();
    std::size_t pos = 0;
    while (pos + 30 <= z.size()) {
        std::uint32_t sig = 0;
        std::memcpy(&sig, z.data() + pos, 4);
        if (sig != 0x04034b50u) {
            break;
        }
        const std::uint16_t name_len = static_cast<std::uint16_t>(z[pos + 26] | (z[pos + 27] << 8));
        const std::uint16_t extra_len = static_cast<std::uint16_t>(z[pos + 28] | (z[pos + 29] << 8));
        const std::uint16_t method = static_cast<std::uint16_t>(z[pos + 8] | (z[pos + 9] << 8));
        std::uint32_t comp = 0;
        std::uint32_t uncomp = 0;
        std::memcpy(&comp, z.data() + pos + 18, 4);
        std::memcpy(&uncomp, z.data() + pos + 22, 4);
        if (method != 0) {
            return false;
        }
        const std::size_t hdr_end = pos + 30 + name_len + extra_len;
        if (hdr_end + comp > z.size()) {
            return false;
        }
        std::string name(reinterpret_cast<const char*>(z.data() + pos + 30), name_len);
        std::vector<std::uint8_t> data(z.begin() + static_cast<std::ptrdiff_t>(hdr_end),
                                      z.begin() + static_cast<std::ptrdiff_t>(hdr_end + comp));
        out_files.emplace_back(std::move(name), std::move(data));
        pos = hdr_end + comp;
    }
    return !out_files.empty();
}

Result<void> mesh_export_strict_gate(detail::KernelState& state, RepresentationConversionService& convert, MeshId mesh_id,
                                     const ExportOptions& options, BodyId body_id) {
    if (options.compatibility_mode) {
        return ok_void(state.create_diagnostic("兼容模式：跳过网格导出严格门控"));
    }
    const auto insp = convert.inspect_mesh(mesh_id);
    if (insp.status != StatusCode::Ok) {
        return error_void(insp.status, insp.diagnostic_id);
    }
    if (!insp.value.has_value()) {
        return detail::failed_void(state, StatusCode::OperationFailed, diag_codes::kIoExportMeshStrictQaFailed,
                                   "严格导出失败：网格检查不可用", "严格导出失败", {body_id.value});
    }
    const auto& r = *insp.value;
    if (r.has_out_of_range_indices || r.has_degenerate_triangles) {
        return detail::failed_void(state, StatusCode::OperationFailed, diag_codes::kIoExportMeshStrictQaFailed,
                                   "严格导出失败：网格存在越界索引或退化三角形", "严格导出失败", {body_id.value});
    }
    return ok_void(state.create_diagnostic("网格导出严格检查通过"));
}

Result<void> mesh_export_write_validation_sidecar(detail::KernelState& state, RepresentationConversionService& convert,
                                                  MeshId mesh_id, std::string_view main_path,
                                                  const ExportOptions& options) {
    if (!options.write_mesh_validation_report) {
        return ok_void(state.create_diagnostic("未请求网格验证侧车报告"));
    }
    std::filesystem::path p {std::string(main_path)};
    const auto sidecar = p.parent_path() / (p.stem().string() + ".mesh_report.json");
    std::error_code ec;
    std::filesystem::create_directories(sidecar.parent_path(), ec);
    const auto w = convert.export_mesh_report_json(mesh_id, sidecar.string());
    if (w.status != StatusCode::Ok) {
        return w;
    }
    std::vector<Issue> issues;
    auto iss = detail::make_info_issue(diag_codes::kIoExportMeshReportSidecar,
                                       "已写入网格验证 JSON 侧车: " + sidecar.string());
    issues.push_back(std::move(iss));
    return ok_void(state.create_diagnostic("已写入网格验证报告侧车", std::move(issues)));
}

Result<void> merge_mesh_export_sidecar_summary(detail::KernelState& state, const Result<void>& sidecar,
                                               std::string export_summary) {
    if (sidecar.status != StatusCode::Ok) {
        return sidecar;
    }
    std::vector<Issue> merged;
    if (sidecar.diagnostic_id.value != 0) {
        const auto it = state.diagnostics.find(sidecar.diagnostic_id.value);
        if (it != state.diagnostics.end()) {
            merged = it->second.issues;
        }
    }
    return ok_void(state.create_diagnostic(std::move(export_summary), std::move(merged)));
}

std::optional<std::string> parse_obj_text_to_mesh(std::string_view text, detail::MeshRecord& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();
    std::vector<std::string> lines;
    {
        std::string t {text};
        std::istringstream in(t);
        std::string line;
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            lines.push_back(std::move(line));
        }
    }
    for (const auto& raw : lines) {
        if (raw.empty() || raw.front() == '#') {
            continue;
        }
        std::istringstream ls(raw);
        std::string tag;
        ls >> tag;
        if (tag == "v") {
            Scalar x {}, y {}, z {};
            ls >> x >> y >> z;
            if (!ls) {
                return std::string("OBJ 解析失败：顶点行非法");
            }
            mesh.vertices.push_back(Point3 {x, y, z});
        } else if (tag == "f") {
            std::vector<Index> face;
            std::string part;
            while (ls >> part) {
                std::size_t slash = part.find('/');
                const std::string idx_str = slash == std::string::npos ? part : part.substr(0, slash);
                if (idx_str.empty()) {
                    return std::string("OBJ 解析失败：面索引非法");
                }
                const long long vi = std::stoll(idx_str);
                if (vi == 0) {
                    return std::string("OBJ 解析失败：零索引非法");
                }
                Index vix {};
                if (vi > 0) {
                    if (static_cast<std::size_t>(vi) > mesh.vertices.size()) {
                        return std::string("OBJ 解析失败：面索引越界");
                    }
                    vix = static_cast<Index>(vi - 1);
                } else {
                    const auto off = static_cast<std::size_t>(-vi);
                    if (off > mesh.vertices.size() || off == 0) {
                        return std::string("OBJ 解析失败：负索引越界");
                    }
                    vix = static_cast<Index>(mesh.vertices.size() - off);
                }
                face.push_back(vix);
            }
            if (face.size() < 3) {
                return std::string("OBJ 解析失败：面顶点数不足");
            }
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                mesh.indices.push_back(face[0]);
                mesh.indices.push_back(face[i]);
                mesh.indices.push_back(face[i + 1]);
            }
        }
    }
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return std::string("OBJ 解析失败：无有效网格数据");
    }
    return std::nullopt;
}

std::string build_3mf_model_xml_from_mesh(const detail::MeshRecord& mesh) {
    std::ostringstream xml;
    xml << std::setprecision(17);
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<model unit=\"millimeter\" xml:lang=\"en-US\" "
           "xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n";
    xml << "<resources><object id=\"1\" type=\"model\"><mesh><vertices>\n";
    for (const auto& v : mesh.vertices) {
        xml << "<vertex x=\"" << v.x << "\" y=\"" << v.y << "\" z=\"" << v.z << "\"/>\n";
    }
    xml << "</vertices><triangles>\n";
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        xml << "<triangle v1=\"" << mesh.indices[i] << "\" v2=\"" << mesh.indices[i + 1] << "\" v3=\""
            << mesh.indices[i + 2] << "\"/>\n";
    }
    xml << "</triangles></mesh></object></resources><build><item objectid=\"1\"/></build></model>\n";
    return xml.str();
}

bool extract_3mf_model_xml(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files,
                           std::string& out_xml) {
    for (const auto& e : files) {
        const std::string& n = e.first;
        if (n.size() >= 13 && n.compare(n.size() - 13, 13, "3dmodel.model") == 0) {
            out_xml.assign(e.second.begin(), e.second.end());
            return true;
        }
    }
    return false;
}

std::optional<std::string> parse_3mf_model_xml_to_mesh(std::string_view xml, detail::MeshRecord& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();
    static const std::regex vtx(R"(<vertex\s+x=\"([^\"]+)\"\s+y=\"([^\"]+)\"\s+z=\"([^\"]+)\"\s*/>)");
    static const std::regex tri(R"(<triangle\s+v1=\"([0-9]+)\"\s+v2=\"([0-9]+)\"\s+v3=\"([0-9]+)\"\s*/>)");
    const std::string s {xml};
    for (std::sregex_iterator it(s.begin(), s.end(), vtx), end; it != end; ++it) {
        const auto& m = *it;
        mesh.vertices.push_back(
            Point3 {std::stod(m[1].str()), std::stod(m[2].str()), std::stod(m[3].str())});
    }
    for (std::sregex_iterator it(s.begin(), s.end(), tri), end; it != end; ++it) {
        const auto& m = *it;
        mesh.indices.push_back(static_cast<Index>(std::stoul(m[1].str())));
        mesh.indices.push_back(static_cast<Index>(std::stoul(m[2].str())));
        mesh.indices.push_back(static_cast<Index>(std::stoul(m[3].str())));
    }
    if (mesh.vertices.empty() || mesh.indices.empty() || (mesh.indices.size() % 3) != 0) {
        return std::string("3MF 解析失败：网格数据不完整");
    }
    if (detail::has_out_of_range_indices(mesh.vertices, mesh.indices)) {
        return std::string("3MF 解析失败：三角形索引越界");
    }
    return std::nullopt;
}

int base64_decode_char(unsigned char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<int>(c - 'A');
    }
    if (c >= 'a' && c <= 'z') {
        return static_cast<int>(c - 'a') + 26;
    }
    if (c >= '0' && c <= '9') {
        return static_cast<int>(c - '0') + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view in) {
    std::vector<std::uint8_t> out;
    out.reserve(in.size() * 3 / 4);
    std::uint32_t buf = 0;
    int bits = 0;
    for (const unsigned char c : in) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            continue;
        }
        if (c == '=') {
            break;
        }
        const int v = base64_decode_char(c);
        if (v < 0) {
            return std::nullopt;
        }
        buf = (buf << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

bool read_f32_le(const std::uint8_t*& p, const std::uint8_t* end, float& out) {
    if (p + 4 > end) {
        return false;
    }
    std::uint32_t u {};
    std::memcpy(&u, p, 4);
    p += 4;
    std::memcpy(&out, &u, sizeof(float));
    return true;
}

bool stl_ascii_parse(std::string_view text, detail::MeshRecord& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();
    std::istringstream in {std::string(text)};
    std::string token;
    in >> token;
    if (lower_copy(token) != "solid") {
        return false;
    }
    std::string rest_line;
    std::getline(in, rest_line);
    while (in >> token) {
        if (lower_copy(token) == "endsolid") {
            break;
        }
        if (lower_copy(token) != "facet") {
            return false;
        }
        in >> token;
        if (lower_copy(token) != "normal") {
            return false;
        }
        double nx {};
        double ny {};
        double nz {};
        in >> nx >> ny >> nz;
        (void)nx;
        (void)ny;
        (void)nz;
        in >> token;
        if (lower_copy(token) != "outer") {
            return false;
        }
        in >> token;
        if (lower_copy(token) != "loop") {
            return false;
        }
        Point3 v[3] {};
        for (int i = 0; i < 3; ++i) {
            in >> token;
            if (lower_copy(token) != "vertex") {
                return false;
            }
            in >> v[i].x >> v[i].y >> v[i].z;
        }
        in >> token;
        if (lower_copy(token) != "endloop") {
            return false;
        }
        in >> token;
        if (lower_copy(token) != "endfacet") {
            return false;
        }
        const Index base = static_cast<Index>(mesh.vertices.size());
        mesh.vertices.push_back(v[0]);
        mesh.vertices.push_back(v[1]);
        mesh.vertices.push_back(v[2]);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
    }
    return !mesh.vertices.empty();
}

bool stl_binary_parse(const std::vector<std::uint8_t>& data, detail::MeshRecord& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();
    if (data.size() < 84) {
        return false;
    }
    std::uint32_t tri_count {};
    std::memcpy(&tri_count, data.data() + 80, 4);
    constexpr std::uint64_t kMaxTris = 50'000'000;
    if (tri_count == 0 || static_cast<std::uint64_t>(tri_count) > kMaxTris) {
        return false;
    }
    const std::uint64_t expected = 84ULL + static_cast<std::uint64_t>(tri_count) * 50ULL;
    if (expected != data.size()) {
        return false;
    }
    const std::uint8_t* p = data.data() + 84;
    const std::uint8_t* end = data.data() + data.size();
    mesh.vertices.reserve(static_cast<std::size_t>(tri_count) * 3);
    mesh.indices.reserve(static_cast<std::size_t>(tri_count) * 3);
    for (std::uint32_t t = 0; t < tri_count; ++t) {
        float nx {};
        float ny {};
        float nz {};
        if (!read_f32_le(p, end, nx) || !read_f32_le(p, end, ny) || !read_f32_le(p, end, nz)) {
            return false;
        }
        (void)nx;
        (void)ny;
        (void)nz;
        Point3 v[3] {};
        for (int i = 0; i < 3; ++i) {
            float fx {};
            float fy {};
            float fz {};
            if (!read_f32_le(p, end, fx) || !read_f32_le(p, end, fy) || !read_f32_le(p, end, fz)) {
                return false;
            }
            v[i].x = static_cast<Scalar>(fx);
            v[i].y = static_cast<Scalar>(fy);
            v[i].z = static_cast<Scalar>(fz);
        }
        if (p + 2 > end) {
            return false;
        }
        p += 2;
        const Index base = static_cast<Index>(mesh.vertices.size());
        mesh.vertices.push_back(v[0]);
        mesh.vertices.push_back(v[1]);
        mesh.vertices.push_back(v[2]);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
    }
    return true;
}

std::optional<std::string> parse_stl_bytes(const std::vector<std::uint8_t>& data, detail::MeshRecord& mesh) {
    if (data.size() < 84) {
        return std::string("STL 文件过小");
    }
    std::uint32_t tri_le {};
    std::memcpy(&tri_le, data.data() + 80, 4);
    const std::uint64_t expected = 84ULL + static_cast<std::uint64_t>(tri_le) * 50ULL;
    if (expected == data.size() && tri_le > 0 && stl_binary_parse(data, mesh)) {
        mesh.label = "stl_import";
        mesh.tessellation_strategy = "io_import_stl";
        mesh.bbox = detail::mesh_bbox_from_vertices(mesh.vertices);
        return std::nullopt;
    }
    std::string text(data.begin(), data.end());
    if (stl_ascii_parse(text, mesh)) {
        mesh.label = "stl_import";
        mesh.tessellation_strategy = "io_import_stl";
        mesh.bbox = detail::mesh_bbox_from_vertices(mesh.vertices);
        return std::nullopt;
    }
    return std::string("STL 解析失败：非合法二进制或 ASCII STL");
}

bool scan_json_uint_after(const std::string& s, std::size_t from, std::string_view key, std::uint64_t& out) {
    const auto pos = s.find(key, from);
    if (pos == std::string::npos) {
        return false;
    }
    std::size_t i = pos + key.size();
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0) {
        ++i;
    }
    if (i >= s.size() || s[i] != ':') {
        return false;
    }
    ++i;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0) {
        ++i;
    }
    out = 0;
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
        return false;
    }
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        out = out * 10ULL + static_cast<std::uint64_t>(s[i] - '0');
        ++i;
    }
    return true;
}

std::optional<std::string> parse_gltf_embedded_minimal(std::string_view json, detail::MeshRecord& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();
    const std::string json_str {json};
    static constexpr std::string_view k_uri_prefix = "data:application/octet-stream;base64,";
    const auto uri_pos = json_str.find(k_uri_prefix);
    if (uri_pos == std::string::npos) {
        return std::string("glTF 解析失败：未找到内嵌 base64 buffer（当前仅支持 AxiomKernel 导出的 data URI 格式）");
    }
    std::size_t b64_start = uri_pos + k_uri_prefix.size();
    const auto b64_end = json_str.find('"', b64_start);
    if (b64_end == std::string::npos) {
        return std::string("glTF 解析失败：base64 URI 未闭合");
    }
    const auto bin_opt = base64_decode(std::string_view(json_str.data() + b64_start, b64_end - b64_start));
    if (!bin_opt.has_value() || bin_opt->empty()) {
        return std::string("glTF 解析失败：base64 解码失败");
    }
    const std::vector<std::uint8_t>& bin = *bin_opt;

    const auto bv_key = json_str.find("\"bufferViews\":[", 0);
    if (bv_key == std::string::npos) {
        return std::string("glTF 解析失败：未找到 bufferViews");
    }
    const auto pos_bv = json_str.find(R"({"buffer":0,"byteOffset":)", bv_key);
    if (pos_bv == std::string::npos) {
        return std::string("glTF 解析失败：未找到 POSITION bufferView");
    }
    const auto idx_bv = json_str.find(R"({"buffer":0,"byteOffset":)", pos_bv + 1);
    if (idx_bv == std::string::npos) {
        return std::string("glTF 解析失败：未找到索引 bufferView");
    }
    std::uint64_t pos_off {};
    std::uint64_t pos_len {};
    std::uint64_t idx_off {};
    std::uint64_t idx_len {};
    if (!scan_json_uint_after(json_str, pos_bv, "\"byteOffset\"", pos_off) ||
        !scan_json_uint_after(json_str, pos_bv, "\"byteLength\"", pos_len) ||
        !scan_json_uint_after(json_str, idx_bv, "\"byteOffset\"", idx_off) ||
        !scan_json_uint_after(json_str, idx_bv, "\"byteLength\"", idx_len)) {
        return std::string("glTF 解析失败：bufferView 字段解析失败");
    }
    const auto t_pos = json_str.find("\"target\":34962", pos_bv);
    const auto t_idx = json_str.find("\"target\":34963", idx_bv);
    if (t_pos == std::string::npos || t_pos > idx_bv || t_idx == std::string::npos) {
        return std::string("glTF 解析失败：bufferView target 不符合预期");
    }

    const auto acc_key = json_str.find("\"accessors\":[", 0);
    if (acc_key == std::string::npos) {
        return std::string("glTF 解析失败：未找到 accessors");
    }
    const auto acc0 = json_str.find("{\"bufferView\":0", acc_key);
    const auto acc1 = json_str.find("{\"bufferView\":1", acc_key);
    if (acc0 == std::string::npos || acc1 == std::string::npos) {
        return std::string("glTF 解析失败：未找到 accessor 定义");
    }
    std::uint64_t vcount {};
    std::uint64_t icount {};
    if (!scan_json_uint_after(json_str, acc0, "\"count\"", vcount) ||
        !scan_json_uint_after(json_str, acc1, "\"count\"", icount)) {
        return std::string("glTF 解析失败：accessor count 解析失败");
    }
    if (json_str.find("\"componentType\":5126", acc0) == std::string::npos ||
        json_str.find("\"componentType\":5125", acc1) == std::string::npos) {
        return std::string("glTF 解析失败：accessor 分量类型不符合预期");
    }
    if (pos_off + pos_len > bin.size() || idx_off + idx_len > bin.size()) {
        return std::string("glTF 解析失败：bufferView 越界");
    }
    if (pos_len < vcount * 12 || idx_len < icount * 4) {
        return std::string("glTF 解析失败：buffer 长度与 accessor 不一致");
    }
    mesh.vertices.resize(static_cast<std::size_t>(vcount));
    for (std::uint64_t i = 0; i < vcount; ++i) {
        const std::size_t o = pos_off + static_cast<std::size_t>(i * 12);
        float fx {};
        float fy {};
        float fz {};
        std::memcpy(&fx, bin.data() + o, 4);
        std::memcpy(&fy, bin.data() + o + 4, 4);
        std::memcpy(&fz, bin.data() + o + 8, 4);
        mesh.vertices[static_cast<std::size_t>(i)] =
            Point3 {static_cast<Scalar>(fx), static_cast<Scalar>(fy), static_cast<Scalar>(fz)};
    }
    mesh.indices.resize(static_cast<std::size_t>(icount));
    for (std::uint64_t i = 0; i < icount; ++i) {
        const std::size_t o = idx_off + static_cast<std::size_t>(i * 4);
        std::uint32_t idx {};
        std::memcpy(&idx, bin.data() + o, 4);
        mesh.indices[static_cast<std::size_t>(i)] = idx;
    }
    mesh.label = "gltf_import";
    mesh.tessellation_strategy = "io_import_gltf";
    mesh.bbox = detail::mesh_bbox_from_vertices(mesh.vertices);
    return std::nullopt;
}

void append_issues_from_import_diag(detail::KernelState* state, std::vector<Issue>& issues, std::vector<Warning>& warnings,
                                    DiagnosticId diagnostic_id, std::initializer_list<std::uint64_t> fallback_related_entities) {
    if (diagnostic_id.value == 0 || state == nullptr) {
        return;
    }
    const auto diag_it = state->diagnostics.find(diagnostic_id.value);
    if (diag_it == state->diagnostics.end()) {
        return;
    }
    for (const auto& issue : diag_it->second.issues) {
        auto tracked_issue = issue;
        if (tracked_issue.related_entities.empty()) {
            tracked_issue.related_entities.assign(fallback_related_entities.begin(), fallback_related_entities.end());
        }
        const bool also_warn = tracked_issue.severity == IssueSeverity::Warning ||
                               tracked_issue.severity == IssueSeverity::Error ||
                               tracked_issue.severity == IssueSeverity::Fatal;
        const std::string wcode = tracked_issue.code;
        const std::string wmsg = tracked_issue.message;
        issues.push_back(std::move(tracked_issue));
        if (also_warn) {
            warnings.push_back(Warning {wcode, wmsg});
        }
    }
}

BodyId run_post_import_validation_pipeline(const std::shared_ptr<detail::KernelState>& state, BodyId body_id,
                                           const ImportOptions& options, const std::string& format_cn,
                                           std::vector<Issue>& issues, std::vector<Warning>& warnings) {
    BodyId result_body_id = body_id;
    if (!options.run_validation) {
        return result_body_id;
    }
    auto validation_issue = detail::make_info_issue(diag_codes::kIoPostImportValidation, format_cn + " 导入后已触发自动验证");
    validation_issue.related_entities = {body_id.value};
    validation_issue.stage = "io.post_import.validation";
    issues.push_back(std::move(validation_issue));

    ValidationService validation {state};
    const auto validation_result = validation.validate_all(result_body_id, ValidationMode::Standard);
    if (validation_result.status != StatusCode::Ok) {
        append_issues_from_import_diag(state.get(), issues, warnings, validation_result.diagnostic_id, {result_body_id.value});
        if (validation_result.diagnostic_id.value == 0) {
            const auto message = format_cn + " 导入后自动验证失败";
            auto fallback_issue = detail::make_warning_issue(diag_codes::kIoImportFailure, message);
            fallback_issue.related_entities = {result_body_id.value};
            issues.push_back(std::move(fallback_issue));
            warnings.push_back(Warning {std::string(diag_codes::kIoImportFailure), message});
        }

        if (options.auto_repair) {
            const auto repair_mode =
                options.repair_mode == RepairMode::ReportOnly ? RepairMode::Safe : options.repair_mode;
            auto repair_mode_issue = detail::make_info_issue(
                diag_codes::kIoPostImportRepairMode,
                format_cn + " 导入后自动修复策略已启用: mode=" + std::to_string(static_cast<int>(repair_mode)));
            repair_mode_issue.related_entities = {result_body_id.value};
            repair_mode_issue.stage = "io.post_import.repair_mode";
            issues.push_back(std::move(repair_mode_issue));

            RepairService repair {state};
            const auto repair_result = repair.auto_repair(result_body_id, repair_mode);
            if (repair_result.status == StatusCode::Ok && repair_result.value.has_value()) {
                result_body_id = repair_result.value->output;
                append_issues_from_import_diag(state.get(), issues, warnings, repair_result.value->diagnostic_id,
                                               {body_id.value, result_body_id.value});

                const auto repaired_validation = validation.validate_all(result_body_id, ValidationMode::Standard);
                if (repaired_validation.status != StatusCode::Ok) {
                    append_issues_from_import_diag(state.get(), issues, warnings, repaired_validation.diagnostic_id,
                                                   {result_body_id.value});
                }
            } else {
                append_issues_from_import_diag(state.get(), issues, warnings, repair_result.diagnostic_id, {body_id.value});
            }
        }
    }
    return result_body_id;
}

}  // namespace io_internal
}  // namespace axiom
