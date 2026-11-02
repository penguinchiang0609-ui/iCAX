#pragma once
#include "GeometryFeatureAnalysisService.h"
namespace iCAX::Geometry::Detail
{
    std::vector<SBoundaryTermination> AnalyzeBoundaryTerminations(const iCAX::GeometryData::BRepModel&,
        const std::vector<std::uint64_t>&,const std::vector<SAnalyticBoundary>&,const SGeometryAnalysisOptions&);
    SBoundarySweepAnalysis AnalyzeBoundarySweep(const IGeometryFeatureAnalysisService&,
        const iCAX::GeometryData::BRepModel&,std::uint64_t,const std::vector<std::uint64_t>&,
        const SGeometryAnalysisOptions&);
}
