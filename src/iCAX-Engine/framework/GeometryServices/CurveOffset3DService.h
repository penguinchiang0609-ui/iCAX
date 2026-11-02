#pragma once

#include <Services/IService.h>
#include <GeometryData/CurveOffset3D.h>
#include <functional>

namespace iCAX::GeometryServices
{
    struct CurveOffsetEvaluationOptions
    {
        std::size_t MaxSamples = 65536;
        double MinTangentMagnitude = 1e-12; // source length per native parameter
        double MinDirectionMagnitude = 1e-12;
        double MinCrossSine = 1e-8;         // scale-independent angular degeneracy
        double MinOffsetSpeedRatio = 1e-10;
        // Absolute position tolerance and work limit for clothoid quadrature.
        double IntegrationTolerance = 1e-10;
        std::size_t MaxIntegrationEvaluations = 65536;
        std::function<bool()> IsCancelled;
    };

    struct CompositeOffsetOptions3
    {
        double Tolerance = 1e-5;
        double MiterLimit = 4;
        GeometryData::EOffsetJoin3 Join = GeometryData::EOffsetJoin3::Miter;
        GeometryData::EOffsetLoopRetention3 LoopRetention = GeometryData::EOffsetLoopRetention3::Longest;
        std::size_t MaxSamples = 65536, MaxIntersectionPairs = 2000000, MaxRemovedLoops = 256;
        std::function<bool()> IsCancelled;
    };

    // Stateless foundation capability: no product, SDO, scene, resource or CAM types.
    // An error returns no partial samples. Success covers only requested parameters;
    // it does not certify intervals, intersections, surface contact or machinability.
    class ICurveOffset3DService : public Services::IService
    {
    public:
        virtual GeometryData::CurveOffsetEvaluation3 Evaluate(
            const GeometryData::DirectionalCurveOffset3& curve,
            const std::vector<double>& parameters,
            const CurveOffsetEvaluationOptions& options = {}) const = 0;

        // Numerical topology construction in 3D, within the requested distance
        // tolerance. Returns exact offset expressions, never fitted polylines.
        // Does not certify surface contact, cutter collision or machinability.
        // Failure is atomic: Curve and RemovedLoops are empty.
        virtual GeometryData::CompositeOffsetResult3 BuildComposite(
            const std::vector<GeometryData::DirectionalCurveOffset3>& segments,
            bool closed, const CompositeOffsetOptions3& options = {}) const = 0;

        virtual GeometryData::CurveOffsetEvaluation3 EvaluatePiece(
            const GeometryData::OffsetCurvePiece3& piece,
            const std::vector<double>& parameters,
            const CurveOffsetEvaluationOptions& options = {}) const = 0;
    };
}
