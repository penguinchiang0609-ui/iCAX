#pragma once

#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"

#include <string>
#include <vector>
#include <map>

#include <TopoDS_Shape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <Geom_Curve.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom_Surface.hxx>

namespace iCAX::OpenCascade
{
    // Shared exact conversion; callers retain the source parameter interval.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP Handle(Geom_Curve) BuildOpenCascadeCurve(const iCAX::GeometryData::Curve3& curve);
    bool IsOpenCascadeCurveRangeValid(const Handle(Geom_Curve)& curve,
        const iCAX::GeometryData::ParameterRange& range);
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP Handle(Geom2d_Curve) BuildOpenCascadeCurve2(const iCAX::GeometryData::Curve2& curve);
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP Handle(Geom_Surface) BuildOpenCascadeSurface(const iCAX::GeometryData::Surface3& surface);
    // Exact support conversion; preserves face/edge topology and trims.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP TopoDS_Shape ConvertOpenCascadeSweptToElementary(const TopoDS_Shape& shape);
    struct SOpenCascadeBRepBuildResult final
    {
        bool bOK = false;
        TopoDS_Shape Shape;
        // Exact reconstructed neutral record identities, retained for CAD
        // selection replay independently of TopExp traversal order.
        std::map<std::uint64_t, TopoDS_Edge> Edges;
        std::map<std::uint64_t, TopoDS_Face> Faces;
        std::vector<std::string> Diagnostics;
    };

    /*
    * @brief 从 iCAX 中性 BRep 重建 OCC Shape。
    * @details CAD 编辑必须以资源池中的规范化 BRep 为事实源，不能依赖仍然存在的原 STEP 文件。
    */
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SOpenCascadeBRepBuildResult BuildOpenCascadeShape(
        IN const iCAX::GeometryData::BRepModel& Geometry_);

    // Reconstruction only. bOK proves construction, not geometric validity.
    // Workflows that perform their own explicit BRepCheck use this entry to
    // avoid the builder's duplicate advisory validity scan.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SOpenCascadeBRepBuildResult ReconstructOpenCascadeShape(
        IN const iCAX::GeometryData::BRepModel& Geometry_);
}
