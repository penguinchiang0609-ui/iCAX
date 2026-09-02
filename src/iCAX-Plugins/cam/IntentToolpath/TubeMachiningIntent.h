#pragma once

#include "IntentToolpathExport.h"

#include "GeometryData/TubeNeutralGeometry.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace iCAX::CAM::Intent::Tube
{
    enum class EBoundaryTracePurpose : std::uint8_t
    {
        CutBoundary = 0,
        EndBoundary = 1,
        UVPath = 2,
        UVRegionBoundary = 3
    };

    /*
    * @brief Side Atlas 上的机器无关名义轨迹。
    * @details UVPath 是权威表达；世界坐标曲线由 Support 指向的 Side Atlas 求值得到。
    *          对切割轨迹，KeptMaterialOnLeft 给后续割缝补偿提供稳定方向，但不决定加工顺序。
    */
    struct SBoundaryTrace final
    {
        std::string ID;
        iCAX::GeometryData::Tube::SBoundarySurfaceRef Support;
        iCAX::GeometryData::CompositeCurve2 UVPath;
        EBoundaryTracePurpose Purpose = EBoundaryTracePurpose::CutBoundary;
        bool KeptMaterialOnLeft = true;
        std::map<std::string, std::string> Metadata;
    };

    struct SExtrudedCutIntent final
    {
        std::string RemovalNodeID;
    };

    struct SWrappedCutIntent final
    {
        std::string RemovalNodeID;
    };

    struct STaperedCutIntent final
    {
        std::string RemovalNodeID;
    };

    struct SAlternativeCutIntent final
    {
        std::string RemovalNodeID;
        std::vector<std::string> CandidateIDs;
        std::string RecommendedCandidateID;
        std::string SelectedCandidateID;
    };

    struct SHalfSpaceCutIntent final
    {
        std::string RemovalNodeID;
    };

    struct SCompositeCutIntent final
    {
        std::string RemovalNodeID;
        std::string BRepResourceID;
        std::size_t AnalyticPatchCount = 0;
        bool SupportsManualFeaturePromotion = false;
    };

    struct SResidualCutIntent final
    {
        std::string RemovalNodeID;
        std::string BRepResourceID;
    };

    enum class EEndBoundary : std::uint8_t
    {
        First = 0,
        Last = 1
    };

    struct SEndCutCandidateIntent final
    {
        std::string BaseExtrusionNodeID;
        EEndBoundary End = EEndBoundary::First;
    };

    struct SUVVectorIntent final
    {
        std::string UVFeatureID;
    };

    struct SUVImageIntent final
    {
        std::string UVFeatureID;
        std::string ImageResourceID;
    };

    using MachiningFeatureData = std::variant<
        SExtrudedCutIntent,
        STaperedCutIntent,
        SWrappedCutIntent,
        SHalfSpaceCutIntent,
        SCompositeCutIntent,
        SResidualCutIntent,
        SAlternativeCutIntent,
        SEndCutCandidateIntent,
        SUVVectorIntent,
        SUVImageIntent>;

    struct SOpeningProfileTarget final
    {
        std::string ProfileSweepNodeID;
        std::string SourceCutNodeID;
        std::string EvaluatedVolumeNodeID;
        std::optional<iCAX::GeometryData::Tube::SFeatureOpeningRef> TargetOpening;
        iCAX::GeometryData::Tube::EOpeningProfileFrameRule FrameRule =
            iCAX::GeometryData::Tube::EOpeningProfileFrameRule::SourceCutNormalPlane;
        iCAX::GeometryData::Tube::SOpeningProfileField ProfileField;
        iCAX::GeometryData::Tube::EParameterObservability Observability =
            iCAX::GeometryData::Tube::EParameterObservability::Observed;
    };

    struct SMachiningFeature final
    {
        std::string ID;
        std::string Label;
        MachiningFeatureData Data;
        std::vector<SBoundaryTrace> BoundaryTraces;
        std::vector<SOpeningProfileTarget> OpeningProfileTargets;
        std::optional<iCAX::GeometryData::Tube::SRemovalSemantics> RemovalSemantics;
        bool RequiresGeometricIntersection = false;
        double Confidence = 1.0;
        std::map<std::string, std::string> Metadata;
    };

    struct SCompilationDiagnostic final
    {
        std::string Code;
        std::string Message;
        std::string SourceID;
    };

    enum class ECompilationStatus : std::uint8_t
    {
        Complete = 0,
        RequiresGeometricIntersection = 1,
        ContainsResidual = 2,
        RequiresInterpretationSelection = 3,
        Invalid = 4
    };

    /*
    * @brief Tube CSG 编译出的机器无关加工意图。
    * @details 本资源不包含排序、割缝补偿、引线、速度、机床姿态或 NC 指令。
    */
    struct CTubeMachiningIntent final
    {
        inline static constexpr const char* kResourceTypeName = "tube.machining_intent";

        std::uint64_t Version = 0;
        std::uint64_t SourceNeutralGeometryVersion = 0;
        std::string SourceNeutralGeometryResourceID;
        ECompilationStatus Status = ECompilationStatus::Invalid;
        std::vector<SMachiningFeature> Features;
        std::vector<SCompilationDiagnostic> Diagnostics;
        std::map<std::string, std::string> Metadata;
    };

    /*
    * @brief 编译无需几何内核即可确定的 CSG 加工语义。
    * @details Wrapped、端部候选和打标直接生成 UV Trace；Extruded/HalfSpace/Residual
    *          生成稳定 Feature，并标记为需要后续几何求交。
    */
    _INTENT_TOOLPATH_EXP CTubeMachiningIntent CompileTubeMachiningIntent(
        IN const iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_);
}
