#include "pch.h"
#include "OpenCascadeBRepReader.h"
#include "OpenCascadeTubeCSGConverter.h"

#include "GeometryData/GeometryData.h"
#include "GeometryData/TubeNeutralGeometry.h"
#include "Resources/BinaryResource.h"
#include "Resources/ResourceFlatBuffer.h"
#include "Resources/ResourceImportExport.h"
#include "Resources/ResourceInfo.h"
#include "Resources/ResourceLibrary.h"
#include "Resources/ResourceLoaderRegistry.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4005)
#endif


#ifdef _MSC_VER
#pragma warning(pop)
#endif


namespace
{
    constexpr const char* kImporterID = "opencascade";
    constexpr const char* kOccVersion = "8.0.0-p1";
    constexpr const char* kStepFormatID = "cad.step";
    constexpr const char* kIgesFormatID = "cad.iges";
    constexpr const char* kSourceRole = "source";
    constexpr const char* kBRepRole = "geometry.brep";
    constexpr const char* kTubeNeutralRole = "tube.neutral_geometry";
    constexpr const char* kTubeRemovalRole = "tube.csg.composite.removal";
    constexpr const char* kTubeAdditionRole = "tube.csg.residual.addition";
    constexpr const char* kTubePreviewRole = "tube.csg.preview";
    constexpr const char* kTubePreviewMediaType =
        "application/vnd.icax.flatbuffer";
    constexpr const char* kTubePreviewFlatBufferIdentifier = "T1PM";
    constexpr const char* kBinaryResourceType = "resource.binary";
    constexpr const char* kBRepResourceType = "geometry.brep";

    using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

    struct SShapeMaps final
    {
        ShapeMap Vertices;
        ShapeMap Edges;
        ShapeMap Faces;
        ShapeMap Shells;
        ShapeMap Solids;
        ShapeMap CompSolids;
        ShapeMap Compounds;
    };

    std::string ToLowerASCII(IN std::string strText_)
    {
        std::transform(strText_.begin(), strText_.end(), strText_.begin(), [](IN const unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });
        return strText_;
    }

    std::string GetExtension(IN const std::string& strSourcePath_)
    {
        return ToLowerASCII(std::filesystem::path(strSourcePath_).extension().string());
    }

    bool IsStepPath(IN const std::string& strSourcePath_)
    {
        const auto _Extension = GetExtension(strSourcePath_);
        return _Extension == ".step" || _Extension == ".stp";
    }

    bool IsIgesPath(IN const std::string& strSourcePath_)
    {
        const auto _Extension = GetExtension(strSourcePath_);
        return _Extension == ".igs" || _Extension == ".iges";
    }

    std::string GetFormatIDFromPath(IN const std::string& strSourcePath_)
    {
        if (IsStepPath(strSourcePath_))
        {
            return kStepFormatID;
        }
        if (IsIgesPath(strSourcePath_))
        {
            return kIgesFormatID;
        }
        return {};
    }

    bool IsSupportedFormatID(IN const std::string& strFormatID_)
    {
        return strFormatID_.empty()
            || strFormatID_ == kStepFormatID
            || strFormatID_ == kIgesFormatID
            || strFormatID_ == "cad";
    }

    bool IsSupportedCadPath(IN const std::string& strSourcePath_)
    {
        return IsStepPath(strSourcePath_) || IsIgesPath(strSourcePath_);
    }

    std::string GetDisplayNameFromPath(IN const std::string& strSourcePath_)
    {
        const auto _Name = std::filesystem::path(strSourcePath_).filename().string();
        return _Name.empty() ? strSourcePath_ : _Name;
    }

    std::string MakeSourceResourceID(IN const iCAX::Resource::CResourceImportRequest& Request_)
    {
        return Request_.TargetResourceID.empty() ? Request_.SourcePath : Request_.TargetResourceID;
    }

    std::string MakeBRepResourceID(
        IN iCAX::Resource::CResourceLibrary& Library_,
        IN const std::string& strSourceResourceID_)
    {
        if (strSourceResourceID_.empty())
        {
            return {};
        }
        return Library_.HasScope()
            ? Library_.MakeDerivedResourceURL(
                strSourceResourceID_,
                "geometry.brep")
            : strSourceResourceID_ + "#geometry.brep";
    }

    std::string MakeDerivedResourceID(
        IN iCAX::Resource::CResourceLibrary& Library_,
        IN const std::string& strSourceResourceID_,
        IN const std::string& strSuffix_)
    {
        if (strSourceResourceID_.empty())
        {
            return {};
        }
        return Library_.HasScope()
            ? Library_.MakeDerivedResourceURL(strSourceResourceID_, strSuffix_)
            : strSourceResourceID_ + "#" + strSuffix_;
    }

    std::vector<uint8_t> ReadAllBytes(IN const std::string& strSourcePath_)
    {
        std::ifstream _Input(std::filesystem::path(strSourcePath_), std::ios::binary);
        if (!_Input)
        {
            throw std::runtime_error("OpenCascade resource import cannot open source file: " + strSourcePath_);
        }

        _Input.seekg(0, std::ios::end);
        const auto _Size = _Input.tellg();
        if (_Size < 0)
        {
            throw std::runtime_error("OpenCascade resource import cannot determine source file size: " + strSourcePath_);
        }

        std::vector<uint8_t> _Bytes(static_cast<size_t>(_Size));
        _Input.seekg(0, std::ios::beg);
        if (!_Bytes.empty())
        {
            _Input.read(reinterpret_cast<char*>(_Bytes.data()), static_cast<std::streamsize>(_Bytes.size()));
            if (!_Input)
            {
                throw std::runtime_error("OpenCascade resource import cannot read source file: " + strSourcePath_);
            }
        }
        return _Bytes;
    }

    uint64_t HashBytes(IN const std::vector<uint8_t>& Bytes_)
    {
        uint64_t _Hash = 1469598103934665603ull;
        for (const auto _Byte : Bytes_)
        {
            _Hash ^= static_cast<uint64_t>(_Byte);
            _Hash *= 1099511628211ull;
        }
        return _Hash;
    }

    std::string ToHex(IN const uint64_t nValue_)
    {
        std::ostringstream _Stream;
        _Stream << std::hex << std::setw(16) << std::setfill('0') << nValue_;
        return _Stream.str();
    }

    iCAX::Resource::CFlatBufferResource MakeTubePreviewMeshResource(
        IN const iCAX::GeometryData::BRepModel& BRep_,
        IN const std::string& strNodeID_)
    {
        std::vector<float> _Positions;
        std::vector<uint32_t> _Indices;
        std::size_t _VertexCount = 0;
        for (const auto& _Record : BRep_.Triangulations3)
        {
            const auto& _Mesh = _Record.Geometry;
            if (_VertexCount + _Mesh.Vertices.size()
                > std::numeric_limits<uint32_t>::max())
            {
                throw std::overflow_error(
                    "Tube preview mesh exceeds the uint32 index range");
            }
            _Positions.reserve(
                _Positions.size() + _Mesh.Vertices.size() * 3);
            _Indices.reserve(
                _Indices.size() + _Mesh.Triangles.size() * 3);
            for (const auto& _Vertex : _Mesh.Vertices)
            {
                _Positions.push_back(static_cast<float>(_Vertex.X));
                _Positions.push_back(static_cast<float>(_Vertex.Y));
                _Positions.push_back(static_cast<float>(_Vertex.Z));
            }
            for (const auto& _Triangle : _Mesh.Triangles)
            {
                _Indices.push_back(static_cast<uint32_t>(
                    _VertexCount + _Triangle[0]));
                _Indices.push_back(static_cast<uint32_t>(
                    _VertexCount + _Triangle[1]));
                _Indices.push_back(static_cast<uint32_t>(
                    _VertexCount + _Triangle[2]));
            }
            _VertexCount += _Mesh.Vertices.size();
        }

        if (_Positions.empty() || _Indices.empty())
        {
            return {};
        }

        flatbuffers::FlatBufferBuilder _Builder;
        const auto _PositionVector = _Builder.CreateVector(_Positions);
        const auto _IndexVector = _Builder.CreateVector(_Indices);
        const auto _NodeID = _Builder.CreateString(strNodeID_);
        const auto _Table = _Builder.StartTable();
        _Builder.AddElement<uint32_t>(4, 1, 0);
        _Builder.AddOffset(6, _PositionVector);
        _Builder.AddOffset(8, _IndexVector);
        _Builder.AddOffset(10, _NodeID);
        const auto _Root = _Builder.EndTable(_Table);
        _Builder.Finish(
            flatbuffers::Offset<void>(_Root),
            kTubePreviewFlatBufferIdentifier);
        return iCAX::Resource::MakeFlatBufferResource(_Builder);
    }

    uint64_t NextResourceVersion(IN iCAX::Resource::CResourceLibrary& Resources_, IN const std::string& strResourceID_)
    {
        const auto _PreviousVersion = Resources_.GetVersion(strResourceID_);
        return _PreviousVersion == 0 ? 1 : _PreviousVersion + 1;
    }

    iCAX::Resource::CResourceInfo MakeResourceInfo(
        IN const std::string& strResourceID_,
        IN const std::string& strName_,
        IN const std::string& strKind_,
        IN iCAX::Resource::EResourcePersistenceMode Persistence_,
        IN const uint64_t nVersion_,
        IN const uint64_t nSize_,
        IN const std::string& strContentHash_,
        IN const std::string& strFormatID_)
    {
        iCAX::Resource::CResourceInfo _Info;
        _Info.Source = strResourceID_;
        _Info.Name = strName_;
        _Info.Persistence = Persistence_;
        _Info.nVersion = nVersion_;
        _Info.nSize = nSize_;
        _Info.ContentHash = strContentHash_;
        _Info.Metadata["kind"] = strKind_;
        _Info.Metadata["formatId"] = strFormatID_;
        _Info.Metadata["importer"] = kImporterID;
        _Info.Metadata["occtVersion"] = kOccVersion;
        return _Info;
    }

    iCAX::GeometryData::Point3 ToPoint3(IN const gp_Pnt& Point_)
    {
        return { Point_.X(), Point_.Y(), Point_.Z() };
    }

    iCAX::GeometryData::Direction3 ToDirection3(IN const gp_Dir& Direction_)
    {
        return { Direction_.X(), Direction_.Y(), Direction_.Z() };
    }

    iCAX::GeometryData::Point2 ToPoint2(IN const gp_Pnt2d& Point_)
    {
        return { Point_.X(), Point_.Y() };
    }

    iCAX::GeometryData::Direction2 ToDirection2(IN const gp_Dir2d& Direction_)
    {
        return { Direction_.X(), Direction_.Y() };
    }

    iCAX::GeometryData::Placement2 ToPlacement2(IN const gp_Ax22d& Axis_)
    {
        iCAX::GeometryData::Placement2 _Placement;
        _Placement.Location = ToPoint2(Axis_.Location());
        _Placement.XDirection = ToDirection2(Axis_.XDirection());
        _Placement.YDirection = ToDirection2(Axis_.YDirection());
        return _Placement;
    }

    iCAX::GeometryData::Placement3 ToPlacement3(IN const gp_Ax2& Axis_)
    {
        iCAX::GeometryData::Placement3 _Placement;
        _Placement.Location = ToPoint3(Axis_.Location());
        _Placement.XDirection = ToDirection3(Axis_.XDirection());
        _Placement.YDirection = ToDirection3(Axis_.YDirection());
        _Placement.ZDirection = ToDirection3(Axis_.Direction());
        return _Placement;
    }

    iCAX::GeometryData::Placement3 ToPlacement3(IN const gp_Ax3& Axis_)
    {
        iCAX::GeometryData::Placement3 _Placement;
        _Placement.Location = ToPoint3(Axis_.Location());
        _Placement.XDirection = ToDirection3(Axis_.XDirection());
        _Placement.YDirection = ToDirection3(Axis_.YDirection());
        _Placement.ZDirection = ToDirection3(Axis_.Direction());
        return _Placement;
    }

    iCAX::GeometryData::ETopologyOrientation ToOrientation(IN const TopAbs_Orientation Orientation_)
    {
        using iCAX::GeometryData::ETopologyOrientation;
        switch (Orientation_)
        {
        case TopAbs_FORWARD:
            return ETopologyOrientation::Forward;
        case TopAbs_REVERSED:
            return ETopologyOrientation::Reversed;
        case TopAbs_INTERNAL:
            return ETopologyOrientation::Internal;
        case TopAbs_EXTERNAL:
            return ETopologyOrientation::External;
        default:
            return ETopologyOrientation::Forward;
        }
    }

    iCAX::GeometryData::EBRepShapeKind ToShapeKind(IN const TopAbs_ShapeEnum ShapeKind_)
    {
        using iCAX::GeometryData::EBRepShapeKind;
        switch (ShapeKind_)
        {
        case TopAbs_VERTEX:
            return EBRepShapeKind::Vertex;
        case TopAbs_EDGE:
            return EBRepShapeKind::Edge;
        case TopAbs_WIRE:
            return EBRepShapeKind::Wire;
        case TopAbs_FACE:
            return EBRepShapeKind::Face;
        case TopAbs_SHELL:
            return EBRepShapeKind::Shell;
        case TopAbs_SOLID:
            return EBRepShapeKind::Solid;
        case TopAbs_COMPSOLID:
            return EBRepShapeKind::CompSolid;
        case TopAbs_COMPOUND:
            return EBRepShapeKind::Compound;
        default:
            return EBRepShapeKind::Compound;
        }
    }

    iCAX::GeometryData::ParameterRange MakeRange(IN const double dFirst_, IN const double dLast_)
    {
        return { dFirst_, dLast_, dLast_ < dFirst_ };
    }

    iCAX::GeometryData::Curve3 ToCurve3(IN BRepAdaptor_Curve& Curve_)
    {
        using namespace iCAX::GeometryData;

        switch (Curve_.GetType())
        {
        case GeomAbs_Line:
        {
            const auto _Line = Curve_.Line();
            Line3 _Result;
            _Result.Axis.Location = ToPoint3(_Line.Location());
            _Result.Axis.Direction = ToDirection3(_Line.Direction());
            return _Result;
        }
        case GeomAbs_Circle:
        {
            const auto _Circle = Curve_.Circle();
            Circle3 _Result;
            _Result.Placement = ToPlacement3(_Circle.Position());
            _Result.Radius = _Circle.Radius();
            return _Result;
        }
        case GeomAbs_Ellipse:
        {
            const auto _Ellipse = Curve_.Ellipse();
            Ellipse3 _Result;
            _Result.Placement = ToPlacement3(_Ellipse.Position());
            _Result.MajorRadius = _Ellipse.MajorRadius();
            _Result.MinorRadius = _Ellipse.MinorRadius();
            return _Result;
        }
        case GeomAbs_BezierCurve:
        {
            const auto _Bezier = Curve_.Bezier();
            Bezier3 _Result;
            if (!_Bezier.IsNull())
            {
                for (int _Index = 1; _Index <= _Bezier->NbPoles(); ++_Index)
                {
                    _Result.Poles.push_back(ToPoint3(_Bezier->Pole(_Index)));
                }
            }
            return _Result;
        }
        case GeomAbs_BSplineCurve:
        {
            const auto _BSpline = Curve_.BSpline();
            if (!_BSpline.IsNull() && _BSpline->IsRational())
            {
                NURBS3 _Result;
                _Result.Degree = _BSpline->Degree();
                _Result.Periodic = _BSpline->IsPeriodic();
                for (int _Index = 1; _Index <= _BSpline->NbPoles(); ++_Index)
                {
                    _Result.Poles.push_back(ToPoint3(_BSpline->Pole(_Index)));
                    _Result.Weights.push_back(_BSpline->Weight(_Index));
                }
                for (int _Index = 1; _Index <= _BSpline->NbKnots(); ++_Index)
                {
                    _Result.Knots.push_back(_BSpline->Knot(_Index));
                    _Result.Multiplicities.push_back(_BSpline->Multiplicity(_Index));
                }
                return _Result;
            }

            BSpline3 _Result;
            if (!_BSpline.IsNull())
            {
                _Result.Degree = _BSpline->Degree();
                _Result.Periodic = _BSpline->IsPeriodic();
                for (int _Index = 1; _Index <= _BSpline->NbPoles(); ++_Index)
                {
                    _Result.Poles.push_back(ToPoint3(_BSpline->Pole(_Index)));
                }
                for (int _Index = 1; _Index <= _BSpline->NbKnots(); ++_Index)
                {
                    _Result.Knots.push_back(_BSpline->Knot(_Index));
                    _Result.Multiplicities.push_back(_BSpline->Multiplicity(_Index));
                }
            }
            return _Result;
        }
        default:
        {
            Polyline3 _Result;
            const auto _First = Curve_.FirstParameter();
            const auto _Last = Curve_.LastParameter();
            constexpr int _SampleCount = 32;
            for (int _Index = 0; _Index <= _SampleCount; ++_Index)
            {
                const double _T = _First + (_Last - _First) * static_cast<double>(_Index) / static_cast<double>(_SampleCount);
                _Result.Points.push_back(ToPoint3(Curve_.Value(_T)));
            }
            return _Result;
        }
        }
    }

    iCAX::GeometryData::Curve2 ToCurve2(IN BRepAdaptor_Curve2d& Curve_)
    {
        using namespace iCAX::GeometryData;
        switch (Curve_.GetType())
        {
        case GeomAbs_Line:
        {
            const auto _Line = Curve_.Line();
            Line2 _Result;
            _Result.Axis.Location = ToPoint2(_Line.Location());
            _Result.Axis.Direction = ToDirection2(_Line.Direction());
            return _Result;
        }
        case GeomAbs_Circle:
        {
            const auto _Circle = Curve_.Circle();
            Circle2 _Result;
            _Result.Placement = ToPlacement2(_Circle.Position());
            _Result.Radius = _Circle.Radius();
            return _Result;
        }
        case GeomAbs_Ellipse:
        {
            const auto _Ellipse = Curve_.Ellipse();
            Ellipse2 _Result;
            _Result.Placement = ToPlacement2(_Ellipse.Axis());
            _Result.MajorRadius = _Ellipse.MajorRadius();
            _Result.MinorRadius = _Ellipse.MinorRadius();
            return _Result;
        }
        case GeomAbs_BezierCurve:
        {
            const auto _Bezier = Curve_.Bezier();
            Bezier2 _Result;
            if (!_Bezier.IsNull())
            {
                for (int _Index = 1; _Index <= _Bezier->NbPoles(); ++_Index)
                    _Result.Poles.push_back(ToPoint2(_Bezier->Pole(_Index)));
            }
            return _Result;
        }
        case GeomAbs_BSplineCurve:
        {
            const auto _BSpline = Curve_.BSpline();
            if (!_BSpline.IsNull() && _BSpline->IsRational())
            {
                NURBS2 _Result;
                _Result.Degree = _BSpline->Degree();
                _Result.Periodic = _BSpline->IsPeriodic();
                for (int _Index = 1; _Index <= _BSpline->NbPoles(); ++_Index)
                {
                    _Result.Poles.push_back(ToPoint2(_BSpline->Pole(_Index)));
                    _Result.Weights.push_back(_BSpline->Weight(_Index));
                }
                for (int _Index = 1; _Index <= _BSpline->NbKnots(); ++_Index)
                {
                    _Result.Knots.push_back(_BSpline->Knot(_Index));
                    _Result.Multiplicities.push_back(_BSpline->Multiplicity(_Index));
                }
                return _Result;
            }
            BSpline2 _Result;
            if (!_BSpline.IsNull())
            {
                _Result.Degree = _BSpline->Degree();
                _Result.Periodic = _BSpline->IsPeriodic();
                for (int _Index = 1; _Index <= _BSpline->NbPoles(); ++_Index)
                    _Result.Poles.push_back(ToPoint2(_BSpline->Pole(_Index)));
                for (int _Index = 1; _Index <= _BSpline->NbKnots(); ++_Index)
                {
                    _Result.Knots.push_back(_BSpline->Knot(_Index));
                    _Result.Multiplicities.push_back(_BSpline->Multiplicity(_Index));
                }
            }
            return _Result;
        }
        default:
        {
            Polyline2 _Result;
            const auto _First = Curve_.FirstParameter();
            const auto _Last = Curve_.LastParameter();
            constexpr int _SampleCount = 32;
            for (int _Index = 0; _Index <= _SampleCount; ++_Index)
            {
                const auto _T = _First + (_Last - _First)
                    * static_cast<double>(_Index) / _SampleCount;
                _Result.Points.push_back(ToPoint2(Curve_.Value(_T)));
            }
            return _Result;
        }
        }
    }

    iCAX::GeometryData::Surface3 ToSurface3(IN BRepAdaptor_Surface& Surface_)
    {
        using namespace iCAX::GeometryData;

        const auto _ToBSplineSurface = [](const Handle(Geom_BSplineSurface)& BSpline_)
        {
            BSplineSurface3 _Result;
            if (!BSpline_.IsNull())
            {
                _Result.UDegree = BSpline_->UDegree();
                _Result.VDegree = BSpline_->VDegree();
                _Result.UCount = static_cast<std::uint32_t>(BSpline_->NbUPoles());
                _Result.VCount = static_cast<std::uint32_t>(BSpline_->NbVPoles());
                _Result.UPeriodic = BSpline_->IsUPeriodic();
                _Result.VPeriodic = BSpline_->IsVPeriodic();
                for (int _U = 1; _U <= BSpline_->NbUPoles(); ++_U)
                {
                    for (int _V = 1; _V <= BSpline_->NbVPoles(); ++_V)
                    {
                        _Result.Poles.push_back(ToPoint3(BSpline_->Pole(_U, _V)));
                        _Result.Weights.push_back(BSpline_->Weight(_U, _V));
                    }
                }
                for (int _Index = 1; _Index <= BSpline_->NbUKnots(); ++_Index)
                {
                    _Result.UKnots.push_back(BSpline_->UKnot(_Index));
                    _Result.UMultiplicities.push_back(BSpline_->UMultiplicity(_Index));
                }
                for (int _Index = 1; _Index <= BSpline_->NbVKnots(); ++_Index)
                {
                    _Result.VKnots.push_back(BSpline_->VKnot(_Index));
                    _Result.VMultiplicities.push_back(BSpline_->VMultiplicity(_Index));
                }
            }
            return _Result;
        };

        switch (Surface_.GetType())
        {
        case GeomAbs_Plane:
        {
            const auto _Plane = Surface_.Plane();
            PlaneSurface3 _Result;
            _Result.Placement = ToPlacement3(_Plane.Position());
            return _Result;
        }
        case GeomAbs_Cylinder:
        {
            const auto _Cylinder = Surface_.Cylinder();
            CylindricalSurface3 _Result;
            _Result.Placement = ToPlacement3(_Cylinder.Position());
            _Result.Radius = _Cylinder.Radius();
            return _Result;
        }
        case GeomAbs_Cone:
        {
            const auto _Cone = Surface_.Cone();
            ConicalSurface3 _Result;
            _Result.Placement = ToPlacement3(_Cone.Position());
            _Result.Radius = _Cone.RefRadius();
            _Result.SemiAngle = _Cone.SemiAngle();
            return _Result;
        }
        case GeomAbs_Sphere:
        {
            const auto _Sphere = Surface_.Sphere();
            SphericalSurface3 _Result;
            _Result.Placement = ToPlacement3(_Sphere.Position());
            _Result.Radius = _Sphere.Radius();
            return _Result;
        }
        case GeomAbs_Torus:
        {
            const auto _Torus = Surface_.Torus();
            ToroidalSurface3 _Result;
            _Result.Placement = ToPlacement3(_Torus.Position());
            _Result.MajorRadius = _Torus.MajorRadius();
            _Result.MinorRadius = _Torus.MinorRadius();
            return _Result;
        }
        case GeomAbs_BSplineSurface:
        {
            return _ToBSplineSurface(Surface_.BSpline());
        }
        case GeomAbs_SurfaceOfExtrusion:
        {
            const auto _Extrusion = Handle(Geom_SurfaceOfLinearExtrusion)::DownCast(
                Surface_.GeomSurfaceOriginal());
            if (_Extrusion.IsNull() || _Extrusion->BasisCurve().IsNull())
                throw std::runtime_error("OCCT linear-extrusion surface has no basis curve");
            const auto _Basis = GeomConvert::CurveToBSplineCurve(_Extrusion->BasisCurve());
            if (_Basis.IsNull())
                throw std::runtime_error("OCCT failed to convert an extrusion basis curve to B-spline form");

            BSplineSurface3 _Result;
            _Result.UDegree = _Basis->Degree();
            _Result.VDegree = 1;
            _Result.UCount = static_cast<std::uint32_t>(_Basis->NbPoles());
            _Result.VCount = 2;
            _Result.UPeriodic = _Basis->IsPeriodic();
            _Result.VPeriodic = false;
            const auto _Transform = Surface_.Trsf();
            const auto _Direction = gp_Vec(_Extrusion->Direction());
            const std::array<double, 2> _VParameters{
                Surface_.FirstVParameter(), Surface_.LastVParameter()
            };
            _Result.Poles.reserve(static_cast<std::size_t>(_Result.UCount) * 2);
            _Result.Weights.reserve(static_cast<std::size_t>(_Result.UCount) * 2);
            for (int _U = 1; _U <= _Basis->NbPoles(); ++_U)
            {
                for (const auto _V : _VParameters)
                {
                    auto _Pole = _Basis->Pole(_U);
                    _Pole.Translate(_Direction.Multiplied(_V));
                    _Pole.Transform(_Transform);
                    _Result.Poles.push_back(ToPoint3(_Pole));
                    _Result.Weights.push_back(_Basis->Weight(_U));
                }
            }
            for (int _Index = 1; _Index <= _Basis->NbKnots(); ++_Index)
            {
                _Result.UKnots.push_back(_Basis->Knot(_Index));
                _Result.UMultiplicities.push_back(_Basis->Multiplicity(_Index));
            }
            _Result.VKnots = { _VParameters[0], _VParameters[1] };
            _Result.VMultiplicities = { 2, 2 };
            return _Result;
        }
        case GeomAbs_BezierSurface:
        case GeomAbs_SurfaceOfRevolution:
        {
            const auto _Basis = Surface_.GeomSurfaceOriginal();
            if (_Basis.IsNull())
                throw std::runtime_error("OCCT swept surface has no basis geometry");
            Handle(Geom_RectangularTrimmedSurface) _Trimmed =
                new Geom_RectangularTrimmedSurface(
                    _Basis,
                    Surface_.FirstUParameter(), Surface_.LastUParameter(),
                    Surface_.FirstVParameter(), Surface_.LastVParameter());
            auto _BSpline = GeomConvert::SurfaceToBSplineSurface(_Trimmed);
            if (_BSpline.IsNull())
                throw std::runtime_error("OCCT failed to convert a swept surface to B-spline form");
            _BSpline->Transform(Surface_.Trsf());
            return _ToBSplineSurface(_BSpline);
        }
        default:
        {
            throw std::runtime_error("Unsupported OCCT surface type for neutral BRep conversion: " + std::to_string(static_cast<int>(Surface_.GetType())));
        }
        }
    }

    TopoDS_Shape ReadShapeWithOpenCascade(IN const std::string& strSourcePath_)
    {
        if (IsStepPath(strSourcePath_))
        {
            STEPControl_Reader _Reader;
            if (_Reader.ReadFile(strSourcePath_.c_str()) != IFSelect_RetDone)
            {
                throw std::runtime_error("OCCT failed to read STEP file: " + strSourcePath_);
            }
            if (_Reader.TransferRoots() <= 0)
            {
                throw std::runtime_error("OCCT did not transfer any STEP root shape: " + strSourcePath_);
            }
            return _Reader.OneShape();
        }

        if (IsIgesPath(strSourcePath_))
        {
            IGESControl_Reader _Reader;
            if (_Reader.ReadFile(strSourcePath_.c_str()) != IFSelect_RetDone)
            {
                throw std::runtime_error("OCCT failed to read IGES file: " + strSourcePath_);
            }
            if (_Reader.TransferRoots() <= 0)
            {
                throw std::runtime_error("OCCT did not transfer any IGES root shape: " + strSourcePath_);
            }
            return _Reader.OneShape();
        }

        throw std::invalid_argument("OCCT CAD import only accepts STEP/STP and IGS/IGES files");
    }

    SShapeMaps BuildShapeMaps(IN const TopoDS_Shape& Shape_)
    {
        SShapeMaps _Maps;
        TopExp::MapShapes(Shape_, TopAbs_VERTEX, _Maps.Vertices);
        TopExp::MapShapes(Shape_, TopAbs_EDGE, _Maps.Edges);
        TopExp::MapShapes(Shape_, TopAbs_FACE, _Maps.Faces);
        TopExp::MapShapes(Shape_, TopAbs_SHELL, _Maps.Shells);
        TopExp::MapShapes(Shape_, TopAbs_SOLID, _Maps.Solids);
        TopExp::MapShapes(Shape_, TopAbs_COMPSOLID, _Maps.CompSolids);
        TopExp::MapShapes(Shape_, TopAbs_COMPOUND, _Maps.Compounds);
        return _Maps;
    }

    std::uint64_t FindShapeId(IN const ShapeMap& Map_, IN const TopoDS_Shape& Shape_)
    {
        const auto _Index = Map_.FindIndex(Shape_);
        return _Index > 0 ? static_cast<std::uint64_t>(_Index) : 0;
    }

    iCAX::GeometryData::Triangulation3 MakeTriangulation(IN const TopoDS_Face& Face_, IN const std::uint64_t nFaceID_)
    {
        iCAX::GeometryData::Triangulation3 _Result;

        TopLoc_Location _Location;
        const auto _Triangulation = BRep_Tool::Triangulation(Face_, _Location);
        if (_Triangulation.IsNull())
        {
            return _Result;
        }

        const auto _Transform = _Location.Transformation();
        for (int _Index = 1; _Index <= _Triangulation->NbNodes(); ++_Index)
        {
            _Result.Vertices.push_back(ToPoint3(_Triangulation->Node(_Index).Transformed(_Transform)));
        }

        for (int _Index = 1; _Index <= _Triangulation->NbTriangles(); ++_Index)
        {
            int _A = 0;
            int _B = 0;
            int _C = 0;
            _Triangulation->Triangle(_Index).Get(_A, _B, _C);
            _Result.Triangles.push_back({
                static_cast<std::uint32_t>(_A - 1),
                static_cast<std::uint32_t>(_B - 1),
                static_cast<std::uint32_t>(_C - 1)
            });
            _Result.TriangleFaceIds.push_back(nFaceID_);
        }
        return _Result;
    }

    void AddRootReferences(IN const TopoDS_Shape& Shape_, IN const SShapeMaps& Maps_, IN OUT iCAX::GeometryData::BRepModel& Model_)
    {
        using namespace iCAX::GeometryData;

        const auto _Kind = Shape_.ShapeType();
        const auto _Orientation = ToOrientation(Shape_.Orientation());

        auto _AddRef = [&](IN const EBRepShapeKind Kind_, IN const std::uint64_t nID_) {
            if (nID_ != 0)
            {
                Model_.RootShapes.push_back({ Kind_, nID_, _Orientation, {} });
            }
        };

        switch (_Kind)
        {
        case TopAbs_FACE:
            _AddRef(EBRepShapeKind::Face, FindShapeId(Maps_.Faces, Shape_));
            break;
        case TopAbs_SHELL:
            _AddRef(EBRepShapeKind::Shell, FindShapeId(Maps_.Shells, Shape_));
            break;
        case TopAbs_SOLID:
            _AddRef(EBRepShapeKind::Solid, FindShapeId(Maps_.Solids, Shape_));
            break;
        case TopAbs_COMPSOLID:
            _AddRef(EBRepShapeKind::CompSolid, FindShapeId(Maps_.CompSolids, Shape_));
            break;
        case TopAbs_COMPOUND:
            _AddRef(EBRepShapeKind::Compound, FindShapeId(Maps_.Compounds, Shape_));
            for (TopExp_Explorer _Explorer(Shape_, TopAbs_FACE); _Explorer.More(); _Explorer.Next())
            {
                const auto _FaceID = FindShapeId(Maps_.Faces, _Explorer.Current());
                if (_FaceID != 0)
                {
                    Model_.RootFaceIds.push_back(_FaceID);
                }
            }
            break;
        default:
            break;
        }

        for (TopExp_Explorer _Explorer(Shape_, TopAbs_FACE); _Explorer.More(); _Explorer.Next())
        {
            const auto _FaceID = FindShapeId(Maps_.Faces, _Explorer.Current());
            if (_FaceID != 0 && std::find(Model_.RootFaceIds.begin(), Model_.RootFaceIds.end(), _FaceID) == Model_.RootFaceIds.end())
            {
                Model_.RootFaceIds.push_back(_FaceID);
            }
        }
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_SHELL); _Explorer.More(); _Explorer.Next())
        {
            const auto _ShellID = FindShapeId(Maps_.Shells, _Explorer.Current());
            if (_ShellID != 0 && std::find(Model_.RootShellIds.begin(), Model_.RootShellIds.end(), _ShellID) == Model_.RootShellIds.end())
            {
                Model_.RootShellIds.push_back(_ShellID);
            }
        }
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_SOLID); _Explorer.More(); _Explorer.Next())
        {
            const auto _SolidID = FindShapeId(Maps_.Solids, _Explorer.Current());
            if (_SolidID != 0 && std::find(Model_.RootSolidIds.begin(), Model_.RootSolidIds.end(), _SolidID) == Model_.RootSolidIds.end())
            {
                Model_.RootSolidIds.push_back(_SolidID);
            }
        }
    }

    iCAX::GeometryData::BRepModel ConvertToBRepModel(
        IN const TopoDS_Shape& Shape_,
        IN const std::string& strDisplayName_,
        IN const std::string& strSourceID_,
        IN const double dTolerance_)
    {
        using namespace iCAX::GeometryData;

        if (Shape_.IsNull())
        {
            throw std::runtime_error("OCCT returned an empty CAD shape");
        }

        const double _MeshDeflection = std::max(0.01, dTolerance_ * 10.0);
        BRepMesh_IncrementalMesh _Mesh(Shape_, _MeshDeflection);
        (void)_Mesh;

        const auto _Maps = BuildShapeMaps(Shape_);

        BRepModel _Model;
        _Model.Metadata.Name = strDisplayName_;
        _Model.Metadata.SourceId = strSourceID_;
        _Model.Metadata.Tags = { kImporterID, "occt-" + std::string(kOccVersion) };

        for (int _Index = 1; _Index <= _Maps.Vertices.Extent(); ++_Index)
        {
            const auto _Vertex = TopoDS::Vertex(_Maps.Vertices(_Index));
            _Model.Vertices.push_back({
                static_cast<std::uint64_t>(_Index),
                ToPoint3(BRep_Tool::Pnt(_Vertex)),
                BRep_Tool::Tolerance(_Vertex),
                { "vertex " + std::to_string(_Index), strSourceID_, {} }
            });
        }

        for (int _Index = 1; _Index <= _Maps.Edges.Extent(); ++_Index)
        {
            // Edge 几何、参数范围和端点始终按 canonical forward 导出；
            // 它在各 Wire 中的实际方向只由 BRepCoedge::Orientation 表达。
            // 否则映射表若首次保存的是 reversed occurrence，会出现曲线参数
            // 仍为正向、端点却已交换的无效 Edge。
            const auto _Edge = TopoDS::Edge(
                _Maps.Edges(_Index).Oriented(TopAbs_FORWARD));
            BRepAdaptor_Curve _Curve(_Edge);

            Curve3Record _CurveRecord;
            _CurveRecord.Id = static_cast<std::uint64_t>(_Index);
            _CurveRecord.Geometry = ToCurve3(_Curve);
            _CurveRecord.Metadata = { "edge curve " + std::to_string(_Index), strSourceID_, {} };
            _Model.Curves3.push_back(std::move(_CurveRecord));

            TopoDS_Vertex _FirstVertex;
            TopoDS_Vertex _LastVertex;
            TopExp::Vertices(_Edge, _FirstVertex, _LastVertex, true);

            _Model.Edges.push_back({
                static_cast<std::uint64_t>(_Index),
                static_cast<std::uint64_t>(_Index),
                FindShapeId(_Maps.Vertices, _FirstVertex),
                FindShapeId(_Maps.Vertices, _LastVertex),
                MakeRange(_Curve.FirstParameter(), _Curve.LastParameter()),
                BRep_Tool::Tolerance(_Edge),
                BRep_Tool::SameParameter(_Edge),
                BRep_Tool::SameRange(_Edge),
                BRep_Tool::Degenerated(_Edge),
                BRep_Tool::IsClosed(_Edge),
                { "edge " + std::to_string(_Index), strSourceID_, {} }
            });
        }

        std::uint64_t _NextWireID = 1;
        std::uint64_t _NextCurve2ID = 1;
        for (int _Index = 1; _Index <= _Maps.Faces.Extent(); ++_Index)
        {
            const auto _FaceOccurrence = TopoDS::Face(_Maps.Faces(_Index));
            // 面自身只保存 canonical forward 的曲面、wire 与 coedge；它在
            // Shell 中的实际方向由 BRepShell::FaceOrientations 单独表达。
            // 若直接用反向 face occurrence 枚举 wire，再在 Shell 层重放方向，
            // 会使布尔产生的面被双重反向，最终成为 UnorientableShape。
            const auto _Face = TopoDS::Face(
                _FaceOccurrence.Oriented(TopAbs_FORWARD));
            BRepAdaptor_Surface _Surface(_Face, true);

            Surface3Record _SurfaceRecord;
            _SurfaceRecord.Id = static_cast<std::uint64_t>(_Index);
            _SurfaceRecord.Geometry = ToSurface3(_Surface);
            _SurfaceRecord.Metadata = { "face surface " + std::to_string(_Index), strSourceID_, {} };
            _Model.Surfaces3.push_back(std::move(_SurfaceRecord));

            Triangulation3Record _TriangulationRecord;
            _TriangulationRecord.Id = static_cast<std::uint64_t>(_Index);
            _TriangulationRecord.Geometry = MakeTriangulation(_Face, static_cast<std::uint64_t>(_Index));
            _TriangulationRecord.Metadata = { "face triangulation " + std::to_string(_Index), strSourceID_, {} };
            _Model.Triangulations3.push_back(std::move(_TriangulationRecord));

            std::vector<std::uint64_t> _WireIDs;
            std::vector<ETopologyOrientation> _WireOrientations;
            // Keep the child occurrence orientation independent, but accumulate
            // the parent location so it resolves against the global shape maps.
            for (TopoDS_Iterator _WireIterator(_Face, false, true);
                _WireIterator.More(); _WireIterator.Next())
            {
                const auto _WireOccurrence = _WireIterator.Value();
                if (_WireOccurrence.ShapeType() != TopAbs_WIRE) continue;
                const auto _Wire = TopoDS::Wire(
                    _WireOccurrence.Oriented(TopAbs_FORWARD));
                BRepWire _WireRecord;
                _WireRecord.Id = _NextWireID++;
                _WireRecord.Closed = true;
                _WireRecord.Metadata = { "face " + std::to_string(_Index) + " wire", strSourceID_, {} };

                for (BRepTools_WireExplorer _EdgeExplorer(_Wire, _Face);
                    _EdgeExplorer.More(); _EdgeExplorer.Next())
                {
                    const auto _Edge = TopoDS::Edge(_EdgeExplorer.Current());
                    BRepAdaptor_Curve _Curve(_Edge);
                    BRepAdaptor_Curve2d _Curve2(_Edge, _Face);
                    TopoDS_Vertex _FirstVertex;
                    TopoDS_Vertex _LastVertex;
                    TopExp::Vertices(_Edge, _FirstVertex, _LastVertex, true);

                    Curve2Record _Curve2Record;
                    _Curve2Record.Id = _NextCurve2ID++;
                    _Curve2Record.Geometry = ToCurve2(_Curve2);
                    _Curve2Record.Metadata = {
                        "face " + std::to_string(_Index) + " pcurve",
                        strSourceID_,
                        {}
                    };
                    const auto _Curve2ID = _Curve2Record.Id;
                    _Model.Curves2.push_back(std::move(_Curve2Record));

                    const auto _EdgeID = FindShapeId(_Maps.Edges, _Edge);
                    if (_EdgeID == 0)
                    {
                        throw std::runtime_error(
                            "OCCT wire edge is absent from the global topology map");
                    }
                    _WireRecord.Coedges.push_back({
                        _EdgeID,
                        _Curve2ID,
                        FindShapeId(_Maps.Vertices, _FirstVertex),
                        FindShapeId(_Maps.Vertices, _LastVertex),
                        MakeRange(_Curve2.FirstParameter(), _Curve2.LastParameter()),
                        ToOrientation(_Edge.Orientation())
                    });
                }

                _WireIDs.push_back(_WireRecord.Id);
                _WireOrientations.push_back(
                    ToOrientation(_WireOccurrence.Orientation()));
                _Model.Wires.push_back(std::move(_WireRecord));
            }

            double _UFirst = 0.0;
            double _ULast = 0.0;
            double _VFirst = 0.0;
            double _VLast = 0.0;
            BRepTools::UVBounds(_Face, _UFirst, _ULast, _VFirst, _VLast);

            BRepFace _FaceRecord;
            _FaceRecord.Id = static_cast<std::uint64_t>(_Index);
            _FaceRecord.Surface3Id = static_cast<std::uint64_t>(_Index);
            _FaceRecord.Triangulation3Id = static_cast<std::uint64_t>(_Index);
            _FaceRecord.WireIds = std::move(_WireIDs);
            _FaceRecord.WireOrientations = std::move(_WireOrientations);
            _FaceRecord.Domain = {
                _UFirst, _ULast, _VFirst, _VLast,
                _ULast < _UFirst, _VLast < _VFirst
            };
            _FaceRecord.Orientation = ToOrientation(
                _FaceOccurrence.Orientation());
            _FaceRecord.Tolerance = BRep_Tool::Tolerance(_Face);
            _FaceRecord.NaturalRestriction = BRep_Tool::NaturalRestriction(_Face);
            _FaceRecord.Metadata = {
                "face " + std::to_string(_Index), strSourceID_, {}
            };
            _Model.Faces.push_back(std::move(_FaceRecord));
        }

        for (int _Index = 1; _Index <= _Maps.Shells.Extent(); ++_Index)
        {
            const auto _Shell = _Maps.Shells(_Index);
            BRepShell _ShellRecord;
            _ShellRecord.Id = static_cast<std::uint64_t>(_Index);
            _ShellRecord.Closed = _Shell.Closed();
            _ShellRecord.Metadata = { "shell " + std::to_string(_Index), strSourceID_, {} };
            for (TopoDS_Iterator _Iterator(_Shell, false, true);
                _Iterator.More(); _Iterator.Next())
            {
                const auto _Face = _Iterator.Value();
                if (_Face.ShapeType() != TopAbs_FACE) continue;
                const auto _FaceID = FindShapeId(_Maps.Faces, _Face);
                if (_FaceID != 0)
                {
                    _ShellRecord.FaceIds.push_back(_FaceID);
                    _ShellRecord.FaceOrientations.push_back(
                        ToOrientation(_Face.Orientation()));
                }
            }
            _Model.Shells.push_back(std::move(_ShellRecord));
        }

        for (int _Index = 1; _Index <= _Maps.Solids.Extent(); ++_Index)
        {
            const auto _Solid = _Maps.Solids(_Index);
            BRepSolid _SolidRecord;
            _SolidRecord.Id = static_cast<std::uint64_t>(_Index);
            _SolidRecord.Metadata = { "solid " + std::to_string(_Index), strSourceID_, {} };
            for (TopoDS_Iterator _Iterator(_Solid, false, true);
                _Iterator.More(); _Iterator.Next())
            {
                const auto _Shell = _Iterator.Value();
                if (_Shell.ShapeType() != TopAbs_SHELL) continue;
                const auto _ShellID = FindShapeId(_Maps.Shells, _Shell);
                if (_ShellID != 0)
                {
                    _SolidRecord.ShellIds.push_back(_ShellID);
                    _SolidRecord.ShellOrientations.push_back(
                        ToOrientation(_Shell.Orientation()));
                }
            }
            _Model.Solids.push_back(std::move(_SolidRecord));
        }

        for (int _Index = 1; _Index <= _Maps.CompSolids.Extent(); ++_Index)
        {
            const auto _CompSolid = _Maps.CompSolids(_Index);
            BRepCompSolid _CompSolidRecord;
            _CompSolidRecord.Id = static_cast<std::uint64_t>(_Index);
            _CompSolidRecord.Metadata = { "compsolid " + std::to_string(_Index), strSourceID_, {} };
            for (TopExp_Explorer _Explorer(_CompSolid, TopAbs_SOLID); _Explorer.More(); _Explorer.Next())
            {
                const auto _SolidID = FindShapeId(_Maps.Solids, _Explorer.Current());
                if (_SolidID != 0)
                {
                    _CompSolidRecord.SolidIds.push_back(_SolidID);
                }
            }
            _Model.CompSolids.push_back(std::move(_CompSolidRecord));
        }

        for (int _Index = 1; _Index <= _Maps.Compounds.Extent(); ++_Index)
        {
            const auto _Compound = _Maps.Compounds(_Index);
            BRepCompound _CompoundRecord;
            _CompoundRecord.Id = static_cast<std::uint64_t>(_Index);
            _CompoundRecord.Metadata = { "compound " + std::to_string(_Index), strSourceID_, {} };
            for (TopoDS_Iterator _Iterator(_Compound); _Iterator.More(); _Iterator.Next())
            {
                const auto _Child = _Iterator.Value();
                const auto _Kind = ToShapeKind(_Child.ShapeType());
                std::uint64_t _ChildID = 0;
                switch (_Child.ShapeType())
                {
                case TopAbs_VERTEX:
                    _ChildID = FindShapeId(_Maps.Vertices, _Child);
                    break;
                case TopAbs_EDGE:
                    _ChildID = FindShapeId(_Maps.Edges, _Child);
                    break;
                case TopAbs_WIRE:
                    break;
                case TopAbs_FACE:
                    _ChildID = FindShapeId(_Maps.Faces, _Child);
                    break;
                case TopAbs_SHELL:
                    _ChildID = FindShapeId(_Maps.Shells, _Child);
                    break;
                case TopAbs_SOLID:
                    _ChildID = FindShapeId(_Maps.Solids, _Child);
                    break;
                case TopAbs_COMPSOLID:
                    _ChildID = FindShapeId(_Maps.CompSolids, _Child);
                    break;
                case TopAbs_COMPOUND:
                    _ChildID = FindShapeId(_Maps.Compounds, _Child);
                    break;
                default:
                    break;
                }
                if (_ChildID != 0)
                {
                    _CompoundRecord.Children.push_back({ _Kind, _ChildID, ToOrientation(_Child.Orientation()), {} });
                }
            }
            _Model.Compounds.push_back(std::move(_CompoundRecord));
        }

        AddRootReferences(Shape_, _Maps, _Model);
        return _Model;
    }

    iCAX::Resource::CResourceImportItem MakeImportItem(
        IN const std::string& strRole_,
        IN const std::string& strResourceID_,
        IN const iCAX::Resource::CResourceInfo& Info_)
    {
        iCAX::Resource::CResourceImportItem _Item;
        _Item.Role = strRole_;
        _Item.ResourceID = strResourceID_;
        _Item.Info = Info_;
        return _Item;
    }

    class COpenCascadeResourceImporter final : public iCAX::Resource::IResourceImporter
    {
    public:
        std::vector<iCAX::Resource::CResourceFormatDescriptor> GetImportFormats() const override
        {
            return {
                { kStepFormatID, "STEP CAD", { ".step", ".stp" }, {}, true, false },
                { kIgesFormatID, "IGES CAD", { ".igs", ".iges" }, {}, true, false }
            };
        }

        bool CanImport(IN const iCAX::Resource::CResourceImportRequest& Request_) const override
        {
            if (Request_.SourcePath.empty() || !IsSupportedFormatID(Request_.FormatID))
            {
                return false;
            }
            if (!Request_.TargetResourceTypeName.empty()
                && Request_.TargetResourceTypeName != kBRepResourceType
                && Request_.TargetResourceTypeName != kBinaryResourceType)
            {
                return false;
            }
            if (!Request_.FormatID.empty() && Request_.FormatID != "cad")
            {
                const auto _Detected = GetFormatIDFromPath(Request_.SourcePath);
                return _Detected.empty() || _Detected == Request_.FormatID;
            }
            return IsSupportedCadPath(Request_.SourcePath);
        }

        iCAX::Resource::CResourceImportResult Import(
            IN iCAX::Resource::CResourceLibrary& Library_,
            IN const iCAX::Resource::CResourceImportRequest& Request_) override
        {
            if (Request_.SourcePath.empty())
            {
                return iCAX::Resource::CResourceImportResult::Invalid(Request_, "OpenCascade resource import requires sourcePath");
            }
            if (!IsSupportedCadPath(Request_.SourcePath))
            {
                return iCAX::Resource::CResourceImportResult::Unsupported(Request_, "OpenCascade resource import only accepts STEP/STP and IGS/IGES files");
            }
            if (!std::filesystem::exists(std::filesystem::path(Request_.SourcePath)))
            {
                return iCAX::Resource::CResourceImportResult::Invalid(Request_, "OpenCascade resource import source file does not exist: " + Request_.SourcePath);
            }

            try
            {
                const auto _FormatID = GetFormatIDFromPath(Request_.SourcePath);
                auto _Bytes = ReadAllBytes(Request_.SourcePath);
                const auto _ContentHash = ToHex(HashBytes(_Bytes));
                const auto _DisplayName = GetDisplayNameFromPath(Request_.SourcePath);
                const auto _SourceResourceID = MakeSourceResourceID(Request_);
                const auto _BRepResourceID =
                    MakeBRepResourceID(
                        Library_,
                        _SourceResourceID);
                const auto _SourceVersion = NextResourceVersion(Library_, _SourceResourceID);
                const auto _BRepVersion = NextResourceVersion(Library_, _BRepResourceID);
                const auto _Tolerance = ReadTolerance(Request_);

                auto _Shape = ReadShapeWithOpenCascade(Request_.SourcePath);
                if (_Shape.IsNull())
                {
                    return iCAX::Resource::CResourceImportResult::Failed(Request_, "OCCT produced an empty shape: " + Request_.SourcePath);
                }

                auto _pSource = std::make_shared<iCAX::Resource::CBinaryResource>();
                _pSource->SourcePath = Request_.SourcePath;
                _pSource->DisplayName = _DisplayName;
                _pSource->FileExtension = GetExtension(Request_.SourcePath);
                _pSource->Content = std::move(_Bytes);
                _pSource->nVersion = _SourceVersion;
                _pSource->Metadata["formatId"] = _FormatID;
                _pSource->Metadata["importer"] = kImporterID;
                _pSource->Metadata["occtVersion"] = kOccVersion;
                _pSource->Metadata["contentHash"] = _ContentHash;
                _pSource->Metadata["fileSize"] = std::to_string(_pSource->Content.size());

                auto _SourceInfo = MakeResourceInfo(
                    _SourceResourceID,
                    _DisplayName,
                    "resource.source",
                    Request_.Persistence,
                    _SourceVersion,
                    static_cast<uint64_t>(_pSource->Content.size()),
                    _ContentHash,
                    _FormatID);
                _SourceInfo.Source = Request_.SourcePath;
                _SourceInfo.Metadata["sourcePath"] = Request_.SourcePath;

                Library_.Set<iCAX::Resource::CBinaryResource>(_SourceResourceID, _pSource, _SourceInfo);

                auto _pBRep = std::make_shared<iCAX::GeometryData::BRepModel>(
                    ConvertToBRepModel(_Shape, _DisplayName, _SourceResourceID, _Tolerance));
                auto _BRepInfo = MakeResourceInfo(
                    _BRepResourceID,
                    _DisplayName + " BRep",
                    "geometry.brep",
                    Request_.Persistence,
                    _BRepVersion,
                    0,
                    _ContentHash,
                    _FormatID);
                _BRepInfo.Source = Request_.SourcePath;
                _BRepInfo.Metadata["sourceResourceId"] = _SourceResourceID;
                _BRepInfo.Metadata["sourcePath"] = Request_.SourcePath;
                _BRepInfo.Metadata["faceCount"] = std::to_string(_pBRep->Faces.size());
                _BRepInfo.Metadata["edgeCount"] = std::to_string(_pBRep->Edges.size());
                _BRepInfo.Metadata["vertexCount"] = std::to_string(_pBRep->Vertices.size());

                Library_.Set<iCAX::GeometryData::BRepModel>(_BRepResourceID, _pBRep, _BRepInfo);

                const auto _TubeNeutralResourceID = MakeDerivedResourceID(
                    Library_,
                    _BRepResourceID,
                    "tube.neutral_geometry");
                const auto _TubeRemovalResourceID = MakeDerivedResourceID(
                    Library_,
                    _BRepResourceID,
                    "tube.csg.composite.removal");
                const auto _TubeAdditionResourceID = MakeDerivedResourceID(
                    Library_,
                    _BRepResourceID,
                    "tube.csg.residual.addition");
                const auto _TubeNeutralVersion = NextResourceVersion(Library_, _TubeNeutralResourceID);

                iCAX::OpenCascade::STubeCSGConversionOptions _TubeOptions;
                _TubeOptions.Tolerance = _Tolerance;
                _TubeOptions.SourceBRepResourceID = _BRepResourceID;
                _TubeOptions.RemovalBRepResourceID = _TubeRemovalResourceID;
                _TubeOptions.AdditionBRepResourceID = _TubeAdditionResourceID;
                auto _TubeConversion = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Shape, _TubeOptions);
                auto _pTubeNeutral = std::make_shared<iCAX::GeometryData::Tube::CTubeNeutralGeometry>(
                    std::move(_TubeConversion.Geometry));
                _pTubeNeutral->Version = _TubeNeutralVersion;
                _pTubeNeutral->Metadata["sourceResourceId"] = _SourceResourceID;
                _pTubeNeutral->Metadata["sourceBRepResourceId"] = _BRepResourceID;

                std::vector<iCAX::Resource::CResourceImportItem> _TubeItems;
                std::vector<iCAX::Resource::CResourceReference>
                    _TubePreviewReferences;
                for (const auto& [_NodeID, _PreviewShape] : _TubeConversion.PreviewShapes)
                {
                    if (_PreviewShape.IsNull())
                    {
                        continue;
                    }
                    const auto _PreviewResourceID = MakeDerivedResourceID(
                        Library_,
                        _BRepResourceID,
                        "tube.csg.preview." + _NodeID);
                    const auto _PreviewBRep =
                        ConvertToBRepModel(
                            _PreviewShape,
                            _DisplayName + " Tube CSG Preview " + _NodeID,
                            _BRepResourceID,
                            _Tolerance);
                    auto _PreviewResource = MakeTubePreviewMeshResource(
                        _PreviewBRep,
                        _NodeID);
                    if (_PreviewResource.Empty())
                    {
                        continue;
                    }
                    const auto _PreviewVersion = NextResourceVersion(
                        Library_,
                        _PreviewResourceID);
                    auto _PreviewInfo = MakeResourceInfo(
                        _PreviewResourceID,
                        _DisplayName + " Tube CSG Preview " + _NodeID,
                        "render.mesh.preview",
                        Request_.Persistence,
                        _PreviewVersion,
                        _PreviewResource.Size(),
                        {},
                        _FormatID);
                    _PreviewInfo.Source = Request_.SourcePath;
                    _PreviewInfo.MediaType = kTubePreviewMediaType;
                    _PreviewInfo.ResourceTypeID =
                        iCAX::Resource::CFlatBufferResource::kResourceTypeName;
                    _PreviewInfo.FlatBufferIdentifier =
                        kTubePreviewFlatBufferIdentifier;
                    _PreviewInfo.nSchemaVersion = 1;
                    _PreviewInfo.nMinimumReaderVersion = 1;
                    _PreviewInfo.Metadata["sourceBRepResourceId"] = _BRepResourceID;
                    _PreviewInfo.Metadata["csgRole"] = "feature-preview";
                    _PreviewInfo.Metadata["csgNodeId"] = _NodeID;
                    _PreviewInfo.Dependencies.push_back(
                        { _BRepResourceID, _BRepVersion });
                    Library_.Set<iCAX::Resource::CFlatBufferResource>(
                        _PreviewResourceID,
                        std::make_shared<iCAX::Resource::CFlatBufferResource>(
                            std::move(_PreviewResource)),
                        _PreviewInfo);
                    const auto _NodeIter = std::find_if(
                        _pTubeNeutral->SolidNodes.begin(),
                        _pTubeNeutral->SolidNodes.end(),
                        [&_NodeID](IN const auto& Node_) {
                            return Node_.ID == _NodeID;
                        });
                    if (_NodeIter != _pTubeNeutral->SolidNodes.end())
                    {
                        _NodeIter->Metadata["previewResourceUrl"] =
                            _PreviewResourceID;
                        _NodeIter->Metadata["previewResourceVersion"] =
                            std::to_string(_PreviewVersion);
                        _NodeIter->Metadata["previewResourceFormat"] =
                            kTubePreviewFlatBufferIdentifier;
                        _NodeIter->Metadata["previewKind"] =
                            _NodeID == "base" ? "base" : "construction-body";
                    }
                    _TubeItems.push_back(MakeImportItem(
                        kTubePreviewRole,
                        _PreviewResourceID,
                        _PreviewInfo));
                    _TubePreviewReferences.push_back(
                        { _PreviewResourceID, _PreviewVersion });
                }
                if (!_TubeConversion.RemovalShape.IsNull())
                {
                    const auto _RemovalVersion = NextResourceVersion(Library_, _TubeRemovalResourceID);
                    auto _pRemoval = std::make_shared<iCAX::GeometryData::BRepModel>(
                        ConvertToBRepModel(
                            _TubeConversion.RemovalShape,
                            _DisplayName + " Tube CSG Composite Removal",
                            _BRepResourceID,
                            _Tolerance));
                    if (!_pRemoval->Faces.empty() || !_pRemoval->Solids.empty())
                    {
                        auto _RemovalInfo = MakeResourceInfo(
                            _TubeRemovalResourceID,
                            _DisplayName + " Tube CSG Composite Removal",
                            iCAX::GeometryData::BRepModel::kResourceTypeName,
                            Request_.Persistence,
                            _RemovalVersion,
                            0,
                            _ContentHash,
                            _FormatID);
                        _RemovalInfo.Source = Request_.SourcePath;
                        _RemovalInfo.Metadata["sourceBRepResourceId"] = _BRepResourceID;
                        _RemovalInfo.Metadata["csgRole"] = "composite-removal";
                        Library_.Set<iCAX::GeometryData::BRepModel>(
                            _TubeRemovalResourceID,
                            _pRemoval,
                            _RemovalInfo);
                        _TubeItems.push_back(MakeImportItem(
                            kTubeRemovalRole,
                            _TubeRemovalResourceID,
                            _RemovalInfo));
                    }
                }
                if (!_TubeConversion.AdditionShape.IsNull())
                {
                    const auto _AdditionVersion = NextResourceVersion(Library_, _TubeAdditionResourceID);
                    auto _pAddition = std::make_shared<iCAX::GeometryData::BRepModel>(
                        ConvertToBRepModel(
                            _TubeConversion.AdditionShape,
                            _DisplayName + " Tube CSG Residual Addition",
                            _BRepResourceID,
                            _Tolerance));
                    if (!_pAddition->Faces.empty() || !_pAddition->Solids.empty())
                    {
                        auto _AdditionInfo = MakeResourceInfo(
                            _TubeAdditionResourceID,
                            _DisplayName + " Tube CSG Residual Addition",
                            iCAX::GeometryData::BRepModel::kResourceTypeName,
                            Request_.Persistence,
                            _AdditionVersion,
                            0,
                            _ContentHash,
                            _FormatID);
                        _AdditionInfo.Source = Request_.SourcePath;
                        _AdditionInfo.Metadata["sourceBRepResourceId"] = _BRepResourceID;
                        _AdditionInfo.Metadata["csgRole"] = "addition";
                        Library_.Set<iCAX::GeometryData::BRepModel>(
                            _TubeAdditionResourceID,
                            _pAddition,
                            _AdditionInfo);
                        _TubeItems.push_back(MakeImportItem(
                            kTubeAdditionRole,
                            _TubeAdditionResourceID,
                            _AdditionInfo));
                    }
                }

                auto _TubeInfo = MakeResourceInfo(
                    _TubeNeutralResourceID,
                    _DisplayName + " Tube Neutral Geometry",
                    iCAX::GeometryData::Tube::CTubeNeutralGeometry::kResourceTypeName,
                    Request_.Persistence,
                    _TubeNeutralVersion,
                    0,
                    _ContentHash,
                    _FormatID);
                _TubeInfo.Source = Request_.SourcePath;
                _TubeInfo.Metadata["sourceResourceId"] = _SourceResourceID;
                _TubeInfo.Metadata["sourceBRepResourceId"] = _BRepResourceID;
                _TubeInfo.Metadata["recognitionStatus"] = std::to_string(
                    static_cast<unsigned int>(_pTubeNeutral->RecognitionStatus));
                _TubeInfo.Metadata["confidence"] = std::to_string(_pTubeNeutral->Confidence);
                _TubeInfo.Dependencies.push_back(
                    { _BRepResourceID, _BRepVersion });
                _TubeInfo.Dependencies.insert(
                    _TubeInfo.Dependencies.end(),
                    _TubePreviewReferences.begin(),
                    _TubePreviewReferences.end());
                Library_.Set<iCAX::GeometryData::Tube::CTubeNeutralGeometry>(
                    _TubeNeutralResourceID,
                    _pTubeNeutral,
                    _TubeInfo);

                std::vector<iCAX::Resource::CResourceImportItem> _Items = {
                    MakeImportItem(kSourceRole, _SourceResourceID, _SourceInfo),
                    MakeImportItem(kBRepRole, _BRepResourceID, _BRepInfo),
                    MakeImportItem(kTubeNeutralRole, _TubeNeutralResourceID, _TubeInfo)
                };
                _Items.insert(_Items.end(), _TubeItems.begin(), _TubeItems.end());
                auto _Result = iCAX::Resource::CResourceImportResult::Succeeded(
                    _SourceResourceID,
                    std::move(_Items));
                _Result.Metadata["formatId"] = _FormatID;
                _Result.Metadata["importer"] = kImporterID;
                _Result.Metadata["occtVersion"] = kOccVersion;
                _Result.Metadata["contentHash"] = _ContentHash;
                _Result.Metadata["sourceResourceId"] = _SourceResourceID;
                _Result.Metadata["brepResourceId"] = _BRepResourceID;
                _Result.Metadata["tubeNeutralResourceId"] = _TubeNeutralResourceID;
                _Result.Metadata["tubeRecognitionStatus"] = std::to_string(
                    static_cast<unsigned int>(_pTubeNeutral->RecognitionStatus));
                _Result.Metadata["tubeRecognitionConfidence"] = std::to_string(_pTubeNeutral->Confidence);
                return _Result;
            }
            catch (const std::exception& Ex_)
            {
                return iCAX::Resource::CResourceImportResult::Failed(Request_, Ex_.what());
            }
        }

    private:
        static double ReadTolerance(IN const iCAX::Resource::CResourceImportRequest& Request_)
        {
            auto _Ite = Request_.Options.find("tolerance");
            if (_Ite == Request_.Options.end() || _Ite->second.empty())
            {
                return 0.001;
            }

            try
            {
                const auto _Tolerance = std::stod(_Ite->second);
                if (_Tolerance <= 0.0)
                {
                    throw std::invalid_argument("tolerance must be positive");
                }
                return _Tolerance;
            }
            catch (const std::exception&)
            {
                throw std::invalid_argument("OpenCascade resource import tolerance must be a positive number");
            }
        }
    };

    ICAX_REGISTER_RESOURCE_IMPORTER_PROVIDER("occ.opencascade", COpenCascadeResourceImporter)
}

iCAX::OpenCascade::SBRepFileReadResult iCAX::OpenCascade::ReadBRepFile(
    IN const std::string& strSourcePath_,
    IN double dTolerance_)
{
    SBRepFileReadResult _Result;
    if (strSourcePath_.empty())
    {
        _Result.Diagnostics.push_back("CAD source path cannot be empty");
        return _Result;
    }
    if (dTolerance_ <= 0.0)
    {
        _Result.Diagnostics.push_back("CAD read tolerance must be positive");
        return _Result;
    }
    if (!IsSupportedCadPath(strSourcePath_))
    {
        _Result.Diagnostics.push_back(
            "Only STEP/STP and IGS/IGES files are supported");
        return _Result;
    }
    if (!std::filesystem::exists(std::filesystem::path(strSourcePath_)))
    {
        _Result.Diagnostics.push_back(
            "CAD source file does not exist: " + strSourcePath_);
        return _Result;
    }

    try
    {
        const auto _Bytes = ReadAllBytes(strSourcePath_);
        const auto _Shape = ReadShapeWithOpenCascade(strSourcePath_);
        if (_Shape.IsNull())
        {
            _Result.Diagnostics.push_back(
                "OpenCascade produced an empty shape: " + strSourcePath_);
            return _Result;
        }
        _Result.FormatID = GetFormatIDFromPath(strSourcePath_);
        _Result.DisplayName = GetDisplayNameFromPath(strSourcePath_);
        _Result.ContentHash = ToHex(HashBytes(_Bytes));
        _Result.Geometry = ConvertToBRepModel(
            _Shape,
            _Result.DisplayName,
            {},
            dTolerance_);
        _Result.bOK = !_Result.Geometry.Triangulations3.empty()
            || !_Result.Geometry.Faces.empty()
            || !_Result.Geometry.Solids.empty();
        if (!_Result.bOK)
        {
            _Result.Diagnostics.push_back(
                "OpenCascade produced no usable BRep geometry");
        }
        return _Result;
    }
    catch (const std::exception& Error_)
    {
        _Result.Diagnostics.push_back(Error_.what());
        return _Result;
    }
}

iCAX::GeometryData::BRepModel iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
    IN const TopoDS_Shape& Shape_,
    IN const std::string& strDisplayName_,
    IN const std::string& strSourceID_,
    IN double dTolerance_)
{
    return ConvertToBRepModel(
        Shape_,
        strDisplayName_,
        strSourceID_,
        dTolerance_);
}
