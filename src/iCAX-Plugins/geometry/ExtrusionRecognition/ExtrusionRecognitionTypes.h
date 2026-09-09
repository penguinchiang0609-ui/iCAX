#pragma once

#include "ExtrusionRecognitionExport.h"

#include "Data/Variant.h"
#include "GeometryData/GeometryData.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace iCAX::ExtrusionRecognition
{
    enum class ERecognitionStatus : std::uint8_t
    {
        Success = 0,
        InvalidRequest,
        InvalidGeometry,
        MultipleSolidCandidates,
        AxisNotFound,
        NotExtrusionBased,
        SectionExtractionFailed,
        SectionTypeNotMatched,
        SectionTypeDefinitionInvalid,
        PlacementFailed
    };

    struct _EXTRUSION_RECOGNITION_EXP SSectionLoop2 final
    {
        std::string ID;
        std::vector<iCAX::GeometryData::Point2> Points;
        bool bInner = false;
    };

    /*
    * @brief 拉伸识别服务使用的中立二维截面快照。
    * @details 该快照只承载匹配所需的稳定几何事实，不保存 OCC Edge/Face 编号。
    */
    struct _EXTRUSION_RECOGNITION_EXP SSectionSnapshot final
    {
        std::vector<SSectionLoop2> Loops;
        iCAX::GeometryData::BoundingBox2 Bounds;
        iCAX::GeometryData::Point2 Centroid;
        iCAX::GeometryData::Direction2 PrincipalLongAxis;
        iCAX::GeometryData::Direction2 PrincipalShortAxis = { 0.0, 1.0 };
        double dMaterialArea = 0.0;
        std::size_t nCavityCount = 0;
    };

    /*
    * @brief 外部声明式截面类型定义。
    * @details Bindings/Match/Parameters/Placement 均为规则表达式；具体算子由 Service 注册表提供。
    */
    struct _EXTRUSION_RECOGNITION_EXP SSectionTypeDefinition final
    {
        std::string TypeID;
        std::string DisplayName;
        iCAX::Data::ObjectMap ParameterSchema;
        iCAX::Data::ObjectMap Bindings;
        iCAX::Data::Variant Match;
        iCAX::Data::ObjectMap Parameters;
        iCAX::Data::ObjectMap Placement;
        std::map<std::string, std::string> Metadata;
    };

    struct _EXTRUSION_RECOGNITION_EXP SRecognitionOptions final
    {
        iCAX::GeometryData::Direction3 TargetAxis = { 0.0, 1.0, 0.0 };
        double dLinearTolerance = 0.001;
        double dAngularToleranceRadians = 0.001;
        std::size_t nSliceSampleCount = 17;
        // 至少需要多少张相邻截面保持不变，才把零件判定为拉伸体。
        std::size_t nMinimumStableSliceCount = 2;
        // 用于抵抗三角化误差；不是截面类型匹配的“可信度”。
        double dStableSectionRelativeTolerance = 0.001;
    };

    /*
    * @brief 一个可被 C++ 依次调用的 Python 截面 fitter。
    * @details ScriptPath 指向包含 fitter(contours, context) 的 profile.py。
    */
    struct _EXTRUSION_RECOGNITION_EXP SPythonSectionFitter final
    {
        std::string TypeID;
        std::string ScriptPath;
        std::string PackageDigest;
    };

    struct _EXTRUSION_RECOGNITION_EXP SPythonSectionFitterOptions final
    {
        // 为空时按当前安装布局自动查找；调用方也可以显式指定。
        std::string PythonRuntimeLibraryPath;
        std::string WorkerScriptPath;
        double dLinearTolerance = 0.001;
        bool bContinueAfterFitterError = true;
    };

    struct _EXTRUSION_RECOGNITION_EXP SExtrusionDirectionOptions final
    {
        double dAngularToleranceRadians = 0.017453292519943295; // 1 degree
        double dMinimumEvidenceLength = 1.0e-6;
        double dAmbiguousSecondToFirstRatio = 0.5;
        double dFaceNormalVoteWeight = 0.35;
    };

    struct _EXTRUSION_RECOGNITION_EXP SExtrusionDirectionResult final
    {
        bool bSuccess = false;
        iCAX::GeometryData::Direction3 Direction;
        // 将输入 BRep 的 Direction 对齐到全局 +X、并将包围盒中心移到原点的 TRSF。
        iCAX::GeometryData::Transform3 TRSF;
        iCAX::GeometryData::BRepModel AlignedGeometry;
        double dConfidence = 0.0;
        double dPrimaryEvidenceWeight = 0.0;
        double dSecondaryEvidenceWeight = 0.0;
        bool bUsedFaceNormalDisambiguation = false;
        std::vector<std::string> Diagnostics;
    };

    struct _EXTRUSION_RECOGNITION_EXP SSectionWireOptions final
    {
        // A face is a section side face when its tangent plane contains +X.
        double dFaceParallelToleranceRadians = 0.001;
        // Endpoints within this distance are represented by one graph node.
        double dConnectionTolerance = 0.001;
        std::size_t nCurveSampleCount = 33;
    };

    struct _EXTRUSION_RECOGNITION_EXP SSectionWireEdge final
    {
        // Curve2 coordinates are (Y, Z) coordinates on the reset BRep's YOZ plane.
        std::uint64_t SourceEdgeId = 0;
        iCAX::GeometryData::Curve2 Curve;
        iCAX::GeometryData::ParameterRange Range;
        iCAX::GeometryData::Point2 Start;
        iCAX::GeometryData::Point2 End;
        // The projected Curve is stored in source-edge orientation. Consumers
        // should reverse it when this flag is true to follow the wire order.
        bool bReversed = false;
        // Discrete support points are included for area/orientation checks and
        // for consumers that need a quick preview without evaluating Curve.
        std::vector<iCAX::GeometryData::Point2> Samples;
    };

    struct _EXTRUSION_RECOGNITION_EXP SSectionWire final
    {
        bool bInner = false;
        bool bClosed = false;
        double dSignedArea = 0.0;
        std::vector<SSectionWireEdge> Edges;
    };

    struct _EXTRUSION_RECOGNITION_EXP SSectionWiresResult final
    {
        bool bSuccess = false;
        std::vector<SSectionWire> Wires;
        std::vector<std::uint64_t> SideFaceIds;
        std::vector<std::string> Diagnostics;
    };

    struct _EXTRUSION_RECOGNITION_EXP SSectionPlacement final
    {
        iCAX::GeometryData::Point2 Origin;
        iCAX::GeometryData::Direction2 XDirection;
        iCAX::GeometryData::Direction2 ZDirection = { 0.0, 1.0 };
    };

    struct _EXTRUSION_RECOGNITION_EXP SSectionMatchResult final
    {
        bool bMatched = false;
        iCAX::Data::ObjectMap Parameters;
        SSectionPlacement Placement;
        // Python fitter 返回的、作用于已对齐 BRep 的最终截面摆正 TRSF。
        iCAX::GeometryData::Transform3 TRSF;
        std::vector<std::string> Diagnostics;
    };

    struct _EXTRUSION_RECOGNITION_EXP SRecognitionResult final
    {
        ERecognitionStatus Status = ERecognitionStatus::InvalidRequest;
        // 识别得到的无向拉伸轴。正负方向等价；归一化阶段会按 TargetAxis
        // 选择最终的正方向。
        iCAX::GeometryData::Direction3 Direction;
        iCAX::GeometryData::BRepModel NormalizedGeometry;
        // 从原始 BRep 到 NormalizedGeometry 的总 TRSF。
        iCAX::GeometryData::Transform3 TRSF;
        std::string SectionTypeID;
        iCAX::Data::ObjectMap SectionParameters;
        SSectionSnapshot Section;
        double dLength = 0.0;
        std::vector<std::string> Diagnostics;

        bool IsOK() const noexcept
        {
            return Status == ERecognitionStatus::Success;
        }
    };

    struct _EXTRUSION_RECOGNITION_EXP SDefinitionValidationResult final
    {
        bool bValid = false;
        std::vector<std::string> Diagnostics;
    };

    struct _EXTRUSION_RECOGNITION_EXP SDefinitionLoadResult final
    {
        bool bValid = false;
        std::vector<SSectionTypeDefinition> Definitions;
        std::vector<std::string> Diagnostics;
    };
}
