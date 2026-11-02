#pragma once

#include "OpenCascadeResourceImportExport.h"
#include <TopoDS_Shape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax3.hxx>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace iCAX::CAD
{
    struct STubeWrappedHostGeometry final
    {
        TopoDS_Shape Shape;
        gp_Ax3 SectionFrame;
        double AxialFirst = 0.0;
        double AxialLast = 0.0;
        std::vector<TopoDS_Wire> SectionWires; // Exact world-space section loops.
    };

    struct STubeWrappedRegionResult final
    {
        TopoDS_Shape Shape; // Exact material-set union of complete removal solids.
        std::size_t ToolPatchCount = 0;
        bool Approximate = false;
    };

    // Host is the uncut extrusion with its known exact section. UVFaces contains complete planar regions
    // in world XY: U is outer-profile arc length, S is axial distance.
    // The cutter crosses the first continuous host wall, preserving curved
    // profile surfaces and the existing small-radius bisector construction.
    // This geometry adapter consumes no sketch recipe or scene state.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP STubeWrappedRegionResult EvaluateTubeWrappedRegion(
        const STubeWrappedHostGeometry& Host_, const TopoDS_Shape& UVFaces_,
        double OuterClearance_ = 0.2, double InnerClearance_ = 0.2);

    // Return exact selected UV boundaries on the host's outer skin. Empty
    // BoundaryEdges selects all input region boundaries. Periodic selection
    // follows actual profile arc length, without using mesh topology IDs.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP std::vector<TopoDS_Edge> MapTubeWrappedBoundaries(
        const STubeWrappedHostGeometry& Host_, const TopoDS_Shape& UVFaces_,
        const std::vector<TopoDS_Edge>& BoundaryEdges_ = {});

    struct STubeWrappedTopologyBoundary final
    {
        std::uint64_t EdgeID = 0;
        std::uint64_t ReferenceFaceID = 0;
    };

    // Bind mapped boundaries to this exact finished BRep and its exposed cut
    // walls. Returned IDs belong to TopExp maps of FinishedShape only. Host
    // skin and ambiguous adjacent cut faces are explicitly excluded.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP std::vector<STubeWrappedTopologyBoundary> BindTubeWrappedBevel(
        const STubeWrappedHostGeometry& Host_, const TopoDS_Shape& UVFaces_,
        const std::vector<TopoDS_Edge>& BoundaryEdges_,
        const TopoDS_Shape& FinishedShape_, const TopoDS_Shape& Cutter_);
}
