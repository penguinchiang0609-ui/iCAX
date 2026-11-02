#pragma once

#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/TubeNeutralGeometry.h"
#include <TopoDS_Shape.hxx>
#include <string>
#include <vector>

namespace iCAX::OpenCascade
{
    struct STubeBoundaryCompilation final
    {
        GeometryData::Tube::CTubeBoundaryGeometry Geometry;
        GeometryData::Placement3 LocalToDocument;
        std::vector<std::string> Diagnostics;
    };

    // Frame.Z is the stock axis; Frame.X/Frame.Y carry its exact section.
    // Compilation owns copies and never changes the supplied CAD geometry.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP STubeBoundaryCompilation CompileTubeBoundary(
        const TopoDS_Shape& source,
        const GeometryData::Tube::SExtrudedRegionNode& nominalStock,
        double tolerance = 1.0e-6);

    // Only Section and MachiningFaces participate. No authoring graph, retained
    // source body, feature recognition, or scene resource is consulted.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP TopoDS_Shape RestoreTubeBoundary(
        const GeometryData::Tube::CTubeBoundaryGeometry& geometry,
        double tolerance = 1.0e-6);

    // Finite nominal material in normalized coordinates, independently derived
    // from the section and explicit boundary's actual X extent.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP TopoDS_Shape BuildTubeBoundaryStock(
        const GeometryData::Tube::CTubeBoundaryGeometry& geometry,
        double tolerance = 1.0e-6);
}
