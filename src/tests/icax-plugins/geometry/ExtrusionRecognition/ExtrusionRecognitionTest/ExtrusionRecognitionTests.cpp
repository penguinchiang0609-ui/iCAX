#include "pch.h"

#include <gtest/gtest.h>

#include <ExtrusionRecognition/ExtrudeRecognizesService.h>
#include <ExtrusionRecognition/ExtrusionRecognitionService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h>
#include <OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h>
#include <TemplateRuntime/PythonTemplateHost.h>
#include <TemplateRuntime/TemplateCodec.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <BRepPrimAPI_MakePrism.hxx>

#include <STEPControl_Reader.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <algorithm>
#include <array>
#include <limits>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    using namespace iCAX::Data;
    using namespace iCAX::ExtrusionRecognition;
    using namespace iCAX::GeometryData;

    Variant Path(const std::string& Path_)
    {
        return ObjectMap{ { "path", Path_ } };
    }

    Variant Op(const std::string& Name_, VariantArray Arguments_)
    {
        return ObjectMap{
            { "op", Name_ },
            { "args", std::move(Arguments_) }
        };
    }

    void AddLoopWalls(
        Triangulation3& Mesh_,
        const std::array<Point2, 4>& Loop_,
        double dLength_)
    {
        for (std::size_t _Index = 0; _Index < Loop_.size(); ++_Index)
        {
            const auto& _A = Loop_[_Index];
            const auto& _B = Loop_[(_Index + 1) % Loop_.size()];
            const auto _Base = static_cast<std::uint32_t>(Mesh_.Vertices.size());
            Mesh_.Vertices.insert(Mesh_.Vertices.end(), {
                { _A.X, _A.Y, 0.0 },
                { _B.X, _B.Y, 0.0 },
                { _B.X, _B.Y, dLength_ },
                { _A.X, _A.Y, dLength_ }
            });
            Mesh_.Triangles.push_back({ _Base, _Base + 1, _Base + 2 });
            Mesh_.Triangles.push_back({ _Base, _Base + 2, _Base + 3 });
        }
    }

    void AddTaperedLoopWalls(
        Triangulation3& Mesh_,
        const std::array<Point2, 4>& First_,
        const std::array<Point2, 4>& Last_,
        double dLength_)
    {
        for (std::size_t _Index = 0; _Index < First_.size(); ++_Index)
        {
            const auto _Next = (_Index + 1) % First_.size();
            const auto _Base = static_cast<std::uint32_t>(Mesh_.Vertices.size());
            Mesh_.Vertices.insert(Mesh_.Vertices.end(), {
                { First_[_Index].X, First_[_Index].Y, 0.0 },
                { First_[_Next].X, First_[_Next].Y, 0.0 },
                { Last_[_Next].X, Last_[_Next].Y, dLength_ },
                { Last_[_Index].X, Last_[_Index].Y, dLength_ }
            });
            Mesh_.Triangles.push_back({ _Base, _Base + 1, _Base + 2 });
            Mesh_.Triangles.push_back({ _Base, _Base + 2, _Base + 3 });
        }
    }

    BRepModel MakeRectangularTube(double dLength_ = 240.0)
    {
        Triangulation3Record _Record;
        _Record.Id = 10;
        AddLoopWalls(_Record.Geometry, {
            Point2{ -50.0, -25.0 },
            Point2{  50.0, -25.0 },
            Point2{  50.0,  25.0 },
            Point2{ -50.0,  25.0 }
        }, dLength_);
        AddLoopWalls(_Record.Geometry, {
            Point2{ -44.0, -19.0 },
            Point2{ -44.0,  19.0 },
            Point2{  44.0,  19.0 },
            Point2{  44.0, -19.0 }
        }, dLength_);

        BRepModel _Model;
        _Model.Triangulations3.push_back(std::move(_Record));
        _Model.Solids.push_back(BRepSolid{ 1 });
        _Model.RootSolidIds.push_back(1);
        return _Model;
    }

    BRepModel MakeStraightEdgeEvidenceModel()
    {
        Curve3Record _Curve;
        _Curve.Id = 1;
        Line3 _Line;
        _Line.Axis.Direction = { 3.0, 4.0, 0.0 };
        _Curve.Geometry = _Line;

        BRepModel _Model;
        _Model.Curves3.push_back(std::move(_Curve));
        for (std::uint64_t _Index = 1; _Index <= 3; ++_Index)
        {
            BRepEdge _Edge;
            _Edge.Id = _Index;
            _Edge.Curve3Id = 1;
            _Edge.Range = { 0.0, 10.0 };
            _Model.Edges.push_back(_Edge);
        }
        return _Model;
    }

    // Boundary evidence owned by an X-parallel face. This fixture isolates
    // projection topology from solid construction and source-edge orientation.
    BRepModel MakeProjectedBoundaryBRep(
        const std::vector<std::pair<Curve3, ParameterRange>>& Edges_)
    {
        BRepModel _Model;
        PlaneSurface3 _Surface;
        _Surface.Placement.ZDirection = { 0.0, 1.0, 0.0 };
        _Model.Surfaces3.push_back({ 1, _Surface });
        BRepWire _Wire;
        _Wire.Id = 1;
        for (const auto& [_Curve, _Range] : Edges_)
        {
            const auto _ID = static_cast<std::uint64_t>(_Model.Edges.size() + 1);
            _Model.Curves3.push_back({ _ID, _Curve });
            BRepEdge _Edge;
            _Edge.Id = _ID;
            _Edge.Curve3Id = _ID;
            _Edge.Range = _Range;
            _Model.Edges.push_back(_Edge);
            BRepCoedge _Coedge;
            _Coedge.EdgeId = _ID;
            _Wire.Coedges.push_back(_Coedge);
        }
        _Model.Wires.push_back(_Wire);
        BRepFace _Face;
        _Face.Id = 1;
        _Face.Surface3Id = 1;
        _Face.WireIds = { 1 };
        _Model.Faces.push_back(_Face);
        return _Model;
    }

    std::pair<Curve3, ParameterRange> ProjectedSegment(const Point2& A_, const Point2& B_)
    {
        return { Segment3{ { 0.0, A_.X, A_.Y }, { 0.0, B_.X, B_.Y } }, { 0.0, 1.0 } };
    }

    std::pair<Curve3, ParameterRange> ProjectedConic(
        double First_, double Last_, bool bEllipse_ = false, double Phase_ = 0.0)
    {
        Placement3 _Placement;
        _Placement.XDirection = { 0.0, std::cos(Phase_), std::sin(Phase_) };
        _Placement.YDirection = { 0.0, -std::sin(Phase_), std::cos(Phase_) };
        _Placement.ZDirection = { 1.0, 0.0, 0.0 };
        return { bEllipse_ ? Curve3(Ellipse3{ _Placement, 20.0, 10.0 })
            : Curve3(Circle3{ _Placement, 10.0 }), { First_, Last_ } };
    }

    BRepModel MakeRectangularSideFaceBRep(bool bAddInteriorEndpointFace_ = false)
    {
        BRepModel _Model;
        std::uint64_t _NextVertexID = 1;
        std::uint64_t _NextCurveID = 1;
        std::uint64_t _NextEdgeID = 1;
        std::uint64_t _NextWireID = 1;
        std::uint64_t _NextSurfaceID = 1;
        std::uint64_t _NextFaceID = 1;

        const auto _FindOrAddVertex = [&](const Point3& Point_)
        {
            const auto _Iter = std::find_if(
                _Model.Vertices.begin(),
                _Model.Vertices.end(),
                [&Point_](const auto& Vertex_) {
                    return std::abs(Vertex_.Position.X - Point_.X) <= 1.0e-12
                        && std::abs(Vertex_.Position.Y - Point_.Y) <= 1.0e-12
                        && std::abs(Vertex_.Position.Z - Point_.Z) <= 1.0e-12;
                });
            if (_Iter != _Model.Vertices.end())
            {
                return _Iter->Id;
            }
            BRepVertex _Vertex;
            _Vertex.Id = _NextVertexID++;
            _Vertex.Position = Point_;
            _Model.Vertices.push_back(_Vertex);
            return _Vertex.Id;
        };

        const auto _AddEdge = [&](const Point3& Start_, const Point3& End_)
        {
            const auto _StartID = _FindOrAddVertex(Start_);
            const auto _EndID = _FindOrAddVertex(End_);
            const auto _Delta = Vector3{
                End_.X - Start_.X,
                End_.Y - Start_.Y,
                End_.Z - Start_.Z };
            const auto _Length = std::sqrt(
                _Delta.X * _Delta.X + _Delta.Y * _Delta.Y + _Delta.Z * _Delta.Z);
            Line3 _Line;
            _Line.Axis.Location = Start_;
            _Line.Axis.Direction = {
                _Delta.X / _Length,
                _Delta.Y / _Length,
                _Delta.Z / _Length };
            Curve3Record _Curve;
            _Curve.Id = _NextCurveID++;
            _Curve.Geometry = _Line;
            _Model.Curves3.push_back(_Curve);

            BRepEdge _Edge;
            _Edge.Id = _NextEdgeID++;
            _Edge.Curve3Id = _Curve.Id;
            _Edge.StartVertexId = _StartID;
            _Edge.EndVertexId = _EndID;
            _Edge.Range = { 0.0, _Length };
            _Model.Edges.push_back(_Edge);
            return std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>{
                _Edge.Id, _StartID, _EndID };
        };

        const auto _AddSideFace = [&](const std::array<Point3, 4>& Points_, const Direction3& Normal_)
        {
            BRepWire _Wire;
            _Wire.Id = _NextWireID++;
            for (std::size_t _Index = 0; _Index < Points_.size(); ++_Index)
            {
                const auto [_EdgeID, _StartID, _EndID] = _AddEdge(
                    Points_[_Index],
                    Points_[(_Index + 1) % Points_.size()]);
                BRepCoedge _Coedge;
                _Coedge.EdgeId = _EdgeID;
                _Coedge.StartVertexId = _StartID;
                _Coedge.EndVertexId = _EndID;
                _Wire.Coedges.push_back(_Coedge);
            }
            _Model.Wires.push_back(_Wire);

            PlaneSurface3 _Plane;
            _Plane.Placement.Location = Points_.front();
            _Plane.Placement.ZDirection = Normal_;
            Surface3Record _Surface;
            _Surface.Id = _NextSurfaceID++;
            _Surface.Geometry = _Plane;
            _Model.Surfaces3.push_back(_Surface);

            BRepFace _Face;
            _Face.Id = _NextFaceID++;
            _Face.Surface3Id = _Surface.Id;
            _Face.WireIds.push_back(_Wire.Id);
            _Model.Faces.push_back(_Face);
        };

        constexpr double _Length = 100.0;
        constexpr double _HalfWidth = 50.0;
        constexpr double _HalfHeight = 25.0;
        _AddSideFace({
            Point3{ 0.0, -_HalfWidth, -_HalfHeight },
            Point3{ _Length, -_HalfWidth, -_HalfHeight },
            Point3{ _Length, -_HalfWidth, _HalfHeight },
            Point3{ 0.0, -_HalfWidth, _HalfHeight } },
            { 0.0, -1.0, 0.0 });
        _AddSideFace({
            Point3{ 0.0, _HalfWidth, -_HalfHeight },
            Point3{ 0.0, _HalfWidth, _HalfHeight },
            Point3{ _Length, _HalfWidth, _HalfHeight },
            Point3{ _Length, _HalfWidth, -_HalfHeight } },
            { 0.0, 1.0, 0.0 });
        _AddSideFace({
            Point3{ 0.0, -_HalfWidth, -_HalfHeight },
            Point3{ 0.0, _HalfWidth, -_HalfHeight },
            Point3{ _Length, _HalfWidth, -_HalfHeight },
            Point3{ _Length, -_HalfWidth, -_HalfHeight } },
            { 0.0, 0.0, -1.0 });
        _AddSideFace({
            Point3{ 0.0, -_HalfWidth, _HalfHeight },
            Point3{ _Length, -_HalfWidth, _HalfHeight },
            Point3{ _Length, _HalfWidth, _HalfHeight },
            Point3{ 0.0, _HalfWidth, _HalfHeight } },
            { 0.0, 0.0, 1.0 });
        if (bAddInteriorEndpointFace_)
        {
            // The first point is in the interior of the long bottom edge of
            // the outer rectangle. The other endpoint is a dirty degree-one
            // branch, so this exercises both endpoint-on-edge splitting and
            // iterative degree-one pruning.
            _AddSideFace({
                Point3{ 0.0, 0.0, -_HalfHeight },
                Point3{ _Length, 0.0, -_HalfHeight },
                Point3{ _Length, 0.0, -10.0 },
                Point3{ 0.0, 0.0, -10.0 } },
                { 0.0, -1.0, 0.0 });
        }
        return _Model;
    }

    BRepModel MakeTubeWithInternalConnectionsBRep(bool bConcaveOuter_ = false)
    {
        BRepModel _Model;
        std::uint64_t _NextVertexID = 1;
        std::uint64_t _NextCurveID = 1;
        std::uint64_t _NextEdgeID = 1;
        std::uint64_t _NextWireID = 1;
        std::uint64_t _NextSurfaceID = 1;
        std::uint64_t _NextFaceID = 1;

        const auto _FindOrAddVertex = [&](const Point3& Point_)
        {
            const auto _Iter = std::find_if(
                _Model.Vertices.begin(),
                _Model.Vertices.end(),
                [&Point_](const auto& Vertex_) {
                    return std::abs(Vertex_.Position.X - Point_.X) <= 1.0e-12
                        && std::abs(Vertex_.Position.Y - Point_.Y) <= 1.0e-12
                        && std::abs(Vertex_.Position.Z - Point_.Z) <= 1.0e-12;
                });
            if (_Iter != _Model.Vertices.end())
            {
                return _Iter->Id;
            }
            BRepVertex _Vertex;
            _Vertex.Id = _NextVertexID++;
            _Vertex.Position = Point_;
            _Model.Vertices.push_back(_Vertex);
            return _Vertex.Id;
        };

        const auto _AddEdge = [&](const Point3& Start_, const Point3& End_)
        {
            const auto _StartID = _FindOrAddVertex(Start_);
            const auto _EndID = _FindOrAddVertex(End_);
            const auto _Delta = Vector3{
                End_.X - Start_.X,
                End_.Y - Start_.Y,
                End_.Z - Start_.Z };
            const auto _Length = std::sqrt(
                _Delta.X * _Delta.X
                + _Delta.Y * _Delta.Y
                + _Delta.Z * _Delta.Z);
            Line3 _Line;
            _Line.Axis.Location = Start_;
            _Line.Axis.Direction = {
                _Delta.X / _Length,
                _Delta.Y / _Length,
                _Delta.Z / _Length };
            Curve3Record _Curve;
            _Curve.Id = _NextCurveID++;
            _Curve.Geometry = _Line;
            _Model.Curves3.push_back(_Curve);

            BRepEdge _Edge;
            _Edge.Id = _NextEdgeID++;
            _Edge.Curve3Id = _Curve.Id;
            _Edge.StartVertexId = _StartID;
            _Edge.EndVertexId = _EndID;
            _Edge.Range = { 0.0, _Length };
            _Model.Edges.push_back(_Edge);
            return std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>{
                _Edge.Id, _StartID, _EndID };
        };

        const auto _AddProfileEdgeFace = [&](const Point2& Start_, const Point2& End_)
        {
            constexpr double _Length = 100.0;
            const Point3 _P0{ 0.0, Start_.X, Start_.Y };
            const Point3 _P1{ _Length, Start_.X, Start_.Y };
            const Point3 _P2{ _Length, End_.X, End_.Y };
            const Point3 _P3{ 0.0, End_.X, End_.Y };
            const std::array<Point3, 4> _Points{ _P0, _P1, _P2, _P3 };

            BRepWire _Wire;
            _Wire.Id = _NextWireID++;
            for (std::size_t _Index = 0; _Index < _Points.size(); ++_Index)
            {
                const auto [_EdgeID, _StartID, _EndID] = _AddEdge(
                    _Points[_Index],
                    _Points[(_Index + 1) % _Points.size()]);
                _Wire.Coedges.push_back({
                    _EdgeID,
                    0,
                    _StartID,
                    _EndID,
                    {},
                    ETopologyOrientation::Forward });
            }
            _Model.Wires.push_back(_Wire);

            const auto _DeltaY = End_.X - Start_.X;
            const auto _DeltaZ = End_.Y - Start_.Y;
            const auto _NormalLength = std::hypot(_DeltaY, _DeltaZ);
            PlaneSurface3 _Plane;
            _Plane.Placement.Location = _P0;
            _Plane.Placement.ZDirection = {
                0.0,
                -_DeltaZ / _NormalLength,
                _DeltaY / _NormalLength };
            Surface3Record _Surface;
            _Surface.Id = _NextSurfaceID++;
            _Surface.Geometry = _Plane;
            _Model.Surfaces3.push_back(_Surface);

            BRepFace _Face;
            _Face.Id = _NextFaceID++;
            _Face.Surface3Id = _Surface.Id;
            _Face.WireIds.push_back(_Wire.Id);
            _Model.Faces.push_back(_Face);
        };

        const std::vector<Point2> _Outer = bConcaveOuter_
            ? std::vector<Point2>{
                Point2{ -50.0, -25.0 },
                Point2{  50.0, -25.0 },
                Point2{  50.0,  25.0 },
                Point2{  15.0,  25.0 },
                Point2{  15.0,   5.0 },
                Point2{ -50.0,   5.0 } }
            : std::vector<Point2>{
                Point2{ -50.0, -25.0 },
                Point2{  50.0, -25.0 },
                Point2{  50.0,  25.0 },
                Point2{ -50.0,  25.0 } };
        const std::vector<Point2> _Inner = bConcaveOuter_
            ? std::vector<Point2>{
                Point2{ -25.0, -10.0 },
                Point2{  25.0, -10.0 },
                Point2{  25.0,   0.0 },
                Point2{ -25.0,   0.0 } }
            : std::vector<Point2>{
                Point2{ -30.0, -12.0 },
                Point2{  30.0, -12.0 },
                Point2{  30.0,  12.0 },
                Point2{ -30.0,  12.0 } };

        for (std::size_t _Index = 0; _Index < _Outer.size(); ++_Index)
        {
            _AddProfileEdgeFace(_Outer[_Index], _Outer[(_Index + 1) % _Outer.size()]);
        }
        for (std::size_t _Index = 0; _Index < _Inner.size(); ++_Index)
        {
            _AddProfileEdgeFace(_Inner[_Index], _Inner[(_Index + 1) % _Inner.size()]);
        }

        // These are section connections whose endpoints lie in the middle
        // of existing outer/inner edges, not at their original vertices.
        if (!bConcaveOuter_)
        {
            _AddProfileEdgeFace({ 0.0, -25.0 }, { 0.0, -12.0 });
            _AddProfileEdgeFace({ 50.0, 0.0 }, { 30.0, 0.0 });
            _AddProfileEdgeFace({ 0.0, 25.0 }, { 0.0, 12.0 });
            _AddProfileEdgeFace({ -50.0, 0.0 }, { -30.0, 0.0 });
        }
        else
        {
            _AddProfileEdgeFace({ 0.0, -25.0 }, { 0.0, -10.0 });
            _AddProfileEdgeFace({ 50.0, 0.0 }, { 25.0, -5.0 });
            _AddProfileEdgeFace({ 30.0, 25.0 }, { 0.0, 0.0 });
            _AddProfileEdgeFace({ -30.0, 5.0 }, { -25.0, -5.0 });
        }
        return _Model;
    }

    BRepModel MakeTaperedRectangularTube(double dLength_ = 240.0)
    {
        Triangulation3Record _Record;
        _Record.Id = 11;
        AddTaperedLoopWalls(_Record.Geometry, {
            Point2{ -50.0, -25.0 }, Point2{ 50.0, -25.0 },
            Point2{ 50.0, 25.0 }, Point2{ -50.0, 25.0 }
        }, {
            Point2{ -70.0, -35.0 }, Point2{ 70.0, -35.0 },
            Point2{ 70.0, 35.0 }, Point2{ -70.0, 35.0 }
        }, dLength_);
        AddTaperedLoopWalls(_Record.Geometry, {
            Point2{ -44.0, -19.0 }, Point2{ -44.0, 19.0 },
            Point2{ 44.0, 19.0 }, Point2{ 44.0, -19.0 }
        }, {
            Point2{ -64.0, -29.0 }, Point2{ -64.0, 29.0 },
            Point2{ 64.0, 29.0 }, Point2{ 64.0, -29.0 }
        }, dLength_);

        BRepModel _Model;
        _Model.Triangulations3.push_back(std::move(_Record));
        _Model.Solids.push_back(BRepSolid{ 1 });
        _Model.RootSolidIds.push_back(1);
        return _Model;
    }

    SSectionTypeDefinition MakeRectangleDefinition(
        const std::string& TypeID_,
        unsigned long long nCavityCount_)
    {
        SSectionTypeDefinition _Definition;
        _Definition.TypeID = TypeID_;
        _Definition.DisplayName = TypeID_;
        _Definition.Bindings["outer"] = Op(
            "fit.rectangle",
            { std::string("outer") });
        _Definition.Match = Op("all", {
            Op("equals", { Path("section.cavityCount"), nCavityCount_ }),
            Path("$outer.valid")
        });
        _Definition.Parameters["width"] = Path("$outer.width");
        _Definition.Parameters["height"] = Path("$outer.height");
        _Definition.Parameters["cavityCount"] = Path("section.cavityCount");
        _Definition.Placement["origin"] = Path("$outer.center");
        _Definition.Placement["xDirection"] = Path("$outer.longAxis");
        return _Definition;
    }

    std::pair<double, double> YRange(const BRepModel& Geometry_)
    {
        auto _Minimum = (std::numeric_limits<double>::max)();
        auto _Maximum = (std::numeric_limits<double>::lowest)();
        for (const auto& _Record : Geometry_.Triangulations3)
        {
            for (const auto& _Point : _Record.Geometry.Vertices)
            {
                _Minimum = std::min(_Minimum, _Point.Y);
                _Maximum = std::max(_Maximum, _Point.Y);
            }
        }
        return { _Minimum, _Maximum };
    }

    std::filesystem::path RepositoryRoot()
    {
        auto _Path = std::filesystem::path(__FILE__).parent_path();
        for (int _Index = 0; _Index < 6; ++_Index)
        {
            _Path = _Path.parent_path();
        }
        return _Path;
    }

    ObjectMap DirectFittingTestRuntime(const std::string& Action_)
    {
        const auto _Root = RepositoryRoot();
        const auto _Worker = _Root / "src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py";
        iCAX::TemplateRuntime::CPythonTemplateHost _Host({
            _Root / "src/x64/Debug/runtime/python/python312.dll", _Worker, _Worker.parent_path() });
        return _Host.Invoke({
            { "protocol", std::string("icax.template-runtime") }, { "protocolVersion", 1ull },
            { "operation", std::string("evaluate") },
            { "templatePath", (std::filesystem::path(__FILE__).parent_path() / "DirectFittingTestRuntime.py").string() },
            { "template", ObjectMap{ { "packageDigest", std::string("native-direct-fitting-tests") } } },
            { "parameters", ObjectMap{ { "action", Action_ },
                { "profileRoot", (_Root / "src/apps/tube-designer/templates/profile").string() } } },
            { "context", ObjectMap{} } });
    }

    struct SForbidForwardFitting final
    {
        SForbidForwardFitting() { DirectFittingTestRuntime("guard"); }
        ~SForbidForwardFitting() { DirectFittingTestRuntime("unguard"); }
    };

    BRepModel ExtrudedProfileFixture(const ObjectMap& Profile_)
    {
        const auto _Angle = 37.0 * 3.14159265358979323846 / 180.0;
        const ObjectMap _Model{
            { "schema", std::string("icax.neutral-model") }, { "schemaVersion", 1ull },
            { "template", ObjectMap{ { "id", std::string("native-fitting-fixture") },
                { "version", std::string("1.0.0") }, { "packageDigest", std::string("test") } } },
            { "geometry", VariantArray{ ObjectMap{
                { "key", std::string("profile") }, { "operator", std::string("profile2d") },
                { "arguments", ObjectMap{
                    { "placement", ObjectMap{ { "origin", VariantArray{ 25.0, 123.0, -45.0 } },
                        { "xAxis", VariantArray{ 0.0, std::cos(_Angle), std::sin(_Angle) } },
                        { "yAxis", VariantArray{ 0.0, -std::sin(_Angle), std::cos(_Angle) } } } },
                    { "contours", Profile_.at("contours") } } } } } } };
        const auto _Face = iCAX::OpenCascade::EvaluateNeutralModel(
            iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Model), { "profile" }).At("profile");
        const auto _Shape = BRepPrimAPI_MakePrism(_Face, gp_Vec(3000.0, 0.0, 0.0)).Shape();
        return iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(_Shape, "direct-fitting-fixture", "", 0.001);
    }

    constexpr std::string_view kSampleDefinitions = R"json(
    {
      "definitions": [
        {
          "typeId": "tube.circular.hollow",
          "bindings": {
            "outer": { "op": "fit.circle", "args": ["outer"] },
            "inner": { "op": "fit.circle", "args": ["inner:0"] }
          },
          "match": { "op": "all", "args": [
            { "op": "equals", "args": [{ "path": "section.cavityCount" }, 1] },
            { "path": "$outer.valid" },
            { "path": "$inner.valid" }
          ] }
        },
        {
          "typeId": "tube.rectangular.multi-cavity",
          "bindings": {
            "outer": { "op": "fit.rectangle", "args": ["outer"] }
          },
          "match": { "op": "all", "args": [
            { "op": "greater", "args": [{ "path": "section.cavityCount" }, 1] },
            { "path": "$outer.valid" }
          ] },
          "parameters": {
            "width": { "path": "$outer.width" },
            "height": { "path": "$outer.height" },
            "cavityCount": { "path": "section.cavityCount" }
          },
          "placement": {
            "origin": { "path": "$outer.center" },
            "xDirection": { "path": "$outer.longAxis" }
          }
        }
      ]
    })json";
}

TEST(ExtrusionRecognitionTest, RecognizesDirectionFromStraightEdgeLengthVotes)
{
    CExtrudeRecognizesService _Service;
    const auto _Result = _Service.Recognize(MakeStraightEdgeEvidenceModel());

    ASSERT_TRUE(_Result.bSuccess);
    EXPECT_NEAR(0.6, _Result.Direction.X, 1.0e-12);
    EXPECT_NEAR(0.8, _Result.Direction.Y, 1.0e-12);
    EXPECT_NEAR(0.0, _Result.Direction.Z, 1.0e-12);
    EXPECT_GT(_Result.dConfidence, 0.99);
    EXPECT_NEAR(0.6, _Result.TRSF.Matrix.Values[0][0], 1.0e-12);
    EXPECT_NEAR(0.8, _Result.TRSF.Matrix.Values[0][1], 1.0e-12);
    ASSERT_EQ(1u, _Result.AlignedGeometry.Curves3.size());
    const auto& _AlignedLine = std::get<Line3>(
        _Result.AlignedGeometry.Curves3.front().Geometry);
    EXPECT_NEAR(1.0, _AlignedLine.Axis.Direction.X, 1.0e-12);
    EXPECT_NEAR(0.0, _AlignedLine.Axis.Direction.Y, 1.0e-12);
    EXPECT_NEAR(0.0, _AlignedLine.Axis.Direction.Z, 1.0e-12);
    EXPECT_NEAR(-5.0, _AlignedLine.Axis.Location.X, 1.0e-12);
    EXPECT_NEAR(0.0, _AlignedLine.Axis.Location.Y, 1.0e-12);
    EXPECT_NEAR(0.0, _AlignedLine.Axis.Location.Z, 1.0e-12);
}

TEST(ExtrusionRecognitionTest, ExtractsOuterSectionWireFromProjectedSideFaces)
{
    CExtrudeRecognizesService _Service;
    SSectionWireOptions _Options;
    _Options.dConnectionTolerance = 1.0e-8;
    const auto _Result = _Service.ExtractSectionWires(
        MakeRectangularSideFaceBRep(),
        _Options);

    ASSERT_TRUE(_Result.bSuccess);
    ASSERT_EQ(4u, _Result.SideFaceIds.size());
    ASSERT_EQ(1u, _Result.Wires.size());
    const auto& _Outer = _Result.Wires.front();
    EXPECT_FALSE(_Outer.bInner);
    EXPECT_TRUE(_Outer.bClosed);
    EXPECT_NEAR(5000.0, _Outer.dSignedArea, 1.0e-6);
    ASSERT_EQ(4u, _Outer.Edges.size());
    for (const auto& _Edge : _Outer.Edges)
    {
        EXPECT_TRUE(std::holds_alternative<Line2>(_Edge.Curve));
        EXPECT_GT(_Edge.Samples.size(), 1u);
        EXPECT_NEAR(_Edge.Start.X, _Edge.Samples.front().X, 1.0e-8);
        EXPECT_NEAR(_Edge.Start.Y, _Edge.Samples.front().Y, 1.0e-8);
    }
}

TEST(ExtrusionRecognitionTest, MergesCoveredEdgesBeforeSplittingAndMergingPoints)
{
    for (const bool _ReverseOrder : { false, true })
    {
        for (const bool _AddJunction : { false, true })
        {
            SCOPED_TRACE(_ReverseOrder);
            SCOPED_TRACE(_AddJunction);
            std::vector<std::pair<Curve3, ParameterRange>> _Edges{
                ProjectedSegment({ -50, -25 }, { 50, -25 }),
                ProjectedSegment({ 50, -25 }, { 50, 25 }),
                ProjectedSegment({ 50, 25 }, { -50, 25 }),
                ProjectedSegment({ -50, 25 }, { -50, -25 }),
                ProjectedSegment({ 50, -25 }, { -50, -25 }),
                ProjectedSegment({ -25, -25 }, { 25, -25 }),
                ProjectedSegment({ 15, -25 }, { -15, -25 }) };
            // A genuine remaining endpoint is slightly off its host but within
            // tolerance. It must split the host and then share its graph node.
            if (_AddJunction)
                _Edges.push_back(ProjectedSegment({ 7, -25 + 4e-9 }, { 7, -10 }));
            if (_ReverseOrder) std::reverse(_Edges.begin(), _Edges.end());
            SSectionWireOptions _Options;
            _Options.dConnectionTolerance = 1e-8;
            const auto _Result = CExtrudeRecognizesService().ExtractSectionWires(
                MakeProjectedBoundaryBRep(_Edges), _Options);
            ASSERT_TRUE(_Result.bSuccess);
            ASSERT_EQ(1u, _Result.Wires.size());
            const auto& _Outer = _Result.Wires.front();
            EXPECT_NEAR(5000.0, _Outer.dSignedArea, 1e-6);
            ASSERT_EQ(_AddJunction ? 5u : 4u, _Outer.Edges.size());
            EXPECT_EQ(_AddJunction ? 1u : 0u, std::count_if(
                _Outer.Edges.begin(), _Outer.Edges.end(), [](const auto& _Edge) {
                    return std::abs(_Edge.Start.X - 7.0) < 1e-8
                        && std::abs(_Edge.Start.Y + 25.0) < 1e-8;
                }));
        }
    }
}

TEST(ExtrusionRecognitionTest, UnionsPartiallyOverlappingEdgesBeforeFindingContours)
{
    std::vector<std::pair<Curve3, ParameterRange>> _Edges{
        ProjectedSegment({ -50, -25 }, { -5, -25 }),
        ProjectedSegment({ 30, -25 }, { -15, -25 }),
        ProjectedSegment({ 20, -25 }, { 50, -25 }),
        ProjectedSegment({ 50, -25 }, { 50, 25 }),
        ProjectedSegment({ 50, 25 }, { -50, 25 }),
        ProjectedSegment({ -50, 25 }, { -50, -25 }) };
    for (const bool _ReverseOrder : { false, true })
    {
        if (_ReverseOrder) std::reverse(_Edges.begin(), _Edges.end());
        const auto _Result = CExtrudeRecognizesService().ExtractSectionWires(
            MakeProjectedBoundaryBRep(_Edges));
        ASSERT_TRUE(_Result.bSuccess);
        ASSERT_EQ(1u, _Result.Wires.size());
        EXPECT_EQ(4u, _Result.Wires.front().Edges.size());
        EXPECT_NEAR(5000.0, _Result.Wires.front().dSignedArea, 1e-6);
    }
}

TEST(ExtrusionRecognitionTest, MergesProjectedStraightSplinesWithAnalyticEdges)
{
    for (const bool _Reverse : { false, true })
    {
        Bezier3 _Spline;
        _Spline.Poles = { { 0, -50, -25 }, { 12, -15, -25 }, { -8, 15, -25 }, { 0, 50, -25 } };
        if (_Reverse) std::reverse(_Spline.Poles.begin(), _Spline.Poles.end());
        const auto _Result = CExtrudeRecognizesService().ExtractSectionWires(MakeProjectedBoundaryBRep({
            ProjectedSegment({ -50, -25 }, { 50, -25 }),
            ProjectedSegment({ 50, -25 }, { 50, 25 }),
            ProjectedSegment({ 50, 25 }, { -50, 25 }),
            ProjectedSegment({ -50, 25 }, { -50, -25 }),
            { _Spline, { 0, 1 } } }));
        ASSERT_TRUE(_Result.bSuccess);
        ASSERT_EQ(1u, _Result.Wires.size());
        EXPECT_EQ(4u, _Result.Wires[0].Edges.size());
        EXPECT_NEAR(5000.0, _Result.Wires[0].dSignedArea, 1e-6);
    }
}

TEST(ExtrusionRecognitionTest, KeepsNonlinearProjectedSplineGeometry)
{
    Bezier3 _Spline;
    _Spline.Poles = { { 0, -50, -25 }, { 12, 0, -40 }, { 0, 50, -25 } };
    const auto _Result = CExtrudeRecognizesService().ExtractSectionWires(MakeProjectedBoundaryBRep({
        { _Spline, { 0, 1 } },
        ProjectedSegment({ 50, -25 }, { 50, 25 }),
        ProjectedSegment({ 50, 25 }, { -50, 25 }),
        ProjectedSegment({ -50, 25 }, { -50, -25 }) }));
    ASSERT_TRUE(_Result.bSuccess);
    ASSERT_EQ(1u, _Result.Wires.size());
    EXPECT_EQ(1, std::count_if(_Result.Wires[0].Edges.begin(), _Result.Wires[0].Edges.end(),
        [](const auto& Edge_) { return std::holds_alternative<Bezier2>(Edge_.Curve); }));
}

TEST(ExtrusionRecognitionTest, DoesNotMergeDistinctParallelEdgesOrBridgeGaps)
{
    const auto _Nested = CExtrudeRecognizesService().ExtractSectionWires(MakeProjectedBoundaryBRep({
        ProjectedSegment({ -50, -25 }, { 50, -25 }),
        ProjectedSegment({ 50, -25 }, { 50, 25 }),
        ProjectedSegment({ 50, 25 }, { -50, 25 }),
        ProjectedSegment({ -50, 25 }, { -50, -25 }),
        ProjectedSegment({ -49, -24 }, { 49, -24 }),
        ProjectedSegment({ 49, -24 }, { 49, 24 }),
        ProjectedSegment({ 49, 24 }, { -49, 24 }),
        ProjectedSegment({ -49, 24 }, { -49, -24 }) }));
    ASSERT_TRUE(_Nested.bSuccess);
    ASSERT_EQ(2u, _Nested.Wires.size());
    EXPECT_NEAR(4704.0, -_Nested.Wires[1].dSignedArea, 1e-6);
    const auto _Gap = CExtrudeRecognizesService().ExtractSectionWires(MakeProjectedBoundaryBRep({
        ProjectedSegment({ -50, -25 }, { -1, -25 }),
        ProjectedSegment({ 1, -25 }, { 50, -25 }),
        ProjectedSegment({ 50, -25 }, { 50, 25 }),
        ProjectedSegment({ 50, 25 }, { -50, 25 }),
        ProjectedSegment({ -50, 25 }, { -50, -25 }) }));
    EXPECT_FALSE(_Gap.bSuccess);
}

TEST(ExtrusionRecognitionTest, FullConicsSwallowCoveredArcsAcrossParameterSeams)
{
    constexpr double _Pi = 3.14159265358979323846;
    for (const bool _Ellipse : { false, true })
    {
        for (const bool _ReverseOrder : { false, true })
        {
            std::vector<std::pair<Curve3, ParameterRange>> _Edges{
                ProjectedConic(0, 2 * _Pi, _Ellipse),
                ProjectedConic(1.7 * _Pi, 2.3 * _Pi, _Ellipse),
                ProjectedConic(_Pi, 0.2 * _Pi, _Ellipse),
                ProjectedConic(0, 2 * _Pi, _Ellipse, _Ellipse ? _Pi : 0.37) };
            if (_ReverseOrder) std::reverse(_Edges.begin(), _Edges.end());
            const auto _Result = CExtrudeRecognizesService().ExtractSectionWires(
                MakeProjectedBoundaryBRep(_Edges));
            ASSERT_TRUE(_Result.bSuccess);
            ASSERT_EQ(1u, _Result.Wires.size());
            ASSERT_EQ(1u, _Result.Wires.front().Edges.size());
            EXPECT_FALSE(_Result.Wires.front().bInner);
            EXPECT_GT(_Result.Wires.front().dSignedArea, _Ellipse ? 600.0 : 300.0);
        }
    }
}

TEST(ExtrusionRecognitionTest, OverlappingArcsMergeButComplementaryArcsStayDistinct)
{
    constexpr double _Pi = 3.14159265358979323846;
    for (const bool _Ellipse : { false, true })
    {
        const auto _Radius = _Ellipse ? 20.0 : 10.0;
        const auto _Merged = CExtrudeRecognizesService().ExtractSectionWires(MakeProjectedBoundaryBRep({
            ProjectedConic(0, 0.7 * _Pi, _Ellipse),
            ProjectedConic(_Pi, 0.4 * _Pi, _Ellipse),
            ProjectedSegment({ -_Radius, 0 }, { _Radius, 0 }) }));
        ASSERT_TRUE(_Merged.bSuccess);
        ASSERT_EQ(1u, _Merged.Wires.size());
        EXPECT_EQ(2u, _Merged.Wires.front().Edges.size());
        const auto _Distinct = CExtrudeRecognizesService().ExtractSectionWires(MakeProjectedBoundaryBRep({
            ProjectedConic(0, _Pi, _Ellipse),
            ProjectedConic(_Pi, 2 * _Pi, _Ellipse) }));
        ASSERT_TRUE(_Distinct.bSuccess);
        ASSERT_EQ(1u, _Distinct.Wires.size());
        EXPECT_EQ(2u, _Distinct.Wires.front().Edges.size());
    }
}

TEST(ExtrusionRecognitionTest, SplitsMergedArcAtSurvivingEndpointOnExactCurve)
{
    constexpr double _Pi = 3.14159265358979323846;
    constexpr double _Angle = 0.37;
    const Point2 _Junction{ 10 * std::cos(_Angle), 10 * std::sin(_Angle) };
    const auto _Result = CExtrudeRecognizesService().ExtractSectionWires(MakeProjectedBoundaryBRep({
        ProjectedConic(0, _Pi),
        ProjectedConic(0.2, 0.8),
        ProjectedSegment({ -10, 0 }, { 10, 0 }),
        ProjectedSegment(_Junction, { _Junction.X * 0.8, _Junction.Y * 0.8 }) }));
    std::string _Diagnostics;
    for (const auto& _Message : _Result.Diagnostics) _Diagnostics += _Message + " | ";
    SCOPED_TRACE(_Diagnostics);
    ASSERT_TRUE(_Result.bSuccess);
    ASSERT_EQ(1u, _Result.Wires.size());
    ASSERT_EQ(3u, _Result.Wires.front().Edges.size());
    EXPECT_EQ(1u, std::count_if(_Result.Wires.front().Edges.begin(), _Result.Wires.front().Edges.end(),
        [&](const auto& _Edge) { return std::hypot(_Edge.Start.X - _Junction.X, _Edge.Start.Y - _Junction.Y) < 1e-8; }));
    for (const auto& _Edge : _Result.Wires.front().Edges)
        if (std::holds_alternative<Arc2>(_Edge.Curve))
            for (const auto& _Point : _Edge.Samples)
                EXPECT_NEAR(10.0, std::hypot(_Point.X, _Point.Y), 1e-8);
}

TEST(ExtrusionRecognitionTest, StartsOuterAndInnerAtLowestLeftmostPositionRegardlessOfInputOrder)
{
    const std::vector<std::pair<Curve3, ParameterRange>> _Original{
        ProjectedSegment({ -50, -25 }, { 50, -25 }),
        ProjectedSegment({ 50, -25 }, { 50, 25 }),
        ProjectedSegment({ 50, 25 }, { -50, 25 }),
        ProjectedSegment({ -50, 25 }, { -50, -25 }),
        ProjectedSegment({ -20, -10 }, { 20, -10 }),
        ProjectedSegment({ 20, -10 }, { 20, 10 }),
        ProjectedSegment({ 20, 10 }, { -20, 10 }),
        ProjectedSegment({ -20, 10 }, { -20, -10 }) };
    for (std::size_t _Shift = 0; _Shift < _Original.size(); ++_Shift)
    {
        for (const unsigned _ReverseMask : { 0u, 0x55u, 0xaau, 0xffu })
        {
            SCOPED_TRACE(_Shift);
            SCOPED_TRACE(_ReverseMask);
            auto _Edges = _Original;
            for (std::size_t _Index = 0; _Index < _Edges.size(); ++_Index)
            {
                auto& _Segment = std::get<Segment3>(_Edges[_Index].first);
                if (_ReverseMask & (1u << _Index)) std::swap(_Segment.Start, _Segment.End);
            }
            std::rotate(_Edges.begin(), _Edges.begin() + _Shift, _Edges.end());
            const auto _Result = CExtrudeRecognizesService().ExtractSectionWires(
                MakeProjectedBoundaryBRep(_Edges));
            ASSERT_TRUE(_Result.bSuccess);
            ASSERT_EQ(2u, _Result.Wires.size());
            for (std::size_t _Index = 0; _Index < _Result.Wires.size(); ++_Index)
            {
                const auto& _Wire = _Result.Wires[_Index];
                ASSERT_EQ(4u, _Wire.Edges.size());
                EXPECT_EQ(_Index != 0, _Wire.bInner);
                EXPECT_NEAR(_Index == 0 ? 5000.0 : -800.0, _Wire.dSignedArea, 1e-8);
                const auto& _First = _Wire.Edges.front();
                EXPECT_NEAR(_Index == 0 ? -50.0 : -20.0, _First.Start.X, 1e-8);
                EXPECT_NEAR(_Index == 0 ? -25.0 : -10.0, _First.Start.Y, 1e-8);
                EXPECT_NEAR(_Index == 0 ? 50.0 : -20.0, _First.End.X, 1e-8);
                EXPECT_NEAR(_Index == 0 ? -25.0 : 10.0, _First.End.Y, 1e-8);
                for (std::size_t _Edge = 0; _Edge < _Wire.Edges.size(); ++_Edge)
                {
                    const auto& _Next = _Wire.Edges[(_Edge + 1) % _Wire.Edges.size()];
                    EXPECT_NEAR(_Wire.Edges[_Edge].End.X, _Next.Start.X, 1e-8);
                    EXPECT_NEAR(_Wire.Edges[_Edge].End.Y, _Next.Start.Y, 1e-8);
                }
            }
        }
    }
}

TEST(ExtrusionRecognitionTest, StartsAtCurveContainingInteriorMinimumWithoutSplittingIt)
{
    constexpr double _Pi = 3.14159265358979323846;
    for (const bool _Ellipse : { false, true })
    {
        for (const bool _Inner : { false, true })
        {
            for (const bool _Reverse : { false, true })
            {
                SCOPED_TRACE(_Ellipse);
                SCOPED_TRACE(_Inner);
                SCOPED_TRACE(_Reverse);
                const auto _Radius = _Ellipse ? 20.0 : 10.0;
                std::vector<std::pair<Curve3, ParameterRange>> _Edges{
                    ProjectedSegment({ -_Radius, 0 }, { _Radius, 0 }),
                    ProjectedConic(_Reverse ? 2 * _Pi : _Pi, _Reverse ? _Pi : 2 * _Pi, _Ellipse) };
                if (_Inner)
                {
                    _Edges.push_back(ProjectedSegment({ -50, -25 }, { 50, -25 }));
                    _Edges.push_back(ProjectedSegment({ 50, -25 }, { 50, 25 }));
                    _Edges.push_back(ProjectedSegment({ 50, 25 }, { -50, 25 }));
                    _Edges.push_back(ProjectedSegment({ -50, 25 }, { -50, -25 }));
                }
                if (_Reverse) std::reverse(_Edges.begin(), _Edges.end());
                const auto _Result = CExtrudeRecognizesService().ExtractSectionWires(
                    MakeProjectedBoundaryBRep(_Edges));
                ASSERT_TRUE(_Result.bSuccess);
                ASSERT_EQ(_Inner ? 2u : 1u, _Result.Wires.size());
                const auto& _Wire = _Result.Wires.back();
                ASSERT_EQ(2u, _Wire.Edges.size());
                EXPECT_EQ(_Inner, _Wire.bInner);
                EXPECT_GT(_Inner ? -_Wire.dSignedArea : _Wire.dSignedArea, 150.0);
                const auto& _First = _Wire.Edges.front();
                EXPECT_TRUE(_Ellipse ? std::holds_alternative<EllipseArc2>(_First.Curve)
                    : std::holds_alternative<Arc2>(_First.Curve));
                EXPECT_NEAR(_Inner ? _Radius : -_Radius, _First.Start.X, 1e-8);
                EXPECT_NEAR(_Inner ? -_Radius : _Radius, _First.End.X, 1e-8);
                EXPECT_NEAR(0.0, _First.Start.Y, 1e-8);
                EXPECT_NEAR(0.0, _First.End.Y, 1e-8);
            }
        }
    }
}

TEST(ExtrusionRecognitionTest, IgnoresLowerPrunedBranchesWhenChoosingFirstContourEdge)
{
    for (const bool _Reverse : { false, true })
    {
        std::vector<std::pair<Curve3, ParameterRange>> _Edges{
            ProjectedSegment({ 0, -100 }, { 0, -25 }),
            ProjectedSegment({ -50, -25 }, { 50, -25 }),
            ProjectedSegment({ 50, -25 }, { 50, 25 }),
            ProjectedSegment({ 50, 25 }, { -50, 25 }),
            ProjectedSegment({ -50, 25 }, { -50, -25 }) };
        if (_Reverse) std::reverse(_Edges.begin(), _Edges.end());
        const auto _Result = CExtrudeRecognizesService().ExtractSectionWires(
            MakeProjectedBoundaryBRep(_Edges));
        ASSERT_TRUE(_Result.bSuccess);
        ASSERT_EQ(1u, _Result.Wires.size());
        const auto& _Wire = _Result.Wires.front();
        ASSERT_EQ(5u, _Wire.Edges.size());
        EXPECT_NEAR(5000.0, _Wire.dSignedArea, 1e-8);
        EXPECT_NEAR(-50.0, _Wire.Edges.front().Start.X, 1e-8);
        EXPECT_NEAR(-25.0, _Wire.Edges.front().Start.Y, 1e-8);
        EXPECT_NEAR(0.0, _Wire.Edges.front().End.X, 1e-8);
        EXPECT_NEAR(-25.0, _Wire.Edges.front().End.Y, 1e-8);
    }
}

TEST(ExtrusionRecognitionTest, SplitsHostEdgeWhenEndpointFallsInItsInterior)
{
    CExtrudeRecognizesService _Service;
    SSectionWireOptions _Options;
    _Options.dConnectionTolerance = 1.0e-8;
    const auto _Result = _Service.ExtractSectionWires(
        MakeRectangularSideFaceBRep(true),
        _Options);

    std::string _Diagnostics;
    for (const auto& _Diagnostic : _Result.Diagnostics)
    {
        _Diagnostics += _Diagnostic + " | ";
    }
    ASSERT_TRUE(_Result.bSuccess) << _Diagnostics;
    ASSERT_EQ(1u, _Result.Wires.size());
    ASSERT_FALSE(_Result.Wires.front().bInner);
    EXPECT_GE(_Result.Wires.front().Edges.size(), 5u);
    EXPECT_NEAR(5000.0, _Result.Wires.front().dSignedArea, 1.0e-6);
    EXPECT_TRUE(std::any_of(
        _Result.Diagnostics.begin(),
        _Result.Diagnostics.end(),
        [](const auto& _Diagnostic) {
            return _Diagnostic.find("endpoint-on-edge") != std::string::npos;
        }));
    EXPECT_TRUE(std::any_of(
        _Result.Diagnostics.begin(),
        _Result.Diagnostics.end(),
        [](const auto& _Diagnostic) {
            return _Diagnostic.find("degree-one") != std::string::npos;
        }));
}

TEST(ExtrusionRecognitionTest, FindsOuterContourWithMultipleInternalConnections)
{
    CExtrudeRecognizesService _Service;
    SSectionWireOptions _Options;
    _Options.dConnectionTolerance = 1.0e-8;
    const auto _Result = _Service.ExtractSectionWires(
        MakeTubeWithInternalConnectionsBRep(),
        _Options);

    ASSERT_TRUE(_Result.bSuccess);
    ASSERT_EQ(2u, _Result.Wires.size());
    const auto& _Outer = _Result.Wires.front();
    EXPECT_FALSE(_Outer.bInner);
    EXPECT_NEAR(5000.0, std::abs(_Outer.dSignedArea), 1.0e-6);
    EXPECT_EQ(8u, _Outer.Edges.size());
    EXPECT_EQ(1u, std::count_if(
        _Result.Wires.begin(),
        _Result.Wires.end(),
        [](const auto& _Wire) { return _Wire.bInner; }));
    EXPECT_TRUE(std::any_of(
        _Result.Diagnostics.begin(),
        _Result.Diagnostics.end(),
        [](const auto& _Diagnostic) {
            return _Diagnostic.find("endpoint-on-edge") != std::string::npos;
        }));
}

TEST(ExtrusionRecognitionTest, FindsConcaveOuterContourWithMultipleInternalConnections)
{
    CExtrudeRecognizesService _Service;
    SSectionWireOptions _Options;
    _Options.dConnectionTolerance = 1.0e-8;
    const auto _Result = _Service.ExtractSectionWires(
        MakeTubeWithInternalConnectionsBRep(true),
        _Options);

    std::string _Diagnostics;
    for (const auto& _Diagnostic : _Result.Diagnostics)
    {
        _Diagnostics += _Diagnostic + " | ";
    }
    ASSERT_TRUE(_Result.bSuccess) << _Diagnostics;
    ASSERT_EQ(2u, _Result.Wires.size());

    const auto& _Outer = _Result.Wires.front();
    EXPECT_FALSE(_Outer.bInner);
    EXPECT_NEAR(3700.0, std::abs(_Outer.dSignedArea), 1.0e-6);
    EXPECT_EQ(10u, _Outer.Edges.size());
    EXPECT_EQ(1u, std::count_if(
        _Result.Wires.begin(),
        _Result.Wires.end(),
        [](const auto& _Wire) { return _Wire.bInner; }));

    const auto _Inner = std::find_if(
        _Result.Wires.begin(),
        _Result.Wires.end(),
        [](const auto& _Wire) { return _Wire.bInner; });
    ASSERT_NE(_Inner, _Result.Wires.end());
    EXPECT_NEAR(500.0, std::abs(_Inner->dSignedArea), 1.0e-6);
    EXPECT_TRUE(std::any_of(
        _Result.Diagnostics.begin(),
        _Result.Diagnostics.end(),
        [](const auto& _Diagnostic) {
            return _Diagnostic.find("endpoint-on-edge") != std::string::npos;
        }));
}

TEST(ExtrusionRecognitionTest, ExtractsSectionWiresFromResetStepBRep)
{
    const auto _Path = RepositoryRoot()
        / "samples" / "tube-one" / "01_round_tube_plain.step";
    const auto _Read = iCAX::OpenCascade::ReadBRepFile(
        _Path.string(),
        0.001);
    ASSERT_TRUE(_Read.bOK);

    CExtrudeRecognizesService _Service;
    const auto _Direction = _Service.Recognize(_Read.Geometry);
    ASSERT_TRUE(_Direction.bSuccess);
    const auto _Sections = _Service.ExtractSectionWires(
        _Direction.AlignedGeometry);

    ASSERT_TRUE(_Sections.bSuccess);
    ASSERT_FALSE(_Sections.Wires.empty());
    EXPECT_EQ(2u, _Sections.Wires.size());
    EXPECT_FALSE(_Sections.Wires.front().bInner);
    EXPECT_GT(_Sections.Wires.front().dSignedArea, 0.0);
    EXPECT_TRUE(_Sections.Wires[1].bInner);
    EXPECT_LT(_Sections.Wires[1].dSignedArea, 0.0);
}

TEST(ExtrusionRecognitionTest, RunsAllTemplateDirectFittersOnNativeExtractedContoursWithoutForwardCalls)
{
    const auto _Fixtures = DirectFittingTestRuntime("fixtures").at("fixtures").To<VariantArray>();
    const auto _Fitters = DiscoverPythonSectionFitters(
        (RepositoryRoot() / "src/apps/tube-designer/templates/profile").string());
    ASSERT_EQ(21u, _Fitters.size());
    ASSERT_EQ(_Fitters.size(), _Fixtures.size());
    std::vector<std::pair<std::string, BRepModel>> _Models;
    for (const auto& _Value : _Fixtures)
    {
        const auto _Fixture = _Value.To<ObjectMap>();
        _Models.emplace_back(_Fixture.at("id").To<std::string>(),
            ExtrudedProfileFixture(_Fixture.at("profile").To<ObjectMap>()));
    }
    SForbidForwardFitting _Guard;
    for (const auto& [_ID, _Geometry] : _Models)
    {
        SCOPED_TRACE(_ID);
        const auto _Fitter = std::find_if(_Fitters.begin(), _Fitters.end(), [&](const auto& F_) { return F_.TypeID == _ID; });
        ASSERT_NE(_Fitters.end(), _Fitter);
        EXPECT_EQ("fitting.py", std::filesystem::path(_Fitter->ScriptPath).filename().string());
        const auto _Result = CExtrusionRecognitionService().RecognizePythonFitters(
            _Geometry, std::span<const SPythonSectionFitter>(&*_Fitter, 1));
        std::string _Diagnostics;
        for (const auto& _Text : _Result.Diagnostics) _Diagnostics += _Text + " | ";
        EXPECT_TRUE(_Result.IsOK()) << _Diagnostics;
        if (!_Result.IsOK()) continue;
        EXPECT_EQ(_ID, _Result.SectionTypeID);
        EXPECT_FALSE(_Result.SectionParameters.empty());
        EXPECT_NEAR(3000.0, _Result.dLength, 1e-6);
        if (_ID == "rect" || _ID == "rect-bar")
        {
            EXPECT_NEAR(_Result.SectionParameters.at("width").To<double>(),
                _Result.Section.Bounds.Max.X - _Result.Section.Bounds.Min.X, 1e-3);
            EXPECT_NEAR(_Result.SectionParameters.at("depth").To<double>(),
                _Result.Section.Bounds.Max.Y - _Result.Section.Bounds.Min.Y, 1e-3);
            EXPECT_NEAR(0.0, _Result.Section.Bounds.Min.X + _Result.Section.Bounds.Max.X, 1e-3);
            EXPECT_NEAR(0.0, _Result.Section.Bounds.Min.Y + _Result.Section.Bounds.Max.Y, 1e-3);
        }
        const auto _Normalized = iCAX::OpenCascade::BuildOpenCascadeShape(_Result.NormalizedGeometry);
        ASSERT_TRUE(_Normalized.bOK);
        GProp_GProps _Before, _After;
        BRepGProp::VolumeProperties(iCAX::OpenCascade::BuildOpenCascadeShape(_Geometry).Shape, _Before);
        BRepGProp::VolumeProperties(_Normalized.Shape, _After);
        EXPECT_NEAR(_Before.Mass(), _After.Mass(), std::abs(_Before.Mass()) * 1e-8);
        // The stored section must be in the same frame as the normalized BRep.
        const auto _Section = CExtrudeRecognizesService().ExtractSectionWires(_Result.NormalizedGeometry);
        ASSERT_TRUE(_Section.bSuccess);
        for (const auto& _Wire : _Section.Wires)
            for (const auto& _Edge : _Wire.Edges)
            {
                EXPECT_GE(_Edge.Start.X, _Result.Section.Bounds.Min.X - 1e-3);
                EXPECT_LE(_Edge.Start.X, _Result.Section.Bounds.Max.X + 1e-3);
                EXPECT_GE(_Edge.Start.Y, _Result.Section.Bounds.Min.Y - 1e-3);
                EXPECT_LE(_Edge.Start.Y, _Result.Section.Bounds.Max.Y + 1e-3);
            }
    }
    EXPECT_GE(DirectFittingTestRuntime("unguard").at("fittingCalls").To<std::int64_t>(), 21);
}

TEST(ExtrusionRecognitionTest, MatchesImportedStepThroughOrderedDirectFitters)
{
    const auto _Read = iCAX::OpenCascade::ReadBRepFile(
        (RepositoryRoot() / "samples/tube-one/01_round_tube_plain.step").string(), 0.001);
    ASSERT_TRUE(_Read.bOK);
    const auto _Fitters = DiscoverPythonSectionFitters(
        (RepositoryRoot() / "src/apps/tube-designer/templates/profile").string());
    SForbidForwardFitting _Guard;
    const auto _Result = CExtrusionRecognitionService().RecognizePythonFitters(_Read.Geometry, _Fitters);
    std::string _Diagnostics;
    for (const auto& _Text : _Result.Diagnostics) _Diagnostics += _Text + " | ";
    ASSERT_TRUE(_Result.IsOK()) << _Diagnostics;
    EXPECT_EQ("round", _Result.SectionTypeID);
    EXPECT_EQ(1u, _Result.Section.nCavityCount);
    EXPECT_GT(_Result.SectionParameters.at("width").To<double>(), 0.0);
    EXPECT_GT(_Result.SectionParameters.at("wallThickness").To<double>(), 0.0);
    EXPECT_NEAR(_Result.SectionParameters.at("width").To<double>(),
        _Result.Section.Bounds.Max.X - _Result.Section.Bounds.Min.X, 1e-6);
    EXPECT_NEAR(_Result.SectionParameters.at("width").To<double>(),
        _Result.Section.Bounds.Max.Y - _Result.Section.Bounds.Min.Y, 1e-6);
}

TEST(ExtrusionRecognitionTest, RecognizesMachinedProductionTubeWithoutFollowingHoleAxes)
{
    const auto _Read = iCAX::OpenCascade::ReadBRepFile(
        (RepositoryRoot() / "samples/production-tubes/rev/REV-21-3558.step").string(), 0.001);
    ASSERT_TRUE(_Read.bOK);
    SForbidForwardFitting _Guard;
    const auto _Fitters = DiscoverPythonSectionFitters(
        (RepositoryRoot() / "src/apps/tube-designer/templates/profile").string());
    const auto _Result = CExtrusionRecognitionService().RecognizePythonFitters(_Read.Geometry, _Fitters);
    ASSERT_TRUE(_Result.IsOK());
    EXPECT_NEAR(1.0, std::abs(_Result.Direction.Z), 1e-9);
    EXPECT_NEAR(76.2, _Result.dLength, 1e-6);
    EXPECT_EQ("rect", _Result.SectionTypeID);
    EXPECT_EQ(1u, _Result.Section.nCavityCount);
    EXPECT_NEAR(3.175, _Result.SectionParameters.at("wallThickness").To<double>(), 1e-6);
}

TEST(ExtrusionRecognitionTest, RetainsCompleteGroovedProductionTubeContours)
{
    const auto _Read = iCAX::OpenCascade::ReadBRepFile(
        (RepositoryRoot() / "samples/production-tubes/rev/REV-21-2163.step").string(), 0.001);
    ASSERT_TRUE(_Read.bOK);
    CExtrudeRecognizesService _Service;
    const auto _Direction = _Service.Recognize(_Read.Geometry);
    ASSERT_TRUE(_Direction.bSuccess);
    EXPECT_NEAR(1.0, std::abs(_Direction.Direction.Z), 1e-9);
    const auto _Section = _Service.ExtractSectionWires(_Direction.AlignedGeometry);
    ASSERT_TRUE(_Section.bSuccess);
    ASSERT_EQ(6u, _Section.Wires.size());
    EXPECT_GT(_Section.Wires[0].dSignedArea, 1290.0);
    for (const auto& _Wire : _Section.Wires) EXPECT_TRUE(_Wire.bClosed);
    SForbidForwardFitting _Guard;
    const auto _Fitters = DiscoverPythonSectionFitters(
        (RepositoryRoot() / "src/apps/tube-designer/templates/profile").string());
    const auto _Result = CExtrusionRecognitionService().RecognizePythonFitters(_Read.Geometry, _Fitters);
    EXPECT_EQ(ERecognitionStatus::SectionTypeNotMatched, _Result.Status);
    EXPECT_TRUE(_Result.SectionParameters.empty());
    EXPECT_EQ(5u, _Result.Section.nCavityCount);
    EXPECT_NEAR(50.8, _Result.Section.Bounds.Max.X - _Result.Section.Bounds.Min.X, 1e-6);
    EXPECT_NEAR(25.4, _Result.Section.Bounds.Max.Y - _Result.Section.Bounds.Min.Y, 1e-6);
}

TEST(ExtrusionRecognitionTest, PrefersSquareTubeProfileOverGenericRegularPolygon)
{
    const auto _Read = iCAX::OpenCascade::ReadBRepFile(
        (RepositoryRoot() / "samples/production-tubes/tubecon/square_-_100.0_x_100.0_x_2.50.step").string(), 0.001);
    ASSERT_TRUE(_Read.bOK);
    SForbidForwardFitting _Guard;
    const auto _Fitters = DiscoverPythonSectionFitters(
        (RepositoryRoot() / "src/apps/tube-designer/templates/profile").string());
    const auto _Result = CExtrusionRecognitionService().RecognizePythonFitters(_Read.Geometry, _Fitters);
    ASSERT_TRUE(_Result.IsOK());
    EXPECT_EQ("rect", _Result.SectionTypeID);
    EXPECT_NEAR(100.0, _Result.SectionParameters.at("width").To<double>(), 1e-6);
    EXPECT_NEAR(100.0, _Result.SectionParameters.at("depth").To<double>(), 1e-6);
    EXPECT_NEAR(2.5, _Result.SectionParameters.at("wallThickness").To<double>(), 1e-6);
}

TEST(ExtrusionRecognitionTest, DistinguishesUnsupportedAndFailedFittersFromNonMatchingGeometry)
{
    const auto _Catalog = DirectFittingTestRuntime("negative-fixtures").at("profileRoot").To<std::string>();
    const auto _Fitters = DiscoverPythonSectionFitters(_Catalog);
    ASSERT_EQ(3u, _Fitters.size());
    const auto _Read = iCAX::OpenCascade::ReadBRepFile(
        (RepositoryRoot() / "samples/tube-one/01_round_tube_plain.step").string(), 0.001);
    ASSERT_TRUE(_Read.bOK);
    SForbidForwardFitting _Guard;
    for (const auto& _Fitter : _Fitters)
    {
        SCOPED_TRACE(_Fitter.TypeID);
        const auto _Result = CExtrusionRecognitionService().RecognizePythonFitters(
            _Read.Geometry, std::span<const SPythonSectionFitter>(&_Fitter, 1));
        EXPECT_EQ(_Fitter.TypeID == "error" ? ERecognitionStatus::SectionTypeDefinitionInvalid
            : ERecognitionStatus::SectionTypeNotMatched, _Result.Status);
        EXPECT_TRUE(_Result.SectionTypeID.empty());
        EXPECT_TRUE(_Result.SectionParameters.empty());
        EXPECT_TRUE(std::any_of(_Result.Diagnostics.begin(), _Result.Diagnostics.end(), [&](const auto& Text_) {
            return Text_.find(_Fitter.TypeID + ": " + _Fitter.TypeID) != std::string::npos;
        }));
    }
}

TEST(ExtrusionRecognitionTest, RecognizesHollowSectionAndNormalizesToYAxis)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();

    const std::vector<SSectionTypeDefinition> _Definitions{
        MakeRectangleDefinition("solid-rectangle", 0),
        MakeRectangleDefinition("rectangular-tube", 1)
    };
    const auto _Result = _Service.Recognize(
        MakeRectangularTube(),
        _Definitions);

    ASSERT_TRUE(_Result.IsOK());
    EXPECT_EQ("rectangular-tube", _Result.SectionTypeID);
    EXPECT_EQ(1u, _Result.Section.nCavityCount);
    EXPECT_EQ(2u, _Result.Section.Loops.size());
    EXPECT_NEAR(240.0, _Result.dLength, 1.0e-6);
    ASSERT_TRUE(_Result.SectionParameters.at("width").Is<double>());
    ASSERT_TRUE(_Result.SectionParameters.at("height").Is<double>());
    EXPECT_NEAR(100.0, _Result.SectionParameters.at("width").To<double>(), 1.0e-6);
    EXPECT_NEAR(50.0, _Result.SectionParameters.at("height").To<double>(), 1.0e-6);

    const auto [_MinimumY, _MaximumY] = YRange(_Result.NormalizedGeometry);
    EXPECT_NEAR(0.0, _MinimumY, 1.0e-6);
    EXPECT_NEAR(240.0, _MaximumY, 1.0e-6);
}

TEST(ExtrusionRecognitionTest, UsesFirstMatchingDefinitionWithoutConfidenceCompetition)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();

    auto _First = MakeRectangleDefinition("first", 1);
    auto _Second = MakeRectangleDefinition("second", 1);
    const std::vector<SSectionTypeDefinition> _Definitions{ _First, _Second };

    const auto _Result = _Service.Recognize(
        MakeRectangularTube(),
        _Definitions);

    ASSERT_TRUE(_Result.IsOK());
    EXPECT_EQ("first", _Result.SectionTypeID);
}

TEST(ExtrusionRecognitionTest, RejectsTaperWhoseAxialSectionsKeepChanging)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();
    const std::vector<SSectionTypeDefinition> _Definitions{
        MakeRectangleDefinition("rectangular-tube", 1)
    };

    const auto _Result = _Service.Recognize(
        MakeTaperedRectangularTube(),
        _Definitions);

    EXPECT_FALSE(_Result.IsOK());
    EXPECT_EQ(ERecognitionStatus::NotExtrusionBased, _Result.Status);
}

TEST(ExtrusionRecognitionTest, RejectsUnknownDeclarativeOperator)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();

    SSectionTypeDefinition _Definition;
    _Definition.TypeID = "invalid";
    _Definition.Match = Op("company.privateShapeGuess", {});

    const auto _Validation = _Service.ValidateDefinition(_Definition);

    EXPECT_FALSE(_Validation.bValid);
    ASSERT_FALSE(_Validation.Diagnostics.empty());
    EXPECT_NE(std::string::npos,
        _Validation.Diagnostics.front().find("company.privateShapeGuess"));
}

TEST(ExtrusionRecognitionTest, LoadsStandardJsonAndPreservesDefinitionOrder)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();

    constexpr std::string_view _JSON = R"json(
    {
      "schema": "icax.extrusion-section-types",
      "schemaVersion": 1,
      "definitions": [
        {
          "typeId": "solid-rectangle",
          "bindings": {
            "outer": { "op": "fit.rectangle", "args": ["outer"] }
          },
          "match": {
            "op": "all",
            "args": [
              { "op": "equals", "args": [
                { "path": "section.cavityCount" }, 0
              ] },
              { "path": "$outer.valid" }
            ]
          }
        },
        {
          "typeId": "rectangular-tube",
          "bindings": {
            "outer": { "op": "fit.rectangle", "args": ["outer"] }
          },
          "match": {
            "op": "all",
            "args": [
              { "op": "equals", "args": [
                { "path": "section.cavityCount" }, 1
              ] },
              { "path": "$outer.valid" }
            ]
          },
          "parameters": {
            "width": { "path": "$outer.width" },
            "height": { "path": "$outer.height" }
          },
          "placement": {
            "origin": { "path": "$outer.center" },
            "xDirection": { "path": "$outer.longAxis" }
          }
        }
      ]
    })json";

    const auto _Loaded = _Service.ParseDefinitionsJSON(_JSON);
    ASSERT_TRUE(_Loaded.bValid);
    ASSERT_EQ(2u, _Loaded.Definitions.size());
    EXPECT_EQ("solid-rectangle", _Loaded.Definitions[0].TypeID);
    EXPECT_EQ("rectangular-tube", _Loaded.Definitions[1].TypeID);

    const auto _Result = _Service.Recognize(
        MakeRectangularTube(),
        _Loaded.Definitions);
    ASSERT_TRUE(_Result.IsOK());
    EXPECT_EQ("rectangular-tube", _Result.SectionTypeID);
}

TEST(ExtrusionRecognitionTest, RecognizesAllTubeOneStepRegressionSamples)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();
    const auto _Definitions = _Service.ParseDefinitionsJSON(kSampleDefinitions);
    ASSERT_TRUE(_Definitions.bValid);

    const std::array<std::pair<const char*, const char*>, 9> _Cases{ {
        { "01_round_tube_plain.step", "tube.circular.hollow" },
        { "02_double_cavity_rectangular_tube.step", "tube.rectangular.multi-cavity" },
        { "03_round_tube_through_hole.step", "tube.circular.hollow" },
        { "04_round_tube_oblique_hole.step", "tube.circular.hollow" },
        { "05_double_cavity_blind_hole.step", "tube.rectangular.multi-cavity" },
        { "06_opened_chamber_internal_wall_hole.step", "tube.rectangular.multi-cavity" },
        { "07_round_tube_stepped_uv_end.step", "tube.circular.hollow" },
        { "08_double_cavity_beveled_hole.step", "tube.rectangular.multi-cavity" },
        { "09_double_cavity_mitered_ends.step", "tube.rectangular.multi-cavity" }
    } };

    for (const auto& [_FileName, _ExpectedType] : _Cases)
    {
        SCOPED_TRACE(_FileName);
        const auto _Path = RepositoryRoot() / "samples" / "tube-one" / _FileName;
        const auto _Read = iCAX::OpenCascade::ReadBRepFile(
            _Path.string(),
            0.001);
        ASSERT_TRUE(_Read.bOK);
        const auto _Recognition = _Service.Recognize(
            _Read.Geometry,
            _Definitions.Definitions);
        std::string _Diagnostics;
        for (const auto& _Item : _Recognition.Diagnostics)
        {
            if (!_Diagnostics.empty()) _Diagnostics += " | ";
            _Diagnostics += _Item;
        }
        _Diagnostics += " | cavities="
            + std::to_string(_Recognition.Section.nCavityCount)
            + " loops=" + std::to_string(_Recognition.Section.Loops.size());
        for (const auto& _Loop : _Recognition.Section.Loops)
        {
            _Diagnostics += " " + _Loop.ID + "="
                + std::to_string(_Loop.Points.size());
        }
        ASSERT_TRUE(_Recognition.IsOK()) << _Diagnostics;
        EXPECT_EQ(_ExpectedType, _Recognition.SectionTypeID);
        EXPECT_GT(_Recognition.dLength, 0.0);
    }
}

TEST(ExtrusionRecognitionTest, RebuildsNormalizedResourceForOnDemandCadIntent)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();
    const auto _Definitions = _Service.ParseDefinitionsJSON(kSampleDefinitions);
    ASSERT_TRUE(_Definitions.bValid);

    const auto _Path = RepositoryRoot()
        / "samples" / "tube-one" / "01_round_tube_plain.step";
    STEPControl_Reader _DirectReader;
    ASSERT_EQ(IFSelect_RetDone, _DirectReader.ReadFile(_Path.string().c_str()));
    ASSERT_GT(_DirectReader.TransferRoots(), 0);
    const auto _DirectIntent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _DirectReader.OneShape());
    std::string _DirectDiagnostics;
    for (const auto& _Item : _DirectIntent.Geometry.Diagnostics)
        _DirectDiagnostics += _Item.Code + ": " + _Item.Message + " | ";
    ASSERT_NE(
        iCAX::GeometryData::Tube::ERecognitionStatus::Failed,
        _DirectIntent.Geometry.RecognitionStatus) << _DirectDiagnostics;
    const auto _Read = iCAX::OpenCascade::ReadBRepFile(_Path.string(), 0.001);
    ASSERT_TRUE(_Read.bOK);
    const auto _Recognition = _Service.Recognize(
        _Read.Geometry,
        _Definitions.Definitions);
    ASSERT_TRUE(_Recognition.IsOK());

    const auto _UnnormalizedRebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(
        _Read.Geometry);
    ASSERT_TRUE(_UnnormalizedRebuilt.bOK);
    const auto _UnnormalizedIntent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _UnnormalizedRebuilt.Shape);
    std::string _UnnormalizedDiagnostics;
    for (const auto& _Item : _UnnormalizedIntent.Geometry.Diagnostics)
        _UnnormalizedDiagnostics += _Item.Code + ": " + _Item.Message + " | ";
    EXPECT_NE(
        iCAX::GeometryData::Tube::ERecognitionStatus::Failed,
        _UnnormalizedIntent.Geometry.RecognitionStatus) << _UnnormalizedDiagnostics;

    const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(
        _Recognition.NormalizedGeometry);
    std::string _Diagnostics;
    for (const auto& _Item : _Rebuilt.Diagnostics)
    {
        if (!_Diagnostics.empty()) _Diagnostics += " | ";
        _Diagnostics += _Item;
    }
    ASSERT_TRUE(_Rebuilt.bOK) << _Diagnostics;

    const auto _Intent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _Rebuilt.Shape);
    for (const auto& _Item : _Intent.Geometry.Diagnostics)
    {
        if (!_Diagnostics.empty()) _Diagnostics += " | ";
        _Diagnostics += _Item.Code + ": " + _Item.Message;
    }
    EXPECT_NE(
        iCAX::GeometryData::Tube::ERecognitionStatus::Failed,
        _Intent.Geometry.RecognitionStatus) << _Diagnostics;
    EXPECT_FALSE(_Intent.Geometry.SolidNodes.empty()) << _Diagnostics;
}

TEST(ExtrusionRecognitionTest, RebuildsNormalizedMultiCavityResourceForCadIntent)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();
    const auto _Definitions = _Service.ParseDefinitionsJSON(kSampleDefinitions);
    ASSERT_TRUE(_Definitions.bValid);
    const auto _Path = RepositoryRoot()
        / "samples" / "tube-one" / "02_double_cavity_rectangular_tube.step";
    STEPControl_Reader _DirectReader;
    ASSERT_EQ(IFSelect_RetDone, _DirectReader.ReadFile(_Path.string().c_str()));
    ASSERT_GT(_DirectReader.TransferRoots(), 0);
    const auto _DirectIntent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _DirectReader.OneShape());
    std::string _DirectDiagnostics;
    for (const auto& _Item : _DirectIntent.Geometry.Diagnostics)
        _DirectDiagnostics += _Item.Code + ": " + _Item.Message + " | ";
    ASSERT_NE(
        iCAX::GeometryData::Tube::ERecognitionStatus::Failed,
        _DirectIntent.Geometry.RecognitionStatus) << _DirectDiagnostics;
    const auto _Read = iCAX::OpenCascade::ReadBRepFile(_Path.string(), 0.001);
    ASSERT_TRUE(_Read.bOK);
    const auto _Recognition = _Service.Recognize(
        _Read.Geometry,
        _Definitions.Definitions);
    ASSERT_TRUE(_Recognition.IsOK());
    ASSERT_EQ(2u, _Recognition.Section.nCavityCount);

    const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(
        _Recognition.NormalizedGeometry);
    ASSERT_TRUE(_Rebuilt.bOK);
    EXPECT_TRUE(_Rebuilt.Diagnostics.empty());
    const auto _Intent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _Rebuilt.Shape);
    std::string _Diagnostics;
    GProp_GProps _DirectMass;
    GProp_GProps _RebuiltMass;
    BRepGProp::VolumeProperties(_DirectReader.OneShape(), _DirectMass);
    BRepGProp::VolumeProperties(_Rebuilt.Shape, _RebuiltMass);
    _Diagnostics += "directVolume=" + std::to_string(_DirectMass.Mass())
        + " rebuiltVolume=" + std::to_string(_RebuiltMass.Mass()) + " | ";
    std::size_t _ForwardFaces = 0;
    std::size_t _ReversedFaces = 0;
    for (const auto& _Shell : _Recognition.NormalizedGeometry.Shells)
    {
        for (const auto _Orientation : _Shell.FaceOrientations)
        {
            if (_Orientation == ETopologyOrientation::Reversed) ++_ReversedFaces;
            else if (_Orientation == ETopologyOrientation::Forward) ++_ForwardFaces;
        }
    }
    _Diagnostics += "shellFaceOrientation(F/R)="
        + std::to_string(_ForwardFaces) + "/" + std::to_string(_ReversedFaces) + " | ";
    for (const auto& _Solid : _Recognition.NormalizedGeometry.Solids)
    {
        _Diagnostics += "solidShellOrientations=";
        for (const auto _Orientation : _Solid.ShellOrientations)
            _Diagnostics += std::to_string(static_cast<int>(_Orientation)) + ",";
        _Diagnostics += " | ";
    }
    for (const auto& _Item : _Intent.Geometry.Diagnostics)
        _Diagnostics += _Item.Code + ": " + _Item.Message + " | ";
    EXPECT_NE(
        iCAX::GeometryData::Tube::ERecognitionStatus::Failed,
        _Intent.Geometry.RecognitionStatus) << _Diagnostics;
    EXPECT_FALSE(_Intent.Geometry.SolidNodes.empty()) << _Diagnostics;
}

TEST(ExtrusionRecognitionTest, PreservesVolumeWhenNormalizingMultiCavityBeveledPart)
{
    CExtrusionRecognitionService _Service;
    _Service.OnLoad();
    const auto _Definitions = _Service.ParseDefinitionsJSON(kSampleDefinitions);
    ASSERT_TRUE(_Definitions.bValid);
    const auto _Path = RepositoryRoot()
        / "samples" / "tube-one" / "08_double_cavity_beveled_hole.step";

    STEPControl_Reader _DirectReader;
    ASSERT_EQ(IFSelect_RetDone, _DirectReader.ReadFile(_Path.string().c_str()));
    ASSERT_GT(_DirectReader.TransferRoots(), 0);
    const auto _Read = iCAX::OpenCascade::ReadBRepFile(_Path.string(), 0.001);
    ASSERT_TRUE(_Read.bOK);
    const auto _Recognition = _Service.Recognize(
        _Read.Geometry,
        _Definitions.Definitions);
    ASSERT_TRUE(_Recognition.IsOK());
    const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(
        _Recognition.NormalizedGeometry);
    ASSERT_TRUE(_Rebuilt.bOK);
    EXPECT_TRUE(_Rebuilt.Diagnostics.empty());

    GProp_GProps _DirectMass;
    GProp_GProps _RebuiltMass;
    BRepGProp::VolumeProperties(_DirectReader.OneShape(), _DirectMass);
    BRepGProp::VolumeProperties(_Rebuilt.Shape, _RebuiltMass);
    ASSERT_GT(std::abs(_DirectMass.Mass()), 1.0);
    EXPECT_NEAR(
        std::abs(_DirectMass.Mass()),
        std::abs(_RebuiltMass.Mass()),
        std::abs(_DirectMass.Mass()) * 1.0e-6);

    const auto _Intent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _Rebuilt.Shape);
    const auto _DirectIntent = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _DirectReader.OneShape());
    GProp_GProps _BaseMass;
    GProp_GProps _RemovalMass;
    GProp_GProps _AdditionMass;
    BRepGProp::VolumeProperties(_Intent.BaseShape, _BaseMass);
    BRepGProp::VolumeProperties(_Intent.RemovalShape, _RemovalMass);
    BRepGProp::VolumeProperties(_Intent.AdditionShape, _AdditionMass);
    GProp_GProps _DirectBaseMass;
    GProp_GProps _DirectRemovalMass;
    GProp_GProps _DirectAdditionMass;
    BRepGProp::VolumeProperties(_DirectIntent.BaseShape, _DirectBaseMass);
    BRepGProp::VolumeProperties(_DirectIntent.RemovalShape, _DirectRemovalMass);
    BRepGProp::VolumeProperties(_DirectIntent.AdditionShape, _DirectAdditionMass);
    const auto _Volumes = "direct=" + std::to_string(_DirectMass.Mass())
        + " rebuilt=" + std::to_string(_RebuiltMass.Mass())
        + " base=" + std::to_string(_BaseMass.Mass())
        + " removal=" + std::to_string(_RemovalMass.Mass())
        + " addition=" + std::to_string(_AdditionMass.Mass())
        + " directBase=" + std::to_string(_DirectBaseMass.Mass())
        + " directRemoval=" + std::to_string(_DirectRemovalMass.Mass())
        + " directAddition=" + std::to_string(_DirectAdditionMass.Mass())
        + " rebuildDiagnostics=" + [&_Rebuilt]() {
            std::string _Text;
            for (const auto& _Item : _Rebuilt.Diagnostics) _Text += _Item + "|";
            return _Text;
        }();
    EXPECT_LT(_Intent.Geometry.RelativeOpaqueResidualVolume, 1.0e-6) << _Volumes;
    EXPECT_LT(_Intent.Geometry.RelativeVolumeError, 1.0e-6) << _Volumes;
}
