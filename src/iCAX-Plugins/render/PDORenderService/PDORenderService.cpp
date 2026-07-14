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
#include <cstddef>
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

    iCAX::RenderPDO::SFloat2 ToRenderVec2(IN const SFloat2& Value_)
    {
        return { Value_.x, Value_.y };
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

    iCAX::RenderPDO::SRenderID ToPDOID(IN const iCAX::Data::uuid& ID_)
    {
        iCAX::RenderPDO::SRenderID _ID;
        const auto _Bytes = ID_.as_bytes();
        for (size_t _Index = 0; _Index < _ID.Bytes.size(); ++_Index)
        {
            _ID.Bytes[_Index] = std::to_integer<uint8_t>(_Bytes[_Index]);
        }
        return _ID;
    }

    std::string RenderIDToText(IN const iCAX::Data::uuid& ID_)
    {
        return ID_.is_nil() ? std::string() : iCAX::Data::to_string(ID_);
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
        if (IsInvalidRenderID(Mesh_.nGeometryID))
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
        if (!Mesh_.TextureCoordinates.empty() && Mesh_.TextureCoordinates.size() != Mesh_.Positions.size())
        {
            throw std::invalid_argument("Render mesh texture coordinate count must match position count");
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
        if (IsInvalidRenderID(Polyline_.nGeometryID))
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
        if (IsInvalidRenderID(Toolpath_.nGeometryID))
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

    void ValidateTransform(IN const STransformData& Transform_)
    {
        if (IsInvalidRenderID(Transform_.nTransformID))
        {
            throw std::invalid_argument("Render transform id cannot be zero");
        }
        ValidateVersion(Transform_.nDataVersion);
    }

    bool SameTransformPayload(
        IN const STransformData& Left_,
        IN const STransformData& Right_) noexcept
    {
        return Left_.nTransformID == Right_.nTransformID
            && Left_.nFlags == Right_.nFlags
            && Left_.LocalToWorld.Values == Right_.LocalToWorld.Values;
    }

    RenderDataVersion MakeVersionNewerThan(IN RenderDataVersion nPreviousVersion_)
    {
        if (nPreviousVersion_ == (std::numeric_limits<RenderDataVersion>::max)())
        {
            throw std::overflow_error("Render transform data version overflows");
        }
        return nPreviousVersion_ + 1;
    }

    void ValidateObject(IN const SRenderInstanceData& Object_)
    {
        if (IsInvalidRenderID(Object_.nObjectID))
        {
            throw std::invalid_argument("Render object entity id cannot be zero");
        }
        if (IsInvalidRenderID(Object_.nGeometryID))
        {
            throw std::invalid_argument("Render object geometry id cannot be zero");
        }
        ValidateVersion(Object_.nDataVersion);
    }

    void ValidateCamera(IN const SRenderCameraData& Camera_)
    {
        if (IsInvalidRenderID(Camera_.nCameraID))
        {
            throw std::invalid_argument("Render camera entity id cannot be zero");
        }
        ValidateVersion(Camera_.nDataVersion);
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
        IN SceneObjectID nObjectID_)
    {
        std::ostringstream _Stream;
        _Stream << MakeScenePrefix(ProjectID_, nSceneID_) << ".object." << nObjectID_;
        return _Stream.str();
    }

    std::string MakeTransformInstanceName(
        IN const iCAX::Data::uuid& ProjectID_,
        IN RenderSceneID nSceneID_,
        IN TransformID nTransformID_)
    {
        std::ostringstream _Stream;
        _Stream << MakeScenePrefix(ProjectID_, nSceneID_) << ".transform." << nTransformID_;
        return _Stream.str();
    }

    std::string MakeCameraInstanceName(
        IN const iCAX::Data::uuid& ProjectID_,
        IN RenderSceneID nSceneID_,
        IN RenderCameraID nCameraID_)
    {
        std::ostringstream _Stream;
        _Stream << MakeScenePrefix(ProjectID_, nSceneID_) << ".camera." << nCameraID_;
        return _Stream.str();
    }

    uint64_t CalculateMeshPayloadCapacity(IN const SRenderMeshData& Mesh_)
    {
        uint64_t _Size = sizeof(iCAX::RenderPDO::SRenderMeshPDOHeader);
        _Size = CheckedAdd(_Size, CheckedBytes<iCAX::RenderPDO::SFloat3>(Mesh_.Positions.size()));
        _Size = CheckedAdd(_Size, CheckedBytes<iCAX::RenderPDO::SFloat3>(Mesh_.Normals.size()));
        _Size = CheckedAdd(_Size, CheckedBytes<uint32_t>(Mesh_.VertexColorsRGBA.size()));
        _Size = CheckedAdd(_Size, CheckedBytes<iCAX::RenderPDO::SFloat2>(Mesh_.TextureCoordinates.size()));
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
        return sizeof(iCAX::RenderPDO::SRenderObjectPDOHeader);
    }

    uint64_t CalculateTransformPayloadCapacity()
    {
        return sizeof(iCAX::RenderPDO::STransformPDOHeader);
    }

    uint64_t CalculateCameraPayloadCapacity()
    {
        return sizeof(iCAX::RenderPDO::SRenderCameraPDOHeader);
    }

    iCAX::PDO::PDODecl MakeRenderDeclWithID(
        IN iCAX::RenderPDO::ERenderPDOPayloadKind ePayloadKind_,
        IN iCAX::PDO::PDOID nPDOID_,
        IN iCAX::PDO::PDODirection eDirection_,
        IN uint64_t nPayloadCapacity_)
    {
        auto _Decl = iCAX::RenderPDO::MakeRenderPDODecl(
            ePayloadKind_,
            "temporary.instance.name.for.dynamic.render.pdo",
            eDirection_,
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
        Slot_.SwapBuffersIfReady();
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

    const auto _RenderSceneID = iCAX::Render::MakeRenderSceneID(SceneContext_.GetSceneID());
    iCAX::Render::SRenderSceneSnapshot _Scene;
    SScenePDOOutputState _State;
    bool _bHasScene = false;
    bool _bHasState = false;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        auto _ProjectIter = m_Projects.find(_ProjectID);
        if (_ProjectIter != m_Projects.end())
        {
            auto _SceneIter = _ProjectIter->second.find(_RenderSceneID);
            if (_SceneIter != _ProjectIter->second.end())
            {
                _Scene = _SceneIter->second;
                _bHasScene = true;
            }
        }
        auto _StateProjectIter = m_PDOOutputs.find(_ProjectID);
        if (_StateProjectIter != m_PDOOutputs.end())
        {
            auto _StateIter = _StateProjectIter->second.find(_RenderSceneID);
            if (_StateIter != _StateProjectIter->second.end())
            {
                _State = _StateIter->second;
                _bHasState = true;
            }
        }
    }

    if (!_bHasScene && !_bHasState)
    {
        return;
    }
    if (!SceneContext_.HasPDOHub())
    {
        throw std::logic_error("PDORenderService requires project PDO hub");
    }

    if (!_bHasScene)
    {
        FreeScenePDOOutput(_ProjectID, _RenderSceneID, SceneContext_, _State);
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        auto _StateProjectIter = m_PDOOutputs.find(_ProjectID);
        if (_StateProjectIter != m_PDOOutputs.end())
        {
            _StateProjectIter->second.erase(_RenderSceneID);
            if (_StateProjectIter->second.empty())
            {
                m_PDOOutputs.erase(_StateProjectIter);
            }
        }
        return;
    }

    SynchronizeScenePDOOutput(_ProjectID, &_Scene, SceneContext_, _State);

    const auto _IsStateEmpty = [](IN const SScenePDOOutputState& State_) noexcept
    {
        return State_.MeshSlots.empty()
            && State_.PolylineSlots.empty()
            && State_.ToolpathSlots.empty()
            && State_.TransformSlots.empty()
            && State_.ObjectSlots.empty()
            && State_.CameraSlots.empty();
    };

    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        if (_IsStateEmpty(_State))
        {
            auto _StateProjectIter = m_PDOOutputs.find(_ProjectID);
            if (_StateProjectIter != m_PDOOutputs.end())
            {
                _StateProjectIter->second.erase(_RenderSceneID);
                if (_StateProjectIter->second.empty())
                {
                    m_PDOOutputs.erase(_StateProjectIter);
                }
            }
        }
        else
        {
            m_PDOOutputs[_ProjectID][_RenderSceneID] = std::move(_State);
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

bool iCAX::PDORenderService::CPDORenderService::SetObjects(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN const std::vector<iCAX::Render::SRenderInstanceData>& Objects_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    for (const auto& _Object : Objects_)
    {
        ValidateObject(_Object);
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        return false;
    }
    _pScene->Objects = Objects_;
    return true;
}

bool iCAX::PDORenderService::CPDORenderService::SetTransforms(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN const std::vector<iCAX::Render::STransformData>& Transforms_,
    IN iCAX::Render::RenderDataVersion nDataVersion_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    ValidateVersion(nDataVersion_);
    for (const auto& _Transform : Transforms_)
    {
        ValidateTransform(_Transform);
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        return false;
    }

    auto _NextTransforms = Transforms_;
    auto _NextSceneVersion = nDataVersion_;
    for (auto& _Transform : _NextTransforms)
    {
        const auto _PreviousIter = std::find_if(
            _pScene->Transforms.begin(),
            _pScene->Transforms.end(),
            [&_Transform](IN const iCAX::Render::STransformData& Item_)
            {
                return Item_.nTransformID == _Transform.nTransformID;
            });
        if (_PreviousIter != _pScene->Transforms.end()
            && !SameTransformPayload(*_PreviousIter, _Transform)
            && _Transform.nDataVersion <= _PreviousIter->nDataVersion)
        {
            _Transform.nDataVersion = MakeVersionNewerThan(_PreviousIter->nDataVersion);
        }
        _NextSceneVersion = (std::max)(_NextSceneVersion, _Transform.nDataVersion);
    }

    _pScene->Transforms = std::move(_NextTransforms);
    _pScene->nTransformDataVersion = _NextSceneVersion;
    return true;
}

bool iCAX::PDORenderService::CPDORenderService::SetCameras(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN const std::vector<iCAX::Render::SRenderCameraData>& Cameras_,
    IN iCAX::Render::RenderCameraID nActiveCameraID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    if (Cameras_.empty())
    {
        if (nActiveCameraID_ != iCAX::Render::kInvalidRenderCameraID)
        {
            throw std::invalid_argument("Render active camera id must be zero when camera list is empty");
        }

        std::lock_guard<std::mutex> _Lock(m_Mutex);
        auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
        if (!_pScene)
        {
            return false;
        }
        _pScene->Cameras.clear();
        _pScene->nActiveCameraID = iCAX::Render::kInvalidRenderCameraID;
        return true;
    }
    for (const auto& _Camera : Cameras_)
    {
        ValidateCamera(_Camera);
    }
    if (nActiveCameraID_ == iCAX::Render::kInvalidRenderCameraID)
    {
        throw std::invalid_argument("Render active camera id cannot be zero");
    }
    const auto _ActiveCameraIter = std::find_if(
        Cameras_.begin(),
        Cameras_.end(),
        [nActiveCameraID_](IN const iCAX::Render::SRenderCameraData& Camera_)
        {
            return Camera_.nCameraID == nActiveCameraID_;
        });
    if (_ActiveCameraIter == Cameras_.end())
    {
        throw std::invalid_argument("Render active camera id is not in camera list");
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto* _pScene = FindSceneNoLock(ProjectID_, nSceneID_);
    if (!_pScene)
    {
        return false;
    }
    _pScene->Cameras = Cameras_;
    _pScene->nActiveCameraID = nActiveCameraID_;
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
    IN iCAX::Render::SceneObjectID nObjectID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    if (nObjectID_ == iCAX::Render::kInvalidSceneObjectID)
    {
        throw std::invalid_argument("Render object id cannot be zero");
    }
    return iCAX::PDO::MakePDOID(
        iCAX::RenderPDO::GetRenderPDOPayloadTypeName(iCAX::RenderPDO::ERenderPDOPayloadKind::Object),
        MakeObjectInstanceName(ProjectID_, nSceneID_, nObjectID_));
}

iCAX::PDO::PDOID iCAX::PDORenderService::CPDORenderService::MakeTransformPDOID(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Render::TransformID nTransformID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    if (nTransformID_ == iCAX::Render::kInvalidTransformID)
    {
        throw std::invalid_argument("Render transform id cannot be zero");
    }
    return iCAX::PDO::MakePDOID(
        iCAX::RenderPDO::GetRenderPDOPayloadTypeName(iCAX::RenderPDO::ERenderPDOPayloadKind::Transform),
        MakeTransformInstanceName(ProjectID_, nSceneID_, nTransformID_));
}

iCAX::PDO::PDOID iCAX::PDORenderService::CPDORenderService::MakeCameraPDOID(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Render::RenderCameraID nCameraID_)
{
    ValidateProjectAndScene(ProjectID_, nSceneID_);
    if (nCameraID_ == iCAX::Render::kInvalidRenderCameraID)
    {
        throw std::invalid_argument("Render camera id cannot be zero");
    }
    return iCAX::PDO::MakePDOID(
        iCAX::RenderPDO::GetRenderPDOPayloadTypeName(iCAX::RenderPDO::ERenderPDOPayloadKind::Camera),
        MakeCameraInstanceName(ProjectID_, nSceneID_, nCameraID_));
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

    std::vector<iCAX::RenderPDO::SFloat2> _TextureCoordinates;
    _TextureCoordinates.reserve(_Mesh.TextureCoordinates.size());
    for (const auto& _Item : _Mesh.TextureCoordinates)
    {
        _TextureCoordinates.push_back(ToRenderVec2(_Item));
    }

    iCAX::RenderPDO::SRenderMeshPDOHeader _Header;
    _Header.Header.nPayloadKind = static_cast<uint32_t>(iCAX::RenderPDO::ERenderPDOPayloadKind::Mesh);
    _Header.Header.nHeaderSize = sizeof(iCAX::RenderPDO::SRenderMeshPDOHeader);
    _Header.Header.nDataVersion = _Mesh.nDataVersion;
    _Header.nGeometryID = ToPDOID(_Mesh.nGeometryID);
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
    if (!_TextureCoordinates.empty())
    {
        _Header.nFlags |= iCAX::RenderPDO::kMeshFlagHasTextureCoordinates;
        _Header.nTextureCoordinatesOffset = AppendArray(_Payload, _TextureCoordinates);
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
    _Header.nGeometryID = ToPDOID(_Polyline.nGeometryID);
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
    _Header.nGeometryID = ToPDOID(_Toolpath.nGeometryID);
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

bool iCAX::PDORenderService::CPDORenderService::WriteObjectToPDO(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Render::SceneObjectID nObjectID_,
    IN iCAX::PDO::IPDOSlot& Slot_) const
{
    auto _Snapshot = GetSceneSnapshot(ProjectID_, nSceneID_);

    const auto _InstanceIter = std::find_if(
        _Snapshot.Objects.begin(),
        _Snapshot.Objects.end(),
        [nObjectID_](IN const iCAX::Render::SRenderInstanceData& Item_)
        {
            return Item_.nObjectID == nObjectID_;
        });
    if (_InstanceIter == _Snapshot.Objects.end())
    {
        return false;
    }

    iCAX::RenderPDO::SRenderObjectPDOHeader _Header;
    _Header.Header.nPayloadKind = static_cast<uint32_t>(iCAX::RenderPDO::ERenderPDOPayloadKind::Object);
    _Header.Header.nHeaderSize = sizeof(iCAX::RenderPDO::SRenderObjectPDOHeader);
    _Header.Header.nDataVersion = _InstanceIter->nDataVersion;
    _Header.Header.nPayloadSize = sizeof(iCAX::RenderPDO::SRenderObjectPDOHeader);
    _Header.nObjectID = ToPDOID(_InstanceIter->nObjectID);
    _Header.nGeometryID = ToPDOID(_InstanceIter->nGeometryID);
    _Header.nMaterialID = ToPDOID(_InstanceIter->nMaterialID);
    _Header.nGeometryKind = ToRenderGeometryKind(_InstanceIter->eGeometryKind);
    _Header.nRenderClass = ToRenderClass(_InstanceIter->eRenderClass);
    _Header.nFlags = _InstanceIter->nFlags;

    std::vector<std::byte> _Payload(sizeof(iCAX::RenderPDO::SRenderObjectPDOHeader));
    std::memcpy(_Payload.data(), &_Header, sizeof(_Header));

    std::string _Error;
    if (!iCAX::RenderPDO::ValidateObjectPDOHeader(_Header, Slot_.GetHeader().nPayloadSize, &_Error))
    {
        throw std::runtime_error("Serialized object PDO is invalid: " + _Error);
    }
    return CommitPayloadToSlot(_Payload, Slot_, _InstanceIter->nDataVersion);
}

bool iCAX::PDORenderService::CPDORenderService::WriteTransformToPDO(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Render::TransformID nTransformID_,
    IN iCAX::PDO::IPDOSlot& Slot_) const
{
    auto _Snapshot = GetSceneSnapshot(ProjectID_, nSceneID_);
    if (_Snapshot.nTransformDataVersion == 0)
    {
        return false;
    }

    const auto _TransformIter = std::find_if(
        _Snapshot.Transforms.begin(),
        _Snapshot.Transforms.end(),
        [nTransformID_](IN const iCAX::Render::STransformData& Item_)
        {
            return Item_.nTransformID == nTransformID_;
        });
    if (_TransformIter == _Snapshot.Transforms.end())
    {
        return false;
    }

    iCAX::RenderPDO::STransformPDOHeader _Header;
    _Header.Header.nPayloadKind = static_cast<uint32_t>(iCAX::RenderPDO::ERenderPDOPayloadKind::Transform);
    _Header.Header.nHeaderSize = sizeof(iCAX::RenderPDO::STransformPDOHeader);
    _Header.Header.nDataVersion = _TransformIter->nDataVersion;
    _Header.Header.nPayloadSize = sizeof(iCAX::RenderPDO::STransformPDOHeader);
    _Header.nTransformID = ToPDOID(_TransformIter->nTransformID);
    _Header.nFlags = _TransformIter->nFlags;
    _Header.LocalToWorld = ToRenderMatrix(_TransformIter->LocalToWorld);

    std::vector<std::byte> _Payload(sizeof(iCAX::RenderPDO::STransformPDOHeader));
    std::memcpy(_Payload.data(), &_Header, sizeof(_Header));

    std::string _Error;
    if (!iCAX::RenderPDO::ValidateTransformPDOHeader(_Header, Slot_.GetHeader().nPayloadSize, &_Error))
    {
        throw std::runtime_error("Serialized transform PDO is invalid: " + _Error);
    }
    return CommitPayloadToSlot(_Payload, Slot_, _TransformIter->nDataVersion);
}

bool iCAX::PDORenderService::CPDORenderService::WriteCameraToPDO(
    IN const iCAX::Data::uuid& ProjectID_,
    IN iCAX::Render::RenderSceneID nSceneID_,
    IN iCAX::Render::RenderCameraID nCameraID_,
    IN iCAX::PDO::IPDOSlot& Slot_) const
{
    auto _Snapshot = GetSceneSnapshot(ProjectID_, nSceneID_);

    const auto _CameraIter = std::find_if(
        _Snapshot.Cameras.begin(),
        _Snapshot.Cameras.end(),
        [nCameraID_](IN const iCAX::Render::SRenderCameraData& Item_)
        {
            return Item_.nCameraID == nCameraID_;
        });
    if (_CameraIter == _Snapshot.Cameras.end())
    {
        return false;
    }

    iCAX::RenderPDO::SRenderCameraPDOHeader _Header;
    _Header.Header.nPayloadKind = static_cast<uint32_t>(iCAX::RenderPDO::ERenderPDOPayloadKind::Camera);
    _Header.Header.nHeaderSize = sizeof(iCAX::RenderPDO::SRenderCameraPDOHeader);
    _Header.Header.nDataVersion = _CameraIter->nDataVersion;
    _Header.Header.nPayloadSize = sizeof(iCAX::RenderPDO::SRenderCameraPDOHeader);
    _Header.nCameraID = ToPDOID(_CameraIter->nCameraID);
    _Header.nFlags = _CameraIter->nFlags;
    if (_CameraIter->nCameraID == _Snapshot.nActiveCameraID)
    {
        _Header.nFlags |= iCAX::RenderPDO::kCameraFlagActive;
    }
    _Header.nNearPlane = _CameraIter->nNearPlane;
    _Header.nFarPlane = _CameraIter->nFarPlane;
    _Header.nVerticalFovRadians = _CameraIter->nVerticalFovRadians;
    _Header.nOrthographicHeight = _CameraIter->nOrthographicHeight;
    std::vector<std::byte> _Payload(sizeof(iCAX::RenderPDO::SRenderCameraPDOHeader));
    std::memcpy(_Payload.data(), &_Header, sizeof(_Header));

    std::string _Error;
    if (!iCAX::RenderPDO::ValidateCameraPDOHeader(_Header, Slot_.GetHeader().nPayloadSize, &_Error))
    {
        throw std::runtime_error("Serialized camera PDO is invalid: " + _Error);
    }
    return CommitPayloadToSlot(_Payload, Slot_, _CameraIter->nDataVersion);
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
            IN iCAX::Render::SceneObjectID nObjectID_,
            IN iCAX::Render::TransformID nTransformID_,
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
                    nTransformID_,
                    pPayloadKind_,
                    Assignment_.nSlotVersion,
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
            IN iCAX::Render::SceneObjectID nObjectID_,
            IN iCAX::Render::TransformID nTransformID_,
            IN iCAX::PDO::PDODirection eDirection_,
            IN uint64_t nPayloadCapacity_,
            IN OUT SSlotAssignment& Assignment_)
        {
            if (Assignment_.nPDOID == 0)
            {
                const auto _Decl = MakeRenderDeclWithID(ePayloadKind_, nPDOID_, eDirection_, nPayloadCapacity_);
                auto& _Slot = _PDOHub.AllocateSlot(_Decl, _AllocationCallbacks);
                Assignment_.nPDOID = nPDOID_;
                Assignment_.nSlotVersion = _Slot.GetHeader().nVersion;
                Assignment_.nPayloadCapacity = nPayloadCapacity_;
                Assignment_.bNeedPublishAllocatedEvent = true;
                return;
            }

            if (Assignment_.nPDOID != nPDOID_)
            {
                throw std::logic_error("Render PDO slot assignment id is inconsistent");
            }

            if (Assignment_.nPayloadCapacity < nPayloadCapacity_)
            {
                _PDOHub.FreeSlot(Assignment_.nPDOID);
                const auto _Decl = MakeRenderDeclWithID(ePayloadKind_, nPDOID_, eDirection_, nPayloadCapacity_);
                auto& _Slot = _PDOHub.AllocateSlot(_Decl, _AllocationCallbacks);
                Assignment_.nSlotVersion = _Slot.GetHeader().nVersion;
                Assignment_.nPayloadCapacity = nPayloadCapacity_;
                Assignment_.bNeedPublishAllocatedEvent = true;
            }
        };

    auto _PublishAssignmentIfReady =
        [this, &ProjectID_, &SceneContext_, _SceneID](
            IN const char* pSlotRole_,
            IN const char* pPayloadKind_,
            IN iCAX::Render::RenderGeometryID nGeometryID_,
            IN iCAX::Render::SceneObjectID nObjectID_,
            IN iCAX::Render::TransformID nTransformID_,
            IN OUT SSlotAssignment& Assignment_)
        {
            if (!Assignment_.bNeedPublishAllocatedEvent)
            {
                return;
            }

            SendSlotEvent(
                ProjectID_,
                SceneContext_,
                kPDORenderSlotAllocatedEvent,
                "SlotAllocated",
                pSlotRole_,
                _SceneID,
                Assignment_.nPDOID,
                nGeometryID_,
                nObjectID_,
                nTransformID_,
                pPayloadKind_,
                Assignment_.nSlotVersion,
                Assignment_.nPayloadCapacity);
            Assignment_.bNeedPublishAllocatedEvent = false;
        };

    for (auto _Iter = State_.MeshSlots.begin(); _Iter != State_.MeshSlots.end();)
    {
        if (pScene_->Meshes.find(_Iter->first) == pScene_->Meshes.end())
        {
            _FreeAssignment("Geometry", "render.mesh", _Iter->first, iCAX::Render::kInvalidSceneObjectID, iCAX::Render::kInvalidTransformID, _Iter->second);
            _Iter = State_.MeshSlots.erase(_Iter);
            continue;
        }
        ++_Iter;
    }

    for (auto _Iter = State_.PolylineSlots.begin(); _Iter != State_.PolylineSlots.end();)
    {
        if (pScene_->Polylines.find(_Iter->first) == pScene_->Polylines.end())
        {
            _FreeAssignment("Geometry", "render.polyline", _Iter->first, iCAX::Render::kInvalidSceneObjectID, iCAX::Render::kInvalidTransformID, _Iter->second);
            _Iter = State_.PolylineSlots.erase(_Iter);
            continue;
        }
        ++_Iter;
    }

    for (auto _Iter = State_.ToolpathSlots.begin(); _Iter != State_.ToolpathSlots.end();)
    {
        if (pScene_->Toolpaths.find(_Iter->first) == pScene_->Toolpaths.end())
        {
            _FreeAssignment("Geometry", "render.toolpath", _Iter->first, iCAX::Render::kInvalidSceneObjectID, iCAX::Render::kInvalidTransformID, _Iter->second);
            _Iter = State_.ToolpathSlots.erase(_Iter);
            continue;
        }
        ++_Iter;
    }

    std::unordered_set<iCAX::Render::TransformID> _LiveTransforms;
    _LiveTransforms.reserve(pScene_->Transforms.size());
    for (const auto& _Transform : pScene_->Transforms)
    {
        if (_Transform.nTransformID == iCAX::Render::kInvalidTransformID)
        {
            throw std::invalid_argument("Render transform id cannot be zero");
        }
        _LiveTransforms.insert(_Transform.nTransformID);
    }

    for (auto _Iter = State_.TransformSlots.begin(); _Iter != State_.TransformSlots.end();)
    {
        if (_LiveTransforms.find(_Iter->first) == _LiveTransforms.end())
        {
            _FreeAssignment("Transform", "render.transform", iCAX::Render::kInvalidRenderGeometryID, iCAX::Render::kInvalidSceneObjectID, _Iter->first, _Iter->second);
            _Iter = State_.TransformSlots.erase(_Iter);
            continue;
        }
        ++_Iter;
    }

    std::unordered_set<iCAX::Render::SceneObjectID> _LiveObjects;
    _LiveObjects.reserve(pScene_->Objects.size());
    for (const auto& _Instance : pScene_->Objects)
    {
        if (_Instance.nObjectID == iCAX::Render::kInvalidSceneObjectID)
        {
            throw std::invalid_argument("Render object id cannot be zero");
        }
        _LiveObjects.insert(_Instance.nObjectID);
    }

    for (auto _Iter = State_.ObjectSlots.begin(); _Iter != State_.ObjectSlots.end();)
    {
        if (_LiveObjects.find(_Iter->first) == _LiveObjects.end())
        {
            _FreeAssignment("Object", "render.object", iCAX::Render::kInvalidRenderGeometryID, _Iter->first, iCAX::Render::kInvalidTransformID, _Iter->second);
            _Iter = State_.ObjectSlots.erase(_Iter);
            continue;
        }
        ++_Iter;
    }

    std::unordered_set<iCAX::Render::RenderCameraID> _LiveCameras;
    _LiveCameras.reserve(pScene_->Cameras.size());
    for (const auto& _Camera : pScene_->Cameras)
    {
        if (_Camera.nCameraID == iCAX::Render::kInvalidRenderCameraID)
        {
            throw std::invalid_argument("Render camera id cannot be zero");
        }
        _LiveCameras.insert(_Camera.nCameraID);
    }

    for (auto _Iter = State_.CameraSlots.begin(); _Iter != State_.CameraSlots.end();)
    {
        if (_LiveCameras.find(_Iter->first) == _LiveCameras.end())
        {
            _FreeAssignment("Camera", "render.camera", iCAX::Render::kInvalidRenderGeometryID, iCAX::Render::kInvalidSceneObjectID, _Iter->first, _Iter->second);
            _Iter = State_.CameraSlots.erase(_Iter);
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
            iCAX::Render::kInvalidSceneObjectID,
            iCAX::Render::kInvalidTransformID,
            iCAX::PDO::kDirection2External,
            CalculateMeshPayloadCapacity(_Mesh),
            _Assignment);
        if (WriteMeshToPDO(ProjectID_, _SceneID, _GeometryID, _PDOHub.GetSlot(_Assignment.nPDOID)))
        {
            _PublishAssignmentIfReady("Geometry", "render.mesh", _GeometryID, iCAX::Render::kInvalidSceneObjectID, iCAX::Render::kInvalidTransformID, _Assignment);
        }
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
            iCAX::Render::kInvalidSceneObjectID,
            iCAX::Render::kInvalidTransformID,
            iCAX::PDO::kDirection2External,
            CalculatePolylinePayloadCapacity(_Polyline),
            _Assignment);
        if (WritePolylineToPDO(ProjectID_, _SceneID, _GeometryID, _PDOHub.GetSlot(_Assignment.nPDOID)))
        {
            _PublishAssignmentIfReady("Geometry", "render.polyline", _GeometryID, iCAX::Render::kInvalidSceneObjectID, iCAX::Render::kInvalidTransformID, _Assignment);
        }
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
            iCAX::Render::kInvalidSceneObjectID,
            iCAX::Render::kInvalidTransformID,
            iCAX::PDO::kDirection2External,
            CalculateToolpathPayloadCapacity(_Toolpath),
            _Assignment);
        if (WriteToolpathToPDO(ProjectID_, _SceneID, _GeometryID, _PDOHub.GetSlot(_Assignment.nPDOID)))
        {
            _PublishAssignmentIfReady("Geometry", "render.toolpath", _GeometryID, iCAX::Render::kInvalidSceneObjectID, iCAX::Render::kInvalidTransformID, _Assignment);
        }
    }

    for (const auto& _Transform : pScene_->Transforms)
    {
        auto& _Assignment = State_.TransformSlots[_Transform.nTransformID];
        const auto _PDOID = MakeTransformPDOID(ProjectID_, _SceneID, _Transform.nTransformID);
        _EnsureAssignment(
            iCAX::RenderPDO::ERenderPDOPayloadKind::Transform,
            "Transform",
            _PDOID,
            iCAX::Render::kInvalidRenderGeometryID,
            iCAX::Render::kInvalidSceneObjectID,
            _Transform.nTransformID,
            iCAX::PDO::kDirection2External,
            CalculateTransformPayloadCapacity(),
            _Assignment);
        if (WriteTransformToPDO(ProjectID_, _SceneID, _Transform.nTransformID, _PDOHub.GetSlot(_Assignment.nPDOID)))
        {
            _PublishAssignmentIfReady("Transform", "render.transform", iCAX::Render::kInvalidRenderGeometryID, iCAX::Render::kInvalidSceneObjectID, _Transform.nTransformID, _Assignment);
        }
    }

    for (const auto& _Instance : pScene_->Objects)
    {
        auto& _Assignment = State_.ObjectSlots[_Instance.nObjectID];
        const auto _PDOID = MakeObjectPDOID(ProjectID_, _SceneID, _Instance.nObjectID);
        _EnsureAssignment(
            iCAX::RenderPDO::ERenderPDOPayloadKind::Object,
            "Object",
            _PDOID,
            _Instance.nGeometryID,
            _Instance.nObjectID,
            _Instance.nObjectID,
            iCAX::PDO::kDirection2External,
            CalculateObjectPayloadCapacity(),
            _Assignment);
        if (WriteObjectToPDO(ProjectID_, _SceneID, _Instance.nObjectID, _PDOHub.GetSlot(_Assignment.nPDOID)))
        {
            _PublishAssignmentIfReady("Object", "render.object", _Instance.nGeometryID, _Instance.nObjectID, _Instance.nObjectID, _Assignment);
        }
    }

    for (const auto& _Camera : pScene_->Cameras)
    {
        auto& _Assignment = State_.CameraSlots[_Camera.nCameraID];
        const auto _PDOID = MakeCameraPDOID(ProjectID_, _SceneID, _Camera.nCameraID);
        _EnsureAssignment(
            iCAX::RenderPDO::ERenderPDOPayloadKind::Camera,
            "Camera",
            _PDOID,
            iCAX::Render::kInvalidRenderGeometryID,
            iCAX::Render::kInvalidSceneObjectID,
            _Camera.nCameraID,
            iCAX::PDO::kDirection2External,
            CalculateCameraPayloadCapacity(),
            _Assignment);
        if (WriteCameraToPDO(ProjectID_, _SceneID, _Camera.nCameraID, _PDOHub.GetSlot(_Assignment.nPDOID)))
        {
            _PublishAssignmentIfReady("Camera", "render.camera", iCAX::Render::kInvalidRenderGeometryID, iCAX::Render::kInvalidSceneObjectID, _Camera.nCameraID, _Assignment);
        }
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
            IN iCAX::Render::SceneObjectID nObjectID_,
            IN iCAX::Render::TransformID nTransformID_,
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
                    nTransformID_,
                    pPayloadKind_,
                    Assignment_.nSlotVersion,
                    Assignment_.nPayloadCapacity);
            }
            Assignment_ = {};
        };

    for (auto& [_GeometryID, _Assignment] : State_.MeshSlots)
    {
        _FreeAssignment("Geometry", "render.mesh", _GeometryID, iCAX::Render::kInvalidSceneObjectID, iCAX::Render::kInvalidTransformID, _Assignment);
    }
    for (auto& [_GeometryID, _Assignment] : State_.PolylineSlots)
    {
        _FreeAssignment("Geometry", "render.polyline", _GeometryID, iCAX::Render::kInvalidSceneObjectID, iCAX::Render::kInvalidTransformID, _Assignment);
    }
    for (auto& [_GeometryID, _Assignment] : State_.ToolpathSlots)
    {
        _FreeAssignment("Geometry", "render.toolpath", _GeometryID, iCAX::Render::kInvalidSceneObjectID, iCAX::Render::kInvalidTransformID, _Assignment);
    }
    for (auto& [_TransformID, _Assignment] : State_.TransformSlots)
    {
        _FreeAssignment("Transform", "render.transform", iCAX::Render::kInvalidRenderGeometryID, iCAX::Render::kInvalidSceneObjectID, _TransformID, _Assignment);
    }
    for (auto& [_ObjectID, _Assignment] : State_.ObjectSlots)
    {
        _FreeAssignment("Object", "render.object", iCAX::Render::kInvalidRenderGeometryID, _ObjectID, _ObjectID, _Assignment);
    }
    for (auto& [_CameraID, _Assignment] : State_.CameraSlots)
    {
        _FreeAssignment("Camera", "render.camera", iCAX::Render::kInvalidRenderGeometryID, iCAX::Render::kInvalidSceneObjectID, _CameraID, _Assignment);
    }

    State_.MeshSlots.clear();
    State_.PolylineSlots.clear();
    State_.ToolpathSlots.clear();
    State_.TransformSlots.clear();
    State_.ObjectSlots.clear();
    State_.CameraSlots.clear();
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
    IN iCAX::Render::SceneObjectID nObjectID_,
    IN iCAX::Render::TransformID nTransformID_,
    IN const char* pPayloadKind_,
    IN uint32_t nSlotVersion_,
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
        << "\",\"geometryId\":\"" << RenderIDToText(nGeometryID_)
        << "\",\"objectId\":\"" << RenderIDToText(nObjectID_)
        << "\",\"transformId\":\"" << RenderIDToText(nTransformID_)
        << "\",\"slotVersion\":\"" << nSlotVersion_
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
