#pragma once

#include "Data/Variant.h"
#include "GeometryData/TubeNeutralGeometry.h"
#include "TubeDesignerExport.h"

#include <string>
#include <vector>

namespace iCAX::TubeDesigner
{
    // Input geometry must already be in the part drawing's immutable blank
    // coordinates: extrusion along +X, X from zero to BlankLength_, and the
    // section bounding box centred on Y=Z=0. The helper only proposes recipes;
    // its caller must independently replay the drawing tools and compare the
    // resulting BRep with the imported part before persisting anything.
    struct SRecoveredPartDrawingFeatures final
    {
        // Complete means every removal node was mapped to a candidate. It does
        // not imply that the candidate reconstructs the imported geometry.
        bool Complete = false;
        iCAX::Data::VariantArray Features;
        iCAX::Data::ObjectMap Ends;
        std::vector<std::string> UnsupportedNodeIDs;
    };

    _TUBE_DESIGNER_EXP SRecoveredPartDrawingFeatures MapNeutralTubeFeatures(
        const iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_,
        double BlankLength_,
        double Tolerance_ = 0.001);
}
