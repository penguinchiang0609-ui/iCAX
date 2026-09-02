#pragma once

#include "ExtrusionRecognitionTypes.h"

namespace iCAX::ExtrusionRecognition
{
    struct SExtrusionAnalysis final
    {
        iCAX::GeometryData::Direction3 Axis;
        iCAX::GeometryData::Direction3 SectionX;
        iCAX::GeometryData::Direction3 SectionZ;
        double dFirst = 0.0;
        double dLast = 0.0;
        SSectionSnapshot Section;
    };

    bool TryAnalyzeExtrusion(
        IN const iCAX::GeometryData::BRepModel& Geometry_,
        IN const SRecognitionOptions& Options_,
        OUT SExtrusionAnalysis& Analysis_,
        OUT ERecognitionStatus& Status_,
        OUT std::vector<std::string>& Diagnostics_);

    bool TryNormalizeExtrusion(
        IN const iCAX::GeometryData::BRepModel& Geometry_,
        IN const SExtrusionAnalysis& Analysis_,
        IN const SSectionPlacement& Placement_,
        IN const iCAX::GeometryData::Direction3& TargetAxis_,
        OUT iCAX::GeometryData::BRepModel& Normalized_,
        OUT std::string& strError_);
}

