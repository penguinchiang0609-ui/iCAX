#pragma once

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>

#include <vector>

namespace etp::tests
{
    struct FaceTrajectoryFixture final
    {
        TopoDS_Face Face;
        TopoDS_Wire TransverseTrajectory;
        TopoDS_Wire RulingTrajectory;
    };

    [[nodiscard]] std::vector<TopoDS_Wire> RectangularTubeSection();
    [[nodiscard]] std::vector<TopoDS_Wire> SolidRectangularSection();
    [[nodiscard]] TopoDS_Face TrapezoidFeatureFace();
    [[nodiscard]] TopoDS_Face TriangleFeatureFace();
    [[nodiscard]] TopoDS_Face NurbsEncodedTrapezoidFeatureFace();
    [[nodiscard]] TopoDS_Shape RectangularTubeSolid();
    [[nodiscard]] TopoDS_Shape RotatedRectangularTubeSolid();
    [[nodiscard]] TopoDS_Wire SegmentedOuterSection();
    [[nodiscard]] TopoDS_Face WideTopFace();
    [[nodiscard]] FaceTrajectoryFixture HalfCylinderFixture();
    [[nodiscard]] FaceTrajectoryFixture PinchedCylinderFixture();
    [[nodiscard]] TopoDS_Face CompositeStadiumSideFace();
    [[nodiscard]] std::vector<TopoDS_Face> IndependentAdjacentFaces();
    [[nodiscard]] std::vector<TopoDS_Face> OffsetRectangularFaceGrid();
}
