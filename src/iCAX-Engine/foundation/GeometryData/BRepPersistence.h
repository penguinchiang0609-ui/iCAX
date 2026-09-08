#pragma once
#include "GeometryData.h"
#include <bit>
#include <limits>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace iCAX::GeometryData::Persistence
{
// Version 1: explicit field order, little-endian integers and IEEE-754 doubles.
// Never persist compiler padding, pointers or an OCC re-numbering of topology.
// Changing these member lists or variant alternatives requires a new version.
inline auto Members(Point2*) { return std::tuple{&Point2::X, &Point2::Y}; }
inline auto Members(TextureCoordinate2*) { return std::tuple{&TextureCoordinate2::U, &TextureCoordinate2::V}; }
inline auto Members(Point3*) { return std::tuple{&Point3::X, &Point3::Y, &Point3::Z}; }
inline auto Members(Vector2*) { return std::tuple{&Vector2::X, &Vector2::Y}; }
inline auto Members(Vector3*) { return std::tuple{&Vector3::X, &Vector3::Y, &Vector3::Z}; }
inline auto Members(Direction2*) { return std::tuple{&Direction2::X, &Direction2::Y}; }
inline auto Members(Direction3*) { return std::tuple{&Direction3::X, &Direction3::Y, &Direction3::Z}; }
inline auto Members(Matrix3x3*) { return std::tuple{&Matrix3x3::Values}; }
inline auto Members(Matrix4x4*) { return std::tuple{&Matrix4x4::Values}; }
inline auto Members(Transform2*) { return std::tuple{&Transform2::Matrix}; }
inline auto Members(Transform3*) { return std::tuple{&Transform3::Matrix}; }
inline auto Members(Quaternion*) { return std::tuple{&Quaternion::X, &Quaternion::Y, &Quaternion::Z, &Quaternion::W}; }
inline auto Members(Axis2*) { return std::tuple{&Axis2::Location, &Axis2::Direction}; }
inline auto Members(Axis3*) { return std::tuple{&Axis3::Location, &Axis3::Direction}; }
inline auto Members(Placement2*) { return std::tuple{&Placement2::Location, &Placement2::XDirection, &Placement2::YDirection}; }
inline auto Members(Placement3*) { return std::tuple{&Placement3::Location, &Placement3::XDirection, &Placement3::YDirection, &Placement3::ZDirection}; }
inline auto Members(Plane3*) { return std::tuple{&Plane3::Location, &Plane3::Normal, &Plane3::XDirection}; }
inline auto Members(BoundingBox2*) { return std::tuple{&BoundingBox2::Min, &BoundingBox2::Max, &BoundingBox2::Empty}; }
inline auto Members(BoundingBox3*) { return std::tuple{&BoundingBox3::Min, &BoundingBox3::Max, &BoundingBox3::Empty}; }
inline auto Members(OrientedBox2*) { return std::tuple{&OrientedBox2::Placement, &OrientedBox2::HalfWidth, &OrientedBox2::HalfHeight}; }
inline auto Members(OrientedBox3*) { return std::tuple{&OrientedBox3::Placement, &OrientedBox3::HalfWidth, &OrientedBox3::HalfDepth, &OrientedBox3::HalfHeight}; }
inline auto Members(ParameterRange*) { return std::tuple{&ParameterRange::First, &ParameterRange::Last, &ParameterRange::Reversed}; }
inline auto Members(SurfaceParameterRange*) { return std::tuple{&SurfaceParameterRange::UFirst, &SurfaceParameterRange::ULast, &SurfaceParameterRange::VFirst, &SurfaceParameterRange::VLast, &SurfaceParameterRange::UReversed, &SurfaceParameterRange::VReversed}; }
inline auto Members(Line2*) { return std::tuple{&Line2::Axis}; }
inline auto Members(Line3*) { return std::tuple{&Line3::Axis}; }
inline auto Members(Ray2*) { return std::tuple{&Ray2::Axis, &Ray2::First}; }
inline auto Members(Ray3*) { return std::tuple{&Ray3::Axis, &Ray3::First}; }
inline auto Members(Segment2*) { return std::tuple{&Segment2::Start, &Segment2::End}; }
inline auto Members(Segment3*) { return std::tuple{&Segment3::Start, &Segment3::End}; }
inline auto Members(Circle2*) { return std::tuple{&Circle2::Placement, &Circle2::Radius}; }
inline auto Members(Circle3*) { return std::tuple{&Circle3::Placement, &Circle3::Radius}; }
inline auto Members(Arc2*) { return std::tuple{&Arc2::Basis, &Arc2::StartAngle, &Arc2::EndAngle, &Arc2::CounterClockwise}; }
inline auto Members(Arc3*) { return std::tuple{&Arc3::Basis, &Arc3::StartAngle, &Arc3::EndAngle, &Arc3::CounterClockwise}; }
inline auto Members(Ellipse2*) { return std::tuple{&Ellipse2::Placement, &Ellipse2::MajorRadius, &Ellipse2::MinorRadius}; }
inline auto Members(Ellipse3*) { return std::tuple{&Ellipse3::Placement, &Ellipse3::MajorRadius, &Ellipse3::MinorRadius}; }
inline auto Members(EllipseArc2*) { return std::tuple{&EllipseArc2::Basis, &EllipseArc2::StartAngle, &EllipseArc2::EndAngle, &EllipseArc2::CounterClockwise}; }
inline auto Members(EllipseArc3*) { return std::tuple{&EllipseArc3::Basis, &EllipseArc3::StartAngle, &EllipseArc3::EndAngle, &EllipseArc3::CounterClockwise}; }
inline auto Members(Polyline2*) { return std::tuple{&Polyline2::Points, &Polyline2::Closed}; }
inline auto Members(Polyline3*) { return std::tuple{&Polyline3::Points, &Polyline3::Closed}; }
inline auto Members(Bezier2*) { return std::tuple{&Bezier2::Poles}; }
inline auto Members(Bezier3*) { return std::tuple{&Bezier3::Poles}; }
inline auto Members(BSpline2*) { return std::tuple{&BSpline2::Degree, &BSpline2::Poles, &BSpline2::Knots, &BSpline2::Multiplicities, &BSpline2::Periodic}; }
inline auto Members(BSpline3*) { return std::tuple{&BSpline3::Degree, &BSpline3::Poles, &BSpline3::Knots, &BSpline3::Multiplicities, &BSpline3::Periodic}; }
inline auto Members(NURBS2*) { return std::tuple{&NURBS2::Degree, &NURBS2::Poles, &NURBS2::Weights, &NURBS2::Knots, &NURBS2::Multiplicities, &NURBS2::Periodic}; }
inline auto Members(NURBS3*) { return std::tuple{&NURBS3::Degree, &NURBS3::Poles, &NURBS3::Weights, &NURBS3::Knots, &NURBS3::Multiplicities, &NURBS3::Periodic}; }
inline auto Members(Clothoid2*) { return std::tuple{&Clothoid2::Placement, &Clothoid2::StartCurvature, &Clothoid2::EndCurvature, &Clothoid2::Length}; }
inline auto Members(Clothoid3*) { return std::tuple{&Clothoid3::Placement, &Clothoid3::StartCurvature, &Clothoid3::EndCurvature, &Clothoid3::Length}; }
inline auto Members(CurveSegment2*) { return std::tuple{&CurveSegment2::Curve, &CurveSegment2::Range}; }
inline auto Members(CurveSegment3*) { return std::tuple{&CurveSegment3::Curve, &CurveSegment3::Range}; }
inline auto Members(CompositeCurve2*) { return std::tuple{&CompositeCurve2::Segments, &CompositeCurve2::Closed}; }
inline auto Members(CompositeCurve3*) { return std::tuple{&CompositeCurve3::Segments, &CompositeCurve3::Closed}; }
inline auto Members(Loop2*) { return std::tuple{&Loop2::Segments}; }
inline auto Members(Region2*) { return std::tuple{&Region2::OuterLoops, &Region2::InnerLoops}; }
inline auto Members(Rect2*) { return std::tuple{&Rect2::Placement, &Rect2::Width, &Rect2::Height}; }
inline auto Members(Box3*) { return std::tuple{&Box3::Placement, &Box3::Width, &Box3::Depth, &Box3::Height}; }
inline auto Members(PlaneSurface3*) { return std::tuple{&PlaneSurface3::Placement}; }
inline auto Members(CylindricalSurface3*) { return std::tuple{&CylindricalSurface3::Placement, &CylindricalSurface3::Radius}; }
inline auto Members(ConicalSurface3*) { return std::tuple{&ConicalSurface3::Placement, &ConicalSurface3::Radius, &ConicalSurface3::SemiAngle}; }
inline auto Members(SphericalSurface3*) { return std::tuple{&SphericalSurface3::Placement, &SphericalSurface3::Radius}; }
inline auto Members(ToroidalSurface3*) { return std::tuple{&ToroidalSurface3::Placement, &ToroidalSurface3::MajorRadius, &ToroidalSurface3::MinorRadius}; }
inline auto Members(BSplineSurface3*) { return std::tuple{&BSplineSurface3::UDegree, &BSplineSurface3::VDegree, &BSplineSurface3::UCount, &BSplineSurface3::VCount, &BSplineSurface3::Poles, &BSplineSurface3::Weights, &BSplineSurface3::UKnots, &BSplineSurface3::VKnots, &BSplineSurface3::UMultiplicities, &BSplineSurface3::VMultiplicities, &BSplineSurface3::UPeriodic, &BSplineSurface3::VPeriodic}; }
inline auto Members(Triangulation3*) { return std::tuple{&Triangulation3::Vertices, &Triangulation3::Normals, &Triangulation3::TextureCoordinates, &Triangulation3::Triangles, &Triangulation3::TriangleFaceIds}; }
inline auto Members(EntityMetadata*) { return std::tuple{&EntityMetadata::Name, &EntityMetadata::SourceId, &EntityMetadata::Tags}; }
inline auto Members(Curve2Record*) { return std::tuple{&Curve2Record::Id, &Curve2Record::Geometry, &Curve2Record::Metadata}; }
inline auto Members(Curve3Record*) { return std::tuple{&Curve3Record::Id, &Curve3Record::Geometry, &Curve3Record::Metadata}; }
inline auto Members(Surface3Record*) { return std::tuple{&Surface3Record::Id, &Surface3Record::Geometry, &Surface3Record::Metadata}; }
inline auto Members(Triangulation3Record*) { return std::tuple{&Triangulation3Record::Id, &Triangulation3Record::Geometry, &Triangulation3Record::Metadata}; }
inline auto Members(CTriangleMeshResource*) { return std::tuple{&CTriangleMeshResource::Mesh, &CTriangleMeshResource::Metadata}; }
inline auto Members(BRepShapeRef*) { return std::tuple{&BRepShapeRef::Kind, &BRepShapeRef::Id, &BRepShapeRef::Orientation, &BRepShapeRef::Location}; }
inline auto Members(BRepVertex*) { return std::tuple{&BRepVertex::Id, &BRepVertex::Position, &BRepVertex::Tolerance, &BRepVertex::Metadata}; }
inline auto Members(BRepEdge*) { return std::tuple{&BRepEdge::Id, &BRepEdge::Curve3Id, &BRepEdge::StartVertexId, &BRepEdge::EndVertexId, &BRepEdge::Range, &BRepEdge::Tolerance, &BRepEdge::SameParameter, &BRepEdge::SameRange, &BRepEdge::Degenerated, &BRepEdge::Closed, &BRepEdge::Metadata}; }
inline auto Members(BRepCoedge*) { return std::tuple{&BRepCoedge::EdgeId, &BRepCoedge::Curve2Id, &BRepCoedge::StartVertexId, &BRepCoedge::EndVertexId, &BRepCoedge::Range, &BRepCoedge::Orientation}; }
inline auto Members(BRepWire*) { return std::tuple{&BRepWire::Id, &BRepWire::Coedges, &BRepWire::Closed, &BRepWire::Metadata}; }
inline auto Members(BRepFace*) { return std::tuple{&BRepFace::Id, &BRepFace::Surface3Id, &BRepFace::Triangulation3Id, &BRepFace::WireIds, &BRepFace::WireOrientations, &BRepFace::Domain, &BRepFace::Orientation, &BRepFace::Tolerance, &BRepFace::NaturalRestriction, &BRepFace::Metadata}; }
inline auto Members(BRepShell*) { return std::tuple{&BRepShell::Id, &BRepShell::FaceIds, &BRepShell::FaceOrientations, &BRepShell::Closed, &BRepShell::Metadata}; }
inline auto Members(BRepSolid*) { return std::tuple{&BRepSolid::Id, &BRepSolid::ShellIds, &BRepSolid::ShellOrientations, &BRepSolid::Metadata}; }
inline auto Members(BRepCompSolid*) { return std::tuple{&BRepCompSolid::Id, &BRepCompSolid::SolidIds, &BRepCompSolid::Metadata}; }
inline auto Members(BRepCompound*) { return std::tuple{&BRepCompound::Id, &BRepCompound::Children, &BRepCompound::Metadata}; }
inline auto Members(BRepModel*) { return std::tuple{&BRepModel::Curves2, &BRepModel::Curves3, &BRepModel::Surfaces3, &BRepModel::Triangulations3, &BRepModel::Vertices, &BRepModel::Edges, &BRepModel::Wires, &BRepModel::Faces, &BRepModel::Shells, &BRepModel::Solids, &BRepModel::CompSolids, &BRepModel::Compounds, &BRepModel::RootShapes, &BRepModel::RootFaceIds, &BRepModel::RootShellIds, &BRepModel::RootSolidIds, &BRepModel::Metadata}; }

inline constexpr std::size_t MaxBytes = 256u * 1024u * 1024u;
inline constexpr std::array<std::uint8_t, 8> Magic{'I','C','B','R','E','P','0','1'};
inline void Check(bool valid) { if (!valid) throw std::runtime_error("Invalid or oversized BRep persistence payload"); }

class Writer
{
public:
    std::vector<std::uint8_t> Bytes;
    void Byte(std::uint8_t value) { Check(Bytes.size() < MaxBytes); Bytes.push_back(value); }
    template<class T> requires std::is_integral_v<T>
    void Write(T value) {
        if constexpr (std::is_same_v<T, bool>) Byte(value ? 1 : 0);
        else {
            using U = std::make_unsigned_t<T>;
            U bits = std::bit_cast<U>(value);
            for (std::size_t i = 0; i < sizeof(T); ++i) { Byte(static_cast<std::uint8_t>(bits & 255)); bits >>= 8; }
        }
    }
    void Write(double value) { static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559); Write(std::bit_cast<std::uint64_t>(value)); }
    template<class T> requires std::is_enum_v<T>
    void Write(T value) { Write(static_cast<std::underlying_type_t<T>>(value)); }
    void Count(std::size_t size) { Check(size <= MaxBytes); Write(static_cast<std::uint32_t>(size)); }
    void Write(const std::string& value) { Count(value.size()); for (unsigned char c : value) Byte(c); }
    template<class T> void Write(const std::vector<T>& value) { Count(value.size()); for (const auto& item : value) Write(item); }
    template<class T, std::size_t N> void Write(const std::array<T,N>& value) { for (const auto& item : value) Write(item); }
    template<class... T> void Write(const std::variant<T...>& value) {
        Check(!value.valueless_by_exception());
        Write(static_cast<std::uint32_t>(value.index()));
        std::visit([this](const auto& item) { Write(item); }, value);
    }
    template<class T> requires requires { Members(static_cast<T*>(nullptr)); }
    void Write(const T& value) {
        std::apply([&](auto... member) { (Write(value.*member), ...); }, Members(static_cast<T*>(nullptr)));
    }
};

class Reader
{
    std::span<const std::uint8_t> Bytes;
    std::size_t Position = 0;
    std::size_t Allocated = 0;
    void Allocate(std::size_t count, std::size_t element) {
        Check(element > 0 && count <= (MaxBytes - Allocated) / element);
        Allocated += count * element;
    }
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : Bytes(bytes) { Check(bytes.size() <= MaxBytes); }
    std::uint8_t Byte() { Check(Position < Bytes.size()); return Bytes[Position++]; }
    bool End() const { return Position == Bytes.size(); }
    template<class T> requires std::is_integral_v<T>
    void Read(T& value) {
        if constexpr (std::is_same_v<T, bool>) { const auto b = Byte(); Check(b <= 1); value = b != 0; }
        else {
            using U = std::make_unsigned_t<T>;
            U bits = 0;
            for (std::size_t i = 0; i < sizeof(T); ++i) bits |= static_cast<U>(static_cast<std::uint64_t>(Byte()) << (8 * i));
            value = std::bit_cast<T>(bits);
        }
    }
    void Read(double& value) { std::uint64_t bits; Read(bits); value = std::bit_cast<double>(bits); }
    template<class T> requires std::is_enum_v<T>
    void Read(T& value) {
        std::underlying_type_t<T> raw; Read(raw);
        if constexpr (std::is_same_v<T, ETopologyOrientation>) Check(raw <= 3);
        else if constexpr (std::is_same_v<T, EBRepShapeKind>) Check(raw <= 7);
        else if constexpr (std::is_same_v<T, ECurveKind>) Check(raw <= 11);
        else if constexpr (std::is_same_v<T, ESurfaceKind>) Check(raw <= 5);
        value = static_cast<T>(raw);
    }
    std::uint32_t Count() { std::uint32_t count; Read(count); Check(count <= Bytes.size() - Position); return count; }
    void Read(std::string& value) {
        const auto count = Count(); Allocate(count, 1);
        value.assign(reinterpret_cast<const char*>(Bytes.data() + Position), count); Position += count;
    }
    template<class T> void Read(std::vector<T>& value) {
        const auto count = Count(); Allocate(count, sizeof(T));
        value.resize(count); for (auto& item : value) Read(item);
    }
    template<class T, std::size_t N> void Read(std::array<T,N>& value) { for (auto& item : value) Read(item); }
    template<std::size_t I = 0, class... T> void Alternative(std::uint32_t index, std::variant<T...>& value) {
        if constexpr (I < sizeof...(T)) {
            if (index == I) { value.template emplace<I>(); Read(std::get<I>(value)); }
            else Alternative<I + 1>(index, value);
        } else Check(false);
    }
    template<class... T> void Read(std::variant<T...>& value) { std::uint32_t index; Read(index); Alternative(index, value); }
    template<class T> requires requires { Members(static_cast<T*>(nullptr)); }
    void Read(T& value) {
        std::apply([&](auto... member) { (Read(value.*member), ...); }, Members(static_cast<T*>(nullptr)));
    }
};

inline std::vector<std::uint8_t> Serialize(const BRepModel& model)
{
    Writer writer;
    writer.Write(Magic);
    writer.Write(model);
    return std::move(writer.Bytes);
}
inline BRepModel Deserialize(std::span<const std::uint8_t> bytes)
{
    Reader reader(bytes);
    std::array<std::uint8_t, 8> magic;
    reader.Read(magic); Check(magic == Magic);
    BRepModel model;
    reader.Read(model); Check(reader.End());
    return model;
}
}
