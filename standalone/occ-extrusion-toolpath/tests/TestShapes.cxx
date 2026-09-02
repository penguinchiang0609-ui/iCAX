#include "TestShapes.hxx"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepLib.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <GeomConvert_CompCurveToBSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>
#include <gp.hxx>
#include <TopoDS.hxx>

#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <numbers>

namespace etp::tests
{
    namespace
    {
        TopoDS_Wire RectangleWire(
            const double minimumX,
            const double maximumX,
            const double minimumZ,
            const double maximumZ)
        {
            BRepBuilderAPI_MakePolygon polygon;
            polygon.Add(gp_Pnt(minimumX, 0.0, minimumZ));
            polygon.Add(gp_Pnt(maximumX, 0.0, minimumZ));
            polygon.Add(gp_Pnt(maximumX, 0.0, maximumZ));
            polygon.Add(gp_Pnt(minimumX, 0.0, maximumZ));
            polygon.Close();
            if (!polygon.IsDone())
                throw std::runtime_error("failed to construct rectangle wire");
            return polygon.Wire();
        }

        TopoDS_Edge BsplineEdge(const gp_Pnt& start, const gp_Pnt& finish)
        {
            TColgp_Array1OfPnt points(1, 5);
            const gp_Vec delta(start, finish);
            for (Standard_Integer index = 1; index <= 5; ++index)
            {
                const auto ratio = static_cast<double>(index - 1) / 4.0;
                points.SetValue(index, start.Translated(delta * ratio));
            }
            const GeomAPI_PointsToBSpline builder(points, 3, 3, GeomAbs_C2, 1.0e-9);
            return BRepBuilderAPI_MakeEdge(builder.Curve());
        }

        occ::handle<Geom_BezierCurve> Bezier(
            const std::initializer_list<gp_Pnt>& points)
        {
            TColgp_Array1OfPnt poles(1, static_cast<Standard_Integer>(points.size()));
            Standard_Integer index = 1;
            for (const auto& point : points)
                poles.SetValue(index++, point);
            return new Geom_BezierCurve(poles);
        }
    }

    std::vector<TopoDS_Wire> RectangularTubeSection()
    {
        return {
            RectangleWire(-20.0, 20.0, -15.0, 15.0),
            RectangleWire(-15.0, 15.0, -10.0, 10.0)
        };
    }

    std::vector<TopoDS_Wire> SolidRectangularSection()
    {
        return { RectangleWire(-20.0, 20.0, -20.0, 20.0) };
    }

    TopoDS_Face TrapezoidFeatureFace()
    {
        BRepBuilderAPI_MakePolygon polygon;
        polygon.Add(gp_Pnt(0.0, -10.0, 15.0));
        polygon.Add(gp_Pnt(0.0, 10.0, 15.0));
        polygon.Add(gp_Pnt(0.0, 8.0, 10.0));
        polygon.Add(gp_Pnt(0.0, -8.0, 10.0));
        polygon.Close();
        return BRepBuilderAPI_MakeFace(polygon.Wire());
    }

    TopoDS_Face TriangleFeatureFace()
    {
        BRepBuilderAPI_MakePolygon polygon;
        polygon.Add(gp_Pnt(0.0, -10.0, 15.0));
        polygon.Add(gp_Pnt(0.0, 10.0, 15.0));
        polygon.Add(gp_Pnt(0.0, 0.0, 12.0));
        polygon.Close();
        return BRepBuilderAPI_MakeFace(polygon.Wire());
    }

    TopoDS_Face NurbsEncodedTrapezoidFeatureFace()
    {
        const gp_Pnt a(0.0, -10.0, 15.0);
        const gp_Pnt b(0.0, 10.0, 15.0);
        const gp_Pnt c(0.0, 8.0, 10.0);
        const gp_Pnt d(0.0, -8.0, 10.0);
        BRepBuilderAPI_MakeWire wire;
        wire.Add(BRepBuilderAPI_MakeEdge(a, b));
        wire.Add(BsplineEdge(b, c));
        wire.Add(BsplineEdge(c, d));
        wire.Add(BRepBuilderAPI_MakeEdge(d, a));
        if (!wire.IsDone())
            throw std::runtime_error("failed to construct NURBS-encoded feature wire");
        return BRepBuilderAPI_MakeFace(wire.Wire());
    }

    TopoDS_Shape RectangularTubeSolid()
    {
        auto wires = RectangularTubeSection();
        auto inner = wires[1];
        inner.Reverse();
        BRepBuilderAPI_MakeFace face(wires[0]);
        face.Add(inner);
        if (!face.IsDone())
            throw std::runtime_error("failed to construct tube section face");
        return BRepPrimAPI_MakePrism(face.Face(), gp_Vec(0.0, 80.0, 0.0)).Shape();
    }

    TopoDS_Shape RotatedRectangularTubeSolid()
    {
        gp_Trsf rotation;
        rotation.SetRotation(
            gp_Ax1(gp::Origin(), gp::DX()), std::numbers::pi / 2.0);
        return BRepBuilderAPI_Transform(
            RectangularTubeSolid(), rotation, true).Shape();
    }

    TopoDS_Wire SegmentedOuterSection()
    {
        BRepBuilderAPI_MakePolygon polygon;
        polygon.Add(gp_Pnt(-20.0, 0.0, -15.0));
        polygon.Add(gp_Pnt(20.0, 0.0, -15.0));
        polygon.Add(gp_Pnt(20.0, 0.0, 15.0));
        polygon.Add(gp_Pnt(0.0, 0.0, 15.0));
        polygon.Add(gp_Pnt(-20.0, 0.0, 15.0));
        polygon.Close();
        return polygon.Wire();
    }

    TopoDS_Face WideTopFace()
    {
        BRepBuilderAPI_MakePolygon polygon;
        polygon.Add(gp_Pnt(-20.0, -40.0, 15.0));
        polygon.Add(gp_Pnt(20.0, -40.0, 15.0));
        polygon.Add(gp_Pnt(20.0, 40.0, 15.0));
        polygon.Add(gp_Pnt(-20.0, 40.0, 15.0));
        polygon.Close();
        return BRepBuilderAPI_MakeFace(polygon.Wire());
    }

    FaceTrajectoryFixture HalfCylinderFixture()
    {
        const occ::handle<Geom_CylindricalSurface> surface =
            new Geom_CylindricalSurface(
                gp_Ax3(gp::Origin(), gp::DY(), gp::DX()), 5.0);
        BRepBuilderAPI_MakeFace maker(
            surface, 0.0, std::numbers::pi, 0.0, 20.0, 1.0e-9);
        if (!maker.IsDone())
            throw std::runtime_error("failed to construct half-cylinder face");

        FaceTrajectoryFixture result;
        result.Face = maker.Face();
        std::optional<TopoDS_Edge> firstCircle;
        std::optional<TopoDS_Edge> firstLine;
        double smallestCircleY = (std::numeric_limits<double>::max)();
        for (BRepTools_WireExplorer explorer(
                 BRepTools::OuterWire(result.Face), result.Face);
             explorer.More(); explorer.Next())
        {
            const auto edge = TopoDS::Edge(explorer.Current());
            BRepAdaptor_Curve curve(edge);
            const auto middle = curve.Value(
                (curve.FirstParameter() + curve.LastParameter()) * 0.5);
            if (curve.GetType() == GeomAbs_Circle && middle.Y() < smallestCircleY)
            {
                smallestCircleY = middle.Y();
                firstCircle = edge;
            }
            if (curve.GetType() == GeomAbs_Line && !firstLine)
                firstLine = edge;
        }
        if (!firstCircle || !firstLine)
            throw std::runtime_error("half-cylinder boundary classification failed");

        result.TransverseTrajectory = BRepBuilderAPI_MakeWire(*firstCircle).Wire();
        result.RulingTrajectory = BRepBuilderAPI_MakeWire(*firstLine).Wire();
        return result;
    }

    FaceTrajectoryFixture PinchedCylinderFixture()
    {
        const occ::handle<Geom_CylindricalSurface> surface =
            new Geom_CylindricalSurface(
                gp_Ax3(gp::Origin(), gp::DY(), gp::DX()), 5.0);
        const auto edgeOnSurface = [&](
            const gp_Pnt2d& first,
            const gp_Pnt2d& last)
        {
            BRepBuilderAPI_MakeEdge maker(
                GCE2d_MakeSegment(first, last).Value(), surface);
            if (!maker.IsDone())
                throw std::runtime_error("failed to construct pinched cylinder edge");
            auto edge = maker.Edge();
            BRepLib::BuildCurve3d(edge);
            return edge;
        };

        // The lower rail and the two-piece upper rail meet at both ends.
        // In UV this is a triangle; on the cylinder it is a no-side ruled Face
        // whose material depth tends to zero at two pinched points.
        const auto lower = edgeOnSurface(
            gp_Pnt2d(0.0, 0.0), gp_Pnt2d(std::numbers::pi, 0.0));
        const auto upperRight = edgeOnSurface(
            gp_Pnt2d(std::numbers::pi, 0.0),
            gp_Pnt2d(std::numbers::pi / 2.0, 6.0));
        const auto upperLeft = edgeOnSurface(
            gp_Pnt2d(std::numbers::pi / 2.0, 6.0), gp_Pnt2d(0.0, 0.0));

        BRepBuilderAPI_MakeWire boundary;
        boundary.Add(lower);
        boundary.Add(upperRight);
        boundary.Add(upperLeft);
        if (!boundary.IsDone())
            throw std::runtime_error("failed to construct pinched cylinder wire");

        BRepBuilderAPI_MakeFace faceMaker(surface, boundary.Wire(), true);
        if (!faceMaker.IsDone())
            throw std::runtime_error("failed to construct pinched cylinder face");

        FaceTrajectoryFixture result;
        result.Face = faceMaker.Face();
        result.TransverseTrajectory = BRepBuilderAPI_MakeWire(lower).Wire();
        return result;
    }

    TopoDS_Face CompositeStadiumSideFace()
    {
        constexpr double halfLength = 12.0;
        constexpr double radius = 5.0;
        constexpr double cubicHalfCircleHandle = 4.0 * radius / 3.0;
        const auto point = [](const double x, const double z)
        { return gp_Pnt(x, 0.0, z); };

        // One closed C1 B-spline intentionally hides four manufacturing spans
        // (line, half-round, line, half-round) inside one geometric curve.
        // SweepFaceDecomposeWork must expose the three internal C2 breaks and
        // materialize four child Faces on the extruded side surface.
        GeomConvert_CompCurveToBSplineCurve composer(
            Bezier({
                point(-halfLength, -radius),
                point(halfLength, -radius) }));
        const bool rightAdded = composer.Add(
            Bezier({
                point(halfLength, -radius),
                point(halfLength + cubicHalfCircleHandle, -radius),
                point(halfLength + cubicHalfCircleHandle, radius),
                point(halfLength, radius) }),
            1.0e-9, true, true, 2);
        const bool topAdded = composer.Add(
            Bezier({
                point(halfLength, radius),
                point(-halfLength, radius) }),
            1.0e-9, true, true, 2);
        const bool leftAdded = composer.Add(
            Bezier({
                point(-halfLength, radius),
                point(-halfLength - cubicHalfCircleHandle, radius),
                point(-halfLength - cubicHalfCircleHandle, -radius),
                point(-halfLength, -radius) }),
            1.0e-9, true, true, 2);
        if (!rightAdded || !topAdded || !leftAdded)
            throw std::runtime_error("failed to compose stadium B-spline");

        const auto curve = composer.BSplineCurve();
        const occ::handle<Geom_SurfaceOfLinearExtrusion> surface =
            new Geom_SurfaceOfLinearExtrusion(curve, gp::DY());
        BRepBuilderAPI_MakeFace maker(
            surface,
            curve->FirstParameter(),
            curve->LastParameter(),
            0.0,
            8.0,
            1.0e-8);
        if (!maker.IsDone())
            throw std::runtime_error("failed to construct composite stadium side face");
        return maker.Face();
    }

    std::vector<TopoDS_Face> IndependentAdjacentFaces()
    {
        // Both rectangles contain the same x=0 geometric boundary, but each
        // wire was built independently. Before Sewing their boundary Edges are
        // intentionally geometrically equal and topologically different.
        BRepBuilderAPI_MakeFace left(RectangleWire(-10.0, 0.0, -5.0, 5.0));
        BRepBuilderAPI_MakeFace right(RectangleWire(0.0, 10.0, -5.0, 5.0));
        if (!left.IsDone() || !right.IsDone())
            throw std::runtime_error("failed to construct adjacent Faces");
        return { left.Face(), right.Face() };
    }

    std::vector<TopoDS_Face> OffsetRectangularFaceGrid()
    {
        const auto makeFace = [](const double minimumX,
                                  const double maximumX,
                                  const double minimumZ,
                                  const double maximumZ)
        {
            BRepBuilderAPI_MakeFace maker(
                RectangleWire(minimumX, maximumX, minimumZ, maximumZ));
            if (!maker.IsDone())
                throw std::runtime_error("failed to construct grid Face");
            return maker.Face();
        };

        // Seven independently built rectangles. The upper and lower vertical
        // divisions do not align, so their common horizontal boundary contains
        // several T-junctions that Sewing must materialize without splitting
        // any already-valid four-segment Face into extra Faces.
        return {
            makeFace(-16.0, -12.0, 0.0, 8.0),
            makeFace(-12.0, 4.0, 0.0, 8.0),
            makeFace(4.0, 12.0, 0.0, 8.0),
            makeFace(12.0, 16.0, 0.0, 8.0),
            makeFace(-16.0, -2.0, -6.0, 0.0),
            makeFace(-2.0, 8.0, -6.0, 0.0),
            makeFace(8.0, 16.0, -6.0, 0.0)
        };
    }
}
