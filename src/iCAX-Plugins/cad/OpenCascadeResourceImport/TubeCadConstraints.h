#pragma once
#include "GeometryData/TubeNeutralGeometry.h"
#include "OpenCascadeTubeCSGConverter.h"

namespace iCAX::CAD
{
    // Resolve geometric host relationships before publishing an edited
    // document. The resolved primitives are persisted with their rules.
    void ResolveTubeCadConstraints(const GeometryData::Tube::CTubeNeutralGeometry& Original,
        GeometryData::Tube::CTubeNeutralGeometry& Edited,
        const OpenCascade::STubeCSGEvaluationOptions& Options);
}
