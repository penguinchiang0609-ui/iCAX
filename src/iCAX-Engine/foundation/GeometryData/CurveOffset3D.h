#pragma once

#include "GeometryData.h"
#include <optional>

namespace iCAX::GeometryData
{
    // Coefficients encode vector components, not positions. Under a rigid transform
    // rotate them without translation. Their parameter is the source curve's u.
    struct ParametricVectorField3
    {
        std::variant<Bezier3, BSpline3, NURBS3> Coefficients;
    };

    // Exact surface normal along a same-parameter pcurve. Orientation changes
    // the direction only. All data is in one geometry frame, with no CAD IDs.
    struct SurfaceNormalField3
    {
        Surface3 Surface;
        Curve2 Parameters;
        double Orientation = 1;
    };

    // An exact local offset law, not a fitted spline or a trimmed toolpath.
    // Q(u) = P(u) + SignedDistance * normalize(D(u) cross P'(u)).
    // P' always refers to increasing native parameter. Range.Reversed changes
    // traversal only; it does not flip the geometric side or the derivatives.
    // Periodic sources keep unwrapped native u. Polyline u uses point indices;
    // Clothoid u is arc length. A finite Source.Range is always required.
    struct DirectionalCurveOffset3
    {
        CurveSegment3 Source;
        std::variant<Direction3, ParametricVectorField3, SurfaceNormalField3> ReferenceDirection = Direction3{};
        double SignedDistance = 0.0;
    };

    struct CurveOffsetSample3
    {
        double SourceParameter = 0.0;
        Point3 SourcePoint;
        Point3 OffsetPoint;
        Vector3 SourceDerivative;
        Vector3 OffsetDerivative;
        Direction3 ReferenceDirection;
        Direction3 OffsetDirection;
    };

    enum class ECurveOffsetStatus
    {
        SamplesEvaluated,
        InvalidInput,
        UnsupportedCurve,
        InsufficientContinuity,
        DegenerateTangent,
        DegenerateDirection,
        ParallelDirection,
        SingularOffset,
        Cancelled,
        BudgetExceeded,
        EvaluationFailed
    };

    enum class ECurveOffsetCoverage { None, RequestedParametersOnly };

    struct CurveOffsetEvaluation3
    {
        ECurveOffsetStatus Status = ECurveOffsetStatus::InvalidInput;
        ECurveOffsetCoverage Coverage = ECurveOffsetCoverage::None;
        std::vector<CurveOffsetSample3> Samples;
        std::optional<double> FailedParameter;
        std::string Diagnostic;
        std::string Backend;
    };

    // The output keeps an evaluable offset law and its trimmed native u range.
    // Corner connectors have their own parameter domain; they are not disguised
    // as samples of a source edge. SourceSegment is absent for connectors.
    struct OffsetCurvePiece3
    {
        std::variant<DirectionalCurveOffset3, CurveSegment3> Geometry;
        std::optional<std::size_t> SourceSegment;
    };

    struct CompositeOffsetCurve3
    {
        std::vector<OffsetCurvePiece3> Segments;
        bool Closed = false;
    };

    enum class EOffsetJoin3 { Miter, Bevel, Round };
    enum class EOffsetLoopRetention3 { Longest, Start };
    enum class ECompositeOffsetStatus3
    {
        Built, InvalidInput, EvaluationFailed, DisconnectedSource,
        Collapsed, IntersectionUnresolved, BudgetExceeded, Cancelled
    };

    struct CompositeOffsetResult3
    {
        ECompositeOffsetStatus3 Status = ECompositeOffsetStatus3::InvalidInput;
        CompositeOffsetCurve3 Curve;
        // Preserve removed loops for inspection. In 3D there is no universal
        // inside/outside winding rule; retention is an explicit caller policy.
        std::vector<CompositeOffsetCurve3> RemovedLoops;
        std::size_t TrimmedJoints = 0, AddedConnectors = 0;
        double Tolerance = 0;
        std::string Diagnostic;
    };
}
