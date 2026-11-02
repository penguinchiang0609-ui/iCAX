#pragma once
#include "GeometryFeatureAnalysisService.h"

namespace iCAX::Geometry {
struct SmoothSurfaceRegion {
    std::uint64_t Component=0;
    std::vector<std::uint64_t> Faces;
    double AreaMm2=0;
};
// Neutral topology/continuity analysis. The caller chooses which regions to use.
class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ISurfaceRegionService:public Services::IService {
public:
    virtual std::vector<SmoothSurfaceRegion> SmoothRegions(const GeometryData::BRepModel&,
        const std::vector<std::uint64_t>& candidates,const SGeometryAnalysisOptions&)const=0;
};
}
