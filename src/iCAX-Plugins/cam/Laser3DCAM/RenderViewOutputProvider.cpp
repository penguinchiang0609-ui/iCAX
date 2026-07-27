#include "pch.h"

#include "RenderViewOutputProvider.h"

#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "RenderService/IRenderService.h"
#include "RenderService/RenderSceneIds.h"
#include "Services/ServiceProvider.h"

#include <limits>
#include <unordered_set>

namespace
{
    using namespace iCAX::Render;

    uint32_t ToUInt32(
        IN const iCAX::Data::Variant& Value_,
        IN const char* pPropertyName_)
    {
        uint64_t _Value = 0;
        if (Value_.Is<unsigned int>())
        {
            _Value = Value_.To<unsigned int>();
        }
        else if (Value_.Is<int>())
        {
            const auto _Signed = Value_.To<int>();
            if (_Signed < 0)
            {
                throw std::invalid_argument(
                    std::string("View presentation property cannot be negative: ")
                    + pPropertyName_);
            }
            _Value = static_cast<uint64_t>(_Signed);
        }
        else if (Value_.Is<unsigned long long>())
        {
            _Value = Value_.To<unsigned long long>();
        }
        else if (Value_.Is<long long>())
        {
            const auto _Signed = Value_.To<long long>();
            if (_Signed < 0)
            {
                throw std::invalid_argument(
                    std::string("View presentation property cannot be negative: ")
                    + pPropertyName_);
            }
            _Value = static_cast<uint64_t>(_Signed);
        }
        else
        {
            throw std::invalid_argument(
                std::string("View presentation property must be an integer: ")
                + pPropertyName_);
        }

        if (_Value > (std::numeric_limits<uint32_t>::max)())
        {
            throw std::out_of_range(
                std::string("View presentation property exceeds uint32: ")
                + pPropertyName_);
        }
        return static_cast<uint32_t>(_Value);
    }

    bool ToBoolean(
        IN const iCAX::Data::Variant& Value_,
        IN const char* pPropertyName_)
    {
        if (!Value_.Is<bool>())
        {
            throw std::invalid_argument(
                std::string("View presentation property must be boolean: ")
                + pPropertyName_);
        }
        return Value_.To<bool>();
    }

    void SetFlag(
        IN OUT uint32_t& nFlags_,
        IN uint32_t nFlag_,
        IN bool bEnabled_)
    {
        if (bEnabled_)
        {
            nFlags_ |= nFlag_;
        }
        else
        {
            nFlags_ &= ~nFlag_;
        }
    }

    bool SameObjectPayload(
        IN const SRenderInstanceData& Left_,
        IN const SRenderInstanceData& Right_) noexcept
    {
        return Left_.nObjectID == Right_.nObjectID
            && Left_.nGeometryID == Right_.nGeometryID
            && Left_.nMaterialID == Right_.nMaterialID
            && Left_.eGeometryKind == Right_.eGeometryKind
            && Left_.eRenderClass == Right_.eRenderClass
            && Left_.nFlags == Right_.nFlags
            && Left_.nLayerMask == Right_.nLayerMask;
    }

    RenderDataVersion MakeNewerVersion(IN RenderDataVersion nVersion_)
    {
        if (nVersion_ == (std::numeric_limits<RenderDataVersion>::max)())
        {
            throw std::overflow_error("View render object data version overflow");
        }
        return nVersion_ + 1;
    }

    void ApplyPresentation(
        IN const iCAX::Data::ObjectMap& Presentation_,
        IN OUT SRenderInstanceData& Object_)
    {
        const auto _ApplyBooleanFlag =
            [&Presentation_, &Object_](
                IN const char* pName_,
                IN uint32_t nFlag_)
        {
            const auto _Iter = Presentation_.find(pName_);
            if (_Iter != Presentation_.end())
            {
                SetFlag(
                    Object_.nFlags,
                    nFlag_,
                    ToBoolean(_Iter->second, pName_));
            }
        };

        if (const auto _Iter = Presentation_.find("flags"); _Iter != Presentation_.end())
        {
            Object_.nFlags = ToUInt32(_Iter->second, "flags");
        }
        _ApplyBooleanFlag("visible", kRenderFlagVisible);
        _ApplyBooleanFlag("selectable", kRenderFlagSelectable);
        _ApplyBooleanFlag("highlighted", kRenderFlagHighlighted);
        _ApplyBooleanFlag("selected", kRenderFlagSelected);
        _ApplyBooleanFlag("disabled", kRenderFlagDisabled);

        if (const auto _Iter = Presentation_.find("layerMask");
            _Iter != Presentation_.end())
        {
            Object_.nLayerMask = ToUInt32(_Iter->second, "layerMask");
        }
        if (const auto _Iter = Presentation_.find("renderClass");
            _Iter != Presentation_.end())
        {
            Object_.eRenderClass =
                static_cast<ERenderClass>(ToUInt32(_Iter->second, "renderClass"));
        }
        if (const auto _Iter = Presentation_.find("materialId");
            _Iter != Presentation_.end())
        {
            if (!_Iter->second.Is<std::string>())
            {
                throw std::invalid_argument(
                    "View presentation property must be a uuid string: materialId");
            }
            const auto _Parsed =
                iCAX::Data::uuid::from_string(_Iter->second.To<std::string>());
            if (!_Parsed.has_value())
            {
                throw std::invalid_argument(
                    "View presentation property must be a uuid string: materialId");
            }
            Object_.nMaterialID = *_Parsed;
        }
    }

    class CRenderViewOutputSession final
        : public iCAX::View::IViewOutputSession
    {
    public:
        CRenderViewOutputSession(
            IN std::shared_ptr<iCAX::Render::IRenderService> pRenderService_,
            IN const iCAX::Data::uuid& ProjectID_,
            IN const iCAX::Data::uuid& SceneID_,
            IN const iCAX::View::ViewInstanceID& ViewInstanceID_)
            : m_pRenderService(std::move(pRenderService_))
            , m_ProjectID(ProjectID_)
            , m_SourceSceneID(iCAX::Render::MakeRenderSceneID(SceneID_))
            , m_TargetSceneID(iCAX::Render::MakeViewRenderSceneID(
                SceneID_,
                ViewInstanceID_))
        {
            if (m_TargetSceneID == m_SourceSceneID)
            {
                throw std::logic_error("View RenderSceneID collides with source RenderSceneID");
            }
            if (!m_pRenderService->CreateScene(
                m_ProjectID,
                m_TargetSceneID,
                SceneID_))
            {
                throw std::logic_error("View RenderSceneID already exists");
            }
        }

        ~CRenderViewOutputSession() override
        {
            Close();
        }

        iCAX::View::SViewOutputDescriptor GetDescriptor() const override
        {
            iCAX::View::SViewOutputDescriptor _Descriptor;
            _Descriptor.Type = "render";
            _Descriptor.Properties["renderSceneId"] =
                std::to_string(m_TargetSceneID);
            return _Descriptor;
        }

        void Synchronize(
            IN iCAX::Project::IProjectContext& Project_,
            IN iCAX::Project::ISceneContext&,
            IN const iCAX::View::SViewContent& Content_) override
        {
            if (m_bClosed)
            {
                return;
            }
            if (Project_.GetProjectID() != m_ProjectID)
            {
                throw std::invalid_argument(
                    "View render output received a different project context");
            }

            if (!m_pRenderService->HasScene(m_ProjectID, m_SourceSceneID))
            {
                if (m_nSourceRevision != 0 || m_nContentRevision != Content_.nRevision)
                {
                    m_pRenderService->ClearScene(m_ProjectID, m_TargetSceneID);
                    m_LastObjects.clear();
                    m_LastGeometryIDs.clear();
                    m_nSourceRevision = 0;
                    m_nContentRevision = Content_.nRevision;
                }
                return;
            }

            const auto _nSourceRevision =
                m_pRenderService->GetSceneRevision(m_ProjectID, m_SourceSceneID);
            if (_nSourceRevision == m_nSourceRevision
                && Content_.nRevision == m_nContentRevision)
            {
                return;
            }

            const auto _Source =
                m_pRenderService->GetSceneSnapshot(m_ProjectID, m_SourceSceneID);
            ProjectSnapshot(_Source, Content_);
            m_nSourceRevision = _nSourceRevision;
            m_nContentRevision = Content_.nRevision;
        }

        void Close() override
        {
            if (m_bClosed)
            {
                return;
            }
            m_bClosed = true;
            if (m_pRenderService)
            {
                m_pRenderService->DestroyScene(m_ProjectID, m_TargetSceneID);
            }
        }

    private:
        void ProjectSnapshot(
            IN const SRenderSceneSnapshot& Source_,
            IN const iCAX::View::SViewContent& Content_)
        {
            std::unordered_map<iCAX::Data::uuid, iCAX::Data::ObjectMap> _Presentations;
            _Presentations.reserve(Content_.Objects.size());
            for (const auto& _Object : Content_.Objects)
            {
                _Presentations.emplace(_Object.EntityID, _Object.Presentation);
            }

            std::vector<SRenderInstanceData> _Objects;
            std::unordered_set<RenderGeometryID> _GeometryIDs;
            _Objects.reserve(Content_.Objects.size());
            _GeometryIDs.reserve(Content_.Objects.size());
            for (auto _Object : Source_.Objects)
            {
                const auto _PresentationIter = _Presentations.find(_Object.nObjectID);
                if (_PresentationIter == _Presentations.end())
                {
                    continue;
                }

                ApplyPresentation(_PresentationIter->second, _Object);
                if (const auto _PreviousIter = m_LastObjects.find(_Object.nObjectID);
                    _PreviousIter != m_LastObjects.end())
                {
                    if (SameObjectPayload(_PreviousIter->second, _Object))
                    {
                        _Object.nDataVersion =
                            (std::max)(
                                _Object.nDataVersion,
                                _PreviousIter->second.nDataVersion);
                    }
                    else if (_Object.nDataVersion <= _PreviousIter->second.nDataVersion)
                    {
                        _Object.nDataVersion =
                            MakeNewerVersion(_PreviousIter->second.nDataVersion);
                    }
                }
                _GeometryIDs.insert(_Object.nGeometryID);
                _Objects.push_back(std::move(_Object));
            }

            for (const auto& _GeometryID : m_LastGeometryIDs)
            {
                if (_GeometryIDs.find(_GeometryID) == _GeometryIDs.end())
                {
                    m_pRenderService->RemoveGeometry(
                        m_ProjectID,
                        m_TargetSceneID,
                        _GeometryID);
                }
            }
            for (const auto& _GeometryID : _GeometryIDs)
            {
                if (const auto _Iter = Source_.Meshes.find(_GeometryID);
                    _Iter != Source_.Meshes.end())
                {
                    m_pRenderService->UpsertMesh(
                        m_ProjectID,
                        m_TargetSceneID,
                        _Iter->second);
                    continue;
                }
                if (const auto _Iter = Source_.Polylines.find(_GeometryID);
                    _Iter != Source_.Polylines.end())
                {
                    m_pRenderService->UpsertPolyline(
                        m_ProjectID,
                        m_TargetSceneID,
                        _Iter->second);
                    continue;
                }
                if (const auto _Iter = Source_.Toolpaths.find(_GeometryID);
                    _Iter != Source_.Toolpaths.end())
                {
                    m_pRenderService->UpsertToolpath(
                        m_ProjectID,
                        m_TargetSceneID,
                        _Iter->second);
                }
            }

            std::unordered_set<TransformID> _TransformIDs;
            _TransformIDs.reserve(_Objects.size() + Source_.Cameras.size());
            for (const auto& _Object : _Objects)
            {
                _TransformIDs.insert(_Object.nObjectID);
            }
            for (const auto& _Camera : Source_.Cameras)
            {
                _TransformIDs.insert(_Camera.nCameraID);
            }

            std::vector<STransformData> _Transforms;
            _Transforms.reserve(_TransformIDs.size());
            for (const auto& _Transform : Source_.Transforms)
            {
                if (_TransformIDs.find(_Transform.nTransformID) != _TransformIDs.end())
                {
                    _Transforms.push_back(_Transform);
                }
            }

            m_pRenderService->SetObjects(
                m_ProjectID,
                m_TargetSceneID,
                _Objects);
            m_pRenderService->SetTransforms(
                m_ProjectID,
                m_TargetSceneID,
                _Transforms,
                MakeNewerVersion(m_nProjectionVersion));
            m_nProjectionVersion = MakeNewerVersion(m_nProjectionVersion);
            m_pRenderService->SetCameras(
                m_ProjectID,
                m_TargetSceneID,
                Source_.Cameras,
                Source_.nActiveCameraID);

            m_LastObjects.clear();
            m_LastObjects.reserve(_Objects.size());
            for (const auto& _Object : _Objects)
            {
                m_LastObjects.emplace(_Object.nObjectID, _Object);
            }
            m_LastGeometryIDs = std::move(_GeometryIDs);
        }

    private:
        std::shared_ptr<iCAX::Render::IRenderService> m_pRenderService;
        iCAX::Data::uuid m_ProjectID;
        RenderSceneID m_SourceSceneID = kInvalidRenderSceneID;
        RenderSceneID m_TargetSceneID = kInvalidRenderSceneID;
        RenderDataVersion m_nSourceRevision = 0;
        RenderDataVersion m_nContentRevision = 0;
        RenderDataVersion m_nProjectionVersion = 0;
        std::unordered_map<SceneObjectID, SRenderInstanceData> m_LastObjects;
        std::unordered_set<RenderGeometryID> m_LastGeometryIDs;
        bool m_bClosed = false;
    };
}

std::unique_ptr<iCAX::View::IViewOutputSession>
iCAX::CAM::CLaser3DCAMRenderViewOutputProvider::Open(
    IN iCAX::Project::IProjectContext& Project_,
    IN iCAX::Project::ISceneContext& Scene_,
    IN const iCAX::View::SViewInstanceInfo& Instance_) const
{
    auto _pRenderService =
        Scene_.Services().Resolve<iCAX::Render::IRenderService>();
    return std::make_unique<CRenderViewOutputSession>(
        std::move(_pRenderService),
        Project_.GetProjectID(),
        Scene_.GetSceneID(),
        Instance_.InstanceID);
}
