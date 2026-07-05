#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace iCAX::GeometryData
{
    enum class ECurveKind : std::uint8_t
    {
        Line,
        Ray,
        Segment,
        Circle,
        Arc,
        Ellipse,
        EllipseArc,
        Polyline,
        Bezier,
        BSpline,
        NURBS,
        Clothoid
    };

    enum class ESurfaceKind : std::uint8_t
    {
        Plane,
        Cylinder,
        Cone,
        Sphere,
        Torus,
        BSpline
    };

    enum class ETopologyOrientation : std::uint8_t
    {
        Forward,
        Reversed,
        Internal,
        External
    };

    enum class EBRepShapeKind : std::uint8_t
    {
        Vertex,
        Edge,
        Wire,
        Face,
        Shell,
        Solid,
        CompSolid,
        Compound
    };

    struct Point2
    {
        double X = 0.0;
        double Y = 0.0;
    };

    struct TextureCoordinate2
    {
        double U = 0.0;
        double V = 0.0;
    };

    struct Point3
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
    };

    struct Vector2
    {
        double X = 0.0;
        double Y = 0.0;
    };

    struct Vector3
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
    };

    struct Direction2
    {
        double X = 1.0;
        double Y = 0.0;
    };

    struct Direction3
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 1.0;
    };

    struct Matrix3x3
    {
        std::array<std::array<double, 3>, 3> Values = { {
            { 1.0, 0.0, 0.0 },
            { 0.0, 1.0, 0.0 },
            { 0.0, 0.0, 1.0 }
        } };
    };

    struct Matrix4x4
    {
        std::array<std::array<double, 4>, 4> Values = { {
            { 1.0, 0.0, 0.0, 0.0 },
            { 0.0, 1.0, 0.0, 0.0 },
            { 0.0, 0.0, 1.0, 0.0 },
            { 0.0, 0.0, 0.0, 1.0 }
        } };
    };

    struct Transform2
    {
        Matrix3x3 Matrix;
    };

    struct Transform3
    {
        Matrix4x4 Matrix;
    };

    struct Quaternion
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
        double W = 1.0;
    };

    struct Axis2
    {
        Point2 Location;
        Direction2 Direction;
    };

    struct Axis3
    {
        Point3 Location;
        Direction3 Direction;
    };

    // 二维局部坐标系。调用方负责保证两个方向向量正交且单位化。
    struct Placement2
    {
        Point2 Location;
        Direction2 XDirection;
        Direction2 YDirection = { 0.0, 1.0 };
    };

    // 三维局部坐标系。这里保存完整三轴，避免数据层通过方法推导缺失方向。
    struct Placement3
    {
        Point3 Location;
        Direction3 XDirection = { 1.0, 0.0, 0.0 };
        Direction3 YDirection = { 0.0, 1.0, 0.0 };
        Direction3 ZDirection = { 0.0, 0.0, 1.0 };
    };

    struct Plane3
    {
        Point3 Location;
        Direction3 Normal;
        Direction3 XDirection = { 1.0, 0.0, 0.0 };
    };

    struct BoundingBox2
    {
        Point2 Min;
        Point2 Max;
        bool Empty = true;
    };

    struct BoundingBox3
    {
        Point3 Min;
        Point3 Max;
        bool Empty = true;
    };

    struct OrientedBox2
    {
        Placement2 Placement;
        double HalfWidth = 0.0;
        double HalfHeight = 0.0;
    };

    struct OrientedBox3
    {
        Placement3 Placement;
        double HalfWidth = 0.0;
        double HalfDepth = 0.0;
        double HalfHeight = 0.0;
    };

    struct ParameterRange
    {
        double First = 0.0;
        double Last = 1.0;
        bool Reversed = false;
    };

    struct SurfaceParameterRange
    {
        double UFirst = 0.0;
        double ULast = 1.0;
        double VFirst = 0.0;
        double VLast = 1.0;
        bool UReversed = false;
        bool VReversed = false;
    };

    struct Line2
    {
        Axis2 Axis;
    };

    struct Line3
    {
        Axis3 Axis;
    };

    struct Ray2
    {
        Axis2 Axis;
        double First = 0.0;
    };

    struct Ray3
    {
        Axis3 Axis;
        double First = 0.0;
    };

    struct Segment2
    {
        Point2 Start;
        Point2 End;
    };

    struct Segment3
    {
        Point3 Start;
        Point3 End;
    };

    struct Circle2
    {
        Placement2 Placement;
        double Radius = 0.0;
    };

    struct Circle3
    {
        Placement3 Placement;
        double Radius = 0.0;
    };

    struct Arc2
    {
        Circle2 Basis;
        double StartAngle = 0.0;
        double EndAngle = 0.0;
        bool CounterClockwise = true;
    };

    struct Arc3
    {
        Circle3 Basis;
        double StartAngle = 0.0;
        double EndAngle = 0.0;
        bool CounterClockwise = true;
    };

    struct Ellipse2
    {
        Placement2 Placement;
        double MajorRadius = 0.0;
        double MinorRadius = 0.0;
    };

    struct Ellipse3
    {
        Placement3 Placement;
        double MajorRadius = 0.0;
        double MinorRadius = 0.0;
    };

    struct EllipseArc2
    {
        Ellipse2 Basis;
        double StartAngle = 0.0;
        double EndAngle = 0.0;
        bool CounterClockwise = true;
    };

    struct EllipseArc3
    {
        Ellipse3 Basis;
        double StartAngle = 0.0;
        double EndAngle = 0.0;
        bool CounterClockwise = true;
    };

    struct Polyline2
    {
        std::vector<Point2> Points;
        bool Closed = false;
    };

    struct Polyline3
    {
        std::vector<Point3> Points;
        bool Closed = false;
    };

    struct Bezier2
    {
        std::vector<Point2> Poles;
    };

    struct Bezier3
    {
        std::vector<Point3> Poles;
    };

    // 采用 OCC 风格的 pole/knot/multiplicity 数据表达，数据层不负责合法性推导。
    struct BSpline2
    {
        std::int32_t Degree = 3;
        std::vector<Point2> Poles;
        std::vector<double> Knots;
        std::vector<std::int32_t> Multiplicities;
        bool Periodic = false;
    };

    struct BSpline3
    {
        std::int32_t Degree = 3;
        std::vector<Point3> Poles;
        std::vector<double> Knots;
        std::vector<std::int32_t> Multiplicities;
        bool Periodic = false;
    };

    struct NURBS2
    {
        std::int32_t Degree = 3;
        std::vector<Point2> Poles;
        std::vector<double> Weights;
        std::vector<double> Knots;
        std::vector<std::int32_t> Multiplicities;
        bool Periodic = false;
    };

    struct NURBS3
    {
        std::int32_t Degree = 3;
        std::vector<Point3> Poles;
        std::vector<double> Weights;
        std::vector<double> Knots;
        std::vector<std::int32_t> Multiplicities;
        bool Periodic = false;
    };

    struct Clothoid2
    {
        Placement2 Placement;
        double StartCurvature = 0.0;
        double EndCurvature = 0.0;
        double Length = 0.0;
    };

    struct Clothoid3
    {
        Placement3 Placement;
        double StartCurvature = 0.0;
        double EndCurvature = 0.0;
        double Length = 0.0;
    };

    using Curve2 = std::variant<
        Line2,
        Ray2,
        Segment2,
        Circle2,
        Arc2,
        Ellipse2,
        EllipseArc2,
        Polyline2,
        Bezier2,
        BSpline2,
        NURBS2,
        Clothoid2>;

    using Curve3 = std::variant<
        Line3,
        Ray3,
        Segment3,
        Circle3,
        Arc3,
        Ellipse3,
        EllipseArc3,
        Polyline3,
        Bezier3,
        BSpline3,
        NURBS3,
        Clothoid3>;

    struct CurveSegment2
    {
        Curve2 Curve;
        ParameterRange Range;
    };

    struct CurveSegment3
    {
        Curve3 Curve;
        ParameterRange Range;
    };

    struct CompositeCurve2
    {
        std::vector<CurveSegment2> Segments;
        bool Closed = false;
    };

    struct CompositeCurve3
    {
        std::vector<CurveSegment3> Segments;
        bool Closed = false;
    };

    struct Loop2
    {
        std::vector<CurveSegment2> Segments;
    };

    struct Region2
    {
        std::vector<Loop2> OuterLoops;
        std::vector<Loop2> InnerLoops;
    };

    struct Rect2
    {
        Placement2 Placement;
        double Width = 0.0;
        double Height = 0.0;
    };

    struct Box3
    {
        Placement3 Placement;
        double Width = 0.0;
        double Depth = 0.0;
        double Height = 0.0;
    };

    struct PlaneSurface3
    {
        Placement3 Placement;
    };

    struct CylindricalSurface3
    {
        Placement3 Placement;
        double Radius = 0.0;
    };

    struct ConicalSurface3
    {
        Placement3 Placement;
        double Radius = 0.0;
        double SemiAngle = 0.0;
    };

    struct SphericalSurface3
    {
        Placement3 Placement;
        double Radius = 0.0;
    };

    struct ToroidalSurface3
    {
        Placement3 Placement;
        double MajorRadius = 0.0;
        double MinorRadius = 0.0;
    };

    struct BSplineSurface3
    {
        std::int32_t UDegree = 3;
        std::int32_t VDegree = 3;
        std::uint32_t UCount = 0;
        std::uint32_t VCount = 0;
        std::vector<Point3> Poles;
        std::vector<double> Weights;
        std::vector<double> UKnots;
        std::vector<double> VKnots;
        std::vector<std::int32_t> UMultiplicities;
        std::vector<std::int32_t> VMultiplicities;
        bool UPeriodic = false;
        bool VPeriodic = false;
    };

    using Surface3 = std::variant<
        PlaneSurface3,
        CylindricalSurface3,
        ConicalSurface3,
        SphericalSurface3,
        ToroidalSurface3,
        BSplineSurface3>;

    struct Triangulation3
    {
        std::vector<Point3> Vertices;
        std::vector<Vector3> Normals;
        std::vector<TextureCoordinate2> TextureCoordinates;
        std::vector<std::array<std::uint32_t, 3>> Triangles;
        std::vector<std::uint64_t> TriangleFaceIds;
    };

    struct EntityMetadata
    {
        std::string Name;
        std::string SourceId;
        std::vector<std::string> Tags;
    };

    struct Curve2Record
    {
        std::uint64_t Id = 0;
        Curve2 Geometry;
        EntityMetadata Metadata;
    };

    struct Curve3Record
    {
        std::uint64_t Id = 0;
        Curve3 Geometry;
        EntityMetadata Metadata;
    };

    struct Surface3Record
    {
        std::uint64_t Id = 0;
        Surface3 Geometry;
        EntityMetadata Metadata;
    };

    struct Triangulation3Record
    {
        std::uint64_t Id = 0;
        Triangulation3 Geometry;
        EntityMetadata Metadata;
    };

    struct BRepShapeRef
    {
        EBRepShapeKind Kind = EBRepShapeKind::Face;
        std::uint64_t Id = 0;
        ETopologyOrientation Orientation = ETopologyOrientation::Forward;
        Transform3 Location;
    };

    // 中立 BRep 数据结构：几何和拓扑分离，Adapter 负责和 OCC/CGAL 等内核互转。
    struct BRepVertex
    {
        std::uint64_t Id = 0;
        Point3 Position;
        double Tolerance = 0.0;
        EntityMetadata Metadata;
    };

    struct BRepEdge
    {
        std::uint64_t Id = 0;
        // 退化边可以没有三维曲线，但仍应通过 Coedge 的 Curve2Id 在面参数域中表达。
        std::uint64_t Curve3Id = 0;
        std::uint64_t StartVertexId = 0;
        std::uint64_t EndVertexId = 0;
        ParameterRange Range;
        double Tolerance = 0.0;
        bool SameParameter = true;
        bool SameRange = true;
        bool Degenerated = false;
        bool Closed = false;
        EntityMetadata Metadata;
    };

    struct BRepCoedge
    {
        std::uint64_t EdgeId = 0;
        std::uint64_t Curve2Id = 0;
        std::uint64_t StartVertexId = 0;
        std::uint64_t EndVertexId = 0;
        ParameterRange Range;
        ETopologyOrientation Orientation = ETopologyOrientation::Forward;
    };

    struct BRepWire
    {
        std::uint64_t Id = 0;
        std::vector<BRepCoedge> Coedges;
        bool Closed = true;
        EntityMetadata Metadata;
    };

    struct BRepFace
    {
        std::uint64_t Id = 0;
        std::uint64_t Surface3Id = 0;
        std::uint64_t Triangulation3Id = 0;
        std::vector<std::uint64_t> WireIds;
        SurfaceParameterRange Domain;
        ETopologyOrientation Orientation = ETopologyOrientation::Forward;
        double Tolerance = 0.0;
        bool NaturalRestriction = false;
        EntityMetadata Metadata;
    };

    struct BRepShell
    {
        std::uint64_t Id = 0;
        std::vector<std::uint64_t> FaceIds;
        bool Closed = false;
        EntityMetadata Metadata;
    };

    struct BRepSolid
    {
        std::uint64_t Id = 0;
        std::vector<std::uint64_t> ShellIds;
        EntityMetadata Metadata;
    };

    struct BRepCompSolid
    {
        std::uint64_t Id = 0;
        std::vector<std::uint64_t> SolidIds;
        EntityMetadata Metadata;
    };

    struct BRepCompound
    {
        std::uint64_t Id = 0;
        std::vector<BRepShapeRef> Children;
        EntityMetadata Metadata;
    };

    struct BRepModel
    {
        std::vector<Curve2Record> Curves2;
        std::vector<Curve3Record> Curves3;
        std::vector<Surface3Record> Surfaces3;
        std::vector<Triangulation3Record> Triangulations3;
        std::vector<BRepVertex> Vertices;
        std::vector<BRepEdge> Edges;
        std::vector<BRepWire> Wires;
        std::vector<BRepFace> Faces;
        std::vector<BRepShell> Shells;
        std::vector<BRepSolid> Solids;
        std::vector<BRepCompSolid> CompSolids;
        std::vector<BRepCompound> Compounds;
        std::vector<BRepShapeRef> RootShapes;
        std::vector<std::uint64_t> RootFaceIds;
        std::vector<std::uint64_t> RootShellIds;
        std::vector<std::uint64_t> RootSolidIds;
        EntityMetadata Metadata;
    };
}
