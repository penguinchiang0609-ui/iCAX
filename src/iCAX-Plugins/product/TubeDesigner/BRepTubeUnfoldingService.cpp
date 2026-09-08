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
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
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
