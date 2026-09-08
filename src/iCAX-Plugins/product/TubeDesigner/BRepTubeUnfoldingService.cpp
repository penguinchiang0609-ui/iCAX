#include "pch.h"

#include "BRepTubeUnfoldingService.h"
#include "FinalGeometryMeasurement.h"

#include "OpenCascadeResourceImport/OpenCascadeBRepBuilder.h"
#include "Services/ServicesHelper.h"

#include <etp/Analyzer.hxx>

#include <BRepAlgoAPI_Section.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Bnd_Box.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <GProp_GProps.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <NCollection_HSequence.hxx>
#include <Precision.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Shape.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <gp.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace iCAX::TubeDesigner
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;

    namespace
    {
        struct SNormalizedTube final
        {
            TopoDS_Shape Shape;
            gp_Ax3 Frame;
            gp_Dir SourceAxis = gp::DX();
            double First = 0.0;
            double Last = 0.0;
            bool UsedAnalyzer = false;
            gp_Trsf NormalizedToSource;
            bool HasNormalizedToSource = false;
        };

        struct SProfile final
        {
            double Station = 0.0;
            double Area = 0.0;
            double Period = 0.0;
            std::vector<gp_Pnt> Points;
            std::vector<double> U;
        };

        struct SProfileLanes final
        {
            // Lane 0 is the largest section loop (the outside surface); the
            // following lanes are inner loops/cavities in descending area.
            std::vector<std::vector<SProfile>> Lanes;
        };

        struct SPointProjection final
        {
            bool Valid = false;
            double U = 0.0;
            double Residual = (std::numeric_limits<double>::max)();
        };

        struct SMappedWire final
        {
            bool Valid = false;
            std::size_t Lane = 0;
            double MaximumResidual = 0.0;
            std::vector<STubeUnfoldedPoint> Points;
        };

        struct SFaceEvidence final
        {
            TopoDS_Face Face;
            std::size_t Index = 0;
            bool Side = false;
            double Coverage = 0.0;
            double AxialSpan = 0.0;
            double AxialMiddle = 0.0;
        };

        bool IsFinite(const double Value_)
        {
            return std::isfinite(Value_);
        }

        bool IsFinitePoint(const gp_Pnt& Point_)
        {
            return IsFinite(Point_.X()) && IsFinite(Point_.Y()) && IsFinite(Point_.Z());
        }

        double AxialStation(const gp_Pnt& Point_, const gp_Ax3& Frame_)
        {
            return gp_Vec(Frame_.Location(), Point_).Dot(gp_Vec(Frame_.Direction()));
        }

        std::array<double, 3> DirectionArray(const gp_Dir& Direction_)
        {
            return { Direction_.X(), Direction_.Y(), Direction_.Z() };
        }

        gp_Dir TransformDirection(const gp_Dir& Direction_, const gp_Trsf& Transform_)
        {
            gp_Vec _Vector(Direction_);
            _Vector.Transform(Transform_);
            if (_Vector.SquareMagnitude() <= Precision::SquareConfusion())
                return gp::DX();
            return gp_Dir(_Vector);
        }

        std::vector<gp_Pnt> SampleEdge(
            const TopoDS_Edge& Edge_,
            const STubeBRepUnfoldingOptions& Options_,
            const bool RespectOrientation_ = true)
        {
            std::vector<gp_Pnt> _Points;
            if (Edge_.IsNull()) return _Points;
            try
            {
                BRepAdaptor_Curve _Curve(Edge_);
                const auto _First = _Curve.FirstParameter();
                const auto _Last = _Curve.LastParameter();
                if (!IsFinite(_First) || !IsFinite(_Last) || _Last < _First)
                    return _Points;
                const auto _IsLine = _Curve.GetType() == GeomAbs_Line;
                const auto _Count = _IsLine
                    ? std::size_t{ 2 }
                    : std::max<std::size_t>(9, Options_.SamplesPerCurve);
                _Points.reserve(_Count);
                for (std::size_t _Index = 0; _Index < _Count; ++_Index)
                {
                    const auto _Ratio = _Count <= 1
                        ? 0.0
                        : static_cast<double>(_Index)
                            / static_cast<double>(_Count - 1);
                    gp_Pnt _Point;
                    _Curve.D0(_First + (_Last - _First) * _Ratio, _Point);
                    if (IsFinitePoint(_Point)) _Points.push_back(_Point);
                }
                if (RespectOrientation_ && Edge_.Orientation() == TopAbs_REVERSED)
                    std::reverse(_Points.begin(), _Points.end());
            }
            catch (const Standard_Failure&)
            {
                _Points.clear();
            }
            return _Points;
        }

        std::vector<gp_Pnt> SampleWire(
            const TopoDS_Wire& Wire_,
            const STubeBRepUnfoldingOptions& Options_)
        {
            std::vector<gp_Pnt> _Points;
            if (Wire_.IsNull()) return _Points;
            for (BRepTools_WireExplorer _Explorer(Wire_);
                _Explorer.More(); _Explorer.Next())
            {
                auto _EdgePoints = SampleEdge(
                    TopoDS::Edge(_Explorer.Current()), Options_, true);
                if (_EdgePoints.empty()) continue;
                if (!_Points.empty()
                    && _Points.back().Distance(_EdgePoints.front())
                        <= Options_.DistanceTolerance * 5.0)
                {
                    _EdgePoints.erase(_EdgePoints.begin());
                }
                _Points.insert(_Points.end(), _EdgePoints.begin(), _EdgePoints.end());
            }
            if (_Points.size() > 1
                && _Points.front().Distance(_Points.back())
                    <= Options_.DistanceTolerance * 5.0)
            {
                _Points.pop_back();
            }
            return _Points;
        }

        std::vector<TopoDS_Wire> SectionWiresAt(
            const TopoDS_Shape& Shape_,
            const gp_Ax3& Frame_,
            const double Station_,
            const STubeBRepUnfoldingOptions& Options_,
            const bool RequireClosed_ = true)
        {
            std::vector<TopoDS_Wire> _Result;
            if (Shape_.IsNull()) return _Result;
            try
            {
                const auto _Origin = Frame_.Location().Translated(
                    gp_Vec(Frame_.Direction()) * Station_);
                const gp_Pln _Plane(_Origin, Frame_.Direction());
                BRepAlgoAPI_Section _Section(Shape_, _Plane, false);
                _Section.Approximation(false);
                _Section.Build();
                if (!_Section.IsDone() || _Section.Shape().IsNull()) return _Result;

                occ::handle<NCollection_HSequence<TopoDS_Shape>> _Edges =
                    new NCollection_HSequence<TopoDS_Shape>();
                for (TopExp_Explorer _Explorer(_Section.Shape(), TopAbs_EDGE);
                    _Explorer.More(); _Explorer.Next())
                {
                    _Edges->Append(_Explorer.Current());
                }
                if (_Edges->IsEmpty()) return _Result;
                const auto _Wires = ShapeAnalysis_FreeBounds::ConnectEdgesToWires(
                    _Edges, Options_.DistanceTolerance * 10.0, false);
                if (_Wires.IsNull()) return _Result;
                for (int _Index = 1; _Index <= _Wires->Length(); ++_Index)
                {
                    const auto& _Candidate = _Wires->Value(_Index);
                    if (_Candidate.ShapeType() != TopAbs_WIRE) continue;
                    auto _Wire = TopoDS::Wire(_Candidate);
                    const auto _Points = SampleWire(_Wire, Options_);
                    TopoDS_Vertex _FirstVertex, _LastVertex;
                    TopExp::Vertices(_Wire, _FirstVertex, _LastVertex);
                    const bool _Closed = _Wire.Closed()
                        || (!_FirstVertex.IsNull() && !_LastVertex.IsNull()
                            && BRep_Tool::Pnt(_FirstVertex).Distance(
                                BRep_Tool::Pnt(_LastVertex))
                                <= Options_.DistanceTolerance * 20.0);
                    if (_Points.size() < 2 || (RequireClosed_ && !_Closed)) continue;
                    _Result.push_back(std::move(_Wire));
                }
            }
            catch (const Standard_Failure&)
            {
                _Result.clear();
            }
            return _Result;
        }

        double SignedSectionArea(
            const std::vector<gp_Pnt>& Points_,
            const gp_Ax3& Frame_)
        {
            if (Points_.size() < 3) return 0.0;
            double _Area = 0.0;
            for (std::size_t _Index = 0; _Index < Points_.size(); ++_Index)
            {
                const auto& _Left = Points_[_Index];
                const auto& _Right = Points_[(_Index + 1) % Points_.size()];
                const gp_Vec _LeftOffset(Frame_.Location(), _Left);
                const gp_Vec _RightOffset(Frame_.Location(), _Right);
                const auto _LeftX = _LeftOffset.Dot(gp_Vec(Frame_.XDirection()));
                const auto _LeftY = _LeftOffset.Dot(gp_Vec(Frame_.YDirection()));
                const auto _RightX = _RightOffset.Dot(gp_Vec(Frame_.XDirection()));
                const auto _RightY = _RightOffset.Dot(gp_Vec(Frame_.YDirection()));
                _Area += _LeftX * _RightY - _RightX * _LeftY;
            }
            return 0.5 * _Area;
        }

        SProfile MakeProfile(
            const TopoDS_Wire& Wire_,
            const gp_Ax3& Frame_,
            const double Station_,
            const STubeBRepUnfoldingOptions& Options_)
        {
            SProfile _Profile;
            _Profile.Station = Station_;
            _Profile.Points = SampleWire(Wire_, Options_);
            if (_Profile.Points.size() < 3) return _Profile;

            _Profile.Area = SignedSectionArea(_Profile.Points, Frame_);
            if (_Profile.Area < 0.0)
            {
                std::reverse(_Profile.Points.begin(), _Profile.Points.end());
                _Profile.Area = -_Profile.Area;
            }

            const auto _Seam = std::min_element(
                _Profile.Points.begin(), _Profile.Points.end(),
                [&](const gp_Pnt& _Left, const gp_Pnt& _Right)
                {
                    const gp_Vec _LeftOffset(Frame_.Location(), _Left);
                    const gp_Vec _RightOffset(Frame_.Location(), _Right);
                    const auto _LeftX = _LeftOffset.Dot(gp_Vec(Frame_.XDirection()));
                    const auto _RightX = _RightOffset.Dot(gp_Vec(Frame_.XDirection()));
                    if (std::abs(_LeftX - _RightX) > Options_.DistanceTolerance)
                        return _LeftX < _RightX;
                    return _LeftOffset.Dot(gp_Vec(Frame_.YDirection()))
                        < _RightOffset.Dot(gp_Vec(Frame_.YDirection()));
                });
            if (_Seam != _Profile.Points.end())
            {
                std::rotate(
                    _Profile.Points.begin(), _Seam, _Profile.Points.end());
            }

            _Profile.U.assign(_Profile.Points.size(), 0.0);
            for (std::size_t _Index = 1; _Index < _Profile.Points.size(); ++_Index)
            {
                _Profile.U[_Index] = _Profile.U[_Index - 1]
                    + _Profile.Points[_Index - 1].Distance(_Profile.Points[_Index]);
            }
            if (_Profile.Points.size() > 1)
            {
                _Profile.Period = _Profile.U.back()
                    + _Profile.Points.back().Distance(_Profile.Points.front());
            }
            if (_Profile.Period <= Options_.DistanceTolerance) _Profile = {};
            return _Profile;
        }

        SProfileLanes BuildProfiles(
            const TopoDS_Shape& Shape_,
            const gp_Ax3& Frame_,
            const double First_,
            const double Last_,
            const STubeBRepUnfoldingOptions& Options_)
        {
            SProfileLanes _Result;
            const auto _Count = std::max<std::size_t>(5, Options_.StationCount);
            for (std::size_t _Index = 0; _Index < _Count; ++_Index)
            {
                const auto _Fraction = _Count <= 1
                    ? 0.5
                    : 0.05 + 0.90 * static_cast<double>(_Index)
                        / static_cast<double>(_Count - 1);
                const auto _Station = First_ + (Last_ - First_) * _Fraction;
                const auto _Wires = SectionWiresAt(Shape_, Frame_, _Station, Options_);
                std::vector<SProfile> _Profiles;
                for (const auto& _Wire : _Wires)
                {
                    auto _Profile = MakeProfile(
                        _Wire, Frame_, _Station, Options_);
                    if (_Profile.Period > Options_.DistanceTolerance
                        && _Profile.Area > Options_.DistanceTolerance
                            * Options_.DistanceTolerance)
                    {
                        _Profiles.push_back(std::move(_Profile));
                    }
                }
                std::sort(_Profiles.begin(), _Profiles.end(),
                    [](const SProfile& _Left, const SProfile& _Right)
                    { return _Left.Area > _Right.Area; });
                if (_Profiles.empty()) continue;
                if (_Result.Lanes.size() < _Profiles.size())
                    _Result.Lanes.resize(_Profiles.size());
                for (std::size_t _Lane = 0; _Lane < _Profiles.size(); ++_Lane)
                    _Result.Lanes[_Lane].push_back(std::move(_Profiles[_Lane]));
            }
            return _Result;
        }

        double ProfileMismatch(
            const SProfileLanes& Profiles_,
            const gp_Ax3& Frame_)
        {
            if (Profiles_.Lanes.empty() || Profiles_.Lanes.front().size() < 2)
                return 0.0;
            const auto& _Lane = Profiles_.Lanes.front();
            const auto _Reference = std::min_element(
                _Lane.begin(), _Lane.end(),
                [](const SProfile& _Left, const SProfile& _Right)
                { return _Left.Station < _Right.Station; });
            const auto _Count = std::min<std::size_t>(128, std::max<std::size_t>(32, _Reference->Points.size()));
            auto _Sample = [&](const SProfile& _Profile, const std::size_t _Index)
            {
                const auto _Target = _Profile.Period * static_cast<double>(_Index)
                    / static_cast<double>(_Count);
                auto _Upper = std::upper_bound(
                    _Profile.U.begin(), _Profile.U.end(), _Target);
                if (_Upper == _Profile.U.begin()) return _Profile.Points.front();
                const auto _Right = _Upper == _Profile.U.end()
                    ? _Profile.Points.size() - 1
                    : static_cast<std::size_t>(_Upper - _Profile.U.begin() - 1);
                const auto _Next = (_Right + 1) % _Profile.Points.size();
                const auto _Start = _Profile.U[_Right];
                const auto _Span = _Right + 1 < _Profile.U.size()
                    ? _Profile.U[_Right + 1] - _Start
                    : _Profile.Period - _Start;
                const auto _Ratio = _Span <= Precision::Confusion()
                    ? 0.0 : (_Target - _Start) / _Span;
                return _Profile.Points[_Right].Translated(
                    gp_Vec(_Profile.Points[_Right], _Profile.Points[_Next]) * _Ratio);
            };

            double _Maximum = 0.0;
            for (const auto& _Profile : _Lane)
            {
                const auto _CountToCompare = std::min<std::size_t>(
                    _Count, std::min(_Reference->Points.size(), _Profile.Points.size()));
                for (std::size_t _Index = 0; _Index < _CountToCompare; ++_Index)
                {
                    const auto _ReferencePoint = _Sample(*_Reference, _Index);
                    const auto _CurrentPoint = _Sample(_Profile, _Index);
                    // Only the cross-section displacement matters.  The two
                    // profiles already use the same section frame and their
                    // station difference is axial, so compare after removing
                    // that axial component explicitly.
                    const gp_Vec _Offset(_ReferencePoint, _CurrentPoint);
                    const auto _Axial = _Offset.Dot(gp_Vec(Frame_.Direction()));
                    const auto _Residual = (_Offset
                        - gp_Vec(Frame_.Direction()) * _Axial).Magnitude();
                    _Maximum = std::max(_Maximum, _Residual);
                }
            }
            return _Maximum;
        }

        std::optional<gp_Dir> ReadMeasuredAxis(const TopoDS_Shape& Shape_)
        {
            try
            {
                const auto _Measurement = MeasureFinalPartGeometry(Shape_, {}, 0);
                const auto _Reference = _Measurement.find("linearReference");
                if (_Reference == _Measurement.end() || !_Reference->second.Is<ObjectMap>())
                    return std::nullopt;
                const auto& _Map = _Reference->second.To<ObjectMap>();
                const auto _Start = _Map.find("start");
                const auto _End = _Map.find("end");
                if (_Start == _Map.end() || _End == _Map.end()
                    || !_Start->second.Is<VariantArray>() || !_End->second.Is<VariantArray>())
                    return std::nullopt;
                const auto& _StartValues = _Start->second.To<VariantArray>();
                const auto& _EndValues = _End->second.To<VariantArray>();
                if (_StartValues.size() < 3 || _EndValues.size() < 3)
                    return std::nullopt;
                const gp_Vec _Direction(
                    _EndValues[0].To<double>() - _StartValues[0].To<double>(),
                    _EndValues[1].To<double>() - _StartValues[1].To<double>(),
                    _EndValues[2].To<double>() - _StartValues[2].To<double>());
                if (_Direction.SquareMagnitude() <= Precision::SquareConfusion())
                    return std::nullopt;
                return gp_Dir(_Direction);
            }
            catch (const Standard_Failure&)
            {
                return std::nullopt;
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }
        }

        std::optional<SNormalizedTube> TryNormalizeWithAnalyzer(
            const TopoDS_Shape& Shape_,
            const STubeBRepUnfoldingOptions& Options_)
        {
            try
            {
                etp::AnalyzerOptions _AnalyzerOptions;
                _AnalyzerOptions.DistanceTolerance = Options_.DistanceTolerance;
                _AnalyzerOptions.AngularToleranceRadians = Options_.AngularToleranceRadians;
                _AnalyzerOptions.MinimumEdgeSamples = std::max<std::size_t>(
                    5, std::min<std::size_t>(Options_.SamplesPerCurve, 65));
                _AnalyzerOptions.SamplingDeflection = std::max(
                    Options_.DistanceTolerance * 5.0, 0.05);
                _AnalyzerOptions.MaterializeFaceSplits = false;
                etp::ExtrusionToolpathAnalyzer _Analyzer(_AnalyzerOptions);
                const auto _Analysis = _Analyzer.Analyze(Shape_);
                if (_Analysis.Shape.IsNull() || _Analysis.SectionWires.empty())
                    return std::nullopt;
                if (_Analysis.AxialLast - _Analysis.AxialFirst
                        <= Options_.DistanceTolerance)
                    return std::nullopt;
                SNormalizedTube _Result;
                _Result.Shape = _Analysis.Shape;
                _Result.Frame = _Analysis.SectionFrame;
                _Result.First = _Analysis.AxialFirst;
                _Result.Last = _Analysis.AxialLast;
                _Result.SourceAxis = TransformDirection(
                    _Result.Frame.Direction(), _Analysis.NormalizedToSource);
                _Result.UsedAnalyzer = true;
                _Result.NormalizedToSource = _Analysis.NormalizedToSource;
                _Result.HasNormalizedToSource = true;
                return _Result;
            }
            catch (const Standard_Failure&)
            {
                return std::nullopt;
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }
        }

        std::optional<SNormalizedTube> TryNormalizeWithMeasuredFrame(
            const TopoDS_Shape& Shape_,
            const STubeBRepUnfoldingOptions& Options_)
        {
            try
            {
                const auto _Normalized = NormalizeLinearPartForManufacturing(Shape_);
                if (_Normalized.IsNull()) return std::nullopt;
                Bnd_Box _Box;
                BRepBndLib::AddOptimal(_Normalized, _Box, false, false);
                _Box.SetGap(0.0);
                if (_Box.IsVoid() || _Box.IsWhole()) return std::nullopt;
                double _X0, _Y0, _Z0, _X1, _Y1, _Z1;
                _Box.Get(_X0, _Y0, _Z0, _X1, _Y1, _Z1);
                if (!IsFinite(_X0) || !IsFinite(_X1)
                    || _X1 - _X0 <= Options_.DistanceTolerance)
                    return std::nullopt;
                SNormalizedTube _Result;
                _Result.Shape = _Normalized;
                _Result.Frame = gp_Ax3(gp::Origin(), gp::DX(), gp::DY());
                _Result.First = _X0;
                _Result.Last = _X1;
                _Result.SourceAxis = ReadMeasuredAxis(Shape_).value_or(gp::DX());
                _Result.UsedAnalyzer = false;
                return _Result;
            }
            catch (const Standard_Failure&)
            {
                return std::nullopt;
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }
        }

        std::optional<SNormalizedTube> NormalizeTube(
            const TopoDS_Shape& Shape_,
            const STubeBRepUnfoldingOptions& Options_)
        {
            if (auto _Analyzer = TryNormalizeWithAnalyzer(Shape_, Options_))
                return _Analyzer;
            return TryNormalizeWithMeasuredFrame(Shape_, Options_);
        }

        double SectionIntersectionLength(
            const TopoDS_Face& Face_,
            const gp_Ax3& Frame_,
            const double Station_,
            const STubeBRepUnfoldingOptions& Options_)
        {
            const auto _Wires = SectionWiresAt(
                Face_, Frame_, Station_, Options_, false);
            double _Length = 0.0;
            for (const auto& _Wire : _Wires)
            {
                const auto _Points = SampleWire(_Wire, Options_);
                for (std::size_t _Index = 1; _Index < _Points.size(); ++_Index)
                    _Length += _Points[_Index - 1].Distance(_Points[_Index]);
                if (_Points.size() > 2)
                    _Length += _Points.back().Distance(_Points.front());
            }
            return _Length;
        }

        std::pair<double, double> FaceAxialRange(
            const TopoDS_Face& Face_, const gp_Ax3& Frame_)
        {
            double _First = (std::numeric_limits<double>::max)();
            double _Last = (std::numeric_limits<double>::lowest)();
            for (TopExp_Explorer _Explorer(Face_, TopAbs_VERTEX);
                _Explorer.More(); _Explorer.Next())
            {
                const auto _Station = AxialStation(
                    BRep_Tool::Pnt(TopoDS::Vertex(_Explorer.Current())), Frame_);
                _First = std::min(_First, _Station);
                _Last = std::max(_Last, _Station);
            }
            if (_First > _Last) return { 0.0, 0.0 };
            return { _First, _Last };
        }

        std::vector<SFaceEvidence> ClassifyFaces(
            const TopoDS_Shape& Shape_,
            const gp_Ax3& Frame_,
            const double First_,
            const double Last_,
            const STubeBRepUnfoldingOptions& Options_)
        {
            TopTools_IndexedMapOfShape _Faces;
            TopExp::MapShapes(Shape_, TopAbs_FACE, _Faces);
            const auto _ProbeCount = std::max<std::size_t>(5, Options_.StationCount);
            std::vector<double> _Stations;
            _Stations.reserve(_ProbeCount);
            for (std::size_t _Index = 0; _Index < _ProbeCount; ++_Index)
            {
                const auto _Fraction = _ProbeCount <= 1
                    ? 0.5
                    : 0.05 + 0.90 * static_cast<double>(_Index)
                        / static_cast<double>(_ProbeCount - 1);
                _Stations.push_back(First_ + (Last_ - First_) * _Fraction);
            }

            std::vector<SFaceEvidence> _Result;
            _Result.reserve(static_cast<std::size_t>(_Faces.Extent()));
            for (Standard_Integer _Index = 1; _Index <= _Faces.Extent(); ++_Index)
            {
                const auto _Face = TopoDS::Face(_Faces(_Index));
                std::size_t _Hits = 0;
                double _FirstHit = (std::numeric_limits<double>::max)();
                double _LastHit = (std::numeric_limits<double>::lowest)();
                for (const auto _Station : _Stations)
                {
                    if (SectionIntersectionLength(_Face, Frame_, _Station, Options_)
                        <= Options_.DistanceTolerance * 5.0)
                    {
                        continue;
                    }
                    ++_Hits;
                    _FirstHit = std::min(_FirstHit, _Station);
                    _LastHit = std::max(_LastHit, _Station);
                }
                const auto _Range = FaceAxialRange(_Face, Frame_);
                const auto _Span = std::max(0.0, _Range.second - _Range.first);
                const auto _Coverage = _Stations.empty()
                    ? 0.0
                    : static_cast<double>(_Hits)
                        / static_cast<double>(_Stations.size());
                const auto _HitSpan = _Hits > 1 ? _LastHit - _FirstHit : 0.0;
                const auto _Length = Last_ - First_;
                const bool _Side = _Hits >= std::max<std::size_t>(3, _ProbeCount / 2)
                    && _HitSpan >= _Length * 0.30
                    && _Span >= _Length * 0.20;
                _Result.push_back({
                    _Face,
                    static_cast<std::size_t>(_Index - 1),
                    _Side,
                    _Coverage,
                    _Span,
                    _Range.first + _Span * 0.5 });
            }

            if (std::none_of(_Result.begin(), _Result.end(),
                [](const SFaceEvidence& _Face) { return _Face.Side; }))
            {
                // A very short tube or a heavily trimmed end can make the
                // station hit ratio conservative.  The second pass uses only
                // axial span and still does not inspect a native UV system.
                const auto _Length = Last_ - First_;
                for (auto& _Face : _Result)
                {
                    if (_Face.AxialSpan >= _Length * 0.45)
                        _Face.Side = true;
                }
            }
            return _Result;
        }

        SPointProjection ProjectPointToLane(
            const gp_Pnt& Point_,
            const gp_Ax3& Frame_,
            const std::vector<SProfile>& Lane_)
        {
            SPointProjection _Best;
            if (Lane_.empty()) return _Best;
            const auto _Station = AxialStation(Point_, Frame_);
            const auto _Axis = gp_Vec(Frame_.Direction());
            for (const auto& _Profile : Lane_)
            {
                const auto _Shift = (_Station - _Profile.Station);
                const auto _ProfileOffset = _Axis * _Shift;
                for (std::size_t _Index = 0; _Index < _Profile.Points.size(); ++_Index)
                {
                    const auto _Next = (_Index + 1) % _Profile.Points.size();
                    const auto _Left = _Profile.Points[_Index].Translated(_ProfileOffset);
                    const auto _Right = _Profile.Points[_Next].Translated(_ProfileOffset);
                    const gp_Vec _Segment(_Left, _Right);
                    const auto _LengthSquared = _Segment.SquareMagnitude();
                    double _Parameter = 0.0;
                    if (_LengthSquared > Precision::SquareConfusion())
                    {
                        _Parameter = gp_Vec(_Left, Point_).Dot(_Segment) / _LengthSquared;
                        _Parameter = std::clamp(_Parameter, 0.0, 1.0);
                    }
                    const auto _Closest = _Left.Translated(_Segment * _Parameter);
                    const auto _Residual = _Closest.Distance(Point_);
                    if (_Residual >= _Best.Residual) continue;
                    const auto _SegmentLength = _Left.Distance(_Right);
                    _Best.Valid = true;
                    _Best.Residual = _Residual;
                    _Best.U = _Profile.U[_Index] + _SegmentLength * _Parameter;
                    if (_Best.U >= _Profile.Period) _Best.U -= _Profile.Period;
                }
            }
            return _Best;
        }

        std::optional<std::pair<std::size_t, SPointProjection>> ProjectPoint(
            const gp_Pnt& Point_,
            const gp_Ax3& Frame_,
            const SProfileLanes& Profiles_,
            const std::optional<std::size_t> PreferredLane_ = std::nullopt)
        {
            SPointProjection _Best;
            std::size_t _BestLane = 0;
            const auto _Begin = PreferredLane_.value_or(0);
            const auto _End = PreferredLane_
                ? std::min<std::size_t>(*PreferredLane_ + 1, Profiles_.Lanes.size())
                : Profiles_.Lanes.size();
            for (std::size_t _Lane = _Begin; _Lane < _End; ++_Lane)
            {
                auto _Projection = ProjectPointToLane(
                    Point_, Frame_, Profiles_.Lanes[_Lane]);
                if (!_Projection.Valid || _Projection.Residual >= _Best.Residual)
                    continue;
                _Best = _Projection;
                _BestLane = _Lane;
            }
            if (!_Best.Valid) return std::nullopt;
            return std::make_pair(_BestLane, _Best);
        }

        void UnwrapU(
            double& U_,
            const double PreviousU_,
            const double Period_)
        {
            if (Period_ <= Precision::Confusion()) return;
            while (U_ - PreviousU_ > Period_ * 0.5) U_ -= Period_;
            while (PreviousU_ - U_ > Period_ * 0.5) U_ += Period_;
        }

        SMappedWire MapWire(
            const TopoDS_Wire& Wire_,
            const gp_Ax3& Frame_,
            const SProfileLanes& Profiles_,
            const STubeBRepUnfoldingOptions& Options_,
            const std::optional<std::size_t> PreferredLane_ = std::nullopt)
        {
            SMappedWire _Result;
            const auto _Samples = SampleWire(Wire_, Options_);
            if (_Samples.size() < 2) return _Result;

            std::vector<std::pair<std::size_t, SPointProjection>> _Projections;
            _Projections.reserve(_Samples.size());
            std::vector<std::size_t> _LaneCounts(Profiles_.Lanes.size(), 0);
            for (const auto& _Point : _Samples)
            {
                const auto _Projection = ProjectPoint(
                    _Point, Frame_, Profiles_, PreferredLane_);
                if (!_Projection) return _Result;
                _Projections.push_back(*_Projection);
                if (_Projection->first < _LaneCounts.size())
                    ++_LaneCounts[_Projection->first];
            }
            if (_Projections.empty()) return _Result;

            if (!PreferredLane_)
            {
                _Result.Lane = static_cast<std::size_t>(std::distance(
                    _LaneCounts.begin(),
                    std::max_element(_LaneCounts.begin(), _LaneCounts.end())));
                if (_Result.Lane >= Profiles_.Lanes.size()) return _Result;
                _Projections.clear();
                for (const auto& _Point : _Samples)
                {
                    const auto _Projection = ProjectPoint(
                        _Point, Frame_, Profiles_, _Result.Lane);
                    if (!_Projection) return _Result;
                    _Projections.push_back(*_Projection);
                }
            }
            else
            {
                _Result.Lane = *PreferredLane_;
            }

            const auto _Period = _Result.Lane < Profiles_.Lanes.size()
                && !Profiles_.Lanes[_Result.Lane].empty()
                ? Profiles_.Lanes[_Result.Lane].front().Period : 0.0;
            double _PreviousU = 0.0;
            bool _First = true;
            for (std::size_t _Index = 0; _Index < _Samples.size(); ++_Index)
            {
                auto _U = _Projections[_Index].second.U;
                if (!_First) UnwrapU(_U, _PreviousU, _Period);
                _PreviousU = _U;
                _First = false;
                const auto _S = AxialStation(_Samples[_Index], Frame_);
                _Result.Points.push_back({ _S, _U });
                _Result.MaximumResidual = std::max(
                    _Result.MaximumResidual, _Projections[_Index].second.Residual);
            }
            _Result.Valid = _Result.Points.size() >= 2;
            return _Result;
        }

        void NormalizeMappedStation(
            SMappedWire& Mapped_, const double StationOrigin_)
        {
            for (auto& _Point : Mapped_.Points)
                _Point.S -= StationOrigin_;
        }

        STubeUnfoldedWire MakeOutputWire(
            SMappedWire Mapped_,
            const std::size_t SurfaceIndex_,
            const std::size_t FaceIndex_,
            const std::size_t EdgeIndex_,
            const std::string& Role_,
            const bool Closed_,
            const double Period_)
        {
            STubeUnfoldedWire _Result;
            _Result.SurfaceIndex = SurfaceIndex_;
            _Result.SourceFaceIndex = FaceIndex_;
            _Result.SourceEdgeIndex = EdgeIndex_;
            _Result.Role = Role_;
            _Result.Closed = Closed_;
            _Result.Points = std::move(Mapped_.Points);
            if (_Result.Points.size() > 1 && Period_ > Precision::Confusion())
            {
                const auto _Minimum = std::min_element(
                    _Result.Points.begin(), _Result.Points.end(),
                    [](const auto& _Left, const auto& _Right)
                    { return _Left.U < _Right.U; });
                const auto _Maximum = std::max_element(
                    _Result.Points.begin(), _Result.Points.end(),
                    [](const auto& _Left, const auto& _Right)
                    { return _Left.U < _Right.U; });
                _Result.WrapsAroundSeam = _Minimum != _Result.Points.end()
                    && _Maximum != _Result.Points.end()
                    && (_Minimum->U < -Period_ * 0.05
                        || _Maximum->U > Period_ * 1.05
                        || _Maximum->U - _Minimum->U >= Period_ * 0.90);
            }
            return _Result;
        }

        bool IsClosedWire(
            const TopoDS_Wire& Wire_,
            const STubeBRepUnfoldingOptions& Options_)
        {
            const auto _Points = SampleWire(Wire_, Options_);
            return _Points.size() > 2
                && _Points.front().Distance(_Points.back())
                    <= Options_.DistanceTolerance * 20.0;
        }

        iCAX::Data::Variant PointVariant(const STubeUnfoldedPoint& Point_)
        {
            return iCAX::Data::VariantArray{ Point_.S, Point_.U };
        }

        iCAX::Data::ObjectMap SerializeRectangle(
            const STubeUnfoldedRectangle& Rectangle_)
        {
            return {
                { "available", Rectangle_.Available },
                { "sStart", Rectangle_.SStart },
                { "sEnd", Rectangle_.SEnd },
                { "uStart", Rectangle_.UStart },
                { "uEnd", Rectangle_.UEnd },
                { "width", std::max(0.0, Rectangle_.SEnd - Rectangle_.SStart) },
                { "height", std::max(0.0, Rectangle_.UEnd - Rectangle_.UStart) },
                { "periodic", Rectangle_.PeriodicU },
                { "seam", std::string("u=0=uPeriod") }
            };
        }

        iCAX::Data::ObjectMap SerializeWire(const STubeUnfoldedWire& Wire_)
        {
            iCAX::Data::VariantArray _Points;
            _Points.reserve(Wire_.Points.size());
            for (const auto& _Point : Wire_.Points)
                _Points.emplace_back(PointVariant(_Point));
            return {
                { "id", static_cast<unsigned long long>(Wire_.ID) },
                { "surfaceIndex", static_cast<unsigned long long>(Wire_.SurfaceIndex) },
                { "sourceFaceIndex", static_cast<unsigned long long>(Wire_.SourceFaceIndex) },
                { "sourceEdgeIndex", static_cast<unsigned long long>(Wire_.SourceEdgeIndex) },
                { "role", Wire_.Role },
                { "closed", Wire_.Closed },
                { "wrapsAroundSeam", Wire_.WrapsAroundSeam },
                { "points", std::move(_Points) }
            };
        }

        void AssignIDs(STubeBRepUnfoldingResult& Result_)
        {
            std::size_t _WireID = 0;
            std::size_t _PanelID = 0;
            std::size_t _PointCount = 0;
            for (auto& _Surface : Result_.Surfaces)
            {
                _Surface.ID = static_cast<std::size_t>(
                    &_Surface - Result_.Surfaces.data());
                for (auto& _Panel : _Surface.Panels)
                {
                    _Panel.ID = _PanelID++;
                    for (auto& _Wire : _Panel.Boundaries)
                    {
                        _Wire.ID = _WireID++;
                        _PointCount += _Wire.Points.size();
                    }
                }
                for (auto& _Wire : _Surface.Wires)
                {
                    _Wire.ID = _WireID++;
                    _PointCount += _Wire.Points.size();
                }
            }
            Result_.PointCount = _PointCount;
        }
    }

    STubeBRepUnfoldingResult UnfoldTubeBRepSurface(
        IN const TopoDS_Shape& Shape_,
        IN const STubeBRepUnfoldingOptions& Options_)
    {
        STubeBRepUnfoldingResult _Result;
        try
        {
            if (Shape_.IsNull())
            {
                _Result.Diagnostic = "输入 BRep 为空";
                return _Result;
            }
            if (!IsFinite(Options_.DistanceTolerance)
                || Options_.DistanceTolerance <= 0.0
                || !IsFinite(Options_.AngularToleranceRadians)
                || Options_.AngularToleranceRadians <= 0.0
                || Options_.StationCount < 5 || Options_.StationCount > 65
                || Options_.SamplesPerCurve < 9 || Options_.SamplesPerCurve > 513
                || Options_.MaximumOutputPoints < 100)
            {
                throw std::invalid_argument("BRep 展开参数无效");
            }

            const auto _Normalized = NormalizeTube(Shape_, Options_);
            if (!_Normalized)
            {
                _Result.Diagnostic = "无法从 BRep 确定线性轴和截面";
                return _Result;
            }
            if (_Normalized->Last - _Normalized->First <= Options_.DistanceTolerance)
            {
                _Result.Diagnostic = "BRep 没有可展开的轴向长度";
                return _Result;
            }

            const auto _Profiles = BuildProfiles(
                _Normalized->Shape,
                _Normalized->Frame,
                _Normalized->First,
                _Normalized->Last,
                Options_);
            if (_Profiles.Lanes.empty() || _Profiles.Lanes.front().empty())
            {
                _Result.Diagnostic = "无法通过垂直于主轴的虚拟截面恢复闭合轮廓";
                return _Result;
            }

            const auto _Faces = ClassifyFaces(
                _Normalized->Shape,
                _Normalized->Frame,
                _Normalized->First,
                _Normalized->Last,
                Options_);

            _Result.bOK = true;
            _Result.Approximate = !_Normalized->UsedAnalyzer;
            _Result.Periodic = true;
            _Result.Method = _Normalized->UsedAnalyzer
                ? "section-arc-length"
                : "section-arc-length-fallback";
            _Result.Axis = DirectionArray(_Normalized->SourceAxis);
            _Result.SectionX = DirectionArray(_Normalized->Frame.XDirection());
            _Result.SectionY = DirectionArray(_Normalized->Frame.YDirection());
            _Result.Length = _Normalized->Last - _Normalized->First;
            _Result.ProfileStationCount = _Profiles.Lanes.front().size();
            _Result.ProfileMismatch = ProfileMismatch(
                _Profiles, _Normalized->Frame);
            _Result.Approximate = _Result.Approximate
                || _Result.ProfileMismatch > Options_.DistanceTolerance * 10.0;
            _Result.Perimeter = _Profiles.Lanes.front().front().Period;
            _Result.LateralRectangle.Available = _Result.Length
                > Precision::Confusion()
                && _Result.Perimeter > Precision::Confusion();
            _Result.LateralRectangle.SStart = 0.0;
            _Result.LateralRectangle.SEnd = _Result.Length;
            _Result.LateralRectangle.UStart = 0.0;
            _Result.LateralRectangle.UEnd = _Result.Perimeter;
            _Result.LateralRectangle.PeriodicU = true;

            const auto _LaneCount = Options_.IncludeInnerSurfaces
                ? _Profiles.Lanes.size() : std::min<std::size_t>(1, _Profiles.Lanes.size());
            _Result.Surfaces.resize(_LaneCount);
            for (std::size_t _Lane = 0; _Lane < _LaneCount; ++_Lane)
            {
                _Result.Surfaces[_Lane].LoopID = _Lane == 0
                    ? "outer" : "inner-" + std::to_string(_Lane);
                _Result.Surfaces[_Lane].Inner = _Lane != 0;
                _Result.Surfaces[_Lane].UPeriod = !_Profiles.Lanes[_Lane].empty()
                    ? _Profiles.Lanes[_Lane].front().Period : 0.0;
                auto& _Rectangle = _Result.Surfaces[_Lane].Rectangle;
                _Rectangle.Available = _Result.Length > Precision::Confusion()
                    && _Result.Surfaces[_Lane].UPeriod > Precision::Confusion();
                _Rectangle.SStart = 0.0;
                _Rectangle.SEnd = _Result.Length;
                _Rectangle.UStart = 0.0;
                _Rectangle.UEnd = _Result.Surfaces[_Lane].UPeriod;
                _Rectangle.PeriodicU = true;
            }

            std::vector<bool> _SideFaces;
            _SideFaces.resize(_Faces.size(), false);
            for (const auto& _Face : _Faces)
                if (_Face.Index < _SideFaces.size()) _SideFaces[_Face.Index] = _Face.Side;

            for (const auto& _Face : _Faces)
            {
                if (!_Face.Side) continue;
                std::vector<STubeUnfoldedWire> _Boundaries;
                std::optional<std::size_t> _DominantLane;
                std::vector<std::pair<TopoDS_Wire, SMappedWire>> _Mapped;
                for (TopExp_Explorer _Explorer(_Face.Face, TopAbs_WIRE);
                    _Explorer.More(); _Explorer.Next())
                {
                    const auto _Wire = TopoDS::Wire(_Explorer.Current());
                    auto _MappedWire = MapWire(
                        _Wire, _Normalized->Frame, _Profiles, Options_);
                    if (!_MappedWire.Valid) continue;
                    NormalizeMappedStation(_MappedWire, _Normalized->First);
                    _Mapped.emplace_back(_Wire, std::move(_MappedWire));
                }
                if (_Mapped.empty()) continue;

                std::vector<std::size_t> _LaneCounts(_Profiles.Lanes.size(), 0);
                for (const auto& [_Wire, _MappedWire] : _Mapped)
                {
                    (void)_Wire;
                    if (_MappedWire.Lane < _LaneCounts.size())
                        _LaneCounts[_MappedWire.Lane] += _MappedWire.Points.size();
                }
                if (!_LaneCounts.empty())
                {
                    _DominantLane = static_cast<std::size_t>(std::distance(
                        _LaneCounts.begin(),
                        std::max_element(_LaneCounts.begin(), _LaneCounts.end())));
                }
                if (!_DominantLane || *_DominantLane >= _LaneCount) continue;

                STubeUnfoldedPanel _Panel;
                _Panel.SurfaceIndex = *_DominantLane;
                _Panel.SourceFaceIndex = _Face.Index;
                for (auto& [_Wire, _MappedWire] : _Mapped)
                {
                    if (_MappedWire.Lane != *_DominantLane) continue;
                    const auto _MaximumResidual = _MappedWire.MaximumResidual;
                    auto _OutputWire = MakeOutputWire(
                        std::move(_MappedWire), *_DominantLane, _Face.Index, 0,
                        "panel-boundary", IsClosedWire(_Wire, Options_),
                        _Result.Surfaces[*_DominantLane].UPeriod);
                    if (_OutputWire.Points.size() < 2) continue;
                    for (const auto& _Point : _OutputWire.Points)
                    {
                        _Panel.UStart = _Panel.Boundaries.empty()
                            ? _Point.U : std::min(_Panel.UStart, _Point.U);
                        _Panel.UEnd = _Panel.Boundaries.empty()
                            ? _Point.U : std::max(_Panel.UEnd, _Point.U);
                    }
                    _Panel.Boundaries.push_back(std::move(_OutputWire));
                    _Result.MaximumProjectionResidual = std::max(
                        _Result.MaximumProjectionResidual,
                        _MaximumResidual);
                }
                if (!_Panel.Boundaries.empty())
                    _Result.Surfaces[*_DominantLane].Panels.push_back(std::move(_Panel));
            }

            if (Options_.IncludeBoundaryWires)
            {
                TopTools_IndexedMapOfShape _Edges;
                TopExp::MapShapes(_Normalized->Shape, TopAbs_EDGE, _Edges);
                TopTools_IndexedMapOfShape _FaceMap;
                TopExp::MapShapes(_Normalized->Shape, TopAbs_FACE, _FaceMap);
                TopTools_IndexedDataMapOfShapeListOfShape _EdgeFaces;
                TopExp::MapShapesAndAncestors(
                    _Normalized->Shape, TopAbs_EDGE, TopAbs_FACE, _EdgeFaces);
                occ::handle<NCollection_HSequence<TopoDS_Shape>> _BoundaryEdges =
                    new NCollection_HSequence<TopoDS_Shape>();
                std::map<std::size_t, std::string> _EdgeRoles;
                std::map<std::size_t, std::size_t> _EdgeSourceFaces;
                for (Standard_Integer _Index = 1;
                    _Index <= _EdgeFaces.Extent(); ++_Index)
                {
                    const auto _EdgeIndex = static_cast<std::size_t>(_Index - 1);
                    const auto _Edge = TopoDS::Edge(_EdgeFaces.FindKey(_Index));
                    bool _TouchesSide = false;
                    bool _OnlySide = true;
                    bool _NearEnd = false;
                    std::size_t _SourceFace = 0;
                    for (TopTools_ListOfShape::Iterator _Iterator(
                        _EdgeFaces.FindFromIndex(_Index)); _Iterator.More(); _Iterator.Next())
                    {
                        const auto _Face = TopoDS::Face(_Iterator.Value());
                        const auto _FaceIndexInMap = _FaceMap.FindIndex(_Face);
                        if (_FaceIndexInMap <= 0) continue;
                        const auto _FaceIndex = static_cast<std::size_t>(
                            _FaceIndexInMap - 1);
                        if (_FaceIndex < _SideFaces.size() && _SideFaces[_FaceIndex])
                        {
                            _TouchesSide = true;
                            _SourceFace = _FaceIndex;
                        }
                        else
                        {
                            _OnlySide = false;
                            const auto _Range = FaceAxialRange(
                                _Face, _Normalized->Frame);
                            const auto _Middle = (_Range.first + _Range.second) * 0.5;
                            _NearEnd = _NearEnd
                                || _Middle <= _Normalized->First + _Result.Length * 0.25
                                || _Middle >= _Normalized->Last - _Result.Length * 0.25;
                        }
                    }
                    if (!_TouchesSide || _OnlySide || _Edge.IsNull()) continue;
                    _BoundaryEdges->Append(_Edge);
                    _EdgeRoles[_EdgeIndex] = _NearEnd
                        ? "end-boundary" : "feature-boundary";
                    _EdgeSourceFaces[_EdgeIndex] = _SourceFace;
                }
                const auto _BoundaryWires = ShapeAnalysis_FreeBounds::ConnectEdgesToWires(
                    _BoundaryEdges, Options_.DistanceTolerance * 10.0, true);
                if (!_BoundaryWires.IsNull())
                {
                    for (int _WireIndex = 1;
                        _WireIndex <= _BoundaryWires->Length(); ++_WireIndex)
                    {
                        const auto& _Candidate = _BoundaryWires->Value(_WireIndex);
                        if (_Candidate.ShapeType() != TopAbs_WIRE) continue;
                        const auto _Wire = TopoDS::Wire(_Candidate);
                        auto _MappedWire = MapWire(
                            _Wire, _Normalized->Frame, _Profiles, Options_);
                        if (!_MappedWire.Valid || _MappedWire.Lane >= _LaneCount)
                            continue;
                        NormalizeMappedStation(_MappedWire, _Normalized->First);
                        std::size_t _SourceEdge = 0;
                        std::size_t _SourceFace = 0;
                        std::string _Role = "feature-boundary";
                        bool _EndBoundary = false;
                        for (BRepTools_WireExplorer _Explorer(_Wire);
                            _Explorer.More(); _Explorer.Next())
                        {
                            const auto _Edge = TopoDS::Edge(_Explorer.Current());
                            const auto _EdgeIndexInMap = _Edges.FindIndex(_Edge);
                            if (_EdgeIndexInMap <= 0) continue;
                            const auto _EdgeIndex = static_cast<std::size_t>(
                                _EdgeIndexInMap - 1);
                            if (_EdgeRoles.contains(_EdgeIndex))
                            {
                                _SourceEdge = _EdgeIndex;
                                _SourceFace = _EdgeSourceFaces[_EdgeIndex];
                                _EndBoundary = _EndBoundary
                                    || _EdgeRoles[_EdgeIndex] == "end-boundary";
                            }
                        }
                        if (_EndBoundary) _Role = "end-boundary";
                        const auto _Lane = _MappedWire.Lane;
                        const auto _MaximumResidual = _MappedWire.MaximumResidual;
                        auto _OutputWire = MakeOutputWire(
                            std::move(_MappedWire), _Lane,
                            _SourceFace, _SourceEdge, _Role,
                            IsClosedWire(_Wire, Options_),
                            _Result.Surfaces[_Lane].UPeriod);
                        if (_OutputWire.Points.size() < 2) continue;
                        _Result.MaximumProjectionResidual = std::max(
                            _Result.MaximumProjectionResidual,
                            _MaximumResidual);
                        _Result.Surfaces[_OutputWire.SurfaceIndex].Wires.push_back(
                            std::move(_OutputWire));
                    }
                }
            }

            AssignIDs(_Result);
            if (_Result.PointCount > Options_.MaximumOutputPoints)
            {
                throw std::runtime_error("BRep 展开结果点数超过上限");
            }
            if (!_Result.LateralRectangle.Available)
            {
                _Result.bOK = false;
                _Result.Diagnostic = "BRep 没有形成有效的整圈侧壁矩形";
                return _Result;
            }
            const auto _HasSideFaceEvidence = std::any_of(
                _Faces.begin(), _Faces.end(),
                [](const SFaceEvidence& _Face) { return _Face.Side; });
            _Result.Diagnostic = _Result.Approximate
                ? "已按整圈侧壁矩形和自有截面弧长坐标生成近似展开；未使用第三方 UV"
                : "已按整圈侧壁矩形和自有截面弧长坐标生成展开；未使用第三方 UV";
            if (!_HasSideFaceEvidence)
                _Result.Diagnostic += "；未找到可单独标注的侧面面片，仍保留完整矩形编辑域";
            return _Result;
        }
        catch (const Standard_Failure& _Error)
        {
            _Result.bOK = false;
            _Result.Diagnostic = std::string("BRep 展开失败: ") + _Error.what();
            return _Result;
        }
        catch (const std::exception& _Error)
        {
            _Result.bOK = false;
            _Result.Diagnostic = std::string("BRep 展开失败: ") + _Error.what();
            return _Result;
        }
    }

    STubeBRepUnfoldingResult UnfoldTubeBRepSurface(
        IN const iCAX::GeometryData::BRepModel& BRep_,
        IN const STubeBRepUnfoldingOptions& Options_)
    {
        try
        {
            const auto _Built = iCAX::OpenCascade::BuildOpenCascadeShape(BRep_);
            if (!_Built.bOK || _Built.Shape.IsNull())
            {
                STubeBRepUnfoldingResult _Result;
                _Result.Diagnostic = "无法从 BRepModel 重建 OCC Shape";
                return _Result;
            }
            return UnfoldTubeBRepSurface(_Built.Shape, Options_);
        }
        catch (const Standard_Failure& _Error)
        {
            STubeBRepUnfoldingResult _Result;
            _Result.Diagnostic = std::string("BRepModel 重建失败: ") + _Error.what();
            return _Result;
        }
        catch (const std::exception& _Error)
        {
            STubeBRepUnfoldingResult _Result;
            _Result.Diagnostic = std::string("BRepModel 重建失败: ") + _Error.what();
            return _Result;
        }
    }

    namespace
    {
        constexpr double kSideSketchPi = 3.1415926535897932384626433832795;

        struct SSideSketchPoint final
        {
            double S = 0.0;
            double U = 0.0;
        };

        struct SSideSketchTrajectory final
        {
            bool Closed = false;
            std::vector<SSideSketchPoint> Points;
        };

        struct SSideSurfaceSample final
        {
            gp_Pnt Point;
            gp_Dir Normal = gp::DY();
        };

        struct SNormalVolumeNode final
        {
            gp_Pnt Outer;
            gp_Pnt Inner;
        };

        struct SNormalVolumeEdgeUse final
        {
            std::size_t Count = 0;
            std::size_t First = 0;
            std::size_t Second = 0;
        };

        bool TrySideSketchNumber(
            const Variant& Value_, double& Result_)
        {
            try
            {
                if (Value_.Is<double>()) Result_ = Value_.To<double>();
                else if (Value_.Is<float>()) Result_ = static_cast<double>(Value_.To<float>());
                else if (Value_.Is<unsigned long long>())
                    Result_ = static_cast<double>(Value_.To<unsigned long long>());
                else if (Value_.Is<long long>()) Result_ = static_cast<double>(Value_.To<long long>());
                else if (Value_.Is<unsigned int>())
                    Result_ = static_cast<double>(Value_.To<unsigned int>());
                else if (Value_.Is<int>()) Result_ = static_cast<double>(Value_.To<int>());
                else if (Value_.Is<std::string>()) Result_ = std::stod(Value_.To<std::string>());
                else return false;
                return std::isfinite(Result_);
            }
            catch (...)
            {
                return false;
            }
        }

        std::optional<double> SideSketchNumber(
            const ObjectMap& Map_, const std::string& Name_)
        {
            const auto _Iterator = Map_.find(Name_);
            if (_Iterator == Map_.end()) return std::nullopt;
            double _Value = 0.0;
            return TrySideSketchNumber(_Iterator->second, _Value)
                ? std::optional<double>(_Value) : std::nullopt;
        }

        std::optional<SSideSketchPoint> SideSketchPoint(
            const Variant& Value_)
        {
            if (!Value_.Is<VariantArray>()) return std::nullopt;
            const auto& _Values = Value_.To<VariantArray>();
            if (_Values.size() < 2) return std::nullopt;
            double _S = 0.0;
            double _U = 0.0;
            if (!TrySideSketchNumber(_Values[0], _S)
                || !TrySideSketchNumber(_Values[1], _U))
                return std::nullopt;
            return SSideSketchPoint{ _S, _U };
        }

        std::optional<SSideSketchPoint> SideSketchMapPoint(
            const ObjectMap& Map_, const std::string& Name_)
        {
            const auto _Iterator = Map_.find(Name_);
            return _Iterator == Map_.end()
                ? std::nullopt : SideSketchPoint(_Iterator->second);
        }

        void AppendSideSketchPoint(
            std::vector<SSideSketchPoint>& Points_, const SSideSketchPoint& Point_)
        {
            if (!std::isfinite(Point_.S) || !std::isfinite(Point_.U)) return;
            if (!Points_.empty()
                && std::hypot(Points_.back().S - Point_.S,
                    Points_.back().U - Point_.U) <= 1.0e-8)
                return;
            Points_.push_back(Point_);
        }

        void AppendSideSketchLinear(
            std::vector<SSideSketchPoint>& Points_,
            const SSideSketchPoint& Start_, const SSideSketchPoint& End_,
            const std::size_t Count_ = 1)
        {
            for (std::size_t _Index = 0; _Index <= Count_; ++_Index)
            {
                const auto _Ratio = Count_ == 0
                    ? 0.0 : static_cast<double>(_Index) / static_cast<double>(Count_);
                AppendSideSketchPoint(Points_, {
                    Start_.S + (End_.S - Start_.S) * _Ratio,
                    Start_.U + (End_.U - Start_.U) * _Ratio });
            }
        }

        void AppendSideSketchQuadratic(
            std::vector<SSideSketchPoint>& Points_,
            const SSideSketchPoint& Start_, const SSideSketchPoint& Control_,
            const SSideSketchPoint& End_, const std::size_t Count_ = 24)
        {
            for (std::size_t _Index = 0; _Index <= Count_; ++_Index)
            {
                const auto _Ratio = Count_ == 0
                    ? 0.0 : static_cast<double>(_Index) / static_cast<double>(Count_);
                const auto _Inverse = 1.0 - _Ratio;
                AppendSideSketchPoint(Points_, {
                    _Inverse * _Inverse * Start_.S
                        + 2.0 * _Inverse * _Ratio * Control_.S
                        + _Ratio * _Ratio * End_.S,
                    _Inverse * _Inverse * Start_.U
                        + 2.0 * _Inverse * _Ratio * Control_.U
                        + _Ratio * _Ratio * End_.U });
            }
        }

        std::vector<SSideSketchPoint> SampleSideSketchSegment(
            const ObjectMap& Segment_)
        {
            std::vector<SSideSketchPoint> _Result;
            const auto _KindIterator = Segment_.find("kind");
            if (_KindIterator == Segment_.end() || !_KindIterator->second.Is<std::string>())
                return _Result;
            const auto _Kind = _KindIterator->second.To<std::string>();
            if (_Kind == "line")
            {
                auto _Start = SideSketchMapPoint(Segment_, "start");
                auto _End = SideSketchMapPoint(Segment_, "end");
                if (!_Start || !_End)
                {
                    const auto _X1 = SideSketchNumber(Segment_, "x1");
                    const auto _Y1 = SideSketchNumber(Segment_, "y1");
                    const auto _X2 = SideSketchNumber(Segment_, "x2");
                    const auto _Y2 = SideSketchNumber(Segment_, "y2");
                    if (_X1 && _Y1 && _X2 && _Y2)
                    {
                        _Start = SSideSketchPoint{ *_X1, *_Y1 };
                        _End = SSideSketchPoint{ *_X2, *_Y2 };
                    }
                }
                if (_Start && _End) AppendSideSketchLinear(_Result, *_Start, *_End);
                return _Result;
            }
            if (_Kind == "bezier")
            {
                const auto _PointsIterator = Segment_.find("points");
                if (_PointsIterator == Segment_.end()
                    || !_PointsIterator->second.Is<VariantArray>()) return _Result;
                std::vector<SSideSketchPoint> _Controls;
                for (const auto& _Value : _PointsIterator->second.To<VariantArray>())
                    if (const auto _Point = SideSketchPoint(_Value)) _Controls.push_back(*_Point);
                if (_Controls.size() < 2) return _Result;
                const auto _Count = std::max<std::size_t>(24,
                    std::min<std::size_t>(128, _Controls.size() * 16));
                for (std::size_t _Index = 0; _Index <= _Count; ++_Index)
                {
                    const auto _Ratio = static_cast<double>(_Index)
                        / static_cast<double>(_Count);
                    std::vector<SSideSketchPoint> _Level = _Controls;
                    for (std::size_t _Order = _Level.size() - 1; _Order > 0; --_Order)
                        for (std::size_t _Cursor = 0; _Cursor < _Order; ++_Cursor)
                            _Level[_Cursor] = {
                                _Level[_Cursor].S * (1.0 - _Ratio)
                                    + _Level[_Cursor + 1].S * _Ratio,
                                _Level[_Cursor].U * (1.0 - _Ratio)
                                    + _Level[_Cursor + 1].U * _Ratio };
                    AppendSideSketchPoint(_Result, _Level.front());
                }
                return _Result;
            }
            if (_Kind == "circleArc" || _Kind == "ellipseArc")
            {
                const auto _CX = SideSketchNumber(Segment_, "cx");
                const auto _CY = SideSketchNumber(Segment_, "cy");
                const auto _Start = SideSketchNumber(Segment_, "startAngle");
                const auto _Sweep = SideSketchNumber(Segment_, "sweep");
                if (!_CX || !_CY || !_Start || !_Sweep) return _Result;
                const auto _RX = _Kind == "circleArc"
                    ? SideSketchNumber(Segment_, "radius")
                    : SideSketchNumber(Segment_, "radiusX");
                const auto _RY = _Kind == "circleArc"
                    ? _RX : SideSketchNumber(Segment_, "radiusY");
                const auto _Rotation = SideSketchNumber(Segment_, "rotation").value_or(0.0);
                if (!_RX || !_RY || *_RX <= 0.0 || *_RY <= 0.0) return _Result;
                const auto _Count = std::max<std::size_t>(8, std::min<std::size_t>(256,
                    static_cast<std::size_t>(std::ceil(std::abs(*_Sweep)
                        / (2.0 * kSideSketchPi) * 96.0))));
                const auto _Cosine = std::cos(_Rotation);
                const auto _Sine = std::sin(_Rotation);
                for (std::size_t _Index = 0; _Index <= _Count; ++_Index)
                {
                    const auto _Angle = *_Start + *_Sweep
                        * static_cast<double>(_Index) / static_cast<double>(_Count);
                    const auto _X = *_RX * std::cos(_Angle);
                    const auto _Y = *_RY * std::sin(_Angle);
                    AppendSideSketchPoint(_Result, {
                        *_CX + _X * _Cosine - _Y * _Sine,
                        *_CY + _X * _Sine + _Y * _Cosine });
                }
                return _Result;
            }
            return _Result;
        }

        std::vector<SSideSketchPoint> SampleSideSketchEntity(
            const ObjectMap& Entity_)
        {
            std::vector<SSideSketchPoint> _Result;
            const auto _KindIterator = Entity_.find("kind");
            if (_KindIterator == Entity_.end() || !_KindIterator->second.Is<std::string>())
                return _Result;
            const auto _Kind = _KindIterator->second.To<std::string>();
            if (_Kind == "text") return _Result;
            if (_Kind == "line")
            {
                const auto _X1 = SideSketchNumber(Entity_, "x1");
                const auto _Y1 = SideSketchNumber(Entity_, "y1");
                const auto _X2 = SideSketchNumber(Entity_, "x2");
                const auto _Y2 = SideSketchNumber(Entity_, "y2");
                if (_X1 && _Y1 && _X2 && _Y2)
                    AppendSideSketchLinear(_Result, { *_X1, *_Y1 }, { *_X2, *_Y2 });
                return _Result;
            }
            if (_Kind == "rectangle")
            {
                const auto _X = SideSketchNumber(Entity_, "x");
                const auto _Y = SideSketchNumber(Entity_, "y");
                const auto _Width = SideSketchNumber(Entity_, "width");
                const auto _Height = SideSketchNumber(Entity_, "height");
                if (!_X || !_Y || !_Width || !_Height) return _Result;
                const auto _Radius = std::max(0.0, std::min({
                    SideSketchNumber(Entity_, "radius").value_or(0.0),
                    *_Width * 0.5, *_Height * 0.5 }));
                if (_Radius <= 1.0e-8)
                {
                    _Result = {
                        { *_X, *_Y }, { *_X + *_Width, *_Y },
                        { *_X + *_Width, *_Y + *_Height },
                        { *_X, *_Y + *_Height } };
                    return _Result;
                }
                const std::array<std::pair<SSideSketchPoint, double>, 4> _Corners{{
                    { { *_X + *_Width - _Radius, *_Y + _Radius }, -kSideSketchPi * 0.5 },
                    { { *_X + *_Width - _Radius, *_Y + *_Height - _Radius }, 0.0 },
                    { { *_X + _Radius, *_Y + *_Height - _Radius }, kSideSketchPi * 0.5 },
                    { { *_X + _Radius, *_Y + _Radius }, kSideSketchPi } }};
                for (const auto& [_Center, _StartAngle] : _Corners)
                    for (std::size_t _Index = 0; _Index <= 12; ++_Index)
                    {
                        const auto _Angle = _StartAngle + kSideSketchPi * 0.5
                            * static_cast<double>(_Index) / 12.0;
                        AppendSideSketchPoint(_Result, {
                            _Center.S + _Radius * std::cos(_Angle),
                            _Center.U + _Radius * std::sin(_Angle) });
                    }
                return _Result;
            }
            if (_Kind == "circle" || _Kind == "ellipse")
            {
                const auto _CX = SideSketchNumber(Entity_, "cx");
                const auto _CY = SideSketchNumber(Entity_, "cy");
                if (!_CX || !_CY) return _Result;
                const auto _RX = _Kind == "circle"
                    ? SideSketchNumber(Entity_, "radius")
                    : SideSketchNumber(Entity_, "radiusX");
                const auto _RY = _Kind == "circle"
                    ? _RX : SideSketchNumber(Entity_, "radiusY");
                if (!_RX || !_RY || *_RX <= 0.0 || *_RY <= 0.0) return _Result;
                const auto _Rotation = SideSketchNumber(Entity_, "rotation").value_or(0.0);
                const auto _Cosine = std::cos(_Rotation);
                const auto _Sine = std::sin(_Rotation);
                constexpr std::size_t _Count = 96;
                for (std::size_t _Index = 0; _Index < _Count; ++_Index)
                {
                    const auto _Angle = 2.0 * kSideSketchPi
                        * static_cast<double>(_Index) / static_cast<double>(_Count);
                    const auto _X = *_RX * std::cos(_Angle);
                    const auto _Y = *_RY * std::sin(_Angle);
                    AppendSideSketchPoint(_Result, {
                        *_CX + _X * _Cosine - _Y * _Sine,
                        *_CY + _X * _Sine + _Y * _Cosine });
                }
                return _Result;
            }
            if (_Kind == "circleArc" || _Kind == "ellipseArc")
                return SampleSideSketchSegment(Entity_);
            if (_Kind == "arc")
            {
                const auto _PointsIterator = Entity_.find("points");
                if (_PointsIterator == Entity_.end()
                    || !_PointsIterator->second.Is<VariantArray>()) return _Result;
                std::vector<SSideSketchPoint> _Points;
                for (const auto& _Value : _PointsIterator->second.To<VariantArray>())
                    if (const auto _Point = SideSketchPoint(_Value)) _Points.push_back(*_Point);
                if (_Points.size() < 3) return _Result;
                AppendSideSketchQuadratic(_Result, _Points[0], _Points[1], _Points[2], 32);
                return _Result;
            }
            if (_Kind == "spline")
            {
                const auto _PointsIterator = Entity_.find("points");
                if (_PointsIterator == Entity_.end()
                    || !_PointsIterator->second.Is<VariantArray>()) return _Result;
                std::vector<SSideSketchPoint> _Points;
                for (const auto& _Value : _PointsIterator->second.To<VariantArray>())
                    if (const auto _Point = SideSketchPoint(_Value)) _Points.push_back(*_Point);
                if (_Points.size() < 3) return _Result;
                auto _Start = _Points.front();
                for (std::size_t _Index = 1; _Index + 1 < _Points.size(); ++_Index)
                {
                    const auto _End = SSideSketchPoint{
                        (_Points[_Index].S + _Points[_Index + 1].S) * 0.5,
                        (_Points[_Index].U + _Points[_Index + 1].U) * 0.5 };
                    AppendSideSketchQuadratic(_Result, _Start, _Points[_Index], _End, 24);
                    _Start = _End;
                }
                const auto _PreviousControl = _Points[_Points.size() - 2];
                const auto _Control = SSideSketchPoint{
                    2.0 * _Start.S - _PreviousControl.S,
                    2.0 * _Start.U - _PreviousControl.U };
                AppendSideSketchQuadratic(_Result, _Start, _Control, _Points.back(), 24);
                return _Result;
            }
            if (_Kind == "path")
            {
                const auto _SegmentsIterator = Entity_.find("segments");
                if (_SegmentsIterator == Entity_.end()
                    || !_SegmentsIterator->second.Is<VariantArray>()) return _Result;
                for (const auto& _Value : _SegmentsIterator->second.To<VariantArray>())
                {
                    if (!_Value.Is<ObjectMap>()) continue;
                    auto _Segment = SampleSideSketchSegment(_Value.To<ObjectMap>());
                    for (const auto& _Point : _Segment)
                        AppendSideSketchPoint(_Result, _Point);
                }
                return _Result;
            }
            const auto _PointsIterator = Entity_.find("points");
            if (_PointsIterator != Entity_.end() && _PointsIterator->second.Is<VariantArray>())
                for (const auto& _Value : _PointsIterator->second.To<VariantArray>())
                    if (const auto _Point = SideSketchPoint(_Value))
                        AppendSideSketchPoint(_Result, *_Point);
            return _Result;
        }

        double SideSketchWrapU(const double Value_, const double Period_)
        {
            if (!std::isfinite(Value_) || !std::isfinite(Period_) || Period_ <= 0.0)
                return 0.0;
            auto _Wrapped = std::fmod(Value_, Period_);
            if (_Wrapped < 0.0) _Wrapped += Period_;
            return _Wrapped;
        }

        bool IsSideSketchSeamPoint(const double Value_, const double Period_)
        {
            if (!std::isfinite(Value_) || !std::isfinite(Period_) || Period_ <= 0.0)
                return false;
            const auto _Wrapped = SideSketchWrapU(Value_, Period_);
            const auto _Distance = std::min(_Wrapped, Period_ - _Wrapped);
            return _Distance <= std::max(1.0e-6, Period_ * 1.0e-5);
        }

        bool IsSideSketchFullLapEdge(
            const SSideSketchPoint& Start_, const SSideSketchPoint& End_,
            const double Period_)
        {
            return std::abs(std::abs(End_.U - Start_.U) - Period_)
                    <= std::max(1.0e-6, Period_ * 1.0e-5)
                && IsSideSketchSeamPoint(Start_.U, Period_)
                && IsSideSketchSeamPoint(End_.U, Period_);
        }

        void UnwrapSideSketchTrajectory(
            std::vector<SSideSketchPoint>& Points_, const double Period_)
        {
            if (Points_.empty() || Period_ <= Precision::Confusion()) return;
            for (std::size_t _Index = 1; _Index < Points_.size(); ++_Index)
            {
                // The two horizontal edges of the side rectangle are the same
                // physical seam.  Keep an explicit seam-to-seam segment as a
                // full lap: a line from U=0 to U=period is the circumferential
                // cut used for a tube end, not a zero-length segment.
                const auto _IsExplicitFullLap = IsSideSketchFullLapEdge(
                    Points_[_Index - 1], Points_[_Index], Period_);
                if (_IsExplicitFullLap) continue;
                while (Points_[_Index].U - Points_[_Index - 1].U > Period_ * 0.5)
                    Points_[_Index].U -= Period_;
                while (Points_[_Index - 1].U - Points_[_Index].U > Period_ * 0.5)
                    Points_[_Index].U += Period_;
            }
        }

        std::vector<SSideSketchPoint> DensifySideSketchPolygon(
            const std::vector<SSideSketchPoint>& Polygon_,
            const SNormalizedTube& Tube_, const SProfileLanes& Profiles_,
            const STubeSideSketchOptions& Options_)
        {
            if (Polygon_.size() < 3) return {};
            const auto _Period = Profiles_.Lanes.empty() || Profiles_.Lanes.front().empty()
                ? 0.0 : Profiles_.Lanes.front().front().Period;
            const auto _Length = std::max(0.0, Tube_.Last - Tube_.First);
            if (_Period <= Precision::Confusion() || _Length <= Precision::Confusion())
                return Polygon_;

            // A polygon edge that goes from U=0 to U=period has coincident
            // endpoints in 3D.  Insert intermediate stations before folding the
            // polygon so that the edge remains a real circumferential curve and
            // can form a closed cutting band.
            const auto _MaximumStep = std::max(
                Options_.DistanceTolerance * 20.0,
                std::min(_Length, _Period) / 32.0);
            constexpr std::size_t _MaximumEdgeSamples = 129;
            std::vector<SSideSketchPoint> _Result;
            _Result.reserve(Polygon_.size() * 4);
            for (std::size_t _Index = 0; _Index < Polygon_.size(); ++_Index)
            {
                const auto& _Start = Polygon_[_Index];
                const auto& _End = Polygon_[(_Index + 1) % Polygon_.size()];
                AppendSideSketchPoint(_Result, _Start);
                const auto _EdgeLength = std::hypot(
                    _End.S - _Start.S, _End.U - _Start.U);
                const auto _FullLapEdge = IsSideSketchFullLapEdge(
                    _Start, _End, _Period);
                const auto _Steps = _FullLapEdge
                    ? std::clamp<std::size_t>(
                        static_cast<std::size_t>(std::ceil(_EdgeLength / _MaximumStep)),
                        1, _MaximumEdgeSamples)
                    : std::size_t{ 1 };
                for (std::size_t _Step = 1; _Step < _Steps; ++_Step)
                {
                    const auto _Ratio = static_cast<double>(_Step)
                        / static_cast<double>(_Steps);
                    AppendSideSketchPoint(_Result, {
                        _Start.S + (_End.S - _Start.S) * _Ratio,
                        _Start.U + (_End.U - _Start.U) * _Ratio });
                }
            }
            return _Result;
        }

        std::vector<SSideSketchPoint> MakeOpenTrajectoryRibbon(
            const std::vector<SSideSketchPoint>& Centerline_, const double Width_)
        {
            std::vector<SSideSketchPoint> _Result;
            if (Centerline_.size() < 2 || Width_ <= 0.0) return _Result;
            const auto _HalfWidth = Width_ * 0.5;
            std::vector<SSideSketchPoint> _Left, _Right;
            _Left.reserve(Centerline_.size());
            _Right.reserve(Centerline_.size());
            for (std::size_t _Index = 0; _Index < Centerline_.size(); ++_Index)
            {
                const auto& _Current = Centerline_[_Index];
                const auto& _Previous = _Index == 0 ? _Current : Centerline_[_Index - 1];
                const auto& _Next = _Index + 1 == Centerline_.size()
                    ? _Current : Centerline_[_Index + 1];
                auto _TangentS = _Next.S - _Previous.S;
                auto _TangentU = _Next.U - _Previous.U;
                const auto _Length = std::hypot(_TangentS, _TangentU);
                if (_Length <= Precision::Confusion()) continue;
                _TangentS /= _Length;
                _TangentU /= _Length;
                const SSideSketchPoint _Offset{ -_TangentU * _HalfWidth,
                    _TangentS * _HalfWidth };
                _Left.push_back({ _Current.S + _Offset.S, _Current.U + _Offset.U });
                _Right.push_back({ _Current.S - _Offset.S, _Current.U - _Offset.U });
            }
            _Result.reserve(_Left.size() + _Right.size());
            for (const auto& _Point : _Left) AppendSideSketchPoint(_Result, _Point);
            for (auto _Iterator = _Right.rbegin(); _Iterator != _Right.rend(); ++_Iterator)
                AppendSideSketchPoint(_Result, *_Iterator);
            return _Result;
        }

        std::vector<SSideSketchTrajectory> ReadSideSketchTrajectories(
            const ObjectMap& Sketch_, const double SketchLength_, const double SketchHeight_,
            const double Length_, const double Period_)
        {
            std::vector<SSideSketchTrajectory> _Result;
            const auto _Entities = Sketch_.find("entities");
            if (_Entities == Sketch_.end() || !_Entities->second.Is<VariantArray>()) return _Result;
            for (const auto& _Value : _Entities->second.To<VariantArray>())
            {
                if (!_Value.Is<ObjectMap>()) continue;
                const auto _Entity = _Value.To<ObjectMap>();
                const auto _Kind = _Entity.find("kind");
                if (_Kind == _Entity.end() || !_Kind->second.Is<std::string>()
                    || _Kind->second.To<std::string>() == "text") continue;
                auto _Points = SampleSideSketchEntity(_Entity);
                if (_Points.size() < 2) continue;
                for (auto& _Point : _Points)
                {
                    _Point.S = _Point.S / SketchLength_ * Length_;
                    _Point.U = _Point.U / SketchHeight_ * Period_;
                }
                UnwrapSideSketchTrajectory(_Points, Period_);
                const auto _ClosedKind = _Kind->second.To<std::string>() == "rectangle"
                    || _Kind->second.To<std::string>() == "circle"
                    || _Kind->second.To<std::string>() == "ellipse";
                const auto _ClosedFlag = _Entity.find("closed");
                const auto _Closed = _ClosedKind
                    || (_ClosedFlag != _Entity.end() && _ClosedFlag->second.Is<bool>()
                        && _ClosedFlag->second.To<bool>())
                    || ((_Kind->second.To<std::string>() == "circleArc"
                        || _Kind->second.To<std::string>() == "ellipseArc")
                        && std::abs(SideSketchNumber(_Entity, "sweep").value_or(0.0))
                            >= 2.0 * kSideSketchPi - 1.0e-6);
                if (_Closed && _Points.size() > 2
                    && std::hypot(_Points.front().S - _Points.back().S,
                        _Points.front().U - _Points.back().U) <= 1.0e-6)
                    _Points.pop_back();
                if (_Closed && _Points.size() >= 3)
                    _Result.push_back({ true, std::move(_Points) });
                else if (!_Closed && _Points.size() >= 2)
                    _Result.push_back({ false, std::move(_Points) });
            }
            return _Result;
        }

        gp_Pnt SideProfilePointAtU(
            const SProfile& Profile_, const double U_)
        {
            if (Profile_.Points.empty() || Profile_.U.size() != Profile_.Points.size())
                return gp::Origin();
            const auto _Target = SideSketchWrapU(U_, Profile_.Period);
            const auto _Upper = std::upper_bound(
                Profile_.U.begin(), Profile_.U.end(), _Target);
            const auto _LeftIndex = _Upper == Profile_.U.begin()
                ? std::size_t{ 0 }
                : static_cast<std::size_t>(_Upper - Profile_.U.begin() - 1);
            const auto _RightIndex = (_LeftIndex + 1) % Profile_.Points.size();
            const auto _Start = Profile_.U[_LeftIndex];
            const auto _Span = _RightIndex > _LeftIndex
                ? Profile_.U[_RightIndex] - _Start
                : Profile_.Period - _Start;
            const auto _Ratio = _Span <= Precision::Confusion()
                ? 0.0 : std::clamp((_Target - _Start) / _Span, 0.0, 1.0);
            return Profile_.Points[_LeftIndex].Translated(gp_Vec(
                Profile_.Points[_LeftIndex], Profile_.Points[_RightIndex]) * _Ratio);
        }

        bool SideProfilePointAtStation(
            const SProfileLanes& Profiles_, const gp_Ax3& Frame_,
            const double Station_, const double U_, gp_Pnt& Point_)
        {
            if (Profiles_.Lanes.empty() || Profiles_.Lanes.front().empty()) return false;
            const auto& _Lane = Profiles_.Lanes.front();
            const auto _Upper = std::upper_bound(_Lane.begin(), _Lane.end(), Station_,
                [](const double Value_, const SProfile& Profile_) {
                    return Value_ < Profile_.Station;
                });
            const auto* _Right = _Upper == _Lane.end() ? &_Lane.back() : &*_Upper;
            const auto* _Left = _Upper == _Lane.begin() ? _Right : &*(_Upper - 1);
            const auto _StationSpan = _Right->Station - _Left->Station;
            const auto _StationRatio = _StationSpan <= Precision::Confusion()
                ? 0.0 : std::clamp((Station_ - _Left->Station) / _StationSpan, 0.0, 1.0);
            const auto _LeftU = _Left->Period * SideSketchWrapU(U_, _Left->Period)
                / std::max(_Left->Period, Precision::Confusion());
            const auto _RightU = _Right->Period * SideSketchWrapU(U_, _Right->Period)
                / std::max(_Right->Period, Precision::Confusion());
            const auto _PointLeft = SideProfilePointAtU(*_Left, _LeftU);
            const auto _PointRight = SideProfilePointAtU(*_Right, _RightU);
            Point_ = _PointLeft.Translated(gp_Vec(_PointLeft, _PointRight) * _StationRatio);
            return IsFinitePoint(Point_);
        }

        gp_Pnt SideSectionCentreAtStation(
            const SProfileLanes& Profiles_, const gp_Ax3& Frame_, const double Station_)
        {
            if (Profiles_.Lanes.empty() || Profiles_.Lanes.front().empty())
                return Frame_.Location().Translated(gp_Vec(Frame_.Direction()) * Station_);
            const auto& _Profile = Profiles_.Lanes.front().front();
            const auto _Count = std::max<std::size_t>(8,
                std::min<std::size_t>(32, _Profile.Points.size()));
            gp_Vec _Sum(0.0, 0.0, 0.0);
            for (std::size_t _Index = 0; _Index < _Count; ++_Index)
            {
                gp_Pnt _Point;
                if (SideProfilePointAtStation(Profiles_, Frame_, Station_,
                    _Profile.Period * static_cast<double>(_Index) / _Count, _Point))
                    _Sum += gp_Vec(Frame_.Location(), _Point);
            }
            return Frame_.Location().Translated(_Sum / static_cast<double>(_Count));
        }

        std::optional<SSideSurfaceSample> EvaluateSideSurface(
            const SNormalizedTube& Tube_, const SProfileLanes& Profiles_,
            const double Station_, const double U_,
            const STubeSideSketchOptions& Options_)
        {
            gp_Pnt _Point;
            if (!SideProfilePointAtStation(Profiles_, Tube_.Frame, Station_, U_, _Point))
                return std::nullopt;
            const auto _Period = Profiles_.Lanes.front().front().Period;
            const auto _UStep = std::max(Options_.DistanceTolerance * 4.0, _Period / 256.0);
            const auto _StationStep = std::max(Options_.DistanceTolerance * 4.0,
                (Tube_.Last - Tube_.First) / 128.0);
            gp_Pnt _UMinus, _UPlus, _SMinus, _SPlus;
            if (!SideProfilePointAtStation(Profiles_, Tube_.Frame, Station_, U_ - _UStep, _UMinus)
                || !SideProfilePointAtStation(Profiles_, Tube_.Frame, Station_, U_ + _UStep, _UPlus)
                || !SideProfilePointAtStation(Profiles_, Tube_.Frame,
                    std::max(Tube_.First, Station_ - _StationStep), U_, _SMinus)
                || !SideProfilePointAtStation(Profiles_, Tube_.Frame,
                    std::min(Tube_.Last, Station_ + _StationStep), U_, _SPlus))
                return std::nullopt;
            gp_Vec _TangentU(_UMinus, _UPlus);
            gp_Vec _TangentS(_SMinus, _SPlus);
            auto _Normal = _TangentU.Crossed(_TangentS);
            if (_Normal.SquareMagnitude() <= Precision::SquareConfusion())
            {
                _Normal = gp_Vec(SideSectionCentreAtStation(
                    Profiles_, Tube_.Frame, Station_), _Point);
            }
            if (_Normal.SquareMagnitude() <= Precision::SquareConfusion()) return std::nullopt;
            const gp_Vec _Radial(SideSectionCentreAtStation(
                Profiles_, Tube_.Frame, Station_), _Point);
            if (_Radial.SquareMagnitude() > Precision::SquareConfusion()
                && _Normal.Dot(_Radial) < 0.0)
                _Normal.Reverse();
            try
            {
                return SSideSurfaceSample{ _Point, gp_Dir(_Normal) };
            }
            catch (const Standard_Failure&)
            {
                return std::nullopt;
            }
        }

        std::optional<double> SideMaterialRayDepth(
            const TopoDS_Shape& Shape_, const gp_Pnt& SurfacePoint_, const gp_Dir& Normal_,
            const double OuterClearance_, const double Reach_, const double Tolerance_)
        {
            try
            {
                IntCurvesFace_ShapeIntersector _Ray;
                _Ray.Load(Shape_, Tolerance_);
                const auto _Origin = SurfacePoint_.Translated(
                    gp_Vec(Normal_) * OuterClearance_);
                _Ray.Perform(gp_Lin(_Origin, gp_Dir(-gp_Vec(Normal_))), 0.0, Reach_);
                if (_Ray.NbPnt() < 2) return std::nullopt;
                std::vector<double> _Parameters;
                _Parameters.reserve(static_cast<std::size_t>(_Ray.NbPnt()));
                for (int _Index = 1; _Index <= _Ray.NbPnt(); ++_Index)
                {
                    const auto _Value = _Ray.WParameter(_Index);
                    if (std::isfinite(_Value) && _Value >= 0.0 && _Value <= Reach_)
                        _Parameters.push_back(_Value);
                }
                std::sort(_Parameters.begin(), _Parameters.end());
                _Parameters.erase(std::unique(_Parameters.begin(), _Parameters.end(),
                    [&](const double Left_, const double Right_) {
                        return std::abs(Left_ - Right_) <= Tolerance_ * 5.0;
                    }), _Parameters.end());
                for (std::size_t _Index = 1; _Index < _Parameters.size(); ++_Index)
                {
                    const auto _First = _Parameters[_Index - 1];
                    const auto _Last = _Parameters[_Index];
                    if (_Last - _First <= Tolerance_) continue;
                    const auto _Middle = _Origin.Translated(
                        -gp_Vec(Normal_) * ((_First + _Last) * 0.5));
                    BRepClass3d_SolidClassifier _Classifier(Shape_, _Middle, Tolerance_ * 5.0);
                    if (_Classifier.State() == TopAbs_IN) return _Last;
                }
            }
            catch (const Standard_Failure&)
            {
            }
            return std::nullopt;
        }

        TopoDS_Face MakeSideTriangleFace(
            const gp_Pnt& A_, const gp_Pnt& B_, const gp_Pnt& C_)
        {
            BRepBuilderAPI_MakePolygon _Polygon;
            _Polygon.Add(A_);
            _Polygon.Add(B_);
            _Polygon.Add(C_);
            _Polygon.Close();
            if (!_Polygon.IsDone()) throw std::runtime_error("二维包覆三角面无法闭合");
            BRepBuilderAPI_MakeFace _Face(_Polygon.Wire());
            if (!_Face.IsDone()) throw std::runtime_error("二维包覆三角面无法生成");
            return _Face.Face();
        }

        double SidePolygonCross(
            const SSideSketchPoint& A_, const SSideSketchPoint& B_,
            const SSideSketchPoint& C_)
        {
            return (B_.S - A_.S) * (C_.U - A_.U)
                - (B_.U - A_.U) * (C_.S - A_.S);
        }

        bool SidePointInTriangle(
            const SSideSketchPoint& Point_, const SSideSketchPoint& A_,
            const SSideSketchPoint& B_, const SSideSketchPoint& C_)
        {
            const auto _AB = SidePolygonCross(A_, B_, Point_);
            const auto _BC = SidePolygonCross(B_, C_, Point_);
            const auto _CA = SidePolygonCross(C_, A_, Point_);
            constexpr double _Tolerance = 1.0e-9;
            return (_AB >= -_Tolerance && _BC >= -_Tolerance && _CA >= -_Tolerance)
                || (_AB <= _Tolerance && _BC <= _Tolerance && _CA <= _Tolerance);
        }

        std::optional<std::vector<std::array<std::size_t, 3>>> TriangulateSidePolygon(
            const std::vector<SSideSketchPoint>& Polygon_)
        {
            if (Polygon_.size() < 3) return std::nullopt;
            double _Area = 0.0;
            for (std::size_t _Index = 0; _Index < Polygon_.size(); ++_Index)
            {
                const auto& _Left = Polygon_[_Index];
                const auto& _Right = Polygon_[(_Index + 1) % Polygon_.size()];
                _Area += _Left.S * _Right.U - _Right.S * _Left.U;
            }
            if (std::abs(_Area) <= 1.0e-9) return std::nullopt;
            std::vector<std::size_t> _Remaining(Polygon_.size());
            std::iota(_Remaining.begin(), _Remaining.end(), 0);
            if (_Area < 0.0) std::reverse(_Remaining.begin(), _Remaining.end());
            std::vector<std::array<std::size_t, 3>> _Result;
            _Result.reserve(Polygon_.size() - 2);
            std::size_t _Guard = 0;
            while (_Remaining.size() > 3 && _Guard++ < Polygon_.size() * Polygon_.size())
            {
                bool _Clipped = false;
                for (std::size_t _Index = 0; _Index < _Remaining.size(); ++_Index)
                {
                    const auto _Previous = _Remaining[(_Index + _Remaining.size() - 1)
                        % _Remaining.size()];
                    const auto _Current = _Remaining[_Index];
                    const auto _Next = _Remaining[(_Index + 1) % _Remaining.size()];
                    if (SidePolygonCross(Polygon_[_Previous], Polygon_[_Current],
                        Polygon_[_Next]) <= 1.0e-9) continue;
                    bool _ContainsPoint = false;
                    for (const auto _Candidate : _Remaining)
                    {
                        if (_Candidate == _Previous || _Candidate == _Current || _Candidate == _Next)
                            continue;
                        if (SidePointInTriangle(Polygon_[_Candidate], Polygon_[_Previous],
                            Polygon_[_Current], Polygon_[_Next]))
                        {
                            _ContainsPoint = true;
                            break;
                        }
                    }
                    if (_ContainsPoint) continue;
                    _Result.push_back({ _Previous, _Current, _Next });
                    _Remaining.erase(_Remaining.begin() + static_cast<std::ptrdiff_t>(_Index));
                    _Clipped = true;
                    break;
                }
                if (!_Clipped) return std::nullopt;
            }
            if (_Remaining.size() == 3)
                _Result.push_back({ _Remaining[0], _Remaining[1], _Remaining[2] });
            return _Result.empty() ? std::nullopt
                : std::optional<std::vector<std::array<std::size_t, 3>>>(std::move(_Result));
        }

        std::optional<TopoDS_Shape> BuildCircumferentialBand(
            const SNormalizedTube& Tube_, const SProfileLanes& Profiles_,
            const std::vector<SSideSketchPoint>& Polygon_,
            const STubeSideSketchOptions& Options_)
        {
            if (Profiles_.Lanes.empty() || Profiles_.Lanes.front().empty()
                || Polygon_.size() < 3)
                return std::nullopt;
            const auto _Period = Profiles_.Lanes.front().front().Period;
            if (_Period <= Precision::Confusion()) return std::nullopt;

            bool _HasFullLap = false;
            double _MinimumS = (std::numeric_limits<double>::max)();
            double _MaximumS = (std::numeric_limits<double>::lowest)();
            for (std::size_t _Index = 0; _Index < Polygon_.size(); ++_Index)
            {
                const auto& _Start = Polygon_[_Index];
                const auto& _End = Polygon_[(_Index + 1) % Polygon_.size()];
                _HasFullLap = _HasFullLap || IsSideSketchFullLapEdge(
                    _Start, _End, _Period);
                _MinimumS = std::min(_MinimumS, std::min(_Start.S, _End.S));
                _MaximumS = std::max(_MaximumS, std::max(_Start.S, _End.S));
            }
            if (!_HasFullLap || !std::isfinite(_MinimumS)
                || !std::isfinite(_MaximumS)
                || _MaximumS - _MinimumS <= Options_.DistanceTolerance)
                return std::nullopt;

            try
            {
                STubeBRepUnfoldingOptions _SectionOptions;
                _SectionOptions.DistanceTolerance = Options_.DistanceTolerance;
                _SectionOptions.AngularToleranceRadians = Options_.AngularToleranceRadians;
                _SectionOptions.SamplesPerCurve = Options_.SamplesPerCurve;
                _SectionOptions.StationCount = Options_.StationCount;
                _SectionOptions.MaximumOutputPoints = Options_.MaximumToolPatches;
                const auto _Station = std::clamp(
                    Tube_.First + (_MinimumS + _MaximumS) * 0.5,
                    Tube_.First + Options_.DistanceTolerance * 2.0,
                    Tube_.Last - Options_.DistanceTolerance * 2.0);
                auto _Wires = SectionWiresAt(
                    Tube_.Shape, Tube_.Frame, _Station, _SectionOptions, true);
                if (_Wires.empty()) return std::nullopt;
                std::sort(_Wires.begin(), _Wires.end(), [&](const auto& _Left, const auto& _Right) {
                    return std::abs(SignedSectionArea(
                        SampleWire(_Left, _SectionOptions), Tube_.Frame))
                        > std::abs(SignedSectionArea(
                            SampleWire(_Right, _SectionOptions), Tube_.Frame));
                });

                BRepBuilderAPI_MakeFace _FaceMaker(_Wires.front(), true);
                if (!_FaceMaker.IsDone()) return std::nullopt;
                for (std::size_t _Index = 1; _Index < _Wires.size(); ++_Index)
                {
                    auto _Hole = _Wires[_Index];
                    // Section edge connection does not guarantee the inner
                    // loop orientation required by MakeFace.  Make every
                    // non-largest loop a hole explicitly.
                    _Hole.Reverse();
                    _FaceMaker.Add(_Hole);
                }
                if (!_FaceMaker.IsDone() || _FaceMaker.Face().IsNull())
                    return std::nullopt;

                const auto _HalfWidth = (_MaximumS - _MinimumS) * 0.5;
                gp_Trsf _Shift;
                _Shift.SetTranslation(gp_Vec(Tube_.Frame.Direction())
                    * -(_HalfWidth + Options_.OuterClearance));
                const auto _StartShape = BRepBuilderAPI_Transform(
                    _FaceMaker.Face(), _Shift, true).Shape();
                const auto _Span = _MaximumS - _MinimumS
                    + Options_.OuterClearance * 2.0
                    + Options_.InnerClearance * 2.0;
                BRepPrimAPI_MakePrism _Prism(
                    _StartShape, gp_Vec(Tube_.Frame.Direction()) * _Span, true, true);
                _Prism.Build();
                if (!_Prism.IsDone() || _Prism.Shape().IsNull()
                    || !BRepCheck_Analyzer(_Prism.Shape()).IsValid())
                    return std::nullopt;
                return _Prism.Shape();
            }
            catch (const Standard_Failure&)
            {
                return std::nullopt;
            }
        }

        std::optional<TopoDS_Shape> BuildNormalSideVolume(
            const TopoDS_Shape& Shape_, const SNormalizedTube& Tube_,
            const SProfileLanes& Profiles_, const std::vector<SSideSketchPoint>& Polygon_,
            const STubeSideSketchOptions& Options_, const double Reach_,
            std::size_t& PatchCount_)
        {
            if (const auto _CircumferentialTool = BuildCircumferentialBand(
                    Tube_, Profiles_, Polygon_, Options_))
            {
                if (PatchCount_ >= Options_.MaximumToolPatches)
                    throw std::runtime_error("二维包覆轨迹过于密集，超过切刀片数量上限");
                ++PatchCount_;
                return _CircumferentialTool;
            }
            const auto _DensifiedPolygon = DensifySideSketchPolygon(
                Polygon_, Tube_, Profiles_, Options_);
            const auto _Triangles = TriangulateSidePolygon(_DensifiedPolygon);
            if (!_Triangles) return std::nullopt;
            if (PatchCount_ + _Triangles->size() > Options_.MaximumToolPatches)
                throw std::runtime_error("二维包覆轨迹过于密集，超过切刀片数量上限");

            std::vector<SNormalVolumeNode> _Nodes;
            _Nodes.reserve(_DensifiedPolygon.size());
            for (const auto& _Point : _DensifiedPolygon)
            {
                const auto _Surface = EvaluateSideSurface(
                    Tube_, Profiles_, Tube_.First + _Point.S, _Point.U, Options_);
                if (!_Surface) return std::nullopt;
                const auto _RayDepth = SideMaterialRayDepth(
                    Shape_, _Surface->Point, _Surface->Normal,
                    Options_.OuterClearance, Reach_, Options_.DistanceTolerance);
                const auto _Depth = _RayDepth.value_or(Reach_ * 0.5)
                    + Options_.InnerClearance;
                if (!std::isfinite(_Depth) || _Depth <= Options_.DistanceTolerance)
                    return std::nullopt;
                _Nodes.push_back({
                    _Surface->Point.Translated(gp_Vec(_Surface->Normal)
                        * Options_.OuterClearance),
                    _Surface->Point.Translated(-gp_Vec(_Surface->Normal) * _Depth) });
            }

            std::vector<TopoDS_Face> _Faces;
            _Faces.reserve(_Triangles->size() * 2 + _DensifiedPolygon.size() * 2);
            std::map<std::pair<std::size_t, std::size_t>, SNormalVolumeEdgeUse> _Edges;
            const auto _RecordEdge = [&_Edges](
                const std::size_t A_, const std::size_t B_) {
                auto _Key = std::minmax(A_, B_);
                auto& _Use = _Edges[_Key];
                ++_Use.Count;
                if (_Use.Count == 1)
                {
                    _Use.First = A_;
                    _Use.Second = B_;
                }
            };
            for (const auto& _Triangle : *_Triangles)
            {
                const auto _A = _Triangle[0];
                const auto _B = _Triangle[1];
                const auto _C = _Triangle[2];
                _Faces.push_back(MakeSideTriangleFace(
                    _Nodes[_A].Inner, _Nodes[_C].Inner, _Nodes[_B].Inner));
                _Faces.push_back(MakeSideTriangleFace(
                    _Nodes[_A].Outer, _Nodes[_B].Outer, _Nodes[_C].Outer));
                _RecordEdge(_A, _B);
                _RecordEdge(_B, _C);
                _RecordEdge(_C, _A);
            }
            for (const auto& [_Key, _Use] : _Edges)
            {
                (void)_Key;
                if (_Use.Count != 1) continue;
                const auto _A = _Use.First;
                const auto _B = _Use.Second;
                _Faces.push_back(MakeSideTriangleFace(
                    _Nodes[_A].Inner, _Nodes[_B].Inner, _Nodes[_B].Outer));
                _Faces.push_back(MakeSideTriangleFace(
                    _Nodes[_A].Inner, _Nodes[_B].Outer, _Nodes[_A].Outer));
            }

            BRepBuilderAPI_Sewing _Sewing(
                std::max(Options_.DistanceTolerance * 10.0, 1.0e-5),
                true, true, true, false);
            for (const auto& _Face : _Faces) _Sewing.Add(_Face);
            _Sewing.Perform();
            const auto _Sewed = _Sewing.SewedShape();
            if (_Sewed.IsNull()) return std::nullopt;
            TopoDS_Shell _Shell;
            if (_Sewed.ShapeType() == TopAbs_SHELL)
            {
                _Shell = TopoDS::Shell(_Sewed);
            }
            else
            {
                TopExp_Explorer _Shells(_Sewed, TopAbs_SHELL);
                if (!_Shells.More()) return std::nullopt;
                _Shell = TopoDS::Shell(_Shells.Current());
                _Shells.Next();
                if (_Shells.More()) return std::nullopt;
            }
            BRepBuilderAPI_MakeSolid _Solid(_Shell);
            _Solid.Build();
            if (!_Solid.IsDone() || _Solid.Shape().IsNull()
                || !BRepCheck_Analyzer(_Solid.Shape()).IsValid()) return std::nullopt;
            PatchCount_ += _Triangles->size();
            return _Solid.Shape();
        }

        double SideShapeVolume(const TopoDS_Shape& Shape_)
        {
            if (Shape_.IsNull()) return 0.0;
            GProp_GProps _Properties;
            BRepGProp::VolumeProperties(Shape_, _Properties);
            return std::abs(_Properties.Mass());
        }
    }

    STubeSideSketchResult ApplyTubeSideSketch(
        IN const TopoDS_Shape& Shape_, IN const ObjectMap& Sketch_,
        IN const STubeSideSketchOptions& Options_)
    {
        STubeSideSketchResult _Result;
        _Result.Method = "wrapped-normal-trajectory";
        try
        {
            if (Shape_.IsNull())
            {
                _Result.Diagnostic = "输入 BRep 为空";
                return _Result;
            }
            if (!std::isfinite(Options_.DistanceTolerance)
                || Options_.DistanceTolerance <= 0.0
                || !std::isfinite(Options_.AngularToleranceRadians)
                || Options_.AngularToleranceRadians <= 0.0
                || Options_.StationCount < 5 || Options_.StationCount > 65
                || Options_.SamplesPerCurve < 9 || Options_.SamplesPerCurve > 513
                || Options_.MaximumToolPatches < 1
                || !std::isfinite(Options_.TrajectoryWidth)
                || Options_.TrajectoryWidth <= 0.0
                || !std::isfinite(Options_.OuterClearance)
                || Options_.OuterClearance <= 0.0
                || !std::isfinite(Options_.InnerClearance)
                || Options_.InnerClearance <= 0.0)
            {
                throw std::invalid_argument("二维包覆参数无效");
            }
            const auto _Entities = Sketch_.find("entities");
            if (_Entities == Sketch_.end() || !_Entities->second.Is<VariantArray>())
                throw std::invalid_argument("二维包覆缺少轨迹实体");
            const auto _Length = SideSketchNumber(Sketch_, "length");
            const auto _Height = SideSketchNumber(Sketch_, "faceHeight");
            if (!_Length || !_Height || *_Length <= Precision::Confusion()
                || *_Height <= Precision::Confusion())
                throw std::invalid_argument("二维包覆坐标范围无效");
            if (_Entities->second.To<VariantArray>().empty())
            {
                _Result.bOK = true;
                _Result.Shape = Shape_;
                _Result.Changed = false;
                _Result.Approximate = false;
                _Result.Diagnostic = "空轨迹，不生成切除体";
                return _Result;
            }

            STubeBRepUnfoldingOptions _UnfoldOptions;
            _UnfoldOptions.DistanceTolerance = Options_.DistanceTolerance;
            _UnfoldOptions.AngularToleranceRadians = Options_.AngularToleranceRadians;
            _UnfoldOptions.StationCount = Options_.StationCount;
            _UnfoldOptions.SamplesPerCurve = Options_.SamplesPerCurve;
            _UnfoldOptions.MaximumOutputPoints = 500000;
            _UnfoldOptions.IncludeInnerSurfaces = false;
            _UnfoldOptions.IncludeBoundaryWires = false;
            const auto _Tube = NormalizeTube(Shape_, _UnfoldOptions);
            if (!_Tube)
            {
                _Result.Diagnostic = "无法确定管材轴向和闭合截面";
                return _Result;
            }
            const auto _LengthValue = _Tube->Last - _Tube->First;
            const auto _Profiles = BuildProfiles(
                _Tube->Shape, _Tube->Frame, _Tube->First, _Tube->Last, _UnfoldOptions);
            if (_Profiles.Lanes.empty() || _Profiles.Lanes.front().empty())
            {
                _Result.Diagnostic = "无法恢复管材侧壁截面";
                return _Result;
            }
            const auto _Period = _Profiles.Lanes.front().front().Period;
            if (_LengthValue <= Precision::Confusion() || _Period <= Precision::Confusion())
            {
                _Result.Diagnostic = "管材没有有效的轴向长度或周长";
                return _Result;
            }
            auto _Trajectories = ReadSideSketchTrajectories(
                Sketch_, *_Length, *_Height, _LengthValue, _Period);
            if (_Trajectories.empty())
            {
                _Result.bOK = true;
                _Result.Shape = Shape_;
                _Result.Changed = false;
                _Result.Approximate = false;
                _Result.Diagnostic = "草图只包含说明文字，没有可切割轨迹";
                return _Result;
            }

            Bnd_Box _Box;
            BRepBndLib::AddOptimal(_Tube->Shape, _Box, false, false);
            _Box.SetGap(0.0);
            double _X0 = 0.0, _Y0 = 0.0, _Z0 = 0.0;
            double _X1 = 0.0, _Y1 = 0.0, _Z1 = 0.0;
            _Box.Get(_X0, _Y0, _Z0, _X1, _Y1, _Z1);
            const auto _Reach = std::max(100.0,
                std::hypot(_X1 - _X0, std::hypot(_Y1 - _Y0, _Z1 - _Z0)) * 2.0);
            std::vector<TopoDS_Shape> _Tools;
            _Tools.reserve(_Trajectories.size());
            for (auto& _Trajectory : _Trajectories)
            {
                _Result.TrajectoryPointCount += _Trajectory.Points.size();
                if (_Trajectory.Closed) ++_Result.ClosedLoopCount;
                else ++_Result.OpenTrajectoryCount;
                const auto _Polygon = _Trajectory.Closed
                    ? _Trajectory.Points
                    : MakeOpenTrajectoryRibbon(_Trajectory.Points, Options_.TrajectoryWidth);
                if (_Polygon.size() < 3) continue;
                auto _Tool = BuildNormalSideVolume(
                    _Tube->Shape, *_Tube, _Profiles, _Polygon, Options_, _Reach,
                    _Result.ToolPatchCount);
                if (_Tool) _Tools.push_back(std::move(*_Tool));
            }
            if (_Tools.empty())
            {
                _Result.Diagnostic = "轨迹没有形成有效的法向切刀";
                return _Result;
            }

            BRep_Builder _Builder;
            TopoDS_Compound _ToolCompound;
            _Builder.MakeCompound(_ToolCompound);
            for (const auto& _Tool : _Tools) _Builder.Add(_ToolCompound, _Tool);
            BRepAlgoAPI_Cut _Cut(_Tube->Shape, _ToolCompound);
            _Cut.SetNonDestructive(true);
            _Cut.SetFuzzyValue(Options_.DistanceTolerance);
            _Cut.Build();
            if (!_Cut.IsDone() || _Cut.Shape().IsNull())
            {
                _Result.Diagnostic = "法向包覆切除失败";
                return _Result;
            }
            auto _Shape = _Cut.Shape();
            if (!BRepCheck_Analyzer(_Shape).IsValid())
            {
                _Result.Diagnostic = "法向包覆切除产生了无效 BRep";
                return _Result;
            }
            const auto _BeforeVolume = SideShapeVolume(_Tube->Shape);
            const auto _AfterVolume = SideShapeVolume(_Shape);
            if (_BeforeVolume > Precision::Confusion()
                && _AfterVolume >= _BeforeVolume - Options_.DistanceTolerance
                    * Options_.DistanceTolerance * Options_.DistanceTolerance)
            {
                _Result.Diagnostic = "轨迹没有穿过当前管壁材料";
                return _Result;
            }
            if (_Tube->HasNormalizedToSource)
                _Shape = BRepBuilderAPI_Transform(
                    _Shape, _Tube->NormalizedToSource, true).Shape();
            _Result.bOK = true;
            _Result.Changed = true;
            _Result.Shape = std::move(_Shape);
            _Result.Approximate = true;
            _Result.Diagnostic = "已按侧面轨迹逐点取局部法向，并沿当前管壁厚度切除";
            return _Result;
        }
        catch (const Standard_Failure& _Error)
        {
            _Result.bOK = false;
            _Result.Diagnostic = std::string("二维包覆应用失败: ") + _Error.what();
            return _Result;
        }
        catch (const std::exception& _Error)
        {
            _Result.bOK = false;
            _Result.Diagnostic = std::string("二维包覆应用失败: ") + _Error.what();
            return _Result;
        }
    }

    STubeSideSketchResult ApplyTubeSideSketch(
        IN const iCAX::GeometryData::BRepModel& BRep_, IN const ObjectMap& Sketch_,
        IN const STubeSideSketchOptions& Options_)
    {
        try
        {
            const auto _Built = iCAX::OpenCascade::BuildOpenCascadeShape(BRep_);
            if (!_Built.bOK || _Built.Shape.IsNull())
            {
                STubeSideSketchResult _Result;
                _Result.Diagnostic = "无法从 BRepModel 重建 OCC Shape";
                return _Result;
            }
            return ApplyTubeSideSketch(_Built.Shape, Sketch_, Options_);
        }
        catch (const Standard_Failure& _Error)
        {
            STubeSideSketchResult _Result;
            _Result.Diagnostic = std::string("BRepModel 重建失败: ") + _Error.what();
            return _Result;
        }
        catch (const std::exception& _Error)
        {
            STubeSideSketchResult _Result;
            _Result.Diagnostic = std::string("BRepModel 重建失败: ") + _Error.what();
            return _Result;
        }
    }

    namespace
    {
        constexpr double kNestingLengthScale = 100.0;

        [[nodiscard]] double WrapU(const double _U, const double _Period)
        {
            if (!std::isfinite(_U) || !std::isfinite(_Period) || _Period <= 0.0) return 0.0;
            double _Wrapped = std::fmod(_U, _Period);
            if (_Wrapped < 0.0) _Wrapped += _Period;
            return _Wrapped;
        }

        [[nodiscard]] std::int64_t QuantizeNestingLength(const double _Value)
        {
            if (!std::isfinite(_Value))
                return (std::numeric_limits<std::int64_t>::max)();
            const long double _Scaled = static_cast<long double>(_Value) * kNestingLengthScale;
            if (_Scaled >= static_cast<long double>((std::numeric_limits<std::int64_t>::max)()))
                return (std::numeric_limits<std::int64_t>::max)();
            if (_Scaled <= static_cast<long double>((std::numeric_limits<std::int64_t>::min)()))
                return (std::numeric_limits<std::int64_t>::min)();
            return static_cast<std::int64_t>(std::llround(_Scaled));
        }

        void AddWireSegmentCandidates(
            const STubeUnfoldedWire& _Wire, const double _Period,
            const std::size_t _SampleCount, std::vector<std::vector<double>>& _Candidates)
        {
            if (_Wire.Points.size() < 2 || _Period <= 0.0) return;
            const auto _AddSegment = [&](const STubeUnfoldedPoint& _Left,
                                         const STubeUnfoldedPoint& _Right)
            {
                const double _U0 = WrapU(_Left.U, _Period);
                double _U1 = WrapU(_Right.U, _Period);
                double _Delta = _U1 - _U0;
                while (_Delta > _Period * 0.5) _Delta -= _Period;
                while (_Delta < -_Period * 0.5) _Delta += _Period;
                _U1 = _U0 + _Delta;
                const double _Tolerance = _Period / _SampleCount * 0.05;
                for (std::size_t _Index = 0; _Index < _SampleCount; ++_Index)
                {
                    const double _Target = (_Index + 0.5) * _Period / _SampleCount;
                    for (int _Copy = -1; _Copy <= 1; ++_Copy)
                    {
                        const double _A = _U0 + _Copy * _Period;
                        const double _B = _U1 + _Copy * _Period;
                        if (std::abs(_B - _A) <= _Tolerance)
                        {
                            if (std::abs(_Target - _A) > _Period / _SampleCount) continue;
                            _Candidates[_Index].push_back((_Left.S + _Right.S) * 0.5);
                            continue;
                        }
                        const double _Minimum = (std::min)(_A, _B) - _Tolerance;
                        const double _Maximum = (std::max)(_A, _B) + _Tolerance;
                        if (_Target < _Minimum || _Target > _Maximum) continue;
                        const double _T = std::clamp((_Target - _A) / (_B - _A), 0.0, 1.0);
                        _Candidates[_Index].push_back(
                            _Left.S + (_Right.S - _Left.S) * _T);
                    }
                }
            };
            for (std::size_t _Index = 1; _Index < _Wire.Points.size(); ++_Index)
                _AddSegment(_Wire.Points[_Index - 1], _Wire.Points[_Index]);
            if (_Wire.Closed)
                _AddSegment(_Wire.Points.back(), _Wire.Points.front());
        }

        [[nodiscard]] std::vector<double> BuildEndProfile(
            const std::vector<const STubeUnfoldedWire*>& _Wires,
            const double _Period, const std::size_t _SampleCount, const bool _LeftEnd)
        {
            std::vector<std::vector<double>> _Candidates(_SampleCount);
            std::vector<STubeUnfoldedPoint> _Fallback;
            for (const auto* _Wire : _Wires)
            {
                if (!_Wire) continue;
                _Fallback.insert(_Fallback.end(), _Wire->Points.begin(), _Wire->Points.end());
                AddWireSegmentCandidates(*_Wire, _Period, _SampleCount, _Candidates);
            }
            std::vector<double> _Result(_SampleCount, 0.0);
            for (std::size_t _Index = 0; _Index < _SampleCount; ++_Index)
            {
                if (_Candidates[_Index].empty() && !_Fallback.empty())
                {
                    const double _Target = (_Index + 0.5) * _Period / _SampleCount;
                    const auto _Nearest = std::min_element(
                        _Fallback.begin(), _Fallback.end(), [&](const auto& _A, const auto& _B)
                        {
                            const auto _Distance = [&](const auto& _Point)
                            {
                                const double _Difference = std::abs(
                                    WrapU(_Point.U, _Period) - _Target);
                                return (std::min)(_Difference, _Period - _Difference);
                            };
                            return _Distance(_A) < _Distance(_B);
                        });
                    _Candidates[_Index].push_back(_Nearest->S);
                }
                if (_Candidates[_Index].empty()) return {};
                _Result[_Index] = _LeftEnd
                    ? *std::min_element(_Candidates[_Index].begin(), _Candidates[_Index].end())
                    : *std::max_element(_Candidates[_Index].begin(), _Candidates[_Index].end());
            }
            return _Result;
        }

        [[nodiscard]] iCAX::Data::ObjectMap SerializeCutLineFeature(
            const iCAX::TubeNesting::CutLineFeatureCode& _Feature)
        {
            iCAX::Data::VariantArray _Samples;
            for (const auto _Value : _Feature.Samples)
                _Samples.emplace_back(static_cast<double>(_Value) / kNestingLengthScale);
            return {
                { "schema", _Feature.Schema },
                { "kind", static_cast<unsigned long long>(_Feature.Kind) },
                { "period", static_cast<double>(_Feature.Period) / kNestingLengthScale },
                { "minimum", static_cast<double>(_Feature.Minimum) / kNestingLengthScale },
                { "maximum", static_cast<double>(_Feature.Maximum) / kNestingLengthScale },
                { "mean", static_cast<double>(_Feature.Mean) / kNestingLengthScale },
                { "totalVariation", static_cast<double>(_Feature.TotalVariation)
                    / kNestingLengthScale },
                { "linearStart", static_cast<double>(_Feature.LinearStart)
                    / kNestingLengthScale },
                { "linearEnd", static_cast<double>(_Feature.LinearEnd)
                    / kNestingLengthScale },
                { "constantValue", static_cast<double>(_Feature.ConstantValue)
                    / kNestingLengthScale },
                { "classKey", _Feature.ClassKey },
                { "fingerprint", static_cast<unsigned long long>(_Feature.Fingerprint) },
                { "samples", std::move(_Samples) }
            };
        }
    }

    STubeUnfoldedEndFeatureCodes EncodeTubeUnfoldedEndFeatures(
        IN const STubeBRepUnfoldingResult& _Result, const std::size_t _RequestedSampleCount)
    {
        STubeUnfoldedEndFeatureCodes _Output;
        if (!_Result.bOK || !_Result.Periodic || !std::isfinite(_Result.Perimeter)
            || _Result.Perimeter <= 0.0)
        {
            _Output.Diagnostic = "展开结果没有可用于排样的周期截面";
            return _Output;
        }
        const auto _SampleCount = std::clamp(
            _RequestedSampleCount, std::size_t{ 8 }, std::size_t{ 256 });
        const STubeUnfoldedSurface* _Outer = nullptr;
        for (const auto& _Surface : _Result.Surfaces)
            if (!_Surface.Inner && _Surface.UPeriod > 0.0)
            {
                _Outer = &_Surface;
                break;
            }
        if (!_Outer)
        {
            _Output.Diagnostic = "展开结果没有 outer 侧面";
            return _Output;
        }

        struct SEndWire final
        {
            const STubeUnfoldedWire* Wire = nullptr;
            double Average = 0.0;
        };
        std::vector<SEndWire> _EndWires;
        double _AxisMinimum = std::numeric_limits<double>::infinity();
        double _AxisMaximum = -std::numeric_limits<double>::infinity();
        for (const auto& _Wire : _Outer->Wires)
        {
            if (_Wire.Role != "end-boundary" || _Wire.Points.size() < 2) continue;
            double _Sum = 0.0;
            for (const auto& _Point : _Wire.Points)
            {
                if (!std::isfinite(_Point.S) || !std::isfinite(_Point.U)) continue;
                _AxisMinimum = (std::min)(_AxisMinimum, _Point.S);
                _AxisMaximum = (std::max)(_AxisMaximum, _Point.S);
                _Sum += _Point.S;
            }
            const double _Average = _Sum / static_cast<double>(_Wire.Points.size());
            _EndWires.push_back({ &_Wire, _Average });
        }
        const double _Midpoint = (_AxisMinimum + _AxisMaximum) * 0.5;
        std::vector<const STubeUnfoldedWire*> _LeftWires, _RightWires;
        for (const auto& _EndWire : _EndWires)
            (_EndWire.Average <= _Midpoint ? _LeftWires : _RightWires).push_back(_EndWire.Wire);
        if (_LeftWires.empty() || _RightWires.empty()
            || !std::isfinite(_AxisMinimum) || !std::isfinite(_AxisMaximum)
            || _AxisMaximum <= _AxisMinimum)
        {
            _Output.Diagnostic = "展开结果没有同时识别出首端和尾端曲线";
            return _Output;
        }

        const double _Period = _Outer->UPeriod > 0.0 ? _Outer->UPeriod : _Result.Perimeter;
        auto _LeftProfile = BuildEndProfile(_LeftWires, _Period, _SampleCount, true);
        auto _RightProfile = BuildEndProfile(_RightWires, _Period, _SampleCount, false);
        if (_LeftProfile.empty() || _RightProfile.empty())
        {
            _Output.Diagnostic = "端部曲线无法覆盖完整截面周向";
            return _Output;
        }
        for (std::size_t _Index = 0; _Index < _SampleCount; ++_Index)
        {
            _LeftProfile[_Index] -= _AxisMinimum;
            _RightProfile[_Index] -= _AxisMaximum;
        }
        std::vector<iCAX::TubeNesting::Length> _LeftQuantized, _RightQuantized;
        _LeftQuantized.reserve(_SampleCount);
        _RightQuantized.reserve(_SampleCount);
        for (const auto _Value : _LeftProfile)
            _LeftQuantized.push_back(QuantizeNestingLength(_Value));
        for (const auto _Value : _RightProfile)
            _RightQuantized.push_back(QuantizeNestingLength(_Value));
        const auto _PeriodQuantized = QuantizeNestingLength(_Period);
        _Output.Left = iCAX::TubeNesting::EncodeCutLineFeature(
            _PeriodQuantized, _LeftQuantized);
        _Output.Right = iCAX::TubeNesting::EncodeCutLineFeature(
            _PeriodQuantized, _RightQuantized);
        _Output.bOK = iCAX::TubeNesting::IsValidCutLineFeature(_Output.Left)
            && iCAX::TubeNesting::IsValidCutLineFeature(_Output.Right);
        _Output.Diagnostic = _Output.bOK
            ? "已生成周期端部特征码" : "端部特征码量化失败";
        return _Output;
    }

    iCAX::Data::ObjectMap SerializeTubeBRepUnfolding(
        IN const STubeBRepUnfoldingResult& Result_)
    {
        const auto _NestingFeatures = EncodeTubeUnfoldedEndFeatures(Result_);
        iCAX::Data::VariantArray _Surfaces;
        for (const auto& _Surface : Result_.Surfaces)
        {
            iCAX::Data::VariantArray _Panels;
            for (const auto& _Panel : _Surface.Panels)
            {
                iCAX::Data::VariantArray _Boundaries;
                for (const auto& _Wire : _Panel.Boundaries)
                    _Boundaries.emplace_back(SerializeWire(_Wire));
                _Panels.emplace_back(iCAX::Data::ObjectMap{
                    { "id", static_cast<unsigned long long>(_Panel.ID) },
                    { "surfaceIndex", static_cast<unsigned long long>(_Panel.SurfaceIndex) },
                    { "sourceFaceIndex", static_cast<unsigned long long>(_Panel.SourceFaceIndex) },
                    { "uStart", _Panel.UStart }, { "uEnd", _Panel.UEnd },
                    { "boundaries", std::move(_Boundaries) }
                });
            }
            iCAX::Data::VariantArray _Wires;
            for (const auto& _Wire : _Surface.Wires)
                _Wires.emplace_back(SerializeWire(_Wire));
            _Surfaces.emplace_back(iCAX::Data::ObjectMap{
                { "id", static_cast<unsigned long long>(_Surface.ID) },
                { "loopId", _Surface.LoopID },
                { "inner", _Surface.Inner },
                { "uPeriod", _Surface.UPeriod },
                { "rectangle", SerializeRectangle(_Surface.Rectangle) },
                { "panels", std::move(_Panels) },
                { "wires", std::move(_Wires) }
            });
        }
        return {
            { "schema", std::string("icax.tube-brep-unfolding") },
            { "schemaVersion", 1ull },
            { "available", Result_.bOK },
            { "approximate", Result_.Approximate },
            { "periodic", Result_.Periodic },
            { "coordinateSpace", Result_.CoordinateSpace },
            { "method", Result_.Method },
            { "diagnostic", Result_.Diagnostic },
            { "axis", iCAX::Data::VariantArray{
                Result_.Axis[0], Result_.Axis[1], Result_.Axis[2] } },
            { "sectionX", iCAX::Data::VariantArray{
                Result_.SectionX[0], Result_.SectionX[1], Result_.SectionX[2] } },
            { "sectionY", iCAX::Data::VariantArray{
                Result_.SectionY[0], Result_.SectionY[1], Result_.SectionY[2] } },
            { "length", Result_.Length },
            { "perimeter", Result_.Perimeter },
            { "profileMismatch", Result_.ProfileMismatch },
            { "maximumProjectionResidual", Result_.MaximumProjectionResidual },
            { "profileStationCount",
                static_cast<unsigned long long>(Result_.ProfileStationCount) },
            { "pointCount", static_cast<unsigned long long>(Result_.PointCount) },
            { "lateralRectangle", SerializeRectangle(Result_.LateralRectangle) },
            { "nestingFeatures", iCAX::Data::ObjectMap{
                { "available", _NestingFeatures.bOK },
                { "diagnostic", _NestingFeatures.Diagnostic },
                { "left", SerializeCutLineFeature(_NestingFeatures.Left) },
                { "right", SerializeCutLineFeature(_NestingFeatures.Right) }
            } },
            { "surfaces", std::move(_Surfaces) }
        };
    }

    class COpenCascadeBRepTubeUnfoldingService final
        : public IBRepTubeUnfoldingService
    {
        AUTO_REGIST_SERVICE(
            iCAX::TubeDesigner::IBRepTubeUnfoldingService,
            COpenCascadeBRepTubeUnfoldingService)

    public:
        void OnLoad() override {}
        void OnUnload() override {}

        STubeBRepUnfoldingResult Unfold(
            IN const TopoDS_Shape& Shape_,
            IN const STubeBRepUnfoldingOptions& Options_) const override
        {
            return UnfoldTubeBRepSurface(Shape_, Options_);
        }

        STubeBRepUnfoldingResult Unfold(
            IN const iCAX::GeometryData::BRepModel& BRep_,
            IN const STubeBRepUnfoldingOptions& Options_) const override
        {
            return UnfoldTubeBRepSurface(BRep_, Options_);
        }
    };
}
