#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"
#include "Services/IService.h"
#include "GeometryFeatureAnalysisService.h"
#include <array>
#include <optional>

namespace iCAX::Geometry
{
    struct SCurveLocation { std::size_t SegmentIndex=0; double NativeParameter=0; };
    struct SCurvePiece
    {
        iCAX::GeometryData::CompositeCurve3 Curve;
        // Each output segment retains the input curve and its native parameter domain.
        std::vector<std::size_t> SourceSegmentIndices;
        std::vector<std::array<double,3>> PreviewPoints;
    };
    struct SCurveTrace
    {
        SCurvePiece Piece;
        SCurveLocation End;
        double PerimeterMm=0;
    };
    struct SCurveSample
    {
        std::size_t SegmentIndex=0;
        double NativeParameter=0;
        std::array<double,3> Point{},Tangent{};
    };
    struct SCurveSamples
    {
        std::vector<SCurveSample> Points;
        std::vector<double> SegmentLengthsMm;
    };
    struct SCurveMeasure
    {
        double LengthMm=0,TurnRad=0;
        std::array<double,3> FirstTangent{},LastTangent{};
    };
    struct SBoundaryPathJunction
    {
        std::size_t SegmentIndex=0;
        SCurveLocation Before,After;
        double LengthMm=0;
    };
    struct SBoundaryPath
    {
        iCAX::GeometryData::CompositeCurve3 Curve;
        std::vector<std::optional<std::size_t>> SourceSegments;
        std::vector<SBoundaryPathJunction> Junctions;
    };
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ICurveEditingService : public iCAX::Services::IService
    {
    public:
        // Retain each supplied exact source curve. Add explicit short line
        // segments only for verified CAD junctions within the requested bound.
        virtual SBoundaryPath JoinBoundary(const iCAX::GeometryData::BRepModel&,
            const iCAX::GeometryData::CompositeCurve3&,const std::vector<SBoundaryCurveSpan>&,
            double maximumJunctionMm) const=0;
        virtual SCurveSamples Sample(const iCAX::GeometryData::CompositeCurve3&,
            double chordToleranceMm,double maximumStepMm) const=0;
        virtual std::vector<SCurvePiece> Split(const iCAX::GeometryData::CompositeCurve3&,
            const std::vector<SCurveLocation>&) const=0;
        // Walk by physical arc length, retaining exact source curves and native domains.
        virtual SCurveTrace TraceClosed(const iCAX::GeometryData::CompositeCurve3&,
            SCurveLocation start,double distanceMm,bool reverse) const=0;
        // Intrinsic interval measures, independent of native parameter speed.
        virtual SCurveMeasure Measure(const iCAX::GeometryData::CurveSegment3&) const=0;
    };
}
