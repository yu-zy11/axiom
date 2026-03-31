#pragma once

#include <string_view>

namespace axiom::diag_codes {

inline constexpr std::string_view kCoreInvalidHandle = "AXM-CORE-E-0001";
inline constexpr std::string_view kCoreParameterOutOfRange = "AXM-CORE-E-0002";
inline constexpr std::string_view kCoreOperationUnsupported = "AXM-CORE-E-0004";

inline constexpr std::string_view kGeoCurveCreationInvalid = "AXM-GEO-E-0001";
inline constexpr std::string_view kGeoSurfaceCreationInvalid = "AXM-GEO-E-0002";
inline constexpr std::string_view kGeoDegenerateGeometry = "AXM-GEO-E-0003";
inline constexpr std::string_view kGeoParameterOutOfDomain = "AXM-GEO-E-0004";
inline constexpr std::string_view kGeoClosestPointFailure = "AXM-GEO-E-0005";
inline constexpr std::string_view kGeoParameterSolveFailure = "AXM-GEO-E-0006";

inline constexpr std::string_view kTopoLoopNotClosed = "AXM-TOPO-E-0002";
inline constexpr std::string_view kTopoFaceOuterLoopInvalid = "AXM-TOPO-E-0003";
inline constexpr std::string_view kTopoShellNotClosed = "AXM-TOPO-E-0005";
inline constexpr std::string_view kTopoCurveTopologyMismatch = "AXM-TOPO-E-0008";
inline constexpr std::string_view kTopoOpenBoundary = "AXM-TOPO-E-0010";
inline constexpr std::string_view kTopoNonManifoldEdge = "AXM-TOPO-E-0011";
inline constexpr std::string_view kTopoSourceRefInvalid = "AXM-TOPO-E-0012";
inline constexpr std::string_view kTopoSourceRefMismatch = "AXM-TOPO-E-0013";

inline constexpr std::string_view kBoolInvalidInput = "AXM-BOOL-E-0001";
inline constexpr std::string_view kBoolIntersectionFailure = "AXM-BOOL-E-0003";
inline constexpr std::string_view kBoolClassificationFailure = "AXM-BOOL-E-0005";
inline constexpr std::string_view kBoolRebuildFailure = "AXM-BOOL-E-0006";
inline constexpr std::string_view kBoolNearDegenerateWarning = "AXM-BOOL-W-0001";
inline constexpr std::string_view kBoolPrepNoCandidateWarning = "AXM-BOOL-W-0002";
inline constexpr std::string_view kBoolPrepCandidatesBuilt = "AXM-BOOL-D-0002";
inline constexpr std::string_view kBoolLocalClipApplied = "AXM-BOOL-D-0003";

inline constexpr std::string_view kBlendInvalidTarget = "AXM-BLEND-E-0001";
inline constexpr std::string_view kBlendParameterTooLarge = "AXM-BLEND-E-0002";

inline constexpr std::string_view kModOffsetInvalid = "AXM-MOD-E-0001";
inline constexpr std::string_view kModOffsetSelfIntersection = "AXM-MOD-E-0002";
inline constexpr std::string_view kModShellFailure = "AXM-MOD-E-0003";
inline constexpr std::string_view kModReplaceFaceIncompatible = "AXM-MOD-E-0005";
inline constexpr std::string_view kModDeleteFaceHealFailure = "AXM-MOD-E-0006";

inline constexpr std::string_view kQueryClosestPointFailure = "AXM-QUERY-E-0001";
inline constexpr std::string_view kQuerySectionFailure = "AXM-QUERY-E-0002";
inline constexpr std::string_view kQueryMassPropertiesFailure = "AXM-QUERY-E-0003";

inline constexpr std::string_view kHealSewFailure = "AXM-HEAL-E-0001";
inline constexpr std::string_view kHealSmallEdgeFailure = "AXM-HEAL-E-0002";
inline constexpr std::string_view kHealSmallFaceFailure = "AXM-HEAL-E-0003";
inline constexpr std::string_view kHealAutoRepairFailure = "AXM-HEAL-E-0006";
inline constexpr std::string_view kHealFeatureRemovedWarning = "AXM-HEAL-W-0001";
inline constexpr std::string_view kHealRepairValidated = "AXM-HEAL-D-0005";

inline constexpr std::string_view kValSelfIntersection = "AXM-VAL-E-0001";
inline constexpr std::string_view kValToleranceConflict = "AXM-VAL-E-0003";
inline constexpr std::string_view kValDegenerateGeometry = "AXM-VAL-E-0004";

inline constexpr std::string_view kIoFileNotFound = "AXM-IO-E-0001";
inline constexpr std::string_view kIoUnknownFormat = "AXM-IO-E-0002";
inline constexpr std::string_view kIoCorruptFile = "AXM-IO-E-0003";
inline constexpr std::string_view kIoImportFailure = "AXM-IO-E-0004";
inline constexpr std::string_view kIoExportFailure = "AXM-IO-E-0005";
inline constexpr std::string_view kIoPostImportValidation = "AXM-IO-D-0004";
inline constexpr std::string_view kIoPostImportRepairMode = "AXM-IO-D-0005";

inline constexpr std::string_view kTesFailure = "AXM-TES-E-0001";

inline constexpr std::string_view kEvalCycleDetected = "AXM-EVAL-E-0001";

inline constexpr std::string_view kPluginLoadFailure = "AXM-PLUGIN-E-0001";

inline constexpr std::string_view kTxCommitFailure = "AXM-TX-E-0001";
inline constexpr std::string_view kTxRollbackFailure = "AXM-TX-E-0002";
inline constexpr std::string_view kTxConflict = "AXM-TX-E-0003";

}  // namespace axiom::diag_codes
