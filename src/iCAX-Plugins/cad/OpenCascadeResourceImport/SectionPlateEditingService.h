#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/TubeNeutralGeometry.h"
#include "SectionPlateService.h"
#include "Services/IService.h"
#include <array>
#include <functional>
#include <vector>

namespace iCAX::Geometry {
struct SectionPlateHoleEditInput {
    GeometryData::BRepModel OriginalGeometry;
    std::array<double,16> Frame;
    // Optional generic spacing constraint; zero leaves manual cutout spacing
    // to the author. ProtectedMaterial/Voids and connectivity still apply.
    double Thickness=0,MinimumWeb=5;
    // Complete exact local-mm cutouts, including immutable holes. Cutouts
    // combine by union and may cross the exterior to form an edge notch.
    // Existing Tube CAD convention: Segment2 uses [0,1] interpolation; all
    // analytic/spline bases retain native parameter ranges. No preview points.
    std::vector<GeometryData::Loop2> Holes;
    // Material protection is clipped to the original material. Protected voids
    // must remain empty (mortises and established clearance passages).
    std::vector<GeometryData::Loop2> ProtectedMaterial,ProtectedVoids;
};
struct SectionPlateHoleEditResult {
    GeometryData::BRepModel Geometry;
    GeometryData::Tube::CPlanarRegion2 Profile;
    std::vector<std::array<double,2>> PreviewOutline;
    std::vector<std::vector<std::array<double,2>>> PreviewHoles;
    // Contributors to each resulting interior boundary, in Profile/preview
    // order. EqualInput is Holes.size() when a merged contour has no exact
    // input counterpart. This lets the owner preserve editable identities.
    std::vector<std::vector<size_t>> HoleInputIndices;
    std::vector<size_t> EqualInput;
    bool OutlineChanged=false;
};
// A single existing plate only. Scene ownership, enabled state, revisions,
// resource publication, history and sibling plates remain with the product.
class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ISectionPlateEditingService:public Services::IService {
public:
    // Boundaries[0] is outer O0; holes are H0,H1,... in the BRep cap's stored
    // wire order. EditHoles returns normalized material and contributor mapping.
    virtual GeometryData::Tube::CPlanarRegion2 ReadProfile(const GeometryData::BRepModel&,
        const std::array<double,16>& frame,double thickness)const=0;
    // Zero band protects the actual interfaces with only geometric tolerance.
    // Full crossing-post preservation is a separate conservative caller policy.
    virtual std::vector<GeometryData::Loop2> ReadProtection(const SectionPlate& current,
        const std::vector<SectionPlate>& assembly,double band,bool entireCrossingPosts=true)const=0;
    virtual SectionPlateHoleEditResult EditHoles(const SectionPlateHoleEditInput&,
        const std::function<bool()>& cancelled={})const=0;
};
}
