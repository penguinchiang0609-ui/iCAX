#include "pch.h"
#include "WorkpieceFacadeImplement.h"

#include "ToolpathResourceKeys.h"
#include "ToolpathResources.h"
#include "TopologyPayloadNames.h"
#include "WorkpieceResourceKeys.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>

namespace iCAX::CAM::Facades::Internal
{
namespace
{
    constexpr size_t kMaxInlineTopologyPickItems = 10000;
}

    struct SProjectionBounds final
    {
        double MinX = (std::numeric_limits<double>::max)();
        double MaxX = (std::numeric_limits<double>::lowest)();
        double MinY = (std::numeric_limits<double>::max)();
        double MaxY = (std::numeric_limits<double>::lowest)();
        bool Empty = true;

        void Add(IN const iCAX::GeometryData::Point3& Point_)
        {
            MinX = std::min(MinX, Point_.X);
            MaxX = std::max(MaxX, Point_.X);
            MinY = std::min(MinY, Point_.Y);
            MaxY = std::max(MaxY, Point_.Y);
            Empty = false;
        }
    };

    bool _IsTopologyResourceEmpty(IN const std::shared_ptr<iCAX::CAM::CTopologyResource>& pTopology_) noexcept
    {
        return !pTopology_ || (pTopology_->Faces.empty() && pTopology_->Loops.empty() && pTopology_->Edges.empty());
    }

    ObjectMap _MakeWorkpiecePayload(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CWorkpieceComponent>& pWorkpiece_)
    {
        ObjectMap _Workpiece;
        _Workpiece["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
        _Workpiece["name"] = pWorkpiece_ ? pWorkpiece_->GetName() : std::string();
        _Workpiece["sourcePath"] = pWorkpiece_ ? pWorkpiece_->GetSourcePath() : std::string();
        _Workpiece["modelResourceId"] = pWorkpiece_ ? pWorkpiece_->GetModelResourceID() : std::string();
        _Workpiece["brepResourceId"] = pWorkpiece_ ? pWorkpiece_->GetBRepResourceID() : std::string();
        _Workpiece["topologyResourceId"] = pWorkpiece_ ? pWorkpiece_->GetTopologyResourceID() : std::string();
        _Workpiece["topologyVersion"] = pWorkpiece_ ? pWorkpiece_->GetTopologyVersion() : 0ull;
        _Workpiece["geometryRevision"] = pWorkpiece_ ? pWorkpiece_->GetGeometryRevision() : 0ull;
        _Workpiece["editState"] = pWorkpiece_ ? pWorkpiece_->GetEditState() : std::string("Current");
        _Workpiece["hasDraft"] = pWorkpiece_ && !pWorkpiece_->GetDraftBRepResourceID().empty();
        _Workpiece["draftTopologyVersion"] = pWorkpiece_ ? pWorkpiece_->GetDraftTopologyVersion() : 0ull;
        _Workpiece["isLoaded"] = pWorkpiece_ && !pWorkpiece_->GetSourcePath().empty();
        return _Workpiece;
    }

    VariantArray _MakeWorkpieceArray(IN iCAX::Database::IRepository& Repository_)
    {
        VariantArray _Workpieces;
        for (const auto& [_pEntity, _pWorkpiece] : _CollectEntitiesWithComponent<iCAX::CAM::CWorkpieceComponent>(Repository_))
        {
            _Workpieces.emplace_back(_MakeWorkpiecePayload(_pEntity, _pWorkpiece));
        }
        return _Workpieces;
    }

    ObjectMap _MakeTopologyStatusPayload(IN const std::shared_ptr<iCAX::CAM::CTopologyResource>& pTopology_)
    {
        ObjectMap _Status;
        _Status["hasTopology"] = !_IsTopologyResourceEmpty(pTopology_);
        _Status["version"] = pTopology_ ? pTopology_->nVersion : 0ull;
        _Status["faceCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Faces.size()) : 0ull;
        _Status["loopCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Loops.size()) : 0ull;
        _Status["edgeCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Edges.size()) : 0ull;
        _Status["inlinePickMapLimit"] = static_cast<unsigned long long>(kMaxInlineTopologyPickItems);
        _Status["inlinePickMapTruncated"] = pTopology_
            && (pTopology_->Faces.size() > kMaxInlineTopologyPickItems
                || pTopology_->Loops.size() > kMaxInlineTopologyPickItems
                || pTopology_->Edges.size() > kMaxInlineTopologyPickItems);
        _Status["metadata"] = pTopology_ ? pTopology_->Metadata : ObjectMap{};

        if (pTopology_)
        {
            auto _ImportMode = pTopology_->Metadata.find("importMode");
            if (_ImportMode != pTopology_->Metadata.end())
            {
                _Status["importMode"] = _ImportMode->second;
            }

            auto _Diagnostic = pTopology_->Metadata.find("diagnostic");
            if (_Diagnostic != pTopology_->Metadata.end())
            {
                _Status["diagnostic"] = _Diagnostic->second;
            }
        }
        return _Status;
    }

    ObjectMap _MakeTopologyPickItemPayload(IN const Variant& Item_)
    {
        if (!Item_.Is<ObjectMap>())
        {
            return ObjectMap{};
        }

        const auto _Source = Item_.To<ObjectMap>();
        ObjectMap _Item;
        _Item["id"] = _GetObjectUInt64(_Source, "id");
        _Item["kind"] = _GetObjectString(_Source, "kind");
        _Item["label"] = _GetObjectString(_Source, "label");
        _Item["role"] = _GetObjectString(_Source, "role");
        _Item["triangleStart"] = _GetObjectUInt64(_Source, "triangleStart");
        _Item["triangleCount"] = _GetObjectUInt64(_Source, "triangleCount");
        return _Item;
    }

    VariantArray _MakeTopologyPickArray(IN const VariantArray& Items_)
    {
        VariantArray _Result;
        if (Items_.size() > kMaxInlineTopologyPickItems)
        {
            return _Result;
        }

        _Result.reserve(Items_.size());
        for (const auto& _Item : Items_)
        {
            _Result.emplace_back(_MakeTopologyPickItemPayload(_Item));
        }
        return _Result;
    }

    template <typename TRecord>
    const TRecord* _FindRecordByID(IN const std::vector<TRecord>& Records_, IN uint64_t nID_) noexcept
    {
        auto _Ite = std::find_if(Records_.begin(), Records_.end(), [nID_](IN const TRecord& Record_) {
            return Record_.Id == nID_;
        });
        return _Ite == Records_.end() ? nullptr : &(*_Ite);
    }

    iCAX::GeometryData::Point3 _AddScaled(
        IN const iCAX::GeometryData::Point3& Origin_,
        IN const iCAX::GeometryData::Direction3& Direction_,
        IN double dScale_)
    {
        return {
            Origin_.X + Direction_.X * dScale_,
            Origin_.Y + Direction_.Y * dScale_,
            Origin_.Z + Direction_.Z * dScale_
        };
    }

    iCAX::GeometryData::Point3 _EllipsePoint(
        IN const iCAX::GeometryData::Placement3& Placement_,
        IN double dMajorRadius_,
        IN double dMinorRadius_,
        IN double dAngle_)
    {
        const auto _Cos = std::cos(dAngle_);
        const auto _Sin = std::sin(dAngle_);
        return {
            Placement_.Location.X + Placement_.XDirection.X * dMajorRadius_ * _Cos + Placement_.YDirection.X * dMinorRadius_ * _Sin,
            Placement_.Location.Y + Placement_.XDirection.Y * dMajorRadius_ * _Cos + Placement_.YDirection.Y * dMinorRadius_ * _Sin,
            Placement_.Location.Z + Placement_.XDirection.Z * dMajorRadius_ * _Cos + Placement_.YDirection.Z * dMinorRadius_ * _Sin
        };
    }

    std::pair<double, double> _ResolveCurveRange(IN const iCAX::GeometryData::ParameterRange& Range_, IN double dDefaultFirst_, IN double dDefaultLast_)
    {
        const bool _HasFiniteRange = std::isfinite(Range_.First) && std::isfinite(Range_.Last);
        if (_HasFiniteRange && Range_.First != Range_.Last)
        {
            return { Range_.First, Range_.Last };
        }
        return { dDefaultFirst_, dDefaultLast_ };
    }

    template <typename TPoint>
    void _AppendControlPoints(IN OUT std::vector<iCAX::GeometryData::Point3>& Points_, IN const std::vector<TPoint>& Poles_)
    {
        for (const auto& _Pole : Poles_)
        {
            Points_.push_back({ _Pole.X, _Pole.Y, _Pole.Z });
        }
    }

    std::vector<iCAX::GeometryData::Point3> _SampleCurve3(
        IN const iCAX::GeometryData::Curve3& Curve_,
        IN const iCAX::GeometryData::ParameterRange& Range_)
    {
        using namespace iCAX::GeometryData;

        return std::visit([&Range_](IN const auto& CurveValue_) {
            using TCurve = std::decay_t<decltype(CurveValue_)>;
            std::vector<Point3> _Points;

            if constexpr (std::is_same_v<TCurve, Segment3>)
            {
                _Points.push_back(CurveValue_.Start);
                _Points.push_back(CurveValue_.End);
            }
            else if constexpr (std::is_same_v<TCurve, Line3>)
            {
                const auto [_First, _Last] = _ResolveCurveRange(Range_, 0.0, 1.0);
                _Points.push_back(_AddScaled(CurveValue_.Axis.Location, CurveValue_.Axis.Direction, _First));
                _Points.push_back(_AddScaled(CurveValue_.Axis.Location, CurveValue_.Axis.Direction, _Last));
            }
            else if constexpr (std::is_same_v<TCurve, Ray3>)
            {
                const auto [_First, _Last] = _ResolveCurveRange(Range_, CurveValue_.First, CurveValue_.First + 1.0);
                _Points.push_back(_AddScaled(CurveValue_.Axis.Location, CurveValue_.Axis.Direction, _First));
                _Points.push_back(_AddScaled(CurveValue_.Axis.Location, CurveValue_.Axis.Direction, _Last));
            }
            else if constexpr (std::is_same_v<TCurve, Circle3>)
            {
                const auto [_First, _Last] = _ResolveCurveRange(Range_, 0.0, 6.28318530717958647692);
                constexpr int _SampleCount = 32;
                for (int _Index = 0; _Index <= _SampleCount; ++_Index)
                {
                    const auto _T = _First + (_Last - _First) * static_cast<double>(_Index) / static_cast<double>(_SampleCount);
                    _Points.push_back(_EllipsePoint(CurveValue_.Placement, CurveValue_.Radius, CurveValue_.Radius, _T));
                }
            }
            else if constexpr (std::is_same_v<TCurve, Arc3>)
            {
                const auto _First = CurveValue_.Basis.Radius <= 0.0 ? 0.0 : CurveValue_.StartAngle;
                const auto _Last = CurveValue_.Basis.Radius <= 0.0 ? 0.0 : CurveValue_.EndAngle;
                constexpr int _SampleCount = 16;
                for (int _Index = 0; _Index <= _SampleCount; ++_Index)
                {
                    const auto _T = _First + (_Last - _First) * static_cast<double>(_Index) / static_cast<double>(_SampleCount);
                    _Points.push_back(_EllipsePoint(CurveValue_.Basis.Placement, CurveValue_.Basis.Radius, CurveValue_.Basis.Radius, _T));
                }
            }
            else if constexpr (std::is_same_v<TCurve, Ellipse3>)
            {
                const auto [_First, _Last] = _ResolveCurveRange(Range_, 0.0, 6.28318530717958647692);
                constexpr int _SampleCount = 32;
                for (int _Index = 0; _Index <= _SampleCount; ++_Index)
                {
                    const auto _T = _First + (_Last - _First) * static_cast<double>(_Index) / static_cast<double>(_SampleCount);
                    _Points.push_back(_EllipsePoint(CurveValue_.Placement, CurveValue_.MajorRadius, CurveValue_.MinorRadius, _T));
                }
            }
            else if constexpr (std::is_same_v<TCurve, EllipseArc3>)
            {
                constexpr int _SampleCount = 16;
                for (int _Index = 0; _Index <= _SampleCount; ++_Index)
                {
                    const auto _T = CurveValue_.StartAngle + (CurveValue_.EndAngle - CurveValue_.StartAngle) * static_cast<double>(_Index) / static_cast<double>(_SampleCount);
                    _Points.push_back(_EllipsePoint(CurveValue_.Basis.Placement, CurveValue_.Basis.MajorRadius, CurveValue_.Basis.MinorRadius, _T));
                }
            }
            else if constexpr (std::is_same_v<TCurve, Polyline3>)
            {
                _Points = CurveValue_.Points;
            }
            else if constexpr (std::is_same_v<TCurve, Bezier3>)
            {
                _AppendControlPoints(_Points, CurveValue_.Poles);
            }
            else if constexpr (std::is_same_v<TCurve, BSpline3>)
            {
                _AppendControlPoints(_Points, CurveValue_.Poles);
            }
            else if constexpr (std::is_same_v<TCurve, NURBS3>)
            {
                _AppendControlPoints(_Points, CurveValue_.Poles);
            }
            else if constexpr (std::is_same_v<TCurve, Clothoid3>)
            {
                _Points.push_back(CurveValue_.Placement.Location);
                _Points.push_back(_AddScaled(CurveValue_.Placement.Location, CurveValue_.Placement.XDirection, CurveValue_.Length));
            }

            return _Points;
        }, Curve_);
    }

    const iCAX::GeometryData::BRepVertex* _FindVertex(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN uint64_t nVertexID_) noexcept
    {
        return _FindRecordByID(Model_.Vertices, nVertexID_);
    }

    std::vector<iCAX::GeometryData::Point3> _SampleEdgePoints(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN const iCAX::GeometryData::BRepEdge& Edge_)
    {
        if (const auto* _pCurve = _FindRecordByID(Model_.Curves3, Edge_.Curve3Id))
        {
            auto _Points = _SampleCurve3(_pCurve->Geometry, Edge_.Range);
            if (!_Points.empty())
            {
                return _Points;
            }
        }

        std::vector<iCAX::GeometryData::Point3> _Points;
        if (const auto* _pStart = _FindVertex(Model_, Edge_.StartVertexId))
        {
            _Points.push_back(_pStart->Position);
        }
        if (const auto* _pEnd = _FindVertex(Model_, Edge_.EndVertexId))
        {
            _Points.push_back(_pEnd->Position);
        }
        return _Points;
    }

    std::vector<iCAX::GeometryData::Point3> _CollectWirePoints(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN const iCAX::GeometryData::BRepWire& Wire_)
    {
        std::vector<iCAX::GeometryData::Point3> _Points;
        for (const auto& _Coedge : Wire_.Coedges)
        {
            if (const auto* _pEdge = _FindRecordByID(Model_.Edges, _Coedge.EdgeId))
            {
                auto _EdgePoints = _SampleEdgePoints(Model_, *_pEdge);
                _Points.insert(_Points.end(), _EdgePoints.begin(), _EdgePoints.end());
            }
        }
        return _Points;
    }

    void _CollectBRepBounds(IN const iCAX::GeometryData::BRepModel& Model_, IN OUT SProjectionBounds& Bounds_)
    {
        for (const auto& _Vertex : Model_.Vertices)
        {
            Bounds_.Add(_Vertex.Position);
        }
        for (const auto& _Triangulation : Model_.Triangulations3)
        {
            for (const auto& _Point : _Triangulation.Geometry.Vertices)
            {
                Bounds_.Add(_Point);
            }
        }
        for (const auto& _Edge : Model_.Edges)
        {
            for (const auto& _Point : _SampleEdgePoints(Model_, _Edge))
            {
                Bounds_.Add(_Point);
            }
        }
    }

    Variant _MakeProjectedPoint(IN double dX_, IN double dY_)
    {
        ObjectMap _Point;
        _Point["x"] = dX_;
        _Point["y"] = dY_;
        return Variant(_Point);
    }

    VariantArray _MakeProjectedPointArray(IN const std::vector<iCAX::GeometryData::Point3>& Points_, IN const SProjectionBounds& Bounds_)
    {
        const double _Width = std::max(1.0, Bounds_.MaxX - Bounds_.MinX);
        const double _Height = std::max(1.0, Bounds_.MaxY - Bounds_.MinY);
        const double _Scale = std::min(700.0 / _Width, 340.0 / _Height);
        const double _OffsetX = 40.0 + (700.0 - _Width * _Scale) * 0.5;
        const double _OffsetY = 40.0 + (340.0 - _Height * _Scale) * 0.5;

        VariantArray _Result;
        _Result.reserve(Points_.size());
        for (const auto& _Point : Points_)
        {
            const double _X = _OffsetX + (_Point.X - Bounds_.MinX) * _Scale;
            const double _Y = _OffsetY + (Bounds_.MaxY - _Point.Y) * _Scale;
            _Result.emplace_back(_MakeProjectedPoint(_X, _Y));
        }
        return _Result;
    }

    ObjectMap _MakeProjectedEdge(
        IN uint64_t nID_,
        IN const std::string& strLabel_,
        IN const std::vector<iCAX::GeometryData::Point3>& Points_,
        IN const SProjectionBounds& Bounds_)
    {
        ObjectMap _Edge;
        _Edge["id"] = static_cast<unsigned long long>(nID_);
        _Edge["kind"] = std::string(kTopologyKindEdge);
        _Edge["label"] = strLabel_;
        _Edge["role"] = std::string("cad-edge");
        _Edge["points"] = _MakeProjectedPointArray(Points_, Bounds_);
        return _Edge;
    }

    ObjectMap _MakeProjectedFace(
        IN uint64_t nID_,
        IN const std::string& strLabel_,
        IN const std::vector<iCAX::GeometryData::Point3>& Points_,
        IN const SProjectionBounds& Bounds_,
        IN uint64_t nTriangleStart_,
        IN uint64_t nTriangleCount_)
    {
        ObjectMap _Face;
        _Face["id"] = static_cast<unsigned long long>(nID_);
        _Face["kind"] = std::string(kTopologyKindFace);
        _Face["label"] = strLabel_;
        _Face["role"] = std::string("cad-face");
        _Face["points"] = _MakeProjectedPointArray(Points_, Bounds_);
        if (nTriangleCount_ > 0)
        {
            _Face["triangleStart"] = static_cast<unsigned long long>(nTriangleStart_);
            _Face["triangleCount"] = static_cast<unsigned long long>(nTriangleCount_);
        }
        return _Face;
    }

    ObjectMap _MakeProjectedLoop(
        IN uint64_t nID_,
        IN const std::string& strLabel_,
        IN const std::vector<iCAX::GeometryData::Point3>& Points_,
        IN const SProjectionBounds& Bounds_,
        IN const std::string& strRole_)
    {
        SProjectionBounds _LoopBounds;
        for (const auto& _Point : Points_)
        {
            _LoopBounds.Add(_Point);
        }
        if (_LoopBounds.Empty)
        {
            _LoopBounds = Bounds_;
        }

        const auto _CenterPoints = _MakeProjectedPointArray(
            { { (_LoopBounds.MinX + _LoopBounds.MaxX) * 0.5, (_LoopBounds.MinY + _LoopBounds.MaxY) * 0.5, 0.0 } },
            Bounds_);
        const auto _RadiusPoints = _MakeProjectedPointArray(
            { { _LoopBounds.MaxX, (_LoopBounds.MinY + _LoopBounds.MaxY) * 0.5, 0.0 } },
            Bounds_);

        double _CenterX = 0.0;
        double _CenterY = 0.0;
        double _RadiusX = 0.0;
        if (!_CenterPoints.empty())
        {
            const auto _Center = _CenterPoints.front().To<ObjectMap>();
            _CenterX = _Center.at("x").To<double>();
            _CenterY = _Center.at("y").To<double>();
        }
        if (!_RadiusPoints.empty())
        {
            const auto _RadiusPoint = _RadiusPoints.front().To<ObjectMap>();
            _RadiusX = _RadiusPoint.at("x").To<double>();
        }

        ObjectMap _Loop;
        _Loop["id"] = static_cast<unsigned long long>(nID_);
        _Loop["kind"] = std::string(kTopologyKindLoop);
        _Loop["label"] = strLabel_;
        _Loop["role"] = strRole_;
        _Loop["center"] = _MakeProjectedPoint(_CenterX, _CenterY);
        _Loop["radius"] = std::max(6.0, std::abs(_RadiusX - _CenterX));
        return _Loop;
    }

    std::vector<iCAX::GeometryData::Point3> _MakeFacePreviewPolygon(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN const iCAX::GeometryData::BRepFace& Face_)
    {
        SProjectionBounds _Bounds;
        if (const auto* _pTriangulation = _FindRecordByID(Model_.Triangulations3, Face_.Triangulation3Id))
        {
            for (const auto& _Point : _pTriangulation->Geometry.Vertices)
            {
                _Bounds.Add(_Point);
            }
        }

        if (_Bounds.Empty)
        {
            for (const auto _WireID : Face_.WireIds)
            {
                if (const auto* _pWire = _FindRecordByID(Model_.Wires, _WireID))
                {
                    for (const auto& _Point : _CollectWirePoints(Model_, *_pWire))
                    {
                        _Bounds.Add(_Point);
                    }
                }
            }
        }

        if (_Bounds.Empty)
        {
            return {};
        }

        return {
            { _Bounds.MinX, _Bounds.MinY, 0.0 },
            { _Bounds.MaxX, _Bounds.MinY, 0.0 },
            { _Bounds.MaxX, _Bounds.MaxY, 0.0 },
            { _Bounds.MinX, _Bounds.MaxY, 0.0 }
        };
    }

    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> _BuildTriangulationTriangleRanges(
        IN const iCAX::GeometryData::BRepModel& Model_)
    {
        std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> _Ranges;
        uint64_t _TriangleStart = 0;
        for (const auto& _TriangulationRecord : Model_.Triangulations3)
        {
            const auto& _Geometry = _TriangulationRecord.Geometry;
            if (_Geometry.Vertices.empty() || _Geometry.Triangles.empty())
            {
                continue;
            }

            const auto _TriangleCount = static_cast<uint64_t>(_Geometry.Triangles.size());
            _Ranges[_TriangulationRecord.Id] = { _TriangleStart, _TriangleCount };
            _TriangleStart += _TriangleCount;
        }
        return _Ranges;
    }

    std::unordered_map<uint64_t, std::string> _BuildWireRoles(IN const iCAX::GeometryData::BRepModel& Model_)
    {
        std::unordered_map<uint64_t, std::string> _Roles;
        for (const auto& _Face : Model_.Faces)
        {
            for (size_t _Index = 0; _Index < _Face.WireIds.size(); ++_Index)
            {
                const auto _WireID = _Face.WireIds[_Index];
                if (_WireID == 0)
                {
                    continue;
                }

                const auto _Role = _Index == 0 ? std::string("cut") : std::string("hole");
                auto _Iter = _Roles.find(_WireID);
                if (_Iter == _Roles.end() || _Role == "hole")
                {
                    _Roles[_WireID] = _Role;
                }
            }
        }
        return _Roles;
    }

    std::shared_ptr<iCAX::CAM::CTopologyResource> _MakeTopologyResourceFromBRep(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN const std::string& strDisplayName_,
        IN uint64_t nTopologyVersion_)
    {
        SProjectionBounds _Bounds;
        _CollectBRepBounds(Model_, _Bounds);
        if (_Bounds.Empty)
        {
            _Bounds.Add({ 0.0, 0.0, 0.0 });
            _Bounds.Add({ 1.0, 1.0, 0.0 });
        }

        auto _pTopology = std::make_shared<iCAX::CAM::CTopologyResource>();
        _pTopology->nVersion = nTopologyVersion_;
        const auto _TriangleRanges = _BuildTriangulationTriangleRanges(Model_);
        const auto _WireRoles = _BuildWireRoles(Model_);

        for (const auto& _Face : Model_.Faces)
        {
            auto _Polygon = _MakeFacePreviewPolygon(Model_, _Face);
            if (!_Polygon.empty())
            {
                const auto _RangeIter = _TriangleRanges.find(_Face.Triangulation3Id);
                const auto _TriangleStart = _RangeIter == _TriangleRanges.end() ? 0ull : _RangeIter->second.first;
                const auto _TriangleCount = _RangeIter == _TriangleRanges.end() ? 0ull : _RangeIter->second.second;
                _pTopology->Faces.emplace_back(_MakeProjectedFace(
                    _Face.Id,
                    strDisplayName_ + " face " + std::to_string(_Face.Id),
                    _Polygon,
                    _Bounds,
                    _TriangleStart,
                    _TriangleCount));
            }
        }

        for (const auto& _Wire : Model_.Wires)
        {
            auto _LoopPoints = _CollectWirePoints(Model_, _Wire);
            if (!_LoopPoints.empty())
            {
                const auto _RoleIter = _WireRoles.find(_Wire.Id);
                _pTopology->Loops.emplace_back(_MakeProjectedLoop(
                    _Wire.Id,
                    "loop " + std::to_string(_Wire.Id),
                    _LoopPoints,
                    _Bounds,
                    _RoleIter == _WireRoles.end() ? std::string("cut") : _RoleIter->second));
            }
        }

        for (const auto& _Edge : Model_.Edges)
        {
            auto _Points = _SampleEdgePoints(Model_, _Edge);
            if (!_Points.empty())
            {
                _pTopology->Edges.emplace_back(_MakeProjectedEdge(
                    _Edge.Id,
                    "edge " + std::to_string(_Edge.Id),
                    _Points,
                    _Bounds));
            }
        }

        _pTopology->Metadata["sourceDisplayName"] = strDisplayName_;
        _pTopology->Metadata["faceCount"] = static_cast<unsigned long long>(_pTopology->Faces.size());
        _pTopology->Metadata["loopCount"] = static_cast<unsigned long long>(_pTopology->Loops.size());
        _pTopology->Metadata["edgeCount"] = static_cast<unsigned long long>(_pTopology->Edges.size());
        return _pTopology;
    }

    std::string _FindImportedResourceID(IN const iCAX::Resource::CResourceImportResult& Result_, IN const std::string& strRole_)
    {
        auto _Ite = std::find_if(Result_.Items.begin(), Result_.Items.end(), [&strRole_](IN const iCAX::Resource::CResourceImportItem& Item_) {
            return Item_.Role == strRole_;
        });
        return _Ite == Result_.Items.end() ? std::string() : _Ite->ResourceID;
    }

    std::string _ToLowerASCII(IN std::string strText_)
    {
        std::transform(strText_.begin(), strText_.end(), strText_.begin(), [](unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });
        return strText_;
    }

    bool _IsSupportedWorkpieceModelPath(IN const std::string& strSourcePath_)
    {
        const auto _Extension = _ToLowerASCII(std::filesystem::path(strSourcePath_).extension().string());
        return _Extension == ".step" || _Extension == ".stp" || _Extension == ".igs" || _Extension == ".iges";
    }

    void _RequireImportedResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::string& strResourceID_,
        IN const std::string& strFieldName_)
    {
        if (strResourceID_.empty())
        {
            throw std::runtime_error("Resource import returned empty resource id: " + strFieldName_);
        }
        if (Scene_.Resources().GetVersion(strResourceID_) == 0)
        {
            throw std::runtime_error("Resource import returned resource id that was not saved: " + strFieldName_);
        }
    }

    SImportedCadResources _ImportCadModel(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::string& strSourcePath_,
        IN double dTolerance_)
    {
        iCAX::Resource::CResourceImportRequest _Request;
        _Request.SourcePath = strSourcePath_;
        _Request.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
        _Request.Options["tolerance"] = std::to_string(dTolerance_);

        auto& _Resources = Scene_.Resources();
        iCAX::Resource::CResourceImportResult _Result;
        auto _pBRep = _Resources.Import<iCAX::GeometryData::BRepModel>(_Request, &_Result);
        if (!_Result.IsOK())
        {
            throw std::runtime_error(_Result.Error.empty() ? "CAD resource import failed" : _Result.Error);
        }

        SImportedCadResources _Imported;
        _Imported.ModelResourceID = _FindImportedResourceID(_Result, "source");
        if (_Imported.ModelResourceID.empty())
        {
            _Imported.ModelResourceID = _Result.PrimaryResourceID;
        }
        _Imported.BRepResourceID = _FindImportedResourceID(_Result, "geometry.brep");
        _RequireImportedResource(Scene_, _Imported.ModelResourceID, "modelResourceId");
        _RequireImportedResource(Scene_, _Imported.BRepResourceID, "brepResourceId");

        if (!_pBRep)
        {
            throw std::runtime_error("Resource import returned BRep resource with unexpected runtime type");
        }

        _Imported.TopologyResourceID = iCAX::CAM::MakeTopologyResourceID(_Imported.ModelResourceID);
        _Imported.nTopologyVersion = _NextResourceVersion(_Resources, _Imported.TopologyResourceID);
        auto _pTopology = _MakeTopologyResourceFromBRep(*_pBRep, _GetDisplayNameFromPath(strSourcePath_), _Imported.nTopologyVersion);
        _pTopology->Metadata["importMode"] = _Result.Metadata.contains("importer") ? _Result.Metadata.at("importer") : std::string("resource-import");
        _pTopology->Metadata["sourcePath"] = strSourcePath_;
        _pTopology->Metadata["sourceResourceId"] = _Imported.ModelResourceID;
        _pTopology->Metadata["brepResourceId"] = _Imported.BRepResourceID;
        if (auto _ContentHash = _Result.Metadata.find("contentHash"); _ContentHash != _Result.Metadata.end())
        {
            _pTopology->Metadata["contentHash"] = _ContentHash->second;
        }

        auto _TopologyInfo = _MakeResourceInfo(
            _Imported.TopologyResourceID,
            _GetDisplayNameFromPath(strSourcePath_) + " topology",
            "topology",
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _Imported.nTopologyVersion);
        _TopologyInfo.Metadata["sourceResourceId"] = _Imported.ModelResourceID;
        _TopologyInfo.Metadata["brepResourceId"] = _Imported.BRepResourceID;
        _Resources.Set<iCAX::CAM::CTopologyResource>(_Imported.TopologyResourceID, _pTopology, _TopologyInfo);

        _RequireImportedResource(Scene_, _Imported.TopologyResourceID, "topologyResourceId");
        return _Imported;
    }

}
