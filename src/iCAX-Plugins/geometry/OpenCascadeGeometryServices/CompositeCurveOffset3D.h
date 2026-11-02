#pragma once
#include <GeometryServices/CurveOffset3DService.h>
#include <memory>
namespace iCAX::GeometryServices
{
    // Per-call preparation for one certified planar composite. The public
    // result continues to carry the original three-dimensional exact laws.
    class IPlanarOffsetEvaluator3
    {
    public:
        virtual ~IPlanarOffsetEvaluator3() = default;
        // Samples remain in the local plane (Z=0). Original spatial endpoint
        // evaluations are used when constructing output connectors.
        virtual GeometryData::CurveOffsetEvaluation3 Evaluate(
            const GeometryData::OffsetCurvePiece3&, const std::vector<double>&,
            const CurveOffsetEvaluationOptions&) const = 0;
        virtual GeometryData::Point3 ToLocal(GeometryData::Point3) const = 0;
        virtual GeometryData::Vector3 ToLocal(GeometryData::Vector3) const = 0;
    };
    struct PlanarOffsetPreparationCancelled {};
    std::unique_ptr<IPlanarOffsetEvaluator3> PreparePlanarOffset3D(
        const std::vector<GeometryData::DirectionalCurveOffset3>&, const CompositeOffsetOptions3&);
    GeometryData::CompositeOffsetResult3 BuildCompositeOffset3D(
        const ICurveOffset3DService&, const std::vector<GeometryData::DirectionalCurveOffset3>&,
        bool, const CompositeOffsetOptions3&);
}
