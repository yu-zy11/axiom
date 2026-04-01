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
inline constexpr std::string_view kGeoIntersectionFailure = "AXM-GEO-E-0007";

inline constexpr std::string_view kTopoLoopNotClosed = "AXM-TOPO-E-0002";
inline constexpr std::string_view kTopoFaceOuterLoopInvalid = "AXM-TOPO-E-0003";
inline constexpr std::string_view kTopoFaceInnerLoopInvalid = "AXM-TOPO-E-0004";
inline constexpr std::string_view kTopoShellNotClosed = "AXM-TOPO-E-0005";
inline constexpr std::string_view kTopoDanglingEdge = "AXM-TOPO-E-0006";
inline constexpr std::string_view kTopoRelationInconsistent = "AXM-TOPO-E-0007";
// Finer-grained relation consistency (Stage 2+)
inline constexpr std::string_view kTopoCoedgeAlreadyOwned = "AXM-TOPO-E-0016";
inline constexpr std::string_view kTopoDuplicateFaceInShell = "AXM-TOPO-E-0017";
inline constexpr std::string_view kTopoShellDisconnected = "AXM-TOPO-E-0018";
inline constexpr std::string_view kTopoCurveTopologyMismatch = "AXM-TOPO-E-0008";
inline constexpr std::string_view kTopoInvariantBroken = "AXM-TOPO-E-0009";
inline constexpr std::string_view kTopoOpenBoundary = "AXM-TOPO-E-0010";
inline constexpr std::string_view kTopoNonManifoldEdge = "AXM-TOPO-E-0011";
inline constexpr std::string_view kTopoSourceRefInvalid = "AXM-TOPO-E-0012";
inline constexpr std::string_view kTopoSourceRefMismatch = "AXM-TOPO-E-0013";
inline constexpr std::string_view kTopoLoopDuplicateEdge = "AXM-TOPO-E-0014";
inline constexpr std::string_view kTopoLoopOrientationMismatch = "AXM-TOPO-E-0015";
inline constexpr std::string_view kTopoDanglingCoedge = "AXM-TOPO-E-0019";
inline constexpr std::string_view kTopoDanglingVertex = "AXM-TOPO-E-0020";
inline constexpr std::string_view kTopoOrphanLoop = "AXM-TOPO-E-0021";

inline constexpr std::string_view kBoolInvalidInput = "AXM-BOOL-E-0001";
inline constexpr std::string_view kBoolIntersectionFailure = "AXM-BOOL-E-0003";
inline constexpr std::string_view kBoolClassificationFailure = "AXM-BOOL-E-0005";
inline constexpr std::string_view kBoolRebuildFailure = "AXM-BOOL-E-0006";
inline constexpr std::string_view kBoolNearDegenerateWarning = "AXM-BOOL-W-0001";
inline constexpr std::string_view kBoolPrepNoCandidateWarning = "AXM-BOOL-W-0002";
/// 已进入布尔候选构建阶段（占位实现：基于壳/包围盒区域统计）。
inline constexpr std::string_view kBoolStageCandidates = "AXM-BOOL-D-0001";
inline constexpr std::string_view kBoolPrepCandidatesBuilt = "AXM-BOOL-D-0002";
inline constexpr std::string_view kBoolLocalClipApplied = "AXM-BOOL-D-0003";
/// 布尔一次运行内的阶段与输入摘要（占位实现：bbox 关系 + 预处理统计），便于追踪 lhs/rhs/output。
inline constexpr std::string_view kBoolRunStageSummary = "AXM-BOOL-D-0004";
/// 已完成结果体占位物化/输出记录（占位实现：最小 owned topology 或 bbox 回退链路已执行）。
inline constexpr std::string_view kBoolStageOutputMaterialized = "AXM-BOOL-D-0005";
/// 已生成面级候选对（真实算法入口：face-face candidate generation）。
inline constexpr std::string_view kBoolFaceCandidatesBuilt = "AXM-BOOL-D-0006";
/// 已生成解析交线/交曲线（真实算法入口：exact intersection for selected face candidates）。
inline constexpr std::string_view kBoolIntersectionCurvesBuilt = "AXM-BOOL-D-0007";
/// 已将交线裁剪为面域内的交线段（真实算法入口：trimming/clipping before imprint）。
inline constexpr std::string_view kBoolIntersectionSegmentsBuilt = "AXM-BOOL-D-0008";
/// 已将交线/交线段存入交线集合（Intersection）以供切分/分类/重建阶段复用。
inline constexpr std::string_view kBoolIntersectionWiresStored = "AXM-BOOL-D-0009";
/// 已应用占位的拓扑切分/imprint（最小闭环：对输出壳的某个矩形面沿对角线切分）。
inline constexpr std::string_view kBoolImprintApplied = "AXM-BOOL-D-0010";
/// 已按交线段对矩形面执行切分/imprint（边上插点拆边 + 切割边），进入真实 split 开发。
inline constexpr std::string_view kBoolImprintSegmentApplied = "AXM-BOOL-D-0011";
/// 已完成分类阶段（占位实现：基于 bbox/面域信息的最小可解释分类统计）。
inline constexpr std::string_view kBoolClassificationCompleted = "AXM-BOOL-D-0012";
/// 已完成重建阶段（占位实现：输出壳/体一致性与 Strict 校验摘要）。
inline constexpr std::string_view kBoolRebuildCompleted = "AXM-BOOL-D-0013";
/// 已进入切分阶段（真实算法入口：imprint/split/trim）。
inline constexpr std::string_view kBoolStageSplit = "AXM-BOOL-D-0014";
/// 已进入分类阶段（真实算法入口：cell/face classification）。
inline constexpr std::string_view kBoolStageClassify = "AXM-BOOL-D-0015";
/// 已进入重建阶段（真实算法入口：shell rebuild / stitch / merge）。
inline constexpr std::string_view kBoolStageRebuild = "AXM-BOOL-D-0016";
/// 已进入验证阶段（Strict/Standard validation for rebuilt topology）。
inline constexpr std::string_view kBoolStageValidate = "AXM-BOOL-D-0017";
/// 已进入修复阶段（auto_repair / heal fallback）。
inline constexpr std::string_view kBoolStageRepair = "AXM-BOOL-D-0018";

inline constexpr std::string_view kBlendInvalidTarget = "AXM-BLEND-E-0001";
inline constexpr std::string_view kBlendParameterTooLarge = "AXM-BLEND-E-0002";
/// 圆角/倒角等混合特征当前仍为占位近似（拓扑骨架 + 参数门禁），未提供工业级几何生成。
inline constexpr std::string_view kBlendApproximatePlaceholder = "AXM-BLEND-W-0001";

inline constexpr std::string_view kModOffsetInvalid = "AXM-MOD-E-0001";
inline constexpr std::string_view kModOffsetSelfIntersection = "AXM-MOD-E-0002";
inline constexpr std::string_view kModShellFailure = "AXM-MOD-E-0003";
inline constexpr std::string_view kModReplaceFaceIncompatible = "AXM-MOD-E-0005";
inline constexpr std::string_view kModDeleteFaceHealFailure = "AXM-MOD-E-0006";
/// 抽壳等修改已生成结果体，但后验校验未通过并已回滚（结果体未保留）。
inline constexpr std::string_view kModShellValidateFailed = "AXM-MOD-E-0008";

inline constexpr std::string_view kQueryClosestPointFailure = "AXM-QUERY-E-0001";
inline constexpr std::string_view kQuerySectionFailure = "AXM-QUERY-E-0002";
inline constexpr std::string_view kQueryMassPropertiesFailure = "AXM-QUERY-E-0003";

inline constexpr std::string_view kHealSewFailure = "AXM-HEAL-E-0001";
inline constexpr std::string_view kHealSmallEdgeFailure = "AXM-HEAL-E-0002";
inline constexpr std::string_view kHealSmallFaceFailure = "AXM-HEAL-E-0003";
inline constexpr std::string_view kHealAutoRepairFailure = "AXM-HEAL-E-0006";
inline constexpr std::string_view kHealFeatureRemovedWarning = "AXM-HEAL-W-0001";
inline constexpr std::string_view kHealRepairValidated = "AXM-HEAL-D-0005";
/// 修复管线阶段标记（可检索/聚合，用于回放与导入闭环追踪）；`Issue::message` 为操作名（如 `auto_repair`）。
inline constexpr std::string_view kHealRepairPipelineTrace = "AXM-HEAL-D-0006";
/// 修复管线回放摘要（逗号分隔操作名，与 D-0006 顺序一致）；便于日志/CI 解析而不依赖 `summarize_repair` 文本。
inline constexpr std::string_view kHealRepairReplaySummary = "AXM-HEAL-D-0007";

inline constexpr std::string_view kValSelfIntersection = "AXM-VAL-E-0001";
/// 与文档字典一致：拓扑非流形等“形状不可用”类验证失败（验证层语义，可与 TOPO 细分码并存）。
inline constexpr std::string_view kValNonManifoldGeometry = "AXM-VAL-E-0002";
inline constexpr std::string_view kValToleranceConflict = "AXM-VAL-E-0003";
inline constexpr std::string_view kValDegenerateGeometry = "AXM-VAL-E-0004";
/// 支撑曲面参数域退化/无效（如修剪域反转、扫掠角为零），Strict 几何验证可定位。
inline constexpr std::string_view kValParameterDomainInvalid = "AXM-VAL-E-0007";
/// 自交检测：三角化规模超出预算（可调大 chordal_error 或后续引入加速结构后重试）。
inline constexpr std::string_view kValSelfIntersectionMeshBudget = "AXM-VAL-E-0008";
/// Strict：包围盒最小厚度相对当前线性容差过小，几何细节可能落在容差噪声尺度内（薄壁/薄片风险）。
inline constexpr std::string_view kValModelFinerThanTolerance = "AXM-VAL-E-0009";

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
inline constexpr std::string_view kPluginVersionIncompatible = "AXM-PLUGIN-E-0002";
inline constexpr std::string_view kPluginCapabilityManifestIncomplete = "AXM-PLUGIN-E-0003";
inline constexpr std::string_view kPluginExecutionFailure = "AXM-PLUGIN-E-0004";
inline constexpr std::string_view kPluginResultValidationWarning = "AXM-PLUGIN-E-0005";
inline constexpr std::string_view kPluginDuplicateManifestName = "AXM-PLUGIN-E-0006";
inline constexpr std::string_view kPluginRegistryQuotaExceeded = "AXM-PLUGIN-E-0007";
inline constexpr std::string_view kPluginNotRegistered = "AXM-PLUGIN-E-0008";

// 与字典/注册路径别名一致（避免重复字面量定义）。
inline constexpr std::string_view kPluginCapabilityIncomplete = kPluginCapabilityManifestIncomplete;
inline constexpr std::string_view kPluginDuplicateImplementation = kPluginDuplicateManifestName;
inline constexpr std::string_view kPluginHostCapacityExceeded = kPluginRegistryQuotaExceeded;

inline constexpr std::string_view kPluginDiscoveryReport = "AXM-PLUGIN-D-0001";

inline constexpr std::string_view kTxCommitFailure = "AXM-TX-E-0001";
inline constexpr std::string_view kTxRollbackFailure = "AXM-TX-E-0002";
inline constexpr std::string_view kTxConflict = "AXM-TX-E-0003";

}  // namespace axiom::diag_codes
