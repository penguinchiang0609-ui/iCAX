#include "pch.h"
#include "PDORenderService.h"

#include "RenderPDO/RenderPDOLayouts.h"
#include "RenderPDO/RenderPDOValidation.h"
#include "PDO/PDOLease.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace iCAX::Render;

    template<typename T>
    uint64_t CheckedBytes(IN size_t nCount_)
    {
        if (nCount_ > (std::numeric_limits<uint64_t>::max)() / sizeof(T))
        {
            throw std::overflow_error("Render PDO payload byte count overflows");
        }
        return static_cast<uint64_t>(nCount_ * sizeof(T));
    }

    uint64_t AppendRaw(IN OUT std::vector<std::byte>& Payload_, IN const void* pData_, IN uint64_t nBytes_)
    {
        if (nBytes_ == 0)
        {
            return 0;
        }
        const uint64_t _Offset = static_cast<uint64_t>(Payload_.size());
        Payload_.resize(Payload_.size() + static_cast<size_t>(nBytes_));
        std::memcpy(Payload_.data() + _Offset, pData_, static_cast<size_t>(nBytes_));
        return _Offset;
    }

    template<typename T>
    uint64_t AppendArray(IN OUT std::vector<std::byte>& Payload_, IN const std::vector<T>& Values_)
    {
        return AppendRaw(Payload_, Values_.data(), CheckedBytes<T>(Values_.size()));
    }

    iCAX::RenderPDO::SFloat3 ToRenderVec3(IN const SFloat3& Value_)
    {
        return { Value_.x, Value_.y, Value_.z };
    }

    iCAX::RenderPDO::SRenderAABB ToRenderAABB(IN const SRenderAABB& Value_)
    {
        return { ToRenderVec3(Value_.Min), ToRenderVec3(Value_.Max) };
    }

    iCAX::RenderPDO::SMatrix4 ToRenderMatrix(IN const SMatrix4& Value_)
    {
        iCAX::RenderPDO::SMatrix4 _Matrix;
        _Matrix.Values = Value_.Values;
        return _Matrix;
    }

    uint32_t ToRenderTopology(IN ERenderTopology eTopology_)
    {
        switch (eTopology_)
        {
        case ERenderTopology::TriangleList:
            return static_cast<uint32_t>(iCAX::RenderPDO::ERenderTopology::TriangleList);
        case ERenderTopology::LineList:
            return static_cast<uint32_t>(iCAX::RenderPDO::ERenderTopology::LineList);
        case ERenderTopology::LineStrip:
            return static_cast<uint32_t>(iCAX::RenderPDO::ERenderTopology::LineStrip);
        case ERenderTopology::PointList:
            return static_cast<uint32_t>(iCAX::RenderPDO::ERenderTopology::PointList);
        default:
            throw std::invalid_argument("Render topology is invalid");
        }
    }

    uint32_t ToRenderGeometryKind(IN ERenderGeometryKind eKind_)
    {
        switch (eKind_)
        {
        case ERenderGeometryKind::Mesh:
            return static_cast<uint32_t>(iCAX::RenderPDO::ERenderGeometryKind::Mesh);
        case ERenderGeometryKind::Polyline:
            return static_cast<uint32_t>(iCAX::RenderPDO::ERenderGeometryKind::Polyline);
        case ERenderGeometryKind::Toolpath:
            return static_cast<uint32_t>(iCAX::RenderPDO::ERenderGeometryKind::Toolpath);
        default:
            throw std::invalid_argument("Render geometry kind is invalid");
        }
    }

    uint32_t ToRenderClass(IN ERenderClass eClass_)
    {
        return static_cast<uint32_t>(eClass_);
    }

    uint32_t ToRenderToolpathMoveKind(IN EToolpathMoveKind eMoveKind_)
    {
        switch (eMoveKind_)
        {
        case EToolpathMoveKind::Rapid:
            return static_cast<uint32_t>(iCAX::RenderPDO::EToolpathMoveKind::Rapid);
        case EToolpathMoveKind::Cutting:
            return static_cast<uint32_t>(iCAX::RenderPDO::EToolpathMoveKind::Cutting);
        case EToolpathMoveKind::Link:
            return static_cast<uint32_t>(iCAX::RenderPDO::EToolpathMoveKind::Link);
        case EToolpathMoveKind::LeadIn:
            return static_cast<uint32_t>(iCAX::RenderPDO::EToolpathMoveKind::LeadIn);
        case EToolpathMoveKind::LeadOut:
            return static_cast<uint32_t>(iCAX::RenderPDO::EToolpathMoveKind::LeadOut);
        case EToolpathMoveKind::Dwell:
            return static_cast<uint32_t>(iCAX::RenderPDO::EToolpathMoveKind::Dwell);
        default:
            throw std::invalid_argument("Render toolpath move kind is invalid");
        }
    }

    iCAX::RenderPDO::SRenderPolylineRangeData ToRenderPolylineRange(IN const SRenderPolylineRangeData& Value_)
    {
        iCAX::RenderPDO::SRenderPolylineRangeData _Range;
        _Range.nFirstPoint = Value_.nFirstPoint;
        _Range.nPointCount = Value_.nPointCount;
        _Range.nRenderClass = ToRenderClass(Value_.eRenderClass);
        _Range.nStyleID = Value_.nStyleID;
        _Range.nFlags = Value_.nFlags;
        return _Range;
    }

    iCAX::RenderPDO::SRenderToolpathPointData ToRenderToolpathPoint(IN const SRenderToolpathPointData& Value_)
    {
        iCAX::RenderPDO::SRenderToolpathPointData _Point;
        _Point.Position = ToRenderVec3(Value_.Position);
        _Point.ToolAxis = ToRenderVec3(Value_.ToolAxis);
        _Point.nFeed = Value_.nFeed;
        _Point.nFlags = Value_.nFlags;
        return _Point;
    }

    iCAX::RenderPDO::SRenderToolpathSpanData ToRenderToolpathSpan(IN const SRenderToolpathSpanData& Value_)
    {
        iCAX::RenderPDO::SRenderToolpathSpanData _Span;
        _Span.nFirstPoint = Value_.nFirstPoint;
        _Span.nPointCount = Value_.nPointCount;
        _Span.nMoveKind = ToRenderToolpathMoveKind(Value_.eMoveKind);
        _Span.nToolID = Value_.nToolID;
        _Span.nStyleID = Value_.nStyleID;
        _Span.nFlags = Value_.nFlags;
        return _Span;
    }

    void ValidateVersion(IN RenderDataVersion nDataVersion_)
    {
        if (nDataVersion_ == 0)
        {
            throw std::invalid_argument("Render data version cannot be zero");
        }
    }

    void ValidateMesh(IN const SRenderMeshData& Mesh_)
    {
        if (Mesh_.nGeometryID == kInvalidRenderGeometryID)
        {
            throw std::invalid_argument("Render mesh geometry id cannot be zero");
        }
        ValidateVersion(Mesh_.nDataVersion);
        if (Mesh_.Positions.empty())
        {
            throw std::invalid_argument("Render mesh positions cannot be empty");
        }
        if (!Mesh_.Normals.empty() && Mesh_.Normals.size() != Mesh_.Positions.size())
        {
            throw std::invalid_argument("Render mesh normal count must match position count");
        }
        if (!Mesh_.VertexColorsRGBA.empty() && Mesh_.VertexColorsRGBA.size() != Mesh_.Positions.size())
        {
            throw std::invalid_argument("Render mesh vertex color count must match position count");
        }
        if (Mesh_.eTopology == ERenderTopology::TriangleList)
        {
            if (!Mesh_.Indices.empty() && Mesh_.Indices.size() % 3 != 0)
            {
                throw std::invalid_argument("Render mesh triangle index count must be a multiple of 3");
            }
            if (Mesh_.Indices.empty() && Mesh_.Positions.size() % 3 != 0)
            {
                throw std::invalid_argument("Render mesh non-indexed triangle vertex count must be a multiple of 3");
            }
        }
    }

    void ValidatePolyline(IN const SRenderPolylineData& Polyline_)
    {
        if (Polyline_.nGeometryID == kInvalidRenderGeometryID)
        {
            throw std::invalid_argument("Render polyline geometry id cannot be zero");
        }
        ValidateVersion(Polyline_.nDataVersion);
        if (Polyline_.Points.empty() || Polyline_.Ranges.empty())
        {
            throw std::invalid_argument("Render polyline points and ranges cannot be empty");
        }
        for (const auto& _Range : Polyline_.Ranges)
        {
            if (_Range.nPointCount == 0)
            {
                throw std::invalid_argument("Render polyline range point count cannot be zero");
            }
            if (static_cast<size_t>(_Range.nFirstPoint) >= Polyline_.Points.size()
                || static_cast<size_t>(_Range.nPointCount) > Polyline_.Points.size() - static_cast<size_t>(_Range.nFirstPoint))
            {
                throw std::invalid_argument("Render polyline range exceeds point array");
            }
        }
    }

    void ValidateToolpath(IN const SRenderToolpathData& Toolpath_)
    {
        if (Toolpath_.nGeometryID == kInvalidRenderGeometryID)
        {
            throw std::invalid_argument("Render toolpath geometry id cannot be zero");
        }
        ValidateVersion(Toolpath_.nDataVersion);
        if (Toolpath_.Points.empty() || Toolpath_.Spans.empty())
        {
            throw std::invalid_argument("Render toolpath points and spans cannot be empty");
        }
        for (const auto& _Span : Toolpath_.Spans)
        {
            if (_Span.nPointCount == 0)
            {
                throw std::invalid_argument("Render toolpath span point count cannot be zero");
            }
            if (static_cast<size_t>(_Span.nFirstPoint) >= Toolpath_.Points.size()
                || static_cast<size_t>(_Span.nPointCount) > Toolpath_.Points.size() - static_cast<size_t>(_Span.nFirstPoint))
            {
                throw std::invalid_argument("Render toolpath span exceeds point array");
            }
        }
    }

    void EnsurePayloadFitsSlot(IN const std::vector<std::byte>& Payload_, IN const iCAX::PDO::IPDOSlot& Slot_)
    {
        if (Payload_.size() > static_cast<size_t>(Slot_.GetHeader().nPayloadSize))
        {
            throw std::runtime_error("Render PDO payload exceeds target slot capacity");
        }
    }

    bool CommitPayloadToSlot(
        IN const std::vector<std::byte>& Payload_,
        IN iCAX::PDO::IPDOSlot& Slot_,
        IN RenderDataVersion nDataVersion_)
    {
        EnsurePayloadFitsSlot(Payload_, Slot_);
        auto _Lease = iCAX::PDO::CPDOWriteLease::TryBeginIfNewer(Slot_, nDataVersion_);
        if (!_Lease)
        {
            return false;
        }
        std::memcpy(_Lease->Data(), Payload_.data(), Payload_.size());
        _Lease->Commit();
        return true;
    }
}

iCAX::PDORenderService::CPDORenderService::CPDORenderService() = default;

iCAX::PDORenderService::CPDORenderService::~CPDORenderService() = default;

void iCAX::PDORenderService::CPDORenderService::OnLoad()
{
}

void iCAX::PDORenderService::CPDORenderService::OnUnload()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Projects.clear();
}

bool iCAX::PDORenderService::CPDORenderService::CreateScene(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto& _Scenes = m_Projects[ProjectID_];
    if (_Scenes.find(nSceneID_) != _Scenes.end())
    {
        return false;
    }

    iCAX::Render::SRenderSceneSnapshot _Scene;
    _Scene.nSceneID = nSceneID_;
    _Scenes.emplace(nSceneID_, std::move(_Scene));
    return true;
}

bool iCAX::PDORenderService::CPDORenderService::DestroyScene(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _ProjectIter = m_Projects.find(ProjectID_);
    if (_ProjectIter == m_Projects.end())
    {
        return false;
    }
    const bool _bRemoved = _ProjectIter->second.erase(nSceneID_) > 0;
    if (_ProjectIter->second.empty())
    {
        m_Projects.erase(_ProjectIter);
    }
    return _bRemoved;
}

void iCAX::PDORenderService::CPDORenderService::DestroyProject(IN const iCAX::Data::uuid& ProjectID_)
{
    if (ProjectID_.is_nil())
    {
        throw std::invalid_argument("Render project id cannot be nil");
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Projects.erase(ProjectID_);
}

bool iCAX::PDORenderService::CPDORenderService::HasScene(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_) const
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return FindSceneNoLock(ProjectID_, nSceneID_) != nullptr;
}

std::vector<iCAX::Render::RenderSceneID> iCAX::PDORenderService::CPDORenderService::ListSceneIDs(
    IN const iCAX::Data::uuid& ProjectID_) const
{
    if (ProjectID_.is_nil())
    {
        throw std::invalid_argument("Render project id cannot be nil");
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    std::vector<iCAX::Render::RenderSceneID> _IDs;
    auto _ProjectIter = m_Projects.find(ProjectID_);
    if (_ProjectIter == m_Projects.end())
    {
        return _IDs;
    }
    _IDs.reserve(_ProjectIter->second.size());
    for (const auto& [_SceneID, _Scene] : _ProjectIter->second)
    {
        _IDs.push_back(_SceneID);
    }
    std::sort(_IDs.begin(), _IDs.end());
    return _IDs;
}

bool iCAX::PDORenderService::CPDORenderService::ClearScene(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        return false;
    }
    *_pScene = iCAX::Render::SRenderSceneSnapshot{};
    _pScene->nSceneID = nSceneID_;
    return true;
}

bool iCAX::PDORenderService::CPDORenderService::UpsertMesh(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN const iCAX::Render::SRenderMeshData& Mesh_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    ValidateMesh(Mesh_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        return false;
    }
    _pScene->Meshes[Mesh_.nGeometryID] = Mesh_;
    return true;
}

bool iCAX::PDORenderService::CPDORenderService::UpsertPolyline(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN const iCAX::Render::SRenderPolylineData& Polyline_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    ValidatePolyline(Polyline_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        return false;
    }
    _pScene->Polylines[Polyline_.nGeometryID] = Polyline_;
    return true;
}

bool iCAX::PDORenderService::CPDORenderService::UpsertToolpath(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN const iCAX::Render::SRenderToolpathData& Toolpath_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    ValidateToolpath(Toolpath_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        return false;
    }
    _pScene->Toolpaths[Toolpath_.nGeometryID] = Toolpath_;
    return true;
}

bool iCAX::PDORenderService::CPDORenderService::RemoveGeometry(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Render::RenderGeometryID nGeometryID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    if (nGeometryID_ == iCAX::Render::kInvalidRenderGeometryID)
    {
        throw std::invalid_argument("Render geometry id cannot be zero");
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        return false;
    }
    const auto _Count =
        _pScene->Meshes.erase(nGeometryID_)
        + _pScene->Polylines.erase(nGeometryID_)
        + _pScene->Toolpaths.erase(nGeometryID_);
    return _Count > 0;
}

bool iCAX::PDORenderService::CPDORenderService::SetInstances(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN const std::vector<iCAX::Render::SRenderInstanceData>& Instances_,
    IN const std::vector<iCAX::Render::SRenderStyleData>& Styles_,
    IN iCAX::Render::RenderDataVersion nDataVersion_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    ValidateVersion(nDataVersion_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        return false;
    }
    _pScene->Instances = Instances_;
    _pScene->Styles = Styles_;
    _pScene->nInstanceDataVersion = nDataVersion_;
    return true;
}

bool iCAX::PDORenderService::CPDORenderService::SetViewStates(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN const std::vector<iCAX::Render::SRenderViewStateData>& Views_,
    IN uint32_t nActiveViewIndex_,
    IN iCAX::Render::RenderDataVersion nDataVersion_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    ValidateVersion(nDataVersion_);
    if (Views_.empty())
    {
        throw std::invalid_argument("Render view state list cannot be empty");
    }
    if (nActiveViewIndex_ >= Views_.size())
    {
        throw std::invalid_argument("Render active view index is out of range");
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        return false;
    }
    _pScene->Views = Views_;
    _pScene->nActiveViewIndex = nActiveViewIndex_;
    _pScene->nViewDataVersion = nDataVersion_;
    return true;
}

iCAX::Render::SRenderSceneSnapshot iCAX::PDORenderService::CPDORenderService::GetSceneSnapshot(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_) const
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    const auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        throw std::logic_error("Render scene does not exist");
    }
    return *_pScene;
}

bool iCAX::PDORenderService::CPDORenderService::WriteMeshToPDO(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Render::RenderGeometryID nGeometryID_,
    IN iCAX::PDO::IPDOSlot& Slot_) const
{
    auto _Snapshot = GetSceneSnapshot(ProjectID_, nSceneID_);
    auto _MeshIter = _Snapshot.Meshes.find(nGeometryID_);
    if (_MeshIter == _Snapshot.Meshes.end())
    {
        return false;
    }
    const auto& _Mesh = _MeshIter->second;

    std::vector<iCAX::RenderPDO::SFloat3> _Positions;
    _Positions.reserve(_Mesh.Positions.size());
    for (const auto& _Item : _Mesh.Positions)
    {
        _Positions.push_back(ToRenderVec3(_Item));
    }

    std::vector<iCAX::RenderPDO::SFloat3> _Normals;
    _Normals.reserve(_Mesh.Normals.size());
    for (const auto& _Item : _Mesh.Normals)
    {
        _Normals.push_back(ToRenderVec3(_Item));
    }

    iCAX::RenderPDO::SRenderMeshPDOHeader _Header;
    _Header.Header.nPayloadKind = static_cast<uint32_t>(iCAX::RenderPDO::ERenderPDOPayloadKind::Mesh);
    _Header.Header.nHeaderSize = sizeof(iCAX::RenderPDO::SRenderMeshPDOHeader);
    _Header.Header.nDataVersion = _Mesh.nDataVersion;
    _Header.nGeometryID = _Mesh.nGeometryID;
    _Header.Bounds = ToRenderAABB(_Mesh.Bounds);
    _Header.nTopology = ToRenderTopology(_Mesh.eTopology);
    _Header.nVertexCount = static_cast<uint32_t>(_Mesh.Positions.size());
    _Header.nIndexCount = static_cast<uint32_t>(_Mesh.Indices.size());
    _Header.nFlags = 0;

    std::vector<std::byte> _Payload(sizeof(iCAX::RenderPDO::SRenderMeshPDOHeader));
    _Header.nPositionsOffset = AppendArray(_Payload, _Positions);
    if (!_Normals.empty())
    {
        _Header.nFlags |= iCAX::RenderPDO::kMeshFlagHasNormals;
        _Header.nNormalsOffset = AppendArray(_Payload, _Normals);
    }
    if (!_Mesh.VertexColorsRGBA.empty())
    {
        _Header.nFlags |= iCAX::RenderPDO::kMeshFlagHasVertexColors;
        _Header.nVertexColorsOffset = AppendArray(_Payload, _Mesh.VertexColorsRGBA);
    }
    _Header.nIndicesOffset = AppendArray(_Payload, _Mesh.Indices);
    _Header.Header.nPayloadSize = static_cast<uint64_t>(_Payload.size());
    std::memcpy(_Payload.data(), &_Header, sizeof(_Header));

    std::string _Error;
    if (!iCAX::RenderPDO::ValidateMeshPDOHeader(_Header, Slot_.GetHeader().nPayloadSize, &_Error))
    {
        throw std::runtime_error("Serialized mesh PDO is invalid: " + _Error);
    }
    return CommitPayloadToSlot(_Payload, Slot_, _Mesh.nDataVersion);
}

bool iCAX::PDORenderService::CPDORenderService::WritePolylineToPDO(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Render::RenderGeometryID nGeometryID_,
    IN iCAX::PDO::IPDOSlot& Slot_) const
{
    auto _Snapshot = GetSceneSnapshot(ProjectID_, nSceneID_);
    auto _PolylineIter = _Snapshot.Polylines.find(nGeometryID_);
    if (_PolylineIter == _Snapshot.Polylines.end())
    {
        return false;
    }
    const auto& _Polyline = _PolylineIter->second;

    std::vector<iCAX::RenderPDO::SFloat3> _Points;
    _Points.reserve(_Polyline.Points.size());
    for (const auto& _Item : _Polyline.Points)
    {
        _Points.push_back(ToRenderVec3(_Item));
    }

    std::vector<iCAX::RenderPDO::SRenderPolylineRangeData> _Ranges;
    _Ranges.reserve(_Polyline.Ranges.size());
    for (const auto& _Item : _Polyline.Ranges)
    {
        _Ranges.push_back(ToRenderPolylineRange(_Item));
    }

    iCAX::RenderPDO::SRenderPolylinePDOHeader _Header;
    _Header.Header.nPayloadKind = static_cast<uint32_t>(iCAX::RenderPDO::ERenderPDOPayloadKind::Polyline);
    _Header.Header.nHeaderSize = sizeof(iCAX::RenderPDO::SRenderPolylinePDOHeader);
    _Header.Header.nDataVersion = _Polyline.nDataVersion;
    _Header.nGeometryID = _Polyline.nGeometryID;
    _Header.Bounds = ToRenderAABB(_Polyline.Bounds);
    _Header.nPointCount = static_cast<uint32_t>(_Points.size());
    _Header.nRangeCount = static_cast<uint32_t>(_Ranges.size());

    std::vector<std::byte> _Payload(sizeof(iCAX::RenderPDO::SRenderPolylinePDOHeader));
    _Header.nPointsOffset = AppendArray(_Payload, _Points);
    _Header.nRangesOffset = AppendArray(_Payload, _Ranges);
    _Header.Header.nPayloadSize = static_cast<uint64_t>(_Payload.size());
    std::memcpy(_Payload.data(), &_Header, sizeof(_Header));

    std::string _Error;
    if (!iCAX::RenderPDO::ValidatePolylinePDOHeader(_Header, Slot_.GetHeader().nPayloadSize, &_Error))
    {
        throw std::runtime_error("Serialized polyline PDO is invalid: " + _Error);
    }
    return CommitPayloadToSlot(_Payload, Slot_, _Polyline.nDataVersion);
}

bool iCAX::PDORenderService::CPDORenderService::WriteToolpathToPDO(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Render::RenderGeometryID nGeometryID_,
    IN iCAX::PDO::IPDOSlot& Slot_) const
{
    auto _Snapshot = GetSceneSnapshot(ProjectID_, nSceneID_);
    auto _ToolpathIter = _Snapshot.Toolpaths.find(nGeometryID_);
    if (_ToolpathIter == _Snapshot.Toolpaths.end())
    {
        return false;
    }
    const auto& _Toolpath = _ToolpathIter->second;

    std::vector<iCAX::RenderPDO::SRenderToolpathPointData> _Points;
    _Points.reserve(_Toolpath.Points.size());
    for (const auto& _Item : _Toolpath.Points)
    {
        _Points.push_back(ToRenderToolpathPoint(_Item));
    }

    std::vector<iCAX::RenderPDO::SRenderToolpathSpanData> _Spans;
    _Spans.reserve(_Toolpath.Spans.size());
    for (const auto& _Item : _Toolpath.Spans)
    {
        _Spans.push_back(ToRenderToolpathSpan(_Item));
    }

    iCAX::RenderPDO::SRenderToolpathPDOHeader _Header;
    _Header.Header.nPayloadKind = static_cast<uint32_t>(iCAX::RenderPDO::ERenderPDOPayloadKind::Toolpath);
    _Header.Header.nHeaderSize = sizeof(iCAX::RenderPDO::SRenderToolpathPDOHeader);
    _Header.Header.nDataVersion = _Toolpath.nDataVersion;
    _Header.nGeometryID = _Toolpath.nGeometryID;
    _Header.Bounds = ToRenderAABB(_Toolpath.Bounds);
    _Header.nPointCount = static_cast<uint32_t>(_Points.size());
    _Header.nSpanCount = static_cast<uint32_t>(_Spans.size());

    std::vector<std::byte> _Payload(sizeof(iCAX::RenderPDO::SRenderToolpathPDOHeader));
    _Header.nPointsOffset = AppendArray(_Payload, _Points);
    _Header.nSpansOffset = AppendArray(_Payload, _Spans);
    _Header.Header.nPayloadSize = static_cast<uint64_t>(_Payload.size());
    std::memcpy(_Payload.data(), &_Header, sizeof(_Header));

    std::string _Error;
    if (!iCAX::RenderPDO::ValidateToolpathPDOHeader(_Header, Slot_.GetHeader().nPayloadSize, &_Error))
    {
        throw std::runtime_error("Serialized toolpath PDO is invalid: " + _Error);
    }
    return CommitPayloadToSlot(_Payload, Slot_, _Toolpath.nDataVersion);
}

bool iCAX::PDORenderService::CPDORenderService::WriteInstanceListToPDO(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::PDO::IPDOSlot& Slot_) const
{
    auto _Snapshot = GetSceneSnapshot(ProjectID_, nSceneID_);
    if (_Snapshot.nInstanceDataVersion == 0)
    {
        return false;
    }

    std::vector<iCAX::RenderPDO::SRenderInstanceData> _Instances;
    _Instances.reserve(_Snapshot.Instances.size());
    for (const auto& _Item : _Snapshot.Instances)
    {
        iCAX::RenderPDO::SRenderInstanceData _Target;
        _Target.nObjectID = _Item.nObjectID;
        _Target.nGeometryID = _Item.nGeometryID;
        _Target.nGeometryKind = ToRenderGeometryKind(_Item.eGeometryKind);
        _Target.nRenderClass = ToRenderClass(_Item.eRenderClass);
        _Target.nStyleID = _Item.nStyleID;
        _Target.nFlags = _Item.nFlags;
        _Target.Transform = ToRenderMatrix(_Item.Transform);
        _Instances.push_back(_Target);
    }

    std::vector<iCAX::RenderPDO::SRenderStyleData> _Styles;
    _Styles.reserve(_Snapshot.Styles.size());
    for (const auto& _Item : _Snapshot.Styles)
    {
        _Styles.push_back({ _Item.nStyleID, _Item.nColorRGBA, _Item.nLineWidth, _Item.nFlags });
    }

    iCAX::RenderPDO::SRenderInstanceListPDOHeader _Header;
    _Header.Header.nPayloadKind = static_cast<uint32_t>(iCAX::RenderPDO::ERenderPDOPayloadKind::InstanceList);
    _Header.Header.nHeaderSize = sizeof(iCAX::RenderPDO::SRenderInstanceListPDOHeader);
    _Header.Header.nDataVersion = _Snapshot.nInstanceDataVersion;
    _Header.nInstanceCount = static_cast<uint32_t>(_Instances.size());
    _Header.nStyleCount = static_cast<uint32_t>(_Styles.size());

    std::vector<std::byte> _Payload(sizeof(iCAX::RenderPDO::SRenderInstanceListPDOHeader));
    _Header.nInstancesOffset = AppendArray(_Payload, _Instances);
    _Header.nStylesOffset = AppendArray(_Payload, _Styles);
    _Header.Header.nPayloadSize = static_cast<uint64_t>(_Payload.size());
    std::memcpy(_Payload.data(), &_Header, sizeof(_Header));

    std::string _Error;
    if (!iCAX::RenderPDO::ValidateInstanceListPDOHeader(_Header, Slot_.GetHeader().nPayloadSize, &_Error))
    {
        throw std::runtime_error("Serialized instance list PDO is invalid: " + _Error);
    }
    return CommitPayloadToSlot(_Payload, Slot_, _Snapshot.nInstanceDataVersion);
}

bool iCAX::PDORenderService::CPDORenderService::WriteViewStatesToPDO(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::PDO::IPDOSlot& Slot_) const
{
    auto _Snapshot = GetSceneSnapshot(ProjectID_, nSceneID_);
    if (_Snapshot.nViewDataVersion == 0)
    {
        return false;
    }

    std::vector<iCAX::RenderPDO::SRenderViewStateData> _Views;
    _Views.reserve(_Snapshot.Views.size());
    for (const auto& _Item : _Snapshot.Views)
    {
        iCAX::RenderPDO::SRenderViewStateData _Target;
        _Target.nViewID = _Item.nViewID;
        _Target.nWidth = _Item.nWidth;
        _Target.nHeight = _Item.nHeight;
        _Target.nDpiScale = _Item.nDpiScale;
        _Target.nFlags = _Item.nFlags;
        _Target.Eye = ToRenderVec3(_Item.Eye);
        _Target.Target = ToRenderVec3(_Item.Target);
        _Target.Up = ToRenderVec3(_Item.Up);
        _Target.nNearPlane = _Item.nNearPlane;
        _Target.nFarPlane = _Item.nFarPlane;
        _Target.nVerticalFovRadians = _Item.nVerticalFovRadians;
        _Target.nOrthographicHeight = _Item.nOrthographicHeight;
        _Target.ViewMatrix = ToRenderMatrix(_Item.ViewMatrix);
        _Target.ProjectionMatrix = ToRenderMatrix(_Item.ProjectionMatrix);
        _Views.push_back(_Target);
    }

    iCAX::RenderPDO::SRenderViewStatePDOHeader _Header;
    _Header.Header.nPayloadKind = static_cast<uint32_t>(iCAX::RenderPDO::ERenderPDOPayloadKind::ViewState);
    _Header.Header.nHeaderSize = sizeof(iCAX::RenderPDO::SRenderViewStatePDOHeader);
    _Header.Header.nDataVersion = _Snapshot.nViewDataVersion;
    _Header.nViewCount = static_cast<uint32_t>(_Views.size());
    _Header.nActiveViewIndex = _Snapshot.nActiveViewIndex;

    std::vector<std::byte> _Payload(sizeof(iCAX::RenderPDO::SRenderViewStatePDOHeader));
    _Header.nViewsOffset = AppendArray(_Payload, _Views);
    _Header.Header.nPayloadSize = static_cast<uint64_t>(_Payload.size());
    std::memcpy(_Payload.data(), &_Header, sizeof(_Header));

    std::string _Error;
    if (!iCAX::RenderPDO::ValidateViewStatePDOHeader(_Header, Slot_.GetHeader().nPayloadSize, &_Error))
    {
        throw std::runtime_error("Serialized view state PDO is invalid: " + _Error);
    }
    return CommitPayloadToSlot(_Payload, Slot_, _Snapshot.nViewDataVersion);
}

iCAX::Render::SRenderSceneSnapshot* iCAX::PDORenderService::CPDORenderService::FindSceneNoLock(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_)
{
    auto _ProjectIter = m_Projects.find(ProjectID_);
    if (_ProjectIter == m_Projects.end())
    {
        return nullptr;
    }
    auto _SceneIter = _ProjectIter->second.find(nSceneID_);
    if (_SceneIter == _ProjectIter->second.end())
    {
        return nullptr;
    }
    return &_SceneIter->second;
}

const iCAX::Render::SRenderSceneSnapshot* iCAX::PDORenderService::CPDORenderService::FindSceneNoLock(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_) const
{
    auto _ProjectIter = m_Projects.find(ProjectID_);
    if (_ProjectIter == m_Projects.end())
    {
        return nullptr;
    }
    auto _SceneIter = _ProjectIter->second.find(nSceneID_);
    if (_SceneIter == _ProjectIter->second.end())
    {
        return nullptr;
    }
    return &_SceneIter->second;
}

void iCAX::PDORenderService::CPDORenderService::ValidateProjectAndScene(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_)
{
    if (ProjectID_.is_nil())
    {
        throw std::invalid_argument("Render project id cannot be nil");
    }
    if (nSceneID_ == iCAX::Render::kInvalidRenderSceneID)
    {
        throw std::invalid_argument("Render scene id cannot be zero");
    }
}
