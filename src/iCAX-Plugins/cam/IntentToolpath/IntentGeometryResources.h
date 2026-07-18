#pragma once

#include "GeometryData/GeometryData.h"

#include <cstdint>
#include <string>

namespace iCAX::CAM::Intent
{
// 意图刀路的内核无关几何资源。二维曲线用于平面/展开域，三维曲线用于场景和空间加工。
struct CIntentCurveResource final
{
    inline static constexpr const char* kResourceTypeName = "cam.intent.curve";

    std::uint64_t Version = 0;
    iCAX::GeometryData::CompositeCurve2 Curve2;
    iCAX::GeometryData::CompositeCurve3 Curve3;
    bool HasCurve2 = false;
    bool HasCurve3 = false;
    std::string Parameterization; // Native / Normalized / ArcLength / SurfaceUV
};

// 可选的承载曲面。管材展开、曲面投影和三维线切割可以共享同一表达。
struct CIntentSupportSurfaceResource final
{
    inline static constexpr const char* kResourceTypeName = "cam.intent.support_surface";

    std::uint64_t Version = 0;
    iCAX::GeometryData::Surface3 Surface;
    std::string CoordinateSystemResourceID;
};
}
