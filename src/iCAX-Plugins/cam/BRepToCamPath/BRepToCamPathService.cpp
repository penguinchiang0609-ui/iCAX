#include "BRepToCamPathService.h"
#include "Data/uuid.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "Services/ServicesHelper.h"
#include <etp/Analyzer.hxx>
#include <GeometryUtils.hxx>
#include <SectionRegion.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <set>
#include <cmath>
#include <stdexcept>

namespace iCAX::CAM
{
    namespace
    {
        std::vector<gp_Pnt> SampleContourEdge(const TopoDS_Edge& Edge_, const etp::AnalyzerOptions& Options_)
        {
            BRepAdaptor_Curve _Curve(Edge_);
            std::vector<gp_Pnt> _Points;
            if (_Curve.GetType() == GeomAbs_Line)
                _Points = { _Curve.Value(_Curve.FirstParameter()), _Curve.Value(_Curve.LastParameter()) };
            else
            {
                // Preserve corners between edges and refine curved spans using
                // OCCT deflection sampling; do not replace a whole loop by a
                // fixed-count polygon which can cut across rectangular corners.
                GCPnts_QuasiUniformDeflection _Samples(_Curve, Options_.DistanceTolerance, GeomAbs_C0);
                if (!_Samples.IsDone() || _Samples.NbPoints() < 2 || _Samples.NbPoints() > 500000)
                    throw std::runtime_error("刀路曲线采样失败或点数超限");
                _Points.reserve(_Samples.NbPoints());
                for (int _Index = 1; _Index <= _Samples.NbPoints(); ++_Index)
                    _Points.push_back(_Curve.Value(_Samples.Parameter(_Index)));
            }
            if (Edge_.Orientation() == TopAbs_REVERSED) std::reverse(_Points.begin(), _Points.end());
            return _Points;
        }
        // The input here is a connected FEATURE-FACE group, never the whole
        // CAD edge set. Shared feature edges and surface seams are not cuts.
        std::vector<iCAX::GeometryData::Polyline3> FeatureContours(
            const etp::FeatureAnalysis& Feature_, const etp::PartAnalysis& Part_,
            const etp::AnalyzerOptions& Options_, bool& Complete_)
        {
            const etp::detail::SectionRegion _Section(Part_.SectionWires, Part_.SectionFrame, Options_);
            std::set<std::size_t> _OuterLoops;
            for (const auto& _Loop : _Section.Loops())
            {
                std::size_t _Depth = 0;
                for (const auto& _Other : _Section.Loops())
                    if (_Other.WireIndex != _Loop.WireIndex && !_Loop.Points.empty()
                        && etp::detail::PointInPolygonEvenOdd(_Loop.Points.front(), _Other.Points)) ++_Depth;
                if (_Depth % 2 == 0) _OuterLoops.insert(_Loop.WireIndex);
            }
            auto _BoundaryLoop = [&](const gp_Pnt& _Point) -> std::optional<std::size_t> {
                double _Best = Options_.DistanceTolerance * 5.0;
                std::optional<std::size_t> _Found;
                // Exact OCCT distance avoids misclassifying circular skins
                // against chord approximations of the recovered section.
                for (std::size_t _Index = 0; _Index < Part_.SectionWires.size(); ++_Index)
                {
                    TopoDS_Vertex _First, _Last;
                    TopExp::Vertices(Part_.SectionWires[_Index], _First, _Last);
                    if (_First.IsNull()) continue;
                    const auto _Station = etp::detail::AxialStation(BRep_Tool::Pnt(_First), Part_.SectionFrame);
                    const auto _Projected = etp::detail::FromSection2d(
                        etp::detail::ToSection2d(_Point, Part_.SectionFrame), _Station, Part_.SectionFrame);
                    const auto _Vertex = BRepBuilderAPI_MakeVertex(_Projected).Vertex();
                    BRepExtrema_DistShapeShape _Distance(_Vertex, Part_.SectionWires[_Index]);
                    if (_Distance.IsDone() && _Distance.Value() <= _Best)
                    { _Best = _Distance.Value(); _Found = _Index; }
                }
                return _Found;
            };
            TopTools_IndexedDataMapOfShapeListOfShape _EdgeFaces;
            TopExp::MapShapesAndAncestors(Feature_.Shape, TopAbs_EDGE, TopAbs_FACE, _EdgeFaces);
            occ::handle<NCollection_HSequence<TopoDS_Shape>> _Edges = new NCollection_HSequence<TopoDS_Shape>();
            Complete_ = true;
            for (int _Index = 1; _Index <= _EdgeFaces.Extent(); ++_Index)
            {
                const auto _Edge = TopoDS::Edge(_EdgeFaces.FindKey(_Index));
                TopTools_IndexedMapOfShape _Faces;
                for (TopTools_ListOfShape::Iterator _It(_EdgeFaces.FindFromIndex(_Index)); _It.More(); _It.Next())
                    _Faces.Add(_It.Value());
                if (BRep_Tool::Degenerated(_Edge) || _Faces.Extent() != 1) continue;
                if (BRep_Tool::IsClosed(_Edge, TopoDS::Face(_Faces(1)))) continue;
                const auto _Samples = etp::detail::SampleEdge(_Edge, Options_.MinimumEdgeSamples, Options_.SamplingDeflection);
                bool _AllOuter = !_Samples.empty(), _AllInner = !_Samples.empty();
                for (const auto& _Point : _Samples)
                {
                    const auto _Loop = _BoundaryLoop(_Point);
                    _AllOuter = _AllOuter && _Loop && _OuterLoops.contains(*_Loop);
                    _AllInner = _AllInner && _Loop && !_OuterLoops.contains(*_Loop);
                }
                if (_AllOuter) _Edges->Append(_Edge);
                else if (!_AllInner) Complete_ = false;
            }
            // Join only shared topology already established by feature sewing.
            // No nearest-edge bridge across separate holes/parts is introduced.
            const auto _Wires = ShapeAnalysis_FreeBounds::ConnectEdgesToWires(_Edges, Options_.DistanceTolerance, true);
            std::vector<iCAX::GeometryData::Polyline3> _Result;
            for (int _Index = 1; _Index <= _Wires->Length(); ++_Index)
            {
                iCAX::GeometryData::Polyline3 _Curve;
                for (BRepTools_WireExplorer _It(TopoDS::Wire(_Wires->Value(_Index))); _It.More(); _It.Next())
                {
                    const auto _Samples = SampleContourEdge(TopoDS::Edge(_It.Current()), Options_);
                    for (const auto& _Sample : _Samples)
                    {
                        const auto _Point = _Sample.Transformed(Part_.NormalizedToSource);
                        if (!_Curve.Points.empty())
                        {
                            const auto& _Last = _Curve.Points.back();
                            if (_Point.Distance(gp_Pnt(_Last.X, _Last.Y, _Last.Z)) <= Options_.DistanceTolerance) continue;
                        }
                        _Curve.Points.push_back({ _Point.X(), _Point.Y(), _Point.Z() });
                    }
                }
                if (_Curve.Points.size() < 2) { Complete_ = false; continue; }
                const auto& _First = _Curve.Points.front();
                const auto& _Last = _Curve.Points.back();
                _Curve.Closed = gp_Pnt(_First.X, _First.Y, _First.Z).Distance(gp_Pnt(_Last.X, _Last.Y, _Last.Z)) <= Options_.DistanceTolerance;
                if (_Curve.Closed) _Curve.Points.pop_back();
                // With only exterior free edges, an open chain means a feature
                // was not completely classified. Keep it editable but report it.
                if (!_Curve.Closed) Complete_ = false;
                _Result.push_back(std::move(_Curve));
            }
            if (_Result.empty()) Complete_ = false;
            return _Result;
        }
    }
    class COpenCascadeBRepToCamPathService final : public IBRepToCamPathService
    {
        AUTO_REGIST_SERVICE(iCAX::CAM::IBRepToCamPathService, COpenCascadeBRepToCamPathService)
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        CamPathAnalysis Analyze(const iCAX::GeometryData::BRepModel& BRep_, const BRepToCamPathOptions& Options_) const override
        {
            if (!std::isfinite(Options_.DistanceTolerance) || Options_.DistanceTolerance <= 0
                || Options_.SamplesPerCurve < 3 || Options_.SamplesPerCurve > 4096)
                throw std::invalid_argument("Invalid BRep-to-CamPath options");
            const auto _Built = iCAX::OpenCascade::BuildOpenCascadeShape(BRep_);
            if (!_Built.bOK || _Built.Shape.IsNull()) throw std::invalid_argument("Cannot build input BRep");
            etp::AnalyzerOptions _Options;
            _Options.DistanceTolerance = Options_.DistanceTolerance;
            _Options.ToolpathSamples = Options_.SamplesPerCurve;
            const auto _Parts = etp::ExtrusionToolpathAnalyzer(_Options).AnalyzeAssembly(_Built.Shape);
            CamPathAnalysis _Result;
            auto _Diagnostic = [&](const etp::Diagnostic& _Item, const std::string& _PartID) {
                if (_Item.Severity == etp::DiagnosticSeverity::Info) return;
                _Result.Complete = false;
                _Result.Diagnostics.push_back({ _PartID, _Item.Code, _Item.Message, _Item.Severity == etp::DiagnosticSeverity::Error });
            };
            for (const auto& _Part : _Parts)
            {
                for (const auto& _Item : _Part.Diagnostics) _Diagnostic(_Item, _Part.Id);
                for (const auto& _Feature : _Part.Features)
                {
                    bool _Complete = false;
                    auto _Contours = FeatureContours(_Feature, _Part, _Options, _Complete);
                    if (!_Contours.empty())
                    {
                        for (auto& _Curve : _Contours)
                        {
                            CamPath _Output;
                            _Output.ID = iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
                            _Output.Name = "刀路 " + std::to_string(_Result.Paths.size() + 1);
                            _Output.SourcePart = _Part.Id;
                            _Output.SourceFeature = _Feature.Id;
                            _Output.Curve.Closed = _Curve.Closed;
                            iCAX::GeometryData::CurveSegment3 _Segment;
                            _Segment.Curve = std::move(_Curve);
                            _Output.Curve.Segments.push_back(std::move(_Segment));
                            _Result.Paths.push_back(std::move(_Output));
                        }
                        if (!_Complete)
                        {
                            _Result.Complete = false;
                            _Result.Diagnostics.push_back({ _Part.Id, "feature.partial-contour",
                                "特征面存在未识别边界或开口刀路，请检查后编辑。", false });
                        }
                        // Nominal geometry does not depend on the later tool
                        // posture/ruling solver, and must not duplicate its rails.
                        continue;
                    }
                    for (const auto& _Item : _Feature.Diagnostics) _Diagnostic(_Item, _Part.Id);
                    for (const auto& _Path : _Feature.Toolpaths)
                    {
                        if (_Path.Poses.size() < 2) continue;
                        CamPath _Output;
                        _Output.ID = iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
                        _Output.Name = "刀路 " + std::to_string(_Result.Paths.size() + 1);
                        _Output.SourcePart = _Part.Id;
                        _Output.SourceFeature = _Feature.Id;
                        iCAX::GeometryData::Polyline3 _Curve;
                        _Curve.Closed = _Path.Closed;
                        for (const auto& _Pose : _Path.Poses)
                        {
                            const auto _Point = _Pose.Position.Transformed(_Part.NormalizedToSource);
                            const auto _Direction = _Pose.ToolDirection.Transformed(_Part.NormalizedToSource);
                            const auto _Normal = _Pose.SurfaceNormal.Transformed(_Part.NormalizedToSource);
                            _Curve.Points.push_back({ _Point.X(), _Point.Y(), _Point.Z() });
                            _Output.Poses.push_back({ { _Direction.X(), _Direction.Y(), _Direction.Z() }, { _Normal.X(), _Normal.Y(), _Normal.Z() } });
                        }
                        _Output.Curve.Closed = _Curve.Closed;
                        iCAX::GeometryData::CurveSegment3 _Segment;
                        _Segment.Curve = std::move(_Curve);
                        _Output.Curve.Segments.push_back(std::move(_Segment));
                        _Result.Paths.push_back(std::move(_Output));
                    }
                }
            }
            if (_Result.Paths.empty()) _Result.Complete = false;
            return _Result;
        }
    };
}
