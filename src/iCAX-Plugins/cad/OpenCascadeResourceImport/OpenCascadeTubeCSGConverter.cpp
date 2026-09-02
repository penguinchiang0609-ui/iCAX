#include "pch.h"
#include "OpenCascadeTubeCSGConverter.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Builder.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepTools.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <NCollection_HSequence.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <Precision.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <ShapeFix_Face.hxx>
#include <TopAbs_State.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Wire.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <NCollection_Array1.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_GTrsf.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>

#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <set>

namespace iCAX::OpenCascade
{
namespace
{
    using iCAX::GeometryData::Arc2;
    using iCAX::GeometryData::BSpline2;
    using iCAX::GeometryData::Circle2;
    using iCAX::GeometryData::CurveSegment2;
    using iCAX::GeometryData::Direction2;
    using iCAX::GeometryData::Direction3;
    using iCAX::GeometryData::Ellipse2;
    using iCAX::GeometryData::NURBS2;
    using iCAX::GeometryData::Placement2;
    using iCAX::GeometryData::Point2;
    using iCAX::GeometryData::Point3;
    using iCAX::GeometryData::Polyline2;
    using iCAX::GeometryData::Segment2;
    namespace Tube = iCAX::GeometryData::Tube;

    struct SAxisCandidate final
    {
        gp_Dir Direction;
        double SourceWeight = 0.0;
        double CompatibleArea = 0.0;
        double TotalArea = 0.0;
        bool FromTangentPlaneConsensus = false;
    };

    struct STangentPlaneSample final
    {
        gp_Pnt Point;
        gp_Dir Normal;
        double Weight = 0.0;
    };

    struct SCircularConeModel final
    {
        gp_Pnt Apex;
        gp_Dir Axis;
        gp_Dir XDirection;
        double SemiAngle = 0.0;
        double EvidenceCoverage = 0.0;
        bool FromTangentPlaneConsensus = false;
    };

    struct SSectionFrame final
    {
        gp_Pnt Origin;
        gp_Dir XDirection;
        gp_Dir YDirection;
        gp_Dir ZDirection;
        double First = 0.0;
        double Last = 0.0;
    };

    struct SSliceLoop final
    {
        TopoDS_Wire Wire;
        std::vector<Point2> Points;
        double SignedArea = 0.0;
    };

    struct SSliceCandidate final
    {
        double Station = 0.0;
        gp_Pln Plane;
        std::vector<SSliceLoop> Loops;
        double MaterialArea = 0.0;
    };

    struct SRecognizedExtrusion final
    {
        Tube::SSolidNode Node;
        TopoDS_Shape FullShape;
        TopoDS_Shape ShapeInsideBase;
        double RelativeFitError = 1.0;
        double CoverageRatio = 0.0;
        double GeometryAcceptanceTolerance = 1.0e-4;
    };

    struct SRecognizedAlternative final
    {
        std::vector<Tube::SSolidNode> Nodes;
        std::string RootNodeID;
        TopoDS_Shape ShapeInsideBase;
        std::map<std::string, TopoDS_Shape> PreviewShapes;
        double RelativeFitError = 1.0;
    };

    struct SRecognitionHypothesis final
    {
        std::vector<Tube::SSolidNode> Nodes;
        std::string RootNodeID;
        TopoDS_Shape ShapeInsideBase;
        TopoDS_Shape PreviewShape;
        std::string CandidateID;
        std::string Label;
        std::string Semantic;
        std::string EvidenceCode;
        std::string EvidenceMessage;
        double Confidence = 0.0;
        double RelativeFitError = 1.0;
        double GeometryAcceptanceTolerance = 1.0e-4;
        std::size_t NestedAlternativeCount = 0;
        std::optional<Tube::SRemovalSemantics> RemovalSemantics;
    };

    struct SPartFaceEvidence final
    {
        std::uint64_t SourceShapeID = 0;
        TopoDS_Face Face;
        bool InheritedFromHost = false;
        GeomAbs_SurfaceType SurfaceType = GeomAbs_OtherSurface;
        std::vector<std::uint64_t> AdjacentFaceIDs;
    };

    struct SPartSurfaceEvidenceGraph final
    {
        std::vector<SPartFaceEvidence> Faces;
        std::map<std::string, std::string> Metadata;
    };

    struct SCylinderEvidenceGroup final
    {
        gp_Cylinder Cylinder;
        std::vector<const SPartFaceEvidence*> LateralFaces;
        bool AdjacentToCompatibleCone = false;
    };

    struct SGlobalFeatureRecognition final
    {
        std::vector<Tube::SSolidNode> Nodes;
        std::string RootNodeID;
        TopoDS_Shape ShapeInsideBase;
        TopoDS_Shape PreviewShape;
        std::vector<std::uint32_t> CoveredRemovalComponentOrdinals;
        std::size_t AlternativeCount = 0;
    };

    struct SStableAtlasEdge final
    {
        TopoDS_Edge Edge;
        std::string CurveID;
        double UFirst = 0.0;
        double ULast = 0.0;
        double ParameterFirst = 0.0;
        double ParameterLast = 0.0;
        bool Reversed = false;
    };

    struct SStableSideAtlas final
    {
        std::string LoopID;
        double UPeriod = 0.0;
        double VFirst = 0.0;
        double VLast = 0.0;
        std::vector<SStableAtlasEdge> Edges;
    };

    struct SStableAtlasProjection final
    {
        std::size_t AtlasIndex = 0;
        std::size_t EdgeIndex = 0;
        std::string CurveID;
        double U = 0.0;
        double V = 0.0;
        double NormalOffset = 0.0;
        double Residual = (std::numeric_limits<double>::max)();
        gp_Pnt SupportPoint;
        gp_Dir MaterialOutwardNormal;
    };

    std::string _ToString(IN double Value_);
    std::optional<std::pair<double, double>> _ProjectAxialRange(
        IN const TopoDS_Shape& Shape_,
        IN const gp_Dir& Axis_);

    double _Clamp(IN double Value_, IN double Min_, IN double Max_) noexcept
    {
        return std::max(Min_, std::min(Max_, Value_));
    }

    double _AbsDot(IN const gp_Dir& Left_, IN const gp_Dir& Right_) noexcept
    {
        return std::abs(Left_.Dot(Right_));
    }

    gp_Dir _CanonicalDirection(IN gp_Dir Direction_)
    {
        const auto _X = Direction_.X();
        const auto _Y = Direction_.Y();
        const auto _Z = Direction_.Z();
        const auto _AbsX = std::abs(_X);
        const auto _AbsY = std::abs(_Y);
        const auto _AbsZ = std::abs(_Z);
        const bool _Reverse = (_AbsX >= _AbsY && _AbsX >= _AbsZ && _X < 0.0)
            || (_AbsY > _AbsX && _AbsY >= _AbsZ && _Y < 0.0)
            || (_AbsZ > _AbsX && _AbsZ > _AbsY && _Z < 0.0);
        if (_Reverse)
        {
            Direction_.Reverse();
        }
        return Direction_;
    }

    void _AddAxisCandidate(
        IN OUT std::vector<SAxisCandidate>& Candidates_,
        IN const gp_Dir& Direction_,
        IN double dWeight_,
        IN bool bFromTangentPlaneConsensus_ = false)
    {
        const auto _Direction = _CanonicalDirection(Direction_);
        for (auto& _Candidate : Candidates_)
        {
            if (_AbsDot(_Candidate.Direction, _Direction) >= 1.0 - 1.0e-6)
            {
                _Candidate.SourceWeight += dWeight_;
                _Candidate.FromTangentPlaneConsensus =
                    _Candidate.FromTangentPlaneConsensus
                    || bFromTangentPlaneConsensus_;
                return;
            }
        }
        Candidates_.push_back({
            _Direction,
            dWeight_,
            0.0,
            0.0,
            bFromTangentPlaneConsensus_
        });
    }

    double _ShapeVolume(IN const TopoDS_Shape& Shape_)
    {
        if (Shape_.IsNull())
        {
            return 0.0;
        }
        GProp_GProps _Properties;
        BRepGProp::VolumeProperties(Shape_, _Properties);
        return std::abs(_Properties.Mass());
    }

    double _FaceArea(IN const TopoDS_Face& Face_)
    {
        GProp_GProps _Properties;
        BRepGProp::SurfaceProperties(Face_, _Properties);
        return std::abs(_Properties.Mass());
    }

    double _ShapeArea(IN const TopoDS_Shape& Shape_)
    {
        if (Shape_.IsNull())
        {
            return 0.0;
        }
        GProp_GProps _Properties;
        BRepGProp::SurfaceProperties(Shape_, _Properties);
        return std::abs(_Properties.Mass());
    }

    struct SRemovalExtentAnalysis final
    {
        Tube::SRemovalSemantics Semantics;
        std::vector<double> InternalTerminationStations;
        std::vector<std::pair<double, double>> BoundaryOpeningRanges;
    };

    bool _IsFaceOnBaseBoundary(
        IN const TopoDS_Face& Face_,
        IN const TopoDS_Shape& Base_,
        IN const STubeCSGConversionOptions& Options_)
    {
        const auto _Area = _FaceArea(Face_);
        if (_Area <= Options_.Tolerance * Options_.Tolerance)
        {
            return false;
        }
        BRepAdaptor_Surface _Surface(Face_, true);
        const auto _UFirst = _Surface.FirstUParameter();
        const auto _ULast = _Surface.LastUParameter();
        const auto _VFirst = _Surface.FirstVParameter();
        const auto _VLast = _Surface.LastVParameter();
        if (std::isfinite(_UFirst) && std::isfinite(_ULast)
            && std::isfinite(_VFirst) && std::isfinite(_VLast))
        {
            BRepClass3d_SolidClassifier _Classifier(Base_);
            std::size_t _SampleCount = 0;
            for (int _UIndex = 1; _UIndex <= 3; ++_UIndex)
            {
                for (int _VIndex = 1; _VIndex <= 3; ++_VIndex)
                {
                    const auto _U = _UFirst + (_ULast - _UFirst)
                        * (static_cast<double>(_UIndex) / 4.0);
                    const auto _V = _VFirst + (_VLast - _VFirst)
                        * (static_cast<double>(_VIndex) / 4.0);
                    try
                    {
                        gp_Pnt _Point;
                        _Surface.D0(_U, _V, _Point);
                        _Classifier.Perform(_Point, 5.0 * Options_.Tolerance);
                        ++_SampleCount;
                        if (_Classifier.State() != TopAbs_ON)
                        {
                            return false;
                        }
                    }
                    catch (const Standard_Failure&)
                    {
                        continue;
                    }
                }
            }
            if (_SampleCount > 0)
            {
                return true;
            }
        }

        /* 极退化参数域才回退到精确但昂贵的面布尔覆盖检查。 */
        BRep_Builder _Builder;
        TopoDS_Compound _BoundaryFaces;
        _Builder.MakeCompound(_BoundaryFaces);
        for (TopExp_Explorer _Explorer(Base_, TopAbs_FACE);
            _Explorer.More();
            _Explorer.Next())
        {
            _Builder.Add(_BoundaryFaces, _Explorer.Current());
        }
        BRepAlgoAPI_Common _Common(Face_, _BoundaryFaces);
        _Common.SetFuzzyValue(Options_.Tolerance);
        _Common.Build();
        const auto _CommonArea = _Common.IsDone()
            ? _ShapeArea(_Common.Shape())
            : 0.0;
        return _CommonArea >= _Area * (1.0 - 1.0e-4);
    }

    bool _AreShapeSamplesInsideOrOn(
        IN const TopoDS_Shape& SampledShape_,
        IN const TopoDS_Shape& Container_,
        IN double dTolerance_)
    {
        BRepClass3d_SolidClassifier _Classifier(Container_);
        const auto _AcceptPoint = [&_Classifier, dTolerance_](IN const gp_Pnt& Point_) {
            _Classifier.Perform(Point_, 5.0 * dTolerance_);
            return _Classifier.State() != TopAbs_OUT;
        };
        for (TopExp_Explorer _Explorer(SampledShape_, TopAbs_VERTEX);
            _Explorer.More();
            _Explorer.Next())
        {
            if (!_AcceptPoint(BRep_Tool::Pnt(TopoDS::Vertex(_Explorer.Current()))))
            {
                return false;
            }
        }
        for (TopExp_Explorer _Explorer(SampledShape_, TopAbs_FACE);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Face = TopoDS::Face(_Explorer.Current());
            BRepAdaptor_Surface _Surface(_Face, true);
            const auto _UFirst = _Surface.FirstUParameter();
            const auto _ULast = _Surface.LastUParameter();
            const auto _VFirst = _Surface.FirstVParameter();
            const auto _VLast = _Surface.LastVParameter();
            if (!std::isfinite(_UFirst) || !std::isfinite(_ULast)
                || !std::isfinite(_VFirst) || !std::isfinite(_VLast))
            {
                continue;
            }
            for (int _UIndex = 1; _UIndex <= 4; ++_UIndex)
            {
                for (int _VIndex = 1; _VIndex <= 4; ++_VIndex)
                {
                    const auto _U = _UFirst + (_ULast - _UFirst)
                        * (static_cast<double>(_UIndex) / 5.0);
                    const auto _V = _VFirst + (_VLast - _VFirst)
                        * (static_cast<double>(_VIndex) / 5.0);
                    BRepClass_FaceClassifier _FaceClassifier(
                        _Face,
                        gp_Pnt2d(_U, _V),
                        dTolerance_,
                        true);
                    if (_FaceClassifier.State() == TopAbs_OUT)
                    {
                        continue;
                    }
                    try
                    {
                        gp_Pnt _Point;
                        _Surface.D0(_U, _V, _Point);
                        if (!_AcceptPoint(_Point))
                        {
                            return false;
                        }
                    }
                    catch (const Standard_Failure&)
                    {
                        continue;
                    }
                }
            }
        }
        return true;
    }

    SPartSurfaceEvidenceGraph _BuildPartSurfaceEvidenceGraph(
        IN const TopoDS_Shape& Part_,
        IN const TopoDS_Shape& Base_,
        IN const STubeCSGConversionOptions& Options_)
    {
        SPartSurfaceEvidenceGraph _Graph;
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> _Faces;
        TopExp::MapShapes(Part_, TopAbs_FACE, _Faces);
        NCollection_IndexedDataMap<
            TopoDS_Shape,
            NCollection_List<TopoDS_Shape>,
            TopTools_ShapeMapHasher> _EdgeFaces;
        TopExp::MapShapesAndAncestors(
            Part_,
            TopAbs_EDGE,
            TopAbs_FACE,
            _EdgeFaces);
        _Graph.Faces.reserve(static_cast<std::size_t>(_Faces.Extent()));
        std::size_t _InheritedCount = 0;
        for (int _FaceIndex = 1; _FaceIndex <= _Faces.Extent(); ++_FaceIndex)
        {
            SPartFaceEvidence _Evidence;
            _Evidence.SourceShapeID = static_cast<std::uint64_t>(_FaceIndex);
            _Evidence.Face = TopoDS::Face(_Faces(_FaceIndex));
            _Evidence.InheritedFromHost = _IsFaceOnBaseBoundary(
                _Evidence.Face,
                Base_,
                Options_);
            if (_Evidence.InheritedFromHost)
            {
                ++_InheritedCount;
            }
            BRepAdaptor_Surface _Surface(_Evidence.Face, true);
            _Evidence.SurfaceType = _Surface.GetType();
            std::set<std::uint64_t> _AdjacentIDs;
            for (TopExp_Explorer _EdgeExplorer(_Evidence.Face, TopAbs_EDGE);
                _EdgeExplorer.More();
                _EdgeExplorer.Next())
            {
                if (!_EdgeFaces.Contains(_EdgeExplorer.Current()))
                {
                    continue;
                }
                const auto& _AdjacentFaces = _EdgeFaces.FindFromKey(_EdgeExplorer.Current());
                for (NCollection_List<TopoDS_Shape>::Iterator _AdjacentIter(_AdjacentFaces);
                    _AdjacentIter.More();
                    _AdjacentIter.Next())
                {
                    const auto _AdjacentIndex = _Faces.FindIndex(_AdjacentIter.Value());
                    if (_AdjacentIndex > 0 && _AdjacentIndex != _FaceIndex)
                    {
                        _AdjacentIDs.insert(static_cast<std::uint64_t>(_AdjacentIndex));
                    }
                }
            }
            _Evidence.AdjacentFaceIDs.assign(_AdjacentIDs.begin(), _AdjacentIDs.end());
            _Graph.Faces.push_back(std::move(_Evidence));
        }
        _Graph.Metadata["sourceFaceCount"] = std::to_string(_Graph.Faces.size());
        _Graph.Metadata["inheritedFaceCount"] = std::to_string(_InheritedCount);
        _Graph.Metadata["generatedFaceCount"] = std::to_string(
            _Graph.Faces.size() - _InheritedCount);
        return _Graph;
    }

    bool _AreSameCylinder(
        IN const gp_Cylinder& Left_,
        IN const gp_Cylinder& Right_,
        IN const STubeCSGConversionOptions& Options_)
    {
        if (_AbsDot(Left_.Axis().Direction(), Right_.Axis().Direction())
                < 1.0 - Options_.AngularToleranceRadians
            || std::abs(Left_.Radius() - Right_.Radius()) > Options_.Tolerance)
        {
            return false;
        }
        const gp_Vec _Offset(Left_.Location(), Right_.Location());
        return _Offset.Crossed(gp_Vec(Left_.Axis().Direction())).Magnitude()
            <= Options_.Tolerance;
    }

    bool _IsConeCoaxialWithCylinder(
        IN const gp_Cone& Cone_,
        IN const gp_Cylinder& Cylinder_,
        IN const STubeCSGConversionOptions& Options_)
    {
        if (_AbsDot(Cone_.Axis().Direction(), Cylinder_.Axis().Direction())
            < 1.0 - Options_.AngularToleranceRadians)
        {
            return false;
        }
        const gp_Vec _Offset(Cone_.Location(), Cylinder_.Location());
        return _Offset.Crossed(gp_Vec(Cylinder_.Axis().Direction())).Magnitude()
            <= Options_.Tolerance;
    }

    std::vector<SCylinderEvidenceGroup> _GroupPartCylinderEvidence(
        IN const SPartSurfaceEvidenceGraph& Graph_,
        IN const STubeCSGConversionOptions& Options_)
    {
        std::vector<SCylinderEvidenceGroup> _Groups;
        for (const auto& _Evidence : Graph_.Faces)
        {
            if (_Evidence.InheritedFromHost
                || _Evidence.SurfaceType != GeomAbs_Cylinder)
            {
                continue;
            }
            BRepAdaptor_Surface _Surface(_Evidence.Face, true);
            const auto _Cylinder = _Surface.Cylinder();
            auto _GroupIter = std::find_if(
                _Groups.begin(),
                _Groups.end(),
                [&_Cylinder, &Options_](IN const auto& Group_) {
                    return _AreSameCylinder(Group_.Cylinder, _Cylinder, Options_);
                });
            if (_GroupIter == _Groups.end())
            {
                SCylinderEvidenceGroup _Group;
                _Group.Cylinder = _Cylinder;
                _Group.LateralFaces.push_back(&_Evidence);
                _Groups.push_back(std::move(_Group));
            }
            else
            {
                _GroupIter->LateralFaces.push_back(&_Evidence);
            }
        }

        for (auto& _Group : _Groups)
        {
            for (const auto* _pLateral : _Group.LateralFaces)
            {
                for (const auto _AdjacentID : _pLateral->AdjacentFaceIDs)
                {
                    if (_AdjacentID == 0 || _AdjacentID > Graph_.Faces.size())
                    {
                        continue;
                    }
                    const auto& _Adjacent = Graph_.Faces[
                        static_cast<std::size_t>(_AdjacentID - 1)];
                    if (_Adjacent.InheritedFromHost
                        || _Adjacent.SurfaceType != GeomAbs_Cone)
                    {
                        continue;
                    }
                    BRepAdaptor_Surface _ConeSurface(_Adjacent.Face, true);
                    if (_IsConeCoaxialWithCylinder(
                        _ConeSurface.Cone(),
                        _Group.Cylinder,
                        Options_))
                    {
                        _Group.AdjacentToCompatibleCone = true;
                        break;
                    }
                }
                if (_Group.AdjacentToCompatibleCone)
                {
                    break;
                }
            }
        }
        return _Groups;
    }

    SRemovalExtentAnalysis _AnalyzeRemovalExtent(
        IN const TopoDS_Shape& RemovedSolid_,
        IN const TopoDS_Shape& MainBase_,
        IN const gp_Dir& ConstructionAxis_,
        IN const STubeCSGConversionOptions& Options_)
    {
        SRemovalExtentAnalysis _Result;
        auto& _Semantics = _Result.Semantics;
        _Semantics.Confidence = 0.95;
        _Semantics.Metadata["basis"] = "brep-boundary-incidence";
        std::uint32_t _PlanarTerminationPatchCount = 0;

        for (TopExp_Explorer _Explorer(RemovedSolid_, TopAbs_FACE);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Face = TopoDS::Face(_Explorer.Current());
            if (_IsFaceOnBaseBoundary(_Face, MainBase_, Options_))
            {
                ++_Semantics.BoundaryOpeningPatchCount;
                if (const auto _Range = _ProjectAxialRange(_Face, ConstructionAxis_))
                {
                    _Result.BoundaryOpeningRanges.push_back(*_Range);
                }
                continue;
            }

            BRepAdaptor_Surface _Surface(_Face, true);
            if (_Surface.GetType() != GeomAbs_Plane
                || _AbsDot(
                    _Surface.Plane().Axis().Direction(),
                    ConstructionAxis_) < 1.0 - Options_.AngularToleranceRadians)
            {
                continue;
            }
            ++_PlanarTerminationPatchCount;
            _Result.InternalTerminationStations.push_back(gp_Vec(
                gp_Pnt(0.0, 0.0, 0.0),
                _Surface.Plane().Location()).Dot(gp_Vec(ConstructionAxis_)));
        }

        if (_PlanarTerminationPatchCount > 0)
        {
            _Semantics.InternalTerminationCount = _PlanarTerminationPatchCount;
        }
        else if (_Semantics.BoundaryOpeningPatchCount >= 2)
        {
            _Semantics.InternalTerminationCount = 0;
        }
        else if (const auto _RemovedRange = _ProjectAxialRange(RemovedSolid_, ConstructionAxis_))
        {
            const auto _EndpointTolerance = std::max(
                10.0 * Options_.Tolerance,
                (_RemovedRange->second - _RemovedRange->first) * 1.0e-7);
            const auto _TouchesEndpoint = [&_Result, _EndpointTolerance](IN double dStation_) {
                return std::any_of(
                    _Result.BoundaryOpeningRanges.begin(),
                    _Result.BoundaryOpeningRanges.end(),
                    [dStation_, _EndpointTolerance](IN const auto& Range_) {
                        return Range_.first <= dStation_ + _EndpointTolerance
                            && Range_.second >= dStation_ - _EndpointTolerance;
                    });
            };
            _Semantics.InternalTerminationCount = static_cast<std::uint32_t>(
                !_TouchesEndpoint(_RemovedRange->first))
                + static_cast<std::uint32_t>(
                    !_TouchesEndpoint(_RemovedRange->second));
        }
        else
        {
            _Semantics.InternalTerminationCount = _PlanarTerminationPatchCount;
        }

        const auto _Openings = _Semantics.BoundaryOpeningPatchCount;
        const auto _Terminations = _Semantics.InternalTerminationCount;
        if (_Openings >= 2 && _Terminations == 0)
        {
            _Semantics.Extent = Tube::ERemovalExtentKind::Through;
        }
        else if (_Openings >= 1 && _Terminations >= 1)
        {
            _Semantics.Extent = Tube::ERemovalExtentKind::Blind;
        }
        else if (_Openings == 0 && _Terminations >= 2)
        {
            _Semantics.Extent = Tube::ERemovalExtentKind::Bubble;
        }
        std::sort(
            _Result.BoundaryOpeningRanges.begin(),
            _Result.BoundaryOpeningRanges.end(),
            [](IN const auto& Left_, IN const auto& Right_) {
                return (Left_.first + Left_.second) < (Right_.first + Right_.second);
            });
        return _Result;
    }

    Tube::SFeatureRelations _MakeRemovalRelations(
        IN const SRemovalExtentAnalysis& Analysis_,
        IN const std::string& strHostNodeID_,
        IN double dPrimitiveFirst_,
        IN double dPrimitiveLast_)
    {
        Tube::SFeatureRelations _Relations;
        _Relations.DependsOnNodeIDs.push_back(strHostNodeID_);
        Tube::SFeatureMaterialEffect _MaterialEffect;
        _MaterialEffect.Role = Tube::EFeatureMaterialRole::Penetration;
        _MaterialEffect.HostNodeID = strHostNodeID_;
        _MaterialEffect.Confidence = Analysis_.Semantics.Confidence;
        _Relations.MaterialEffect = std::move(_MaterialEffect);
        Tube::SFeatureExtentConstraint _Extent;
        _Extent.HostNodeID = strHostNodeID_;
        _Extent.PrimitiveFirst.Value = dPrimitiveFirst_;
        _Extent.PrimitiveLast.Value = dPrimitiveLast_;
        _Extent.Confidence = Analysis_.Semantics.Confidence;
        _Extent.Metadata["coordinateSpace"] = "feature-frame";

        switch (Analysis_.Semantics.Extent)
        {
        case Tube::ERemovalExtentKind::Through:
            _Extent.Rule = Tube::EFeatureExtentRule::ThroughSelectedHostBoundaries;
            _Extent.RecomputeAgainstHost = true;
            _Extent.PrimitiveFirst.Observability =
                Tube::EParameterObservability::Underconstrained;
            _Extent.PrimitiveLast.Observability =
                Tube::EParameterObservability::Underconstrained;
            _Extent.Metadata["editInvariant"] = "remain-through-selected-material-boundaries";
            break;
        case Tube::ERemovalExtentKind::Blind:
            _Extent.Rule = Tube::EFeatureExtentRule::BlindFromHostBoundary;
            _Extent.RecomputeAgainstHost = true;
            _Extent.Metadata["editInvariant"] = "remain-blind-from-opening";
            break;
        case Tube::ERemovalExtentKind::Bubble:
            _Extent.Rule = Tube::EFeatureExtentRule::Enclosed;
            _Extent.Metadata["editInvariant"] = "remain-enclosed";
            break;
        case Tube::ERemovalExtentKind::Unknown:
        default:
            _Extent.Rule = Tube::EFeatureExtentRule::FixedInterval;
            break;
        }

        std::uint32_t _Ordinal = 0;
        for (const auto& _Range : Analysis_.BoundaryOpeningRanges)
        {
            Tube::SMaterialBoundaryReference _Boundary;
            _Boundary.ID = "opening-" + std::to_string(++_Ordinal);
            _Boundary.HostNodeID = strHostNodeID_;
            _Boundary.TravelOrdinal = _Ordinal;
            _Boundary.WitnessStation.Value = 0.5 * (_Range.first + _Range.second);
            _Boundary.WitnessStation.Observability =
                Tube::EParameterObservability::Observed;
            _Boundary.WitnessStation.Metadata["coordinateSpace"] =
                "world-projection-on-feature-axis";
            _Extent.Boundaries.push_back(std::move(_Boundary));
        }

        if (Analysis_.Semantics.Extent == Tube::ERemovalExtentKind::Blind
            && !_Extent.Boundaries.empty()
            && !Analysis_.InternalTerminationStations.empty())
        {
            const auto _OpeningStation = _Extent.Boundaries.front().WitnessStation.Value;
            const auto _TerminationStation = Analysis_.InternalTerminationStations.front();
            _Extent.Direction = _TerminationStation >= _OpeningStation
                ? Tube::EExtentDirection::Positive
                : Tube::EExtentDirection::Negative;
        }
        _Relations.Extent = std::move(_Extent);
        return _Relations;
    }

    std::optional<Tube::SMaterialBoundaryReference> _FindClosestOpening(
        IN const Tube::SFeatureRelations& Relations_,
        IN double dStation_)
    {
        if (!Relations_.Extent || Relations_.Extent->Boundaries.empty())
        {
            return std::nullopt;
        }
        const auto _Iter = std::min_element(
            Relations_.Extent->Boundaries.begin(),
            Relations_.Extent->Boundaries.end(),
            [dStation_](IN const auto& Left_, IN const auto& Right_) {
                return std::abs(Left_.WitnessStation.Value - dStation_)
                    < std::abs(Right_.WitnessStation.Value - dStation_);
            });
        return *_Iter;
    }

    Tube::SOpeningProfileSweepNode _MakeConstantOpeningProfileSweep(
        IN const std::string& strSourceCutNodeID_,
        IN const std::string& strEvaluatedVolumeNodeID_,
        IN const Tube::SFeatureRelations& Relations_,
        IN double dOpeningStation_,
        IN double dProfileFirst_,
        IN double dProfileLast_,
        IN double dOffsetFirst_,
        IN double dOffsetLast_)
    {
        Tube::SOpeningProfileSweepNode _Sweep;
        _Sweep.SourceCutNodeID = strSourceCutNodeID_;
        _Sweep.EvaluatedVolumeNodeID = strEvaluatedVolumeNodeID_;
        if (auto _Boundary = _FindClosestOpening(Relations_, dOpeningStation_))
        {
            Tube::SFeatureOpeningRef _Opening;
            _Opening.SourceFeatureNodeID = strSourceCutNodeID_;
            _Opening.OpeningID = _Boundary->ID;
            _Opening.Boundary = std::move(_Boundary);
            _Sweep.TargetOpening = std::move(_Opening);
        }
        _Sweep.ProfileField.ContourFirst = 0.0;
        _Sweep.ProfileField.ContourLast = 1.0;
        _Sweep.ProfileField.Periodic = true;
        _Sweep.ProfileField.Interpolation =
            Tube::EOpeningProfileInterpolation::PiecewiseConstant;

        Tube::SOpeningProfileKey _Key;
        _Key.ContourParameter.Value = 0.0;
        CurveSegment2 _Segment;
        _Segment.Curve = Segment2{
            { dProfileFirst_, dOffsetFirst_ },
            { dProfileLast_, dOffsetLast_ }
        };
        _Segment.Range = { 0.0, 1.0, false };
        _Key.Profile.Segments.push_back(std::move(_Segment));
        _Key.Confidence = 0.95;
        _Sweep.ProfileField.Keys.push_back(std::move(_Key));
        _Sweep.ProfileField.Metadata["profileCoordinates"] =
            "source-direction,signed-removal-offset";
        return _Sweep;
    }

    Tube::SInterpretationEvaluation _EvaluateInterpretation(
        IN double dRelativeGeometryError_,
        IN double dEvidenceCoverage_,
        IN double dParameterObservability_,
        IN double dModelComplexity_,
        IN double dFieldComplexity_,
        IN double dPerturbationStability_,
        IN double dEditStability_,
        IN double dGeometryAcceptanceTolerance_ = 1.0e-4)
    {
        Tube::SInterpretationEvaluation _Evaluation;
        _Evaluation.GeometryAcceptanceTolerance = dGeometryAcceptanceTolerance_;
        _Evaluation.GeometryAccepted =
            dRelativeGeometryError_ <= dGeometryAcceptanceTolerance_;
        _Evaluation.RelativeGeometryError = dRelativeGeometryError_;
        /* 当前求值器只保存了对称差；分项将在候选求值器返回 missing/excess 时覆盖。 */
        _Evaluation.RelativeOvercut = dRelativeGeometryError_;
        _Evaluation.RelativeUncovered = dRelativeGeometryError_;
        _Evaluation.EvidenceCoverage = _Clamp(dEvidenceCoverage_, 0.0, 1.0);
        _Evaluation.ParameterObservability = _Clamp(dParameterObservability_, 0.0, 1.0);
        _Evaluation.ModelComplexity = _Clamp(dModelComplexity_, 0.0, 1.0);
        _Evaluation.FieldComplexity = _Clamp(dFieldComplexity_, 0.0, 1.0);
        _Evaluation.PerturbationStability = _Clamp(dPerturbationStability_, 0.0, 1.0);
        _Evaluation.EditStability = _Clamp(dEditStability_, 0.0, 1.0);
        if (_Evaluation.GeometryAccepted)
        {
            _Evaluation.RecommendationScore =
                0.24 * _Evaluation.EvidenceCoverage
                + 0.18 * _Evaluation.ParameterObservability
                + 0.18 * _Evaluation.PerturbationStability
                + 0.24 * _Evaluation.EditStability
                + 0.08 * (1.0 - _Evaluation.ModelComplexity)
                + 0.08 * (1.0 - _Evaluation.FieldComplexity);
        }
        _Evaluation.Metadata["geometryErrorBreakdown"] =
            "symmetric-difference-only";
        return _Evaluation;
    }

    std::vector<std::uint32_t> _FindCoveredRemovalComponents(
        IN const TopoDS_Shape& CandidateInsideBase_,
        IN const TopoDS_Shape& FullRemovalShape_,
        IN const STubeCSGConversionOptions& Options_)
    {
        std::vector<std::uint32_t> _Ordinals;
        std::uint32_t _Ordinal = 0;
        const auto _VolumeTolerance = Options_.Tolerance
            * Options_.Tolerance
            * Options_.Tolerance;
        for (TopExp_Explorer _Explorer(FullRemovalShape_, TopAbs_SOLID);
            _Explorer.More();
            _Explorer.Next())
        {
            ++_Ordinal;
            BRepAlgoAPI_Common _Common(CandidateInsideBase_, _Explorer.Current());
            _Common.SetFuzzyValue(Options_.Tolerance);
            _Common.Build();
            if (_Common.IsDone() && _ShapeVolume(_Common.Shape()) > _VolumeTolerance)
            {
                _Ordinals.push_back(_Ordinal);
            }
        }
        return _Ordinals;
    }

    void _AppendMaterialSpans(
        IN const TopoDS_Shape& CandidateInsideBase_,
        IN const TopoDS_Shape& MainBase_,
        IN const TopoDS_Shape& FullRemovalShape_,
        IN const gp_Dir& ConstructionAxis_,
        IN const STubeCSGConversionOptions& Options_,
        IN OUT Tube::SFeatureRelations& Relations_)
    {
        std::uint32_t _SpanIndex = 0;
        for (TopExp_Explorer _Explorer(CandidateInsideBase_, TopAbs_SOLID);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Range = _ProjectAxialRange(_Explorer.Current(), ConstructionAxis_);
            if (!_Range)
            {
                continue;
            }
            const auto _Analysis = _AnalyzeRemovalExtent(
                _Explorer.Current(),
                MainBase_,
                ConstructionAxis_,
                Options_);
            const auto _EndpointTolerance = std::max(
                10.0 * Options_.Tolerance,
                (_Range->second - _Range->first) * 1.0e-7);
            const auto _MakeEndpoint = [
                &_Analysis,
                &_EndpointTolerance](IN double dStation_, IN const std::string& strID_) {
                Tube::SMaterialSpanEndpoint _Endpoint;
                _Endpoint.Station.Value = dStation_;
                _Endpoint.Station.Observability = Tube::EParameterObservability::Observed;
                const auto _OpeningIter = std::find_if(
                    _Analysis.BoundaryOpeningRanges.begin(),
                    _Analysis.BoundaryOpeningRanges.end(),
                    [dStation_, _EndpointTolerance](IN const auto& Opening_) {
                        return Opening_.first <= dStation_ + _EndpointTolerance
                            && Opening_.second >= dStation_ - _EndpointTolerance;
                    });
                if (_OpeningIter != _Analysis.BoundaryOpeningRanges.end())
                {
                    _Endpoint.Kind = Tube::EMaterialSpanEndpointKind::HostBoundary;
                    Tube::SMaterialBoundaryReference _Boundary;
                    _Boundary.ID = strID_;
                    _Boundary.HostNodeID = "base";
                    _Boundary.WitnessStation.Value = dStation_;
                    _Boundary.WitnessStation.Observability =
                        Tube::EParameterObservability::Observed;
                    _Endpoint.Boundary = std::move(_Boundary);
                }
                else
                {
                    _Endpoint.Kind = Tube::EMaterialSpanEndpointKind::FeatureTermination;
                }
                return _Endpoint;
            };

            Tube::SMaterialSpan _Span;
            _Span.ID = "material-span-" + std::to_string(++_SpanIndex);
            _Span.Start = _MakeEndpoint(
                _Range->first,
                _Span.ID + "/start-opening");
            _Span.End = _MakeEndpoint(
                _Range->second,
                _Span.ID + "/end-opening");
            _Span.RemovalComponentOrdinals = _FindCoveredRemovalComponents(
                _Explorer.Current(),
                FullRemovalShape_,
                Options_);
            Relations_.MaterialSpans.push_back(std::move(_Span));
        }
        Relations_.Metadata["materialSpanCount"] = std::to_string(
            Relations_.MaterialSpans.size());
    }

    double _WireLength(IN const TopoDS_Wire& Wire_)
    {
        GProp_GProps _Properties;
        BRepGProp::LinearProperties(Wire_, _Properties);
        return std::abs(_Properties.Mass());
    }

    bool _TrySurfaceNormal(
        IN BRepAdaptor_Surface& Surface_,
        IN double dU_,
        IN double dV_,
        OUT gp_Dir& Normal_)
    {
        try
        {
            gp_Pnt _Point;
            gp_Vec _DU;
            gp_Vec _DV;
            Surface_.D1(dU_, dV_, _Point, _DU, _DV);
            auto _Normal = _DU.Crossed(_DV);
            if (_Normal.SquareMagnitude() <= Precision::SquareConfusion())
            {
                return false;
            }
            Normal_ = gp_Dir(_Normal);
            return true;
        }
        catch (const Standard_Failure&)
        {
            return false;
        }
    }

    std::vector<STangentPlaneSample> _SampleTangentPlanes(
        IN const TopoDS_Face& Face_)
    {
        std::vector<STangentPlaneSample> _Samples;
        BRepAdaptor_Surface _Surface(Face_, true);
        const auto _UFirst = _Surface.FirstUParameter();
        const auto _ULast = _Surface.LastUParameter();
        const auto _VFirst = _Surface.FirstVParameter();
        const auto _VLast = _Surface.LastVParameter();
        if (!std::isfinite(_UFirst) || !std::isfinite(_ULast)
            || !std::isfinite(_VFirst) || !std::isfinite(_VLast))
        {
            return _Samples;
        }

        const auto _Area = _FaceArea(Face_);
        for (int _UIndex = 1; _UIndex <= 4; ++_UIndex)
        {
            for (int _VIndex = 1; _VIndex <= 4; ++_VIndex)
            {
                const auto _U = _UFirst + (_ULast - _UFirst)
                    * (static_cast<double>(_UIndex) / 5.0);
                const auto _V = _VFirst + (_VLast - _VFirst)
                    * (static_cast<double>(_VIndex) / 5.0);
                try
                {
                    gp_Pnt _Point;
                    gp_Vec _DU;
                    gp_Vec _DV;
                    _Surface.D1(_U, _V, _Point, _DU, _DV);
                    auto _Normal = _DU.Crossed(_DV);
                    if (_Normal.SquareMagnitude() <= Precision::SquareConfusion())
                    {
                        continue;
                    }
                    if (Face_.Orientation() == TopAbs_REVERSED)
                    {
                        _Normal.Reverse();
                    }
                    _Samples.push_back({ _Point, gp_Dir(_Normal), 0.0 });
                }
                catch (const Standard_Failure&)
                {
                    continue;
                }
            }
        }
        if (!_Samples.empty())
        {
            const auto _Weight = _Area / static_cast<double>(_Samples.size());
            for (auto& _Sample : _Samples)
            {
                _Sample.Weight = _Weight;
            }
        }
        return _Samples;
    }

    std::vector<STangentPlaneSample> _CollectGeneratedTangentPlanes(
        IN const TopoDS_Shape& Shape_,
        IN const TopoDS_Shape& Base_,
        IN const STubeCSGConversionOptions& Options_)
    {
        std::vector<STangentPlaneSample> _Unique;
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_FACE);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Face = TopoDS::Face(_Explorer.Current());
            if (_IsFaceOnBaseBoundary(_Face, Base_, Options_))
            {
                continue;
            }
            for (auto _Sample : _SampleTangentPlanes(_Face))
            {
                _Sample.Normal = _CanonicalDirection(_Sample.Normal);
                const auto _Offset = gp_Vec(
                    gp_Pnt(0.0, 0.0, 0.0),
                    _Sample.Point).Dot(gp_Vec(_Sample.Normal));
                const auto _Iter = std::find_if(
                    _Unique.begin(),
                    _Unique.end(),
                    [&_Sample, _Offset, &Options_](IN const auto& Existing_) {
                        const auto _ExistingOffset = gp_Vec(
                            gp_Pnt(0.0, 0.0, 0.0),
                            Existing_.Point).Dot(gp_Vec(Existing_.Normal));
                        return _AbsDot(Existing_.Normal, _Sample.Normal)
                                >= 1.0 - Options_.AngularToleranceRadians
                            && std::abs(_ExistingOffset - _Offset)
                                <= 5.0 * Options_.Tolerance;
                    });
                if (_Iter == _Unique.end())
                {
                    _Unique.push_back(std::move(_Sample));
                }
                else
                {
                    _Iter->Weight += _Sample.Weight;
                }
            }
        }
        return _Unique;
    }

    bool _IntersectThreeTangentPlanes(
        IN const STangentPlaneSample& First_,
        IN const STangentPlaneSample& Second_,
        IN const STangentPlaneSample& Third_,
        OUT gp_Pnt& Intersection_)
    {
        const auto _N1 = gp_Vec(First_.Normal);
        const auto _N2 = gp_Vec(Second_.Normal);
        const auto _N3 = gp_Vec(Third_.Normal);
        const auto _Determinant = _N1.Dot(_N2.Crossed(_N3));
        if (std::abs(_Determinant) <= 1.0e-8)
        {
            return false;
        }
        const auto _B1 = gp_Vec(
            gp_Pnt(0.0, 0.0, 0.0),
            First_.Point).Dot(_N1);
        const auto _B2 = gp_Vec(
            gp_Pnt(0.0, 0.0, 0.0),
            Second_.Point).Dot(_N2);
        const auto _B3 = gp_Vec(
            gp_Pnt(0.0, 0.0, 0.0),
            Third_.Point).Dot(_N3);
        const auto _Position = (
            _N2.Crossed(_N3) * _B1
            + _N3.Crossed(_N1) * _B2
            + _N1.Crossed(_N2) * _B3) / _Determinant;
        Intersection_ = gp_Pnt(_Position.X(), _Position.Y(), _Position.Z());
        return true;
    }

    std::optional<SCircularConeModel> _RecognizeCircularConeFromTangentPlanes(
        IN const TopoDS_Shape& RemovedSolid_,
        IN const TopoDS_Shape& MainBase_,
        IN const STubeCSGConversionOptions& Options_)
    {
        auto _Samples = _CollectGeneratedTangentPlanes(
            RemovedSolid_,
            MainBase_,
            Options_);
        if (_Samples.size() < 4)
        {
            return std::nullopt;
        }
        if (_Samples.size() > 48)
        {
            std::sort(
                _Samples.begin(),
                _Samples.end(),
                [](IN const auto& Left_, IN const auto& Right_) {
                    return Left_.Weight > Right_.Weight;
                });
            _Samples.resize(48);
        }

        Bnd_Box _Bounds;
        BRepBndLib::Add(RemovedSolid_, _Bounds);
        double _XMin = 0.0;
        double _YMin = 0.0;
        double _ZMin = 0.0;
        double _XMax = 0.0;
        double _YMax = 0.0;
        double _ZMax = 0.0;
        if (_Bounds.IsVoid())
        {
            return std::nullopt;
        }
        _Bounds.Get(_XMin, _YMin, _ZMin, _XMax, _YMax, _ZMax);
        const gp_Pnt _BoundsCenter(
            0.5 * (_XMin + _XMax),
            0.5 * (_YMin + _YMax),
            0.5 * (_ZMin + _ZMax));
        const auto _BoundsDiagonal = std::max(
            gp_Pnt(_XMin, _YMin, _ZMin).Distance(gp_Pnt(_XMax, _YMax, _ZMax)),
            Options_.Tolerance);
        const auto _DistanceTolerance = std::max(10.0 * Options_.Tolerance, 1.0e-6);
        const auto _TotalWeight = std::accumulate(
            _Samples.begin(),
            _Samples.end(),
            0.0,
            [](IN double Sum_, IN const auto& Sample_) {
                return Sum_ + Sample_.Weight;
            });

        gp_Pnt _BestApex;
        double _BestWeight = 0.0;
        std::vector<std::size_t> _BestInliers;
        for (std::size_t _First = 0; _First + 2 < _Samples.size(); ++_First)
        {
            for (std::size_t _Second = _First + 1;
                _Second + 1 < _Samples.size();
                ++_Second)
            {
                for (std::size_t _Third = _Second + 1;
                    _Third < _Samples.size();
                    ++_Third)
                {
                    gp_Pnt _Apex;
                    if (!_IntersectThreeTangentPlanes(
                        _Samples[_First],
                        _Samples[_Second],
                        _Samples[_Third],
                        _Apex)
                        || _Apex.Distance(_BoundsCenter) > 1.0e4 * _BoundsDiagonal)
                    {
                        continue;
                    }
                    double _Weight = 0.0;
                    std::vector<std::size_t> _Inliers;
                    for (std::size_t _Index = 0; _Index < _Samples.size(); ++_Index)
                    {
                        const auto& _Sample = _Samples[_Index];
                        const auto _Residual = std::abs(gp_Vec(
                            _Sample.Point,
                            _Apex).Dot(gp_Vec(_Sample.Normal)));
                        if (_Residual <= _DistanceTolerance)
                        {
                            _Weight += _Sample.Weight;
                            _Inliers.push_back(_Index);
                        }
                    }
                    if (_Weight > _BestWeight)
                    {
                        _BestWeight = _Weight;
                        _BestApex = _Apex;
                        _BestInliers = std::move(_Inliers);
                    }
                }
            }
        }
        if (_BestInliers.size() < 4
            || _TotalWeight <= 0.0
            || _BestWeight / _TotalWeight < 0.55)
        {
            return std::nullopt;
        }

        std::vector<gp_Vec> _Rays;
        _Rays.reserve(_BestInliers.size());
        for (const auto _Index : _BestInliers)
        {
            gp_Vec _Ray(_BestApex, _Samples[_Index].Point);
            if (_Ray.SquareMagnitude() <= Precision::SquareConfusion())
            {
                continue;
            }
            _Ray.Normalize();
            _Rays.push_back(std::move(_Ray));
        }
        if (_Rays.size() < 4)
        {
            return std::nullopt;
        }

        std::optional<gp_Dir> _BestAxis;
        double _BestAxisResidual = (std::numeric_limits<double>::max)();
        double _BestMeanCosine = 0.0;
        for (std::size_t _First = 0; _First + 2 < _Rays.size(); ++_First)
        {
            for (std::size_t _Second = _First + 1;
                _Second + 1 < _Rays.size();
                ++_Second)
            {
                for (std::size_t _Third = _Second + 1;
                    _Third < _Rays.size();
                    ++_Third)
                {
                    const auto _Difference1 = _Rays[_Second] - _Rays[_First];
                    const auto _Difference2 = _Rays[_Third] - _Rays[_First];
                    const auto _AxisVector = _Difference1.Crossed(_Difference2);
                    if (_AxisVector.SquareMagnitude() <= 1.0e-10)
                    {
                        continue;
                    }
                    auto _Axis = gp_Dir(_AxisVector);
                    auto _Mean = std::accumulate(
                        _Rays.begin(),
                        _Rays.end(),
                        0.0,
                        [&_Axis](IN double Sum_, IN const gp_Vec& Ray_) {
                            return Sum_ + Ray_.Dot(gp_Vec(_Axis));
                        }) / static_cast<double>(_Rays.size());
                    if (_Mean < 0.0)
                    {
                        _Axis.Reverse();
                        _Mean = -_Mean;
                    }
                    const auto _Residual = std::sqrt(std::accumulate(
                        _Rays.begin(),
                        _Rays.end(),
                        0.0,
                        [&_Axis, _Mean](IN double Sum_, IN const gp_Vec& Ray_) {
                            const auto _Delta = Ray_.Dot(gp_Vec(_Axis)) - _Mean;
                            return Sum_ + _Delta * _Delta;
                        }) / static_cast<double>(_Rays.size()));
                    if (_Residual < _BestAxisResidual)
                    {
                        _BestAxisResidual = _Residual;
                        _BestAxis = _Axis;
                        _BestMeanCosine = _Mean;
                    }
                }
            }
        }
        if (!_BestAxis
            || _BestAxisResidual > Options_.AngularToleranceRadians
            || _BestMeanCosine <= 1.0e-4
            || _BestMeanCosine >= 1.0 - 1.0e-8)
        {
            return std::nullopt;
        }

        auto _Reference = gp_Dir(1.0, 0.0, 0.0);
        if (_AbsDot(_Reference, *_BestAxis) > 0.9)
        {
            _Reference = gp_Dir(0.0, 1.0, 0.0);
        }
        auto _XVector = gp_Vec(_Reference)
            - gp_Vec(*_BestAxis) * _Reference.Dot(*_BestAxis);
        _XVector.Normalize();

        SCircularConeModel _Model;
        _Model.Apex = _BestApex;
        _Model.Axis = *_BestAxis;
        _Model.XDirection = gp_Dir(_XVector);
        _Model.SemiAngle = std::acos(_Clamp(_BestMeanCosine, -1.0, 1.0));
        _Model.EvidenceCoverage = _BestWeight / _TotalWeight;
        _Model.FromTangentPlaneConsensus = true;
        return _Model;
    }

    bool _IsTranslationCompatible(
        IN const TopoDS_Face& Face_,
        IN const gp_Dir& Axis_,
        IN double dAngularTolerance_)
    {
        BRepAdaptor_Surface _Surface(Face_, true);
        switch (_Surface.GetType())
        {
        case GeomAbs_Plane:
            return std::abs(_Surface.Plane().Axis().Direction().Dot(Axis_)) <= dAngularTolerance_;
        case GeomAbs_Cylinder:
            return _AbsDot(_Surface.Cylinder().Axis().Direction(), Axis_) >= 1.0 - dAngularTolerance_;
        case GeomAbs_SurfaceOfExtrusion:
            return _AbsDot(_Surface.Direction(), Axis_) >= 1.0 - dAngularTolerance_;
        default:
            break;
        }

        const auto _UFirst = _Surface.FirstUParameter();
        const auto _ULast = _Surface.LastUParameter();
        const auto _VFirst = _Surface.FirstVParameter();
        const auto _VLast = _Surface.LastVParameter();
        if (!std::isfinite(_UFirst) || !std::isfinite(_ULast)
            || !std::isfinite(_VFirst) || !std::isfinite(_VLast))
        {
            return false;
        }
        for (int _UIndex = 1; _UIndex <= 3; ++_UIndex)
        {
            for (int _VIndex = 1; _VIndex <= 3; ++_VIndex)
            {
                const auto _U = _UFirst + (_ULast - _UFirst) * (static_cast<double>(_UIndex) / 4.0);
                const auto _V = _VFirst + (_VLast - _VFirst) * (static_cast<double>(_VIndex) / 4.0);
                gp_Dir _Normal;
                if (!_TrySurfaceNormal(_Surface, _U, _V, _Normal)
                    || std::abs(_Normal.Dot(Axis_)) > dAngularTolerance_)
                {
                    return false;
                }
            }
        }
        return true;
    }

    void _AppendTranslationSurfaceEvidence(
        IN OUT Tube::SSolidNode& Node_,
        IN const SPartSurfaceEvidenceGraph& Graph_,
        IN const gp_Dir& Axis_,
        IN const STubeCSGConversionOptions& Options_)
    {
        if (!Node_.Relations)
        {
            return;
        }
        for (const auto& _FaceEvidence : Graph_.Faces)
        {
            if (_FaceEvidence.InheritedFromHost
                || !_IsTranslationCompatible(
                    _FaceEvidence.Face,
                    Axis_,
                    Options_.AngularToleranceRadians))
            {
                continue;
            }
            Tube::SSurfaceEvidenceReference _Reference;
            _Reference.SourceResourceID = Options_.SourceBRepResourceID;
            _Reference.SourceShapeID = _FaceEvidence.SourceShapeID;
            _Reference.Role = Tube::ESurfaceEvidenceRole::GeneratedLateral;
            _Reference.Confidence = 0.9;
            _Reference.Metadata["model"] = "tangent-plane-common-direction";
            Node_.Relations->SurfaceEvidence.push_back(std::move(_Reference));
        }
        Node_.Relations->Metadata["translationEvidenceFaceCount"] = std::to_string(
            Node_.Relations->SurfaceEvidence.size());
    }

    std::vector<SAxisCandidate> _RecognizeAxes(
        IN const TopoDS_Shape& Shape_,
        IN const STubeCSGConversionOptions& Options_,
        IN const TopoDS_Shape* pBase_ = nullptr)
    {
        std::vector<SAxisCandidate> _Candidates;
        struct SSampledFace final
        {
            TopoDS_Face Face;
            double Area = 0.0;
            std::vector<STangentPlaneSample> Samples;
        };
        std::vector<SSampledFace> _SampledFaces;
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_FACE); _Explorer.More(); _Explorer.Next())
        {
            const auto _Face = TopoDS::Face(_Explorer.Current());
            if (pBase_ && _IsFaceOnBaseBoundary(_Face, *pBase_, Options_))
            {
                continue;
            }
            const auto _Area = _FaceArea(_Face);
            BRepAdaptor_Surface _Surface(_Face, true);
            if (_Surface.GetType() == GeomAbs_Cylinder)
            {
                _AddAxisCandidate(_Candidates, _Surface.Cylinder().Axis().Direction(), _Area);
            }
            else if (_Surface.GetType() == GeomAbs_SurfaceOfExtrusion)
            {
                _AddAxisCandidate(_Candidates, _Surface.Direction(), _Area);
            }
            auto _Samples = _SampleTangentPlanes(_Face);
            for (std::size_t _Left = 0; _Left < _Samples.size(); ++_Left)
            {
                for (std::size_t _Right = _Left + 1;
                    _Right < _Samples.size();
                    ++_Right)
                {
                    const auto _Direction = gp_Vec(_Samples[_Left].Normal).Crossed(
                        gp_Vec(_Samples[_Right].Normal));
                    if (_Direction.SquareMagnitude() <= 1.0e-10)
                    {
                        continue;
                    }
                    _AddAxisCandidate(
                        _Candidates,
                        gp_Dir(_Direction),
                        std::min(_Samples[_Left].Weight, _Samples[_Right].Weight),
                        true);
                }
            }
            _SampledFaces.push_back({ _Face, _Area, std::move(_Samples) });
        }

        /* 平面侧壁本身不能给出拉伸方向；相邻碎面法向的交线可以。 */
        for (std::size_t _Left = 0; _Left < _SampledFaces.size(); ++_Left)
        {
            if (_SampledFaces[_Left].Samples.empty())
            {
                continue;
            }
            for (std::size_t _Right = _Left + 1;
                _Right < _SampledFaces.size();
                ++_Right)
            {
                if (_SampledFaces[_Right].Samples.empty())
                {
                    continue;
                }
                const auto _Direction = gp_Vec(
                    _SampledFaces[_Left].Samples.front().Normal).Crossed(gp_Vec(
                        _SampledFaces[_Right].Samples.front().Normal));
                if (_Direction.SquareMagnitude() <= 1.0e-10)
                {
                    continue;
                }
                _AddAxisCandidate(
                    _Candidates,
                    gp_Dir(_Direction),
                    std::sqrt(
                        _SampledFaces[_Left].Area
                        * _SampledFaces[_Right].Area),
                    true);
            }
        }

        for (TopExp_Explorer _Explorer(Shape_, TopAbs_EDGE); _Explorer.More(); _Explorer.Next())
        {
            const auto _Edge = TopoDS::Edge(_Explorer.Current());
            BRepAdaptor_Curve _Curve(_Edge);
            if (_Curve.GetType() != GeomAbs_Line)
            {
                continue;
            }
            gp_Pnt _First;
            gp_Pnt _Last;
            _Curve.D0(_Curve.FirstParameter(), _First);
            _Curve.D0(_Curve.LastParameter(), _Last);
            const auto _Length = _First.Distance(_Last);
            if (_Length > Options_.Tolerance)
            {
                _AddAxisCandidate(_Candidates, _Curve.Line().Direction(), _Length);
            }
        }

        if (_Candidates.empty())
        {
            return {};
        }

        std::sort(
            _Candidates.begin(),
            _Candidates.end(),
            [](IN const auto& Left_, IN const auto& Right_) {
                return Left_.SourceWeight > Right_.SourceWeight;
            });
        if (_Candidates.size() > 64)
        {
            _Candidates.resize(64);
        }

        double _TotalArea = 0.0;
        for (const auto& _SampledFace : _SampledFaces)
        {
            _TotalArea += _SampledFace.Area;
        }
        for (auto& _Candidate : _Candidates)
        {
            _Candidate.TotalArea = _TotalArea;
            for (const auto& _SampledFace : _SampledFaces)
            {
                if (_IsTranslationCompatible(
                    _SampledFace.Face,
                    _Candidate.Direction,
                    Options_.AngularToleranceRadians))
                {
                    _Candidate.CompatibleArea += _SampledFace.Area;
                }
            }
        }

        const auto _BestCompatibleArea = _Candidates.empty()
            ? 0.0
            : std::max_element(
                _Candidates.begin(),
                _Candidates.end(),
                [](IN const auto& Left_, IN const auto& Right_) {
                    return Left_.CompatibleArea < Right_.CompatibleArea;
                })->CompatibleArea;
        const auto _MinimumConsensusArea = std::max(
            Options_.Tolerance * Options_.Tolerance,
            0.15 * _BestCompatibleArea);
        std::erase_if(
            _Candidates,
            [_MinimumConsensusArea](IN const SAxisCandidate& Candidate_) {
                return Candidate_.CompatibleArea < _MinimumConsensusArea;
            });
        std::sort(
            _Candidates.begin(),
            _Candidates.end(),
            [](IN const SAxisCandidate& Left_, IN const SAxisCandidate& Right_) {
                if (std::abs(Left_.CompatibleArea - Right_.CompatibleArea) > 1.0e-9)
                {
                    return Left_.CompatibleArea > Right_.CompatibleArea;
                }
                if (std::abs(Left_.SourceWeight - Right_.SourceWeight) > 1.0e-9)
                {
                    return Left_.SourceWeight > Right_.SourceWeight;
                }
                if (std::abs(Left_.Direction.X() - Right_.Direction.X()) > 1.0e-12)
                {
                    return Left_.Direction.X() > Right_.Direction.X();
                }
                if (std::abs(Left_.Direction.Y() - Right_.Direction.Y()) > 1.0e-12)
                {
                    return Left_.Direction.Y() > Right_.Direction.Y();
                }
                return Left_.Direction.Z() > Right_.Direction.Z();
            });
        /* 后续每个方向都要做截面重建和布尔验证；只保留最强的有限个共识峰。 */
        if (_Candidates.size() > 8)
        {
            _Candidates.resize(8);
        }
        return _Candidates;
    }

    std::optional<SAxisCandidate> _RecognizeAxis(
        IN const TopoDS_Shape& Shape_,
        IN const STubeCSGConversionOptions& Options_)
    {
        const auto _Candidates = _RecognizeAxes(Shape_, Options_);
        return _Candidates.empty()
            ? std::nullopt
            : std::optional<SAxisCandidate>(_Candidates.front());
    }

    std::optional<std::pair<double, double>> _ProjectAxialRange(
        IN const TopoDS_Shape& Shape_,
        IN const gp_Dir& Axis_)
    {
        auto _First = (std::numeric_limits<double>::max)();
        auto _Last = (std::numeric_limits<double>::lowest)();
        bool _Found = false;
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_VERTEX); _Explorer.More(); _Explorer.Next())
        {
            const auto _Point = BRep_Tool::Pnt(TopoDS::Vertex(_Explorer.Current()));
            const auto _Station = gp_Vec(gp_Pnt(0.0, 0.0, 0.0), _Point).Dot(gp_Vec(Axis_));
            _First = std::min(_First, _Station);
            _Last = std::max(_Last, _Station);
            _Found = true;
        }
        /* 面可能正好垂直于投影轴，此时零宽区间仍是有效的边界穿越位置。 */
        if (!_Found || _Last < _First)
        {
            return std::nullopt;
        }
        return std::make_pair(_First, _Last);
    }

    SSectionFrame _MakeSectionFrame(
        IN const gp_Dir& Axis_,
        IN double dFirst_,
        IN double dLast_)
    {
        auto _Reference = gp_Dir(1.0, 0.0, 0.0);
        if (_AbsDot(_Reference, Axis_) > 0.9)
        {
            _Reference = gp_Dir(0.0, 1.0, 0.0);
        }
        auto _XVector = gp_Vec(_Reference) - gp_Vec(Axis_) * _Reference.Dot(Axis_);
        _XVector.Normalize();
        const auto _X = gp_Dir(_XVector);
        const auto _Y = gp_Dir(gp_Vec(Axis_).Crossed(gp_Vec(_X)));
        const auto _Origin = gp_Pnt(
            Axis_.X() * dFirst_,
            Axis_.Y() * dFirst_,
            Axis_.Z() * dFirst_);
        return { _Origin, _X, _Y, Axis_, dFirst_, dLast_ };
    }

    Point2 _ToPoint2(IN const gp_Pnt& Point_, IN const SSectionFrame& Frame_)
    {
        const gp_Vec _Offset(Frame_.Origin, Point_);
        return {
            _Offset.Dot(gp_Vec(Frame_.XDirection)),
            _Offset.Dot(gp_Vec(Frame_.YDirection))
        };
    }

    Direction2 _ToDirection2(IN const gp_Dir& Direction_, IN const SSectionFrame& Frame_)
    {
        return {
            Direction_.Dot(Frame_.XDirection),
            Direction_.Dot(Frame_.YDirection)
        };
    }

    double _SignedArea(IN const std::vector<Point2>& Points_)
    {
        if (Points_.size() < 3)
        {
            return 0.0;
        }
        double _TwiceArea = 0.0;
        for (std::size_t _Index = 0; _Index < Points_.size(); ++_Index)
        {
            const auto& _Current = Points_[_Index];
            const auto& _Next = Points_[(_Index + 1) % Points_.size()];
            _TwiceArea += _Current.X * _Next.Y - _Next.X * _Current.Y;
        }
        return _TwiceArea * 0.5;
    }

    std::vector<Point2> _SampleWire(
        IN const TopoDS_Wire& Wire_,
        IN const SSectionFrame& Frame_)
    {
        std::vector<Point2> _Points;
        for (BRepTools_WireExplorer _Explorer(Wire_); _Explorer.More(); _Explorer.Next())
        {
            const auto _Edge = _Explorer.Current();
            BRepAdaptor_Curve _Curve(_Edge);
            const auto _First = _Curve.FirstParameter();
            const auto _Last = _Curve.LastParameter();
            constexpr int kSamplesPerEdge = 16;
            std::vector<Point2> _EdgePoints;
            _EdgePoints.reserve(kSamplesPerEdge + 1);
            for (int _Index = 0; _Index <= kSamplesPerEdge; ++_Index)
            {
                const auto _Parameter = _First + (_Last - _First) * (static_cast<double>(_Index) / kSamplesPerEdge);
                gp_Pnt _Point;
                _Curve.D0(_Parameter, _Point);
                _EdgePoints.push_back(_ToPoint2(_Point, Frame_));
            }
            const auto _StartVertex = BRep_Tool::Pnt(_Explorer.CurrentVertex());
            const auto _Start2 = _ToPoint2(_StartVertex, Frame_);
            const auto _DistanceToFirst = std::hypot(_EdgePoints.front().X - _Start2.X, _EdgePoints.front().Y - _Start2.Y);
            const auto _DistanceToLast = std::hypot(_EdgePoints.back().X - _Start2.X, _EdgePoints.back().Y - _Start2.Y);
            if (_DistanceToLast < _DistanceToFirst)
            {
                std::reverse(_EdgePoints.begin(), _EdgePoints.end());
            }
            if (!_Points.empty())
            {
                _EdgePoints.erase(_EdgePoints.begin());
            }
            _Points.insert(_Points.end(), _EdgePoints.begin(), _EdgePoints.end());
        }
        if (_Points.size() > 1
            && std::hypot(_Points.front().X - _Points.back().X, _Points.front().Y - _Points.back().Y) <= 1.0e-7)
        {
            _Points.pop_back();
        }
        return _Points;
    }

    TopoDS_Wire _OrientedWire(IN const SSliceLoop& Loop_, IN bool bCounterClockwise_)
    {
        auto _Wire = Loop_.Wire;
        if ((Loop_.SignedArea > 0.0) != bCounterClockwise_)
        {
            _Wire.Reverse();
        }
        return _Wire;
    }

    std::optional<TopoDS_Face> _MakeSliceFace(IN const SSliceCandidate& Slice_)
    {
        if (Slice_.Loops.empty())
        {
            return std::nullopt;
        }
        const auto _Outer = std::max_element(
            Slice_.Loops.begin(),
            Slice_.Loops.end(),
            [](IN const SSliceLoop& Left_, IN const SSliceLoop& Right_) {
                return std::abs(Left_.SignedArea) < std::abs(Right_.SignedArea);
            });
        auto _Maker = BRepBuilderAPI_MakeFace(Slice_.Plane, _OrientedWire(*_Outer, true), true);
        if (!_Maker.IsDone())
        {
            return std::nullopt;
        }
        for (auto _Iter = Slice_.Loops.begin(); _Iter != Slice_.Loops.end(); ++_Iter)
        {
            if (_Iter == _Outer)
            {
                continue;
            }
            _Maker.Add(_OrientedWire(*_Iter, false));
        }
        if (!_Maker.IsDone())
        {
            return std::nullopt;
        }
        // A section loop can consist of a single periodic edge (a full circle is
        // the common tube case). Reversing only the wire orientation is not
        // sufficient for every such wire: MakeFace may still classify an inner
        // loop as another positive island. Let OCC resolve wire nesting on the
        // finished face so circular and mixed-curve cavities remain voids.
        ShapeFix_Face _FaceFixer(_Maker.Face());
        _FaceFixer.FixOrientation();
        return _FaceFixer.Face();
    }

    std::optional<SSliceCandidate> _MakeSliceCandidate(
        IN const TopoDS_Shape& Shape_,
        IN const SSectionFrame& Frame_,
        IN double dStation_,
        IN double dTolerance_)
    {
        const auto _PlaneOrigin = gp_Pnt(
            Frame_.ZDirection.X() * dStation_,
            Frame_.ZDirection.Y() * dStation_,
            Frame_.ZDirection.Z() * dStation_);
        const auto _Plane = gp_Pln(gp_Ax3(_PlaneOrigin, Frame_.ZDirection, Frame_.XDirection));
        BRepAlgoAPI_Section _Section(Shape_, _Plane, false);
        _Section.Approximation(false);
        _Section.Build();
        if (!_Section.IsDone() || _Section.Shape().IsNull())
        {
            return std::nullopt;
        }

        occ::handle<NCollection_HSequence<TopoDS_Shape>> _Edges = new NCollection_HSequence<TopoDS_Shape>();
        for (TopExp_Explorer _Explorer(_Section.Shape(), TopAbs_EDGE); _Explorer.More(); _Explorer.Next())
        {
            _Edges->Append(_Explorer.Current());
        }
        if (_Edges->IsEmpty())
        {
            return std::nullopt;
        }
        const auto _Wires = ShapeAnalysis_FreeBounds::ConnectEdgesToWires(_Edges, dTolerance_, false);
        if (_Wires.IsNull() || _Wires->IsEmpty())
        {
            return std::nullopt;
        }

        SSliceCandidate _Candidate;
        _Candidate.Station = dStation_;
        _Candidate.Plane = _Plane;
        for (int _Index = 1; _Index <= _Wires->Length(); ++_Index)
        {
            const auto& _Shape = _Wires->Value(_Index);
            if (_Shape.ShapeType() != TopAbs_WIRE)
            {
                continue;
            }
            SSliceLoop _Loop;
            _Loop.Wire = TopoDS::Wire(_Shape);
            _Loop.Points = _SampleWire(_Loop.Wire, Frame_);
            _Loop.SignedArea = _SignedArea(_Loop.Points);
            if (_Loop.Points.size() >= 3 && std::abs(_Loop.SignedArea) > dTolerance_ * dTolerance_)
            {
                _Candidate.Loops.push_back(std::move(_Loop));
            }
        }
        if (_Candidate.Loops.empty())
        {
            return std::nullopt;
        }
        const auto _Outer = std::max_element(
            _Candidate.Loops.begin(),
            _Candidate.Loops.end(),
            [](IN const SSliceLoop& Left_, IN const SSliceLoop& Right_) {
                return std::abs(Left_.SignedArea) < std::abs(Right_.SignedArea);
            });
        _Candidate.MaterialArea = std::abs(_Outer->SignedArea);
        for (auto _Iter = _Candidate.Loops.begin(); _Iter != _Candidate.Loops.end(); ++_Iter)
        {
            if (_Iter != _Outer)
            {
                _Candidate.MaterialArea -= std::abs(_Iter->SignedArea);
            }
        }
        if (_Candidate.MaterialArea <= dTolerance_ * dTolerance_ || !_MakeSliceFace(_Candidate))
        {
            return std::nullopt;
        }
        return _Candidate;
    }

    std::optional<SSliceCandidate> _RecognizeSection(
        IN const TopoDS_Shape& Shape_,
        IN const SSectionFrame& Frame_,
        IN const STubeCSGConversionOptions& Options_,
        IN bool bReturnCentralSectionWhenValid_ = false)
    {
        if (bReturnCentralSectionWhenValid_)
        {
            const auto _CentralStation = 0.5 * (Frame_.First + Frame_.Last);
            if (auto _Central = _MakeSliceCandidate(
                Shape_,
                Frame_,
                _CentralStation,
                Options_.Tolerance))
            {
                return _Central;
            }
        }
        const auto _Count = std::max<std::size_t>(5, Options_.SliceSampleCount);
        std::optional<SSliceCandidate> _Best;
        for (std::size_t _Index = 0; _Index < _Count; ++_Index)
        {
            const auto _Fraction = 0.03 + 0.94 * (static_cast<double>(_Index) / static_cast<double>(_Count - 1));
            const auto _Station = Frame_.First + (Frame_.Last - Frame_.First) * _Fraction;
            auto _Candidate = _MakeSliceCandidate(Shape_, Frame_, _Station, Options_.Tolerance);
            if (_Candidate && (!_Best || _Candidate->MaterialArea > _Best->MaterialArea))
            {
                _Best = std::move(_Candidate);
            }
        }
        return _Best;
    }

    CurveSegment2 _MakeCurveSegment2(
        IN const TopoDS_Edge& Edge_,
        IN const SSectionFrame& Frame_,
        IN const Point2& OrientedStart_)
    {
        BRepAdaptor_Curve _Curve(Edge_);
        const auto _First = _Curve.FirstParameter();
        const auto _Last = _Curve.LastParameter();
        gp_Pnt _FirstPoint3;
        gp_Pnt _LastPoint3;
        _Curve.D0(_First, _FirstPoint3);
        _Curve.D0(_Last, _LastPoint3);
        const auto _FirstPoint = _ToPoint2(_FirstPoint3, Frame_);
        const auto _LastPoint = _ToPoint2(_LastPoint3, Frame_);
        const auto _FirstDistance = std::hypot(_FirstPoint.X - OrientedStart_.X, _FirstPoint.Y - OrientedStart_.Y);
        const auto _LastDistance = std::hypot(_LastPoint.X - OrientedStart_.X, _LastPoint.Y - OrientedStart_.Y);
        const bool _Reversed = _LastDistance < _FirstDistance;

        CurveSegment2 _Segment;
        _Segment.Range = { _First, _Last, _Reversed };
        switch (_Curve.GetType())
        {
        case GeomAbs_Line:
            _Segment.Curve = Segment2{
                _Reversed ? _LastPoint : _FirstPoint,
                _Reversed ? _FirstPoint : _LastPoint
            };
            _Segment.Range = { 0.0, 1.0, false };
            return _Segment;
        case GeomAbs_Circle:
        {
            const auto _Circle3 = _Curve.Circle();
            Circle2 _Circle2;
            _Circle2.Placement.Location = _ToPoint2(_Circle3.Location(), Frame_);
            _Circle2.Placement.XDirection = _ToDirection2(_Circle3.XAxis().Direction(), Frame_);
            _Circle2.Placement.YDirection = _ToDirection2(_Circle3.YAxis().Direction(), Frame_);
            _Circle2.Radius = _Circle3.Radius();
            _Segment.Curve = _Circle2;
            return _Segment;
        }
        case GeomAbs_Ellipse:
        {
            const auto _Ellipse3 = _Curve.Ellipse();
            Ellipse2 _Ellipse2;
            _Ellipse2.Placement.Location = _ToPoint2(_Ellipse3.Location(), Frame_);
            _Ellipse2.Placement.XDirection = _ToDirection2(_Ellipse3.XAxis().Direction(), Frame_);
            _Ellipse2.Placement.YDirection = _ToDirection2(_Ellipse3.YAxis().Direction(), Frame_);
            _Ellipse2.MajorRadius = _Ellipse3.MajorRadius();
            _Ellipse2.MinorRadius = _Ellipse3.MinorRadius();
            _Segment.Curve = _Ellipse2;
            return _Segment;
        }
        case GeomAbs_BSplineCurve:
        {
            const auto _Spline3 = _Curve.BSpline();
            if (!_Spline3.IsNull())
            {
                if (_Spline3->IsRational())
                {
                    NURBS2 _Spline2;
                    _Spline2.Degree = _Spline3->Degree();
                    _Spline2.Periodic = _Spline3->IsPeriodic();
                    _Spline2.Poles.reserve(_Spline3->NbPoles());
                    _Spline2.Weights.reserve(_Spline3->NbPoles());
                    for (int _Index = 1; _Index <= _Spline3->NbPoles(); ++_Index)
                    {
                        _Spline2.Poles.push_back(_ToPoint2(_Spline3->Pole(_Index), Frame_));
                        _Spline2.Weights.push_back(_Spline3->Weight(_Index));
                    }
                    _Spline2.Knots.reserve(_Spline3->NbKnots());
                    _Spline2.Multiplicities.reserve(_Spline3->NbKnots());
                    for (int _Index = 1; _Index <= _Spline3->NbKnots(); ++_Index)
                    {
                        _Spline2.Knots.push_back(_Spline3->Knot(_Index));
                        _Spline2.Multiplicities.push_back(_Spline3->Multiplicity(_Index));
                    }
                    _Segment.Curve = std::move(_Spline2);
                }
                else
                {
                    BSpline2 _Spline2;
                    _Spline2.Degree = _Spline3->Degree();
                    _Spline2.Periodic = _Spline3->IsPeriodic();
                    _Spline2.Poles.reserve(_Spline3->NbPoles());
                    for (int _Index = 1; _Index <= _Spline3->NbPoles(); ++_Index)
                    {
                        _Spline2.Poles.push_back(_ToPoint2(_Spline3->Pole(_Index), Frame_));
                    }
                    _Spline2.Knots.reserve(_Spline3->NbKnots());
                    _Spline2.Multiplicities.reserve(_Spline3->NbKnots());
                    for (int _Index = 1; _Index <= _Spline3->NbKnots(); ++_Index)
                    {
                        _Spline2.Knots.push_back(_Spline3->Knot(_Index));
                        _Spline2.Multiplicities.push_back(_Spline3->Multiplicity(_Index));
                    }
                    _Segment.Curve = std::move(_Spline2);
                }
                return _Segment;
            }
            break;
        }
        default:
            break;
        }

        Polyline2 _Polyline;
        constexpr int kFallbackSamples = 64;
        _Polyline.Points.reserve(kFallbackSamples + 1);
        for (int _Index = 0; _Index <= kFallbackSamples; ++_Index)
        {
            const auto _Parameter = _First + (_Last - _First) * (static_cast<double>(_Index) / kFallbackSamples);
            gp_Pnt _Point;
            _Curve.D0(_Parameter, _Point);
            _Polyline.Points.push_back(_ToPoint2(_Point, Frame_));
        }
        if (_Reversed)
        {
            std::reverse(_Polyline.Points.begin(), _Polyline.Points.end());
        }
        _Segment.Curve = std::move(_Polyline);
        _Segment.Range = { 0.0, 1.0, false };
        return _Segment;
    }

    Tube::SRegionLoop2 _MakeNeutralLoop(
        IN const SSliceLoop& Loop_,
        IN const SSectionFrame& Frame_,
        IN const std::string& strLoopID_,
        IN bool bCounterClockwise_)
    {
        Tube::SRegionLoop2 _Result;
        _Result.ID = strLoopID_;
        const auto _Wire = _OrientedWire(Loop_, bCounterClockwise_);
        std::vector<TopoDS_Edge> _Edges;
        std::vector<Point2> _Starts;
        for (BRepTools_WireExplorer _Explorer(_Wire); _Explorer.More(); _Explorer.Next())
        {
            _Edges.push_back(_Explorer.Current());
            _Starts.push_back(_ToPoint2(BRep_Tool::Pnt(_Explorer.CurrentVertex()), Frame_));
        }
        if (!_Starts.empty())
        {
            const auto _Seam = std::min_element(
                _Starts.begin(),
                _Starts.end(),
                [](IN const Point2& Left_, IN const Point2& Right_) {
                    return Left_.X < Right_.X
                        || (Left_.X == Right_.X && Left_.Y < Right_.Y);
                });
            const auto _Offset = static_cast<std::size_t>(std::distance(_Starts.begin(), _Seam));
            std::rotate(_Edges.begin(), _Edges.begin() + _Offset, _Edges.end());
            std::rotate(_Starts.begin(), _Starts.begin() + _Offset, _Starts.end());
        }
        _Result.Curves.reserve(_Edges.size());
        for (std::size_t _Index = 0; _Index < _Edges.size(); ++_Index)
        {
            auto _Segment = _MakeCurveSegment2(_Edges[_Index], Frame_, _Starts[_Index]);
            Tube::SRegionCurve2 _Curve;
            _Curve.ID = strLoopID_ + "/curve-" + std::to_string(_Index + 1);
            _Curve.Segment = std::move(_Segment);
            _Result.Curves.push_back(std::move(_Curve));
        }
        return _Result;
    }

    std::pair<double, double> _LoopPositionKey(IN const SSliceLoop& Loop_)
    {
        auto _MinX = (std::numeric_limits<double>::max)();
        auto _MinY = (std::numeric_limits<double>::max)();
        for (const auto& _Point : Loop_.Points)
        {
            if (_Point.X < _MinX || (_Point.X == _MinX && _Point.Y < _MinY))
            {
                _MinX = _Point.X;
                _MinY = _Point.Y;
            }
        }
        return { _MinX, _MinY };
    }

    std::vector<const SSliceLoop*> _OrderedSliceLoops(IN const SSliceCandidate& Slice_)
    {
        const auto _Outer = std::max_element(
            Slice_.Loops.begin(),
            Slice_.Loops.end(),
            [](IN const SSliceLoop& Left_, IN const SSliceLoop& Right_) {
                return std::abs(Left_.SignedArea) < std::abs(Right_.SignedArea);
            });
        std::vector<const SSliceLoop*> _Ordered{ &*_Outer };
        for (auto _Iter = Slice_.Loops.begin(); _Iter != Slice_.Loops.end(); ++_Iter)
        {
            if (_Iter != _Outer)
            {
                _Ordered.push_back(&*_Iter);
            }
        }
        std::sort(
            _Ordered.begin() + 1,
            _Ordered.end(),
            [](IN const SSliceLoop* pLeft_, IN const SSliceLoop* pRight_) {
                return _LoopPositionKey(*pLeft_) < _LoopPositionKey(*pRight_);
            });
        return _Ordered;
    }

    Tube::CPlanarRegion2 _MakeNeutralRegion(
        IN const SSliceCandidate& Slice_,
        IN const SSectionFrame& Frame_)
    {
        Tube::CPlanarRegion2 _Region;
        _Region.FillRule = Tube::EPlanarFillRule::NonZero;
        const auto _Ordered = _OrderedSliceLoops(Slice_);
        for (std::size_t _Index = 0; _Index < _Ordered.size(); ++_Index)
        {
            const auto _LoopID = _Index == 0
                ? std::string("outer")
                : "inner-" + std::to_string(_Index);
            _Region.Boundaries.push_back(_MakeNeutralLoop(
                *_Ordered[_Index],
                Frame_,
                _LoopID,
                _Index == 0));
        }
        return _Region;
    }

    Tube::SScalarParameter _ObservedSectionParameter(IN double dValue_)
    {
        Tube::SScalarParameter _Parameter;
        _Parameter.Value = dValue_;
        _Parameter.Observability = Tube::EParameterObservability::Observed;
        return _Parameter;
    }

    std::vector<Point2> _SectionLoopVertices(
        IN const Tube::SRegionLoop2& Loop_)
    {
        std::vector<Point2> _Vertices;
        for (const auto& _Curve : Loop_.Curves)
        {
            const auto* _pSegment = std::get_if<Segment2>(&_Curve.Segment.Curve);
            if (!_pSegment)
            {
                return {};
            }
            if (_Vertices.empty()
                || std::hypot(
                    _Vertices.back().X - _pSegment->Start.X,
                    _Vertices.back().Y - _pSegment->Start.Y) > 1.0e-9)
            {
                _Vertices.push_back(_pSegment->Start);
            }
            _Vertices.push_back(_pSegment->End);
        }
        if (_Vertices.size() > 1
            && std::hypot(
                _Vertices.front().X - _Vertices.back().X,
                _Vertices.front().Y - _Vertices.back().Y) <= 1.0e-9)
        {
            _Vertices.pop_back();
        }

        bool _Changed = true;
        while (_Changed && _Vertices.size() > 4)
        {
            _Changed = false;
            for (std::size_t _Index = 0; _Index < _Vertices.size(); ++_Index)
            {
                const auto& _Previous = _Vertices[(_Index + _Vertices.size() - 1) % _Vertices.size()];
                const auto& _Current = _Vertices[_Index];
                const auto& _Next = _Vertices[(_Index + 1) % _Vertices.size()];
                const auto _AX = _Current.X - _Previous.X;
                const auto _AY = _Current.Y - _Previous.Y;
                const auto _BX = _Next.X - _Current.X;
                const auto _BY = _Next.Y - _Current.Y;
                const auto _Scale = std::max(
                    std::hypot(_AX, _AY) * std::hypot(_BX, _BY),
                    1.0e-18);
                if (std::abs(_AX * _BY - _AY * _BX) <= 1.0e-8 * _Scale
                    && _AX * _BX + _AY * _BY >= 0.0)
                {
                    _Vertices.erase(_Vertices.begin() + _Index);
                    _Changed = true;
                    break;
                }
            }
        }
        return _Vertices;
    }

    Tube::SSectionPrimitive _MakeSectionPrimitive(
        IN const Tube::SRegionLoop2& Loop_,
        IN const std::string& strOwnerNodeID_,
        IN bool bOuter_,
        IN std::size_t nCavityOrdinal_)
    {
        Tube::SSectionPrimitive _Primitive;
        _Primitive.ID = strOwnerNodeID_ + "/section/"
            + (bOuter_ ? std::string("outer") : "cavity-" + std::to_string(nCavityOrdinal_));
        _Primitive.Label = bOuter_
            ? "主体外轮廓"
            : "腔体 " + std::to_string(nCavityOrdinal_);
        _Primitive.LoopID = Loop_.ID;
        _Primitive.Role = bOuter_
            ? Tube::ESectionPrimitiveRole::OuterBoundary
            : Tube::ESectionPrimitiveRole::Cavity;
        _Primitive.Metadata["source"] = "recognized-section-loop";
        _Primitive.Metadata["ownerNodeId"] = strOwnerNodeID_;

        if (Loop_.Curves.size() == 1)
        {
            if (const auto* _pCircle = std::get_if<Circle2>(
                &Loop_.Curves.front().Segment.Curve))
            {
                _Primitive.Kind = Tube::ESectionPrimitiveKind::Circle;
                _Primitive.Center = _pCircle->Placement.Location;
                _Primitive.RotationRadians = std::atan2(
                    _pCircle->Placement.XDirection.Y,
                    _pCircle->Placement.XDirection.X);
                _Primitive.Radius = _ObservedSectionParameter(_pCircle->Radius);
                return _Primitive;
            }
        }

        const auto _Vertices = _SectionLoopVertices(Loop_);
        if (_Vertices.size() == 4)
        {
            std::array<double, 4> _Lengths{};
            std::array<Point2, 4> _Directions{};
            bool _Valid = true;
            for (std::size_t _Index = 0; _Index < 4; ++_Index)
            {
                const auto& _Start = _Vertices[_Index];
                const auto& _End = _Vertices[(_Index + 1) % 4];
                _Lengths[_Index] = std::hypot(
                    _End.X - _Start.X,
                    _End.Y - _Start.Y);
                if (_Lengths[_Index] <= 1.0e-12)
                {
                    _Valid = false;
                    break;
                }
                _Directions[_Index] = {
                    (_End.X - _Start.X) / _Lengths[_Index],
                    (_End.Y - _Start.Y) / _Lengths[_Index]
                };
            }
            for (std::size_t _Index = 0; _Valid && _Index < 4; ++_Index)
            {
                const auto& _First = _Directions[_Index];
                const auto& _Second = _Directions[(_Index + 1) % 4];
                _Valid = std::abs(_First.X * _Second.X + _First.Y * _Second.Y) <= 1.0e-7;
            }
            _Valid = _Valid
                && std::abs(_Lengths[0] - _Lengths[2]) <= 1.0e-7 * std::max(_Lengths[0], _Lengths[2])
                && std::abs(_Lengths[1] - _Lengths[3]) <= 1.0e-7 * std::max(_Lengths[1], _Lengths[3]);
            if (_Valid)
            {
                _Primitive.Kind = Tube::ESectionPrimitiveKind::Rectangle;
                for (const auto& _Point : _Vertices)
                {
                    _Primitive.Center.X += 0.25 * _Point.X;
                    _Primitive.Center.Y += 0.25 * _Point.Y;
                }
                _Primitive.RotationRadians = std::atan2(
                    _Directions[0].Y, _Directions[0].X);
                _Primitive.Width = _ObservedSectionParameter(_Lengths[0]);
                _Primitive.Height = _ObservedSectionParameter(_Lengths[1]);
                return _Primitive;
            }
        }

        _Primitive.Kind = Tube::ESectionPrimitiveKind::CurveLoop;
        if (!_Vertices.empty())
        {
            for (const auto& _Point : _Vertices)
            {
                _Primitive.Center.X += _Point.X / static_cast<double>(_Vertices.size());
                _Primitive.Center.Y += _Point.Y / static_cast<double>(_Vertices.size());
            }
        }
        _Primitive.Observability = Tube::EParameterObservability::Reconstructed;
        _Primitive.Metadata["curveCount"] = std::to_string(Loop_.Curves.size());
        return _Primitive;
    }

    std::vector<Tube::SSectionPrimitive> _MakeSectionPrimitives(
        IN const Tube::CPlanarRegion2& Section_,
        IN const std::string& strOwnerNodeID_)
    {
        std::vector<Tube::SSectionPrimitive> _Primitives;
        _Primitives.reserve(Section_.Boundaries.size());
        for (std::size_t _Index = 0; _Index < Section_.Boundaries.size(); ++_Index)
        {
            _Primitives.push_back(_MakeSectionPrimitive(
                Section_.Boundaries[_Index],
                strOwnerNodeID_,
                _Index == 0,
                _Index));
        }
        return _Primitives;
    }

    std::optional<TopoDS_Shape> _MakeBaseShape(
        IN const SSliceCandidate& Slice_,
        IN const SSectionFrame& Frame_)
    {
        auto _Face = _MakeSliceFace(Slice_);
        if (!_Face)
        {
            return std::nullopt;
        }
        gp_Trsf _Translation;
        _Translation.SetTranslation(gp_Vec(Frame_.ZDirection) * (Frame_.First - Slice_.Station));
        const auto _StartFace = BRepBuilderAPI_Transform(*_Face, _Translation, true).Shape();
        const auto _Length = Frame_.Last - Frame_.First;
        BRepPrimAPI_MakePrism _Prism(_StartFace, gp_Vec(Frame_.ZDirection) * _Length, false, true);
        if (!_Prism.IsDone())
        {
            return std::nullopt;
        }
        return _Prism.Shape();
    }

    std::optional<TopoDS_Shape> _MakeSectionPrimitivePreviewShape(
        IN const SSliceLoop& Loop_,
        IN const SSliceCandidate& Slice_,
        IN const SSectionFrame& Frame_)
    {
        auto _FaceBuilder = BRepBuilderAPI_MakeFace(
            Slice_.Plane,
            _OrientedWire(Loop_, true),
            true);
        if (!_FaceBuilder.IsDone())
        {
            return std::nullopt;
        }
        gp_Trsf _Translation;
        _Translation.SetTranslation(
            gp_Vec(Frame_.ZDirection) * (Frame_.First - Slice_.Station));
        const auto _StartFace = BRepBuilderAPI_Transform(
            _FaceBuilder.Face(), _Translation, true).Shape();
        BRepPrimAPI_MakePrism _Prism(
            _StartFace,
            gp_Vec(Frame_.ZDirection) * (Frame_.Last - Frame_.First),
            false,
            true);
        if (!_Prism.IsDone())
        {
            return std::nullopt;
        }
        return _Prism.Shape();
    }

    Tube::SExtrudedRegionNode _MakeExtrudedRegionNodeData(
        IN const SSliceCandidate& Slice_,
        IN const SSectionFrame& Frame_,
        IN const std::string& strOwnerNodeID_ = "feature")
    {
        Tube::SExtrudedRegionNode _Node;
        _Node.Section = _MakeNeutralRegion(Slice_, Frame_);
        _Node.SectionPrimitives = _MakeSectionPrimitives(
            _Node.Section, strOwnerNodeID_);
        _Node.Frame.Location = { Frame_.Origin.X(), Frame_.Origin.Y(), Frame_.Origin.Z() };
        _Node.Frame.XDirection = { Frame_.XDirection.X(), Frame_.XDirection.Y(), Frame_.XDirection.Z() };
        _Node.Frame.YDirection = { Frame_.YDirection.X(), Frame_.YDirection.Y(), Frame_.YDirection.Z() };
        _Node.Frame.ZDirection = { Frame_.ZDirection.X(), Frame_.ZDirection.Y(), Frame_.ZDirection.Z() };
        _Node.First = 0.0;
        _Node.Last = Frame_.Last - Frame_.First;
        const auto _Ordered = _OrderedSliceLoops(Slice_);
        _Node.SideAtlases.reserve(_Ordered.size());
        for (std::size_t _Index = 0; _Index < _Ordered.size(); ++_Index)
        {
            Tube::SSideAtlasDefinition _Atlas;
            _Atlas.LoopID = _Index == 0
                ? std::string("outer")
                : "inner-" + std::to_string(_Index);
            _Atlas.ID = _Atlas.LoopID + "/side-atlas";
            _Atlas.UPeriod = _WireLength(_Ordered[_Index]->Wire);
            _Atlas.VFirst = _Node.First;
            _Atlas.VLast = _Node.Last;
            _Node.SideAtlases.push_back(std::move(_Atlas));
        }
        return _Node;
    }

    double _CurveLength(
        IN const BRepAdaptor_Curve& Curve_,
        IN double dFirst_,
        IN double dLast_)
    {
        try
        {
            return std::abs(GCPnts_AbscissaPoint::Length(
                Curve_,
                dFirst_,
                dLast_,
                Precision::Confusion()));
        }
        catch (const Standard_Failure&)
        {
            gp_Pnt _FirstPoint;
            gp_Pnt _LastPoint;
            Curve_.D0(dFirst_, _FirstPoint);
            Curve_.D0(dLast_, _LastPoint);
            return _FirstPoint.Distance(_LastPoint);
        }
    }

    std::vector<SStableSideAtlas> _BuildStableSideAtlases(
        IN const SSliceCandidate& Slice_,
        IN const SSectionFrame& Frame_)
    {
        std::vector<SStableSideAtlas> _Atlases;
        const auto _Ordered = _OrderedSliceLoops(Slice_);
        _Atlases.reserve(_Ordered.size());
        for (std::size_t _LoopIndex = 0; _LoopIndex < _Ordered.size(); ++_LoopIndex)
        {
            const auto _LoopID = _LoopIndex == 0
                ? std::string("outer")
                : "inner-" + std::to_string(_LoopIndex);
            const auto _Wire = _OrientedWire(*_Ordered[_LoopIndex], _LoopIndex == 0);
            std::vector<TopoDS_Edge> _Edges;
            std::vector<gp_Pnt> _Starts;
            for (BRepTools_WireExplorer _Explorer(_Wire);
                _Explorer.More();
                _Explorer.Next())
            {
                _Edges.push_back(_Explorer.Current());
                _Starts.push_back(BRep_Tool::Pnt(_Explorer.CurrentVertex()));
            }
            if (_Edges.empty())
            {
                continue;
            }
            const auto _Seam = std::min_element(
                _Starts.begin(),
                _Starts.end(),
                [&Frame_](IN const gp_Pnt& Left_, IN const gp_Pnt& Right_) {
                    const auto _Left = _ToPoint2(Left_, Frame_);
                    const auto _Right = _ToPoint2(Right_, Frame_);
                    return _Left.X < _Right.X
                        || (_Left.X == _Right.X && _Left.Y < _Right.Y);
                });
            const auto _Offset = static_cast<std::size_t>(
                std::distance(_Starts.begin(), _Seam));
            std::rotate(_Edges.begin(), _Edges.begin() + _Offset, _Edges.end());
            std::rotate(_Starts.begin(), _Starts.begin() + _Offset, _Starts.end());

            SStableSideAtlas _Atlas;
            _Atlas.LoopID = _LoopID;
            _Atlas.VFirst = 0.0;
            _Atlas.VLast = Frame_.Last - Frame_.First;
            double _U = 0.0;
            for (std::size_t _EdgeIndex = 0;
                _EdgeIndex < _Edges.size();
                ++_EdgeIndex)
            {
                BRepAdaptor_Curve _Curve(_Edges[_EdgeIndex]);
                const auto _First = _Curve.FirstParameter();
                const auto _Last = _Curve.LastParameter();
                gp_Pnt _FirstPoint;
                gp_Pnt _LastPoint;
                _Curve.D0(_First, _FirstPoint);
                _Curve.D0(_Last, _LastPoint);
                const auto _Reversed = _Starts[_EdgeIndex].Distance(_LastPoint)
                    < _Starts[_EdgeIndex].Distance(_FirstPoint);
                const auto _Length = _CurveLength(_Curve, _First, _Last);
                SStableAtlasEdge _AtlasEdge;
                _AtlasEdge.Edge = _Edges[_EdgeIndex];
                _AtlasEdge.CurveID = _LoopID + "/curve-"
                    + std::to_string(_EdgeIndex + 1);
                _AtlasEdge.UFirst = _U;
                _AtlasEdge.ULast = _U + _Length;
                _AtlasEdge.ParameterFirst = _First;
                _AtlasEdge.ParameterLast = _Last;
                _AtlasEdge.Reversed = _Reversed;
                _Atlas.Edges.push_back(std::move(_AtlasEdge));
                _U += _Length;
            }
            _Atlas.UPeriod = _U;
            if (_Atlas.UPeriod > 0.0)
            {
                _Atlases.push_back(std::move(_Atlas));
            }
        }
        return _Atlases;
    }

    std::pair<double, gp_Pnt> _ProjectPointToCurve(
        IN const gp_Pnt& Point_,
        IN const BRepAdaptor_Curve& Curve_,
        IN double dFirst_,
        IN double dLast_)
    {
        constexpr int kCoarseSamples = 48;
        auto _BestParameter = dFirst_;
        auto _BestDistance = (std::numeric_limits<double>::max)();
        int _BestIndex = 0;
        for (int _Index = 0; _Index <= kCoarseSamples; ++_Index)
        {
            const auto _Parameter = dFirst_ + (dLast_ - dFirst_)
                * (static_cast<double>(_Index) / kCoarseSamples);
            gp_Pnt _Candidate;
            Curve_.D0(_Parameter, _Candidate);
            const auto _Distance = Point_.SquareDistance(_Candidate);
            if (_Distance < _BestDistance)
            {
                _BestDistance = _Distance;
                _BestParameter = _Parameter;
                _BestIndex = _Index;
            }
        }
        auto _Lower = dFirst_ + (dLast_ - dFirst_)
            * (static_cast<double>(std::max(0, _BestIndex - 1)) / kCoarseSamples);
        auto _Upper = dFirst_ + (dLast_ - dFirst_)
            * (static_cast<double>(std::min(kCoarseSamples, _BestIndex + 1)) / kCoarseSamples);
        for (int _Iteration = 0; _Iteration < 36; ++_Iteration)
        {
            const auto _Left = _Lower + (_Upper - _Lower) / 3.0;
            const auto _Right = _Upper - (_Upper - _Lower) / 3.0;
            gp_Pnt _LeftPoint;
            gp_Pnt _RightPoint;
            Curve_.D0(_Left, _LeftPoint);
            Curve_.D0(_Right, _RightPoint);
            if (Point_.SquareDistance(_LeftPoint) <= Point_.SquareDistance(_RightPoint))
            {
                _Upper = _Right;
            }
            else
            {
                _Lower = _Left;
            }
        }
        _BestParameter = 0.5 * (_Lower + _Upper);
        gp_Pnt _BestPoint;
        Curve_.D0(_BestParameter, _BestPoint);
        return { _BestParameter, _BestPoint };
    }

    std::optional<SStableAtlasProjection> _ProjectToStableSideAtlas(
        IN const gp_Pnt& Point_,
        IN const std::vector<SStableSideAtlas>& Atlases_,
        IN const SSliceCandidate& Slice_,
        IN const SSectionFrame& Frame_,
        IN std::optional<std::size_t> AtlasIndex_ = std::nullopt)
    {
        if (Atlases_.empty())
        {
            return std::nullopt;
        }
        const auto _V = gp_Vec(Frame_.Origin, Point_).Dot(gp_Vec(Frame_.ZDirection));
        const auto _SliceV = Slice_.Station - Frame_.First;
        const auto _Flattened = Point_.Translated(
            gp_Vec(Frame_.ZDirection) * (_SliceV - _V));
        std::optional<SStableAtlasProjection> _Best;
        for (std::size_t _AtlasIndex = 0;
            _AtlasIndex < Atlases_.size();
            ++_AtlasIndex)
        {
            if (AtlasIndex_ && *AtlasIndex_ != _AtlasIndex)
            {
                continue;
            }
            const auto& _Atlas = Atlases_[_AtlasIndex];
            for (std::size_t _EdgeIndex = 0;
                _EdgeIndex < _Atlas.Edges.size();
                ++_EdgeIndex)
            {
                const auto& _Edge = _Atlas.Edges[_EdgeIndex];
                BRepAdaptor_Curve _Curve(_Edge.Edge);
                const auto [_Parameter, _CurvePoint] = _ProjectPointToCurve(
                    _Flattened,
                    _Curve,
                    _Edge.ParameterFirst,
                    _Edge.ParameterLast);
                gp_Pnt _PointOnCurve;
                gp_Vec _Tangent;
                _Curve.D1(_Parameter, _PointOnCurve, _Tangent);
                if (_Edge.Reversed)
                {
                    _Tangent.Reverse();
                }
                _Tangent -= gp_Vec(Frame_.ZDirection)
                    * _Tangent.Dot(gp_Vec(Frame_.ZDirection));
                if (_Tangent.SquareMagnitude() <= Precision::SquareConfusion())
                {
                    continue;
                }
                _Tangent.Normalize();
                auto _Normal = _Tangent.Crossed(gp_Vec(Frame_.ZDirection));
                if (_Normal.SquareMagnitude() <= Precision::SquareConfusion())
                {
                    continue;
                }
                _Normal.Normalize();
                const auto _SupportPoint = _PointOnCurve.Translated(
                    gp_Vec(Frame_.ZDirection) * (_V - _SliceV));
                const gp_Vec _Offset(_SupportPoint, Point_);
                const auto _NormalOffset = _Offset.Dot(_Normal);
                const auto _ResidualVector = _Offset - _Normal * _NormalOffset;
                const auto _Residual = _ResidualVector.Magnitude();
                if (_Best && _Residual >= _Best->Residual)
                {
                    continue;
                }
                double _PartialLength = 0.0;
                if (_Edge.Reversed)
                {
                    _PartialLength = _CurveLength(
                        _Curve,
                        _Parameter,
                        _Edge.ParameterLast);
                }
                else
                {
                    _PartialLength = _CurveLength(
                        _Curve,
                        _Edge.ParameterFirst,
                        _Parameter);
                }
                SStableAtlasProjection _Projection;
                _Projection.AtlasIndex = _AtlasIndex;
                _Projection.EdgeIndex = _EdgeIndex;
                _Projection.CurveID = _Edge.CurveID;
                _Projection.U = _Edge.UFirst + _PartialLength;
                _Projection.V = _V;
                _Projection.NormalOffset = _NormalOffset;
                _Projection.Residual = _Residual;
                _Projection.SupportPoint = _SupportPoint;
                _Projection.MaterialOutwardNormal = gp_Dir(_Normal);
                _Best = std::move(_Projection);
            }
        }
        return _Best;
    }

    std::vector<gp_Pnt> _SampleFacePoints(
        IN const TopoDS_Face& Face_,
        IN int nInteriorCount_ = 3,
        IN bool bIncludeVertices_ = true)
    {
        std::vector<gp_Pnt> _Points;
        if (bIncludeVertices_)
        {
            for (TopExp_Explorer _Explorer(Face_, TopAbs_VERTEX);
                _Explorer.More();
                _Explorer.Next())
            {
                _Points.push_back(BRep_Tool::Pnt(TopoDS::Vertex(_Explorer.Current())));
            }
        }
        BRepAdaptor_Surface _Surface(Face_, true);
        const auto _UFirst = _Surface.FirstUParameter();
        const auto _ULast = _Surface.LastUParameter();
        const auto _VFirst = _Surface.FirstVParameter();
        const auto _VLast = _Surface.LastVParameter();
        if (!std::isfinite(_UFirst) || !std::isfinite(_ULast)
            || !std::isfinite(_VFirst) || !std::isfinite(_VLast))
        {
            return _Points;
        }
        for (int _UIndex = 1; _UIndex <= nInteriorCount_; ++_UIndex)
        {
            for (int _VIndex = 1; _VIndex <= nInteriorCount_; ++_VIndex)
            {
                const auto _U = _UFirst + (_ULast - _UFirst)
                    * (static_cast<double>(_UIndex) / (nInteriorCount_ + 1));
                const auto _V = _VFirst + (_VLast - _VFirst)
                    * (static_cast<double>(_VIndex) / (nInteriorCount_ + 1));
                BRepClass_FaceClassifier _Classifier(
                    Face_,
                    gp_Pnt2d(_U, _V),
                    Precision::Confusion(),
                    true);
                if (_Classifier.State() == TopAbs_OUT)
                {
                    continue;
                }
                try
                {
                    gp_Pnt _Point;
                    _Surface.D0(_U, _V, _Point);
                    _Points.push_back(_Point);
                }
                catch (const Standard_Failure&)
                {
                }
            }
        }
        return _Points;
    }

    std::optional<Tube::SRegionLoop2> _MapWireToStableUV(
        IN const TopoDS_Wire& Wire_,
        IN bool bOuter_,
        IN std::size_t nAtlasIndex_,
        IN const std::vector<SStableSideAtlas>& Atlases_,
        IN const SSliceCandidate& Slice_,
        IN const SSectionFrame& Frame_,
        IN double dTolerance_,
        IN const std::string& strLoopID_,
        OUT std::set<std::string>* pCurveIDs_)
    {
        const auto& _Atlas = Atlases_[nAtlasIndex_];
        std::vector<Point2> _Points;
        for (BRepTools_WireExplorer _Explorer(Wire_);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Edge = _Explorer.Current();
            BRepAdaptor_Curve _Curve(_Edge);
            const auto _First = _Curve.FirstParameter();
            const auto _Last = _Curve.LastParameter();
            const auto _Start = BRep_Tool::Pnt(_Explorer.CurrentVertex());
            gp_Pnt _FirstPoint;
            gp_Pnt _LastPoint;
            _Curve.D0(_First, _FirstPoint);
            _Curve.D0(_Last, _LastPoint);
            const auto _Reversed = _Start.Distance(_LastPoint) < _Start.Distance(_FirstPoint);
            constexpr int kSamplesPerEdge = 12;
            for (int _Index = 0; _Index <= kSamplesPerEdge; ++_Index)
            {
                if (!_Points.empty() && _Index == 0)
                {
                    continue;
                }
                auto _Fraction = static_cast<double>(_Index) / kSamplesPerEdge;
                if (_Reversed)
                {
                    _Fraction = 1.0 - _Fraction;
                }
                gp_Pnt _Point;
                _Curve.D0(_First + (_Last - _First) * _Fraction, _Point);
                auto _Projection = _ProjectToStableSideAtlas(
                    _Point,
                    Atlases_,
                    Slice_,
                    Frame_,
                    nAtlasIndex_);
                if (!_Projection || _Projection->Residual > 10.0 * dTolerance_
                    || std::abs(_Projection->NormalOffset) > 10.0 * dTolerance_)
                {
                    return std::nullopt;
                }
                if (pCurveIDs_)
                {
                    pCurveIDs_->insert(_Projection->CurveID);
                }
                auto _U = _Projection->U;
                if (!_Points.empty() && _Atlas.UPeriod > dTolerance_)
                {
                    while (_U - _Points.back().X > 0.5 * _Atlas.UPeriod)
                    {
                        _U -= _Atlas.UPeriod;
                    }
                    while (_Points.back().X - _U > 0.5 * _Atlas.UPeriod)
                    {
                        _U += _Atlas.UPeriod;
                    }
                }
                _Points.push_back({ _U, _Projection->V });
            }
        }
        if (_Points.size() > 2
            && std::hypot(
                _Points.front().X - _Points.back().X,
                _Points.front().Y - _Points.back().Y) <= 10.0 * dTolerance_)
        {
            _Points.pop_back();
        }
        if (_Points.size() < 3)
        {
            return std::nullopt;
        }
        const auto _Area = _SignedArea(_Points);
        if ((_Area > 0.0) != bOuter_)
        {
            std::reverse(_Points.begin(), _Points.end());
        }
        Tube::SRegionLoop2 _Loop;
        _Loop.ID = strLoopID_;
        Tube::SRegionCurve2 _Curve;
        _Curve.ID = strLoopID_ + "/polyline";
        Polyline2 _Polyline;
        _Polyline.Points = std::move(_Points);
        _Polyline.Closed = true;
        _Curve.Segment.Curve = std::move(_Polyline);
        _Curve.Segment.Range = { 0.0, 1.0, false };
        _Loop.Curves.push_back(std::move(_Curve));
        return _Loop;
    }

    std::optional<SRecognizedExtrusion> _TryRecognizeWrappedNormalRemoval(
        IN const TopoDS_Shape& RemovedSolid_,
        IN const TopoDS_Shape& MainBase_,
        IN const SSliceCandidate& Slice_,
        IN const SSectionFrame& Frame_,
        IN const std::vector<SStableSideAtlas>& Atlases_,
        IN const STubeCSGConversionOptions& Options_,
        IN const std::string& strNodeID_,
        OUT std::string* pFailureReason_ = nullptr)
    {
        const auto _SetFailure = [pFailureReason_](IN const std::string& Reason_) {
            if (pFailureReason_)
            {
                *pFailureReason_ = Reason_;
            }
        };
        if (Atlases_.empty())
        {
            _SetFailure("base has no stable side atlas");
            return std::nullopt;
        }
        struct SSupportFace final
        {
            TopoDS_Face Face;
            std::size_t AtlasIndex = 0;
            double Area = 0.0;
        };
        std::vector<SSupportFace> _SupportFaces;
        std::vector<std::pair<TopoDS_Face, bool>> _Faces;
        for (TopExp_Explorer _Explorer(RemovedSolid_, TopAbs_FACE);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Face = TopoDS::Face(_Explorer.Current());
            const auto _Inherited = _IsFaceOnBaseBoundary(_Face, MainBase_, Options_);
            _Faces.emplace_back(_Face, _Inherited);
            if (!_Inherited)
            {
                continue;
            }
            const auto _Samples = _SampleFacePoints(_Face, 2);
            for (std::size_t _AtlasIndex = 0;
                _AtlasIndex < Atlases_.size();
                ++_AtlasIndex)
            {
                const auto _OnAtlas = !_Samples.empty()
                    && std::all_of(
                        _Samples.begin(),
                        _Samples.end(),
                        [&](IN const gp_Pnt& Point_) {
                            const auto _Projection = _ProjectToStableSideAtlas(
                                Point_,
                                Atlases_,
                                Slice_,
                                Frame_,
                                _AtlasIndex);
                            return _Projection
                                && _Projection->Residual <= 10.0 * Options_.Tolerance
                                && std::abs(_Projection->NormalOffset)
                                    <= 10.0 * Options_.Tolerance;
                        });
                if (_OnAtlas)
                {
                    _SupportFaces.push_back({
                        _Face,
                        _AtlasIndex,
                        _FaceArea(_Face)
                    });
                }
            }
        }
        if (_SupportFaces.empty())
        {
            _SetFailure("removed solid has no inherited side-atlas support patch");
            return std::nullopt;
        }

        std::vector<double> _AtlasAreas(Atlases_.size(), 0.0);
        for (const auto& _Support : _SupportFaces)
        {
            _AtlasAreas[_Support.AtlasIndex] += _Support.Area;
        }
        const auto _BestAtlasIter = std::max_element(
            _AtlasAreas.begin(),
            _AtlasAreas.end());
        const auto _AtlasIndex = static_cast<std::size_t>(
            std::distance(_AtlasAreas.begin(), _BestAtlasIter));
        const auto& _Atlas = Atlases_[_AtlasIndex];
        if (*_BestAtlasIter <= Options_.Tolerance * Options_.Tolerance)
        {
            _SetFailure("side-atlas support patch area is degenerate");
            return std::nullopt;
        }

        Tube::CPlanarRegion2 _UVRegion;
        _UVRegion.FillRule = Tube::EPlanarFillRule::NonZero;
        std::set<std::string> _CurveIDs;
        std::vector<TopoDS_Face> _SelectedSupportFaces;
        std::size_t _UVLoopIndex = 0;
        for (const auto& _Support : _SupportFaces)
        {
            if (_Support.AtlasIndex != _AtlasIndex)
            {
                continue;
            }
            _SelectedSupportFaces.push_back(_Support.Face);
            const auto _OuterWire = BRepTools::OuterWire(_Support.Face);
            for (TopExp_Explorer _WireExplorer(_Support.Face, TopAbs_WIRE);
                _WireExplorer.More();
                _WireExplorer.Next())
            {
                const auto _Wire = TopoDS::Wire(_WireExplorer.Current());
                const auto _IsOuter = !_OuterWire.IsNull() && _Wire.IsSame(_OuterWire);
                auto _Loop = _MapWireToStableUV(
                    _Wire,
                    _IsOuter,
                    _AtlasIndex,
                    Atlases_,
                    Slice_,
                    Frame_,
                    Options_.Tolerance,
                    "uv-loop-" + std::to_string(++_UVLoopIndex),
                    &_CurveIDs);
                if (_Loop)
                {
                    _UVRegion.Boundaries.push_back(std::move(*_Loop));
                }
            }
        }
        if (_UVRegion.Boundaries.empty())
        {
            _SetFailure("support patches could not be mapped to a closed stable UV region");
            return std::nullopt;
        }

        auto _MinimumOffset = 0.0;
        auto _MaximumPositiveOffset = 0.0;
        auto _MaximumResidual = 0.0;
        std::size_t _GeneratedNormalSampleCount = 0;
        std::size_t _GeneratedCompatibleSampleCount = 0;
        bool _HasGeneratedNormalTermination = false;
        for (const auto& [_Face, _Inherited] : _Faces)
        {
            for (const auto& _Point : _SampleFacePoints(_Face, 2))
            {
                const auto _Projection = _ProjectToStableSideAtlas(
                    _Point,
                    Atlases_,
                    Slice_,
                    Frame_,
                    _AtlasIndex);
                if (!_Projection)
                {
                    _SetFailure("removed boundary leaves the selected normal-coordinate chart");
                    return std::nullopt;
                }
                _MaximumResidual = std::max(_MaximumResidual, _Projection->Residual);
                _MinimumOffset = std::min(_MinimumOffset, _Projection->NormalOffset);
                _MaximumPositiveOffset = std::max(
                    _MaximumPositiveOffset,
                    _Projection->NormalOffset);
            }
            if (_Inherited)
            {
                continue;
            }
            for (const auto& _Sample : _SampleTangentPlanes(_Face))
            {
                const auto _Projection = _ProjectToStableSideAtlas(
                    _Sample.Point,
                    Atlases_,
                    Slice_,
                    Frame_,
                    _AtlasIndex);
                if (!_Projection)
                {
                    continue;
                }
                ++_GeneratedNormalSampleCount;
                const auto _Dot = _AbsDot(
                    _Sample.Normal,
                    _Projection->MaterialOutwardNormal);
                const auto _Lateral = _Dot <= std::max(
                    0.02,
                    8.0 * Options_.AngularToleranceRadians);
                const auto _Termination = _Dot >= 1.0 - std::max(
                    0.02,
                    8.0 * Options_.AngularToleranceRadians);
                if (_Lateral || _Termination)
                {
                    ++_GeneratedCompatibleSampleCount;
                    _HasGeneratedNormalTermination =
                        _HasGeneratedNormalTermination || _Termination;
                }
            }
        }
        const auto _NormalCoverage = _GeneratedNormalSampleCount > 0
            ? static_cast<double>(_GeneratedCompatibleSampleCount)
                / static_cast<double>(_GeneratedNormalSampleCount)
            : 0.0;
        const auto _FlipObservedNormal = _MaximumPositiveOffset > -_MinimumOffset;
        if (_FlipObservedNormal)
        {
            const auto _OldMinimumOffset = _MinimumOffset;
            const auto _OldMaximumPositiveOffset = _MaximumPositiveOffset;
            _MinimumOffset = -_OldMaximumPositiveOffset;
            _MaximumPositiveOffset = std::max(0.0, -_OldMinimumOffset);
        }
        const auto _Depth = -_MinimumOffset;
        if (_Depth <= 5.0 * Options_.Tolerance
            || _MaximumPositiveOffset > 10.0 * Options_.Tolerance
            || _MaximumResidual > 20.0 * Options_.Tolerance
            || _NormalCoverage < 0.9)
        {
            _SetFailure(
                "normal-column boundary evidence rejected: depth="
                + _ToString(_Depth)
                + ", positiveOffset=" + _ToString(_MaximumPositiveOffset)
                + ", residual=" + _ToString(_MaximumResidual)
                + ", normalCoverage=" + _ToString(_NormalCoverage));
            return std::nullopt;
        }

        BRepClass3d_SolidClassifier _BaseClassifier(MainBase_);
        BRepClass3d_SolidClassifier _RemovalClassifier(RemovedSolid_);
        std::size_t _RaySampleCount = 0;
        std::size_t _RayMismatchCount = 0;
        for (const auto& _Face : _SelectedSupportFaces)
        {
            for (const auto& _Point : _SampleFacePoints(_Face, 4, false))
            {
                const auto _Projection = _ProjectToStableSideAtlas(
                    _Point,
                    Atlases_,
                    Slice_,
                    Frame_,
                    _AtlasIndex);
                if (!_Projection)
                {
                    continue;
                }
                for (int _DepthIndex = 1; _DepthIndex <= 7; ++_DepthIndex)
                {
                    const auto _Offset = _MinimumOffset
                        * (static_cast<double>(_DepthIndex) / 8.0);
                    auto _ObservedOutwardNormal = gp_Vec(
                        _Projection->MaterialOutwardNormal);
                    if (_FlipObservedNormal)
                    {
                        _ObservedOutwardNormal.Reverse();
                    }
                    const auto _Probe = _Projection->SupportPoint.Translated(
                        _ObservedOutwardNormal * _Offset);
                    _BaseClassifier.Perform(_Probe, 5.0 * Options_.Tolerance);
                    if (_BaseClassifier.State() == TopAbs_OUT)
                    {
                        continue;
                    }
                    ++_RaySampleCount;
                    _RemovalClassifier.Perform(_Probe, 5.0 * Options_.Tolerance);
                    if (_RemovalClassifier.State() == TopAbs_OUT)
                    {
                        ++_RayMismatchCount;
                    }
                }
            }
        }
        const auto _RayCoverage = _RaySampleCount > 0
            ? 1.0 - static_cast<double>(_RayMismatchCount)
                / static_cast<double>(_RaySampleCount)
            : 0.0;
        if (_RayCoverage < 0.98)
        {
            _SetFailure("normal-column material ray coverage=" + _ToString(_RayCoverage));
            return std::nullopt;
        }

        auto _VMinimum = (std::numeric_limits<double>::max)();
        auto _VMaximum = (std::numeric_limits<double>::lowest)();
        for (const auto& _Loop : _UVRegion.Boundaries)
        {
            for (const auto& _Curve : _Loop.Curves)
            {
                const auto* _pPolyline = std::get_if<Polyline2>(&_Curve.Segment.Curve);
                if (!_pPolyline)
                {
                    continue;
                }
                for (const auto& _Point : _pPolyline->Points)
                {
                    _VMinimum = std::min(_VMinimum, _Point.Y);
                    _VMaximum = std::max(_VMaximum, _Point.Y);
                }
            }
        }
        const auto _TouchesEnd = _VMinimum <= _Atlas.VFirst + 10.0 * Options_.Tolerance
            || _VMaximum >= _Atlas.VLast - 10.0 * Options_.Tolerance;

        Tube::SWrappedVolumeNode _Wrapped;
        _Wrapped.Support.ExtrusionNodeID = "base";
        _Wrapped.Support.LoopID = _Atlas.LoopID;
        if (_CurveIDs.size() == 1)
        {
            _Wrapped.Support.CurveID = *_CurveIDs.begin();
        }
        _Wrapped.UVRegion = std::move(_UVRegion);
        _Wrapped.LowerNormalOffset = _MinimumOffset;
        _Wrapped.UpperNormalOffset = 0.0;

        Tube::SFeatureRelations _Relations;
        _Relations.DependsOnNodeIDs.push_back("base");
        Tube::SHostPlacementConstraint _Placement;
        _Placement.Rule = Tube::EHostPlacementRule::AttachedToHostBoundary;
        _Placement.HostNodeID = "base";
        _Placement.Support = _Wrapped.Support;
        _Placement.Confidence = 0.94;
        _Relations.Placement = std::move(_Placement);
        Tube::SFeatureExtentConstraint _Extent;
        _Extent.Rule = _HasGeneratedNormalTermination
            ? Tube::EFeatureExtentRule::FixedInterval
            : Tube::EFeatureExtentRule::ThroughAllHostMaterial;
        _Extent.Direction = Tube::EExtentDirection::Negative;
        _Extent.HostNodeID = "base";
        _Extent.PrimitiveFirst.Value = _MinimumOffset;
        _Extent.PrimitiveLast.Value = 0.0;
        _Extent.RecomputeAgainstHost = !_HasGeneratedNormalTermination;
        _Extent.Confidence = _HasGeneratedNormalTermination ? 0.82 : 0.94;
        _Relations.Extent = std::move(_Extent);
        Tube::SFeatureMaterialEffect _MaterialEffect;
        _MaterialEffect.Role = _TouchesEnd
            ? Tube::EFeatureMaterialRole::Truncation
            : Tube::EFeatureMaterialRole::Penetration;
        _MaterialEffect.HostNodeID = "base";
        _MaterialEffect.Confidence = _TouchesEnd ? 0.94 : 0.86;
        _MaterialEffect.Metadata["classificationBasis"] = _TouchesEnd
            ? "uv-region-touches-virtual-end"
            : "uv-region-is-interior";
        _Relations.MaterialEffect = std::move(_MaterialEffect);

        Tube::SRemovalSemantics _Semantics;
        _Semantics.Extent = _HasGeneratedNormalTermination
            ? Tube::ERemovalExtentKind::Blind
            : Tube::ERemovalExtentKind::Through;
        _Semantics.BoundaryOpeningPatchCount = static_cast<std::uint32_t>(
            _SelectedSupportFaces.size());
        _Semantics.InternalTerminationCount = _HasGeneratedNormalTermination ? 1u : 0u;
        _Semantics.Confidence = std::min(_NormalCoverage, _RayCoverage);
        _Semantics.Metadata["extentDirection"] = "host-material-outward-normal";

        SRecognizedExtrusion _Result;
        _Result.ShapeInsideBase = RemovedSolid_;
        _Result.RelativeFitError = std::max(
            _MaximumResidual / std::max(_Depth, Options_.Tolerance),
            1.0 - std::min(_NormalCoverage, _RayCoverage));
        _Result.CoverageRatio = _RayCoverage;
        _Result.GeometryAcceptanceTolerance = 2.0e-2;
        _Result.Node.ID = strNodeID_;
        _Result.Node.Label = _TouchesEnd
            ? "Recognized wrapped-normal truncation"
            : "Recognized wrapped-normal removal";
        _Result.Node.Data = std::move(_Wrapped);
        _Result.Node.Relations = std::move(_Relations);
        _Result.Node.RemovalSemantics = std::move(_Semantics);
        _Result.Node.Metadata["source"] = "brep.stable-side-atlas-normal-columns";
        _Result.Node.Metadata["featureSemantic"] = _TouchesEnd
            ? "wrapped-normal-end-trim"
            : "wrapped-normal-penetration";
        _Result.Node.Metadata["normalEvidenceCoverage"] = _ToString(_NormalCoverage);
        _Result.Node.Metadata["normalRayCoverage"] = _ToString(_RayCoverage);
        _Result.Node.Metadata["fitError"] = _ToString(_Result.RelativeFitError);
        _Result.Node.Metadata["offsetField"] = "constant-observed-snapshot";
        _Result.Node.Metadata["atlasNormalOrientationAdjusted"] =
            _FlipObservedNormal ? "true" : "false";
        _Result.Node.Metadata["supportUPeriod"] = _ToString(_Atlas.UPeriod);
        return _Result;
    }

    Tube::CPlanarRegion2 _MakeCircularRegion(
        IN const Point2& Center_,
        IN double dRadius_)
    {
        Tube::CPlanarRegion2 _Region;
        _Region.FillRule = Tube::EPlanarFillRule::NonZero;
        Tube::SRegionLoop2 _Loop;
        _Loop.ID = "outer";
        Tube::SRegionCurve2 _Curve;
        _Curve.ID = "outer/curve-1";
        Circle2 _Circle;
        _Circle.Placement.Location = Center_;
        _Circle.Placement.XDirection = { 1.0, 0.0 };
        _Circle.Placement.YDirection = { 0.0, 1.0 };
        _Circle.Radius = dRadius_;
        _Curve.Segment.Curve = std::move(_Circle);
        _Curve.Segment.Range = { 0.0, 2.0 * std::acos(-1.0), false };
        _Loop.Curves.push_back(std::move(_Curve));
        _Region.Boundaries.push_back(std::move(_Loop));
        return _Region;
    }

    iCAX::GeometryData::Placement3 _MakeNeutralPlacement(IN const SSectionFrame& Frame_)
    {
        iCAX::GeometryData::Placement3 _Placement;
        _Placement.Location = { Frame_.Origin.X(), Frame_.Origin.Y(), Frame_.Origin.Z() };
        _Placement.XDirection = {
            Frame_.XDirection.X(),
            Frame_.XDirection.Y(),
            Frame_.XDirection.Z()
        };
        _Placement.YDirection = {
            Frame_.YDirection.X(),
            Frame_.YDirection.Y(),
            Frame_.YDirection.Z()
        };
        _Placement.ZDirection = {
            Frame_.ZDirection.X(),
            Frame_.ZDirection.Y(),
            Frame_.ZDirection.Z()
        };
        return _Placement;
    }

    std::optional<SRecognizedAlternative> _TryRecognizeConicalRemoval(
        IN const TopoDS_Shape& RemovedSolid_,
        IN const TopoDS_Shape& MainBase_,
        IN const STubeCSGConversionOptions& Options_,
        IN const std::string& strNodeID_)
    {
        std::optional<std::pair<gp_Cone, double>> _BestCone;
        for (TopExp_Explorer _Explorer(RemovedSolid_, TopAbs_FACE);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Face = TopoDS::Face(_Explorer.Current());
            BRepAdaptor_Surface _Surface(_Face, true);
            if (_Surface.GetType() != GeomAbs_Cone)
            {
                continue;
            }
            const auto _Area = _FaceArea(_Face);
            if (!_BestCone || _Area > _BestCone->second)
            {
                _BestCone = std::make_pair(_Surface.Cone(), _Area);
            }
        }
        std::optional<SCircularConeModel> _ProjectiveCone;
        if (!_BestCone)
        {
            _ProjectiveCone = _RecognizeCircularConeFromTangentPlanes(
                RemovedSolid_,
                MainBase_,
                Options_);
            if (!_ProjectiveCone)
            {
                return std::nullopt;
            }
        }

        const auto _Axis = _BestCone
            ? _BestCone->first.Axis().Direction()
            : _ProjectiveCone->Axis;
        const auto _XDirection = _BestCone
            ? _BestCone->first.Position().XDirection()
            : _ProjectiveCone->XDirection;
        const auto _ReferencePoint = _BestCone
            ? _BestCone->first.Location()
            : _ProjectiveCone->Apex;
        const auto _ReferenceRadius = _BestCone
            ? _BestCone->first.RefRadius()
            : 0.0;
        const auto _SemiAngle = _BestCone
            ? _BestCone->first.SemiAngle()
            : _ProjectiveCone->SemiAngle;
        const auto _Range = _ProjectAxialRange(RemovedSolid_, _Axis);
        if (!_Range)
        {
            return std::nullopt;
        }
        const auto _Length = _Range->second - _Range->first;
        const auto _ReferenceStation = gp_Vec(
            gp_Pnt(0.0, 0.0, 0.0),
            _ReferencePoint).Dot(gp_Vec(_Axis));
        const auto _FirstOffset = _Range->first - _ReferenceStation;
        const auto _LastOffset = _Range->second - _ReferenceStation;
        const auto _Slope = std::tan(_SemiAngle);
        const auto _FirstRadius = _ReferenceRadius + _FirstOffset * _Slope;
        const auto _LastRadius = _ReferenceRadius + _LastOffset * _Slope;
        if (_Length <= Options_.Tolerance
            || _FirstRadius <= Options_.Tolerance
            || _LastRadius <= Options_.Tolerance
            || std::abs(_FirstRadius - _LastRadius) <= Options_.Tolerance)
        {
            return std::nullopt;
        }

        const auto _StartCenter = _ReferencePoint.Translated(gp_Vec(_Axis) * _FirstOffset);
        BRepPrimAPI_MakeCone _ConeBuilder(
            gp_Ax2(_StartCenter, _Axis, _XDirection),
            _FirstRadius,
            _LastRadius,
            _Length);
        _ConeBuilder.Build();
        if (!_ConeBuilder.IsDone())
        {
            return std::nullopt;
        }
        const auto _FullCone = _ConeBuilder.Shape();
        BRepAlgoAPI_Common _InsideBase(_FullCone, MainBase_);
        _InsideBase.SetFuzzyValue(Options_.Tolerance);
        _InsideBase.Build();
        if (!_InsideBase.IsDone())
        {
            return std::nullopt;
        }
        const auto _InsideShape = _InsideBase.Shape();
        const auto _RemovedVolume = _ShapeVolume(RemovedSolid_);
        if (_RemovedVolume <= Options_.Tolerance * Options_.Tolerance * Options_.Tolerance)
        {
            return std::nullopt;
        }
        BRepAlgoAPI_Cut _Missing(RemovedSolid_, _InsideShape);
        _Missing.SetFuzzyValue(Options_.Tolerance);
        _Missing.Build();
        BRepAlgoAPI_Cut _Excess(_InsideShape, RemovedSolid_);
        _Excess.SetFuzzyValue(Options_.Tolerance);
        _Excess.Build();
        if (!_Missing.IsDone() || !_Excess.IsDone())
        {
            return std::nullopt;
        }
        const auto _RelativeError = (
            _ShapeVolume(_Missing.Shape()) + _ShapeVolume(_Excess.Shape())) / _RemovedVolume;
        if (_RelativeError > 1.0e-4)
        {
            return std::nullopt;
        }
        const auto _Extent = _AnalyzeRemovalExtent(
            RemovedSolid_,
            MainBase_,
            _Axis,
            Options_);
        const auto _Relations = _MakeRemovalRelations(
            _Extent,
            "base",
            0.0,
            _Length);

        const auto _Frame = _MakeSectionFrame(_Axis, _Range->first, _Range->second);
        const auto _Center2 = _ToPoint2(_StartCenter, _Frame);
        Tube::STaperedRegionNode _Tapered;
        _Tapered.Section = _MakeCircularRegion(_Center2, _FirstRadius);
        _Tapered.Frame = _MakeNeutralPlacement(_Frame);
        _Tapered.First = 0.0;
        _Tapered.Last = _Length;
        _Tapered.Reference = 0.0;
        _Tapered.ScaleCenter = _Center2;
        _Tapered.FirstScale = 1.0;
        _Tapered.LastScale = _LastRadius / _FirstRadius;

        SRecognizedAlternative _Result;
        _Result.ShapeInsideBase = _InsideShape;
        _Result.RelativeFitError = _RelativeError;

        Tube::SSolidNode _TaperedNode;
        _TaperedNode.ID = strNodeID_ + "/tapered";
        _TaperedNode.Label = "Recognized true tapered penetration candidate";
        _TaperedNode.Data = _Tapered;
        _TaperedNode.Relations = _Relations;
        _TaperedNode.RemovalSemantics = _Extent.Semantics;
        _TaperedNode.Metadata["source"] = _ProjectiveCone
            ? "brep.tangent-plane-common-apex"
            : "occ.conical-surface-accelerator";
        _TaperedNode.Metadata["tangentPlaneEvidenceCoverage"] = _ToString(
            _ProjectiveCone ? _ProjectiveCone->EvidenceCoverage : 1.0);
        _TaperedNode.Metadata["fitError"] = _ToString(_RelativeError);
        _Result.Nodes.push_back(std::move(_TaperedNode));

        const auto _NominalRadius = std::min(_FirstRadius, _LastRadius);
        BRepPrimAPI_MakeCylinder _NominalCylinderBuilder(
            gp_Ax2(_StartCenter, _Axis, _XDirection),
            _NominalRadius,
            _Length);
        _NominalCylinderBuilder.Build();
        if (!_NominalCylinderBuilder.IsDone())
        {
            return std::nullopt;
        }
        const auto _NominalCylinder = _NominalCylinderBuilder.Shape();
        BRepAlgoAPI_Cut _BevelExtra(_FullCone, _NominalCylinder);
        _BevelExtra.SetFuzzyValue(Options_.Tolerance);
        _BevelExtra.Build();

        Tube::SExtrudedRegionNode _Nominal;
        _Nominal.Section = _MakeCircularRegion(_Center2, _NominalRadius);
        _Nominal.Frame = _MakeNeutralPlacement(_Frame);
        _Nominal.First = 0.0;
        _Nominal.Last = _Length;
        Tube::SSolidNode _NominalNode;
        _NominalNode.ID = strNodeID_ + "/nominal-extrusion";
        _NominalNode.Label = "Nominal straight penetration candidate";
        _NominalNode.Data = std::move(_Nominal);
        _NominalNode.Relations = _Relations;
        _NominalNode.RemovalSemantics = _Extent.Semantics;
        _NominalNode.Metadata["source"] = "occ.conical-alternative";
        _Result.Nodes.push_back(std::move(_NominalNode));

        Tube::SBooleanNode _ExtraDifference;
        _ExtraDifference.Operation = Tube::EBooleanOperation::Difference;
        _ExtraDifference.Children = {
            strNodeID_ + "/tapered",
            strNodeID_ + "/nominal-extrusion"
        };
        Tube::SSolidNode _ExtraDifferenceNode;
        _ExtraDifferenceNode.ID = strNodeID_ + "/bevel-extra-volume";
        _ExtraDifferenceNode.Label = "Evaluated additional bevel volume";
        _ExtraDifferenceNode.Data = std::move(_ExtraDifference);
        _Result.Nodes.push_back(std::move(_ExtraDifferenceNode));

        auto _Bevel = _MakeConstantOpeningProfileSweep(
            strNodeID_ + "/nominal-extrusion",
            strNodeID_ + "/bevel-extra-volume",
            _Relations,
            0.5 * (_Range->first + _Range->second),
            0.0,
            _Length,
            _FirstRadius - _NominalRadius,
            _LastRadius - _NominalRadius);
        _Bevel.ProfileField.Metadata["derivedSemiAngleRadians"] =
            _ToString(std::abs(_SemiAngle));
        Tube::SSolidNode _BevelNode;
        _BevelNode.ID = strNodeID_ + "/bevel";
        _BevelNode.Label = "Full-depth zero-land bevel candidate";
        _BevelNode.Data = std::move(_Bevel);
        _BevelNode.Metadata["source"] = "occ.conical-alternative";
        _Result.Nodes.push_back(std::move(_BevelNode));

        Tube::SBooleanNode _BeveledUnion;
        _BeveledUnion.Operation = Tube::EBooleanOperation::Union;
        _BeveledUnion.Children = {
            strNodeID_ + "/nominal-extrusion",
            strNodeID_ + "/bevel"
        };
        Tube::SSolidNode _BeveledUnionNode;
        _BeveledUnionNode.ID = strNodeID_ + "/beveled-penetration";
        _BeveledUnionNode.Label = "Straight penetration with bevel candidate";
        _BeveledUnionNode.Data = std::move(_BeveledUnion);
        _BeveledUnionNode.Relations = _Relations;
        _BeveledUnionNode.RemovalSemantics = _Extent.Semantics;
        _Result.Nodes.push_back(std::move(_BeveledUnionNode));

        Tube::SAlternativeNode _Alternative;
        Tube::SInterpretationCandidate _TrueTaper;
        _TrueTaper.ID = "true-tapered-penetration";
        _TrueTaper.Label = "True tapered penetration";
        _TrueTaper.CandidateNodeID = strNodeID_ + "/tapered";
        _TrueTaper.Confidence = 0.82;
        _TrueTaper.RelativeGeometryError = _RelativeError;
        _TrueTaper.Evaluation = _EvaluateInterpretation(
            _RelativeError,
            0.95,
            0.92,
            0.22,
            0.05,
            0.86,
            0.91);
        _TrueTaper.Metadata["semantic"] = "true-tapered-penetration";
        _TrueTaper.Evidence.push_back({
            "conical-surface",
            "The removal contains a conical surface and is equivalent to a tapered cutter",
            0.82,
            {}
        });
        _Alternative.Candidates.push_back(std::move(_TrueTaper));

        Tube::SInterpretationCandidate _Beveled;
        _Beveled.ID = "straight-penetration-with-bevel";
        _Beveled.Label = "Straight penetration with full-depth bevel";
        _Beveled.CandidateNodeID = strNodeID_ + "/beveled-penetration";
        _Beveled.Confidence = 0.62;
        _Beveled.RelativeGeometryError = _RelativeError;
        _Beveled.Evaluation = _EvaluateInterpretation(
            _RelativeError,
            0.95,
            0.68,
            0.48,
            0.28,
            0.73,
            0.82);
        _Beveled.Metadata["semantic"] = "straight-penetration-with-bevel";
        _Beveled.Evidence.push_back({
            "geometry-equivalent-bevel",
            "A zero-land full-depth bevel is geometrically equivalent to the tapered cutter",
            0.62,
            {}
        });
        _Alternative.Candidates.push_back(std::move(_Beveled));
        _Alternative.RecommendedCandidateID = "true-tapered-penetration";

        Tube::SSolidNode _AlternativeNode;
        _AlternativeNode.ID = strNodeID_;
        _AlternativeNode.Label = "Unresolved conical removal interpretation";
        _AlternativeNode.Data = std::move(_Alternative);
        _AlternativeNode.Relations = _Relations;
        _AlternativeNode.RemovalSemantics = _Extent.Semantics;
        _AlternativeNode.Metadata["source"] = "occ.semantic-alternatives";
        _Result.Nodes.push_back(std::move(_AlternativeNode));
        _Result.RootNodeID = strNodeID_;
        _Result.PreviewShapes[strNodeID_ + "/tapered"] = _FullCone;
        _Result.PreviewShapes[strNodeID_ + "/nominal-extrusion"] = _NominalCylinder;
        _Result.PreviewShapes[strNodeID_ + "/beveled-penetration"] = _FullCone;
        _Result.PreviewShapes[strNodeID_] = _FullCone;
        if (_BevelExtra.IsDone() && !_BevelExtra.Shape().IsNull())
        {
            _Result.PreviewShapes[strNodeID_ + "/bevel-extra-volume"] =
                _BevelExtra.Shape();
            _Result.PreviewShapes[strNodeID_ + "/bevel"] = _BevelExtra.Shape();
        }
        return _Result;
    }

    std::optional<SRecognizedAlternative> _TryRecognizeBeveledCircularRemoval(
        IN const TopoDS_Shape& RemovedSolid_,
        IN const TopoDS_Shape& MainBase_,
        IN const STubeCSGConversionOptions& Options_,
        IN const std::string& strNodeID_)
    {
        std::optional<std::pair<gp_Cone, TopoDS_Face>> _ConeFace;
        double _ConeArea = 0.0;
        for (TopExp_Explorer _Explorer(RemovedSolid_, TopAbs_FACE);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Face = TopoDS::Face(_Explorer.Current());
            BRepAdaptor_Surface _Surface(_Face, true);
            if (_Surface.GetType() != GeomAbs_Cone)
            {
                continue;
            }
            const auto _Area = _FaceArea(_Face);
            if (!_ConeFace || _Area > _ConeArea)
            {
                _ConeFace = std::make_pair(_Surface.Cone(), _Face);
                _ConeArea = _Area;
            }
        }
        if (!_ConeFace)
        {
            return std::nullopt;
        }

        const auto& _Cone = _ConeFace->first;
        const auto _Axis = _Cone.Axis().Direction();
        std::optional<std::pair<gp_Cylinder, double>> _Cylinder;
        for (TopExp_Explorer _Explorer(RemovedSolid_, TopAbs_FACE);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Face = TopoDS::Face(_Explorer.Current());
            BRepAdaptor_Surface _Surface(_Face, true);
            if (_Surface.GetType() != GeomAbs_Cylinder)
            {
                continue;
            }
            const auto _Candidate = _Surface.Cylinder();
            if (_AbsDot(_Candidate.Axis().Direction(), _Axis) < 1.0 - 1.0e-6)
            {
                continue;
            }
            const gp_Vec _AxisOffset(_Cone.Location(), _Candidate.Location());
            const auto _PerpendicularOffset = _AxisOffset.Crossed(gp_Vec(_Axis)).Magnitude();
            if (_PerpendicularOffset > Options_.Tolerance)
            {
                continue;
            }
            const auto _Area = _FaceArea(_Face);
            if (!_Cylinder || _Area > _Cylinder->second)
            {
                _Cylinder = std::make_pair(_Candidate, _Area);
            }
        }
        if (!_Cylinder)
        {
            return std::nullopt;
        }

        const auto _FullRange = _ProjectAxialRange(RemovedSolid_, _Axis);
        const auto _ConeRange = _ProjectAxialRange(_ConeFace->second, _Axis);
        if (!_FullRange || !_ConeRange)
        {
            return std::nullopt;
        }
        const auto _FullLength = _FullRange->second - _FullRange->first;
        const auto _ConeLength = _ConeRange->second - _ConeRange->first;
        if (_FullLength <= Options_.Tolerance || _ConeLength <= Options_.Tolerance)
        {
            return std::nullopt;
        }

        const auto _CylinderStation = gp_Vec(
            gp_Pnt(0.0, 0.0, 0.0),
            _Cylinder->first.Location()).Dot(gp_Vec(_Axis));
        const auto _NominalStart = _Cylinder->first.Location().Translated(
            gp_Vec(_Axis) * (_FullRange->first - _CylinderStation));
        BRepPrimAPI_MakeCylinder _CylinderBuilder(
            gp_Ax2(_NominalStart, _Axis, _Cylinder->first.Position().XDirection()),
            _Cylinder->first.Radius(),
            _FullLength);
        _CylinderBuilder.Build();
        if (!_CylinderBuilder.IsDone())
        {
            return std::nullopt;
        }

        const auto _ConeReferenceStation = gp_Vec(
            gp_Pnt(0.0, 0.0, 0.0),
            _Cone.Location()).Dot(gp_Vec(_Axis));
        const auto _ConeFirstOffset = _ConeRange->first - _ConeReferenceStation;
        const auto _ConeLastOffset = _ConeRange->second - _ConeReferenceStation;
        const auto _Slope = std::tan(_Cone.SemiAngle());
        const auto _ConeFirstRadius = _Cone.RefRadius() + _ConeFirstOffset * _Slope;
        const auto _ConeLastRadius = _Cone.RefRadius() + _ConeLastOffset * _Slope;
        if (_ConeFirstRadius <= Options_.Tolerance || _ConeLastRadius <= Options_.Tolerance)
        {
            return std::nullopt;
        }
        const auto _ConeStart = _Cone.Location().Translated(gp_Vec(_Axis) * _ConeFirstOffset);
        BRepPrimAPI_MakeCone _ConeBuilder(
            gp_Ax2(_ConeStart, _Axis, _Cone.Position().XDirection()),
            _ConeFirstRadius,
            _ConeLastRadius,
            _ConeLength);
        _ConeBuilder.Build();
        if (!_ConeBuilder.IsDone())
        {
            return std::nullopt;
        }

        BRepAlgoAPI_Fuse _EnvelopeFuse(_CylinderBuilder.Shape(), _ConeBuilder.Shape());
        _EnvelopeFuse.SetFuzzyValue(Options_.Tolerance);
        _EnvelopeFuse.Build();
        if (!_EnvelopeFuse.IsDone())
        {
            return std::nullopt;
        }
        BRepAlgoAPI_Cut _BevelExtra(
            _EnvelopeFuse.Shape(),
            _CylinderBuilder.Shape());
        _BevelExtra.SetFuzzyValue(Options_.Tolerance);
        _BevelExtra.Build();
        BRepAlgoAPI_Common _InsideBase(_EnvelopeFuse.Shape(), MainBase_);
        _InsideBase.SetFuzzyValue(Options_.Tolerance);
        _InsideBase.Build();
        if (!_InsideBase.IsDone())
        {
            return std::nullopt;
        }
        const auto _InsideShape = _InsideBase.Shape();
        const auto _RemovedVolume = _ShapeVolume(RemovedSolid_);
        BRepAlgoAPI_Cut _Missing(RemovedSolid_, _InsideShape);
        _Missing.SetFuzzyValue(Options_.Tolerance);
        _Missing.Build();
        BRepAlgoAPI_Cut _Excess(_InsideShape, RemovedSolid_);
        _Excess.SetFuzzyValue(Options_.Tolerance);
        _Excess.Build();
        if (_RemovedVolume <= Options_.Tolerance * Options_.Tolerance * Options_.Tolerance
            || !_Missing.IsDone()
            || !_Excess.IsDone())
        {
            return std::nullopt;
        }
        const auto _RelativeError = (
            _ShapeVolume(_Missing.Shape()) + _ShapeVolume(_Excess.Shape())) / _RemovedVolume;
        if (_RelativeError > 1.0e-4)
        {
            return std::nullopt;
        }
        const auto _Extent = _AnalyzeRemovalExtent(
            RemovedSolid_,
            MainBase_,
            _Axis,
            Options_);
        const auto _Relations = _MakeRemovalRelations(
            _Extent,
            "base",
            0.0,
            _FullLength);

        SRecognizedAlternative _Result;
        _Result.RootNodeID = strNodeID_;
        _Result.ShapeInsideBase = _InsideShape;
        _Result.RelativeFitError = _RelativeError;

        const auto _NominalFrame = _MakeSectionFrame(
            _Axis,
            _FullRange->first,
            _FullRange->second);
        const auto _NominalCenter2 = _ToPoint2(_NominalStart, _NominalFrame);
        Tube::SExtrudedRegionNode _Nominal;
        _Nominal.Section = _MakeCircularRegion(_NominalCenter2, _Cylinder->first.Radius());
        _Nominal.Frame = _MakeNeutralPlacement(_NominalFrame);
        _Nominal.First = 0.0;
        _Nominal.Last = _FullLength;
        Tube::SSolidNode _NominalNode;
        _NominalNode.ID = strNodeID_ + "/nominal-extrusion";
        _NominalNode.Label = "Recognized nominal straight penetration";
        _NominalNode.Data = std::move(_Nominal);
        _NominalNode.Relations = _Relations;
        _NominalNode.RemovalSemantics = _Extent.Semantics;
        _NominalNode.Metadata["source"] = "occ.cylindrical-land";
        _Result.Nodes.push_back(std::move(_NominalNode));

        const auto _TaperFrame = _MakeSectionFrame(
            _Axis,
            _ConeRange->first,
            _ConeRange->second);
        const auto _TaperCenter2 = _ToPoint2(_ConeStart, _TaperFrame);
        Tube::STaperedRegionNode _Tapered;
        _Tapered.Section = _MakeCircularRegion(_TaperCenter2, _ConeFirstRadius);
        _Tapered.Frame = _MakeNeutralPlacement(_TaperFrame);
        _Tapered.First = 0.0;
        _Tapered.Last = _ConeLength;
        _Tapered.Reference = 0.0;
        _Tapered.ScaleCenter = _TaperCenter2;
        _Tapered.FirstScale = 1.0;
        _Tapered.LastScale = _ConeLastRadius / _ConeFirstRadius;
        Tube::SSolidNode _TaperedNode;
        _TaperedNode.ID = strNodeID_ + "/bevel-envelope";
        _TaperedNode.Label = "Recognized bevel envelope";
        _TaperedNode.Data = std::move(_Tapered);
        _TaperedNode.Metadata["source"] = "occ.conical-bevel-band";
        _Result.Nodes.push_back(std::move(_TaperedNode));

        Tube::SBooleanNode _ExtraDifference;
        _ExtraDifference.Operation = Tube::EBooleanOperation::Difference;
        _ExtraDifference.Children = {
            strNodeID_ + "/bevel-envelope",
            strNodeID_ + "/nominal-extrusion"
        };
        Tube::SSolidNode _ExtraNode;
        _ExtraNode.ID = strNodeID_ + "/bevel-extra-volume";
        _ExtraNode.Label = "Recognized additional bevel removal";
        _ExtraNode.Data = std::move(_ExtraDifference);
        _Result.Nodes.push_back(std::move(_ExtraNode));

        auto _Bevel = _MakeConstantOpeningProfileSweep(
            strNodeID_ + "/nominal-extrusion",
            strNodeID_ + "/bevel-extra-volume",
            _Relations,
            0.5 * (_ConeRange->first + _ConeRange->second),
            _ConeRange->first - _FullRange->first,
            _ConeRange->second - _FullRange->first,
            _ConeFirstRadius - _Cylinder->first.Radius(),
            _ConeLastRadius - _Cylinder->first.Radius());
        _Bevel.ProfileField.Metadata["derivedSemiAngleRadians"] =
            _ToString(std::abs(_Cone.SemiAngle()));
        _Bevel.ProfileField.Metadata["derivedLand"] =
            _ToString(std::max(0.0, _FullLength - _ConeLength));
        Tube::SSolidNode _BevelNode;
        _BevelNode.ID = strNodeID_ + "/bevel";
        _BevelNode.Label = "Recognized bevel modifier";
        _BevelNode.Data = std::move(_Bevel);
        _BevelNode.Metadata["source"] = "occ.cylinder-plus-cone";
        _Result.Nodes.push_back(std::move(_BevelNode));

        Tube::SBooleanNode _RemovalUnion;
        _RemovalUnion.Operation = Tube::EBooleanOperation::Union;
        _RemovalUnion.Children = {
            strNodeID_ + "/nominal-extrusion",
            strNodeID_ + "/bevel"
        };
        Tube::SSolidNode _RootNode;
        _RootNode.ID = strNodeID_;
        _RootNode.Label = "Recognized straight penetration with bevel";
        _RootNode.Data = std::move(_RemovalUnion);
        _RootNode.Relations = _Relations;
        _RootNode.RemovalSemantics = _Extent.Semantics;
        _RootNode.Metadata["source"] = "occ.explicit-bevel";
        _Result.Nodes.push_back(std::move(_RootNode));
        _Result.PreviewShapes[strNodeID_ + "/nominal-extrusion"] =
            _CylinderBuilder.Shape();
        _Result.PreviewShapes[strNodeID_ + "/bevel-envelope"] =
            _ConeBuilder.Shape();
        _Result.PreviewShapes[strNodeID_] = _EnvelopeFuse.Shape();
        if (_BevelExtra.IsDone() && !_BevelExtra.Shape().IsNull())
        {
            _Result.PreviewShapes[strNodeID_ + "/bevel-extra-volume"] =
                _BevelExtra.Shape();
            _Result.PreviewShapes[strNodeID_ + "/bevel"] = _BevelExtra.Shape();
        }
        return _Result;
    }

    std::optional<SRecognizedExtrusion> _TryRecognizeRemovalExtrusion(
        IN const TopoDS_Shape& RemovedSolid_,
        IN const TopoDS_Shape& MainBase_,
        IN const SAxisCandidate& Axis_,
        IN const gp_Dir& BaseAxis_,
        IN const STubeCSGConversionOptions& Options_,
        IN const std::string& strNodeID_,
        IN bool bRequireCompleteMatch_ = true)
    {
        const auto _RemovedRange = _ProjectAxialRange(RemovedSolid_, Axis_.Direction);
        if (!_RemovedRange)
        {
            return std::nullopt;
        }
        const auto _SourceExtent = _AnalyzeRemovalExtent(
            RemovedSolid_,
            MainBase_,
            Axis_.Direction,
            Options_);

        std::optional<Tube::SExtrudedRegionNode> _NeutralExtrusion;
        std::optional<TopoDS_Shape> _FullShape;
        auto _ConstructionRange = *_RemovedRange;

        std::optional<std::pair<gp_Cylinder, double>> _Cylinder;
        TopoDS_Face _CylinderFace;
        for (TopExp_Explorer _Explorer(RemovedSolid_, TopAbs_FACE);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Face = TopoDS::Face(_Explorer.Current());
            BRepAdaptor_Surface _Surface(_Face, true);
            if (_Surface.GetType() != GeomAbs_Cylinder)
            {
                continue;
            }
            const auto _Candidate = _Surface.Cylinder();
            if (_AbsDot(_Candidate.Axis().Direction(), Axis_.Direction) < 1.0 - 1.0e-6)
            {
                continue;
            }
            const auto _Area = _FaceArea(_Face);
            if (!_Cylinder || _Area > _Cylinder->second)
            {
                _Cylinder = std::make_pair(_Candidate, _Area);
                _CylinderFace = _Face;
            }
        }

        if (_Cylinder)
        {
            if (!bRequireCompleteMatch_ && !_CylinderFace.IsNull())
            {
                if (const auto _VisibleCylinderRange = _ProjectAxialRange(
                    _CylinderFace,
                    Axis_.Direction))
                {
                    _ConstructionRange = *_VisibleCylinderRange;
                }
            }
            const auto _Pad = std::max(
                2.0 * _Cylinder->first.Radius(),
                10.0 * Options_.Tolerance);
            const auto _HasFirstTermination = std::any_of(
                _SourceExtent.InternalTerminationStations.begin(),
                _SourceExtent.InternalTerminationStations.end(),
                [&_ConstructionRange, &Options_](IN double dStation_) {
                    return std::abs(dStation_ - _ConstructionRange.first)
                        <= 10.0 * Options_.Tolerance;
                });
            const auto _HasLastTermination = std::any_of(
                _SourceExtent.InternalTerminationStations.begin(),
                _SourceExtent.InternalTerminationStations.end(),
                [&_ConstructionRange, &Options_](IN double dStation_) {
                    return std::abs(dStation_ - _ConstructionRange.second)
                        <= 10.0 * Options_.Tolerance;
                });
            const auto _First = _ConstructionRange.first - (_HasFirstTermination ? 0.0 : _Pad);
            const auto _Last = _ConstructionRange.second + (_HasLastTermination ? 0.0 : _Pad);
            const auto _EvaluationFrame = _MakeSectionFrame(
                Axis_.Direction,
                _First,
                _Last);
            const auto _AxisStation = gp_Vec(
                gp_Pnt(0.0, 0.0, 0.0),
                _Cylinder->first.Location()).Dot(gp_Vec(Axis_.Direction));
            const auto _StartCenter = _Cylinder->first.Location().Translated(
                gp_Vec(Axis_.Direction) * (_First - _AxisStation));
            const auto _Length = _Last - _First;
            BRepPrimAPI_MakeCylinder _CylinderBuilder(
                gp_Ax2(
                    _StartCenter,
                    Axis_.Direction,
                    _Cylinder->first.Position().XDirection()),
                _Cylinder->first.Radius(),
                _Length);
            _CylinderBuilder.Build();
            if (_CylinderBuilder.IsDone())
            {
                Tube::SExtrudedRegionNode _Extrusion;
                _Extrusion.Section = _MakeCircularRegion(
                    _ToPoint2(_StartCenter, _EvaluationFrame),
                    _Cylinder->first.Radius());
                _Extrusion.Frame = _MakeNeutralPlacement(_EvaluationFrame);
                _Extrusion.First = 0.0;
                _Extrusion.Last = _Length;
                _NeutralExtrusion = std::move(_Extrusion);
                _FullShape = _CylinderBuilder.Shape();
            }
        }

        if (!_NeutralExtrusion || !_FullShape)
        {
            const auto _RecognitionFrame = _MakeSectionFrame(
                Axis_.Direction,
                _RemovedRange->first,
                _RemovedRange->second);
            const auto _Section = _RecognizeSection(
                RemovedSolid_,
                _RecognitionFrame,
                Options_,
                true);
            if (!_Section)
            {
                return std::nullopt;
            }
            _FullShape = _MakeBaseShape(*_Section, _RecognitionFrame);
            if (!_FullShape)
            {
                return std::nullopt;
            }
            _NeutralExtrusion = _MakeExtrudedRegionNodeData(
                *_Section, _RecognitionFrame, strNodeID_);
        }
        if (!_NeutralExtrusion || !_FullShape)
        {
            return std::nullopt;
        }

        BRepAlgoAPI_Common _InsideBase(*_FullShape, MainBase_);
        _InsideBase.SetFuzzyValue(Options_.Tolerance);
        _InsideBase.Build();
        if (!_InsideBase.IsDone())
        {
            return std::nullopt;
        }
        const auto _InsideShape = _InsideBase.Shape();
        const auto _RemovedVolume = _ShapeVolume(RemovedSolid_);
        if (_RemovedVolume <= Options_.Tolerance * Options_.Tolerance * Options_.Tolerance)
        {
            return std::nullopt;
        }

        const auto _InsideVolume = _ShapeVolume(_InsideShape);
        const auto _VolumeOnlyError = std::abs(_InsideVolume - _RemovedVolume)
            / _RemovedVolume;
        const auto _TangentEvidenceCoverage = Axis_.TotalArea > 0.0
            ? Axis_.CompatibleArea / Axis_.TotalArea
            : 0.0;
        const auto _FitTolerance = Axis_.FromTangentPlaneConsensus
                && _TangentEvidenceCoverage >= 0.95
            ? 1.0e-2
            : 1.0e-4;
        /*
        * 完整匹配时，截面直接来自缺失体，方向已由生成侧壁切平面验证；再次对
        * NURBS 缺失体和重建棱柱做体布尔代价很高。体积一致性作为最后的独立
        * 硬约束。子集分解没有这项保证，仍执行包含性采样。
        */
        const auto _CandidateInsideRemoval = bRequireCompleteMatch_
            ? _VolumeOnlyError <= _FitTolerance
            : _AreShapeSamplesInsideOrOn(
                _InsideShape,
                RemovedSolid_,
                Options_.Tolerance);
        const auto _RemovalInsideCandidate = bRequireCompleteMatch_
            ? _VolumeOnlyError <= _FitTolerance
            : false;
        const auto _OverlapVolume = _CandidateInsideRemoval
            ? std::min(_InsideVolume, _RemovedVolume)
            : 0.0;
        const auto _MissingVolume = std::max(0.0, _RemovedVolume - _OverlapVolume);
        const auto _ExcessVolume = _CandidateInsideRemoval
            ? std::max(0.0, _InsideVolume - _RemovedVolume)
            : _InsideVolume;
        const auto _RelativeError = (_MissingVolume + _ExcessVolume) / _RemovedVolume;
        const auto _CoverageRatio = _Clamp(
            _OverlapVolume / _RemovedVolume,
            0.0,
            1.0);
        if ((bRequireCompleteMatch_
                && (!_CandidateInsideRemoval
                    || !_RemovalInsideCandidate
                    || _RelativeError > _FitTolerance))
            || (!bRequireCompleteMatch_
                && (!_CandidateInsideRemoval
                    || _InsideVolume <= Options_.Tolerance * Options_.Tolerance * Options_.Tolerance
                    || _ExcessVolume / std::max(_InsideVolume, _RemovedVolume) > 1.0e-4)))
        {
            return std::nullopt;
        }

        const auto _Extent = _AnalyzeRemovalExtent(
            _InsideShape,
            MainBase_,
            Axis_.Direction,
            Options_);

        SRecognizedExtrusion _Result;
        _Result.FullShape = *_FullShape;
        _Result.ShapeInsideBase = _InsideShape;
        _Result.RelativeFitError = bRequireCompleteMatch_
            ? _RelativeError
            : _ExcessVolume / std::max(_InsideVolume, _RemovedVolume);
        _Result.CoverageRatio = _CoverageRatio;
        _Result.GeometryAcceptanceTolerance = _FitTolerance;
        _Result.Node.ID = strNodeID_;
        _Result.Node.Label = "Recognized extruded removal";
        _Result.Node.Data = std::move(*_NeutralExtrusion);
        auto& _ExtrusionData = std::get<Tube::SExtrudedRegionNode>(_Result.Node.Data);
        if (_ExtrusionData.SectionPrimitives.empty())
        {
            _ExtrusionData.SectionPrimitives = _MakeSectionPrimitives(
                _ExtrusionData.Section, strNodeID_);
        }
        _Result.Node.Relations = _MakeRemovalRelations(
            _Extent,
            "base",
            _ExtrusionData.First,
            _ExtrusionData.Last);
        _Result.Node.RemovalSemantics = _Extent.Semantics;
        _Result.Node.Metadata["source"] = "occ.removal-extrusion";
        _Result.Node.Metadata["axisRecognition"] = Axis_.FromTangentPlaneConsensus
            ? "tangent-plane-common-direction"
            : "analytic-accelerator";
        _Result.Node.Metadata["fitError"] = _ToString(_RelativeError);
        _Result.Node.Metadata["sourceCoverage"] = _ToString(_CoverageRatio);
        if (!bRequireCompleteMatch_)
        {
            _Result.Node.Metadata["recognitionMode"] = "global-subset";
        }
        const auto _AxisDot = _AbsDot(Axis_.Direction, BaseAxis_);
        _Result.Node.Metadata["featureSemantic"] = _AxisDot <= 1.0e-6
            ? "perpendicular-penetration"
            : (_AxisDot >= 1.0 - 1.0e-6
                ? "parallel-penetration"
                : "oblique-penetration");
        _Result.Node.Metadata["baseAxisDot"] = _ToString(_AxisDot);
        return _Result;
    }

    std::optional<SGlobalFeatureRecognition> _TryRecognizeGlobalCylinderGroup(
        IN const SCylinderEvidenceGroup& Group_,
        IN const SPartSurfaceEvidenceGraph& Graph_,
        IN const TopoDS_Shape& MainBase_,
        IN const TopoDS_Shape& FullRemovalShape_,
        IN const gp_Dir& BaseAxis_,
        IN const STubeCSGConversionOptions& Options_,
        IN const std::string& strNodeID_,
        OUT std::string* pFailureReason_ = nullptr)
    {
        const auto _SetFailure = [pFailureReason_](IN const std::string& strReason_) {
            if (pFailureReason_)
            {
                *pFailureReason_ = strReason_;
            }
        };
        if (Group_.LateralFaces.empty() || Group_.AdjacentToCompatibleCone)
        {
            _SetFailure(Group_.AdjacentToCompatibleCone
                ? "adjacent-compatible-cone"
                : "no-lateral-faces");
            return std::nullopt;
        }
        const auto _Axis = _CanonicalDirection(Group_.Cylinder.Axis().Direction());
        auto _VisibleFirst = (std::numeric_limits<double>::max)();
        auto _VisibleLast = (std::numeric_limits<double>::lowest)();
        for (const auto* _pEvidence : Group_.LateralFaces)
        {
            const auto _Range = _ProjectAxialRange(_pEvidence->Face, _Axis);
            if (!_Range)
            {
                continue;
            }
            _VisibleFirst = std::min(_VisibleFirst, _Range->first);
            _VisibleLast = std::max(_VisibleLast, _Range->second);
        }
        if (_VisibleLast - _VisibleFirst <= Options_.Tolerance)
        {
            _SetFailure("invalid-visible-range");
            return std::nullopt;
        }

        std::vector<std::pair<double, std::uint64_t>> _TerminationStations;
        for (const auto* _pEvidence : Group_.LateralFaces)
        {
            for (const auto _AdjacentID : _pEvidence->AdjacentFaceIDs)
            {
                if (_AdjacentID == 0 || _AdjacentID > Graph_.Faces.size())
                {
                    continue;
                }
                const auto& _Adjacent = Graph_.Faces[
                    static_cast<std::size_t>(_AdjacentID - 1)];
                if (_Adjacent.InheritedFromHost
                    || _Adjacent.SurfaceType != GeomAbs_Plane)
                {
                    continue;
                }
                BRepAdaptor_Surface _Surface(_Adjacent.Face, true);
                const auto _Plane = _Surface.Plane();
                if (_AbsDot(_Plane.Axis().Direction(), _Axis)
                    < 1.0 - Options_.AngularToleranceRadians)
                {
                    continue;
                }
                const auto _Station = gp_Vec(
                    gp_Pnt(0.0, 0.0, 0.0),
                    _Plane.Location()).Dot(gp_Vec(_Axis));
                _TerminationStations.emplace_back(_Station, _Adjacent.SourceShapeID);
            }
        }
        const auto _HasTerminationAt = [
            &_TerminationStations,
            &Options_](IN double dStation_) {
            return std::any_of(
                _TerminationStations.begin(),
                _TerminationStations.end(),
                [dStation_, &Options_](IN const auto& Termination_) {
                    return std::abs(Termination_.first - dStation_)
                        <= 10.0 * Options_.Tolerance;
                });
        };
        const auto _Pad = std::max(
            2.0 * Group_.Cylinder.Radius(),
            10.0 * Options_.Tolerance);
        const auto _First = _VisibleFirst
            - (_HasTerminationAt(_VisibleFirst) ? 0.0 : _Pad);
        const auto _Last = _VisibleLast
            + (_HasTerminationAt(_VisibleLast) ? 0.0 : _Pad);
        const auto _AxisStation = gp_Vec(
            gp_Pnt(0.0, 0.0, 0.0),
            Group_.Cylinder.Location()).Dot(gp_Vec(_Axis));
        const auto _StartCenter = Group_.Cylinder.Location().Translated(
            gp_Vec(_Axis) * (_First - _AxisStation));
        BRepPrimAPI_MakeCylinder _CylinderBuilder(
            gp_Ax2(
                _StartCenter,
                _Axis,
                Group_.Cylinder.Position().XDirection()),
            Group_.Cylinder.Radius(),
            _Last - _First);
        _CylinderBuilder.Build();
        if (!_CylinderBuilder.IsDone())
        {
            _SetFailure("cylinder-evaluation-failed");
            return std::nullopt;
        }
        BRepAlgoAPI_Common _InsideBase(_CylinderBuilder.Shape(), MainBase_);
        _InsideBase.SetFuzzyValue(Options_.Tolerance);
        _InsideBase.Build();
        if (!_InsideBase.IsDone())
        {
            _SetFailure("base-intersection-failed");
            return std::nullopt;
        }
        const auto _InsideShape = _InsideBase.Shape();
        const auto _InsideVolume = _ShapeVolume(_InsideShape);
        if (_InsideVolume <= Options_.Tolerance * Options_.Tolerance * Options_.Tolerance)
        {
            _SetFailure(
                "empty-base-intersection:first=" + _ToString(_First)
                + ",last=" + _ToString(_Last)
                + ",start=" + _ToString(_StartCenter.X()) + ","
                + _ToString(_StartCenter.Y()) + ","
                + _ToString(_StartCenter.Z())
                + ",baseVolume=" + _ToString(_ShapeVolume(MainBase_))
                + ",cutterVolume=" + _ToString(_ShapeVolume(_CylinderBuilder.Shape())));
            return std::nullopt;
        }
        BRepAlgoAPI_Cut _Excess(_InsideShape, FullRemovalShape_);
        _Excess.SetFuzzyValue(Options_.Tolerance);
        _Excess.Build();
        if (!_Excess.IsDone()
            || _ShapeVolume(_Excess.Shape()) / _InsideVolume > 1.0e-4)
        {
            _SetFailure(!_Excess.IsDone()
                ? "excess-validation-failed"
                : "candidate-exceeds-removed-volume:"
                    + _ToString(_ShapeVolume(_Excess.Shape()) / _InsideVolume));
            return std::nullopt;
        }

        const auto _Extent = _AnalyzeRemovalExtent(
            _InsideShape,
            MainBase_,
            _Axis,
            Options_);
        const auto _Frame = _MakeSectionFrame(_Axis, _First, _Last);
        Tube::SExtrudedRegionNode _Extrusion;
        _Extrusion.Section = _MakeCircularRegion(
            _ToPoint2(_StartCenter, _Frame),
            Group_.Cylinder.Radius());
        _Extrusion.Frame = _MakeNeutralPlacement(_Frame);
        _Extrusion.First = 0.0;
        _Extrusion.Last = _Last - _First;

        Tube::SSolidNode _SingleGeneratorNode;
        _SingleGeneratorNode.ID = strNodeID_ + "/single-generator";
        _SingleGeneratorNode.Label = "Part-surface grouped cylindrical penetration";
        _SingleGeneratorNode.Data = std::move(_Extrusion);
        _SingleGeneratorNode.Relations = _MakeRemovalRelations(
            _Extent,
            "base",
            0.0,
            _Last - _First);
        for (const auto* _pEvidence : Group_.LateralFaces)
        {
            Tube::SSurfaceEvidenceReference _Reference;
            _Reference.SourceResourceID = Options_.SourceBRepResourceID;
            _Reference.SourceShapeID = _pEvidence->SourceShapeID;
            _Reference.Role = Tube::ESurfaceEvidenceRole::GeneratedLateral;
            _Reference.Confidence = 0.98;
            _SingleGeneratorNode.Relations->SurfaceEvidence.push_back(
                std::move(_Reference));
        }
        for (const auto& [_Station, _SourceShapeID] : _TerminationStations)
        {
            Tube::SSurfaceEvidenceReference _Reference;
            _Reference.SourceResourceID = Options_.SourceBRepResourceID;
            _Reference.SourceShapeID = _SourceShapeID;
            _Reference.Role = Tube::ESurfaceEvidenceRole::GeneratedTermination;
            _Reference.Confidence = 0.96;
            _SingleGeneratorNode.Relations->SurfaceEvidence.push_back(
                std::move(_Reference));
        }
        _AppendMaterialSpans(
            _InsideShape,
            MainBase_,
            FullRemovalShape_,
            _Axis,
            Options_,
            *_SingleGeneratorNode.Relations);
        _SingleGeneratorNode.Relations->Metadata["groupingBasis"] =
            "part-surface-common-generator";
        _SingleGeneratorNode.RemovalSemantics = _Extent.Semantics;
        _SingleGeneratorNode.Metadata["source"] = "occ.part-surface-group";
        _SingleGeneratorNode.Metadata["sourceFaceCount"] = std::to_string(
            Group_.LateralFaces.size());
        const auto _BaseAxisDot = _AbsDot(_Axis, BaseAxis_);
        _SingleGeneratorNode.Metadata["featureSemantic"] = _BaseAxisDot <= 1.0e-6
            ? "perpendicular-penetration"
            : (_BaseAxisDot >= 1.0 - 1.0e-6
                ? "parallel-penetration"
                : "oblique-penetration");
        _SingleGeneratorNode.Metadata["baseAxisDot"] = _ToString(_BaseAxisDot);

        SGlobalFeatureRecognition _Result;
        _Result.ShapeInsideBase = _InsideShape;
        _Result.PreviewShape = _CylinderBuilder.Shape();
        _Result.CoveredRemovalComponentOrdinals = _FindCoveredRemovalComponents(
            _InsideShape,
            FullRemovalShape_,
            Options_);
        _SingleGeneratorNode.Metadata["coveredRemovalComponentCount"] = std::to_string(
            _Result.CoveredRemovalComponentOrdinals.size());
        _Result.Nodes.push_back(std::move(_SingleGeneratorNode));
        _Result.RootNodeID = strNodeID_ + "/single-generator";

        if (_Result.CoveredRemovalComponentOrdinals.size() < 2)
        {
            return _Result;
        }

        SAxisCandidate _AxisCandidate;
        _AxisCandidate.Direction = _Axis;
        std::vector<std::string> _IndependentNodeIDs;
        std::vector<Tube::SSolidNode> _IndependentNodes;
        std::size_t _IndependentIndex = 0;
        bool _AllSpansRecognized = true;
        for (TopExp_Explorer _Explorer(_InsideShape, TopAbs_SOLID);
            _Explorer.More();
            _Explorer.Next())
        {
            auto _Independent = _TryRecognizeRemovalExtrusion(
                _Explorer.Current(),
                MainBase_,
                _AxisCandidate,
                BaseAxis_,
                Options_,
                strNodeID_ + "/independent-span-"
                    + std::to_string(++_IndependentIndex));
            if (!_Independent)
            {
                _AllSpansRecognized = false;
                break;
            }
            _Independent->Node.Metadata["groupingInterpretation"] =
                "independent-material-span";
            _IndependentNodeIDs.push_back(_Independent->Node.ID);
            _IndependentNodes.push_back(std::move(_Independent->Node));
        }
        if (!_AllSpansRecognized || _IndependentNodeIDs.size() < 2)
        {
            return _Result;
        }
        for (auto& _IndependentNode : _IndependentNodes)
        {
            _Result.Nodes.push_back(std::move(_IndependentNode));
        }

        Tube::SBooleanNode _IndependentUnion;
        _IndependentUnion.Operation = Tube::EBooleanOperation::Union;
        _IndependentUnion.Children = _IndependentNodeIDs;
        Tube::SSolidNode _IndependentUnionNode;
        _IndependentUnionNode.ID = strNodeID_ + "/independent-spans";
        _IndependentUnionNode.Label = "Independent coaxial material-span cuts";
        _IndependentUnionNode.Data = std::move(_IndependentUnion);
        _IndependentUnionNode.RemovalSemantics = _Extent.Semantics;
        _Result.Nodes.push_back(std::move(_IndependentUnionNode));

        Tube::SAlternativeNode _Alternative;
        Tube::SInterpretationCandidate _SingleGenerator;
        _SingleGenerator.ID = "single-cross-material-generator";
        _SingleGenerator.Label = "One generator crossing multiple material spans";
        _SingleGenerator.CandidateNodeID = strNodeID_ + "/single-generator";
        _SingleGenerator.Confidence = 0.90;
        _SingleGenerator.RelativeGeometryError = 0.0;
        _SingleGenerator.Evaluation = _EvaluateInterpretation(
            0.0,
            1.0,
            0.90,
            0.18,
            0.04,
            0.88,
            0.95);
        _SingleGenerator.Metadata["semantic"] = "single-generator";
        _SingleGenerator.Evidence.push_back({
            "common-part-surface-generator",
            "Disconnected generated faces lie on the same analytic cylinder",
            0.90,
            {}
        });
        _Alternative.Candidates.push_back(std::move(_SingleGenerator));

        Tube::SInterpretationCandidate _IndependentSpans;
        _IndependentSpans.ID = "independent-coaxial-cuts";
        _IndependentSpans.Label = "Independent coaxial cuts for each material span";
        _IndependentSpans.CandidateNodeID = strNodeID_ + "/independent-spans";
        _IndependentSpans.Confidence = 0.70;
        _IndependentSpans.RelativeGeometryError = 0.0;
        _IndependentSpans.Evaluation = _EvaluateInterpretation(
            0.0,
            1.0,
            0.78,
            0.42,
            0.04,
            0.80,
            0.67);
        _IndependentSpans.Metadata["semantic"] = "independent-generators";
        _IndependentSpans.Evidence.push_back({
            "disconnected-removed-material",
            "Each disconnected material span is independently representable",
            0.70,
            {}
        });
        _Alternative.Candidates.push_back(std::move(_IndependentSpans));
        _Alternative.RecommendedCandidateID = "single-cross-material-generator";

        Tube::SSolidNode _AlternativeNode;
        _AlternativeNode.ID = strNodeID_;
        _AlternativeNode.Label = "Optional cross-material cylinder grouping";
        _AlternativeNode.Data = std::move(_Alternative);
        _AlternativeNode.Relations = _Result.Nodes.front().Relations;
        _AlternativeNode.RemovalSemantics = _Extent.Semantics;
        _AlternativeNode.Metadata["source"] = "occ.part-surface-group-alternative";
        _Result.Nodes.push_back(std::move(_AlternativeNode));
        _Result.RootNodeID = strNodeID_;
        _Result.AlternativeCount = 1;
        return _Result;
    }

    std::optional<SRecognizedExtrusion> _TryRecognizeHalfSpaceRemoval(
        IN const TopoDS_Shape& RemovedSolid_,
        IN const TopoDS_Shape& MainBase_,
        IN const gp_Dir& BaseAxis_,
        IN const STubeCSGConversionOptions& Options_,
        IN const std::string& strNodeID_,
        IN bool bRequireCompleteMatch_ = true)
    {
        GProp_GProps _RemovedProperties;
        BRepGProp::VolumeProperties(RemovedSolid_, _RemovedProperties);
        const auto _RemovedVolume = std::abs(_RemovedProperties.Mass());
        if (_RemovedVolume <= Options_.Tolerance * Options_.Tolerance * Options_.Tolerance)
        {
            return std::nullopt;
        }
        const auto _InsidePoint = _RemovedProperties.CentreOfMass();

        for (TopExp_Explorer _Explorer(RemovedSolid_, TopAbs_FACE);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Face = TopoDS::Face(_Explorer.Current());
            BRepAdaptor_Surface _Surface(_Face, true);
            if (_Surface.GetType() != GeomAbs_Plane)
            {
                continue;
            }
            const auto _Plane = _Surface.Plane();
            const auto _SignedDistance = gp_Vec(
                _Plane.Location(),
                _InsidePoint).Dot(gp_Vec(_Plane.Axis().Direction()));
            if (std::abs(_SignedDistance) <= Options_.Tolerance)
            {
                continue;
            }
            const auto _PlaneFace = BRepBuilderAPI_MakeFace(_Plane).Face();
            BRepPrimAPI_MakeHalfSpace _HalfSpaceBuilder(_PlaneFace, _InsidePoint);
            _HalfSpaceBuilder.Build();
            if (!_HalfSpaceBuilder.IsDone())
            {
                continue;
            }
            BRepAlgoAPI_Common _InsideBase(_HalfSpaceBuilder.Solid(), MainBase_);
            _InsideBase.SetFuzzyValue(Options_.Tolerance);
            _InsideBase.Build();
            if (!_InsideBase.IsDone())
            {
                continue;
            }
            const auto _InsideShape = _InsideBase.Shape();
            BRepAlgoAPI_Cut _Missing(RemovedSolid_, _InsideShape);
            _Missing.SetFuzzyValue(Options_.Tolerance);
            _Missing.Build();
            BRepAlgoAPI_Cut _Excess(_InsideShape, RemovedSolid_);
            _Excess.SetFuzzyValue(Options_.Tolerance);
            _Excess.Build();
            if (!_Missing.IsDone() || !_Excess.IsDone())
            {
                continue;
            }
            const auto _RelativeError = (
                _ShapeVolume(_Missing.Shape()) + _ShapeVolume(_Excess.Shape())) / _RemovedVolume;
            const auto _InsideVolume = _ShapeVolume(_InsideShape);
            const auto _ExcessVolume = _ShapeVolume(_Excess.Shape());
            const auto _CoverageRatio = _Clamp(
                (_InsideVolume - _ExcessVolume) / _RemovedVolume,
                0.0,
                1.0);
            if ((bRequireCompleteMatch_ && _RelativeError > 1.0e-4)
                || (!bRequireCompleteMatch_
                    && (_InsideVolume <= Options_.Tolerance * Options_.Tolerance * Options_.Tolerance
                        || _ExcessVolume / std::max(_InsideVolume, _RemovedVolume) > 1.0e-4)))
            {
                continue;
            }

            Tube::SHalfSpaceNode _HalfSpace;
            _HalfSpace.Boundary.Location = {
                _Plane.Location().X(),
                _Plane.Location().Y(),
                _Plane.Location().Z()
            };
            _HalfSpace.Boundary.Normal = {
                _Plane.Axis().Direction().X(),
                _Plane.Axis().Direction().Y(),
                _Plane.Axis().Direction().Z()
            };
            _HalfSpace.Boundary.XDirection = {
                _Plane.Position().XDirection().X(),
                _Plane.Position().XDirection().Y(),
                _Plane.Position().XDirection().Z()
            };
            _HalfSpace.KeepNegativeSide = _SignedDistance < 0.0;

            SRecognizedExtrusion _Result;
            _Result.ShapeInsideBase = _InsideShape;
            _Result.RelativeFitError = bRequireCompleteMatch_
                ? _RelativeError
                : _ExcessVolume / std::max(_InsideVolume, _RemovedVolume);
            _Result.CoverageRatio = _CoverageRatio;
            _Result.Node.ID = strNodeID_;
            _Result.Node.Label = "Recognized planar end trim";
            _Result.Node.Data = std::move(_HalfSpace);
            Tube::SFeatureRelations _Relations;
            _Relations.DependsOnNodeIDs.push_back("base");
            Tube::SFeatureMaterialEffect _MaterialEffect;
            _MaterialEffect.Role = Tube::EFeatureMaterialRole::Truncation;
            _MaterialEffect.HostNodeID = "base";
            _MaterialEffect.Confidence = 0.96;
            _Relations.MaterialEffect = std::move(_MaterialEffect);
            _Result.Node.Relations = std::move(_Relations);
            _Result.Node.Metadata["source"] = "occ.planar-half-space";
            _Result.Node.Metadata["fitError"] = _ToString(_RelativeError);
            _Result.Node.Metadata["sourceCoverage"] = _ToString(_CoverageRatio);
            if (!bRequireCompleteMatch_)
            {
                _Result.Node.Metadata["recognitionMode"] = "global-subset";
            }
            const auto _AxisDot = _AbsDot(_Plane.Axis().Direction(), BaseAxis_);
            _Result.Node.Metadata["featureSemantic"] = _AxisDot >= 1.0 - 1.0e-6
                ? "straight-end-trim"
                : "oblique-end-trim";
            _Result.Node.Metadata["axisDot"] = _ToString(_AxisDot);
            return _Result;
        }
        return std::nullopt;
    }

    TopoDS_Shape _FuseShapes(IN const std::vector<TopoDS_Shape>& Shapes_, IN double dTolerance_)
    {
        if (Shapes_.empty())
        {
            return {};
        }
        auto _Result = Shapes_.front();
        for (std::size_t _Index = 1; _Index < Shapes_.size(); ++_Index)
        {
            BRepAlgoAPI_Fuse _Fuse(_Result, Shapes_[_Index]);
            _Fuse.SetFuzzyValue(dTolerance_);
            _Fuse.Build();
            if (!_Fuse.IsDone())
            {
                return {};
            }
            _Result = _Fuse.Shape();
        }
        return _Result;
    }

    Tube::SCompositeVolumeNode _MakeCompositeVolumeNode(
        IN const TopoDS_Shape& Shape_,
        IN const std::string& strBRepResourceID_)
    {
        Tube::SCompositeVolumeNode _Composite;
        _Composite.BRepResourceID = strBRepResourceID_;
        _Composite.EditCapabilities = {
            Tube::ECompositeEditCapability::WholeTransform,
            Tube::ECompositeEditCapability::BooleanReuse,
            Tube::ECompositeEditCapability::AnalyticFaceEdit,
            Tube::ECompositeEditCapability::ManualFeaturePromotion
        };
        _Composite.Metadata["fallbackLevel"] = "semi-parametric-composite";
        _Composite.Metadata["patchIdentifierScope"] = "composite-local";

        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> _Faces;
        TopExp::MapShapes(Shape_, TopAbs_FACE, _Faces);
        NCollection_IndexedDataMap<
            TopoDS_Shape,
            NCollection_List<TopoDS_Shape>,
            TopTools_ShapeMapHasher> _EdgeFaces;
        TopExp::MapShapesAndAncestors(
            Shape_,
            TopAbs_EDGE,
            TopAbs_FACE,
            _EdgeFaces);

        for (int _FaceIndex = 1; _FaceIndex <= _Faces.Extent(); ++_FaceIndex)
        {
            const auto _Face = TopoDS::Face(_Faces(_FaceIndex));
            Tube::SCompositeSurfacePatch _Patch;
            _Patch.SourceShapeID = static_cast<std::uint64_t>(_FaceIndex);
            BRepAdaptor_Surface _Surface(_Face, true);
            const auto _PutAxis = [&_Patch](
                IN const gp_Pnt& Location_,
                IN const gp_Dir& Direction_) {
                _Patch.Parameters["axis.location.x"] = Location_.X();
                _Patch.Parameters["axis.location.y"] = Location_.Y();
                _Patch.Parameters["axis.location.z"] = Location_.Z();
                _Patch.Parameters["axis.direction.x"] = Direction_.X();
                _Patch.Parameters["axis.direction.y"] = Direction_.Y();
                _Patch.Parameters["axis.direction.z"] = Direction_.Z();
            };
            switch (_Surface.GetType())
            {
            case GeomAbs_Plane:
            {
                _Patch.Kind = Tube::EAnalyticSurfaceKind::Plane;
                const auto _Plane = _Surface.Plane();
                _PutAxis(_Plane.Location(), _Plane.Axis().Direction());
                break;
            }
            case GeomAbs_Cylinder:
            {
                _Patch.Kind = Tube::EAnalyticSurfaceKind::Cylinder;
                const auto _Cylinder = _Surface.Cylinder();
                _PutAxis(_Cylinder.Location(), _Cylinder.Axis().Direction());
                _Patch.Parameters["radius"] = _Cylinder.Radius();
                break;
            }
            case GeomAbs_Cone:
            {
                _Patch.Kind = Tube::EAnalyticSurfaceKind::Cone;
                const auto _Cone = _Surface.Cone();
                _PutAxis(_Cone.Location(), _Cone.Axis().Direction());
                _Patch.Parameters["referenceRadius"] = _Cone.RefRadius();
                _Patch.Parameters["semiAngleRadians"] = _Cone.SemiAngle();
                break;
            }
            case GeomAbs_Sphere:
            {
                _Patch.Kind = Tube::EAnalyticSurfaceKind::Sphere;
                const auto _Sphere = _Surface.Sphere();
                _PutAxis(_Sphere.Location(), _Sphere.Position().Direction());
                _Patch.Parameters["radius"] = _Sphere.Radius();
                break;
            }
            case GeomAbs_Torus:
            {
                _Patch.Kind = Tube::EAnalyticSurfaceKind::Torus;
                const auto _Torus = _Surface.Torus();
                _PutAxis(_Torus.Location(), _Torus.Axis().Direction());
                _Patch.Parameters["majorRadius"] = _Torus.MajorRadius();
                _Patch.Parameters["minorRadius"] = _Torus.MinorRadius();
                break;
            }
            case GeomAbs_SurfaceOfExtrusion:
                _Patch.Kind = Tube::EAnalyticSurfaceKind::Extrusion;
                break;
            case GeomAbs_SurfaceOfRevolution:
                _Patch.Kind = Tube::EAnalyticSurfaceKind::Revolution;
                break;
            case GeomAbs_BSplineSurface:
            case GeomAbs_BezierSurface:
                _Patch.Kind = Tube::EAnalyticSurfaceKind::BSpline;
                break;
            default:
                _Patch.Kind = Tube::EAnalyticSurfaceKind::Unknown;
                break;
            }
            _Patch.Parameters["area"] = _FaceArea(_Face);

            std::set<std::uint64_t> _AdjacentIDs;
            for (TopExp_Explorer _EdgeExplorer(_Face, TopAbs_EDGE);
                _EdgeExplorer.More();
                _EdgeExplorer.Next())
            {
                if (!_EdgeFaces.Contains(_EdgeExplorer.Current()))
                {
                    continue;
                }
                const auto& _AdjacentFaces = _EdgeFaces.FindFromKey(_EdgeExplorer.Current());
                for (NCollection_List<TopoDS_Shape>::Iterator _AdjacentIter(_AdjacentFaces);
                    _AdjacentIter.More();
                    _AdjacentIter.Next())
                {
                    const auto _AdjacentIndex = _Faces.FindIndex(_AdjacentIter.Value());
                    if (_AdjacentIndex > 0 && _AdjacentIndex != _FaceIndex)
                    {
                        _AdjacentIDs.insert(static_cast<std::uint64_t>(_AdjacentIndex));
                    }
                }
            }
            _Patch.AdjacentShapeIDs.assign(_AdjacentIDs.begin(), _AdjacentIDs.end());
            _Composite.AnalyticPatches.push_back(std::move(_Patch));
        }
        _Composite.Metadata["patchCount"] = std::to_string(_Composite.AnalyticPatches.size());
        return _Composite;
    }

    std::string _ToString(IN double Value_)
    {
        std::ostringstream _Stream;
        _Stream << std::setprecision(17) << Value_;
        return _Stream.str();
    }

    Tube::SRecognitionDiagnostic _MakeDiagnostic(
        IN const std::string& strCode_,
        IN const std::string& strMessage_,
        IN double dConfidence_)
    {
        Tube::SRecognitionDiagnostic _Diagnostic;
        _Diagnostic.Code = strCode_;
        _Diagnostic.Message = strMessage_;
        _Diagnostic.Confidence = _Clamp(dConfidence_, 0.0, 1.0);
        return _Diagnostic;
    }
}

STubeCSGConversionResult ConvertBRepToTubeCSG(
    IN const TopoDS_Shape& Shape_,
    IN const STubeCSGConversionOptions& Options_)
{
    STubeCSGConversionResult _Result;
    auto& _Geometry = _Result.Geometry;
    _Geometry.Version = 5;
    _Geometry.Metadata["converter"] = "occ.projective-brep-csg.v5";
    if (Shape_.IsNull())
    {
        _Geometry.Diagnostics.push_back(_MakeDiagnostic("empty-brep", "OCC BRep is empty", 0.0));
        return _Result;
    }
    if (Options_.Tolerance <= 0.0)
    {
        throw std::invalid_argument("Tube CSG conversion tolerance must be positive");
    }

    const auto _Axis = _RecognizeAxis(Shape_, Options_);
    if (!_Axis)
    {
        _Geometry.Diagnostics.push_back(_MakeDiagnostic(
            "axis-not-recognized",
            "No common translational side-surface direction was recognized",
            0.0));
        return _Result;
    }
    const auto _AxisAreaRatio = _Axis->TotalArea > 0.0
        ? _Axis->CompatibleArea / _Axis->TotalArea
        : 0.0;
    _Geometry.Metadata["axisAreaRatio"] = _ToString(_AxisAreaRatio);

    const auto _Range = _ProjectAxialRange(Shape_, _Axis->Direction);
    if (!_Range)
    {
        _Geometry.Diagnostics.push_back(_MakeDiagnostic(
            "axial-range-not-recognized",
            "The BRep has no usable axial extent",
            _AxisAreaRatio));
        return _Result;
    }
    const auto _Frame = _MakeSectionFrame(_Axis->Direction, _Range->first, _Range->second);
    const auto _Section = _RecognizeSection(Shape_, _Frame, Options_);
    if (!_Section)
    {
        _Geometry.Diagnostics.push_back(_MakeDiagnostic(
            "section-not-recognized",
            "No closed virtual cross-section could be reconstructed from sampled side-surface stations",
            _AxisAreaRatio));
        return _Result;
    }
    const auto _BaseShape = _MakeBaseShape(*_Section, _Frame);
    if (!_BaseShape)
    {
        _Geometry.Diagnostics.push_back(_MakeDiagnostic(
            "base-evaluation-failed",
            "OCC could not evaluate the recognized extruded base",
            _AxisAreaRatio));
        return _Result;
    }
    _Result.BaseShape = *_BaseShape;
    _Result.PreviewShapes["base"] = *_BaseShape;
    const auto _OrderedBaseLoops = _OrderedSliceLoops(*_Section);
    for (std::size_t _LoopIndex = 0;
        _LoopIndex < _OrderedBaseLoops.size();
        ++_LoopIndex)
    {
        const auto _PrimitiveID = _LoopIndex == 0
            ? std::string("base/section/outer")
            : "base/section/cavity-" + std::to_string(_LoopIndex);
        if (const auto _Preview = _MakeSectionPrimitivePreviewShape(
            *_OrderedBaseLoops[_LoopIndex], *_Section, _Frame))
        {
            _Result.PreviewShapes[_PrimitiveID] = *_Preview;
        }
    }

    const auto _StableSideAtlases = _BuildStableSideAtlases(*_Section, _Frame);
    auto _Base = _MakeExtrudedRegionNodeData(*_Section, _Frame, "base");

    Tube::SSolidNode _BaseNode;
    _BaseNode.ID = "base";
    _BaseNode.Label = "Recognized standard extrusion";
    _BaseNode.Data = std::move(_Base);
    _BaseNode.Metadata["source"] = "occ.side-surfaces";
    _BaseNode.Metadata["sliceStation"] = _ToString(_Section->Station - _Frame.First);
    _Geometry.SolidNodes.push_back(std::move(_BaseNode));
    _Geometry.BaseNodeID = "base";
    _Geometry.RootNodeID = "base";

    BRepAlgoAPI_Cut _Removal(*_BaseShape, Shape_);
    _Removal.SetFuzzyValue(Options_.Tolerance);
    _Removal.Build();
    if (_Removal.IsDone())
    {
        _Result.RemovalShape = _Removal.Shape();
    }
    BRepAlgoAPI_Cut _Addition(Shape_, *_BaseShape);
    _Addition.SetFuzzyValue(Options_.Tolerance);
    _Addition.Build();
    if (_Addition.IsDone())
    {
        _Result.AdditionShape = _Addition.Shape();
    }

    const auto _InputVolume = _ShapeVolume(Shape_);
    const auto _BaseVolume = _ShapeVolume(*_BaseShape);
    const auto _RemovalVolume = _ShapeVolume(_Result.RemovalShape);
    const auto _AdditionVolume = _ShapeVolume(_Result.AdditionShape);
    const auto _FullRemovalShape = _Result.RemovalShape;
    const auto _PartSurfaceGraph = _BuildPartSurfaceEvidenceGraph(
        Shape_,
        *_BaseShape,
        Options_);
    _Geometry.Metadata["inputVolume"] = _ToString(_InputVolume);
    _Geometry.Metadata["baseVolume"] = _ToString(_BaseVolume);
    _Geometry.Metadata["removalVolume"] = _ToString(_RemovalVolume);
    _Geometry.Metadata["additionVolume"] = _ToString(_AdditionVolume);
    _Geometry.Metadata["sectionBoundaryCount"] = std::to_string(_Section->Loops.size());
    _Geometry.Metadata["innerBoundaryCount"] = std::to_string(_Section->Loops.size() > 0 ? _Section->Loops.size() - 1 : 0);
    for (const auto& [_Key, _Value] : _PartSurfaceGraph.Metadata)
    {
        _Geometry.Metadata["partSurfaceGraph." + _Key] = _Value;
    }

    auto _CurrentRoot = std::string("base");
    const auto _VolumeTolerance = std::max(
        Options_.Tolerance * Options_.Tolerance * Options_.Tolerance,
        std::max(_InputVolume, _BaseVolume) * 1.0e-10);

    std::vector<std::string> _RemovalNodeIDs;
    std::vector<TopoDS_Shape> _RecognizedRemovalShapes;
    std::size_t _RecognizedRemovalCount = 0;
    std::size_t _AlternativeCount = 0;
    std::size_t _OverlapDecompositionCount = 0;
    std::size_t _PartSurfaceGroupCount = 0;
    bool _UsedApproximateBRepFit = false;
    double _ApproximateRemovalFitError = 0.0;
    if (_RemovalVolume > _VolumeTolerance)
    {
        const auto _CylinderGroups = _GroupPartCylinderEvidence(
            _PartSurfaceGraph,
            Options_);
        _Geometry.Metadata["partSurfaceGraph.cylinderGroupCount"] = std::to_string(
            _CylinderGroups.size());
        std::size_t _CylinderGroupIndex = 0;
        for (const auto& _CylinderGroup : _CylinderGroups)
        {
            _Geometry.Metadata[
                "partSurfaceGraph.cylinderGroup."
                    + std::to_string(_CylinderGroupIndex + 1)
                    + ".faceCount"] = std::to_string(_CylinderGroup.LateralFaces.size());
            std::string _FailureReason;
            auto _GlobalFeature = _TryRecognizeGlobalCylinderGroup(
                _CylinderGroup,
                _PartSurfaceGraph,
                *_BaseShape,
                _FullRemovalShape,
                _Frame.ZDirection,
                Options_,
                "surface-group-cylinder-"
                    + std::to_string(++_CylinderGroupIndex),
                &_FailureReason);
            if (!_GlobalFeature)
            {
                _Geometry.Metadata[
                    "partSurfaceGraph.cylinderGroup."
                        + std::to_string(_CylinderGroupIndex)
                        + ".rejection"] = _FailureReason;
                continue;
            }
            if (!_GlobalFeature->PreviewShape.IsNull())
            {
                _Result.PreviewShapes[_GlobalFeature->RootNodeID] =
                    _GlobalFeature->PreviewShape;
                for (const auto& _Node : _GlobalFeature->Nodes)
                {
                    constexpr auto _SingleGeneratorSuffix = "/single-generator";
                    if (_Node.ID == _GlobalFeature->RootNodeID
                        || (_Node.ID.size() >= std::char_traits<char>::length(_SingleGeneratorSuffix)
                            && _Node.ID.compare(
                                _Node.ID.size() - std::char_traits<char>::length(_SingleGeneratorSuffix),
                                std::char_traits<char>::length(_SingleGeneratorSuffix),
                                _SingleGeneratorSuffix) == 0))
                    {
                        _Result.PreviewShapes[_Node.ID] =
                            _GlobalFeature->PreviewShape;
                    }
                }
            }
            for (auto& _Node : _GlobalFeature->Nodes)
            {
                _Geometry.SolidNodes.push_back(std::move(_Node));
            }
            _RemovalNodeIDs.push_back(_GlobalFeature->RootNodeID);
            _RecognizedRemovalShapes.push_back(_GlobalFeature->ShapeInsideBase);
            _AlternativeCount += _GlobalFeature->AlternativeCount;
            ++_RecognizedRemovalCount;
            ++_PartSurfaceGroupCount;
        }

        for (TopExp_Explorer _Explorer(_FullRemovalShape, TopAbs_SOLID);
            _Explorer.More();
            _Explorer.Next())
        {
            if (!_RecognizedRemovalShapes.empty())
            {
                const auto _RecognizedUnion = _FuseShapes(
                    _RecognizedRemovalShapes,
                    Options_.Tolerance);
                if (!_RecognizedUnion.IsNull())
                {
                    BRepAlgoAPI_Cut _UncoveredComponent(
                        _Explorer.Current(),
                        _RecognizedUnion);
                    _UncoveredComponent.SetFuzzyValue(Options_.Tolerance);
                    _UncoveredComponent.Build();
                    if (_UncoveredComponent.IsDone()
                        && _ShapeVolume(_UncoveredComponent.Shape()) <= _VolumeTolerance)
                    {
                        continue;
                    }
                }
            }
            const auto _NodeID = "removal-"
                + std::to_string(_RecognizedRemovalCount + 1);
            std::vector<SRecognitionHypothesis> _Hypotheses;

            if (auto _HalfSpace = _TryRecognizeHalfSpaceRemoval(
                _Explorer.Current(),
                *_BaseShape,
                _Frame.ZDirection,
                Options_,
                _NodeID + "/half-space"))
            {
                SRecognitionHypothesis _Hypothesis;
                _Hypothesis.RootNodeID = _HalfSpace->Node.ID;
                _Hypothesis.ShapeInsideBase = _HalfSpace->ShapeInsideBase;
                _Hypothesis.PreviewShape = _HalfSpace->ShapeInsideBase;
                _Hypothesis.CandidateID = "planar-half-space";
                _Hypothesis.Label = "Planar end trim";
                _Hypothesis.Semantic = _HalfSpace->Node.Metadata["featureSemantic"];
                _Hypothesis.EvidenceCode = "planar-boundary";
                _Hypothesis.EvidenceMessage =
                    "A planar half-space reproduces the complete removed volume";
                _Hypothesis.Confidence = 0.96;
                _Hypothesis.RelativeFitError = _HalfSpace->RelativeFitError;
                _Hypothesis.RemovalSemantics = _HalfSpace->Node.RemovalSemantics;
                _Hypothesis.Nodes.push_back(std::move(_HalfSpace->Node));
                _Hypotheses.push_back(std::move(_Hypothesis));
            }

            std::string _WrappedFailureReason;
            if (auto _Wrapped = _TryRecognizeWrappedNormalRemoval(
                _Explorer.Current(),
                *_BaseShape,
                *_Section,
                _Frame,
                _StableSideAtlases,
                Options_,
                _NodeID + "/wrapped-normal",
                &_WrappedFailureReason))
            {
                SRecognitionHypothesis _Hypothesis;
                _Hypothesis.RootNodeID = _Wrapped->Node.ID;
                _Hypothesis.ShapeInsideBase = _Wrapped->ShapeInsideBase;
                _Hypothesis.PreviewShape = _Wrapped->ShapeInsideBase;
                _Hypothesis.CandidateID = "stable-uv-normal-volume";
                _Hypothesis.Label = "Stable Side Atlas normal-volume interpretation";
                _Hypothesis.Semantic = _Wrapped->Node.Metadata["featureSemantic"];
                _Hypothesis.EvidenceCode = "stable-side-atlas-normal-columns";
                _Hypothesis.EvidenceMessage =
                    "The removed volume follows material outward-normal columns over a stable physical UV region";
                _Hypothesis.Confidence = _Hypothesis.Semantic == "wrapped-normal-end-trim"
                    ? 0.90
                    : 0.84;
                _Hypothesis.RelativeFitError = _Wrapped->RelativeFitError;
                _Hypothesis.GeometryAcceptanceTolerance =
                    _Wrapped->GeometryAcceptanceTolerance;
                _Hypothesis.RemovalSemantics = _Wrapped->Node.RemovalSemantics;
                _Hypothesis.Nodes.push_back(std::move(_Wrapped->Node));
                _Hypotheses.push_back(std::move(_Hypothesis));
            }
            else if (!_WrappedFailureReason.empty())
            {
                _Geometry.Metadata[
                    "wrappedNormal." + _NodeID + ".rejection"] =
                    std::move(_WrappedFailureReason);
            }

            const auto _RemovalAxes = _RecognizeAxes(
                _Explorer.Current(),
                Options_,
                &*_BaseShape);
            std::size_t _ExtrusionIndex = 0;
            bool _HasCompleteExtrusion = false;
            for (const auto& _RemovalAxis : _RemovalAxes)
            {
                auto _Extrusion = _TryRecognizeRemovalExtrusion(
                    _Explorer.Current(),
                    *_BaseShape,
                    _RemovalAxis,
                    _Frame.ZDirection,
                    Options_,
                    _NodeID + "/extrusion-" + std::to_string(++_ExtrusionIndex));
                if (!_Extrusion)
                {
                    continue;
                }
                _AppendTranslationSurfaceEvidence(
                    _Extrusion->Node,
                    _PartSurfaceGraph,
                    _RemovalAxis.Direction,
                    Options_);
                SRecognitionHypothesis _Hypothesis;
                _Hypothesis.RootNodeID = _Extrusion->Node.ID;
                _Hypothesis.ShapeInsideBase = _Extrusion->ShapeInsideBase;
                _Hypothesis.PreviewShape = _Extrusion->FullShape;
                _Hypothesis.CandidateID = "extruded-region-" + std::to_string(_ExtrusionIndex);
                _Hypothesis.Label = "Constant-section penetration";
                _Hypothesis.Semantic = _Extrusion->Node.Metadata["featureSemantic"];
                _Hypothesis.EvidenceCode = "translation-compatible-boundaries";
                _Hypothesis.EvidenceMessage =
                    "A constant-section extrusion reproduces the complete removed volume";
                const auto _AreaRatio = _RemovalAxis.TotalArea > 0.0
                    ? _RemovalAxis.CompatibleArea / _RemovalAxis.TotalArea
                    : 0.0;
                _Hypothesis.Confidence = _Clamp(0.72 + 0.2 * _AreaRatio, 0.0, 0.92);
                _Hypothesis.RelativeFitError = _Extrusion->RelativeFitError;
                _Hypothesis.GeometryAcceptanceTolerance =
                    _Extrusion->GeometryAcceptanceTolerance;
                _Hypothesis.RemovalSemantics = _Extrusion->Node.RemovalSemantics;
                _Hypothesis.Nodes.push_back(std::move(_Extrusion->Node));
                _Hypotheses.push_back(std::move(_Hypothesis));
                _HasCompleteExtrusion = true;
                if (_RemovalAxis.FromTangentPlaneConsensus
                    && _Extrusion->RelativeFitError > 1.0e-4)
                {
                    _UsedApproximateBRepFit = true;
                    _ApproximateRemovalFitError = std::max(
                        _ApproximateRemovalFitError,
                        _Extrusion->RelativeFitError);
                }
            }

            if (auto _Beveled = _TryRecognizeBeveledCircularRemoval(
                _Explorer.Current(),
                *_BaseShape,
                Options_,
                _NodeID + "/explicit-bevel"))
            {
                for (const auto& [_PreviewNodeID, _PreviewShape] :
                    _Beveled->PreviewShapes)
                {
                    if (!_PreviewShape.IsNull())
                    {
                        _Result.PreviewShapes[_PreviewNodeID] = _PreviewShape;
                    }
                }
                SRecognitionHypothesis _Hypothesis;
                _Hypothesis.Nodes = std::move(_Beveled->Nodes);
                _Hypothesis.RootNodeID = std::move(_Beveled->RootNodeID);
                _Hypothesis.ShapeInsideBase = _Beveled->ShapeInsideBase;
                const auto _RootPreview = _Beveled->PreviewShapes.find(
                    _Beveled->RootNodeID);
                _Hypothesis.PreviewShape = _RootPreview != _Beveled->PreviewShapes.end()
                    ? _RootPreview->second
                    : _Beveled->ShapeInsideBase;
                _Hypothesis.CandidateID = "straight-penetration-with-bevel";
                _Hypothesis.Label = "Straight penetration with explicit bevel";
                _Hypothesis.Semantic = "beveled-penetration";
                _Hypothesis.EvidenceCode = "cylindrical-land-and-conical-band";
                _Hypothesis.EvidenceMessage =
                    "A cylindrical land joined to a conical band explicitly identifies a bevel";
                _Hypothesis.Confidence = 0.97;
                _Hypothesis.RelativeFitError = _Beveled->RelativeFitError;
                if (!_Beveled->Nodes.empty())
                {
                    _Hypothesis.RemovalSemantics = _Beveled->Nodes.back().RemovalSemantics;
                }
                _Hypotheses.push_back(std::move(_Hypothesis));
            }

            if (!_HasCompleteExtrusion)
            {
                if (auto _Conical = _TryRecognizeConicalRemoval(
                    _Explorer.Current(),
                    *_BaseShape,
                    Options_,
                    _NodeID + "/conical"))
                {
                    for (const auto& [_PreviewNodeID, _PreviewShape] :
                        _Conical->PreviewShapes)
                    {
                        if (!_PreviewShape.IsNull())
                        {
                            _Result.PreviewShapes[_PreviewNodeID] = _PreviewShape;
                        }
                    }
                    SRecognitionHypothesis _Hypothesis;
                    _Hypothesis.Nodes = std::move(_Conical->Nodes);
                    _Hypothesis.RootNodeID = std::move(_Conical->RootNodeID);
                    _Hypothesis.ShapeInsideBase = _Conical->ShapeInsideBase;
                    const auto _RootPreview = _Conical->PreviewShapes.find(
                        _Conical->RootNodeID);
                    _Hypothesis.PreviewShape = _RootPreview != _Conical->PreviewShapes.end()
                        ? _RootPreview->second
                        : _Conical->ShapeInsideBase;
                    _Hypothesis.CandidateID = "conical-removal";
                    _Hypothesis.Label = "Conical removal interpretations";
                    _Hypothesis.Semantic = "conical-removal";
                    _Hypothesis.EvidenceCode = "common-tangent-plane-center";
                    _Hypothesis.EvidenceMessage =
                        "A tapered generator reconstructed from a common tangent-plane apex reproduces the removal";
                    _Hypothesis.Confidence = 0.82;
                    _Hypothesis.RelativeFitError = _Conical->RelativeFitError;
                    _Hypothesis.NestedAlternativeCount = 1;
                    if (!_Conical->Nodes.empty())
                    {
                        _Hypothesis.RemovalSemantics =
                            _Conical->Nodes.back().RemovalSemantics;
                    }
                    _Hypotheses.push_back(std::move(_Hypothesis));
                }
            }

            bool _IsOverlapDecomposition = false;
            if (_Hypotheses.empty())
            {
                /*
                * 一个截断体和一个孔相交后，OCC 往往只给出一个连通缺失 Solid。
                * 此时单个特征都不等于完整缺失体，但各自仍应是缺失体的子集。
                * 先生成严格子集候选，再用它们的全局并集覆盖当前缺失体。
                */
                std::vector<SRecognitionHypothesis> _SubsetHypotheses;
                if (auto _HalfSpace = _TryRecognizeHalfSpaceRemoval(
                    _Explorer.Current(),
                    *_BaseShape,
                    _Frame.ZDirection,
                    Options_,
                    _NodeID + "/subset-half-space",
                    false))
                {
                    SRecognitionHypothesis _Hypothesis;
                    _Hypothesis.RootNodeID = _HalfSpace->Node.ID;
                    _Hypothesis.ShapeInsideBase = _HalfSpace->ShapeInsideBase;
                    _Hypothesis.PreviewShape = _HalfSpace->ShapeInsideBase;
                    _Hypothesis.CandidateID = "overlap-planar-half-space";
                    _Hypothesis.Label = "Planar trim participating in an overlap";
                    _Hypothesis.Semantic = _HalfSpace->Node.Metadata["featureSemantic"];
                    _Hypothesis.EvidenceCode = "verified-removal-subset";
                    _Hypothesis.EvidenceMessage =
                        "The planar half-space is a verified subset of the connected removed volume";
                    _Hypothesis.Confidence = 0.92;
                    _Hypothesis.RelativeFitError = _HalfSpace->RelativeFitError;
                    _Hypothesis.Nodes.push_back(std::move(_HalfSpace->Node));
                    _SubsetHypotheses.push_back(std::move(_Hypothesis));
                }

                const auto _SubsetAxes = _RecognizeAxes(
                    _Explorer.Current(),
                    Options_,
                    &*_BaseShape);
                std::size_t _SubsetExtrusionIndex = 0;
                for (const auto& _RemovalAxis : _SubsetAxes)
                {
                    auto _Extrusion = _TryRecognizeRemovalExtrusion(
                        _Explorer.Current(),
                        *_BaseShape,
                        _RemovalAxis,
                        _Frame.ZDirection,
                        Options_,
                        _NodeID + "/subset-extrusion-"
                            + std::to_string(++_SubsetExtrusionIndex),
                        false);
                    if (!_Extrusion || _Extrusion->CoverageRatio <= 1.0e-6)
                    {
                        continue;
                    }
                    SRecognitionHypothesis _Hypothesis;
                    _Hypothesis.RootNodeID = _Extrusion->Node.ID;
                    _Hypothesis.ShapeInsideBase = _Extrusion->ShapeInsideBase;
                    _Hypothesis.PreviewShape = _Extrusion->FullShape;
                    _Hypothesis.CandidateID = "overlap-extruded-region-"
                        + std::to_string(_SubsetExtrusionIndex);
                    _Hypothesis.Label = "Extruded cut participating in an overlap";
                    _Hypothesis.Semantic = _Extrusion->Node.Metadata["featureSemantic"];
                    _Hypothesis.EvidenceCode = "verified-removal-subset";
                    _Hypothesis.EvidenceMessage =
                        "The analytically continued extrusion is a verified subset of the connected removed volume";
                    const auto _AreaRatio = _RemovalAxis.TotalArea > 0.0
                        ? _RemovalAxis.CompatibleArea / _RemovalAxis.TotalArea
                        : 0.0;
                    _Hypothesis.Confidence = _Clamp(0.68 + 0.2 * _AreaRatio, 0.0, 0.90);
                    _Hypothesis.RelativeFitError = _Extrusion->RelativeFitError;
                    _Hypothesis.RemovalSemantics = _Extrusion->Node.RemovalSemantics;
                    _Hypothesis.Nodes.push_back(std::move(_Extrusion->Node));
                    _SubsetHypotheses.push_back(std::move(_Hypothesis));
                }

                std::sort(
                    _SubsetHypotheses.begin(),
                    _SubsetHypotheses.end(),
                    [](IN const auto& Left_, IN const auto& Right_) {
                        return Left_.Confidence > Right_.Confidence;
                    });
                TopoDS_Shape _SelectedUnion = _FuseShapes(
                    _RecognizedRemovalShapes,
                    Options_.Tolerance);
                for (auto& _Hypothesis : _SubsetHypotheses)
                {
                    auto _NewContribution = _Hypothesis.ShapeInsideBase;
                    if (!_SelectedUnion.IsNull())
                    {
                        BRepAlgoAPI_Cut _SubtractSelected(
                            _Hypothesis.ShapeInsideBase,
                            _SelectedUnion);
                        _SubtractSelected.SetFuzzyValue(Options_.Tolerance);
                        _SubtractSelected.Build();
                        if (!_SubtractSelected.IsDone())
                        {
                            continue;
                        }
                        _NewContribution = _SubtractSelected.Shape();
                    }
                    if (_ShapeVolume(_NewContribution) <= _VolumeTolerance)
                    {
                        continue;
                    }

                    if (_SelectedUnion.IsNull())
                    {
                        _SelectedUnion = _Hypothesis.ShapeInsideBase;
                    }
                    else
                    {
                        BRepAlgoAPI_Fuse _FuseSelected(
                            _SelectedUnion,
                            _Hypothesis.ShapeInsideBase);
                        _FuseSelected.SetFuzzyValue(Options_.Tolerance);
                        _FuseSelected.Build();
                        if (!_FuseSelected.IsDone())
                        {
                            continue;
                        }
                        _SelectedUnion = _FuseSelected.Shape();
                    }
                    _Hypotheses.push_back(std::move(_Hypothesis));
                }
                _IsOverlapDecomposition = !_Hypotheses.empty();
            }

            if (_Hypotheses.empty())
            {
                continue;
            }

            if (_IsOverlapDecomposition)
            {
                for (auto& _Hypothesis : _Hypotheses)
                {
                    if (!_Hypothesis.PreviewShape.IsNull())
                    {
                        _Result.PreviewShapes[_Hypothesis.RootNodeID] =
                            _Hypothesis.PreviewShape;
                    }
                    for (auto& _Node : _Hypothesis.Nodes)
                    {
                        _Geometry.SolidNodes.push_back(std::move(_Node));
                    }
                    _RemovalNodeIDs.push_back(_Hypothesis.RootNodeID);
                    _RecognizedRemovalShapes.push_back(_Hypothesis.ShapeInsideBase);
                    ++_RecognizedRemovalCount;
                }
                ++_OverlapDecompositionCount;
                continue;
            }

            const auto _RepresentativeShape = _Hypotheses.front().ShapeInsideBase;
            for (auto& _Hypothesis : _Hypotheses)
            {
                if (!_Hypothesis.PreviewShape.IsNull())
                {
                    _Result.PreviewShapes[_Hypothesis.RootNodeID] =
                        _Hypothesis.PreviewShape;
                }
                for (auto& _Node : _Hypothesis.Nodes)
                {
                    _Geometry.SolidNodes.push_back(std::move(_Node));
                }
                _AlternativeCount += _Hypothesis.NestedAlternativeCount;
            }

            auto _RemovalRootID = _Hypotheses.front().RootNodeID;
            if (_Hypotheses.size() > 1)
            {
                Tube::SAlternativeNode _Alternative;
                const SRecognitionHypothesis* _pRecommended = nullptr;
                for (const auto& _Hypothesis : _Hypotheses)
                {
                    Tube::SInterpretationCandidate _Candidate;
                    _Candidate.ID = _Hypothesis.CandidateID;
                    _Candidate.Label = _Hypothesis.Label;
                    _Candidate.CandidateNodeID = _Hypothesis.RootNodeID;
                    _Candidate.Optional = true;
                    _Candidate.Confidence = _Hypothesis.Confidence;
                    _Candidate.RelativeGeometryError = _Hypothesis.RelativeFitError;
                    _Candidate.Evaluation = _EvaluateInterpretation(
                        _Hypothesis.RelativeFitError,
                        _Hypothesis.Confidence,
                        _Hypothesis.Confidence,
                        _Hypothesis.NestedAlternativeCount > 0 ? 0.62 : 0.35,
                        _Hypothesis.Semantic == "beveled-penetration"
                            ? 0.45
                            : (_Hypothesis.Semantic.rfind("wrapped-normal", 0) == 0
                                ? 0.28
                                : 0.08),
                        _Hypothesis.Confidence,
                        _Hypothesis.Semantic == "composite-removal" ? 0.35 : 0.82,
                        _Hypothesis.GeometryAcceptanceTolerance);
                    _Candidate.Metadata["semantic"] = _Hypothesis.Semantic;
                    _Candidate.Evidence.push_back({
                        _Hypothesis.EvidenceCode,
                        _Hypothesis.EvidenceMessage,
                        _Hypothesis.Confidence,
                        {}
                    });
                    _Alternative.Candidates.push_back(std::move(_Candidate));
                    if (!_pRecommended
                        || _Hypothesis.Confidence > _pRecommended->Confidence)
                    {
                        _pRecommended = &_Hypothesis;
                    }
                }
                if (_pRecommended)
                {
                    _Alternative.RecommendedCandidateID = _pRecommended->CandidateID;
                }

                Tube::SSolidNode _AlternativeNode;
                _AlternativeNode.ID = _NodeID;
                _AlternativeNode.Label = "Unresolved equivalent CSG interpretations";
                _AlternativeNode.Data = std::move(_Alternative);
                _AlternativeNode.RemovalSemantics = _Hypotheses.front().RemovalSemantics;
                _AlternativeNode.Metadata["source"] = "occ.enumerated-hypotheses";
                _Geometry.SolidNodes.push_back(std::move(_AlternativeNode));
                _RemovalRootID = _NodeID;
                if (!_Hypotheses.front().PreviewShape.IsNull())
                {
                    _Result.PreviewShapes[_NodeID] =
                        _Hypotheses.front().PreviewShape;
                }
                ++_AlternativeCount;
            }

            _RemovalNodeIDs.push_back(std::move(_RemovalRootID));
            _RecognizedRemovalShapes.push_back(_RepresentativeShape);
            ++_RecognizedRemovalCount;
        }
    }

    auto _ResidualRemovalVolume = _RemovalVolume;
    if (!_RecognizedRemovalShapes.empty())
    {
        const auto _RecognizedUnion = _FuseShapes(_RecognizedRemovalShapes, Options_.Tolerance);
        if (!_RecognizedUnion.IsNull())
        {
            const auto _RecognizedUnionVolume = _ShapeVolume(_RecognizedUnion);
            const auto _UnionVolumeError = _RemovalVolume > _VolumeTolerance
                ? std::abs(_RecognizedUnionVolume - _RemovalVolume) / _RemovalVolume
                : 0.0;
            if (_UsedApproximateBRepFit && _UnionVolumeError <= 1.0e-2)
            {
                _Result.RemovalShape = {};
                _ResidualRemovalVolume = 0.0;
                _ApproximateRemovalFitError = std::max(
                    _ApproximateRemovalFitError,
                    _UnionVolumeError);
                _Geometry.Metadata["removalCoverageValidation"] =
                    "tangent-evidence-and-volume";
            }
            else
            {
                BRepAlgoAPI_Cut _UnresolvedRemoval(_FullRemovalShape, _RecognizedUnion);
                _UnresolvedRemoval.SetFuzzyValue(Options_.Tolerance);
                _UnresolvedRemoval.Build();
                if (_UnresolvedRemoval.IsDone())
                {
                    _Result.RemovalShape = _UnresolvedRemoval.Shape();
                    _ResidualRemovalVolume = _ShapeVolume(_Result.RemovalShape);
                }
            }
        }
    }
    if (_ResidualRemovalVolume <= _VolumeTolerance)
    {
        _Result.RemovalShape = {};
        _ResidualRemovalVolume = 0.0;
    }
    else
    {
        auto _Composite = _MakeCompositeVolumeNode(
            _Result.RemovalShape,
            Options_.RemovalBRepResourceID);
        Tube::SSolidNode _CompositeNode;
        _CompositeNode.ID = "composite-removal";
        _CompositeNode.Label = "Locally unresolved editable composite removal";
        _CompositeNode.Data = std::move(_Composite);
        _CompositeNode.Metadata["volume"] = _ToString(_ResidualRemovalVolume);
        _CompositeNode.Metadata["source"] = "occ.local-composite-fallback";
        _Geometry.SolidNodes.push_back(std::move(_CompositeNode));
        _RemovalNodeIDs.push_back("composite-removal");
    }

    if (!_RemovalNodeIDs.empty())
    {
        auto _RemovalOperandID = _RemovalNodeIDs.front();
        if (_RemovalNodeIDs.size() > 1)
        {
            Tube::SBooleanNode _RemovalUnion;
            _RemovalUnion.Operation = Tube::EBooleanOperation::Union;
            _RemovalUnion.Children = _RemovalNodeIDs;
            Tube::SSolidNode _RemovalUnionNode;
            _RemovalUnionNode.ID = "all-removals";
            _RemovalUnionNode.Label = "Union of recognized and residual removals";
            _RemovalUnionNode.Data = std::move(_RemovalUnion);
            _Geometry.SolidNodes.push_back(std::move(_RemovalUnionNode));
            _RemovalOperandID = "all-removals";
        }

        Tube::SBooleanNode _Difference;
        _Difference.Operation = Tube::EBooleanOperation::Difference;
        _Difference.Children = { _CurrentRoot, _RemovalOperandID };
        Tube::SSolidNode _DifferenceNode;
        _DifferenceNode.ID = "base-minus-removals";
        _DifferenceNode.Label = "Recognized base minus removal volumes";
        _DifferenceNode.Data = std::move(_Difference);
        _Geometry.SolidNodes.push_back(std::move(_DifferenceNode));
        _CurrentRoot = "base-minus-removals";
    }

    _Geometry.Metadata["recognizedRemovalCount"] = std::to_string(_RecognizedRemovalCount);
    _Geometry.Metadata["alternativeInterpretationCount"] = std::to_string(_AlternativeCount);
    _Geometry.Metadata["overlapDecompositionCount"] = std::to_string(
        _OverlapDecompositionCount);
    _Geometry.Metadata["partSurfaceGroupCount"] = std::to_string(
        _PartSurfaceGroupCount);
    _Geometry.Metadata["residualRemovalVolume"] = _ToString(_ResidualRemovalVolume);
    if (_AdditionVolume > _VolumeTolerance)
    {
        Tube::SResidualBRepNode _Residual;
        _Residual.BRepResourceID = Options_.AdditionBRepResourceID;
        Tube::SSolidNode _ResidualNode;
        _ResidualNode.ID = "residual-addition";
        _ResidualNode.Label = "Geometry outside recognized base";
        _ResidualNode.Data = std::move(_Residual);
        _ResidualNode.Metadata["volume"] = _ToString(_AdditionVolume);
        _Geometry.SolidNodes.push_back(std::move(_ResidualNode));

        Tube::SBooleanNode _Union;
        _Union.Operation = Tube::EBooleanOperation::Union;
        _Union.Children = { _CurrentRoot, "residual-addition" };
        Tube::SSolidNode _UnionNode;
        _UnionNode.ID = "root-with-residual-addition";
        _UnionNode.Label = "Recognized geometry with residual addition";
        _UnionNode.Data = std::move(_Union);
        _Geometry.SolidNodes.push_back(std::move(_UnionNode));
        _CurrentRoot = "root-with-residual-addition";
    }
    _Geometry.RootNodeID = _CurrentRoot;

    _Geometry.RelativeUnparameterizedVolume = std::max(_InputVolume, _BaseVolume) > _VolumeTolerance
        ? (_AdditionVolume + _ResidualRemovalVolume) / std::max(_InputVolume, _BaseVolume)
        : 0.0;
    _Geometry.RelativeOpaqueResidualVolume = std::max(_InputVolume, _BaseVolume) > _VolumeTolerance
        ? _AdditionVolume / std::max(_InputVolume, _BaseVolume)
        : 0.0;

    auto _ReconstructedShape = *_BaseShape;
    bool _ReconstructionEvaluated = true;
    if (_UsedApproximateBRepFit && _ResidualRemovalVolume == 0.0)
    {
        _Geometry.RelativeVolumeError = _ApproximateRemovalFitError
            * _RemovalVolume / std::max(_InputVolume, _BaseVolume);
        _Geometry.Metadata["csgValidation"] =
            "projective-evidence-plus-relative-volume";
    }
    else
    {
        if (_RemovalVolume > _VolumeTolerance)
        {
            BRepAlgoAPI_Cut _EvaluateRemoval(_ReconstructedShape, _FullRemovalShape);
            _EvaluateRemoval.SetFuzzyValue(Options_.Tolerance);
            _EvaluateRemoval.Build();
            _ReconstructionEvaluated = _EvaluateRemoval.IsDone();
            if (_ReconstructionEvaluated)
            {
                _ReconstructedShape = _EvaluateRemoval.Shape();
            }
        }
        if (_ReconstructionEvaluated && _AdditionVolume > _VolumeTolerance)
        {
            BRepAlgoAPI_Fuse _EvaluateAddition(_ReconstructedShape, _Result.AdditionShape);
            _EvaluateAddition.SetFuzzyValue(Options_.Tolerance);
            _EvaluateAddition.Build();
            _ReconstructionEvaluated = _EvaluateAddition.IsDone();
            if (_ReconstructionEvaluated)
            {
                _ReconstructedShape = _EvaluateAddition.Shape();
            }
        }
        if (_ReconstructionEvaluated)
        {
            BRepAlgoAPI_Cut _Unexpected(_ReconstructedShape, Shape_);
            _Unexpected.SetFuzzyValue(Options_.Tolerance);
            _Unexpected.Build();
            BRepAlgoAPI_Cut _Missing(Shape_, _ReconstructedShape);
            _Missing.SetFuzzyValue(Options_.Tolerance);
            _Missing.Build();
            _ReconstructionEvaluated = _Unexpected.IsDone() && _Missing.IsDone();
            if (_ReconstructionEvaluated)
            {
                _Geometry.RelativeVolumeError = std::max(_InputVolume, _BaseVolume) > _VolumeTolerance
                    ? (_ShapeVolume(_Unexpected.Shape()) + _ShapeVolume(_Missing.Shape()))
                        / std::max(_InputVolume, _BaseVolume)
                    : 0.0;
            }
        }
    }
    if (!_ReconstructionEvaluated)
    {
        _Geometry.RelativeVolumeError = 1.0;
        _Geometry.Diagnostics.push_back(_MakeDiagnostic(
            "csg-validation-failed",
            "OCC could not evaluate the reconstructed CSG for equivalence validation",
            0.0));
    }
    _Geometry.Metadata["relativeVolumeError"] = _ToString(_Geometry.RelativeVolumeError);
    _Geometry.Metadata["relativeUnparameterizedVolume"] = _ToString(
        _Geometry.RelativeUnparameterizedVolume);
    _Geometry.Metadata["relativeOpaqueResidualVolume"] = _ToString(
        _Geometry.RelativeOpaqueResidualVolume);
    _Geometry.Confidence = _Clamp(
        0.50 * _AxisAreaRatio
            + 0.30 * (1.0 - _Clamp(_Geometry.RelativeUnparameterizedVolume, 0.0, 1.0))
            + 0.20 * (1.0 - _Clamp(_Geometry.RelativeVolumeError, 0.0, 1.0)),
        0.0,
        1.0);
    if (_AdditionVolume > _VolumeTolerance)
    {
        _Geometry.RecognitionStatus = Tube::ERecognitionStatus::Partial;
        _Geometry.Diagnostics.push_back(_MakeDiagnostic(
            "geometry-outside-base",
            "Some input geometry lies outside the recognized minimal extrusion and is preserved as residual",
            _Geometry.Confidence));
    }
    else if (_ResidualRemovalVolume > _VolumeTolerance)
    {
        _Geometry.RecognitionStatus = Tube::ERecognitionStatus::Partial;
        _Geometry.Diagnostics.push_back(_MakeDiagnostic(
            "composite-removal-fallback",
            "The standard extrusion and local analytic patches were recognized; the unresolved removed material is preserved as an exact semi-parametric composite volume",
            _Geometry.Confidence));
    }
    else if (_RemovalVolume > _VolumeTolerance)
    {
        _Geometry.RecognitionStatus = Tube::ERecognitionStatus::EquivalentButAmbiguous;
        _Geometry.Diagnostics.push_back(_MakeDiagnostic(
            _AlternativeCount > 0
                ? "optional-semantic-interpretations"
                : "equivalent-parameterized-csg",
            _AlternativeCount > 0
                ? "Multiple geometry-equivalent editable construction interpretations are preserved as optional candidates"
                : "The BRep is represented by an equivalent canonical CSG tree; original modeling history is not asserted",
            _Geometry.Confidence));
    }
    else
    {
        _Geometry.RecognitionStatus = Tube::ERecognitionStatus::Exact;
        _Geometry.Diagnostics.push_back(_MakeDiagnostic(
            "exact-standard-extrusion",
            "The BRep is equivalent to the recognized standard extrusion within tolerance",
            _Geometry.Confidence));
    }
    return _Result;
}

namespace
{
    namespace EvaluationTube = iCAX::GeometryData::Tube;

    struct SEvaluationFrame final
    {
        gp_Ax3 Axis;

        explicit SEvaluationFrame(
            IN const iCAX::GeometryData::Placement3& Frame_)
            : Axis(
                gp_Pnt(
                    Frame_.Location.X,
                    Frame_.Location.Y,
                    Frame_.Location.Z),
                gp_Dir(
                    Frame_.ZDirection.X,
                    Frame_.ZDirection.Y,
                    Frame_.ZDirection.Z),
                gp_Dir(
                    Frame_.XDirection.X,
                    Frame_.XDirection.Y,
                    Frame_.XDirection.Z))
        {
        }

        gp_Pnt Point(
            IN const iCAX::GeometryData::Point2& Point_,
            IN double dStation_,
            IN double dScale_ = 1.0,
            IN const iCAX::GeometryData::Point2& ScaleCenter_ = {}) const
        {
            const auto _X = ScaleCenter_.X
                + dScale_ * (Point_.X - ScaleCenter_.X);
            const auto _Y = ScaleCenter_.Y
                + dScale_ * (Point_.Y - ScaleCenter_.Y);
            return Axis.Location().Translated(
                gp_Vec(Axis.XDirection()) * _X
                + gp_Vec(Axis.YDirection()) * _Y
                + gp_Vec(Axis.Direction()) * dStation_);
        }

        gp_Dir Direction(
            IN const iCAX::GeometryData::Direction2& Direction_) const
        {
            return gp_Dir(
                gp_Vec(Axis.XDirection()) * Direction_.X
                + gp_Vec(Axis.YDirection()) * Direction_.Y);
        }

        gp_Pln Plane(IN double dStation_) const
        {
            return gp_Pln(gp_Ax3(
                Axis.Location().Translated(
                    gp_Vec(Axis.Direction()) * dStation_),
                Axis.Direction(),
                Axis.XDirection()));
        }
    };

    template<class TArrayValue_, class TValue_>
    TArrayValue_ _MakeOneBasedArray(IN const std::vector<TValue_>& Values_)
    {
        if (Values_.empty())
        {
            throw std::invalid_argument("Tube CSG curve array cannot be empty");
        }
        TArrayValue_ _Result(1, static_cast<int>(Values_.size()));
        for (std::size_t _Index = 0; _Index < Values_.size(); ++_Index)
        {
            _Result.SetValue(
                static_cast<int>(_Index + 1),
                Values_[_Index]);
        }
        return _Result;
    }

    std::vector<TopoDS_Edge> _MakeEvaluationEdges(
        IN const iCAX::GeometryData::CurveSegment2& Segment_,
        IN const SEvaluationFrame& Frame_,
        IN double dStation_,
        IN double dScale_,
        IN const iCAX::GeometryData::Point2& ScaleCenter_)
    {
        using namespace iCAX::GeometryData;
        std::vector<TopoDS_Edge> _Edges;
        const auto _AddEdge = [&_Edges, &Segment_](IN const TopoDS_Edge& Edge_) {
            if (Edge_.IsNull())
            {
                throw std::runtime_error("Tube CSG produced a null section edge");
            }
            auto _Oriented = Edge_;
            if (Segment_.Range.Reversed)
            {
                _Oriented.Reverse();
            }
            _Edges.push_back(std::move(_Oriented));
        };
        const auto _Point = [&Frame_, dStation_, dScale_, &ScaleCenter_](
            IN const Point2& Point_) {
            return Frame_.Point(Point_, dStation_, dScale_, ScaleCenter_);
        };
        const auto _CircleAxis = [
            &Frame_, dStation_, dScale_, &ScaleCenter_](
            IN const Placement2& Placement_) {
            return gp_Ax2(
                Frame_.Point(
                    Placement_.Location,
                    dStation_,
                    dScale_,
                    ScaleCenter_),
                Frame_.Axis.Direction(),
                Frame_.Direction(Placement_.XDirection));
        };

        std::visit([&](IN const auto& Curve_) {
            using T = std::decay_t<decltype(Curve_)>;
            if constexpr (std::is_same_v<T, Segment2>)
            {
                BRepBuilderAPI_MakeEdge _Maker(
                    _Point(Curve_.Start),
                    _Point(Curve_.End));
                if (!_Maker.IsDone())
                    throw std::runtime_error("Tube CSG cannot build a segment edge");
                _AddEdge(_Maker.Edge());
            }
            else if constexpr (std::is_same_v<T, Circle2>)
            {
                if (Curve_.Radius <= 0.0 || dScale_ <= 0.0)
                    throw std::invalid_argument("Tube CSG circle radius must be positive");
                const gp_Circ _Circle(
                    _CircleAxis(Curve_.Placement),
                    Curve_.Radius * dScale_);
                const auto _First = Segment_.Range.First;
                const auto _Last = Segment_.Range.Last;
                BRepBuilderAPI_MakeEdge _Maker(_Circle, _First, _Last);
                if (!_Maker.IsDone())
                    throw std::runtime_error("Tube CSG cannot build a circle edge");
                _AddEdge(_Maker.Edge());
            }
            else if constexpr (std::is_same_v<T, Arc2>)
            {
                if (Curve_.Basis.Radius <= 0.0 || dScale_ <= 0.0)
                    throw std::invalid_argument("Tube CSG arc radius must be positive");
                const gp_Circ _Circle(
                    _CircleAxis(Curve_.Basis.Placement),
                    Curve_.Basis.Radius * dScale_);
                BRepBuilderAPI_MakeEdge _Maker(
                    _Circle,
                    Curve_.StartAngle,
                    Curve_.EndAngle);
                if (!_Maker.IsDone())
                    throw std::runtime_error("Tube CSG cannot build an arc edge");
                auto _Edge = _Maker.Edge();
                if (!Curve_.CounterClockwise) _Edge.Reverse();
                _AddEdge(_Edge);
            }
            else if constexpr (std::is_same_v<T, Ellipse2>)
            {
                if (Curve_.MajorRadius <= 0.0
                    || Curve_.MinorRadius <= 0.0
                    || dScale_ <= 0.0)
                {
                    throw std::invalid_argument(
                        "Tube CSG ellipse radii must be positive");
                }
                const gp_Elips _Ellipse(
                    _CircleAxis(Curve_.Placement),
                    Curve_.MajorRadius * dScale_,
                    Curve_.MinorRadius * dScale_);
                BRepBuilderAPI_MakeEdge _Maker(
                    _Ellipse,
                    Segment_.Range.First,
                    Segment_.Range.Last);
                if (!_Maker.IsDone())
                    throw std::runtime_error("Tube CSG cannot build an ellipse edge");
                _AddEdge(_Maker.Edge());
            }
            else if constexpr (std::is_same_v<T, EllipseArc2>)
            {
                if (Curve_.Basis.MajorRadius <= 0.0
                    || Curve_.Basis.MinorRadius <= 0.0
                    || dScale_ <= 0.0)
                {
                    throw std::invalid_argument(
                        "Tube CSG ellipse arc radii must be positive");
                }
                const gp_Elips _Ellipse(
                    _CircleAxis(Curve_.Basis.Placement),
                    Curve_.Basis.MajorRadius * dScale_,
                    Curve_.Basis.MinorRadius * dScale_);
                BRepBuilderAPI_MakeEdge _Maker(
                    _Ellipse,
                    Curve_.StartAngle,
                    Curve_.EndAngle);
                if (!_Maker.IsDone())
                    throw std::runtime_error("Tube CSG cannot build an ellipse arc edge");
                auto _Edge = _Maker.Edge();
                if (!Curve_.CounterClockwise) _Edge.Reverse();
                _AddEdge(_Edge);
            }
            else if constexpr (std::is_same_v<T, Polyline2>)
            {
                if (Curve_.Points.size() < 2)
                    throw std::invalid_argument("Tube CSG polyline needs two points");
                for (std::size_t _Index = 1;
                    _Index < Curve_.Points.size();
                    ++_Index)
                {
                    BRepBuilderAPI_MakeEdge _Maker(
                        _Point(Curve_.Points[_Index - 1]),
                        _Point(Curve_.Points[_Index]));
                    if (!_Maker.IsDone())
                        throw std::runtime_error("Tube CSG cannot build a polyline edge");
                    _Edges.push_back(_Maker.Edge());
                }
                if (Curve_.Closed)
                {
                    BRepBuilderAPI_MakeEdge _Maker(
                        _Point(Curve_.Points.back()),
                        _Point(Curve_.Points.front()));
                    if (!_Maker.IsDone())
                        throw std::runtime_error("Tube CSG cannot close a polyline");
                    _Edges.push_back(_Maker.Edge());
                }
                if (Segment_.Range.Reversed)
                {
                    std::reverse(_Edges.begin(), _Edges.end());
                    for (auto& _Edge : _Edges) _Edge.Reverse();
                }
            }
            else if constexpr (std::is_same_v<T, Bezier2>)
            {
                std::vector<gp_Pnt> _Poles;
                _Poles.reserve(Curve_.Poles.size());
                for (const auto& _Pole : Curve_.Poles) _Poles.push_back(_Point(_Pole));
                const auto _PoleArray =
                    _MakeOneBasedArray<NCollection_Array1<gp_Pnt>>(_Poles);
                const occ::handle<Geom_BezierCurve> _Curve =
                    new Geom_BezierCurve(_PoleArray);
                BRepBuilderAPI_MakeEdge _Maker(
                    _Curve,
                    Segment_.Range.First,
                    Segment_.Range.Last);
                if (!_Maker.IsDone())
                    throw std::runtime_error("Tube CSG cannot build a Bezier edge");
                _AddEdge(_Maker.Edge());
            }
            else if constexpr (std::is_same_v<T, BSpline2>
                || std::is_same_v<T, NURBS2>)
            {
                if (Curve_.Degree < 1
                    || Curve_.Poles.empty()
                    || Curve_.Knots.empty()
                    || Curve_.Knots.size() != Curve_.Multiplicities.size())
                {
                    throw std::invalid_argument("Tube CSG spline definition is invalid");
                }
                std::vector<gp_Pnt> _Poles;
                _Poles.reserve(Curve_.Poles.size());
                for (const auto& _Pole : Curve_.Poles) _Poles.push_back(_Point(_Pole));
                const auto _PoleArray =
                    _MakeOneBasedArray<NCollection_Array1<gp_Pnt>>(_Poles);
                const auto _KnotArray =
                    _MakeOneBasedArray<NCollection_Array1<double>>(Curve_.Knots);
                const auto _MultiplicityArray =
                    _MakeOneBasedArray<NCollection_Array1<int>>(Curve_.Multiplicities);
                occ::handle<Geom_BSplineCurve> _Curve;
                if constexpr (std::is_same_v<T, NURBS2>)
                {
                    if (Curve_.Weights.size() != Curve_.Poles.size())
                        throw std::invalid_argument("Tube CSG NURBS weights are invalid");
                    const auto _WeightArray =
                        _MakeOneBasedArray<NCollection_Array1<double>>(Curve_.Weights);
                    _Curve = new Geom_BSplineCurve(
                        _PoleArray,
                        _WeightArray,
                        _KnotArray,
                        _MultiplicityArray,
                        Curve_.Degree,
                        Curve_.Periodic);
                }
                else
                {
                    _Curve = new Geom_BSplineCurve(
                        _PoleArray,
                        _KnotArray,
                        _MultiplicityArray,
                        Curve_.Degree,
                        Curve_.Periodic);
                }
                BRepBuilderAPI_MakeEdge _Maker(
                    _Curve,
                    Segment_.Range.First,
                    Segment_.Range.Last);
                if (!_Maker.IsDone())
                    throw std::runtime_error("Tube CSG cannot build a spline edge");
                _AddEdge(_Maker.Edge());
            }
            else
            {
                throw std::invalid_argument(
                    "Tube CSG section contains an unsupported unbounded or procedural curve");
            }
        }, Segment_.Curve);
        return _Edges;
    }

    TopoDS_Wire _MakeEvaluationWire(
        IN const EvaluationTube::SRegionLoop2& Loop_,
        IN const SEvaluationFrame& Frame_,
        IN double dStation_,
        IN double dScale_,
        IN const iCAX::GeometryData::Point2& ScaleCenter_)
    {
        if (Loop_.Curves.empty())
            throw std::invalid_argument("Tube CSG section loop is empty: " + Loop_.ID);
        BRepBuilderAPI_MakeWire _Wire;
        for (const auto& _Curve : Loop_.Curves)
        {
            for (const auto& _Edge : _MakeEvaluationEdges(
                _Curve.Segment,
                Frame_,
                dStation_,
                dScale_,
                ScaleCenter_))
            {
                _Wire.Add(_Edge);
            }
        }
        if (!_Wire.IsDone())
            throw std::runtime_error("Tube CSG cannot close section loop: " + Loop_.ID);
        return _Wire.Wire();
    }

    TopoDS_Face _MakeEvaluationFace(
        IN const EvaluationTube::CPlanarRegion2& Region_,
        IN const SEvaluationFrame& Frame_,
        IN double dStation_,
        IN double dScale_,
        IN const iCAX::GeometryData::Point2& ScaleCenter_)
    {
        if (Region_.Boundaries.empty())
            throw std::invalid_argument("Tube CSG planar region has no boundary");
        auto _Face = BRepBuilderAPI_MakeFace(
            Frame_.Plane(dStation_),
            _MakeEvaluationWire(
                Region_.Boundaries.front(),
                Frame_,
                dStation_,
                dScale_,
                ScaleCenter_),
            true);
        if (!_Face.IsDone())
            throw std::runtime_error("Tube CSG cannot build section face");
        for (std::size_t _Index = 1;
            _Index < Region_.Boundaries.size();
            ++_Index)
        {
            _Face.Add(_MakeEvaluationWire(
                Region_.Boundaries[_Index],
                Frame_,
                dStation_,
                dScale_,
                ScaleCenter_));
        }
        if (!_Face.IsDone())
            throw std::runtime_error("Tube CSG cannot add section cavities");
        ShapeFix_Face _Fixer(_Face.Face());
        _Fixer.FixOrientation();
        return _Fixer.Face();
    }

    TopoDS_Shape _MakePrism(
        IN const TopoDS_Face& StartFace_,
        IN const SEvaluationFrame& Frame_,
        IN double dLength_)
    {
        if (dLength_ <= 0.0)
            throw std::invalid_argument("Tube CSG extrusion length must be positive");
        BRepPrimAPI_MakePrism _Prism(
            StartFace_,
            gp_Vec(Frame_.Axis.Direction()) * dLength_,
            false,
            true);
        _Prism.Build();
        if (!_Prism.IsDone() || _Prism.Shape().IsNull())
            throw std::runtime_error("Tube CSG extrusion failed");
        return _Prism.Shape();
    }

    TopoDS_Shape _MakeLoopPrism(
        IN const EvaluationTube::SRegionLoop2& Loop_,
        IN const SEvaluationFrame& Frame_,
        IN double dFirst_,
        IN double dLast_)
    {
        EvaluationTube::CPlanarRegion2 _Region;
        _Region.Boundaries.push_back(Loop_);
        return _MakePrism(
            _MakeEvaluationFace(_Region, Frame_, dFirst_, 1.0, {}),
            Frame_,
            dLast_ - dFirst_);
    }

    TopoDS_Shape _MakeLoopLoft(
        IN const EvaluationTube::SRegionLoop2& Loop_,
        IN const SEvaluationFrame& Frame_,
        IN const EvaluationTube::STaperedRegionNode& Taper_,
        IN double dTolerance_)
    {
        BRepOffsetAPI_ThruSections _Loft(true, false, dTolerance_);
        _Loft.CheckCompatibility(false);
        _Loft.AddWire(_MakeEvaluationWire(
            Loop_,
            Frame_,
            Taper_.First,
            Taper_.FirstScale,
            Taper_.ScaleCenter));
        _Loft.AddWire(_MakeEvaluationWire(
            Loop_,
            Frame_,
            Taper_.Last,
            Taper_.LastScale,
            Taper_.ScaleCenter));
        _Loft.Build();
        if (!_Loft.IsDone() || _Loft.Shape().IsNull())
            throw std::runtime_error("Tube CSG tapered loft failed for loop: " + Loop_.ID);
        return _Loft.Shape();
    }

    const EvaluationTube::SSectionPrimitive* _FindPrimitiveForLoop(
        IN const std::vector<EvaluationTube::SSectionPrimitive>& Primitives_,
        IN const std::string& strLoopID_)
    {
        const auto _Iter = std::find_if(
            Primitives_.begin(),
            Primitives_.end(),
            [&strLoopID_](IN const auto& Primitive_) {
                return Primitive_.LoopID == strLoopID_;
            });
        return _Iter == Primitives_.end() ? nullptr : &*_Iter;
    }

    struct SSideSupportSample final
    {
        double U = 0.0;
        gp_Pnt Point;
        gp_Vec Tangent;
    };

    std::vector<SSideSupportSample> _SampleSideSupport(
        IN const EvaluationTube::SRegionLoop2& Loop_,
        IN const SEvaluationFrame& Frame_)
    {
        const auto _Wire = _MakeEvaluationWire(
            Loop_, Frame_, 0.0, 1.0, {});
        std::vector<SSideSupportSample> _Samples;
        constexpr int kSamplesPerEdge = 256;
        for (BRepTools_WireExplorer _Explorer(_Wire);
            _Explorer.More();
            _Explorer.Next())
        {
            const auto _Edge = _Explorer.Current();
            BRepAdaptor_Curve _Curve(_Edge);
            const auto _First = _Curve.FirstParameter();
            const auto _Last = _Curve.LastParameter();
            const auto _StartVertex = BRep_Tool::Pnt(_Explorer.CurrentVertex());
            gp_Pnt _FirstPoint;
            gp_Pnt _LastPoint;
            _Curve.D0(_First, _FirstPoint);
            _Curve.D0(_Last, _LastPoint);
            const auto _Reversed = _StartVertex.Distance(_LastPoint)
                < _StartVertex.Distance(_FirstPoint);
            for (int _Index = 0; _Index <= kSamplesPerEdge; ++_Index)
            {
                if (!_Samples.empty() && _Index == 0) continue;
                auto _Fraction = static_cast<double>(_Index)
                    / static_cast<double>(kSamplesPerEdge);
                if (_Reversed) _Fraction = 1.0 - _Fraction;
                gp_Pnt _Point;
                gp_Vec _Derivative;
                _Curve.D1(
                    _First + (_Last - _First) * _Fraction,
                    _Point,
                    _Derivative);
                if (_Reversed) _Derivative.Reverse();
                if (_Derivative.SquareMagnitude() <= 1.0e-24) continue;
                const auto _U = _Samples.empty()
                    ? 0.0
                    : _Samples.back().U + _Samples.back().Point.Distance(_Point);
                _Derivative.Normalize();
                _Samples.push_back({ _U, _Point, _Derivative });
            }
        }
        if (_Samples.size() < 3
            || _Samples.back().U <= Precision::Confusion())
        {
            throw std::runtime_error(
                "Tube CSG cannot sample wrapped-volume support loop: "
                + Loop_.ID);
        }
        return _Samples;
    }

    struct SWrappedSupportPoint final
    {
        gp_Pnt Point;
        gp_Vec OutwardNormal;
    };

    SWrappedSupportPoint _EvaluateSideSupport(
        IN const std::vector<SSideSupportSample>& Samples_,
        IN const SEvaluationFrame& Frame_,
        IN double dU_,
        IN double dV_)
    {
        const auto _Period = Samples_.back().U;
        auto _U = std::fmod(dU_, _Period);
        if (_U < 0.0) _U += _Period;
        const auto _Upper = std::upper_bound(
            Samples_.begin(),
            Samples_.end(),
            _U,
            [](IN double Value_, IN const SSideSupportSample& Sample_) {
                return Value_ < Sample_.U;
            });
        const auto _Right = _Upper == Samples_.end()
            ? Samples_.end() - 1
            : _Upper;
        const auto _Left = _Right == Samples_.begin()
            ? _Right
            : _Right - 1;
        const auto _Span = std::max(
            _Right->U - _Left->U,
            Precision::Confusion());
        const auto _Fraction = std::clamp(
            (_U - _Left->U) / _Span,
            0.0,
            1.0);
        gp_XYZ _Point = _Left->Point.XYZ() * (1.0 - _Fraction)
            + _Right->Point.XYZ() * _Fraction;
        _Point += gp_Vec(Frame_.Axis.Direction()).XYZ() * dV_;
        gp_Vec _Tangent = _Left->Tangent * (1.0 - _Fraction)
            + _Right->Tangent * _Fraction;
        if (_Tangent.SquareMagnitude() <= 1.0e-24)
            _Tangent = _Left->Tangent;
        _Tangent.Normalize();
        auto _Outward = _Tangent.Crossed(gp_Vec(Frame_.Axis.Direction()));
        if (_Outward.SquareMagnitude() <= 1.0e-24)
            throw std::runtime_error("Tube CSG wrapped support has a degenerate normal");
        _Outward.Normalize();
        return { gp_Pnt(_Point), _Outward };
    }

    double _EvaluateScalarField(
        IN const EvaluationTube::ScalarField2& Field_,
        IN double dU_,
        IN double dV_)
    {
        if (const auto* _pConstant = std::get_if<double>(&Field_))
            return *_pConstant;
        const auto& _Grid = std::get<EvaluationTube::SScalarGrid2>(Field_);
        if (_Grid.UCount < 2 || _Grid.VCount < 2
            || _Grid.Values.size()
                != static_cast<std::size_t>(_Grid.UCount) * _Grid.VCount
            || _Grid.ULast <= _Grid.UFirst
            || _Grid.VLast <= _Grid.VFirst)
        {
            throw std::invalid_argument("Tube CSG wrapped scalar grid is invalid");
        }
        const auto _UFraction = std::clamp(
            (dU_ - _Grid.UFirst) / (_Grid.ULast - _Grid.UFirst),
            0.0,
            1.0) * (_Grid.UCount - 1);
        const auto _VFraction = std::clamp(
            (dV_ - _Grid.VFirst) / (_Grid.VLast - _Grid.VFirst),
            0.0,
            1.0) * (_Grid.VCount - 1);
        const auto _U0 = static_cast<std::uint32_t>(std::floor(_UFraction));
        const auto _V0 = static_cast<std::uint32_t>(std::floor(_VFraction));
        const auto _U1 = std::min(_U0 + 1, _Grid.UCount - 1);
        const auto _V1 = std::min(_V0 + 1, _Grid.VCount - 1);
        const auto _TX = _UFraction - _U0;
        const auto _TY = _VFraction - _V0;
        const auto _Value = [&_Grid](IN std::uint32_t U_, IN std::uint32_t V_) {
            return _Grid.Values[static_cast<std::size_t>(V_) * _Grid.UCount + U_];
        };
        return (1.0 - _TY) * (
                (1.0 - _TX) * _Value(_U0, _V0)
                + _TX * _Value(_U1, _V0))
            + _TY * (
                (1.0 - _TX) * _Value(_U0, _V1)
                + _TX * _Value(_U1, _V1));
    }

    TopoDS_Face _MakeTriangleFace(
        IN const gp_Pnt& A_,
        IN const gp_Pnt& B_,
        IN const gp_Pnt& C_)
    {
        BRepBuilderAPI_MakePolygon _Polygon;
        _Polygon.Add(A_);
        _Polygon.Add(B_);
        _Polygon.Add(C_);
        _Polygon.Close();
        if (!_Polygon.IsDone())
            throw std::runtime_error("Tube CSG cannot build a wrapped triangle");
        BRepBuilderAPI_MakeFace _Face(_Polygon.Wire());
        if (!_Face.IsDone())
            throw std::runtime_error("Tube CSG cannot build a wrapped face");
        return _Face.Face();
    }

    class CTubeCSGEvaluator final
    {
    public:
        CTubeCSGEvaluator(
            IN const EvaluationTube::CTubeNeutralGeometry& Geometry_,
            IN const STubeCSGEvaluationOptions& Options_)
            : m_Geometry(Geometry_)
            , m_Options(Options_)
        {
            if (m_Options.Tolerance <= 0.0)
                throw std::invalid_argument("Tube CSG evaluation tolerance must be positive");
            for (const auto& _Node : m_Geometry.SolidNodes)
            {
                if (_Node.ID.empty() || !m_Nodes.emplace(_Node.ID, &_Node).second)
                    throw std::invalid_argument("Tube CSG contains an empty or duplicate node ID");
            }
        }

        STubeCSGEvaluationResult Evaluate()
        {
            STubeCSGEvaluationResult _Result;
            try
            {
                if (m_Geometry.RootNodeID.empty())
                    throw std::invalid_argument("Tube CSG root node ID is empty");
                _Result.Shape = EvaluateNode(m_Geometry.RootNodeID);
                if (_Result.Shape.IsNull())
                    throw std::runtime_error("Tube CSG root evaluated to an empty shape");
                _Result.bOK = true;
            }
            catch (const Standard_Failure& Error_)
            {
                _Result.Diagnostics.push_back(
                    std::string("OCC Tube CSG evaluation failed: ") + Error_.what());
            }
            catch (const std::exception& Error_)
            {
                _Result.Diagnostics.push_back(Error_.what());
            }

            if (_Result.bOK && m_Options.EvaluateAllConstructionBodies)
            {
                for (const auto& _Node : m_Geometry.SolidNodes)
                {
                    try
                    {
                        (void)EvaluateNode(_Node.ID);
                    }
                    catch (const std::exception& Error_)
                    {
                        _Result.Diagnostics.push_back(
                            "Construction preview '" + _Node.ID
                            + "' was not evaluated: " + Error_.what());
                    }
                }
            }
            _Result.NodeShapes = std::move(m_Shapes);
            _Result.SectionPrimitiveShapes = std::move(m_PrimitiveShapes);
            return _Result;
        }

    private:
        TopoDS_Shape EvaluateNode(IN const std::string& strNodeID_)
        {
            if (const auto _Cached = m_Shapes.find(strNodeID_);
                _Cached != m_Shapes.end())
            {
                return _Cached->second;
            }
            if (!m_Evaluating.insert(strNodeID_).second)
                throw std::runtime_error("Tube CSG contains a cycle at node: " + strNodeID_);
            const auto _NodeIter = m_Nodes.find(strNodeID_);
            if (_NodeIter == m_Nodes.end())
            {
                m_Evaluating.erase(strNodeID_);
                throw std::runtime_error("Tube CSG references a missing node: " + strNodeID_);
            }

            try
            {
                const auto& _Node = *_NodeIter->second;
                auto _Shape = std::visit([this, &_Node](IN const auto& Data_) {
                    return EvaluateData(_Node, Data_);
                }, _Node.Data);
                if (_Shape.IsNull())
                    throw std::runtime_error("Tube CSG node evaluated to empty: " + strNodeID_);
                m_Evaluating.erase(strNodeID_);
                m_Shapes[strNodeID_] = _Shape;
                return _Shape;
            }
            catch (...)
            {
                m_Evaluating.erase(strNodeID_);
                throw;
            }
        }

        TopoDS_Shape EvaluateData(
            IN const EvaluationTube::SSolidNode&,
            IN const EvaluationTube::SExtrudedRegionNode& Extrusion_)
        {
            if (Extrusion_.Last <= Extrusion_.First)
                throw std::invalid_argument("Tube CSG extrusion interval is invalid");
            const SEvaluationFrame _Frame(Extrusion_.Frame);
            const auto _Shape = _MakePrism(
                _MakeEvaluationFace(
                    Extrusion_.Section,
                    _Frame,
                    Extrusion_.First,
                    1.0,
                    {}),
                _Frame,
                Extrusion_.Last - Extrusion_.First);
            for (const auto& _Loop : Extrusion_.Section.Boundaries)
            {
                if (const auto* _pPrimitive = _FindPrimitiveForLoop(
                    Extrusion_.SectionPrimitives,
                    _Loop.ID))
                {
                    m_PrimitiveShapes[_pPrimitive->ID] = _MakeLoopPrism(
                        _Loop,
                        _Frame,
                        Extrusion_.First,
                        Extrusion_.Last);
                }
            }
            return _Shape;
        }

        TopoDS_Shape EvaluateData(
            IN const EvaluationTube::SSolidNode&,
            IN const EvaluationTube::STaperedRegionNode& Taper_)
        {
            if (Taper_.Section.Boundaries.empty()
                || Taper_.Last <= Taper_.First
                || Taper_.FirstScale <= 0.0
                || Taper_.LastScale <= 0.0)
            {
                throw std::invalid_argument("Tube CSG tapered region is invalid");
            }
            const SEvaluationFrame _Frame(Taper_.Frame);
            auto _Shape = _MakeLoopLoft(
                Taper_.Section.Boundaries.front(),
                _Frame,
                Taper_,
                m_Options.Tolerance);
            if (const auto* _pPrimitive = _FindPrimitiveForLoop(
                Taper_.SectionPrimitives,
                Taper_.Section.Boundaries.front().ID))
            {
                m_PrimitiveShapes[_pPrimitive->ID] = _Shape;
            }
            for (std::size_t _Index = 1;
                _Index < Taper_.Section.Boundaries.size();
                ++_Index)
            {
                const auto& _Loop = Taper_.Section.Boundaries[_Index];
                const auto _Cavity = _MakeLoopLoft(
                    _Loop,
                    _Frame,
                    Taper_,
                    m_Options.Tolerance);
                if (const auto* _pPrimitive = _FindPrimitiveForLoop(
                    Taper_.SectionPrimitives,
                    _Loop.ID))
                {
                    m_PrimitiveShapes[_pPrimitive->ID] = _Cavity;
                }
                BRepAlgoAPI_Cut _Cut(_Shape, _Cavity);
                _Cut.SetFuzzyValue(m_Options.Tolerance);
                _Cut.Build();
                if (!_Cut.IsDone())
                    throw std::runtime_error("Tube CSG cannot subtract tapered cavity");
                _Shape = _Cut.Shape();
            }
            return _Shape;
        }

        TopoDS_Shape EvaluateData(
            IN const EvaluationTube::SSolidNode&,
            IN const EvaluationTube::SHalfSpaceNode& HalfSpace_)
        {
            const gp_Pnt _Origin(
                HalfSpace_.Boundary.Location.X,
                HalfSpace_.Boundary.Location.Y,
                HalfSpace_.Boundary.Location.Z);
            const gp_Dir _Normal(
                HalfSpace_.Boundary.Normal.X,
                HalfSpace_.Boundary.Normal.Y,
                HalfSpace_.Boundary.Normal.Z);
            const gp_Dir _X(
                HalfSpace_.Boundary.XDirection.X,
                HalfSpace_.Boundary.XDirection.Y,
                HalfSpace_.Boundary.XDirection.Z);
            BRepBuilderAPI_MakeFace _PlaneFace(gp_Pln(gp_Ax3(_Origin, _Normal, _X)));
            if (!_PlaneFace.IsDone())
                throw std::runtime_error("Tube CSG cannot build half-space boundary");
            const auto _Inside = _Origin.Translated(
                gp_Vec(_Normal) * (HalfSpace_.KeepNegativeSide ? -1.0 : 1.0));
            BRepPrimAPI_MakeHalfSpace _HalfSpace(_PlaneFace.Face(), _Inside);
            _HalfSpace.Build();
            if (!_HalfSpace.IsDone())
                throw std::runtime_error("Tube CSG cannot build half-space");
            return _HalfSpace.Solid();
        }

        TopoDS_Shape EvaluateData(
            IN const EvaluationTube::SSolidNode&,
            IN const EvaluationTube::SBooleanNode& Boolean_)
        {
            if (Boolean_.Children.empty())
                throw std::invalid_argument("Tube CSG Boolean has no children");
            auto _Shape = EvaluateNode(Boolean_.Children.front());
            for (std::size_t _Index = 1;
                _Index < Boolean_.Children.size();
                ++_Index)
            {
                const auto _Right = EvaluateNode(Boolean_.Children[_Index]);
                if (Boolean_.Operation == EvaluationTube::EBooleanOperation::Union)
                {
                    BRepAlgoAPI_Fuse _Operation(_Shape, _Right);
                    _Operation.SetFuzzyValue(m_Options.Tolerance);
                    _Operation.Build();
                    if (!_Operation.IsDone())
                        throw std::runtime_error("Tube CSG union failed");
                    _Shape = _Operation.Shape();
                }
                else if (Boolean_.Operation == EvaluationTube::EBooleanOperation::Difference)
                {
                    BRepAlgoAPI_Cut _Operation(_Shape, _Right);
                    _Operation.SetFuzzyValue(m_Options.Tolerance);
                    _Operation.Build();
                    if (!_Operation.IsDone())
                        throw std::runtime_error("Tube CSG difference failed");
                    _Shape = _Operation.Shape();
                }
                else
                {
                    BRepAlgoAPI_Common _Operation(_Shape, _Right);
                    _Operation.SetFuzzyValue(m_Options.Tolerance);
                    _Operation.Build();
                    if (!_Operation.IsDone())
                        throw std::runtime_error("Tube CSG intersection failed");
                    _Shape = _Operation.Shape();
                }
            }
            return _Shape;
        }

        TopoDS_Shape EvaluateData(
            IN const EvaluationTube::SSolidNode&,
            IN const EvaluationTube::STransformNode& Transform_)
        {
            gp_GTrsf _Transform;
            for (int _Row = 1; _Row <= 3; ++_Row)
            {
                for (int _Column = 1; _Column <= 4; ++_Column)
                {
                    _Transform.SetValue(
                        _Row,
                        _Column,
                        Transform_.Transform.Matrix.Values[_Row - 1][_Column - 1]);
                }
            }
            BRepBuilderAPI_GTransform _Builder(
                EvaluateNode(Transform_.Child),
                _Transform,
                true);
            _Builder.Build();
            if (!_Builder.IsDone())
                throw std::runtime_error("Tube CSG transform failed");
            return _Builder.Shape();
        }

        TopoDS_Shape EvaluateData(
            IN const EvaluationTube::SSolidNode& Node_,
            IN const EvaluationTube::SWrappedVolumeNode& Wrapped_)
        {
            const auto _SupportNode = m_Nodes.find(
                Wrapped_.Support.ExtrusionNodeID);
            if (_SupportNode == m_Nodes.end())
            {
                throw std::invalid_argument(
                    "Tube CSG wrapped-volume support node is missing: "
                    + Wrapped_.Support.ExtrusionNodeID);
            }
            const auto* _pSupport = std::get_if<
                EvaluationTube::SExtrudedRegionNode>(
                    &_SupportNode->second->Data);
            if (!_pSupport)
            {
                throw std::invalid_argument(
                    "Tube CSG wrapped-volume support must be an extrusion: "
                    + Node_.ID);
            }
            const auto _Loop = std::find_if(
                _pSupport->Section.Boundaries.begin(),
                _pSupport->Section.Boundaries.end(),
                [&Wrapped_](IN const auto& Loop_) {
                    return Loop_.ID == Wrapped_.Support.LoopID;
                });
            if (_Loop == _pSupport->Section.Boundaries.end())
            {
                throw std::invalid_argument(
                    "Tube CSG wrapped-volume support loop is missing: "
                    + Wrapped_.Support.LoopID);
            }

            const SEvaluationFrame _SupportFrame(_pSupport->Frame);
            const auto _SupportSamples = _SampleSideSupport(
                *_Loop, _SupportFrame);
            iCAX::GeometryData::Placement3 _UVPlacement;
            const SEvaluationFrame _UVFrame(_UVPlacement);
            auto _UVFace = _MakeEvaluationFace(
                Wrapped_.UVRegion,
                _UVFrame,
                0.0,
                1.0,
                {});
            BRepMesh_IncrementalMesh _Mesh(
                _UVFace,
                std::max(0.01, m_Options.Tolerance),
                false,
                0.1,
                true);
            _Mesh.Perform();
            TopLoc_Location _Location;
            const auto _Triangulation = BRep_Tool::Triangulation(
                _UVFace, _Location);
            if (_Triangulation.IsNull()
                || _Triangulation->NbNodes() < 3
                || _Triangulation->NbTriangles() < 1)
            {
                throw std::runtime_error(
                    "Tube CSG cannot triangulate wrapped UV region: "
                    + Node_.ID);
            }

            struct SEdgeUse final
            {
                int Count = 0;
                int First = 0;
                int Second = 0;
            };
            std::map<std::pair<int, int>, SEdgeUse> _EdgeUses;
            std::vector<gp_Pnt> _UVNodes(
                static_cast<std::size_t>(_Triangulation->NbNodes()) + 1);
            for (int _Index = 1;
                _Index <= _Triangulation->NbNodes();
                ++_Index)
            {
                _UVNodes[_Index] = _Triangulation->Node(_Index)
                    .Transformed(_Location.Transformation());
            }
            using SUVTriangle = std::array<gp_Pnt, 3>;
            std::vector<SUVTriangle> _UVTriangles;
            _UVTriangles.reserve(_Triangulation->NbTriangles());
            std::vector<TopoDS_Face> _Faces;
            const auto _RecordEdge = [&_EdgeUses](IN int A_, IN int B_) {
                const auto _Key = std::minmax(A_, B_);
                auto& _Use = _EdgeUses[_Key];
                ++_Use.Count;
                if (_Use.Count == 1)
                {
                    _Use.First = A_;
                    _Use.Second = B_;
                }
            };
            for (int _Index = 1;
                _Index <= _Triangulation->NbTriangles();
                ++_Index)
            {
                int _A = 0;
                int _B = 0;
                int _C = 0;
                _Triangulation->Triangle(_Index).Get(_A, _B, _C);
                _UVTriangles.push_back({
                    _UVNodes[_A], _UVNodes[_B], _UVNodes[_C]
                });
                _RecordEdge(_A, _B);
                _RecordEdge(_B, _C);
                _RecordEdge(_C, _A);
            }

            auto _MaximumUSpan = 0.0;
            auto _MaximumVSpan = 0.0;
            for (const auto& _Triangle : _UVTriangles)
            {
                for (std::size_t _A = 0; _A < 3; ++_A)
                {
                    const auto _B = (_A + 1) % 3;
                    _MaximumUSpan = std::max(
                        _MaximumUSpan,
                        std::abs(_Triangle[_A].X() - _Triangle[_B].X()));
                    _MaximumVSpan = std::max(
                        _MaximumVSpan,
                        std::abs(_Triangle[_A].Y() - _Triangle[_B].Y()));
                }
            }
            const auto _TargetUSpan = std::max(
                _SupportSamples.back().U / 32.0,
                0.5);
            constexpr double kTargetVSpan = 20.0;
            int _SubdivisionLevel = 0;
            while (_SubdivisionLevel < 3
                && (_MaximumUSpan > _TargetUSpan
                    || _MaximumVSpan > kTargetVSpan))
            {
                _MaximumUSpan *= 0.5;
                _MaximumVSpan *= 0.5;
                ++_SubdivisionLevel;
            }
            for (int _Level = 0; _Level < _SubdivisionLevel; ++_Level)
            {
                std::vector<SUVTriangle> _Subdivided;
                _Subdivided.reserve(_UVTriangles.size() * 4);
                for (const auto& _Triangle : _UVTriangles)
                {
                    const gp_Pnt _AB(
                        0.5 * (_Triangle[0].XYZ() + _Triangle[1].XYZ()));
                    const gp_Pnt _BC(
                        0.5 * (_Triangle[1].XYZ() + _Triangle[2].XYZ()));
                    const gp_Pnt _CA(
                        0.5 * (_Triangle[2].XYZ() + _Triangle[0].XYZ()));
                    _Subdivided.push_back({ _Triangle[0], _AB, _CA });
                    _Subdivided.push_back({ _AB, _Triangle[1], _BC });
                    _Subdivided.push_back({ _CA, _BC, _Triangle[2] });
                    _Subdivided.push_back({ _AB, _BC, _CA });
                }
                _UVTriangles = std::move(_Subdivided);
            }

            const auto _MapOffsets = [
                &_SupportSamples,
                &_SupportFrame,
                &Wrapped_](IN const gp_Pnt& UV_) {
                const auto _Support = _EvaluateSideSupport(
                    _SupportSamples,
                    _SupportFrame,
                    UV_.X(),
                    UV_.Y());
                const auto _LowerOffset = _EvaluateScalarField(
                    Wrapped_.LowerNormalOffset,
                    UV_.X(),
                    UV_.Y());
                const auto _UpperOffset = _EvaluateScalarField(
                    Wrapped_.UpperNormalOffset,
                    UV_.X(),
                    UV_.Y());
                if (_LowerOffset > _UpperOffset)
                {
                    throw std::invalid_argument(
                        "Tube CSG wrapped lower offset exceeds upper offset");
                }
                return std::make_pair(
                    _Support.Point.Translated(
                        _Support.OutwardNormal * _LowerOffset),
                    _Support.Point.Translated(
                        _Support.OutwardNormal * _UpperOffset));
            };
            _Faces.reserve(_UVTriangles.size() * 2);
            for (const auto& _Triangle : _UVTriangles)
            {
                const auto _A = _MapOffsets(_Triangle[0]);
                const auto _B = _MapOffsets(_Triangle[1]);
                const auto _C = _MapOffsets(_Triangle[2]);
                _Faces.push_back(_MakeTriangleFace(
                    _A.first, _C.first, _B.first));
                _Faces.push_back(_MakeTriangleFace(
                    _A.second, _B.second, _C.second));
            }

            const auto _BoundarySubdivisionCount = 1 << _SubdivisionLevel;
            for (const auto& [_Key, _Use] : _EdgeUses)
            {
                (void)_Key;
                if (_Use.Count != 1) continue;
                const auto& _Start = _UVNodes[_Use.First];
                const auto& _End = _UVNodes[_Use.Second];
                for (int _Index = 0;
                    _Index < _BoundarySubdivisionCount;
                    ++_Index)
                {
                    const auto _FirstFraction = static_cast<double>(_Index)
                        / _BoundarySubdivisionCount;
                    const auto _SecondFraction = static_cast<double>(_Index + 1)
                        / _BoundarySubdivisionCount;
                    const gp_Pnt _UVFirst(
                        _Start.XYZ() * (1.0 - _FirstFraction)
                        + _End.XYZ() * _FirstFraction);
                    const gp_Pnt _UVSecond(
                        _Start.XYZ() * (1.0 - _SecondFraction)
                        + _End.XYZ() * _SecondFraction);
                    const auto _First = _MapOffsets(_UVFirst);
                    const auto _Second = _MapOffsets(_UVSecond);
                    _Faces.push_back(_MakeTriangleFace(
                        _First.first, _Second.first, _Second.second));
                    _Faces.push_back(_MakeTriangleFace(
                        _First.first, _Second.second, _First.second));
                }
            }

            BRepBuilderAPI_Sewing _Sewing(
                std::max(10.0 * m_Options.Tolerance, 1.0e-5),
                true,
                true,
                true,
                false);
            for (const auto& _Face : _Faces) _Sewing.Add(_Face);
            _Sewing.Perform();
            const auto _Sewed = _Sewing.SewedShape();
            if (_Sewed.IsNull())
                throw std::runtime_error("Tube CSG wrapped-volume sewing failed");
            if (_Sewed.ShapeType() == TopAbs_SOLID)
                return _Sewed;
            TopoDS_Shell _Shell;
            if (_Sewed.ShapeType() == TopAbs_SHELL)
            {
                _Shell = TopoDS::Shell(_Sewed);
            }
            else
            {
                TopExp_Explorer _Shells(_Sewed, TopAbs_SHELL);
                if (!_Shells.More())
                    throw std::runtime_error("Tube CSG wrapped volume has no closed shell");
                _Shell = TopoDS::Shell(_Shells.Current());
                _Shells.Next();
                if (_Shells.More())
                    throw std::runtime_error("Tube CSG wrapped volume produced multiple shells");
            }
            BRepBuilderAPI_MakeSolid _Solid(_Shell);
            _Solid.Build();
            if (!_Solid.IsDone() || _Solid.Shape().IsNull())
                throw std::runtime_error("Tube CSG wrapped-volume solidification failed");
            return _Solid.Shape();
        }

        TopoDS_Shape EvaluateData(
            IN const EvaluationTube::SSolidNode& Node_,
            IN const EvaluationTube::SOpeningProfileSweepNode& Opening_)
        {
            if (Opening_.EvaluatedVolumeNodeID.empty())
            {
                throw std::invalid_argument(
                    "Tube CSG opening profile has no evaluated volume: "
                    + Node_.ID);
            }
            return EvaluateNode(Opening_.EvaluatedVolumeNodeID);
        }

        TopoDS_Shape EvaluateExternal(
            IN const std::string& strResourceID_,
            IN const std::string& strNodeID_)
        {
            const auto _Iter = m_Options.ExternalBRepShapes.find(strResourceID_);
            if (_Iter == m_Options.ExternalBRepShapes.end()
                || _Iter->second.IsNull())
            {
                throw std::runtime_error(
                    "Tube CSG external BRep is unavailable for node '"
                    + strNodeID_ + "': " + strResourceID_);
            }
            return _Iter->second;
        }

        TopoDS_Shape EvaluateData(
            IN const EvaluationTube::SSolidNode& Node_,
            IN const EvaluationTube::SCompositeVolumeNode& Composite_)
        {
            return EvaluateExternal(Composite_.BRepResourceID, Node_.ID);
        }

        TopoDS_Shape EvaluateData(
            IN const EvaluationTube::SSolidNode& Node_,
            IN const EvaluationTube::SResidualBRepNode& Residual_)
        {
            return EvaluateExternal(Residual_.BRepResourceID, Node_.ID);
        }

        TopoDS_Shape EvaluateData(
            IN const EvaluationTube::SSolidNode& Node_,
            IN const EvaluationTube::SAlternativeNode& Alternative_)
        {
            if (Alternative_.Candidates.empty())
                throw std::invalid_argument("Tube CSG Alternative has no candidates: " + Node_.ID);
            auto _Candidate = Alternative_.Candidates.begin();
            const auto _Selection = !Alternative_.SelectedCandidateID.empty()
                ? Alternative_.SelectedCandidateID
                : Alternative_.RecommendedCandidateID;
            if (!_Selection.empty())
            {
                const auto _Selected = std::find_if(
                    Alternative_.Candidates.begin(),
                    Alternative_.Candidates.end(),
                    [&_Selection](IN const auto& Candidate_) {
                        return Candidate_.ID == _Selection;
                    });
                if (_Selected == Alternative_.Candidates.end())
                    throw std::invalid_argument("Tube CSG Alternative selection is missing: " + _Selection);
                _Candidate = _Selected;
            }
            return EvaluateNode(_Candidate->CandidateNodeID);
        }

        const EvaluationTube::CTubeNeutralGeometry& m_Geometry;
        const STubeCSGEvaluationOptions& m_Options;
        std::map<std::string, const EvaluationTube::SSolidNode*> m_Nodes;
        std::map<std::string, TopoDS_Shape> m_Shapes;
        std::map<std::string, TopoDS_Shape> m_PrimitiveShapes;
        std::set<std::string> m_Evaluating;
    };
}

STubeCSGEvaluationResult EvaluateTubeCSG(
    IN const iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_,
    IN const STubeCSGEvaluationOptions& Options_)
{
    try
    {
        return CTubeCSGEvaluator(Geometry_, Options_).Evaluate();
    }
    catch (const Standard_Failure& Error_)
    {
        STubeCSGEvaluationResult _Result;
        _Result.Diagnostics.push_back(
            std::string("OCC Tube CSG setup failed: ") + Error_.what());
        return _Result;
    }
    catch (const std::exception& Error_)
    {
        STubeCSGEvaluationResult _Result;
        _Result.Diagnostics.push_back(Error_.what());
        return _Result;
    }
}
} // namespace iCAX::OpenCascade
