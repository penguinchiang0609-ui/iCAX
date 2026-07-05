#include "pch.h"

#include "GeometryData/GeometryData.h"
#include "Resources/BinaryResource.h"
#include "Resources/ResourceImportExport.h"
#include "Resources/ResourceInfo.h"
#include "Resources/ResourceLibrary.h"
#include "Resources/ResourceLoaderRegistry.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4005)
#endif

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierCurve.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <IGESControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Poly_Triangle.hxx>
#include <STEPControl_Reader.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <NCollection_IndexedMap.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Dir.hxx>
#include <gp_Elips.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr const char* kImporterID = "opencascade";
    constexpr const char* kOccVersion = "8.0.0-p1";
    constexpr const char* kStepFormatID = "cad.step";
    constexpr const char* kIgesFormatID = "cad.iges";
    constexpr const char* kSourceRole = "source";
    constexpr const char* kBRepRole = "geometry.brep";

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

    std::string MakeBRepResourceID(IN const std::string& strSourceResourceID_)
    {
        return strSourceResourceID_.empty() ? std::string() : strSourceResourceID_ + "#geometry.brep";
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

    iCAX::GeometryData::Surface3 ToSurface3(IN BRepAdaptor_Surface& Surface_)
    {
        using namespace iCAX::GeometryData;

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
            const auto _BSpline = Surface_.BSpline();
            BSplineSurface3 _Result;
            if (!_BSpline.IsNull())
            {
                _Result.UDegree = _BSpline->UDegree();
                _Result.VDegree = _BSpline->VDegree();
                _Result.UCount = static_cast<std::uint32_t>(_BSpline->NbUPoles());
                _Result.VCount = static_cast<std::uint32_t>(_BSpline->NbVPoles());
                _Result.UPeriodic = _BSpline->IsUPeriodic();
                _Result.VPeriodic = _BSpline->IsVPeriodic();
                for (int _U = 1; _U <= _BSpline->NbUPoles(); ++_U)
                {
                    for (int _V = 1; _V <= _BSpline->NbVPoles(); ++_V)
                    {
                        _Result.Poles.push_back(ToPoint3(_BSpline->Pole(_U, _V)));
                        _Result.Weights.push_back(_BSpline->Weight(_U, _V));
                    }
                }
                for (int _Index = 1; _Index <= _BSpline->NbUKnots(); ++_Index)
                {
                    _Result.UKnots.push_back(_BSpline->UKnot(_Index));
                    _Result.UMultiplicities.push_back(_BSpline->UMultiplicity(_Index));
                }
                for (int _Index = 1; _Index <= _BSpline->NbVKnots(); ++_Index)
                {
                    _Result.VKnots.push_back(_BSpline->VKnot(_Index));
                    _Result.VMultiplicities.push_back(_BSpline->VMultiplicity(_Index));
                }
            }
            return _Result;
        }
        default:
        {
            PlaneSurface3 _Result;
            return _Result;
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
            const auto _Edge = TopoDS::Edge(_Maps.Edges(_Index));
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
        for (int _Index = 1; _Index <= _Maps.Faces.Extent(); ++_Index)
        {
            const auto _Face = TopoDS::Face(_Maps.Faces(_Index));
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
            for (TopExp_Explorer _WireExplorer(_Face, TopAbs_WIRE); _WireExplorer.More(); _WireExplorer.Next())
            {
                const auto _Wire = TopoDS::Wire(_WireExplorer.Current());
                BRepWire _WireRecord;
                _WireRecord.Id = _NextWireID++;
                _WireRecord.Closed = true;
                _WireRecord.Metadata = { "face " + std::to_string(_Index) + " wire", strSourceID_, {} };

                for (TopExp_Explorer _EdgeExplorer(_Wire, TopAbs_EDGE); _EdgeExplorer.More(); _EdgeExplorer.Next())
                {
                    const auto _Edge = TopoDS::Edge(_EdgeExplorer.Current());
                    BRepAdaptor_Curve _Curve(_Edge);
                    TopoDS_Vertex _FirstVertex;
                    TopoDS_Vertex _LastVertex;
                    TopExp::Vertices(_Edge, _FirstVertex, _LastVertex, true);

                    _WireRecord.Coedges.push_back({
                        FindShapeId(_Maps.Edges, _Edge),
                        0,
                        FindShapeId(_Maps.Vertices, _FirstVertex),
                        FindShapeId(_Maps.Vertices, _LastVertex),
                        MakeRange(_Curve.FirstParameter(), _Curve.LastParameter()),
                        ToOrientation(_Edge.Orientation())
                    });
                }

                _WireIDs.push_back(_WireRecord.Id);
                _Model.Wires.push_back(std::move(_WireRecord));
            }

            double _UFirst = 0.0;
            double _ULast = 0.0;
            double _VFirst = 0.0;
            double _VLast = 0.0;
            BRepTools::UVBounds(_Face, _UFirst, _ULast, _VFirst, _VLast);

            _Model.Faces.push_back({
                static_cast<std::uint64_t>(_Index),
                static_cast<std::uint64_t>(_Index),
                static_cast<std::uint64_t>(_Index),
                std::move(_WireIDs),
                { _UFirst, _ULast, _VFirst, _VLast, _ULast < _UFirst, _VLast < _VFirst },
                ToOrientation(_Face.Orientation()),
                BRep_Tool::Tolerance(_Face),
                BRep_Tool::NaturalRestriction(_Face),
                { "face " + std::to_string(_Index), strSourceID_, {} }
            });
        }

        for (int _Index = 1; _Index <= _Maps.Shells.Extent(); ++_Index)
        {
            const auto _Shell = _Maps.Shells(_Index);
            BRepShell _ShellRecord;
            _ShellRecord.Id = static_cast<std::uint64_t>(_Index);
            _ShellRecord.Metadata = { "shell " + std::to_string(_Index), strSourceID_, {} };
            for (TopExp_Explorer _Explorer(_Shell, TopAbs_FACE); _Explorer.More(); _Explorer.Next())
            {
                const auto _FaceID = FindShapeId(_Maps.Faces, _Explorer.Current());
                if (_FaceID != 0)
                {
                    _ShellRecord.FaceIds.push_back(_FaceID);
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
            for (TopExp_Explorer _Explorer(_Solid, TopAbs_SHELL); _Explorer.More(); _Explorer.Next())
            {
                const auto _ShellID = FindShapeId(_Maps.Shells, _Explorer.Current());
                if (_ShellID != 0)
                {
                    _SolidRecord.ShellIds.push_back(_ShellID);
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
                const auto _BRepResourceID = MakeBRepResourceID(_SourceResourceID);
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
                _BRepInfo.Metadata["sourceResourceId"] = _SourceResourceID;
                _BRepInfo.Metadata["sourcePath"] = Request_.SourcePath;
                _BRepInfo.Metadata["faceCount"] = std::to_string(_pBRep->Faces.size());
                _BRepInfo.Metadata["edgeCount"] = std::to_string(_pBRep->Edges.size());
                _BRepInfo.Metadata["vertexCount"] = std::to_string(_pBRep->Vertices.size());

                Library_.Set<iCAX::GeometryData::BRepModel>(_BRepResourceID, _pBRep, _BRepInfo);

                auto _Result = iCAX::Resource::CResourceImportResult::Succeeded(
                    _SourceResourceID,
                    {
                        MakeImportItem(kSourceRole, _SourceResourceID, _SourceInfo),
                        MakeImportItem(kBRepRole, _BRepResourceID, _BRepInfo)
                    });
                _Result.Metadata["formatId"] = _FormatID;
                _Result.Metadata["importer"] = kImporterID;
                _Result.Metadata["occtVersion"] = kOccVersion;
                _Result.Metadata["contentHash"] = _ContentHash;
                _Result.Metadata["sourceResourceId"] = _SourceResourceID;
                _Result.Metadata["brepResourceId"] = _BRepResourceID;
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

    ICAX_REGISTER_RESOURCE_IMPORTER(COpenCascadeResourceImporter)
}
