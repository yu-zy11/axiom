#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "axiom/core/result.h"

namespace axiom {
namespace detail {
struct KernelState;
}

class IOService {
public:
  explicit IOService(std::shared_ptr<detail::KernelState> state);

  Result<BodyId> import_step(std::string_view path,
                             const ImportOptions &options);
  Result<void> export_step(BodyId body_id, std::string_view path,
                           const ExportOptions &options);
  Result<void> export_gltf(BodyId body_id, std::string_view path,
                           const ExportOptions &options);
  Result<void> export_stl(BodyId body_id, std::string_view path,
                          const ExportOptions &options);
  Result<BodyId> import_axmjson(std::string_view path,
                                const ImportOptions &options);
  Result<BodyId> import_stl(std::string_view path, const ImportOptions &options);
  Result<BodyId> import_gltf(std::string_view path, const ImportOptions &options);
  Result<BodyId> import_iges(std::string_view path, const ImportOptions &options);
  Result<BodyId> import_brep(std::string_view path, const ImportOptions &options);
  Result<BodyId> import_obj(std::string_view path, const ImportOptions &options);
  Result<BodyId> import_3mf(std::string_view path, const ImportOptions &options);
  Result<void> export_axmjson(BodyId body_id, std::string_view path,
                              const ExportOptions &options);
  Result<void> export_iges(BodyId body_id, std::string_view path,
                           const ExportOptions &options);
  Result<void> export_brep(BodyId body_id, std::string_view path,
                           const ExportOptions &options);
  Result<void> export_obj(BodyId body_id, std::string_view path,
                          const ExportOptions &options);
  Result<void> export_3mf(BodyId body_id, std::string_view path,
                          const ExportOptions &options);
  Result<bool> file_exists(std::string_view path) const;
  Result<bool> is_regular_file(std::string_view path) const;
  Result<std::uint64_t> file_size_bytes(std::string_view path) const;
  Result<bool> has_extension(std::string_view path, std::string_view ext) const;
  Result<std::string> detect_format(std::string_view path) const;
  Result<bool> is_step_path(std::string_view path) const;
  Result<bool> is_axmjson_path(std::string_view path) const;
  Result<bool> is_gltf_path(std::string_view path) const;
  Result<bool> is_stl_path(std::string_view path) const;
  Result<std::string> normalize_path(std::string_view path) const;
  Result<BodyId> import_step_default(std::string_view path);
  Result<BodyId> import_axmjson_default(std::string_view path);
  Result<BodyId> import_stl_default(std::string_view path);
  Result<BodyId> import_gltf_default(std::string_view path);
  Result<BodyId> import_iges_default(std::string_view path);
  Result<BodyId> import_brep_default(std::string_view path);
  Result<BodyId> import_obj_default(std::string_view path);
  Result<BodyId> import_3mf_default(std::string_view path);
  Result<void> export_step_default(BodyId body_id, std::string_view path);
  Result<void> export_axmjson_default(BodyId body_id, std::string_view path);
  Result<void> export_gltf_default(BodyId body_id, std::string_view path);
  Result<void> export_stl_default(BodyId body_id, std::string_view path);
  Result<void> export_iges_default(BodyId body_id, std::string_view path);
  Result<void> export_brep_default(BodyId body_id, std::string_view path);
  Result<void> export_obj_default(BodyId body_id, std::string_view path);
  Result<void> export_3mf_default(BodyId body_id, std::string_view path);
  Result<BodyId> import_auto(std::string_view path,
                             const ImportOptions &options);
  Result<void> export_auto(BodyId body_id, std::string_view path,
                           const ExportOptions &options);
  Result<std::vector<BodyId>>
  import_many_step(std::span<const std::string> paths,
                   const ImportOptions &options);
  Result<std::vector<BodyId>>
  import_many_axmjson(std::span<const std::string> paths,
                      const ImportOptions &options);
  Result<void> export_many_step(std::span<const BodyId> body_ids,
                                std::span<const std::string> paths,
                                const ExportOptions &options);
  Result<void> export_many_axmjson(std::span<const BodyId> body_ids,
                                   std::span<const std::string> paths,
                                   const ExportOptions &options);
  Result<std::vector<BodyId>>
  import_many_auto(std::span<const std::string> paths,
                   const ImportOptions &options);
  Result<void> export_many_auto(std::span<const BodyId> body_ids,
                                std::span<const std::string> paths,
                                const ExportOptions &options);
  Result<std::uint64_t> count_lines(std::string_view path) const;
  Result<std::string> read_text_preview(std::string_view path,
                                        std::uint64_t max_chars) const;
  Result<void> write_text_snapshot(std::string_view path,
                                   std::string_view content) const;
  Result<void> export_body_summary_txt(BodyId body_id,
                                       std::string_view path) const;
  Result<void> validate_import_path(std::string_view path) const;
  Result<void> validate_export_path(std::string_view path) const;
  Result<std::string> temp_path_for(std::string_view stem,
                                    std::string_view ext) const;
  Result<void> copy_file(std::string_view from, std::string_view to) const;
  Result<void> remove_file(std::string_view path) const;
  Result<void> ensure_parent_directory(std::string_view path) const;
  Result<std::pair<BodyId, std::uint64_t>>
  import_step_with_warnings_count(std::string_view path,
                                  const ImportOptions &options);
  Result<std::pair<BodyId, std::uint64_t>>
  import_axmjson_with_warnings_count(std::string_view path,
                                     const ImportOptions &options);
  Result<std::pair<BodyId, std::uint64_t>>
  import_auto_with_warnings_count(std::string_view path,
                                  const ImportOptions &options);
  Result<bool> export_step_checked(BodyId body_id, std::string_view path,
                                   const ExportOptions &options);
  Result<bool> export_axmjson_checked(BodyId body_id, std::string_view path,
                                      const ExportOptions &options);
  Result<bool> export_auto_checked(BodyId body_id, std::string_view path,
                                   const ExportOptions &options);
  Result<std::uint64_t>
  import_many_step_count(std::span<const std::string> paths,
                         const ImportOptions &options);
  Result<std::uint64_t>
  import_many_axmjson_count(std::span<const std::string> paths,
                            const ImportOptions &options);
  Result<std::uint64_t>
  import_many_auto_count(std::span<const std::string> paths,
                         const ImportOptions &options);
  Result<std::uint64_t>
  export_many_step_checked(std::span<const BodyId> body_ids,
                           std::span<const std::string> paths,
                           const ExportOptions &options);
  Result<std::uint64_t>
  export_many_axmjson_checked(std::span<const BodyId> body_ids,
                              std::span<const std::string> paths,
                              const ExportOptions &options);
  Result<std::uint64_t>
  export_many_auto_checked(std::span<const BodyId> body_ids,
                           std::span<const std::string> paths,
                           const ExportOptions &options);
  Result<std::vector<std::string>>
  scan_formats(std::span<const std::string> paths) const;
  Result<std::uint64_t>
  count_existing_files(std::span<const std::string> paths) const;
  Result<std::vector<std::string>>
  filter_existing_files(std::span<const std::string> paths) const;
  Result<std::vector<std::string>>
  filter_missing_files(std::span<const std::string> paths) const;
  Result<std::string>
  first_missing_file(std::span<const std::string> paths) const;
  Result<std::string>
  first_existing_file(std::span<const std::string> paths) const;
  Result<std::string> sanitize_export_stem(std::string_view stem) const;
  Result<std::string> compose_path(std::string_view dir, std::string_view name,
                                   std::string_view ext) const;
  Result<std::string> change_extension(std::string_view path,
                                       std::string_view ext) const;
  Result<std::string> basename(std::string_view path) const;
  Result<std::string> dirname(std::string_view path) const;
  Result<std::uint64_t> file_mtime_unix(std::string_view path) const;
  Result<void> touch_empty_file(std::string_view path) const;
  Result<void> append_text(std::string_view path,
                           std::string_view content) const;
  Result<std::string> read_all_text(std::string_view path) const;
  Result<bool> compare_file_text(std::string_view lhs,
                                 std::string_view rhs) const;
  Result<void> export_bodies_summary_txt(std::span<const BodyId> body_ids,
                                         std::string_view path) const;
  Result<BodyId> import_auto_from_candidates(std::span<const std::string> paths,
                                             const ImportOptions &options);
  Result<std::uint64_t>
  count_importable_paths(std::span<const std::string> paths) const;
  Result<std::uint64_t>
  count_exportable_paths(std::span<const std::string> paths) const;
  Result<std::vector<std::string>>
  filter_step_paths(std::span<const std::string> paths) const;
  Result<std::vector<std::string>>
  filter_axmjson_paths(std::span<const std::string> paths) const;
  Result<std::vector<std::string>>
  filter_unknown_format_paths(std::span<const std::string> paths) const;
  Result<std::vector<std::string>>
  normalize_paths(std::span<const std::string> paths) const;
  Result<std::vector<std::string>>
  compose_paths(std::string_view dir, std::span<const std::string> names,
                std::string_view ext) const;
  Result<std::vector<std::string>>
  change_extensions(std::span<const std::string> paths,
                    std::string_view ext) const;
  Result<std::vector<std::string>>
  basenames(std::span<const std::string> paths) const;
  Result<std::vector<std::string>>
  dirnames(std::span<const std::string> paths) const;
  Result<std::vector<std::uint64_t>>
  file_sizes(std::span<const std::string> paths) const;
  Result<std::vector<std::uint64_t>>
  file_mtimes(std::span<const std::string> paths) const;
  Result<void>
  ensure_parent_directories(std::span<const std::string> paths) const;
  Result<void> touch_empty_files(std::span<const std::string> paths) const;
  Result<void> remove_files(std::span<const std::string> paths) const;
  Result<void> append_text_many(std::span<const std::string> paths,
                                std::string_view content) const;
  Result<std::vector<std::string>>
  read_all_text_many(std::span<const std::string> paths) const;
  Result<std::vector<std::uint64_t>>
  count_lines_many(std::span<const std::string> paths) const;
  Result<std::vector<std::string>>
  read_text_preview_many(std::span<const std::string> paths,
                         std::uint64_t max_chars) const;
  Result<void>
  export_body_summaries_many(std::span<const BodyId> body_ids,
                             std::span<const std::string> paths) const;
  Result<std::uint64_t>
  compare_file_text_many_equal(std::span<const std::string> lhs,
                               std::span<const std::string> rhs) const;
  Result<std::vector<std::pair<std::string, std::string>>>
  detect_formats_with_paths(std::span<const std::string> paths) const;
  Result<std::string>
  first_importable_path(std::span<const std::string> paths) const;
  Result<std::string>
  first_exportable_path(std::span<const std::string> paths) const;
  Result<std::vector<BodyId>>
  import_existing_auto(std::span<const std::string> paths,
                       const ImportOptions &options);
  Result<void> export_auto_to_directory(std::span<const BodyId> body_ids,
                                        std::string_view directory,
                                        std::string_view ext,
                                        const ExportOptions &options);
  Result<std::uint64_t>
  import_existing_auto_count(std::span<const std::string> paths,
                             const ImportOptions &options);
  Result<std::uint64_t> export_auto_to_directory_count(
      std::span<const BodyId> body_ids, std::string_view directory,
      std::string_view ext, const ExportOptions &options);
  Result<std::vector<std::string>>
  summarize_files_txt(std::span<const std::string> paths) const;
  Result<void> export_files_summary_txt(std::span<const std::string> paths,
                                        std::string_view out_path) const;
  Result<std::string> canonical_or_normalized_path(std::string_view path) const;
  Result<std::string> relative_to(std::string_view path,
                                  std::string_view base) const;
  Result<std::string>
  common_parent_directory(std::span<const std::string> paths) const;
  Result<std::vector<std::string>>
  unique_paths(std::span<const std::string> paths) const;
  Result<std::vector<std::string>>
  sort_paths_lex(std::span<const std::string> paths) const;
  Result<std::vector<std::pair<std::string, std::uint64_t>>>
  count_by_format(std::span<const std::string> paths) const;
  Result<std::vector<std::string>>
  paths_of_format(std::span<const std::string> paths,
                  std::string_view format) const;
  Result<void> rename_file(std::string_view from, std::string_view to) const;
  Result<void> move_file(std::string_view from, std::string_view to) const;
  Result<void> ensure_directory(std::string_view directory) const;
  Result<bool> directory_exists(std::string_view directory) const;
  Result<std::vector<std::string>>
  list_files_in_directory(std::string_view directory) const;
  Result<std::vector<std::string>>
  list_files_recursive(std::string_view directory) const;
  Result<std::uint64_t> count_files_in_directory(std::string_view directory,
                                                 bool recursive) const;
  Result<void> write_lines(std::string_view path,
                           std::span<const std::string> lines) const;
  Result<std::vector<std::string>> read_lines(std::string_view path) const;
  Result<std::vector<std::string>>
  grep_lines_contains(std::string_view path, std::string_view token) const;
  Result<std::uint64_t> replace_in_file_text(std::string_view path,
                                             std::string_view from,
                                             std::string_view to) const;
  Result<void> prepend_text(std::string_view path,
                            std::string_view content) const;
  Result<void> truncate_file(std::string_view path) const;
  Result<std::string> file_stem(std::string_view path) const;
  Result<std::string> extension_of(std::string_view path) const;
  Result<std::string> with_stem(std::string_view path,
                                std::string_view new_stem) const;
  Result<std::string> append_suffix_before_ext(std::string_view path,
                                               std::string_view suffix) const;
  Result<std::vector<std::string>>
  generate_sequential_paths(std::string_view directory, std::string_view prefix,
                            std::string_view ext, std::uint64_t count) const;
  Result<std::string>
  first_writable_path(std::span<const std::string> paths) const;
  Result<std::uint64_t>
  export_auto_existing_only(std::span<const BodyId> body_ids,
                            std::span<const std::string> paths,
                            const ExportOptions &options);
  Result<std::vector<BodyId>>
  import_auto_existing_strict(std::span<const std::string> paths,
                              const ImportOptions &options);
  Result<std::vector<std::string>>
  summarize_format_histogram_txt(std::span<const std::string> paths) const;

private:
  std::shared_ptr<detail::KernelState> state_;
};

} // namespace axiom
