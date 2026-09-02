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
        std::vector<std::string> Diagnostics;
    };

    struct _EXTRUSION_RECOGNITION_EXP SRecognitionResult final
    {
        ERecognitionStatus Status = ERecognitionStatus::InvalidRequest;
        iCAX::GeometryData::BRepModel NormalizedGeometry;
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
