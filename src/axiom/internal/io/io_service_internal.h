#pragma once

#include <initializer_list>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <cstdint>
#include <type_traits>

#include "axiom/core/result.h"
#include "axiom/core/types.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/rep/representation_conversion_service.h"

namespace axiom {
namespace io_internal {

template <typename T>
void append_pod(std::vector<std::uint8_t>& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

std::string_view body_kind_name(detail::BodyKind kind);
detail::BodyKind parse_body_kind(std::string_view value);
bool parse_bbox_line(std::string_view line, BoundingBox& bbox);
bool parse_triplet_line(std::string_view line, std::string_view expected_prefix, Scalar& x, Scalar& y, Scalar& z);
std::string trim_comment(std::string line);
std::string json_escape(const std::string& in);
bool extract_json_string(const std::string& content, std::string_view key, std::string& out);
bool extract_json_number(const std::string& content, std::string_view key, Scalar& out);
std::string lower_copy(std::string value);
std::string base64_encode(std::span<const std::uint8_t> data);
void append_padding_4(std::vector<std::uint8_t>& out);
void parse_axiom_interchange_metadata_lines(std::istream& in, detail::BodyRecord& record);
std::string strip_leading_hash_lines(const std::string& in);
void fill_body_record_from_axmjson_content(const std::string& content, detail::BodyRecord& record);
void write_axmjson_payload(std::ostream& out, const detail::BodyRecord& body, const ExportOptions& options,
                           std::string_view format_json_value);
std::uint32_t crc32_ieee_update(std::uint32_t crc, const std::uint8_t* p, std::size_t n);
std::uint32_t crc32_ieee(const std::vector<std::uint8_t>& d);
void append_le_u16(std::vector<std::uint8_t>& z, std::uint16_t v);
void append_le_u32(std::vector<std::uint8_t>& z, std::uint32_t v);
std::vector<std::uint8_t> build_zip_store_archive(
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files);
bool zip_parse_stored_locals(const std::vector<std::uint8_t>& z,
                             std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& out_files);
Result<void> mesh_export_strict_gate(detail::KernelState& state, RepresentationConversionService& convert, MeshId mesh_id,
                                     const ExportOptions& options, BodyId body_id);
Result<void> mesh_export_write_validation_sidecar(detail::KernelState& state, RepresentationConversionService& convert,
                                                  MeshId mesh_id, std::string_view main_path,
                                                  const ExportOptions& options);
Result<void> merge_mesh_export_sidecar_summary(detail::KernelState& state, const Result<void>& sidecar,
                                               std::string export_summary);
std::optional<std::string> parse_obj_text_to_mesh(std::string_view text, detail::MeshRecord& mesh);
std::string build_3mf_model_xml_from_mesh(const detail::MeshRecord& mesh);
bool extract_3mf_model_xml(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files,
                             std::string& out_xml);
std::optional<std::string> parse_3mf_model_xml_to_mesh(std::string_view xml, detail::MeshRecord& mesh);
int base64_decode_char(unsigned char c);
std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view in);
bool read_f32_le(const std::uint8_t*& p, const std::uint8_t* end, float& out);
bool stl_ascii_parse(std::string_view text, detail::MeshRecord& mesh);
bool stl_binary_parse(const std::vector<std::uint8_t>& data, detail::MeshRecord& mesh);
std::optional<std::string> parse_stl_bytes(const std::vector<std::uint8_t>& data, detail::MeshRecord& mesh);
bool scan_json_uint_after(const std::string& s, std::size_t from, std::string_view key, std::uint64_t& out);
std::optional<std::string> parse_gltf_embedded_minimal(std::string_view json, detail::MeshRecord& mesh);
void append_issues_from_import_diag(detail::KernelState* state, std::vector<Issue>& issues, std::vector<Warning>& warnings,
                                    DiagnosticId diagnostic_id,
                                    std::initializer_list<std::uint64_t> fallback_related_entities);
BodyId run_post_import_validation_pipeline(const std::shared_ptr<detail::KernelState>& state, BodyId body_id,
                                             const ImportOptions& options, const std::string& format_cn,
                                             std::vector<Issue>& issues, std::vector<Warning>& warnings);

}  // namespace io_internal
}  // namespace axiom
