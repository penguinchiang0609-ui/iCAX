#include "pch.h"
#include "PDORenderService.h"

#include "Mailbox/MailPayload.h"
#include "RenderPDO/RenderPDODecl.h"
#include "RenderPDO/RenderPDOLayouts.h"
#include "RenderPDO/RenderPDOValidation.h"
#include "PDO/IPDOHub.h"
#include "PDO/PDOLease.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
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

    uint64_t CheckedAdd(IN uint64_t nLeft_, IN uint64_t nRight_)
    {
        if (nLeft_ > (std::numeric_limits<uint64_t>::max)() - nRight_)
        {
            throw std::overflow_error("Render PDO payload byte count overflows");
        }
        return nLeft_ + nRight_;
    }

    iCAX::RenderPDO::ERenderPDOPayloadKind ToPDOPayloadKind(IN ERenderGeometryKind eKind_)
    {
        switch (eKind_)
        {
        case ERenderGeometryKind::Mesh:
            return iCAX::RenderPDO::ERenderPDOPayloadKind::Mesh;
        case ERenderGeometryKind::Polyline:
            return iCAX::RenderPDO::ERenderPDOPayloadKind::Polyline;
        case ERenderGeometryKind::Toolpath:
            return iCAX::RenderPDO::ERenderPDOPayloadKind::Toolpath;
        default:
            throw std::invalid_argument("Render geometry kind is invalid");
        }
    }

    const char* GetGeometryKindName(IN ERenderGeometryKind eKind_)
    {
        switch (eKind_)
        {
        case ERenderGeometryKind::Mesh:
            return "Mesh";
        case ERenderGeometryKind::Polyline:
            return "Polyline";
        case ERenderGeometryKind::Toolpath:
            return "Toolpath";
        default:
            throw std::invalid_argument("Render geometry kind is invalid");
        }
    }

    std::string MakeScenePrefix(
        IN const iCAX::Data::uuid& ProjectID_,
        IN RenderSceneID nSceneID_)
    {
        std::ostringstream _Stream;
        _Stream << iCAX::Data::to_string(ProjectID_) << ".scene." << nSceneID_;
        return _Stream.str();
    }

    std::string MakeGeometryInstanceName(
        IN const iCAX::Data::uuid& ProjectID_,
        IN RenderSceneID nSceneID_,
        IN ERenderGeometryKind eGeometryKind_,
        IN RenderGeometryID nGeometryID_)
    {
        std::ostringstream _Stream;
        _Stream << MakeScenePrefix(ProjectID_, nSceneID_)
            << ".geometry." << GetGeometryKindName(eGeometryKind_) << "." << nGeometryID_;
        return _Stream.str();
    }

    std::string MakeObjectInstanceName(
        IN const iCAX::Data::uuid& ProjectID_,
        IN RenderSceneID nSceneID_,
        IN RenderObjectID nObjectID_)
    {
        std::ostringstream _Stream;
        _Stream << MakeScenePrefix(ProjectID_, nSceneID_) << ".object." << nObjectID_;
        return _Stream.str();
    }

    std::string MakeViewInstanceName(
        IN const iCAX::Data::uuid& ProjectID_,
        IN RenderSceneID nSceneID_)
    {
        std::ostringstream _Stream;
        _Stream << MakeScenePrefix(ProjectID_, nSceneID_) << ".view";
        return _Stream.str();
    }

    uint64_t CalculateMeshPayloadCapacity(IN const SRenderMeshData& Mesh_)
    {
        uint64_t _Size = sizeof(iCAX::RenderPDO::SRenderMeshPDOHeader);
        _Size = CheckedAdd(_Size, CheckedBytes<iCAX::RenderPDO::SFloat3>(Mesh_.Positions.size()));
        _Size = CheckedAdd(_Size, CheckedBytes<iCAX::RenderPDO::SFloat3>(Mesh_.Normals.size()));
        _Size = CheckedAdd(_Size, CheckedBytes<uint32_t>(Mesh_.VertexColorsRGBA.size()));
        _Size = CheckedAdd(_Size, CheckedBytes<uint32_t>(Mesh_.Indices.size()));
        return _Size;
    }

    uint64_t CalculatePolylinePayloadCapacity(IN const SRenderPolylineData& Polyline_)
    {
        uint64_t _Size = sizeof(iCAX::RenderPDO::SRenderPolylinePDOHeader);
        _Size = CheckedAdd(_Size, CheckedBytes<iCAX::RenderPDO::SFloat3>(Polyline_.Points.size()));
        _Size = CheckedAdd(_Size, CheckedBytes<iCAX::RenderPDO::SRenderPolylineRangeData>(Polyline_.Ranges.size()));
        return _Size;
    }

    uint64_t CalculateToolpathPayloadCapacity(IN const SRenderToolpathData& Toolpath_)
    {
        uint64_t _Size = sizeof(iCAX::RenderPDO::SRenderToolpathPDOHeader);
        _Size = CheckedAdd(_Size, CheckedBytes<iCAX::RenderPDO::SRenderToolpathPointData>(Toolpath_.Points.size()));
        _Size = CheckedAdd(_Size, CheckedBytes<iCAX::RenderPDO::SRenderToolpathSpanData>(Toolpath_.Spans.size()));
        return _Size;
    }

    uint64_t CalculateObjectPayloadCapacity()
    {
        uint64_t _Size = sizeof(iCAX::RenderPDO::SRenderInstanceListPDOHeader);
        _Size = CheckedAdd(_Size, sizeof(iCAX::RenderPDO::SRenderInstanceData));
        _Size = CheckedAdd(_Size, sizeof(iCAX::RenderPDO::SRenderStyleData));
        return _Size;
    }

    uint64_t CalculateViewPayloadCapacity(IN const SRenderSceneSnapshot& Snapshot_)
    {
        uint64_t _Size = sizeof(iCAX::RenderPDO::SRenderViewStatePDOHeader);
        _Size = CheckedAdd(_Size, CheckedBytes<iCAX::RenderPDO::SRenderViewStateData>(Snapshot_.Views.size()));
        return _Size;
    }

    iCAX::PDO::PDODecl MakeRenderDeclWithID(
        IN iCAX::RenderPDO::ERenderPDOPayloadKind ePayloadKind_,
        IN iCAX::PDO::PDOID nPDOID_,
        IN uint64_t nPayloadCapacity_)
    {
        auto _Decl = iCAX::RenderPDO::MakeRenderPDODecl(
            ePayloadKind_,
            "temporary.instance.name.for.dynamic.render.pdo",
            iCAX::PDO::kDirection2External,
            nPayloadCapacity_);
        _Decl.nID = nPDOID_;
        return _Decl;
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
    m_PDOOutputs.clear();
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
    m_PDOOutputs.erase(ProjectID_);
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

void iCAX::PDORenderService::CPDORenderService::Update(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN iCAX::Project::ISceneContext& SceneContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_)
{
    (void)ApplicationContext_;
    (void)ProductContext_;
    (void)nDeltaTime_;
    (void)nTotalTime_;

    const auto& _ProjectID = ProjectContext_.GetProjectID();
    if (_ProjectID.is_nil())
    {
        throw std::invalid_argument("Render project id cannot be nil");
    }

    std::unordered_map<iCAX::Render::RenderSceneID, iCAX::Render::SRenderSceneSnapshot> _Scenes;
    std::unordered_map<iCAX::Render::RenderSceneID, SScenePDOOutputState> _States;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        auto _ProjectIter = m_Projects.find(_ProjectID);
        if (_ProjectIter != m_Projects.end())
        {
            _Scenes = _ProjectIter->second;
        }
        auto _StateProjectIter = m_PDOOutputs.find(_ProjectID);
        if (_StateProjectIter != m_PDOOutputs.end())
        {
            _States = _StateProjectIter->second;
        }
    }

    if (_Scenes.empty() && _States.empty())
    {
        return;
    }
    if (!SceneContext_.HasPDOHub())
    {
        throw std::logic_error("PDORenderService requires project PDO hub");
    }

    for (auto _StateIter = _States.begin(); _StateIter != _States.end();)
    {
        const auto _SceneIter = _Scenes.find(_StateIter->first);
        if (_SceneIter == _Scenes.end())
        {
            FreeScenePDOOutput(_ProjectID, _StateIter->first, SceneContext_, _StateIter->second);
            _StateIter = _States.erase(_StateIter);
            continue;
        }
        ++_StateIter;
    }

    for (const auto& [_SceneID, _Scene] : _Scenes)
    {
        auto& _State = _States[_SceneID];
        SynchronizeScenePDOOutput(_ProjectID, &_Scene, SceneContext_, _State);
    }

    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        if (_States.empty())
        {
            m_PDOOutputs.erase(_ProjectID);
        }
        else
        {
            m_PDOOutputs[_ProjectID] = std::move(_States);
        }
    }
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

iCAX::PDO::PDOID iCAX::PDORenderService::CPDORenderService::MakeGeometryPDOID(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Render::ERenderGeometryKind eGeometryKind_,
    IN iCAX::Render::RenderGeometryID nGeometryID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    if (nGeometryID_ == iCAX::Render::kInvalidRenderGeometryID)
    {
        throw std::invalid_argument("Render geometry id cannot be zero");
    }
    return iCAX::PDO::MakePDOID(
        iCAX::RenderPDO::GetRenderPDOPayloadTypeName(ToPDOPayloadKind(eGeometryKind_)),
        MakeGeometryInstanceName(ProjectID_, nSceneID_, eGeometryKind_, nGeometryID_));
}

iCAX::PDO::PDOID iCAX::PDORenderService::CPDORenderService::MakeObjectPDOID(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Render::RenderObjectID nObjectID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    if (nObjectID_ == iCAX::Render::kInvalidRenderObjectID)
    {
        throw std::invalid_argument("Render object id cannot be zero");
    }
    return iCAX::PDO::MakePDOID(
        iCAX::RenderPDO::GetRenderPDOPayloadTypeName(iCAX::RenderPDO::ERenderPDOPayloadKind::InstanceList),
        MakeObjectInstanceName(ProjectID_, nSceneID_, nObjectID_));
}

iCAX::PDO::PDOID iCAX::PDORenderService::CPDORenderService::MakeViewStatePDOID(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    return iCAX::PDO::MakePDOID(
        iCAX::RenderPDO::GetRenderPDOPayloadTypeName(iCAX::RenderPDO::ERenderPDOPayloadKind::ViewState),
        MakeViewInstanceName(ProjectID_, nSceneID_));
}

void iCAX::PDORenderService::CPDORenderService::NotifyPDODefragBegin(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Project::ISceneContext& SceneContext_) const
{
    SendDefragEvent(ProjectID_, SceneContext_, kPDORenderDefragBeginEvent, "DefragBegin", 0);
}

void iCAX::PDORenderService::CPDORenderService::NotifyPDOSlotMoved(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Project::ISceneContext& SceneContext_,
    IN iCAX::PDO::PDOID nPDOID_) const
{
    if (nPDOID_ == 0)
    {
        throw std::invalid_argument("PDO id cannot be zero");
    }
    SendDefragEvent(ProjectID_, SceneContext_, kPDORenderSlotMovedEvent, "SlotMoved", nPDOID_);
}

void iCAX::PDORenderService::CPDORenderService::NotifyPDODefragEnd(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Project::ISceneContext& SceneContext_) const
{
    SendDefragEvent(ProjectID_, SceneContext_, kPDORenderDefragEndEvent, "DefragEnd", 0);
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

bool iCAX::PDORenderService::CPDORenderService::WriteObjectToPDO(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Render::RenderObjectID nObjectID_,
    IN iCAX::PDO::IPDOSlot& Slot_) const
{
    auto _Snapshot = GetSceneSnapshot(ProjectID_, nSceneID_);
    if (_Snapshot.nInstanceDataVersion == 0)
    {
        return false;
    }

    const auto _InstanceIter = std::find_if(
        _Snapshot.Instances.begin(),
        _Snapshot.Instances.end(),
        [nObjectID_](IN const iCAX::Render::SRenderInstanceData& Item_)
        {
            return Item_.nObjectID == nObjectID_;
        });
    if (_InstanceIter == _Snapshot.Instances.end())
    {
        return false;
    }

    iCAX::RenderPDO::SRenderInstanceData _Instance;
    _Instance.nObjectID = _InstanceIter->nObjectID;
    _Instance.nGeometryID = _InstanceIter->nGeometryID;
    _Instance.nGeometryKind = ToRenderGeometryKind(_InstanceIter->eGeometryKind);
    _Instance.nRenderClass = ToRenderClass(_InstanceIter->eRenderClass);
    _Instance.nStyleID = _InstanceIter->nStyleID;
    _Instance.nFlags = _InstanceIter->nFlags;
    _Instance.Transform = ToRenderMatrix(_InstanceIter->Transform);

    iCAX::RenderPDO::SRenderStyleData _Style;
    _Style.nStyleID = _InstanceIter->nStyleID;
    const auto _StyleIter = std::find_if(
        _Snapshot.Styles.begin(),
        _Snapshot.Styles.end(),
        [_InstanceIter](IN const iCAX::Render::SRenderStyleData& Item_)
        {
            return Item_.nStyleID == _InstanceIter->nStyleID;
        });
    if (_StyleIter != _Snapshot.Styles.end())
    {
        _Style.nStyleID = _StyleIter->nStyleID;
        _Style.nColorRGBA = _StyleIter->nColorRGBA;
        _Style.nLineWidth = _StyleIter->nLineWidth;
        _Style.nFlags = _StyleIter->nFlags;
    }

    iCAX::RenderPDO::SRenderInstanceListPDOHeader _Header;
    _Header.Header.nPayloadKind = static_cast<uint32_t>(iCAX::RenderPDO::ERenderPDOPayloadKind::InstanceList);
    _Header.Header.nHeaderSize = sizeof(iCAX::RenderPDO::SRenderInstanceListPDOHeader);
    _Header.Header.nDataVersion = _Snapshot.nInstanceDataVersion;
    _Header.nInstanceCount = 1;
    _Header.nStyleCount = 1;

    std::vector<std::byte> _Payload(sizeof(iCAX::RenderPDO::SRenderInstanceListPDOHeader));
    _Header.nInstancesOffset = AppendRaw(_Payload, &_Instance, sizeof(_Instance));
    _Header.nStylesOffset = AppendRaw(_Payload, &_Style, sizeof(_Style));
    _Header.Header.nPayloadSize = static_cast<uint64_t>(_Payload.size());
    std::memcpy(_Payload.data(), &_Header, sizeof(_Header));

    std::string _Error;
    if (!iCAX::RenderPDO::ValidateInstanceListPDOHeader(_Header, Slot_.GetHeader().nPayloadSize, &_Error))
    {
        throw std::runtime_error("Serialized object PDO is invalid: " + _Error);
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

void iCAX::PDORenderService::CPDORenderService::SynchronizeScenePDOOutput(
    IN const iCAX::Data::uuid& ProjectID_,
    IN const iCAX::Render::SRenderSceneSnapshot* pScene_,
    IN iCAX::Project::ISceneContext& SceneContext_,
    IN OUT SScenePDOOutputState& State_) const
{
    if (!pScene_)
    {
        FreeScenePDOOutput(ProjectID_, iCAX::Render::kInvalidRenderSceneID, SceneContext_, State_);
        return;
    }

    auto& _PDOHub = SceneContext_.PDOHub();
    const auto _SceneID = pScene_->nSceneID;

    auto _FreeAssignment =
        [this, &ProjectID_, &_PDOHub, &SceneContext_, _SceneID](
            IN const char* pSlotRole_,
            IN const char* pPayloadKind_,
            IN iCAX::Render::RenderGeometryID nGeometryID_,
            IN iCAX::Render::RenderObjectID nObjectID_,
            IN OUT SSlotAssignment& Assignment_)
        {
            if (Assignment_.nPDOID == 0)
            {
                return;
            }
            if (_PDOHub.FreeSlot(Assignment_.nPDOID))
            {
                SendSlotEvent(
                    ProjectID_,
                    SceneContext_,
                    kPDORenderSlotFreedEvent,
                    "SlotFreed",
                    pSlotRole_,
                    _SceneID,
                    Assignment_.nPDOID,
                    nGeometryID_,
                    nObjectID_,
                    pPayloadKind_,
                    Assignment_.nPayloadCapacity);
            }
            Assignment_ = {};
        };

    iCAX::PDO::CPDOHubAllocationCallbacks _AllocationCallbacks;
    _AllocationCallbacks.OnDefragmentBegin =
        [this, &ProjectID_, &SceneContext_]()
        {
            NotifyPDODefragBegin(ProjectID_, SceneContext_);
        };
    _AllocationCallbacks.OnDefragmentEnd =
        [this, &ProjectID_, &SceneContext_](IN const std::vector<iCAX::PDO::CPDOHubDefragMove>& Moves_)
        {
            for (const auto& _Move : Moves_)
            {
                NotifyPDOSlotMoved(ProjectID_, SceneContext_, _Move.nPDOID);
            }
            NotifyPDODefragEnd(ProjectID_, SceneContext_);
        };

    auto _EnsureAssignment =
        [this, &ProjectID_, &_PDOHub, &SceneContext_, _SceneID, &_AllocationCallbacks](
            IN iCAX::RenderPDO::ERenderPDOPayloadKind ePayloadKind_,
            IN const char* pSlotRole_,
            IN iCAX::PDO::PDOID nPDOID_,
            IN iCAX::Render::RenderGeometryID nGeometryID_,
            IN iCAX::Render::RenderObjectID nObjectID_,
            IN uint64_t nPayloadCapacity_,
            IN OUT SSlotAssignment& Assignment_)
        {
            const char* _pPayloadKindName = iCAX::RenderPDO::GetRenderPDOPayloadTypeName(ePayloadKind_);
            if (Assignment_.nPDOID == 0)
            {
                const auto _Decl = MakeRenderDeclWithID(ePayloadKind_, nPDOID_, nPayloadCapacity_);
                _PDOHub.AllocateSlot(_Decl, _AllocationCallbacks);
                Assignment_.nPDOID = nPDOID_;
                Assignment_.nPayloadCapacity = nPayloadCapacity_;
                SendSlotEvent(
                    ProjectID_,
                    SceneContext_,
                    kPDORenderSlotAllocatedEvent,
                    "SlotAllocated",
                    pSlotRole_,
                    _SceneID,
                    nPDOID_,
                    nGeometryID_,
                    nObjectID_,
                    _pPayloadKindName,
                    nPayloadCapacity_);
                return;
            }

            if (Assignment_.nPDOID != nPDOID_)
            {
                throw std::logic_error("Render PDO slot assignment id is inconsistent");
            }

            if (Assignment_.nPayloadCapacity < nPayloadCapacity_)
            {
                _PDOHub.FreeSlot(Assignment_.nPDOID);
                const auto _Decl = MakeRenderDeclWithID(ePayloadKind_, nPDOID_, nPayloadCapacity_);
                _PDOHub.AllocateSlot(_Decl, _AllocationCallbacks);
                Assignment_.nPayloadCapacity = nPayloadCapacity_;
                SendSlotEvent(
                    ProjectID_,
                    SceneContext_,
                    kPDORenderSlotMovedEvent,
                    "SlotMoved",
                    pSlotRole_,
                    _SceneID,
                    nPDOID_,
                    nGeometryID_,
                    nObjectID_,
                    _pPayloadKindName,
                    nPayloadCapacity_);
            }
        };

    for (auto _Iter = State_.MeshSlots.begin(); _Iter != State_.MeshSlots.end();)
    {
        if (pScene_->Meshes.find(_Iter->first) == pScene_->Meshes.end())
        {
            _FreeAssignment("Geometry", "render.mesh", _Iter->first, 0, _Iter->second);
            _Iter = State_.MeshSlots.erase(_Iter);
            continue;
        }
        ++_Iter;
    }

    for (auto _Iter = State_.PolylineSlots.begin(); _Iter != State_.PolylineSlots.end();)
    {
        if (pScene_->Polylines.find(_Iter->first) == pScene_->Polylines.end())
        {
            _FreeAssignment("Geometry", "render.polyline", _Iter->first, 0, _Iter->second);
            _Iter = State_.PolylineSlots.erase(_Iter);
            continue;
        }
        ++_Iter;
    }

    for (auto _Iter = State_.ToolpathSlots.begin(); _Iter != State_.ToolpathSlots.end();)
    {
        if (pScene_->Toolpaths.find(_Iter->first) == pScene_->Toolpaths.end())
        {
            _FreeAssignment("Geometry", "render.toolpath", _Iter->first, 0, _Iter->second);
            _Iter = State_.ToolpathSlots.erase(_Iter);
            continue;
        }
        ++_Iter;
    }

    std::unordered_set<iCAX::Render::RenderObjectID> _LiveObjects;
    _LiveObjects.reserve(pScene_->Instances.size());
    for (const auto& _Instance : pScene_->Instances)
    {
        if (_Instance.nObjectID == iCAX::Render::kInvalidRenderObjectID)
        {
            throw std::invalid_argument("Render object id cannot be zero");
        }
        _LiveObjects.insert(_Instance.nObjectID);
    }

    for (auto _Iter = State_.ObjectSlots.begin(); _Iter != State_.ObjectSlots.end();)
    {
        if (_LiveObjects.find(_Iter->first) == _LiveObjects.end())
        {
            _FreeAssignment("Object", "render.instance_list", 0, _Iter->first, _Iter->second);
            _Iter = State_.ObjectSlots.erase(_Iter);
            continue;
        }
        ++_Iter;
    }

    for (const auto& [_GeometryID, _Mesh] : pScene_->Meshes)
    {
        auto& _Assignment = State_.MeshSlots[_GeometryID];
        const auto _PDOID = MakeGeometryPDOID(ProjectID_, _SceneID, iCAX::Render::ERenderGeometryKind::Mesh, _GeometryID);
        _EnsureAssignment(
            iCAX::RenderPDO::ERenderPDOPayloadKind::Mesh,
            "Geometry",
            _PDOID,
            _GeometryID,
            0,
            CalculateMeshPayloadCapacity(_Mesh),
            _Assignment);
        (void)WriteMeshToPDO(ProjectID_, _SceneID, _GeometryID, _PDOHub.GetSlot(_Assignment.nPDOID));
    }

    for (const auto& [_GeometryID, _Polyline] : pScene_->Polylines)
    {
        auto& _Assignment = State_.PolylineSlots[_GeometryID];
        const auto _PDOID = MakeGeometryPDOID(ProjectID_, _SceneID, iCAX::Render::ERenderGeometryKind::Polyline, _GeometryID);
        _EnsureAssignment(
            iCAX::RenderPDO::ERenderPDOPayloadKind::Polyline,
            "Geometry",
            _PDOID,
            _GeometryID,
            0,
            CalculatePolylinePayloadCapacity(_Polyline),
            _Assignment);
        (void)WritePolylineToPDO(ProjectID_, _SceneID, _GeometryID, _PDOHub.GetSlot(_Assignment.nPDOID));
    }

    for (const auto& [_GeometryID, _Toolpath] : pScene_->Toolpaths)
    {
        auto& _Assignment = State_.ToolpathSlots[_GeometryID];
        const auto _PDOID = MakeGeometryPDOID(ProjectID_, _SceneID, iCAX::Render::ERenderGeometryKind::Toolpath, _GeometryID);
        _EnsureAssignment(
            iCAX::RenderPDO::ERenderPDOPayloadKind::Toolpath,
            "Geometry",
            _PDOID,
            _GeometryID,
            0,
            CalculateToolpathPayloadCapacity(_Toolpath),
            _Assignment);
        (void)WriteToolpathToPDO(ProjectID_, _SceneID, _GeometryID, _PDOHub.GetSlot(_Assignment.nPDOID));
    }

    for (const auto& _Instance : pScene_->Instances)
    {
        auto& _Assignment = State_.ObjectSlots[_Instance.nObjectID];
        const auto _PDOID = MakeObjectPDOID(ProjectID_, _SceneID, _Instance.nObjectID);
        _EnsureAssignment(
            iCAX::RenderPDO::ERenderPDOPayloadKind::InstanceList,
            "Object",
            _PDOID,
            _Instance.nGeometryID,
            _Instance.nObjectID,
            CalculateObjectPayloadCapacity(),
            _Assignment);
        (void)WriteObjectToPDO(ProjectID_, _SceneID, _Instance.nObjectID, _PDOHub.GetSlot(_Assignment.nPDOID));
    }

    if (pScene_->Views.empty() || pScene_->nViewDataVersion == 0)
    {
        _FreeAssignment("ViewState", "render.view_state", 0, 0, State_.ViewSlot);
    }
    else
    {
        const auto _PDOID = MakeViewStatePDOID(ProjectID_, _SceneID);
        _EnsureAssignment(
            iCAX::RenderPDO::ERenderPDOPayloadKind::ViewState,
            "ViewState",
            _PDOID,
            0,
            0,
            CalculateViewPayloadCapacity(*pScene_),
            State_.ViewSlot);
        (void)WriteViewStatesToPDO(ProjectID_, _SceneID, _PDOHub.GetSlot(State_.ViewSlot.nPDOID));
    }
}

void iCAX::PDORenderService::CPDORenderService::FreeScenePDOOutput(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Project::ISceneContext& SceneContext_,
    IN OUT SScenePDOOutputState& State_) const
{
    auto& _PDOHub = SceneContext_.PDOHub();

    auto _FreeAssignment =
        [this, &ProjectID_, &_PDOHub, &SceneContext_, nSceneID_](
            IN const char* pSlotRole_,
            IN const char* pPayloadKind_,
            IN iCAX::Render::RenderGeometryID nGeometryID_,
            IN iCAX::Render::RenderObjectID nObjectID_,
            IN OUT SSlotAssignment& Assignment_)
        {
            if (Assignment_.nPDOID == 0)
            {
                return;
            }
            if (_PDOHub.FreeSlot(Assignment_.nPDOID))
            {
                SendSlotEvent(
                    ProjectID_,
                    SceneContext_,
                    kPDORenderSlotFreedEvent,
                    "SlotFreed",
                    pSlotRole_,
                    nSceneID_,
                    Assignment_.nPDOID,
                    nGeometryID_,
                    nObjectID_,
                    pPayloadKind_,
                    Assignment_.nPayloadCapacity);
            }
            Assignment_ = {};
        };

    for (auto& [_GeometryID, _Assignment] : State_.MeshSlots)
    {
        _FreeAssignment("Geometry", "render.mesh", _GeometryID, 0, _Assignment);
    }
    for (auto& [_GeometryID, _Assignment] : State_.PolylineSlots)
    {
        _FreeAssignment("Geometry", "render.polyline", _GeometryID, 0, _Assignment);
    }
    for (auto& [_GeometryID, _Assignment] : State_.ToolpathSlots)
    {
        _FreeAssignment("Geometry", "render.toolpath", _GeometryID, 0, _Assignment);
    }
    for (auto& [_ObjectID, _Assignment] : State_.ObjectSlots)
    {
        _FreeAssignment("Object", "render.instance_list", 0, _ObjectID, _Assignment);
    }
    _FreeAssignment("ViewState", "render.view_state", 0, 0, State_.ViewSlot);

    State_.MeshSlots.clear();
    State_.PolylineSlots.clear();
    State_.ToolpathSlots.clear();
    State_.ObjectSlots.clear();
}

void iCAX::PDORenderService::CPDORenderService::SendSlotEvent(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Project::ISceneContext& SceneContext_,
    IN uint64_t nEventTypeCode_,
    IN const char* pEventName_,
    IN const char* pSlotRole_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::PDO::PDOID nPDOID_,
    IN iCAX::Render::RenderGeometryID nGeometryID_,
    IN iCAX::Render::RenderObjectID nObjectID_,
    IN const char* pPayloadKind_,
    IN uint64_t nPayloadCapacity_) const
{
    std::ostringstream _Payload;
    _Payload
        << "{\"event\":\"" << pEventName_
        << "\",\"projectId\":\"" << iCAX::Data::to_string(ProjectID_)
        << "\",\"channelId\":\"" << iCAX::Data::to_string(SceneContext_.GetSceneChannelID())
        << "\",\"sceneId\":\"" << nSceneID_
        << "\",\"slotRole\":\"" << pSlotRole_
        << "\",\"payloadKind\":\"" << pPayloadKind_
        << "\",\"pdoId\":\"" << nPDOID_
        << "\",\"geometryId\":\"" << nGeometryID_
        << "\",\"objectId\":\"" << nObjectID_
        << "\",\"payloadCapacity\":\"" << nPayloadCapacity_
        << "\"}";

    iCAX::Mail::MailHeader _Header;
    _Header.nMailId = m_nNextEventMailID.fetch_add(1);
    _Header.nTypeCode = nEventTypeCode_;
    _Header.nStamp = iCAX::Mail::kMailOk;
    SceneContext_.GetBackendPostOffice().SendText(_Header, _Payload.str());
}

void iCAX::PDORenderService::CPDORenderService::SendDefragEvent(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Project::ISceneContext& SceneContext_,
    IN uint64_t nEventTypeCode_,
    IN const char* pEventName_,
    IN iCAX::PDO::PDOID nPDOID_) const
{
    std::ostringstream _Payload;
    _Payload
        << "{\"event\":\"" << pEventName_
        << "\",\"projectId\":\"" << iCAX::Data::to_string(ProjectID_)
        << "\",\"channelId\":\"" << iCAX::Data::to_string(SceneContext_.GetSceneChannelID())
        << "\",\"pdoId\":\"" << nPDOID_
        << "\"}";

    iCAX::Mail::MailHeader _Header;
    _Header.nMailId = m_nNextEventMailID.fetch_add(1);
    _Header.nTypeCode = nEventTypeCode_;
    _Header.nStamp = iCAX::Mail::kMailOk;
    SceneContext_.GetBackendPostOffice().SendText(_Header, _Payload.str());
}
