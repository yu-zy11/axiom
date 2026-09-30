#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "axiom/diag/error_codes.h"
#include "axiom/sdk/kernel.h"
#include "axiom/internal/core/kernel_state.h"
#include "axiom/internal/io/io_service_internal.h"

namespace {

bool has_issue_code(const axiom::DiagnosticReport& report, std::string_view code) {
    for (const auto& issue : report.issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

const axiom::Issue* find_issue(const axiom::DiagnosticReport& report, std::string_view code) {
    for (const auto& issue : report.issues) {
        if (issue.code == code) {
            return &issue;
        }
    }
    return nullptr;
}

bool check_mesh_export_failure_package(const std::filesystem::path& root) {
    using Exporter = axiom::Result<void> (axiom::IOService::*)(
        axiom::BodyId, std::string_view, const axiom::ExportOptions&);
    using Importer = axiom::Result<axiom::BodyId> (axiom::IOService::*)(
        std::string_view, const axiom::ImportOptions&);
    struct Format {
        const char* name;
        Exporter export_file;
        Importer import_file;
    };
    const Format formats[] = {
        {"obj", &axiom::IOService::export_obj, &axiom::IOService::import_obj},
        {"stl", &axiom::IOService::export_stl, &axiom::IOService::import_stl},
        {"gltf", &axiom::IOService::export_gltf, &axiom::IOService::import_gltf},
        {"3mf", &axiom::IOService::export_3mf, &axiom::IOService::import_3mf},
    };
    std::filesystem::create_directories(root);
    const auto sentinel_path = root / "sentinel";
    const auto diagnostic_path = root / "diagnostic.json";
    const std::string sentinel = "keep existing output\n";
    { std::ofstream out {sentinel_path}; out << sentinel; }
    const auto read_text = [](const std::filesystem::path& path) {
        std::ifstream in {path, std::ios::binary};
        return std::string {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    };

    for (const auto& format : formats) {
        auto state = std::make_shared<axiom::detail::KernelState>(axiom::KernelConfig {});
        axiom::IOService io {state};
        axiom::DiagnosticService diagnostics {state};
        axiom::RepresentationConversionService convert {state};
        axiom::SweepService sweeps {state};
        axiom::ProfileRef profile;
        profile.label = "mesh_export_failure_prism";
        profile.polygon_xyz = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
        const auto created = sweeps.extrude(profile, {0, 0, 1}, 3);
        if (created.status != axiom::StatusCode::Ok || !created.value) {
            std::cerr << format.name << " failed to create mesh export fixture\n";
            return false;
        }
        const auto body = *created.value;
        const auto prefix = std::string("io.export.") + format.name + ".";
        const auto output = root / (std::string("retry.") + format.name);
        const auto sidecar = root / "retry.mesh_report.json";
        axiom::ExportOptions options;

        const auto check_failure = [&](axiom::BodyId target, const std::filesystem::path& path,
                                       axiom::StatusCode status, std::string_view code,
                                       const std::string& stage) {
            const auto next_id = state->next_id;
            const auto body_cache = state->tessellation_cache;
            const auto face_cache = state->face_tessellation_cache;
            const auto stats = state->tessellation_cache_stats;
            const auto meshes = state->meshes;
            const auto counts = std::array {state->bodies.size(), state->shells.size(), state->faces.size(),
                state->loops.size(), state->coedges.size(), state->edges.size(), state->vertices.size(),
                state->curves.size(), state->surfaces.size(), state->curve_eval_cache.size(),
                state->surface_eval_cache.size()};
            const auto result = (io.*format.export_file)(target, path.string(), options);
            const auto report = diagnostics.get(result.diagnostic_id);
            const auto* issue = report.value ? find_issue(*report.value, code) : nullptr;
            if (result.status != status || result.diagnostic_id.value == 0 || issue == nullptr ||
                issue->severity != axiom::IssueSeverity::Error || issue->stage != stage ||
                issue->related_entities != std::vector<std::uint64_t> {target.value} ||
                issue->numeric_evidence.empty()) {
                std::cerr << format.name << " missing failure evidence for " << stage << '\n';
                return false;
            }
            const auto staged = diagnostics.find_by_issue_stage(stage, 1000);
            const auto prefixed = diagnostics.find_by_issue_stage_prefix("io.export.", 1000);
            const auto coded = diagnostics.find_by_issue_code_prefix(code, 1000);
            const auto related = diagnostics.find_by_related_entity(target.value, 1000);
            for (const auto* found : {&staged, &prefixed, &coded}) {
                if (!found->value || std::find(found->value->begin(), found->value->end(), result.diagnostic_id) ==
                                         found->value->end()) return false;
            }
            // Zero is a valid piece of invalid-input evidence, but not a queryable entity handle.
            if (target.value != 0 && (!related.value ||
                std::find(related.value->begin(), related.value->end(), result.diagnostic_id) == related.value->end())) {
                return false;
            }
            if (diagnostics.export_report_json(result.diagnostic_id, diagnostic_path.string()).status !=
                axiom::StatusCode::Ok) return false;
            const auto json = read_text(diagnostic_path);
            if (json.find("\"stage\":\"" + stage + "\"") == std::string::npos ||
                json.find(std::string(code)) == std::string::npos ||
                json.find("\"related_entities\":[" + std::to_string(target.value) + "]") == std::string::npos) {
                return false;
            }
            const auto& after = state->tessellation_cache_stats;
            const auto after_counts = std::array {state->bodies.size(), state->shells.size(), state->faces.size(),
                state->loops.size(), state->coedges.size(), state->edges.size(), state->vertices.size(),
                state->curves.size(), state->surfaces.size(), state->curve_eval_cache.size(),
                state->surface_eval_cache.size()};
            if (state->next_id != next_id || counts != after_counts || state->meshes.size() != meshes.size() ||
                state->tessellation_cache != body_cache || state->face_tessellation_cache != face_cache ||
                after.body_cache_hits != stats.body_cache_hits || after.body_cache_misses != stats.body_cache_misses ||
                after.body_cache_stale_evictions != stats.body_cache_stale_evictions ||
                after.face_cache_hits != stats.face_cache_hits || after.face_cache_misses != stats.face_cache_misses ||
                after.face_cache_stale_evictions != stats.face_cache_stale_evictions ||
                read_text(sentinel_path) != sentinel) {
                std::cerr << format.name << " export failure polluted model/cache/file at " << stage << '\n';
                return false;
            }
            for (const auto& [id, before] : meshes) {
                const auto it = state->meshes.find(id);
                if (it == state->meshes.end() || it->second.indices != before.indices ||
                    it->second.source_body != before.source_body || it->second.label != before.label ||
                    it->second.vertices.size() != before.vertices.size()) return false;
                for (std::size_t i = 0; i < before.vertices.size(); ++i) {
                    const auto& a = before.vertices[i];
                    const auto& b = it->second.vertices[i];
                    const auto same = [](double lhs, double rhs) {
                        return lhs == rhs || (std::isnan(lhs) && std::isnan(rhs));
                    };
                    if (!same(a.x, b.x) || !same(a.y, b.y) || !same(a.z, b.z)) return false;
                }
            }
            return true;
        };

        // Cover both policy switches, first without caches and then with body/face cache hits.
        for (const bool compatibility : {false, true}) {
            for (const bool report : {false, true}) {
                options.compatibility_mode = compatibility;
                options.write_mesh_validation_report = report;
                if (!check_failure({}, sentinel_path, axiom::StatusCode::InvalidInput,
                                   axiom::diag_codes::kIoExportFailure, prefix + "input") ||
                    !check_failure({999999}, sentinel_path, axiom::StatusCode::InvalidInput,
                                   axiom::diag_codes::kIoExportFailure, prefix + "input") ||
                    !check_failure(body, {}, axiom::StatusCode::InvalidInput,
                                   axiom::diag_codes::kIoExportFailure, prefix + "input") ||
                    !check_failure(body, sentinel_path / "child", axiom::StatusCode::OperationFailed,
                                   axiom::diag_codes::kIoExportFailure, prefix + "path") ||
                    !check_failure(body, root, axiom::StatusCode::OperationFailed,
                                   axiom::diag_codes::kIoExportFailure, prefix + "open")) return false;
                const auto bbox = state->bodies.at(body.value).bbox;
                state->bodies.at(body.value).bbox.is_valid = false;
                const bool conversion_rejected = check_failure(body, sentinel_path, axiom::StatusCode::DegenerateGeometry,
                    axiom::diag_codes::kValDegenerateGeometry, prefix + "convert");
                state->bodies.at(body.value).bbox = bbox;
                if (!conversion_rejected) return false;
#ifdef __linux__
                // Use a local symlink so a requested sidecar would be observable without writing in /dev.
                const auto full_path = root / (std::string("full.") + format.name);
                std::filesystem::remove(full_path);
                std::filesystem::create_symlink("/dev/full", full_path);
                if (!check_failure(body, full_path, axiom::StatusCode::OperationFailed,
                                   axiom::diag_codes::kIoExportFailure, prefix + "write") ||
                    std::filesystem::exists(root / "full.mesh_report.json")) return false;
                std::filesystem::remove(full_path);
#endif
                if (report) {
                    std::filesystem::create_directory(sidecar);
                    if (!check_failure(body, output, axiom::StatusCode::OperationFailed,
                                       axiom::diag_codes::kIoExportFailure, prefix + "sidecar")) return false;
                    axiom::Kernel reader;
                    const auto primary = (reader.io().*format.import_file)(output.string(), axiom::ImportOptions {});
                    if (primary.status != axiom::StatusCode::Ok || !primary.value) return false;
                    std::filesystem::remove(sidecar);
#ifdef __linux__
                    std::filesystem::create_symlink("/dev/full", sidecar);
                    if (!check_failure(body, output, axiom::StatusCode::OperationFailed,
                                       axiom::diag_codes::kIoExportFailure, prefix + "sidecar")) return false;
                    std::filesystem::remove(sidecar);
#endif
                }
                const auto success = (io.*format.export_file)(body, output.string(), options);
                if (success.status != axiom::StatusCode::Ok || std::filesystem::file_size(output) == 0 ||
                    state->tessellation_cache.empty() || state->face_tessellation_cache.empty()) return false;
                const auto success_report = diagnostics.get(success.diagnostic_id);
                if (!success_report.value ||
                    has_issue_code(*success_report.value, axiom::diag_codes::kIoExportMeshReportSidecar) != report ||
                    std::filesystem::exists(sidecar) != report) return false;
                // Read back with a separate kernel so import allocations cannot mask export isolation.
                axiom::Kernel reader;
                const auto imported = (reader.io().*format.import_file)(output.string(), axiom::ImportOptions {});
                if (imported.status != axiom::StatusCode::Ok || !imported.value) return false;
                std::filesystem::remove(output);
                std::filesystem::remove(sidecar);
            }
        }

        // A stale body cache must also be restored after conversion evicts it and reuses face meshes.
        const auto cached_id = state->tessellation_cache.begin()->second;
        const auto cached_mesh = state->meshes.at(cached_id.value);
        state->meshes.erase(cached_id.value);
        if (!check_failure(body, root, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoExportFailure, prefix + "open")) return false;
        state->meshes.emplace(cached_id.value, cached_mesh);

        // Inject damaged embedded mesh records: validation must precede opening the sentinel file.
        const axiom::MeshId mesh_id {state->allocate_id()};
        axiom::detail::MeshRecord valid;
        valid.vertices = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
        valid.indices = {0, 1, 2};
        state->meshes.emplace(mesh_id.value, valid);
        const auto mesh_body = convert.mesh_to_brep(mesh_id);
        if (!mesh_body.value) return false;
        valid = state->meshes.at(mesh_id.value);
        options.write_mesh_validation_report = true;
        for (int variant = 0; variant < 8; ++variant) {
            auto damaged = valid;
            switch (variant) {
                case 0: damaged.vertices.clear(); break;
                case 1: damaged.indices.clear(); break;
                case 2: damaged.indices = {0, 1}; break;
                case 3: damaged.indices = {0, 1, 3}; break;
                case 4: damaged.vertices[0].x = std::numeric_limits<double>::infinity(); break;
                case 5: damaged.vertices[0].y = std::numeric_limits<double>::quiet_NaN(); break;
                case 6: damaged.indices = {0, 0, 2}; break;
                case 7: damaged.vertices[0].x = 1e100; break;
            }
            if (variant == 7 && std::string(format.name) != "gltf") continue;
            state->meshes.at(mesh_id.value) = damaged;
            // Strict QA retains its published error code and stage for triangle/index failures.
            options.compatibility_mode = false;
            const bool qa_failure = variant == 0 || variant == 2 || variant == 3 || variant == 6;
            if (!check_failure(*mesh_body.value, sentinel_path,
                               qa_failure ? axiom::StatusCode::OperationFailed : axiom::StatusCode::InvalidInput,
                               qa_failure ? axiom::diag_codes::kIoExportMeshStrictQaFailed
                                          : axiom::diag_codes::kIoExportFailure,
                               qa_failure ? "io.export.mesh_strict_qa" : prefix + "mesh")) return false;
            options.compatibility_mode = true;
            if (variant != 6) {
                if (!check_failure(*mesh_body.value, sentinel_path, axiom::StatusCode::InvalidInput,
                                   axiom::diag_codes::kIoExportFailure, prefix + "mesh")) return false;
            } else {
                // Compatibility still permits degenerate triangles, as documented.
                if ((io.*format.export_file)(*mesh_body.value, output.string(), options).status !=
                    axiom::StatusCode::Ok) return false;
                std::filesystem::remove(output);
                std::filesystem::remove(sidecar);
            }
        }
        state->meshes.at(mesh_id.value) = valid;
        options.compatibility_mode = false;
        if ((io.*format.export_file)(*mesh_body.value, output.string(), options).status != axiom::StatusCode::Ok) {
            return false;
        }
        std::filesystem::remove(output);
        std::filesystem::remove(sidecar);
    }
    std::filesystem::remove_all(root);
    return true;
}

bool check_exact_brep_import_failure_package(const std::filesystem::path& root) {
    using Importer = axiom::Result<axiom::BodyId> (axiom::IOService::*)(
        std::string_view, const axiom::ImportOptions&);
    struct FormatCase {
        const char* name;
        Importer import_file;
        std::string valid;
        std::string malformed;
        std::string wrong_kind_or_format;
        std::string reversed_bounds;
        std::string zero_axis;
    };
    const auto json_payload = [](std::string_view format, std::string_view body_kind,
                                 std::string_view axis_z, std::string_view min_x,
                                 std::string_view max_x) {
        return std::string("{\n  \"format\": \"") + std::string(format) +
               "\",\n  \"label\": \"exact interchange\",\n  \"body_kind\": \"" +
               std::string(body_kind) +
               "\",\n  \"origin_x\": 0,\n  \"origin_y\": 0,\n  \"origin_z\": 0,\n"
               "  \"axis_x\": 0,\n  \"axis_y\": 0,\n  \"axis_z\": " + std::string(axis_z) +
               ",\n  \"param_a\": 2,\n  \"param_b\": 3,\n  \"param_c\": 4,\n"
               "  \"bbox_min_x\": " + std::string(min_x) +
               ",\n  \"bbox_min_y\": 0,\n  \"bbox_min_z\": 0,\n"
               "  \"bbox_max_x\": " + std::string(max_x) +
               ",\n  \"bbox_max_y\": 3,\n  \"bbox_max_z\": 4\n}\n";
    };
    const auto iges_payload = [](std::string_view body_kind, std::string_view axis,
                                 std::string_view bounds) {
        return std::string("START\n1H,,1HAXIOM,AxiomKernel IGES metadata interchange (subset)\n") +
               "AXIOM_IGES_ENTITY 186_SUBSET\nAXIOM_LABEL exact interchange\nAXIOM_BODY_KIND " +
               std::string(body_kind) + "\nAXIOM_ORIGIN 0 0 0\nAXIOM_AXIS " + std::string(axis) +
               "\nAXIOM_PARAMS 2 3 4\nAXIOM_BBOX " + std::string(bounds) +
               "\nS 1\nTERMINATE\n";
    };

    const std::string axm_valid = json_payload("AXMJSON", "Box", "1", "0", "2");
    const std::string brep_valid =
        "# AXIOM_BREP_INTERCHANGE v1\n" + json_payload("AXIOM_BREP", "Box", "1", "0", "2");
    const FormatCase formats[] = {
        {"axmjson", &axiom::IOService::import_axmjson,
         axm_valid,
         "{\"format\":\"AXMJSON\",\"label\":\"truncated\"}",
         json_payload("WRONG", "Box", "1", "0", "2"),
         json_payload("AXMJSON", "Box", "1", "5", "2"),
         json_payload("AXMJSON", "Box", "0", "0", "2")},
        {"iges", &axiom::IOService::import_iges,
         iges_payload("Box", "0 0 1", "0 0 0 2 3 4"),
         iges_payload("Box", "0 0 1", "bad bounds"),
         iges_payload("FutureBody", "0 0 1", "0 0 0 2 3 4"),
         iges_payload("Box", "0 0 1", "5 0 0 2 3 4"),
         iges_payload("Box", "0 0 0", "0 0 0 2 3 4")},
        {"brep", &axiom::IOService::import_brep,
         brep_valid,
         "# AXIOM_BREP_INTERCHANGE v1\n{\"format\":\"AXIOM_BREP\"}",
         "# AXIOM_BREP_INTERCHANGE v1\n" + json_payload("WRONG", "Box", "1", "0", "2"),
         "# AXIOM_BREP_INTERCHANGE v1\n" + json_payload("AXIOM_BREP", "Box", "1", "5", "2"),
         "# AXIOM_BREP_INTERCHANGE v1\n" + json_payload("AXIOM_BREP", "Box", "0", "0", "2")},
    };

    std::filesystem::create_directories(root);
    for (const auto& format : formats) {
        auto state = std::make_shared<axiom::detail::KernelState>(axiom::KernelConfig {});
        axiom::IOService io {state};
        axiom::DiagnosticService diagnostics {state};
        axiom::ImportOptions options;
        options.run_validation = false;
        const auto missing = root / (std::string("missing.") + format.name);
        const auto malformed = root / (std::string("malformed.") + format.name);
        const auto wrong = root / (std::string("wrong.") + format.name);
        const auto reversed = root / (std::string("reversed.") + format.name);
        const auto zero_axis = root / (std::string("zero_axis.") + format.name);
        const auto oversized = root / (std::string("oversized.") + format.name);
        const auto valid = root / (std::string("valid.") + format.name);
        const auto json = root / (std::string("failure.") + format.name + ".json");
        const auto write = [](const std::filesystem::path& path, const std::string& text) {
            std::ofstream out {path, std::ios::binary};
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
        };
        write(malformed, format.malformed);
        write(wrong, format.wrong_kind_or_format);
        write(reversed, format.reversed_bounds);
        write(zero_axis, format.zero_axis);
        write(valid, format.valid);
        {
            std::ofstream out {oversized, std::ios::binary};
            out.seekp(static_cast<std::streamoff>(64) * 1024 * 1024);
            out.put('\0');
        }

        const auto next_id_before = state->next_id;
        const auto body_count_before = state->bodies.size();
        const auto mesh_count_before = state->meshes.size();
        const auto empty_result = (io.*format.import_file)("", options);
        const auto missing_result = (io.*format.import_file)(missing.string(), options);
        const auto directory_result = (io.*format.import_file)(root.string(), options);
        const auto malformed_result = (io.*format.import_file)(malformed.string(), options);
        const auto wrong_result = (io.*format.import_file)(wrong.string(), options);
        const auto reversed_result = (io.*format.import_file)(reversed.string(), options);
        const auto zero_axis_result = (io.*format.import_file)(zero_axis.string(), options);
        const auto oversized_result = (io.*format.import_file)(oversized.string(), options);
        const auto prefix = std::string("io.import.") + format.name + ".";

        const auto check_failure = [&](const axiom::Result<axiom::BodyId>& result,
                                       axiom::StatusCode status, std::string_view code,
                                       const std::string& stage) {
            const auto report = diagnostics.get(result.diagnostic_id);
            const auto* issue = report.value ? find_issue(*report.value, code) : nullptr;
            if (result.status != status || result.value || result.diagnostic_id.value == 0 ||
                issue == nullptr || issue->severity != axiom::IssueSeverity::Error ||
                issue->stage != stage ||
                issue->related_entities != std::vector<std::uint64_t> {0} ||
                issue->numeric_evidence.empty()) return false;
            const auto exact = diagnostics.find_by_issue_stage(stage, 20);
            const auto by_prefix = diagnostics.find_by_issue_stage_prefix(prefix, 20);
            const auto by_code = diagnostics.find_by_issue_code(code, 20);
            return exact.value && std::find(exact.value->begin(), exact.value->end(), result.diagnostic_id) != exact.value->end() &&
                   by_prefix.value && std::find(by_prefix.value->begin(), by_prefix.value->end(), result.diagnostic_id) != by_prefix.value->end() &&
                   by_code.value && std::find(by_code.value->begin(), by_code.value->end(), result.diagnostic_id) != by_code.value->end();
        };
        if (!check_failure(empty_result, axiom::StatusCode::InvalidInput,
                           axiom::diag_codes::kIoImportFailure, prefix + "input") ||
            !check_failure(missing_result, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, prefix + "path") ||
            !check_failure(directory_result, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, prefix + "open") ||
            !check_failure(malformed_result, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoCorruptFile, prefix + "parse") ||
            !check_failure(wrong_result, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoCorruptFile, prefix + "parse") ||
            !check_failure(reversed_result, axiom::StatusCode::DegenerateGeometry,
                           axiom::diag_codes::kValDegenerateGeometry, prefix + "validation") ||
            !check_failure(zero_axis_result, axiom::StatusCode::DegenerateGeometry,
                           axiom::diag_codes::kValDegenerateGeometry, prefix + "validation") ||
            !check_failure(oversized_result, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, prefix + "read")) {
            std::cerr << format.name << " exact BREP import failure evidence is incomplete\n";
            return false;
        }

        if (diagnostics.export_report_json(reversed_result.diagnostic_id, json.string()).status !=
            axiom::StatusCode::Ok) return false;
        std::ifstream json_in {json, std::ios::binary};
        const std::string json_text {std::istreambuf_iterator<char>(json_in), std::istreambuf_iterator<char>()};
        const auto staged = diagnostics.find_by_issue_stage_prefix(prefix, 20);
        if (json_text.find("\"stage\":\"" + prefix + "validation\"") == std::string::npos ||
            json_text.find(std::string(axiom::diag_codes::kValDegenerateGeometry)) == std::string::npos ||
            !staged.value || staged.value->size() != 8 || state->next_id != next_id_before ||
            state->bodies.size() != body_count_before || state->meshes.size() != mesh_count_before ||
            std::filesystem::exists(missing)) {
            std::cerr << format.name << " exact BREP import failure lookup, JSON, or isolation failed\n";
            return false;
        }

        const auto retried = (io.*format.import_file)(valid.string(), options);
        if (retried.status != axiom::StatusCode::Ok || !retried.value ||
            retried.value->value != next_id_before || state->next_id != next_id_before + 1 ||
            state->bodies.size() != body_count_before + 1 || state->meshes.size() != mesh_count_before) {
            std::cerr << format.name << " exact BREP retry did not materialize exactly one body\n";
            return false;
        }
        const auto& imported = state->bodies.at(retried.value->value);
        if (imported.kind != axiom::detail::BodyKind::Box || imported.bbox.min.x != 0.0 ||
            imported.bbox.max.x != 2.0 || imported.bbox.max.y != 3.0 || imported.bbox.max.z != 4.0) {
            std::cerr << format.name << " exact BREP successful import lost record data\n";
            return false;
        }
    }
    std::filesystem::remove_all(root);
    return true;
}

bool check_io_failure_evidence_and_batch_rollback(
    const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    auto state = std::make_shared<axiom::detail::KernelState>(axiom::KernelConfig {});
    axiom::IOService io {state};
    axiom::DiagnosticService diagnostics {state};
    axiom::SweepService sweeps {state};

    axiom::ProfileRef profile;
    profile.label = "io_audit_source";
    profile.polygon_xyz = {{0, 0, 0}, {3, 0, 0}, {0, 2, 0}};
    const auto source = sweeps.extrude(profile, {0, 0, 1}, 4);
    if (source.status != axiom::StatusCode::Ok || !source.value) return false;

    axiom::ExportOptions export_options;
    const auto valid_json = root / "valid.axmjson";
    const auto valid_obj = root / "valid.obj";
    if (io.export_axmjson(*source.value, valid_json.string(), export_options).status !=
            axiom::StatusCode::Ok ||
        io.export_obj(*source.value, valid_obj.string(), export_options).status !=
            axiom::StatusCode::Ok) {
        return false;
    }

    const auto model_counts = [&]() {
        return std::array<std::size_t, 12> {
            state->curves.size(), state->pcurves.size(), state->surfaces.size(),
            state->vertices.size(), state->edges.size(), state->coedges.size(),
            state->loops.size(), state->faces.size(), state->shells.size(),
            state->bodies.size(), state->meshes.size(), state->intersections.size()};
    };
    std::vector<axiom::DiagnosticId> failures;
    const auto record_failure = [&](const auto& result) {
        if (result.status == axiom::StatusCode::Ok ||
            result.diagnostic_id.value == 0) {
            return false;
        }
        failures.push_back(result.diagnostic_id);
        return true;
    };

    axiom::ImportOptions import_options;
    import_options.run_validation = false;
    const auto missing_json = root / "missing.axmjson";
    const std::array<std::string, 2> late_json_failure {
        valid_json.string(), missing_json.string()};
    const auto before_json_counts = model_counts();
    const auto before_json_next_id = state->next_id;
    const auto before_json_body_cache = state->tessellation_cache;
    const auto before_json_face_cache = state->face_tessellation_cache;
    const auto rejected_json = io.import_many_axmjson(late_json_failure, import_options);
    if (!record_failure(rejected_json) || model_counts() != before_json_counts ||
        state->next_id != before_json_next_id ||
        state->tessellation_cache != before_json_body_cache ||
        state->face_tessellation_cache != before_json_face_cache) {
        std::cerr << "AXMJSON late batch failure was not model/cache atomic\n";
        return false;
    }
    const auto json_retry = io.import_axmjson(valid_json.string(), import_options);
    if (!json_retry.value || json_retry.value->value != before_json_next_id) {
        std::cerr << "AXMJSON in-place retry did not reuse rolled-back id\n";
        return false;
    }

    const auto missing_stl = root / "missing.stl";
    const std::array<std::string, 2> late_mesh_failure {
        valid_obj.string(), missing_stl.string()};
    const auto before_mesh_counts = model_counts();
    const auto before_mesh_next_id = state->next_id;
    const auto rejected_mesh = io.import_many_auto(late_mesh_failure, import_options);
    if (!record_failure(rejected_mesh) || model_counts() != before_mesh_counts ||
        state->next_id != before_mesh_next_id) {
        std::cerr << "mesh late batch failure was not Body/Mesh atomic\n";
        return false;
    }
    const auto mesh_retry = io.import_obj(valid_obj.string(), import_options);
    if (!mesh_retry.value || mesh_retry.value->value != before_mesh_next_id ||
        state->next_id != before_mesh_next_id + 2) {
        std::cerr << "mesh in-place retry did not reuse Body/Mesh ids\n";
        return false;
    }

    const auto degenerate_stl = root / "degenerate.stl";
    {
        std::ofstream out {degenerate_stl};
        out << "solid degenerate\n"
               "facet normal 0 0 1\nouter loop\n"
               "vertex 0 0 0\nvertex 0 0 0\nvertex 1 0 0\n"
               "endloop\nendfacet\nendsolid degenerate\n";
    }
    if (!record_failure(io.import_stl(degenerate_stl.string(), import_options)) ||
        !record_failure(io.import_auto((root / "unknown.xyz").string(), import_options)) ||
        !record_failure(io.export_step({999000001}, (root / "bad.step").string(), export_options)) ||
        !record_failure(io.export_axmjson({999000002}, (root / "bad.axmjson").string(), export_options)) ||
        !record_failure(io.export_iges({999000003}, (root / "bad.iges").string(), export_options)) ||
        !record_failure(io.export_brep({999000004}, (root / "bad.brep").string(), export_options)) ||
        !record_failure(io.export_auto(*source.value, (root / "bad.unknown").string(), export_options))) {
        return false;
    }
    const std::span<const std::string> no_paths;
    const std::span<const axiom::BodyId> no_bodies;
    if (!record_failure(io.import_many_auto(no_paths, import_options)) ||
        !record_failure(io.export_many_auto(no_bodies, no_paths, export_options))) {
        return false;
    }
    const std::array<std::string, 1> missing_candidates {
        (root / "missing_candidate.step").string()};
    const std::array<axiom::BodyId, 1> invalid_export_bodies {
        axiom::BodyId {999000005}};
    const std::array<axiom::BodyId, 1> source_body {*source.value};
    const std::array<std::string, 1> conditional_paths {
        (root / "conditional.step").string()};
    if (!record_failure(io.import_auto_from_candidates(
            missing_candidates, import_options)) ||
        !record_failure(io.import_auto_existing_strict(
            missing_candidates, import_options)) ||
        !record_failure(io.export_auto_to_directory(
            source_body, root.string(), "unsupported", export_options)) ||
        !record_failure(io.export_auto_existing_only(
            invalid_export_bodies, conditional_paths, export_options)) ||
        std::filesystem::exists(conditional_paths.front())) {
        std::cerr << "candidate/directory/conditional workflow failure was silent\n";
        return false;
    }

    axiom::DiagnosticEvidencePolicy policy;
    policy.issue_code_prefix = "AXM-";
    policy.stage_prefix = "io.";
    const auto first_before = diagnostics.get(failures.front());
    const auto audit = diagnostics.audit_evidence(failures, policy);
    if (!first_before.value || !audit.value || !audit.value->passed() ||
        audit.value->reports_inspected != failures.size() ||
        audit.value->matching_issues != audit.value->complete_issues ||
        audit.value->matching_issues < failures.size() || !audit.value->findings.empty()) {
        std::cerr << "IO failure evidence audit did not pass\n";
        return false;
    }
    const auto audit_json = root / "io_failure_audit.json";
    const auto failure_json = root / "io_failure_report.json";
    if (diagnostics.export_report_json(
            failures.front(), failure_json.string()).status != axiom::StatusCode::Ok) {
        return false;
    }
    std::ifstream failure_in {failure_json};
    const std::string failure_text {std::istreambuf_iterator<char>(failure_in),
                                    std::istreambuf_iterator<char>()};
    if (failure_text.find("\"stage\":\"io.batch_import\"") == std::string::npos ||
        failure_text.find("\"numeric_evidence\":[") == std::string::npos ||
        failure_text.find("\"name\":\"status_code\"") == std::string::npos ||
        failure_text.find("\"name\":\"failed_item_index\"") == std::string::npos) {
        return false;
    }
    if (diagnostics.export_evidence_audit_json(
            failures, policy, audit_json.string()).status != axiom::StatusCode::Ok) {
        return false;
    }
    std::ifstream audit_in {audit_json};
    const std::string audit_text {std::istreambuf_iterator<char>(audit_in),
                                  std::istreambuf_iterator<char>()};
    const auto first_after = diagnostics.get(failures.front());
    if (audit_text.find("\"passed\":true") == std::string::npos ||
        audit_text.find("\"issues_missing_numeric_evidence\":0") == std::string::npos ||
        !first_after.value ||
        first_after.value->issues.size() != first_before.value->issues.size()) {
        return false;
    }

    axiom::KernelConfig invalid_tolerance;
    invalid_tolerance.tolerance.linear = 0.0;
    auto post_state = std::make_shared<axiom::detail::KernelState>(invalid_tolerance);
    axiom::IOService post_io {post_state};
    axiom::DiagnosticService post_diagnostics {post_state};
    axiom::ImportOptions post_options;
    post_options.run_validation = true;
    post_options.auto_repair = true;
    const auto post_import = post_io.import_axmjson(valid_json.string(), post_options);
    if (post_import.status != axiom::StatusCode::Ok || !post_import.value) return false;
    const auto post_report = post_diagnostics.get(post_import.diagnostic_id);
    bool saw_validation = false;
    bool saw_repair = false;
    if (post_report.value) {
        for (const auto& issue : post_report.value->issues) {
            saw_validation = saw_validation || issue.stage == "io.post_import.validation";
            saw_repair = saw_repair || issue.stage == "io.post_import.repair";
        }
    }
    const std::array<axiom::DiagnosticId, 1> post_ids {post_import.diagnostic_id};
    const auto post_audit = post_diagnostics.audit_evidence(post_ids, policy);
    if (!saw_validation || !saw_repair || !post_audit.value ||
        !post_audit.value->passed() ||
        post_audit.value->matching_issues != post_audit.value->complete_issues) {
        std::cerr << "post-import validation/repair evidence is incomplete\n";
        return false;
    }

    std::filesystem::remove_all(root);
    return true;
}

}  // namespace

int main() {
    const auto evidence_root = std::filesystem::temp_directory_path() /
        ("axiom_io_failure_evidence_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!check_io_failure_evidence_and_batch_rollback(evidence_root)) {
        std::cerr << "IO failure evidence or batch rollback regression\n";
        std::filesystem::remove_all(evidence_root);
        return 1;
    }

    {
        auto state = std::make_shared<axiom::detail::KernelState>(axiom::KernelConfig {});
        axiom::RepresentationConversionService convert {state};
        axiom::DiagnosticService diagnostics {state};
        constexpr axiom::BodyId body_id {7101};
        const axiom::MeshId valid_id {7102};
        const axiom::MeshId out_of_range_id {7103};
        const axiom::MeshId degenerate_id {7104};

        axiom::detail::MeshRecord valid;
        valid.vertices = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
        valid.indices = {0, 1, 2};
        state->meshes.emplace(valid_id.value, valid);
        auto out_of_range = valid;
        out_of_range.indices = {0, 1, 3};
        state->meshes.emplace(out_of_range_id.value, std::move(out_of_range));
        auto degenerate = valid;
        degenerate.indices = {0, 0, 2};
        state->meshes.emplace(degenerate_id.value, std::move(degenerate));

        axiom::ExportOptions strict;
        strict.compatibility_mode = false;
        const auto mesh_count_before = state->meshes.size();
        const auto passed = axiom::io_internal::mesh_export_strict_gate(*state, convert, valid_id, strict, body_id);
        const auto bad_index = axiom::io_internal::mesh_export_strict_gate(
            *state, convert, out_of_range_id, strict, body_id);
        const auto bad_triangle = axiom::io_internal::mesh_export_strict_gate(
            *state, convert, degenerate_id, strict, body_id);
        if (passed.status != axiom::StatusCode::Ok || bad_index.status != axiom::StatusCode::OperationFailed ||
            bad_triangle.status != axiom::StatusCode::OperationFailed || state->meshes.size() != mesh_count_before) {
            std::cerr << "strict mesh QA success/failure contract or failure isolation is unexpected\n";
            return 1;
        }
        for (const auto result : {bad_index, bad_triangle}) {
            const auto report = diagnostics.get(result.diagnostic_id);
            const auto* issue = report.value ? find_issue(*report.value, axiom::diag_codes::kIoExportMeshStrictQaFailed)
                                             : nullptr;
            if (issue == nullptr || issue->severity != axiom::IssueSeverity::Error ||
                issue->stage != "io.export.mesh_strict_qa" || issue->related_entities.size() != 1 ||
                issue->related_entities.front() != body_id.value) {
                std::cerr << "strict mesh QA failure is missing stage or body evidence\n";
                return 1;
            }
        }
        const auto staged = diagnostics.find_by_issue_stage("io.export.mesh_strict_qa", 10);
        const auto json_path = std::filesystem::temp_directory_path() / "axiom_io_strict_mesh_qa_diag.json";
        const auto exported = diagnostics.export_report_json(bad_index.diagnostic_id, json_path.string());
        std::ifstream json_in {json_path, std::ios::binary};
        const std::string json {(std::istreambuf_iterator<char>(json_in)), std::istreambuf_iterator<char>()};
        std::filesystem::remove(json_path);
        if (!staged.value || staged.value->size() != 2 || exported.status != axiom::StatusCode::Ok ||
            json.find("\"stage\":\"io.export.mesh_strict_qa\"") == std::string::npos ||
            json.find("7101") == std::string::npos) {
            std::cerr << "strict mesh QA diagnostic lookup or JSON evidence is unexpected\n";
            return 1;
        }
    }

    axiom::Kernel kernel;

    auto body = kernel.primitives().box({0.0, 0.0, 0.0}, 10.0, 20.0, 30.0);
    if (body.status != axiom::StatusCode::Ok || !body.value.has_value()) {
        std::cerr << "failed to create body for io test\n";
        return 1;
    }

    const auto uniq = std::to_string(
        static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto tmp = std::filesystem::temp_directory_path();
    const auto out_path = tmp / ("axiom_io_workflow_test_" + uniq + ".step");
    const auto out_json_path = tmp / ("axiom_io_workflow_test_" + uniq + ".axmjson");
    const auto out_gltf_path = tmp / ("axiom_io_workflow_test_" + uniq + ".gltf");
    const auto out_stl_path = tmp / ("axiom_io_workflow_test_" + uniq + ".stl");

    if (!check_exact_brep_import_failure_package(tmp / ("axiom_exact_brep_import_" + uniq))) {
        std::cerr << "exact BREP import failure package regression failed\n";
        return 1;
    }
    if (!check_mesh_export_failure_package(tmp / ("axiom_mesh_export_" + uniq))) {
        std::cerr << "mesh export failure package regression failed\n";
        return 1;
    }

    axiom::ExportOptions export_options;

    const auto body_count_before_step_import_failures = kernel.body_count();
    const auto missing_import_path = tmp / ("axiom_io_missing_import_" + uniq + ".step");
    const auto check_step_import_failure = [&](const axiom::Result<axiom::BodyId>& result,
                                               axiom::StatusCode expected_status,
                                               std::string_view expected_stage) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        const auto* issue = report.value ? find_issue(*report.value, axiom::diag_codes::kIoImportFailure) : nullptr;
        return result.status == expected_status && !result.value.has_value() && issue != nullptr &&
               issue->severity == axiom::IssueSeverity::Error && issue->stage == expected_stage &&
               issue->related_entities == std::vector<std::uint64_t> {0} &&
               !issue->numeric_evidence.empty();
    };
    const auto empty_step_import = kernel.io().import_step("", axiom::ImportOptions {});
    const auto missing_step_import = kernel.io().import_step(missing_import_path.string(), axiom::ImportOptions {});
    const auto directory_step_import = kernel.io().import_step(tmp.string(), axiom::ImportOptions {});
    if (!check_step_import_failure(empty_step_import, axiom::StatusCode::InvalidInput, "io.import.step.input") ||
        !check_step_import_failure(missing_step_import, axiom::StatusCode::OperationFailed, "io.import.step.path") ||
        !check_step_import_failure(directory_step_import, axiom::StatusCode::OperationFailed, "io.import.step.open")) {
        std::cerr << "STEP import failure is missing stable stage evidence\n";
        return 1;
    }
    const auto import_failure_json = tmp / ("axiom_io_step_import_failure_" + uniq + ".json");
    const auto exported_import_failure = kernel.diagnostics().export_report_json(
        directory_step_import.diagnostic_id, import_failure_json.string());
    std::ifstream import_failure_json_in {import_failure_json, std::ios::binary};
    const std::string import_failure_json_text {
        (std::istreambuf_iterator<char>(import_failure_json_in)), std::istreambuf_iterator<char>()};
    std::filesystem::remove(import_failure_json);
    const auto staged_step_import_failures = kernel.diagnostics().find_by_issue_stage_prefix("io.import.step.", 10);
    const auto body_count_after_step_import_failures = kernel.body_count();
    if (exported_import_failure.status != axiom::StatusCode::Ok ||
        import_failure_json_text.find("\"stage\":\"io.import.step.open\"") == std::string::npos ||
        !staged_step_import_failures.value || staged_step_import_failures.value->size() != 3 ||
        !body_count_before_step_import_failures.value || !body_count_after_step_import_failures.value ||
        *body_count_before_step_import_failures.value != *body_count_after_step_import_failures.value ||
        std::filesystem::exists(missing_import_path)) {
        std::cerr << "STEP import failure lookup, JSON evidence, or model isolation is unexpected\n";
        return 1;
    }

    const auto body_count_before_obj_import_failures = kernel.body_count();
    const auto mesh_count_before_obj_import_failures = kernel.mesh_count();
    const auto missing_obj_path = tmp / ("axiom_io_missing_import_" + uniq + ".obj");
    const auto malformed_obj_path = tmp / ("axiom_io_malformed_import_" + uniq + ".obj");
    const auto degenerate_obj_path = tmp / ("axiom_io_degenerate_import_" + uniq + ".obj");
    const auto valid_obj_path = tmp / ("axiom_io_valid_import_" + uniq + ".obj");
    {
        std::ofstream malformed {malformed_obj_path};
        malformed << "v 0 0 0\nf 1 2 3\n";
        std::ofstream degenerate {degenerate_obj_path};
        degenerate << "v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n";
        std::ofstream valid {valid_obj_path};
        valid << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    const auto check_obj_import_failure = [&](const axiom::Result<axiom::BodyId>& result,
                                              axiom::StatusCode expected_status, std::string_view expected_code,
                                              std::string_view expected_stage) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        const auto* issue = report.value ? find_issue(*report.value, expected_code) : nullptr;
        return result.status == expected_status && !result.value.has_value() && issue != nullptr &&
               issue->severity == axiom::IssueSeverity::Error && issue->stage == expected_stage &&
               issue->related_entities == std::vector<std::uint64_t> {0} &&
               !issue->numeric_evidence.empty();
    };
    const auto empty_obj_import = kernel.io().import_obj("", axiom::ImportOptions {});
    const auto missing_obj_import = kernel.io().import_obj(missing_obj_path.string(), axiom::ImportOptions {});
    const auto directory_obj_import = kernel.io().import_obj(tmp.string(), axiom::ImportOptions {});
    const auto malformed_obj_import = kernel.io().import_obj(malformed_obj_path.string(), axiom::ImportOptions {});
    const auto degenerate_obj_import = kernel.io().import_obj(degenerate_obj_path.string(), axiom::ImportOptions {});
    if (!check_obj_import_failure(empty_obj_import, axiom::StatusCode::InvalidInput,
                                  axiom::diag_codes::kIoImportFailure, "io.import.obj.input") ||
        !check_obj_import_failure(missing_obj_import, axiom::StatusCode::OperationFailed,
                                  axiom::diag_codes::kIoImportFailure, "io.import.obj.path") ||
        !check_obj_import_failure(directory_obj_import, axiom::StatusCode::OperationFailed,
                                  axiom::diag_codes::kIoImportFailure, "io.import.obj.open") ||
        !check_obj_import_failure(malformed_obj_import, axiom::StatusCode::OperationFailed,
                                  axiom::diag_codes::kIoImportFailure, "io.import.obj.parse") ||
        !check_obj_import_failure(degenerate_obj_import, axiom::StatusCode::DegenerateGeometry,
                                  axiom::diag_codes::kValDegenerateGeometry, "io.import.obj.validation")) {
        std::cerr << "OBJ import failure is missing stable root-cause stage evidence\n";
        return 1;
    }
    const auto obj_failure_json = tmp / ("axiom_io_obj_import_failure_" + uniq + ".json");
    const auto exported_obj_failure = kernel.diagnostics().export_report_json(
        degenerate_obj_import.diagnostic_id, obj_failure_json.string());
    std::ifstream obj_failure_json_in {obj_failure_json, std::ios::binary};
    const std::string obj_failure_json_text {
        (std::istreambuf_iterator<char>(obj_failure_json_in)), std::istreambuf_iterator<char>()};
    const auto staged_obj_import_failures = kernel.diagnostics().find_by_issue_stage_prefix("io.import.obj.", 10);
    const auto body_count_after_obj_import_failures = kernel.body_count();
    const auto mesh_count_after_obj_import_failures = kernel.mesh_count();
    const auto valid_obj_import = kernel.io().import_obj(valid_obj_path.string(), axiom::ImportOptions {});
    std::filesystem::remove(obj_failure_json);
    std::filesystem::remove(malformed_obj_path);
    std::filesystem::remove(degenerate_obj_path);
    std::filesystem::remove(valid_obj_path);
    if (exported_obj_failure.status != axiom::StatusCode::Ok ||
        obj_failure_json_text.find("\"stage\":\"io.import.obj.validation\"") == std::string::npos ||
        !staged_obj_import_failures.value || staged_obj_import_failures.value->size() != 5 ||
        !body_count_before_obj_import_failures.value || !body_count_after_obj_import_failures.value ||
        *body_count_before_obj_import_failures.value != *body_count_after_obj_import_failures.value ||
        !mesh_count_before_obj_import_failures.value || !mesh_count_after_obj_import_failures.value ||
        *mesh_count_before_obj_import_failures.value != *mesh_count_after_obj_import_failures.value ||
        valid_obj_import.status != axiom::StatusCode::Ok || !valid_obj_import.value.has_value() ||
        std::filesystem::exists(missing_obj_path)) {
        std::cerr << "OBJ import failure lookup, JSON evidence, isolation, or retry is unexpected\n";
        return 1;
    }

    const auto body_count_before_stl_failures = kernel.body_count();
    const auto mesh_count_before_stl_failures = kernel.mesh_count();
    const auto missing_stl_path = tmp / ("axiom_io_missing_import_" + uniq + ".stl");
    const auto malformed_stl_path = tmp / ("axiom_io_malformed_import_" + uniq + ".stl");
    const auto degenerate_stl_path = tmp / ("axiom_io_degenerate_import_" + uniq + ".stl");
    const auto valid_stl_path = tmp / ("axiom_io_valid_import_" + uniq + ".stl");
    {
        std::ofstream malformed {malformed_stl_path};
        malformed << "solid broken\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\n";
        std::ofstream degenerate {degenerate_stl_path};
        degenerate << "solid degenerate\nfacet normal 0 0 1\nouter loop\n"
                      "vertex 0 0 0\nvertex 1 0 0\nvertex 2 0 0\n"
                      "endloop\nendfacet\nendsolid degenerate\n";
        std::ofstream valid {valid_stl_path};
        valid << "solid valid\nfacet normal 0 0 1\nouter loop\n"
                 "vertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\n"
                 "endloop\nendfacet\nendsolid valid\n";
    }
    const auto check_stl_failure = [&](const axiom::Result<axiom::BodyId>& result,
                                       axiom::StatusCode status, std::string_view code,
                                       std::string_view stage) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        const auto* issue = report.value ? find_issue(*report.value, code) : nullptr;
        return result.status == status && !result.value.has_value() && issue != nullptr &&
               issue->severity == axiom::IssueSeverity::Error && issue->stage == stage &&
               issue->related_entities == std::vector<std::uint64_t> {0} &&
               !issue->numeric_evidence.empty();
    };
    const auto empty_stl = kernel.io().import_stl("", axiom::ImportOptions {});
    const auto missing_stl = kernel.io().import_stl(missing_stl_path.string(), axiom::ImportOptions {});
    const auto directory_stl = kernel.io().import_stl(tmp.string(), axiom::ImportOptions {});
    const auto malformed_stl = kernel.io().import_stl(malformed_stl_path.string(), axiom::ImportOptions {});
    const auto degenerate_stl = kernel.io().import_stl(degenerate_stl_path.string(), axiom::ImportOptions {});
    if (!check_stl_failure(empty_stl, axiom::StatusCode::InvalidInput,
                           axiom::diag_codes::kIoImportFailure, "io.import.stl.input") ||
        !check_stl_failure(missing_stl, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, "io.import.stl.path") ||
        !check_stl_failure(directory_stl, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, "io.import.stl.open") ||
        !check_stl_failure(malformed_stl, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, "io.import.stl.parse") ||
        !check_stl_failure(degenerate_stl, axiom::StatusCode::DegenerateGeometry,
                           axiom::diag_codes::kValDegenerateGeometry, "io.import.stl.validation")) {
        std::cerr << "STL import failure is missing stable root-cause stage evidence\n";
        return 1;
    }
    const auto stl_failure_json = tmp / ("axiom_io_stl_import_failure_" + uniq + ".json");
    const auto exported_stl_failure = kernel.diagnostics().export_report_json(
        degenerate_stl.diagnostic_id, stl_failure_json.string());
    std::ifstream stl_failure_json_in {stl_failure_json, std::ios::binary};
    const std::string stl_failure_json_text {
        (std::istreambuf_iterator<char>(stl_failure_json_in)), std::istreambuf_iterator<char>()};
    const auto staged_stl_failures = kernel.diagnostics().find_by_issue_stage_prefix("io.import.stl.", 10);
    const auto body_count_after_stl_failures = kernel.body_count();
    const auto mesh_count_after_stl_failures = kernel.mesh_count();
    const auto valid_stl = kernel.io().import_stl(valid_stl_path.string(), axiom::ImportOptions {});
    std::filesystem::remove(stl_failure_json);
    std::filesystem::remove(malformed_stl_path);
    std::filesystem::remove(degenerate_stl_path);
    std::filesystem::remove(valid_stl_path);
    if (exported_stl_failure.status != axiom::StatusCode::Ok ||
        stl_failure_json_text.find("\"stage\":\"io.import.stl.validation\"") == std::string::npos ||
        !staged_stl_failures.value || staged_stl_failures.value->size() != 5 ||
        !body_count_before_stl_failures.value || !body_count_after_stl_failures.value ||
        *body_count_before_stl_failures.value != *body_count_after_stl_failures.value ||
        !mesh_count_before_stl_failures.value || !mesh_count_after_stl_failures.value ||
        *mesh_count_before_stl_failures.value != *mesh_count_after_stl_failures.value ||
        valid_stl.status != axiom::StatusCode::Ok || !valid_stl.value.has_value() ||
        std::filesystem::exists(missing_stl_path)) {
        std::cerr << "STL import failure lookup, JSON evidence, isolation, or retry is unexpected\n";
        return 1;
    }

    const auto body_count_before_gltf_failures = kernel.body_count();
    const auto mesh_count_before_gltf_failures = kernel.mesh_count();
    const auto missing_gltf_path = tmp / ("axiom_io_missing_import_" + uniq + ".gltf");
    const auto malformed_gltf_path = tmp / ("axiom_io_malformed_import_" + uniq + ".gltf");
    const auto degenerate_gltf_path = tmp / ("axiom_io_degenerate_import_" + uniq + ".gltf");
    const auto out_of_range_gltf_path = tmp / ("axiom_io_out_of_range_import_" + uniq + ".gltf");
    const auto valid_gltf_path = tmp / ("axiom_io_valid_import_" + uniq + ".gltf");
    const auto write_triangle_gltf = [](const std::filesystem::path& path, std::string_view encoded_buffer) {
        std::ofstream out {path};
        out << "{\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," << encoded_buffer
            << "\"}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36,\"target\":34962},"
               "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":12,\"target\":34963}],"
               "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3},"
               "{\"bufferView\":1,\"componentType\":5125,\"count\":3}]}";
    };
    {
        std::ofstream malformed {malformed_gltf_path};
        malformed << "{\"asset\":{\"version\":\"2.0\"}}";
    }
    write_triangle_gltf(degenerate_gltf_path,
                        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAACAAAA");
    write_triangle_gltf(out_of_range_gltf_path,
                        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAEAAAADAAAA");
    write_triangle_gltf(valid_gltf_path,
                        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAEAAAACAAAA");
    const auto check_gltf_failure = [&](const axiom::Result<axiom::BodyId>& result,
                                        axiom::StatusCode status, std::string_view code,
                                        std::string_view stage) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        const auto* issue = report.value ? find_issue(*report.value, code) : nullptr;
        return result.status == status && !result.value.has_value() && issue != nullptr &&
               issue->severity == axiom::IssueSeverity::Error && issue->stage == stage &&
               issue->related_entities == std::vector<std::uint64_t> {0} &&
               !issue->numeric_evidence.empty();
    };
    const auto empty_gltf = kernel.io().import_gltf("", axiom::ImportOptions {});
    const auto missing_gltf = kernel.io().import_gltf(missing_gltf_path.string(), axiom::ImportOptions {});
    const auto directory_gltf = kernel.io().import_gltf(tmp.string(), axiom::ImportOptions {});
    const auto malformed_gltf = kernel.io().import_gltf(malformed_gltf_path.string(), axiom::ImportOptions {});
    const auto degenerate_gltf = kernel.io().import_gltf(degenerate_gltf_path.string(), axiom::ImportOptions {});
    const auto out_of_range_gltf = kernel.io().import_gltf(out_of_range_gltf_path.string(), axiom::ImportOptions {});
    if (!check_gltf_failure(empty_gltf, axiom::StatusCode::InvalidInput,
                            axiom::diag_codes::kIoImportFailure, "io.import.gltf.input") ||
        !check_gltf_failure(missing_gltf, axiom::StatusCode::OperationFailed,
                            axiom::diag_codes::kIoImportFailure, "io.import.gltf.path") ||
        !check_gltf_failure(directory_gltf, axiom::StatusCode::OperationFailed,
                            axiom::diag_codes::kIoImportFailure, "io.import.gltf.open") ||
        !check_gltf_failure(malformed_gltf, axiom::StatusCode::OperationFailed,
                            axiom::diag_codes::kIoImportFailure, "io.import.gltf.parse") ||
        !check_gltf_failure(out_of_range_gltf, axiom::StatusCode::InvalidInput,
                            axiom::diag_codes::kIoImportFailure, "io.import.gltf.validation") ||
        !check_gltf_failure(degenerate_gltf, axiom::StatusCode::DegenerateGeometry,
                            axiom::diag_codes::kValDegenerateGeometry, "io.import.gltf.validation")) {
        std::cerr << "glTF import failure is missing stable root-cause stage evidence\n";
        return 1;
    }
    const auto gltf_failure_json = tmp / ("axiom_io_gltf_import_failure_" + uniq + ".json");
    const auto exported_gltf_failure = kernel.diagnostics().export_report_json(
        degenerate_gltf.diagnostic_id, gltf_failure_json.string());
    std::ifstream gltf_failure_json_in {gltf_failure_json, std::ios::binary};
    const std::string gltf_failure_json_text {
        (std::istreambuf_iterator<char>(gltf_failure_json_in)), std::istreambuf_iterator<char>()};
    const auto staged_gltf_failures = kernel.diagnostics().find_by_issue_stage_prefix("io.import.gltf.", 10);
    const auto body_count_after_gltf_failures = kernel.body_count();
    const auto mesh_count_after_gltf_failures = kernel.mesh_count();
    const auto valid_gltf = kernel.io().import_gltf(valid_gltf_path.string(), axiom::ImportOptions {});
    std::filesystem::remove(gltf_failure_json);
    std::filesystem::remove(malformed_gltf_path);
    std::filesystem::remove(degenerate_gltf_path);
    std::filesystem::remove(out_of_range_gltf_path);
    std::filesystem::remove(valid_gltf_path);
    if (exported_gltf_failure.status != axiom::StatusCode::Ok ||
        gltf_failure_json_text.find("\"stage\":\"io.import.gltf.validation\"") == std::string::npos ||
        !staged_gltf_failures.value || staged_gltf_failures.value->size() != 6 ||
        !body_count_before_gltf_failures.value || !body_count_after_gltf_failures.value ||
        *body_count_before_gltf_failures.value != *body_count_after_gltf_failures.value ||
        !mesh_count_before_gltf_failures.value || !mesh_count_after_gltf_failures.value ||
        *mesh_count_before_gltf_failures.value != *mesh_count_after_gltf_failures.value ||
        valid_gltf.status != axiom::StatusCode::Ok || !valid_gltf.value.has_value() ||
        std::filesystem::exists(missing_gltf_path)) {
        std::cerr << "glTF import failure lookup, JSON evidence, isolation, or retry is unexpected\n";
        return 1;
    }

    const auto body_count_before_3mf_failures = kernel.body_count();
    const auto mesh_count_before_3mf_failures = kernel.mesh_count();
    const auto missing_3mf_path = tmp / ("axiom_io_missing_import_" + uniq + ".3mf");
    const auto malformed_3mf_path = tmp / ("axiom_io_malformed_import_" + uniq + ".3mf");
    const auto no_model_3mf_path = tmp / ("axiom_io_no_model_import_" + uniq + ".3mf");
    const auto invalid_number_3mf_path = tmp / ("axiom_io_invalid_number_import_" + uniq + ".3mf");
    const auto bad_index_3mf_path = tmp / ("axiom_io_bad_index_import_" + uniq + ".3mf");
    const auto overflow_index_3mf_path = tmp / ("axiom_io_overflow_index_import_" + uniq + ".3mf");
    const auto nonfinite_3mf_path = tmp / ("axiom_io_nonfinite_import_" + uniq + ".3mf");
    const auto degenerate_3mf_path = tmp / ("axiom_io_degenerate_import_" + uniq + ".3mf");
    const auto valid_3mf_path = tmp / ("axiom_io_valid_import_" + uniq + ".3mf");
    const auto write_3mf = [](const std::filesystem::path& path, const std::string& model_xml,
                              std::string model_path = "3D/3dmodel.model") {
        const auto archive = axiom::io_internal::build_zip_store_archive(
            {{std::move(model_path), std::vector<std::uint8_t>(model_xml.begin(), model_xml.end())}});
        std::ofstream out {path, std::ios::binary};
        out.write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
    };
    const auto triangle_3mf_xml = [](std::string_view first_x, std::string_view indices) {
        return std::string("<model><vertices><vertex x=\"") + std::string(first_x) +
               "\" y=\"0\" z=\"0\"/><vertex x=\"1\" y=\"0\" z=\"0\"/>"
               "<vertex x=\"0\" y=\"1\" z=\"0\"/></vertices><triangles>" +
               std::string(indices) + "</triangles></model>";
    };
    const auto triangle_3mf = [](std::string_view a, std::string_view b, std::string_view c) {
        return std::string("<triangle v1=\"") + std::string(a) + "\" v2=\"" + std::string(b) +
               "\" v3=\"" + std::string(c) + "\"/>";
    };
    {
        std::ofstream malformed {malformed_3mf_path, std::ios::binary};
        malformed << "not a ZIP archive";
    }
    write_3mf(no_model_3mf_path, "<model/>", "Other/file.xml");
    write_3mf(invalid_number_3mf_path, triangle_3mf_xml("bad", triangle_3mf("0", "1", "2")));
    write_3mf(bad_index_3mf_path, triangle_3mf_xml("0", triangle_3mf("0", "1", "9")));
    write_3mf(overflow_index_3mf_path, triangle_3mf_xml("0", triangle_3mf("4294967296", "1", "2")));
    write_3mf(nonfinite_3mf_path, triangle_3mf_xml("nan", triangle_3mf("0", "1", "2")));
    write_3mf(degenerate_3mf_path, triangle_3mf_xml("0", triangle_3mf("0", "0", "2")));
    write_3mf(valid_3mf_path, triangle_3mf_xml("0", triangle_3mf("0", "1", "2")));
    const auto check_3mf_failure = [&](const axiom::Result<axiom::BodyId>& result,
                                       axiom::StatusCode status, std::string_view code,
                                       std::string_view stage) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        const auto* issue = report.value ? find_issue(*report.value, code) : nullptr;
        return result.status == status && !result.value.has_value() && issue != nullptr &&
               issue->severity == axiom::IssueSeverity::Error && issue->stage == stage &&
               issue->related_entities == std::vector<std::uint64_t> {0} &&
               !issue->numeric_evidence.empty();
    };
    const auto empty_3mf = kernel.io().import_3mf("", axiom::ImportOptions {});
    const auto missing_3mf = kernel.io().import_3mf(missing_3mf_path.string(), axiom::ImportOptions {});
    const auto directory_3mf = kernel.io().import_3mf(tmp.string(), axiom::ImportOptions {});
    const auto malformed_3mf = kernel.io().import_3mf(malformed_3mf_path.string(), axiom::ImportOptions {});
    const auto no_model_3mf = kernel.io().import_3mf(no_model_3mf_path.string(), axiom::ImportOptions {});
    const auto invalid_number_3mf = kernel.io().import_3mf(invalid_number_3mf_path.string(), axiom::ImportOptions {});
    const auto bad_index_3mf = kernel.io().import_3mf(bad_index_3mf_path.string(), axiom::ImportOptions {});
    const auto overflow_index_3mf = kernel.io().import_3mf(overflow_index_3mf_path.string(), axiom::ImportOptions {});
    const auto nonfinite_3mf = kernel.io().import_3mf(nonfinite_3mf_path.string(), axiom::ImportOptions {});
    const auto degenerate_3mf = kernel.io().import_3mf(degenerate_3mf_path.string(), axiom::ImportOptions {});
    if (!check_3mf_failure(empty_3mf, axiom::StatusCode::InvalidInput,
                           axiom::diag_codes::kIoImportFailure, "io.import.3mf.input") ||
        !check_3mf_failure(missing_3mf, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, "io.import.3mf.path") ||
        !check_3mf_failure(directory_3mf, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, "io.import.3mf.open") ||
        !check_3mf_failure(malformed_3mf, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, "io.import.3mf.parse") ||
        !check_3mf_failure(no_model_3mf, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, "io.import.3mf.parse") ||
        !check_3mf_failure(invalid_number_3mf, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, "io.import.3mf.parse") ||
        !check_3mf_failure(bad_index_3mf, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, "io.import.3mf.parse") ||
        !check_3mf_failure(overflow_index_3mf, axiom::StatusCode::OperationFailed,
                           axiom::diag_codes::kIoImportFailure, "io.import.3mf.parse") ||
        !check_3mf_failure(nonfinite_3mf, axiom::StatusCode::InvalidInput,
                           axiom::diag_codes::kIoImportFailure, "io.import.3mf.validation") ||
        !check_3mf_failure(degenerate_3mf, axiom::StatusCode::DegenerateGeometry,
                           axiom::diag_codes::kValDegenerateGeometry, "io.import.3mf.validation")) {
        std::cerr << "3MF import failure is missing stable root-cause stage evidence\n";
        return 1;
    }
    const auto json_3mf_path = tmp / ("axiom_io_3mf_import_failure_" + uniq + ".json");
    const auto exported_3mf_failure = kernel.diagnostics().export_report_json(
        degenerate_3mf.diagnostic_id, json_3mf_path.string());
    std::ifstream json_3mf_in {json_3mf_path, std::ios::binary};
    const std::string json_3mf_text {
        (std::istreambuf_iterator<char>(json_3mf_in)), std::istreambuf_iterator<char>()};
    const auto staged_3mf_failures = kernel.diagnostics().find_by_issue_stage_prefix("io.import.3mf.", 20);
    const auto body_count_after_3mf_failures = kernel.body_count();
    const auto mesh_count_after_3mf_failures = kernel.mesh_count();
    const auto valid_3mf = kernel.io().import_3mf(valid_3mf_path.string(), axiom::ImportOptions {});
    for (const auto& path : {malformed_3mf_path, no_model_3mf_path, invalid_number_3mf_path,
                             bad_index_3mf_path, overflow_index_3mf_path, nonfinite_3mf_path,
                             degenerate_3mf_path, valid_3mf_path, json_3mf_path}) {
        std::filesystem::remove(path);
    }
    if (exported_3mf_failure.status != axiom::StatusCode::Ok ||
        json_3mf_text.find("\"stage\":\"io.import.3mf.validation\"") == std::string::npos ||
        !staged_3mf_failures.value || staged_3mf_failures.value->size() != 10 ||
        !body_count_before_3mf_failures.value || !body_count_after_3mf_failures.value ||
        *body_count_before_3mf_failures.value != *body_count_after_3mf_failures.value ||
        !mesh_count_before_3mf_failures.value || !mesh_count_after_3mf_failures.value ||
        *mesh_count_before_3mf_failures.value != *mesh_count_after_3mf_failures.value ||
        valid_3mf.status != axiom::StatusCode::Ok || !valid_3mf.value.has_value() ||
        std::filesystem::exists(missing_3mf_path)) {
        std::cerr << "3MF import lookup, JSON evidence, isolation, or retry is unexpected\n";
        return 1;
    }

    const auto body_count_before_step_failures = kernel.body_count();
    const auto check_step_failure = [&](const axiom::Result<void>& result, axiom::StatusCode expected_status,
                                        std::string_view expected_stage) {
        const auto report = kernel.diagnostics().get(result.diagnostic_id);
        const auto* issue = report.value ? find_issue(*report.value, axiom::diag_codes::kIoExportFailure) : nullptr;
        return result.status == expected_status && issue != nullptr && issue->stage == expected_stage &&
               issue->related_entities == std::vector<std::uint64_t> {body.value->value};
    };
    const auto empty_step_path = kernel.io().export_step(*body.value, "", export_options);
    const auto missing_step_body = kernel.io().export_step(axiom::BodyId {body.value->value + 1000000},
                                                           out_path.string(), export_options);
    if (!check_step_failure(empty_step_path, axiom::StatusCode::InvalidInput, "io.export.step.input") ||
        missing_step_body.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "STEP input failure is missing stable stage or body evidence\n";
        return 1;
    }
    const auto missing_report = kernel.diagnostics().get(missing_step_body.diagnostic_id);
    const auto* missing_issue = missing_report.value
        ? find_issue(*missing_report.value, axiom::diag_codes::kIoExportFailure) : nullptr;
    if (missing_issue == nullptr || missing_issue->stage != "io.export.step.input" ||
        missing_issue->related_entities != std::vector<std::uint64_t> {body.value->value + 1000000}) {
        std::cerr << "STEP invalid Body failure is missing rejected entity evidence\n";
        return 1;
    }
    const auto missing_parent_path = tmp / ("axiom_io_missing_parent_" + uniq) / "model.step";
    const auto missing_parent = kernel.io().export_step(*body.value, missing_parent_path.string(), export_options);
    const auto step_failure_json = tmp / ("axiom_io_step_failure_" + uniq + ".json");
    const auto exported_step_failure = kernel.diagnostics().export_report_json(
        missing_parent.diagnostic_id, step_failure_json.string());
    std::ifstream step_failure_json_in {step_failure_json, std::ios::binary};
    const std::string step_failure_json_text {(std::istreambuf_iterator<char>(step_failure_json_in)),
                                              std::istreambuf_iterator<char>()};
    std::filesystem::remove(step_failure_json);
    if (!check_step_failure(missing_parent, axiom::StatusCode::InvalidInput, "io.export.step.path") ||
        std::filesystem::exists(missing_parent_path) || exported_step_failure.status != axiom::StatusCode::Ok ||
        step_failure_json_text.find("\"stage\":\"io.export.step.path\"") == std::string::npos ||
        step_failure_json_text.find(std::to_string(body.value->value)) == std::string::npos) {
        std::cerr << "STEP path failure diagnostics or output isolation is unexpected\n";
        return 1;
    }
#if defined(__linux__)
    const auto write_failure = kernel.io().export_step(*body.value, "/dev/full", export_options);
    if (!check_step_failure(write_failure, axiom::StatusCode::OperationFailed, "io.export.step.write")) {
        std::cerr << "STEP device write failure was reported as success or lacks evidence\n";
        return 1;
    }
#endif
    const auto staged_step_failures = kernel.diagnostics().find_by_issue_stage_prefix("io.export.step.", 10);
    const auto body_count_after_step_failures = kernel.body_count();
    if (!staged_step_failures.value || staged_step_failures.value->size() < 3 ||
        !body_count_before_step_failures.value || !body_count_after_step_failures.value ||
        *body_count_after_step_failures.value != *body_count_before_step_failures.value ||
        kernel.io().export_step(*body.value, out_path.string(), export_options).status != axiom::StatusCode::Ok) {
        std::cerr << "STEP failure lookup, model isolation, or retry regression\n";
        return 1;
    }
    auto exported = kernel.io().export_step(*body.value, out_path.string(), export_options);
    if (exported.status != axiom::StatusCode::Ok) {
        std::cerr << "export failed\n";
        return 1;
    }

    axiom::ImportOptions import_options;
    auto imported = kernel.io().import_step(out_path.string(), import_options);
    if (imported.status != axiom::StatusCode::Ok || !imported.value.has_value()) {
        std::cerr << "import failed\n";
        return 1;
    }
    auto exported_json = kernel.io().export_axmjson(*body.value, out_json_path.string(), export_options);
    if (exported_json.status != axiom::StatusCode::Ok) {
        std::cerr << "axmjson export failed\n";
        std::filesystem::remove(out_path);
        return 1;
    }

    auto exported_gltf = kernel.io().export_gltf(*body.value, out_gltf_path.string(), export_options);
    if (exported_gltf.status != axiom::StatusCode::Ok) {
        std::cerr << "gltf export failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        return 1;
    }
    std::ifstream gltf_in {out_gltf_path};
    std::string gltf_text((std::istreambuf_iterator<char>(gltf_in)), std::istreambuf_iterator<char>());
    if (gltf_text.find("\"asset\"") == std::string::npos ||
        gltf_text.find("\"meshes\"") == std::string::npos ||
        gltf_text.find("\"buffers\"") == std::string::npos ||
        gltf_text.find("data:application/octet-stream;base64,") == std::string::npos) {
        std::cerr << "gltf export content is unexpected\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        return 1;
    }

    auto exported_stl = kernel.io().export_stl(*body.value, out_stl_path.string(), export_options);
    if (exported_stl.status != axiom::StatusCode::Ok) {
        std::cerr << "stl export failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        return 1;
    }
    std::ifstream stl_in {out_stl_path};
    std::string stl_text((std::istreambuf_iterator<char>(stl_in)), std::istreambuf_iterator<char>());
    if (stl_text.find("solid") == std::string::npos ||
        stl_text.find("facet normal") == std::string::npos ||
        stl_text.find("vertex") == std::string::npos ||
        stl_text.find("endsolid") == std::string::npos) {
        std::cerr << "stl export content is unexpected\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }

    auto imported_stl = kernel.io().import_auto(out_stl_path.string(), import_options);
    if (imported_stl.status != axiom::StatusCode::Ok || !imported_stl.value.has_value()) {
        std::cerr << "stl import failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }
    auto imported_stl_bbox = kernel.representation().bbox_of_body(*imported_stl.value);
    if (imported_stl_bbox.status != axiom::StatusCode::Ok || !imported_stl_bbox.value.has_value() ||
        imported_stl_bbox.value->max.x < 9.9 || imported_stl_bbox.value->max.y < 19.9 ||
        imported_stl_bbox.value->max.z < 29.9) {
        std::cerr << "stl import bbox was not plausible for box body\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }
    auto imported_stl_diag = kernel.diagnostics().get(imported_stl.diagnostic_id);
    if (imported_stl_diag.status != axiom::StatusCode::Ok || !imported_stl_diag.value.has_value() ||
        !has_issue_code(*imported_stl_diag.value, axiom::diag_codes::kIoPostImportValidation)) {
        std::cerr << "missing stl import post-validation diagnostic\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }

    auto imported_gltf = kernel.io().import_gltf(out_gltf_path.string(), import_options);
    if (imported_gltf.status != axiom::StatusCode::Ok || !imported_gltf.value.has_value()) {
        std::cerr << "gltf import failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }
    auto imported_gltf_bbox = kernel.representation().bbox_of_body(*imported_gltf.value);
    if (imported_gltf_bbox.status != axiom::StatusCode::Ok || !imported_gltf_bbox.value.has_value() ||
        imported_gltf_bbox.value->max.x < 9.9 || imported_gltf_bbox.value->max.y < 19.9 ||
        imported_gltf_bbox.value->max.z < 29.9) {
        std::cerr << "gltf import bbox was not plausible for box body\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }
    auto imported_gltf_diag = kernel.diagnostics().get(imported_gltf.diagnostic_id);
    if (imported_gltf_diag.status != axiom::StatusCode::Ok || !imported_gltf_diag.value.has_value() ||
        !has_issue_code(*imported_gltf_diag.value, axiom::diag_codes::kIoPostImportValidation)) {
        std::cerr << "missing gltf import post-validation diagnostic\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        return 1;
    }

    auto imported_json = kernel.io().import_axmjson(out_json_path.string(), import_options);
    if (imported_json.status != axiom::StatusCode::Ok || !imported_json.value.has_value()) {
        std::cerr << "axmjson import failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        return 1;
    }
    auto imported_json_bbox = kernel.representation().bbox_of_body(*imported_json.value);
    if (imported_json_bbox.status != axiom::StatusCode::Ok || !imported_json_bbox.value.has_value() ||
        imported_json_bbox.value->max.x < 9.9 || imported_json_bbox.value->max.y < 19.9 ||
        imported_json_bbox.value->max.z < 29.9) {
        std::cerr << "axmjson import bbox was not preserved\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        return 1;
    }
    if (!imported.warnings.empty()) {
        std::cerr << "unexpected import warning for valid step\n";
        return 1;
    }

    auto import_diag = kernel.diagnostics().get(imported.diagnostic_id);
    if (import_diag.status != axiom::StatusCode::Ok || !import_diag.value.has_value() ||
        !has_issue_code(*import_diag.value, axiom::diag_codes::kIoPostImportValidation)) {
        std::cerr << "missing automatic import validation diagnostic\n";
        return 1;
    }

    auto valid = kernel.validate().validate_all(*imported.value, axiom::ValidationMode::Standard);
    if (valid.status != axiom::StatusCode::Ok) {
        std::cerr << "imported body validation failed\n";
        return 1;
    }

    const auto dirty_path = std::filesystem::temp_directory_path() / "axiom_io_workflow_dirty.step";
    {
        std::ofstream out {dirty_path};
        out << "ISO-10303-21;\n";
        out << "HEADER;\n";
        out << "AXIOM_LABEL dirty_import\n";
        out << "AXIOM_BBOX 4 4 4 1 1 1\n";
        out << "ENDSEC;\n";
        out << "END-ISO-10303-21;\n";
    }

    axiom::ImportOptions repair_import_options;
    repair_import_options.auto_repair = true;
    repair_import_options.repair_mode = axiom::RepairMode::Aggressive;
    auto repaired_import = kernel.io().import_step(dirty_path.string(), repair_import_options);
    if (repaired_import.status != axiom::StatusCode::Ok || !repaired_import.value.has_value()) {
        std::cerr << "auto repair import failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }
    if (repaired_import.warnings.empty()) {
        std::cerr << "expected repaired import to retain validation or repair warnings\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }
    auto repaired_import_diag = kernel.diagnostics().get(repaired_import.diagnostic_id);

    auto repaired_valid = kernel.validate().validate_all(*repaired_import.value, axiom::ValidationMode::Standard);
    auto repaired_sources = kernel.topology().query().source_bodies_of_body(*repaired_import.value);
    if (repaired_valid.status != axiom::StatusCode::Ok ||
        repaired_import_diag.status != axiom::StatusCode::Ok || !repaired_import_diag.value.has_value() ||
        !has_issue_code(*repaired_import_diag.value, axiom::diag_codes::kHealRepairValidated) ||
        !has_issue_code(*repaired_import_diag.value, axiom::diag_codes::kHealRepairPipelineTrace) ||
        !has_issue_code(*repaired_import_diag.value, axiom::diag_codes::kHealRepairReplaySummary) ||
        !has_issue_code(*repaired_import_diag.value, axiom::diag_codes::kIoPostImportRepairMode) ||
        repaired_sources.status != axiom::StatusCode::Ok || !repaired_sources.value.has_value() ||
        repaired_sources.value->size() != 1) {
        std::cerr << "auto repaired import did not produce a valid derived body\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }
    const auto* repaired_issue = find_issue(*repaired_import_diag.value, axiom::diag_codes::kHealRepairValidated);
    if (repaired_issue == nullptr || repaired_issue->related_entities.size() != 2 ||
        repaired_issue->related_entities.front() == repaired_issue->related_entities.back()) {
        std::cerr << "auto repaired import diagnostic does not expose expected related entities\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }

    auto exists_step = kernel.io().file_exists(out_path.string());
    auto regular_step = kernel.io().is_regular_file(out_path.string());
    auto size_step = kernel.io().file_size_bytes(out_path.string());
    auto ext_step = kernel.io().has_extension(out_path.string(), ".step");
    auto fmt_step = kernel.io().detect_format(out_path.string());
    auto is_step = kernel.io().is_step_path(out_path.string());
    auto is_axmjson = kernel.io().is_axmjson_path(out_json_path.string());
    auto is_gltf = kernel.io().is_gltf_path(out_gltf_path.string());
    auto is_stl = kernel.io().is_stl_path(out_stl_path.string());
    auto normalized = kernel.io().normalize_path(out_path.string());
    auto lines = kernel.io().count_lines(out_path.string());
    auto preview = kernel.io().read_text_preview(out_path.string(), 32);
    if (exists_step.status != axiom::StatusCode::Ok || !exists_step.value.has_value() || !*exists_step.value ||
        regular_step.status != axiom::StatusCode::Ok || !regular_step.value.has_value() || !*regular_step.value ||
        size_step.status != axiom::StatusCode::Ok || !size_step.value.has_value() || *size_step.value == 0 ||
        ext_step.status != axiom::StatusCode::Ok || !ext_step.value.has_value() || !*ext_step.value ||
        fmt_step.status != axiom::StatusCode::Ok || !fmt_step.value.has_value() || *fmt_step.value != "step" ||
        is_step.status != axiom::StatusCode::Ok || !is_step.value.has_value() || !*is_step.value ||
        is_axmjson.status != axiom::StatusCode::Ok || !is_axmjson.value.has_value() || !*is_axmjson.value ||
        is_gltf.status != axiom::StatusCode::Ok || !is_gltf.value.has_value() || !*is_gltf.value ||
        is_stl.status != axiom::StatusCode::Ok || !is_stl.value.has_value() || !*is_stl.value ||
        normalized.status != axiom::StatusCode::Ok || !normalized.value.has_value() || normalized.value->empty() ||
        lines.status != axiom::StatusCode::Ok || !lines.value.has_value() || *lines.value == 0 ||
        preview.status != axiom::StatusCode::Ok || !preview.value.has_value() || preview.value->empty()) {
        std::cerr << "io path/file helper behavior is unexpected\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(out_gltf_path);
        std::filesystem::remove(out_stl_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }

    auto validate_import = kernel.io().validate_import_path(out_path.string());
    auto validate_export = kernel.io().validate_export_path(out_path.string());
    if (validate_import.status != axiom::StatusCode::Ok || validate_export.status != axiom::StatusCode::Ok) {
        std::cerr << "io path validation behavior is unexpected\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }
    const auto missing_validate = tmp / ("axiom_io_missing_validate_" + uniq + ".step");
    auto validate_missing = kernel.io().validate_import_path(missing_validate.string());
    if (validate_missing.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected InvalidInput for validate_import_path on missing file\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }
    auto validate_missing_diag = kernel.diagnostics().get(validate_missing.diagnostic_id);
    if (validate_missing_diag.status != axiom::StatusCode::Ok || !validate_missing_diag.value.has_value() ||
        !has_issue_code(*validate_missing_diag.value, axiom::diag_codes::kIoFileNotFound)) {
        std::cerr << "validate_import_path missing file should report kIoFileNotFound\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }

    auto temp_txt = kernel.io().temp_path_for("axiom_io_snapshot", ".txt");
    if (temp_txt.status != axiom::StatusCode::Ok || !temp_txt.value.has_value()) {
        std::cerr << "temp path generation failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }
    auto write_snapshot = kernel.io().write_text_snapshot(*temp_txt.value, "axiom-io-snapshot");
    auto export_summary = kernel.io().export_body_summary_txt(*body.value, *temp_txt.value);
    auto ensure_parent = kernel.io().ensure_parent_directory(*temp_txt.value);
    if (write_snapshot.status != axiom::StatusCode::Ok || export_summary.status != axiom::StatusCode::Ok ||
        ensure_parent.status != axiom::StatusCode::Ok) {
        std::cerr << "text snapshot/summary helpers failed\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }

    auto copied_step = kernel.io().temp_path_for("axiom_io_copy", ".step");
    if (copied_step.status != axiom::StatusCode::Ok || !copied_step.value.has_value()) {
        std::cerr << "copy temp path generation failed\n";
        return 1;
    }
    auto do_copy = kernel.io().copy_file(out_path.string(), *copied_step.value);
    auto rm_copy = kernel.io().remove_file(*copied_step.value);
    if (do_copy.status != axiom::StatusCode::Ok || rm_copy.status != axiom::StatusCode::Ok) {
        std::cerr << "copy/remove helper behavior is unexpected\n";
        std::filesystem::remove(out_path);
        std::filesystem::remove(out_json_path);
        std::filesystem::remove(dirty_path);
        return 1;
    }

    auto import_default_step = kernel.io().import_step_default(out_path.string());
    auto import_default_json = kernel.io().import_axmjson_default(out_json_path.string());
    auto export_default_step_path = kernel.io().temp_path_for("axiom_io_export_default_step", ".step");
    auto export_default_json_path = kernel.io().temp_path_for("axiom_io_export_default_json", ".axmjson");
    if (import_default_step.status != axiom::StatusCode::Ok || !import_default_step.value.has_value() ||
        import_default_json.status != axiom::StatusCode::Ok || !import_default_json.value.has_value() ||
        export_default_step_path.status != axiom::StatusCode::Ok || !export_default_step_path.value.has_value() ||
        export_default_json_path.status != axiom::StatusCode::Ok || !export_default_json_path.value.has_value() ||
        kernel.io().export_step_default(*body.value, *export_default_step_path.value).status != axiom::StatusCode::Ok ||
        kernel.io().export_axmjson_default(*body.value, *export_default_json_path.value).status != axiom::StatusCode::Ok) {
        std::cerr << "default io wrappers behavior is unexpected\n";
        return 1;
    }

    auto auto_import_step = kernel.io().import_auto(out_path.string(), import_options);
    auto auto_import_json = kernel.io().import_auto(out_json_path.string(), import_options);
    auto auto_export_step_path = kernel.io().temp_path_for("axiom_io_export_auto_step", ".step");
    auto auto_export_json_path = kernel.io().temp_path_for("axiom_io_export_auto_json", ".axmjson");
    if (auto_import_step.status != axiom::StatusCode::Ok || !auto_import_step.value.has_value() ||
        auto_import_json.status != axiom::StatusCode::Ok || !auto_import_json.value.has_value() ||
        auto_export_step_path.status != axiom::StatusCode::Ok || !auto_export_step_path.value.has_value() ||
        auto_export_json_path.status != axiom::StatusCode::Ok || !auto_export_json_path.value.has_value() ||
        kernel.io().export_auto(*body.value, *auto_export_step_path.value, export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_auto(*body.value, *auto_export_json_path.value, export_options).status != axiom::StatusCode::Ok) {
        std::cerr << "auto io wrappers behavior is unexpected\n";
        return 1;
    }

    const std::vector<std::string> import_step_paths {out_path.string()};
    const std::vector<std::string> import_json_paths {out_json_path.string()};
    auto many_step = kernel.io().import_many_step(import_step_paths, import_options);
    auto many_json = kernel.io().import_many_axmjson(import_json_paths, import_options);
    auto many_auto = kernel.io().import_many_auto(import_json_paths, import_options);
    if (many_step.status != axiom::StatusCode::Ok || !many_step.value.has_value() || many_step.value->size() != 1 ||
        many_json.status != axiom::StatusCode::Ok || !many_json.value.has_value() || many_json.value->size() != 1 ||
        many_auto.status != axiom::StatusCode::Ok || !many_auto.value.has_value() || many_auto.value->size() != 1) {
        std::cerr << "batch import wrappers behavior is unexpected\n";
        return 1;
    }

    auto batch_step_1 = kernel.io().temp_path_for("axiom_io_batch_1", ".step");
    auto batch_step_2 = kernel.io().temp_path_for("axiom_io_batch_2", ".step");
    auto batch_json_1 = kernel.io().temp_path_for("axiom_io_batch_1", ".axmjson");
    auto batch_json_2 = kernel.io().temp_path_for("axiom_io_batch_2", ".axmjson");
    if (!batch_step_1.value.has_value() || !batch_step_2.value.has_value() ||
        !batch_json_1.value.has_value() || !batch_json_2.value.has_value()) {
        std::cerr << "batch export path generation failed\n";
        return 1;
    }
    const std::array<axiom::BodyId, 2> batch_bodies {*body.value, *imported.value};
    auto batch_validate_all = kernel.validate().validate_all_many(batch_bodies, axiom::ValidationMode::Standard);
    auto batch_validate_tol = kernel.validate().validate_tolerance_many(batch_bodies, axiom::ValidationMode::Standard);
    if (batch_validate_all.status != axiom::StatusCode::Ok || batch_validate_tol.status != axiom::StatusCode::Ok) {
        std::cerr << "batch bodies should pass Standard validate_all_many / validate_tolerance_many after io workflow\n";
        return 1;
    }
    const std::array<std::string, 2> batch_step_paths {*batch_step_1.value, *batch_step_2.value};
    const std::array<std::string, 2> batch_json_paths {*batch_json_1.value, *batch_json_2.value};
    if (kernel.io().export_many_step(batch_bodies, batch_step_paths, export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_many_axmjson(batch_bodies, batch_json_paths, export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_many_auto(batch_bodies, batch_step_paths, export_options).status != axiom::StatusCode::Ok) {
        std::cerr << "batch export wrappers behavior is unexpected\n";
        return 1;
    }

    auto step_warn_count = kernel.io().import_step_with_warnings_count(out_path.string(), import_options);
    auto json_warn_count = kernel.io().import_axmjson_with_warnings_count(out_json_path.string(), import_options);
    auto auto_warn_count = kernel.io().import_auto_with_warnings_count(out_path.string(), import_options);
    auto step_checked = kernel.io().export_step_checked(*body.value, *batch_step_1.value, export_options);
    auto json_checked = kernel.io().export_axmjson_checked(*body.value, *batch_json_1.value, export_options);
    auto auto_checked = kernel.io().export_auto_checked(*body.value, *batch_json_2.value, export_options);
    auto import_step_count = kernel.io().import_many_step_count(import_step_paths, import_options);
    auto import_json_count = kernel.io().import_many_axmjson_count(import_json_paths, import_options);
    auto import_auto_count = kernel.io().import_many_auto_count(import_step_paths, import_options);
    auto export_step_count = kernel.io().export_many_step_checked(batch_bodies, batch_step_paths, export_options);
    auto export_json_count = kernel.io().export_many_axmjson_checked(batch_bodies, batch_json_paths, export_options);
    auto export_auto_count = kernel.io().export_many_auto_checked(batch_bodies, batch_json_paths, export_options);
    if (step_warn_count.status != axiom::StatusCode::Ok || !step_warn_count.value.has_value() ||
        json_warn_count.status != axiom::StatusCode::Ok || !json_warn_count.value.has_value() ||
        auto_warn_count.status != axiom::StatusCode::Ok || !auto_warn_count.value.has_value() ||
        step_checked.status != axiom::StatusCode::Ok || !step_checked.value.has_value() || !*step_checked.value ||
        json_checked.status != axiom::StatusCode::Ok || !json_checked.value.has_value() || !*json_checked.value ||
        auto_checked.status != axiom::StatusCode::Ok || !auto_checked.value.has_value() || !*auto_checked.value ||
        import_step_count.status != axiom::StatusCode::Ok || !import_step_count.value.has_value() || *import_step_count.value != 1 ||
        import_json_count.status != axiom::StatusCode::Ok || !import_json_count.value.has_value() || *import_json_count.value != 1 ||
        import_auto_count.status != axiom::StatusCode::Ok || !import_auto_count.value.has_value() || *import_auto_count.value != 1 ||
        export_step_count.status != axiom::StatusCode::Ok || !export_step_count.value.has_value() || *export_step_count.value != 2 ||
        export_json_count.status != axiom::StatusCode::Ok || !export_json_count.value.has_value() || *export_json_count.value != 2 ||
        export_auto_count.status != axiom::StatusCode::Ok || !export_auto_count.value.has_value() || *export_auto_count.value != 2) {
        std::cerr << "io checked/count wrappers behavior is unexpected\n";
        return 1;
    }

    const std::array<std::string, 3> candidates {"/tmp/non-existent-axiom.file", out_json_path.string(), out_path.string()};
    auto scanned_formats = kernel.io().scan_formats(candidates);
    auto existing_count = kernel.io().count_existing_files(candidates);
    auto existing_files = kernel.io().filter_existing_files(candidates);
    auto missing_files = kernel.io().filter_missing_files(candidates);
    auto first_missing = kernel.io().first_missing_file(candidates);
    auto first_existing = kernel.io().first_existing_file(candidates);
    if (scanned_formats.status != axiom::StatusCode::Ok || !scanned_formats.value.has_value() || scanned_formats.value->size() != 3 ||
        existing_count.status != axiom::StatusCode::Ok || !existing_count.value.has_value() || *existing_count.value != 2 ||
        existing_files.status != axiom::StatusCode::Ok || !existing_files.value.has_value() || existing_files.value->size() != 2 ||
        missing_files.status != axiom::StatusCode::Ok || !missing_files.value.has_value() || missing_files.value->size() != 1 ||
        first_missing.status != axiom::StatusCode::Ok || !first_missing.value.has_value() || first_missing.value->empty() ||
        first_existing.status != axiom::StatusCode::Ok || !first_existing.value.has_value() || first_existing.value->empty()) {
        std::cerr << "io candidate filtering behavior is unexpected\n";
        return 1;
    }

    auto sanitized = kernel.io().sanitize_export_stem("a x/i:o*m?");
    auto composed = kernel.io().compose_path(std::filesystem::temp_directory_path().string(), "axiom_compose", "txt");
    auto changed = kernel.io().change_extension(out_path.string(), "axmjson");
    auto base = kernel.io().basename(out_path.string());
    auto dir = kernel.io().dirname(out_path.string());
    auto mtime = kernel.io().file_mtime_unix(out_path.string());
    if (sanitized.status != axiom::StatusCode::Ok || !sanitized.value.has_value() ||
        composed.status != axiom::StatusCode::Ok || !composed.value.has_value() ||
        changed.status != axiom::StatusCode::Ok || !changed.value.has_value() ||
        base.status != axiom::StatusCode::Ok || !base.value.has_value() || base.value->empty() ||
        dir.status != axiom::StatusCode::Ok || !dir.value.has_value() || dir.value->empty() ||
        mtime.status != axiom::StatusCode::Ok || !mtime.value.has_value()) {
        std::cerr << "io path compose behavior is unexpected\n";
        return 1;
    }

    auto text_file = kernel.io().temp_path_for("axiom_io_text_ops", ".txt");
    if (text_file.status != axiom::StatusCode::Ok || !text_file.value.has_value()) {
        std::cerr << "text file path generation failed\n";
        return 1;
    }
    auto touch = kernel.io().touch_empty_file(*text_file.value);
    auto append = kernel.io().append_text(*text_file.value, "line-1\nline-2\n");
    auto all_text = kernel.io().read_all_text(*text_file.value);
    auto same_text = kernel.io().compare_file_text(*text_file.value, *text_file.value);
    auto export_many_summary = kernel.io().export_bodies_summary_txt(batch_bodies, *text_file.value);
    auto import_candidate = kernel.io().import_auto_from_candidates(candidates, import_options);
    if (touch.status != axiom::StatusCode::Ok || append.status != axiom::StatusCode::Ok ||
        all_text.status != axiom::StatusCode::Ok || !all_text.value.has_value() ||
        same_text.status != axiom::StatusCode::Ok || !same_text.value.has_value() || !*same_text.value ||
        export_many_summary.status != axiom::StatusCode::Ok ||
        import_candidate.status != axiom::StatusCode::Ok || !import_candidate.value.has_value()) {
        std::cerr << "io text and candidate helpers behavior is unexpected\n";
        return 1;
    }

    const std::array<std::string, 3> io_paths {out_path.string(), out_json_path.string(), "/tmp/axiom_missing_unknown.ext"};
    auto count_importable = kernel.io().count_importable_paths(io_paths);
    auto count_exportable = kernel.io().count_exportable_paths(io_paths);
    auto step_only = kernel.io().filter_step_paths(io_paths);
    auto json_only = kernel.io().filter_axmjson_paths(io_paths);
    auto unknown_only = kernel.io().filter_unknown_format_paths(io_paths);
    auto normalized_many = kernel.io().normalize_paths(io_paths);
    std::array<std::string, 2> names {"n1", "n2"};
    auto composed_many = kernel.io().compose_paths(std::filesystem::temp_directory_path().string(), names, "txt");
    auto changed_many = kernel.io().change_extensions(io_paths, "bak");
    auto basenames = kernel.io().basenames(io_paths);
    auto dirnames = kernel.io().dirnames(io_paths);
    auto sizes = kernel.io().file_sizes(io_paths);
    auto mtimes = kernel.io().file_mtimes(io_paths);
    auto first_importable = kernel.io().first_importable_path(io_paths);
    auto first_exportable = kernel.io().first_exportable_path(io_paths);
    auto detect_with_paths = kernel.io().detect_formats_with_paths(io_paths);
    if (count_importable.status != axiom::StatusCode::Ok || !count_importable.value.has_value() || *count_importable.value < 2 ||
        count_exportable.status != axiom::StatusCode::Ok || !count_exportable.value.has_value() || *count_exportable.value < 2 ||
        step_only.status != axiom::StatusCode::Ok || !step_only.value.has_value() || step_only.value->size() != 1 ||
        json_only.status != axiom::StatusCode::Ok || !json_only.value.has_value() || json_only.value->size() != 1 ||
        unknown_only.status != axiom::StatusCode::Ok || !unknown_only.value.has_value() || unknown_only.value->size() != 1 ||
        normalized_many.status != axiom::StatusCode::Ok || !normalized_many.value.has_value() || normalized_many.value->size() != 3 ||
        composed_many.status != axiom::StatusCode::Ok || !composed_many.value.has_value() || composed_many.value->size() != 2 ||
        changed_many.status != axiom::StatusCode::Ok || !changed_many.value.has_value() || changed_many.value->size() != 3 ||
        basenames.status != axiom::StatusCode::Ok || !basenames.value.has_value() || basenames.value->size() != 3 ||
        dirnames.status != axiom::StatusCode::Ok || !dirnames.value.has_value() || dirnames.value->size() != 3 ||
        sizes.status != axiom::StatusCode::Ok || !sizes.value.has_value() || sizes.value->size() != 3 ||
        mtimes.status != axiom::StatusCode::Ok || !mtimes.value.has_value() || mtimes.value->size() != 3 ||
        first_importable.status != axiom::StatusCode::Ok || !first_importable.value.has_value() || first_importable.value->empty() ||
        first_exportable.status != axiom::StatusCode::Ok || !first_exportable.value.has_value() || first_exportable.value->empty() ||
        detect_with_paths.status != axiom::StatusCode::Ok || !detect_with_paths.value.has_value() || detect_with_paths.value->size() != 3) {
        std::cerr << "extended io path batch behavior is unexpected\n";
        return 1;
    }

    auto tmp_batch_a = kernel.io().temp_path_for("axiom_batch_a", ".txt");
    auto tmp_batch_b = kernel.io().temp_path_for("axiom_batch_b", ".txt");
    std::array<std::string, 2> batch_text_paths {*tmp_batch_a.value, *tmp_batch_b.value};
    if (kernel.io().ensure_parent_directories(batch_text_paths).status != axiom::StatusCode::Ok ||
        kernel.io().touch_empty_files(batch_text_paths).status != axiom::StatusCode::Ok ||
        kernel.io().append_text_many(batch_text_paths, "x\n").status != axiom::StatusCode::Ok) {
        std::cerr << "extended io batch file create/append behavior is unexpected\n";
        return 1;
    }
    auto all_many = kernel.io().read_all_text_many(batch_text_paths);
    auto line_many = kernel.io().count_lines_many(batch_text_paths);
    auto preview_many = kernel.io().read_text_preview_many(batch_text_paths, 4);
    auto compare_many = kernel.io().compare_file_text_many_equal(batch_text_paths, batch_text_paths);
    auto files_summary_lines = kernel.io().summarize_files_txt(batch_text_paths);
    auto summary_out = kernel.io().temp_path_for("axiom_files_summary", ".txt");
    auto export_files_summary = kernel.io().export_files_summary_txt(batch_text_paths, *summary_out.value);
    if (all_many.status != axiom::StatusCode::Ok || !all_many.value.has_value() || all_many.value->size() != 2 ||
        line_many.status != axiom::StatusCode::Ok || !line_many.value.has_value() || line_many.value->size() != 2 ||
        preview_many.status != axiom::StatusCode::Ok || !preview_many.value.has_value() || preview_many.value->size() != 2 ||
        compare_many.status != axiom::StatusCode::Ok || !compare_many.value.has_value() || *compare_many.value != 2 ||
        files_summary_lines.status != axiom::StatusCode::Ok || !files_summary_lines.value.has_value() || files_summary_lines.value->size() != 2 ||
        export_files_summary.status != axiom::StatusCode::Ok) {
        std::cerr << "extended io batch read/summary behavior is unexpected\n";
        return 1;
    }
    {
        const auto missing_read = tmp / ("axiom_missing_read_" + uniq + ".txt");
        const std::vector<std::string> bad_read {*tmp_batch_a.value, missing_read.string()};
        auto read_fail = kernel.io().read_all_text_many(bad_read);
        if (read_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected read_all_text_many failure when a file is missing\n";
            return 1;
        }
        auto read_diag = kernel.diagnostics().get(read_fail.diagnostic_id);
        const auto* read_ctx = read_diag.status == axiom::StatusCode::Ok && read_diag.value.has_value()
                                  ? find_issue(*read_diag.value, axiom::diag_codes::kIoBatchReadItemContext)
                                  : nullptr;
        if (read_ctx == nullptr || read_ctx->stage != "io.batch_read" ||
            !has_issue_code(*read_diag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "read_all_text_many batch failure missing batch context or merged read root cause\n";
            return 1;
        }
    }
    {
        const auto missing_cmp = tmp / ("axiom_missing_cmp_" + uniq + ".txt");
        const std::vector<std::string> lhs_cmp {*tmp_batch_a.value, *tmp_batch_b.value};
        const std::vector<std::string> rhs_cmp {*tmp_batch_a.value, missing_cmp.string()};
        auto cmp_fail = kernel.io().compare_file_text_many_equal(lhs_cmp, rhs_cmp);
        if (cmp_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected compare_file_text_many_equal failure when rhs file is missing\n";
            return 1;
        }
        auto cmp_diag = kernel.diagnostics().get(cmp_fail.diagnostic_id);
        const auto* cmp_ctx = cmp_diag.status == axiom::StatusCode::Ok && cmp_diag.value.has_value()
                                  ? find_issue(*cmp_diag.value, axiom::diag_codes::kIoBatchCompareItemContext)
                                  : nullptr;
        if (cmp_ctx == nullptr || cmp_ctx->stage != "io.batch_compare" ||
            !has_issue_code(*cmp_diag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "compare_file_text_many_equal batch failure missing batch compare context or root cause\n";
            return 1;
        }
    }
    {
        const std::vector<std::string> bad_append {*tmp_batch_a.value, std::string{}};
        auto append_fail = kernel.io().append_text_many(bad_append, "z\n");
        if (append_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected append_text_many failure when a path is empty\n";
            return 1;
        }
        auto append_diag = kernel.diagnostics().get(append_fail.diagnostic_id);
        const auto* append_ctx = append_diag.status == axiom::StatusCode::Ok && append_diag.value.has_value()
                                     ? find_issue(*append_diag.value, axiom::diag_codes::kIoBatchPathOpItemContext)
                                     : nullptr;
        if (append_ctx == nullptr || append_ctx->stage != "io.batch_path_op" ||
            !has_issue_code(*append_diag.value, axiom::diag_codes::kIoExportFailure)) {
            std::cerr << "append_text_many batch failure missing batch path_op context or merged export root cause\n";
            return 1;
        }
    }

    auto import_existing = kernel.io().import_existing_auto(io_paths, import_options);
    auto import_existing_count = kernel.io().import_existing_auto_count(io_paths, import_options);
    auto export_dir = std::filesystem::temp_directory_path() / "axiom_io_export_dir";
    std::filesystem::create_directories(export_dir);
    auto export_to_dir = kernel.io().export_auto_to_directory(batch_bodies, export_dir.string(), "step", export_options);
    auto export_to_dir_count = kernel.io().export_auto_to_directory_count(batch_bodies, export_dir.string(), "axmjson", export_options);
    auto body_summary_many_a = kernel.io().temp_path_for("axiom_body_summary_a", ".txt");
    auto body_summary_many_b = kernel.io().temp_path_for("axiom_body_summary_b", ".txt");
    std::array<std::string, 2> body_summary_paths {*body_summary_many_a.value, *body_summary_many_b.value};
    auto export_body_summaries_many = kernel.io().export_body_summaries_many(batch_bodies, body_summary_paths);
    if (import_existing.status != axiom::StatusCode::Ok || !import_existing.value.has_value() ||
        import_existing_count.status != axiom::StatusCode::Ok || !import_existing_count.value.has_value() ||
        export_to_dir.status != axiom::StatusCode::Ok ||
        export_to_dir_count.status != axiom::StatusCode::Ok || !export_to_dir_count.value.has_value() || *export_to_dir_count.value != 2 ||
        export_body_summaries_many.status != axiom::StatusCode::Ok) {
        std::cerr << "extended io import/export directory behavior is unexpected\n";
        return 1;
    }
    {
        std::array<std::string, 2> bad_summary_paths {*body_summary_many_a.value, export_dir.string()};
        auto summary_fail = kernel.io().export_body_summaries_many(batch_bodies, bad_summary_paths);
        if (summary_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected export_body_summaries_many failure when output path is a directory\n";
            return 1;
        }
        auto summary_diag = kernel.diagnostics().get(summary_fail.diagnostic_id);
        const auto* summary_ctx = summary_diag.status == axiom::StatusCode::Ok && summary_diag.value.has_value()
                                      ? find_issue(*summary_diag.value, axiom::diag_codes::kIoBatchExportItemContext)
                                      : nullptr;
        if (summary_ctx == nullptr || summary_ctx->stage != "io.batch_export" ||
            !has_issue_code(*summary_diag.value, axiom::diag_codes::kIoExportFailure)) {
            std::cerr << "export_body_summaries_many failure missing batch export context or root cause\n";
            return 1;
        }
    }
    if (kernel.io().remove_files(batch_text_paths).status != axiom::StatusCode::Ok) {
        std::cerr << "extended io batch remove behavior is unexpected\n";
        return 1;
    }

    std::vector<std::string> extra_paths {out_path.string(), out_json_path.string(), out_path.string()};
    auto canon = kernel.io().canonical_or_normalized_path(out_path.string());
    auto rel = kernel.io().relative_to(out_path.string(), std::filesystem::temp_directory_path().string());
    auto common_dir = kernel.io().common_parent_directory(extra_paths);
    auto unique = kernel.io().unique_paths(extra_paths);
    auto sorted = kernel.io().sort_paths_lex(extra_paths);
    auto by_fmt = kernel.io().count_by_format(extra_paths);
    auto only_step = kernel.io().paths_of_format(extra_paths, "step");
    if (canon.status != axiom::StatusCode::Ok || !canon.value.has_value() ||
        rel.status != axiom::StatusCode::Ok || !rel.value.has_value() ||
        common_dir.status != axiom::StatusCode::Ok || !common_dir.value.has_value() || common_dir.value->empty() ||
        unique.status != axiom::StatusCode::Ok || !unique.value.has_value() || unique.value->size() != 2 ||
        sorted.status != axiom::StatusCode::Ok || !sorted.value.has_value() || sorted.value->size() != 3 ||
        by_fmt.status != axiom::StatusCode::Ok || !by_fmt.value.has_value() || by_fmt.value->empty() ||
        only_step.status != axiom::StatusCode::Ok || !only_step.value.has_value() || only_step.value->size() != 2) {
        std::cerr << "extra io path organize behavior is unexpected\n";
        return 1;
    }

    auto move_src = kernel.io().temp_path_for("axiom_move_src", ".txt");
    auto move_dst = kernel.io().temp_path_for("axiom_move_dst", ".txt");
    auto rename_dst = kernel.io().temp_path_for("axiom_rename_dst", ".txt");
    if (!move_src.value.has_value() || !move_dst.value.has_value() || !rename_dst.value.has_value() ||
        kernel.io().write_text_snapshot(*move_src.value, "abc").status != axiom::StatusCode::Ok ||
        kernel.io().move_file(*move_src.value, *move_dst.value).status != axiom::StatusCode::Ok ||
        kernel.io().rename_file(*move_dst.value, *rename_dst.value).status != axiom::StatusCode::Ok) {
        std::cerr << "extra io move/rename behavior is unexpected\n";
        return 1;
    }

    auto scan_dir = std::filesystem::temp_directory_path() / "axiom_io_scan_dir";
    auto nested_dir = scan_dir / "nested";
    if (kernel.io().ensure_directory(scan_dir.string()).status != axiom::StatusCode::Ok ||
        kernel.io().ensure_directory(nested_dir.string()).status != axiom::StatusCode::Ok) {
        std::cerr << "ensure directory failed\n";
        return 1;
    }
    auto scan_a = scan_dir / "a.txt";
    auto scan_b = nested_dir / "b.txt";
    kernel.io().write_text_snapshot(scan_a.string(), "A");
    kernel.io().write_text_snapshot(scan_b.string(), "B");
    auto dir_exists = kernel.io().directory_exists(scan_dir.string());
    auto list_flat = kernel.io().list_files_in_directory(scan_dir.string());
    auto list_rec = kernel.io().list_files_recursive(scan_dir.string());
    auto cnt_flat = kernel.io().count_files_in_directory(scan_dir.string(), false);
    auto cnt_rec = kernel.io().count_files_in_directory(scan_dir.string(), true);
    if (dir_exists.status != axiom::StatusCode::Ok || !dir_exists.value.has_value() || !*dir_exists.value ||
        list_flat.status != axiom::StatusCode::Ok || !list_flat.value.has_value() || list_flat.value->size() != 1 ||
        list_rec.status != axiom::StatusCode::Ok || !list_rec.value.has_value() || list_rec.value->size() != 2 ||
        cnt_flat.status != axiom::StatusCode::Ok || !cnt_flat.value.has_value() || *cnt_flat.value != 1 ||
        cnt_rec.status != axiom::StatusCode::Ok || !cnt_rec.value.has_value() || *cnt_rec.value != 2) {
        std::cerr << "extra io directory scan behavior is unexpected\n";
        return 1;
    }

    auto line_file = kernel.io().temp_path_for("axiom_line_ops", ".txt");
    std::array<std::string, 3> lines_to_write {"alpha", "beta", "alpha-beta"};
    if (!line_file.value.has_value() ||
        kernel.io().write_lines(*line_file.value, lines_to_write).status != axiom::StatusCode::Ok) {
        std::cerr << "write lines failed\n";
        return 1;
    }
    auto lines_read = kernel.io().read_lines(*line_file.value);
    auto lines_hit = kernel.io().grep_lines_contains(*line_file.value, "alpha");
    auto replaced = kernel.io().replace_in_file_text(*line_file.value, "alpha", "A");
    auto prepended = kernel.io().prepend_text(*line_file.value, "head\n");
    auto stem = kernel.io().file_stem(*line_file.value);
    auto ext = kernel.io().extension_of(*line_file.value);
    auto with_stem = kernel.io().with_stem(*line_file.value, "axiom_line_ops_2");
    auto with_suffix = kernel.io().append_suffix_before_ext(*line_file.value, "_v2");
    auto seq = kernel.io().generate_sequential_paths(std::filesystem::temp_directory_path().string(), "axiom_seq_", "txt", 3);
    std::array<std::string, 2> writable_candidates {"/tmp/no/such/dir/file.txt", *line_file.value};
    auto writable = kernel.io().first_writable_path(writable_candidates);
    auto strict_import = kernel.io().import_auto_existing_strict(import_step_paths, import_options);
    auto fmt_hist = kernel.io().summarize_format_histogram_txt(io_paths);
    if (lines_read.status != axiom::StatusCode::Ok || !lines_read.value.has_value() || lines_read.value->size() != 3 ||
        lines_hit.status != axiom::StatusCode::Ok || !lines_hit.value.has_value() || lines_hit.value->size() != 2 ||
        replaced.status != axiom::StatusCode::Ok || !replaced.value.has_value() || *replaced.value == 0 ||
        prepended.status != axiom::StatusCode::Ok ||
        stem.status != axiom::StatusCode::Ok || !stem.value.has_value() || stem.value->empty() ||
        ext.status != axiom::StatusCode::Ok || !ext.value.has_value() || ext.value->empty() ||
        with_stem.status != axiom::StatusCode::Ok || !with_stem.value.has_value() || with_stem.value->empty() ||
        with_suffix.status != axiom::StatusCode::Ok || !with_suffix.value.has_value() || with_suffix.value->empty() ||
        seq.status != axiom::StatusCode::Ok || !seq.value.has_value() || seq.value->size() != 3 ||
        writable.status != axiom::StatusCode::Ok || !writable.value.has_value() || writable.value->empty() ||
        strict_import.status != axiom::StatusCode::Ok || !strict_import.value.has_value() || strict_import.value->empty() ||
        fmt_hist.status != axiom::StatusCode::Ok || !fmt_hist.value.has_value() || fmt_hist.value->empty()) {
        std::cerr << "extra io text/stem/hist behavior is unexpected\n";
        return 1;
    }
    auto conditional_export = kernel.io().export_auto_existing_only(batch_bodies, batch_step_paths, export_options);
    if (conditional_export.status != axiom::StatusCode::Ok || !conditional_export.value.has_value() ||
        *conditional_export.value != 2 ||
        kernel.io().truncate_file(*line_file.value).status != axiom::StatusCode::Ok) {
        std::cerr << "extra io conditional export/truncate behavior is unexpected\n";
        return 1;
    }

    const auto p_iges = tmp / ("axiom_io_interchange_" + uniq + ".iges");
    const auto p_brep = tmp / ("axiom_io_interchange_" + uniq + ".brep");
    const auto p_obj = tmp / ("axiom_io_interchange_" + uniq + ".obj");
    const auto p_3mf = tmp / ("axiom_io_interchange_" + uniq + ".3mf");
    const auto p_stl_report = tmp / ("axiom_io_mesh_report_" + uniq + ".stl");
    const auto p_sidecar = tmp / ("axiom_io_mesh_report_" + uniq + ".mesh_report.json");
    if (kernel.io().export_iges(*body.value, p_iges.string(), export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_brep(*body.value, p_brep.string(), export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_obj(*body.value, p_obj.string(), export_options).status != axiom::StatusCode::Ok ||
        kernel.io().export_3mf(*body.value, p_3mf.string(), export_options).status != axiom::StatusCode::Ok) {
        std::cerr << "interchange mesh export (iges/brep/obj/3mf) failed\n";
        return 1;
    }
    auto fmt_iges = kernel.io().detect_format(p_iges.string());
    auto fmt_brep = kernel.io().detect_format(p_brep.string());
    auto fmt_obj = kernel.io().detect_format(p_obj.string());
    auto fmt_3mf = kernel.io().detect_format(p_3mf.string());
    if (fmt_iges.status != axiom::StatusCode::Ok || !fmt_iges.value.has_value() || *fmt_iges.value != "iges" ||
        fmt_brep.status != axiom::StatusCode::Ok || !fmt_brep.value.has_value() || *fmt_brep.value != "brep" ||
        fmt_obj.status != axiom::StatusCode::Ok || !fmt_obj.value.has_value() || *fmt_obj.value != "obj" ||
        fmt_3mf.status != axiom::StatusCode::Ok || !fmt_3mf.value.has_value() || *fmt_3mf.value != "3mf") {
        std::cerr << "detect_format for iges/brep/obj/3mf mismatch\n";
        return 1;
    }
    auto imp_iges = kernel.io().import_auto(p_iges.string(), import_options);
    auto imp_brep = kernel.io().import_auto(p_brep.string(), import_options);
    auto imp_obj = kernel.io().import_auto(p_obj.string(), import_options);
    auto imp_3mf = kernel.io().import_auto(p_3mf.string(), import_options);
    if (imp_iges.status != axiom::StatusCode::Ok || !imp_iges.value.has_value() ||
        imp_brep.status != axiom::StatusCode::Ok || !imp_brep.value.has_value() ||
        imp_obj.status != axiom::StatusCode::Ok || !imp_obj.value.has_value() ||
        imp_3mf.status != axiom::StatusCode::Ok || !imp_3mf.value.has_value()) {
        std::cerr << "import_auto roundtrip for iges/brep/obj/3mf failed\n";
        return 1;
    }
    auto bb_iges = kernel.representation().bbox_of_body(*imp_iges.value);
    auto bb_brep = kernel.representation().bbox_of_body(*imp_brep.value);
    auto bb_obj = kernel.representation().bbox_of_body(*imp_obj.value);
    auto bb_3mf = kernel.representation().bbox_of_body(*imp_3mf.value);
    if (bb_iges.status != axiom::StatusCode::Ok || !bb_iges.value.has_value() || bb_iges.value->max.x < 9.9 ||
        bb_brep.status != axiom::StatusCode::Ok || !bb_brep.value.has_value() || bb_brep.value->max.x < 9.9 ||
        bb_obj.status != axiom::StatusCode::Ok || !bb_obj.value.has_value() || bb_obj.value->max.x < 9.9 ||
        bb_3mf.status != axiom::StatusCode::Ok || !bb_3mf.value.has_value() || bb_3mf.value->max.x < 9.9) {
        std::cerr << "interchange import bbox not plausible for box body\n";
        return 1;
    }
    std::ifstream iges_in {p_iges};
    std::string iges_txt((std::istreambuf_iterator<char>(iges_in)), std::istreambuf_iterator<char>());
    if (iges_txt.find("START") == std::string::npos || iges_txt.find("TERMINATE") == std::string::npos) {
        std::cerr << "iges export missing expected markers\n";
        return 1;
    }
    std::ifstream brep_in {p_brep};
    std::string brep_txt((std::istreambuf_iterator<char>(brep_in)), std::istreambuf_iterator<char>());
    if (brep_txt.find("AXIOM_BREP_INTERCHANGE") == std::string::npos ||
        brep_txt.find("\"format\"") == std::string::npos || brep_txt.find("AXIOM_BREP") == std::string::npos) {
        std::cerr << "brep export missing axiom interchange payload\n";
        return 1;
    }
    std::ifstream obj_in {p_obj};
    std::string obj_txt((std::istreambuf_iterator<char>(obj_in)), std::istreambuf_iterator<char>());
    if (obj_txt.find("v ") == std::string::npos || obj_txt.find("f ") == std::string::npos) {
        std::cerr << "obj export missing vertices/faces\n";
        return 1;
    }
    axiom::ExportOptions report_opts = export_options;
    report_opts.write_mesh_validation_report = true;
    auto ex_stl_report = kernel.io().export_stl(*body.value, p_stl_report.string(), report_opts);
    if (ex_stl_report.status != axiom::StatusCode::Ok) {
        std::cerr << "stl export with mesh validation sidecar failed\n";
        return 1;
    }
    if (!std::filesystem::exists(p_sidecar)) {
        std::cerr << "expected mesh_report sidecar next to stl export\n";
        return 1;
    }
    auto sidecar_diag = kernel.diagnostics().get(ex_stl_report.diagnostic_id);
    if (sidecar_diag.status != axiom::StatusCode::Ok || !sidecar_diag.value.has_value() ||
        !has_issue_code(*sidecar_diag.value, axiom::diag_codes::kIoExportMeshReportSidecar)) {
        std::cerr << "missing mesh export sidecar diagnostic code\n";
        return 1;
    }
    std::ifstream sidecar_in {p_sidecar};
    std::string sidecar_json((std::istreambuf_iterator<char>(sidecar_in)), std::istreambuf_iterator<char>());
    if (sidecar_json.find('{') == std::string::npos || sidecar_json.find('}') == std::string::npos) {
        std::cerr << "mesh_report sidecar is not plausible json\n";
        return 1;
    }

    const auto bad_ext_path = tmp / ("axiom_io_unknown_ext_" + uniq + ".unsupported_io_ext");
    {
        std::ofstream bf(bad_ext_path);
        bf << "not a registered interchange format\n";
    }
    auto imp_unknown = kernel.io().import_auto(bad_ext_path.string(), import_options);
    if (imp_unknown.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected InvalidInput for import_auto with unknown extension\n";
        std::filesystem::remove(bad_ext_path);
        return 1;
    }
    auto imp_unknown_diag = kernel.diagnostics().get(imp_unknown.diagnostic_id);
    if (imp_unknown_diag.status != axiom::StatusCode::Ok || !imp_unknown_diag.value.has_value() ||
        !has_issue_code(*imp_unknown_diag.value, axiom::diag_codes::kIoImportFailure)) {
        std::cerr << "missing kIoImportFailure diagnostic for unknown format import\n";
        std::filesystem::remove(bad_ext_path);
        return 1;
    }
    auto exp_unknown = kernel.io().export_auto(*body.value, bad_ext_path.string(), export_options);
    if (exp_unknown.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected InvalidInput for export_auto with unknown extension\n";
        std::filesystem::remove(bad_ext_path);
        return 1;
    }
    auto exp_unknown_diag = kernel.diagnostics().get(exp_unknown.diagnostic_id);
    if (exp_unknown_diag.status != axiom::StatusCode::Ok || !exp_unknown_diag.value.has_value() ||
        !has_issue_code(*exp_unknown_diag.value, axiom::diag_codes::kIoExportFailure)) {
        std::cerr << "missing kIoExportFailure diagnostic for unknown format export\n";
        std::filesystem::remove(bad_ext_path);
        return 1;
    }
    std::filesystem::remove(bad_ext_path);

    const auto bad_batch_item = tmp / ("axiom_io_batch_item_bad_" + uniq + ".unsupported_io_ext");
    {
        std::ofstream bf(bad_batch_item);
        bf << "batch item placeholder\n";
    }
    const std::vector<std::string> batch_import_mixed {out_path.string(), bad_batch_item.string()};
    auto batch_import_fail = kernel.io().import_many_auto(batch_import_mixed, import_options);
    if (batch_import_fail.status == axiom::StatusCode::Ok) {
        std::cerr << "expected batch import failure when second path has unknown extension\n";
        std::filesystem::remove(bad_batch_item);
        return 1;
    }
    auto batch_import_diag = kernel.diagnostics().get(batch_import_fail.diagnostic_id);
    const auto* batch_import_ctx = batch_import_diag.status == axiom::StatusCode::Ok && batch_import_diag.value.has_value()
                                      ? find_issue(*batch_import_diag.value, axiom::diag_codes::kIoBatchImportItemContext)
                                      : nullptr;
    if (batch_import_ctx == nullptr || batch_import_ctx->stage != "io.batch_import" ||
        !has_issue_code(*batch_import_diag.value, axiom::diag_codes::kIoImportFailure)) {
        std::cerr << "batch import failure diagnostic missing batch context or merged root cause\n";
        std::filesystem::remove(bad_batch_item);
        return 1;
    }

    const auto bad_batch_export_path = tmp / ("axiom_io_batch_export_bad_" + uniq + ".unsupported_io_ext");
    const std::array<axiom::BodyId, 2> batch_export_bodies {*body.value, *body.value};
    const std::array<std::string, 2> batch_export_paths {out_path.string(), bad_batch_export_path.string()};
    auto batch_export_fail = kernel.io().export_many_auto(batch_export_bodies, batch_export_paths, export_options);
    if (batch_export_fail.status == axiom::StatusCode::Ok) {
        std::cerr << "expected batch export failure when second path has unknown extension\n";
        std::filesystem::remove(bad_batch_item);
        return 1;
    }
    auto batch_export_diag = kernel.diagnostics().get(batch_export_fail.diagnostic_id);
    const auto* batch_export_ctx = batch_export_diag.status == axiom::StatusCode::Ok && batch_export_diag.value.has_value()
                                       ? find_issue(*batch_export_diag.value, axiom::diag_codes::kIoBatchExportItemContext)
                                       : nullptr;
    if (batch_export_ctx == nullptr || batch_export_ctx->stage != "io.batch_export" ||
        batch_export_ctx->related_entities.empty() || batch_export_ctx->related_entities[0] != body.value->value ||
        !has_issue_code(*batch_export_diag.value, axiom::diag_codes::kIoExportFailure)) {
        std::cerr << "batch export failure diagnostic missing batch context, body relation, or merged root cause\n";
        std::filesystem::remove(bad_batch_item);
        return 1;
    }
    std::filesystem::remove(bad_batch_item);

    {
        const std::vector<std::string> detect_mixed {out_path.string(), std::string{}};
        auto detect_fail = kernel.io().detect_formats_with_paths(detect_mixed);
        if (detect_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected detect_formats_with_paths failure when an item path is empty\n";
            return 1;
        }
        auto detect_diag = kernel.diagnostics().get(detect_fail.diagnostic_id);
        const auto* detect_ctx = detect_diag.status == axiom::StatusCode::Ok && detect_diag.value.has_value()
                                    ? find_issue(*detect_diag.value, axiom::diag_codes::kIoBatchDetectFormatItemContext)
                                    : nullptr;
        if (detect_ctx == nullptr || detect_ctx->stage != "io.batch_detect_format" ||
            !has_issue_code(*detect_diag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "detect_formats_with_paths batch failure missing batch context or merged root cause\n";
            return 1;
        }
    }

    {
        const std::vector<std::string> two_items {out_path.string(), std::string{}};
        auto vfail = kernel.io().validate_import_paths(two_items);
        if (vfail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_import_paths failure when an item path is empty\n";
            return 1;
        }
        auto vdiag = kernel.diagnostics().get(vfail.diagnostic_id);
        const auto* vctx = vdiag.status == axiom::StatusCode::Ok && vdiag.value.has_value()
                               ? find_issue(*vdiag.value, axiom::diag_codes::kIoBatchPathTransformItemContext)
                               : nullptr;
        if (vctx == nullptr || vctx->stage != "io.batch_validate_import" ||
            !has_issue_code(*vdiag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "validate_import_paths batch failure missing D-0015 context or merged root cause\n";
            return 1;
        }
    }

    {
        const std::vector<std::string> two_export {out_path.string(), std::string{}};
        auto vexp_fail = kernel.io().validate_export_paths(two_export);
        if (vexp_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected validate_export_paths failure when an item path is empty\n";
            return 1;
        }
        auto vexp_diag = kernel.diagnostics().get(vexp_fail.diagnostic_id);
        const auto* vexp_ctx = vexp_diag.status == axiom::StatusCode::Ok && vexp_diag.value.has_value()
                                  ? find_issue(*vexp_diag.value, axiom::diag_codes::kIoBatchPathTransformItemContext)
                                  : nullptr;
        if (vexp_ctx == nullptr || vexp_ctx->stage != "io.batch_validate_export" ||
            !has_issue_code(*vexp_diag.value, axiom::diag_codes::kIoExportFailure)) {
            std::cerr << "validate_export_paths batch failure missing D-0015 context or merged root cause\n";
            return 1;
        }
    }

    {
        const std::vector<std::string> two_items {out_path.string(), std::string{}};
        auto nfail = kernel.io().normalize_paths(two_items);
        if (nfail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected normalize_paths failure when an item path is empty\n";
            return 1;
        }
        auto ndiag = kernel.diagnostics().get(nfail.diagnostic_id);
        const auto* nctx = ndiag.status == axiom::StatusCode::Ok && ndiag.value.has_value()
                               ? find_issue(*ndiag.value, axiom::diag_codes::kIoBatchPathTransformItemContext)
                               : nullptr;
        if (nctx == nullptr || nctx->stage != "io.batch_path_transform" ||
            !has_issue_code(*ndiag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "normalize_paths batch failure missing D-0015 context or merged root cause\n";
            return 1;
        }
    }
    {
        const std::vector<std::string> two_items {out_path.string(), std::string{}};
        auto cfail = kernel.io().change_extensions(two_items, "bak");
        if (cfail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected change_extensions failure when an item path is empty\n";
            return 1;
        }
        auto cdiag = kernel.diagnostics().get(cfail.diagnostic_id);
        const auto* cctx = cdiag.status == axiom::StatusCode::Ok && cdiag.value.has_value()
                               ? find_issue(*cdiag.value, axiom::diag_codes::kIoBatchPathTransformItemContext)
                               : nullptr;
        if (cctx == nullptr || cctx->stage != "io.batch_path_transform" ||
            !has_issue_code(*cdiag.value, axiom::diag_codes::kIoExportFailure)) {
            std::cerr << "change_extensions batch failure missing D-0015 context or merged root cause\n";
            return 1;
        }
    }
    {
        const std::array<std::string, 2> bad_names {"n1", ""};
        auto pfail = kernel.io().compose_paths(std::filesystem::temp_directory_path().string(), bad_names, "txt");
        if (pfail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected compose_paths failure when a name is empty\n";
            return 1;
        }
        auto pdiag = kernel.diagnostics().get(pfail.diagnostic_id);
        const auto* pctx = pdiag.status == axiom::StatusCode::Ok && pdiag.value.has_value()
                               ? find_issue(*pdiag.value, axiom::diag_codes::kIoBatchPathTransformItemContext)
                               : nullptr;
        if (pctx == nullptr || pctx->stage != "io.batch_path_transform" ||
            !has_issue_code(*pdiag.value, axiom::diag_codes::kIoExportFailure)) {
            std::cerr << "compose_paths batch failure missing D-0015 context or merged root cause\n";
            return 1;
        }
    }
    {
        const std::vector<std::string> two_items {out_path.string(), std::string{}};
        auto count_fail = kernel.io().count_by_format(two_items);
        if (count_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected count_by_format failure when an item path is empty\n";
            return 1;
        }
        auto count_diag = kernel.diagnostics().get(count_fail.diagnostic_id);
        const auto* count_ctx = count_diag.status == axiom::StatusCode::Ok && count_diag.value.has_value()
                                    ? find_issue(*count_diag.value, axiom::diag_codes::kIoBatchDetectFormatItemContext)
                                    : nullptr;
        if (count_ctx == nullptr || count_ctx->stage != "io.batch_detect_format" ||
            !has_issue_code(*count_diag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "count_by_format batch failure missing D-0011 context or merged root cause\n";
            return 1;
        }
    }
    {
        const std::vector<std::string> two_items {out_path.string(), std::string{}};
        auto pof_fail = kernel.io().paths_of_format(two_items, "step");
        if (pof_fail.status == axiom::StatusCode::Ok) {
            std::cerr << "expected paths_of_format failure when an item path is empty\n";
            return 1;
        }
        auto pof_diag = kernel.diagnostics().get(pof_fail.diagnostic_id);
        const auto* pof_ctx = pof_diag.status == axiom::StatusCode::Ok && pof_diag.value.has_value()
                                  ? find_issue(*pof_diag.value, axiom::diag_codes::kIoBatchDetectFormatItemContext)
                                  : nullptr;
        if (pof_ctx == nullptr || pof_ctx->stage != "io.batch_detect_format" ||
            !has_issue_code(*pof_diag.value, axiom::diag_codes::kIoImportFailure)) {
            std::cerr << "paths_of_format batch failure missing D-0011 context or merged root cause\n";
            return 1;
        }
    }

    const auto export_dir_bad = tmp / ("axiom_io_export_dir_bad_" + uniq);
    std::filesystem::create_directories(export_dir_bad);
    auto export_dir_bad_ext =
        kernel.io().export_auto_to_directory(batch_bodies, export_dir_bad.string(), "not_a_real_ext", export_options);
    if (export_dir_bad_ext.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected InvalidInput for export_auto_to_directory with unknown extension\n";
        std::filesystem::remove_all(export_dir_bad);
        return 1;
    }
    auto export_dir_bad_diag = kernel.diagnostics().get(export_dir_bad_ext.diagnostic_id);
    if (export_dir_bad_diag.status != axiom::StatusCode::Ok || !export_dir_bad_diag.value.has_value() ||
        !has_issue_code(*export_dir_bad_diag.value, axiom::diag_codes::kIoUnknownFormat)) {
        std::cerr << "export_auto_to_directory unknown ext should report kIoUnknownFormat\n";
        std::filesystem::remove_all(export_dir_bad);
        return 1;
    }
    std::filesystem::remove_all(export_dir_bad);

    const auto ro_root = tmp / ("axiom_io_readonly_" + uniq);
    std::filesystem::create_directories(ro_root);
    try {
        std::filesystem::permissions(ro_root,
                                       std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
    } catch (...) {
    }
    const auto ro_step_path = ro_root / "denied.step";
    auto export_ro = kernel.io().export_step(*body.value, ro_step_path.string(), export_options);
    if (export_ro.status != axiom::StatusCode::Ok) {
        auto ro_diag = kernel.diagnostics().get(export_ro.diagnostic_id);
        if (ro_diag.status != axiom::StatusCode::Ok || !ro_diag.value.has_value() ||
            !has_issue_code(*ro_diag.value, axiom::diag_codes::kIoExportPathNotWritable)) {
            std::cerr << "export to read-only directory should surface kIoExportPathNotWritable when write is denied\n";
            try {
                std::filesystem::permissions(ro_root, std::filesystem::perms::owner_all);
            } catch (...) {
            }
            std::filesystem::remove_all(ro_root);
            return 1;
        }
    }
    try {
        std::filesystem::permissions(ro_root, std::filesystem::perms::owner_all);
    } catch (...) {
    }
    std::filesystem::remove_all(ro_root);

    auto imp_empty = kernel.io().import_auto("", import_options);
    if (imp_empty.status != axiom::StatusCode::InvalidInput) {
        std::cerr << "expected InvalidInput for empty import path\n";
        return 1;
    }

    std::filesystem::remove(*rename_dst.value);
    std::filesystem::remove(scan_a);
    std::filesystem::remove(scan_b);
    std::filesystem::remove(nested_dir);
    std::filesystem::remove(scan_dir);
    std::filesystem::remove(*line_file.value);

    std::filesystem::remove(p_iges);
    std::filesystem::remove(p_brep);
    std::filesystem::remove(p_obj);
    std::filesystem::remove(p_3mf);
    std::filesystem::remove(p_stl_report);
    std::filesystem::remove(p_sidecar);
    std::filesystem::remove(out_path);
    std::filesystem::remove(out_json_path);
    std::filesystem::remove(dirty_path);
    std::filesystem::remove(*temp_txt.value);
    std::filesystem::remove(*export_default_step_path.value);
    std::filesystem::remove(*export_default_json_path.value);
    std::filesystem::remove(*auto_export_step_path.value);
    std::filesystem::remove(*auto_export_json_path.value);
    std::filesystem::remove(*batch_step_1.value);
    std::filesystem::remove(*batch_step_2.value);
    std::filesystem::remove(*batch_json_1.value);
    std::filesystem::remove(*batch_json_2.value);
    std::filesystem::remove(*text_file.value);
    std::filesystem::remove(*summary_out.value);
    std::filesystem::remove(*body_summary_many_a.value);
    std::filesystem::remove(*body_summary_many_b.value);
    std::filesystem::remove(export_dir / "body_0.step");
    std::filesystem::remove(export_dir / "body_1.step");
    std::filesystem::remove(export_dir / "body_0.axmjson");
    std::filesystem::remove(export_dir / "body_1.axmjson");
    std::filesystem::remove(export_dir);
    return 0;
}
