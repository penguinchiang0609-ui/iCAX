#pragma once

#include "OpenCascadeResourceImportExport.h"
#include "Services/IService.h"
#include "GeometryData/GeometryData.h"
#include <TopoDS_Shape.hxx>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace iCAX::CAD
{
    // IDs identify exact BRep topology, never display triangles. For an
    // imported document these are the persisted neutral BRep record IDs.
    struct SCadBevelOperation final
    {
        std::uint64_t EdgeID = 0;
        std::uint64_t ReferenceFaceID = 0; // 0: find the narrow through-wall face.
        std::string Side = "outer"; // outer = selected edge; inner = opposite edge.
        double AngleDegrees = 45.0; // Angle to the original through-wall cut face.
        double RootFace = 0.0; // Remaining through-thickness land, in model units.
    };
    struct SCadSelectableEdge final
    {
        std::uint64_t EdgeID = 0;
        std::vector<std::uint64_t> AdjacentFaceIDs;
        std::vector<std::array<double, 3>> Points; // Display only.
    };
    struct SCadSelectableFace final
    {
        std::uint64_t FaceID = 0;
        std::vector<std::uint64_t> EdgeIDs;
        std::vector<std::array<double, 3>> TriangleVertices; // Three per triangle.
        std::string SurfaceKind; // Geometric classification, independent of UI labels.
    };
    struct SCadTopologyResult final
    {
        bool bOK = false;
        std::vector<SCadSelectableEdge> Edges;
        std::vector<SCadSelectableFace> Faces;
        std::string Diagnostic;
    };
    struct SCadBevelResult final
    {
        bool bOK = false;
        iCAX::GeometryData::BRepModel Geometry;
        double RemovedVolume = 0.0;
        std::string Diagnostic;
    };
    struct SCadNativeBevelResult final
    {
        bool bOK = false;
        TopoDS_Shape Shape;
        double RemovedVolume = 0.0;
        std::string Diagnostic;
    };
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP std::string CadExactTopologySignature(const TopoDS_Shape& Shape);
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SCadNativeBevelResult ApplyCadNativeBevel(const TopoDS_Shape& Shape,
        const std::vector<SCadBevelOperation>& Operations);

    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ICadBevelService : public iCAX::Services::IService
    {
    public:
        ~ICadBevelService() override = default;
        virtual SCadTopologyResult Inspect(const iCAX::GeometryData::BRepModel& Geometry_) const = 0;
        virtual SCadBevelResult Apply(const iCAX::GeometryData::BRepModel& Geometry_,
            const std::vector<SCadBevelOperation>& Operations_) const = 0;
        // Native geometry adapter for an immutable kernel shape. Topology IDs
        // are TopExp map indices of this same shape, not neutral record IDs.
        virtual SCadTopologyResult InspectNative(const TopoDS_Shape& Shape_) const = 0;
        virtual SCadNativeBevelResult ApplyNative(const TopoDS_Shape& Shape_,
            const std::vector<SCadBevelOperation>& Operations_) const = 0;
    };
}
