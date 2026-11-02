#pragma once

#include "OpenCascadeResourceImportExport.h"

#include "GeometryData/GeometryData.h"

#include <string>
#include <vector>
#include <functional>

#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>

namespace iCAX::OpenCascade
{
    struct _OPEN_CASCADE_RESOURCE_IMPORT_EXP SBRepFileReadResult final
    {
        bool bOK = false;
        iCAX::GeometryData::BRepModel Geometry;
        std::string FormatID;
        std::string DisplayName;
        std::string ContentHash;
        std::vector<std::string> Diagnostics;
    };

    /*
    * @brief 把 CAD 文件解码成内存 BRep，不向任何资源库写入数据。
    * @details 供“解码 -> 识别/规范化 -> 成功后发布资源”的事务式导入流程使用。
    */
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SBRepFileReadResult ReadBRepFile(
        IN const std::string& strSourcePath_,
        IN double dTolerance_ = 0.001);

    _OPEN_CASCADE_RESOURCE_IMPORT_EXP iCAX::GeometryData::BRepModel ConvertOpenCascadeShapeToBRep(
        IN const TopoDS_Shape& Shape_,
        IN const std::string& strDisplayName_,
        IN const std::string& strSourceID_,
        IN double dTolerance_ = 0.001);

    // Exact topology export with caller-supplied display triangulations. The
    // provider must supply the corresponding current face, in model coordinates;
    // no remeshing or tessellation-derived CAD reconstruction is performed.
    using BRepTriangulationProvider = std::function<iCAX::GeometryData::Triangulation3(const TopoDS_Face&,std::uint64_t)>;
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP iCAX::GeometryData::BRepModel ConvertOpenCascadeShapeToBRepWithDisplay(
        const TopoDS_Shape& shape, const std::string& displayName, const std::string& sourceID,
        const BRepTriangulationProvider& triangulations);

    struct SBRepConversionInput final
    {
        TopoDS_Shape Shape;
        std::string DisplayName;
        std::string SourceID;
    };

    // Pure in-memory conversion, including display triangulation. Inputs are
    // deep-copied before meshing so shared instances stay unchanged. Results
    // preserve input order; all tasks finish before returning/throwing. No
    // resource library or scene access occurs on workers. 0 = auto, 1 = serial.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP std::vector<iCAX::GeometryData::BRepModel>
        ConvertOpenCascadeShapesToBRep(
            const std::vector<SBRepConversionInput>& Inputs_,
            double dTolerance_ = 0.001,
            std::size_t MaximumConcurrency_ = 0);

    // Display-only conversion. Unlike the BRep conversion above, this skips
    // vertices/edges/surfaces/topology and publishes only the triangulation
    // needed by a renderer. Inputs and result ordering follow the same rules.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP std::vector<iCAX::GeometryData::CTriangleMeshResource>
        ConvertOpenCascadeShapesToTriangleMeshes(
            const std::vector<SBRepConversionInput>& Inputs_,
            double dTolerance_ = 0.001,
            std::size_t MaximumConcurrency_ = 0);

    // Read-only display edges: no triangulation, shape copying or resource writes.
    // Curves are sampled coarsely for a lightweight interactive wireframe.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP std::vector<iCAX::GeometryData::Polyline3>
        ConvertOpenCascadeShapeToPreviewWireframe(const TopoDS_Shape& Shape_);
}
