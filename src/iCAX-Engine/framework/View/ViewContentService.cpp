#include "pch.h"

#include "ViewContentService.h"

#include "Data/VariantSerializer.h"
#include "Database/IEntityViewEvent.h"
#include "Database/IRepository.h"
#include "Database/IRepositoryEvent.h"
#include "SDO/SDOEndpoint.h"
#include "SDO/SDOFrame.h"
#include "SDO/SDOMethod.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"

#include <atomic>
#include <functional>
#include <tuple>

namespace
{
    inline constexpr uint64_t kViewContentChangedEvent =
        iCAX::Interaction::MakeSDOMethodCode("View", "ContentChanged");

    struct SViewContentCacheKey final
    {
        std::string SceneID;
        iCAX::View::ViewDefinitionID DefinitionID;
        std::string ContextKey;

        bool operator<(IN const SViewContentCacheKey& Other_) const
        {
            return std::tie(SceneID, DefinitionID, ContextKey)
                < std::tie(Other_.SceneID, Other_.DefinitionID, Other_.ContextKey);
        }
    };

    struct SViewContentCacheEntry final
    {
        iCAX::Database::IRepository* pRepository = nullptr;
        std::shared_ptr<iCAX::Database::IEntityView> pEntityView;
        uint64_t nEntityViewRevision = 0;
        iCAX::View::SViewContent Content;
    };

    struct SViewInstanceState final
    {
        iCAX::View::ViewInstanceID InstanceID;
        iCAX::Data::uuid SceneID;
        iCAX::View::SViewDefinition Definition;
        iCAX::Data::ObjectMap Context;
        iCAX::Database::IRepository* pRepository = nullptr;
        std::shared_ptr<iCAX::Database::IEntityView> pEntityView;
        std::shared_ptr<iCAX::Database::IEntityViewEventListener> pEntityViewObserver;
        std::shared_ptr<iCAX::Database::IRepositoryEventListener> pRepositoryObserver;
        std::vector<std::unique_ptr<iCAX::View::IViewOutputSession>> OutputSessions;
        iCAX::View::SViewContent Content;
        std::atomic_bool bContentDirty = true;
        std::atomic_bool bClosed = false;
        std::mutex ContentMutex;
    };

    class CViewInstanceObserver final
        : public iCAX::Database::IEntityViewEventListener
        , public iCAX::Database::IRepositoryEventListener
    {
    public:
        explicit CViewInstanceObserver(IN std::weak_ptr<SViewInstanceState> pState_)
            : m_pState(std::move(pState_))
        {
        }

        void OnEntityViewChanged(
            IN void*,
            IN const iCAX::Database::EntityViewEventArgs&) override
        {
            MarkDirty();
        }

        void OnRepositoryChanging(
            IN void*,
            IN const iCAX::Database::RepositoryEventArgs&) override
        {
        }

        void OnRepositoryChanged(
            IN void*,
            IN const iCAX::Database::RepositoryEventArgs&) override
        {
            MarkDirty();
        }

    private:
        void MarkDirty()
        {
            if (const auto _pState = m_pState.lock(); _pState && !_pState->bClosed.load())
            {
                _pState->bContentDirty.store(true);
            }
        }

    private:
        std::weak_ptr<SViewInstanceState> m_pState;
    };

    std::string MakeContextKey(IN const iCAX::Data::ObjectMap& Context_)
    {
        return iCAX::Data::VariantSerializer::Serialize(iCAX::Data::Variant(Context_));
    }

    bool SameObjects(
        IN const std::vector<iCAX::View::SViewObject>& Left_,
        IN const std::vector<iCAX::View::SViewObject>& Right_)
    {
        if (Left_.size() != Right_.size())
        {
            return false;
        }
        for (size_t _Index = 0; _Index < Left_.size(); ++_Index)
        {
            if (Left_[_Index].EntityID != Right_[_Index].EntityID
                || Left_[_Index].Presentation != Right_[_Index].Presentation)
            {
                return false;
            }
        }
        return true;
    }

    bool SameOutputs(
        IN const std::vector<iCAX::View::SViewOutputDescriptor>& Left_,
        IN const std::vector<iCAX::View::SViewOutputDescriptor>& Right_)
    {
        if (Left_.size() != Right_.size())
        {
            return false;
        }
        for (size_t _Index = 0; _Index < Left_.size(); ++_Index)
        {
            if (Left_[_Index].Type != Right_[_Index].Type
                || Left_[_Index].Properties != Right_[_Index].Properties)
            {
                return false;
            }
        }
        return true;
    }

    bool RebuildInstanceContent(
        IN const std::shared_ptr<SViewInstanceState>& pState_,
        IN iCAX::Project::ISceneContext& Scene_)
    {
        if (!pState_ || pState_->bClosed.load())
        {
            return false;
        }

        std::lock_guard<std::mutex> _Lock(pState_->ContentMutex);
        if (!pState_->bContentDirty.exchange(false) && pState_->Content.nRevision != 0)
        {
            return false;
        }

        try
        {
            iCAX::View::SViewContent _Built;
            _Built.InstanceID = pState_->InstanceID;
            _Built.DefinitionID = pState_->Definition.ID;
            _Built.Outputs.reserve(pState_->OutputSessions.size());
            for (const auto& _pSession : pState_->OutputSessions)
            {
                _Built.Outputs.push_back(_pSession->GetDescriptor());
            }

            for (const auto& _EntityID : pState_->pEntityView->GetEntityIDs())
            {
                iCAX::View::SViewObject _Object;
                _Object.EntityID = _EntityID;
                if (pState_->Definition.pPresentationProvider)
                {
                    _Object.Presentation =
                        pState_->Definition.pPresentationProvider->Resolve(
                            Scene_,
                            _EntityID,
                            pState_->Context);
                }
                _Built.Objects.push_back(std::move(_Object));
            }

            const bool _bChanged =
                pState_->Content.nRevision == 0
                || !SameObjects(pState_->Content.Objects, _Built.Objects)
                || !SameOutputs(pState_->Content.Outputs, _Built.Outputs);
            _Built.nRevision = pState_->Content.nRevision == 0
                ? 1
                : pState_->Content.nRevision + (_bChanged ? 1 : 0);
            pState_->Content = std::move(_Built);
            return _bChanged;
        }
        catch (...)
        {
            pState_->bContentDirty.store(true);
            throw;
        }
    }

    iCAX::View::SViewContent CopyInstanceContent(
        IN const std::shared_ptr<SViewInstanceState>& pState_)
    {
        std::lock_guard<std::mutex> _Lock(pState_->ContentMutex);
        return pState_->Content;
    }

    void SynchronizeInstanceOutputs(
        IN const std::shared_ptr<SViewInstanceState>& pState_,
        IN iCAX::Project::IProjectContext& Project_,
        IN iCAX::Project::ISceneContext& Scene_)
    {
        const auto _Content = CopyInstanceContent(pState_);
        for (const auto& _pSession : pState_->OutputSessions)
        {
            _pSession->Synchronize(Project_, Scene_, _Content);
        }
    }

    void ReleaseInstance(IN const std::shared_ptr<SViewInstanceState>& pState_)
    {
        if (!pState_ || pState_->bClosed.exchange(true))
        {
            return;
        }

        for (auto& _pSession : pState_->OutputSessions)
        {
            try
            {
                _pSession->Close();
            }
            catch (...)
            {
                // Service unload/scene close must continue releasing the remaining resources.
            }
        }
        pState_->OutputSessions.clear();

        if (pState_->pEntityView && pState_->pEntityViewObserver)
        {
            pState_->pEntityView->RemoveObserver(pState_->pEntityViewObserver);
        }
        if (pState_->pRepository && pState_->pRepositoryObserver)
        {
            pState_->pRepository->RemoveObserver(pState_->pRepositoryObserver);
        }
        if (pState_->pRepository && pState_->pEntityView)
        {
            pState_->pRepository->ReleaseEntityView(pState_->pEntityView);
        }
        pState_->pEntityView.reset();
        pState_->pEntityViewObserver.reset();
        pState_->pRepositoryObserver.reset();
    }

    std::string MakeContentChangedPayload(IN const iCAX::View::SViewContent& Content_)
    {
        iCAX::Data::VariantArray _Outputs;
        _Outputs.reserve(Content_.Outputs.size());
        for (const auto& _Output : Content_.Outputs)
        {
            iCAX::Data::ObjectMap _Item;
            _Item["type"] = _Output.Type;
            _Item["properties"] = _Output.Properties;
            _Outputs.emplace_back(std::move(_Item));
        }

        iCAX::Data::ObjectMap _Payload;
        _Payload["viewInstanceId"] = iCAX::Data::to_string(Content_.InstanceID);
        _Payload["viewDefinitionId"] = Content_.DefinitionID;
        _Payload["revision"] = static_cast<unsigned long long>(Content_.nRevision);
        _Payload["outputs"] = std::move(_Outputs);
        return iCAX::Data::VariantSerializer::Serialize(iCAX::Data::Variant(_Payload));
    }
}

struct iCAX::View::CViewContentService::SImpl final
{
    CViewDefinitionRegistry Registry;
    std::mutex Mutex;
    std::map<SViewContentCacheKey, SViewContentCacheEntry> Cache;
    std::map<ViewInstanceID, std::shared_ptr<SViewInstanceState>> Instances;
};

iCAX::View::CViewContentService::CViewContentService()
    : m_pImpl(std::make_unique<SImpl>())
{
}

iCAX::View::CViewContentService::~CViewContentService() = default;

void iCAX::View::CViewContentService::OnLoad()
{
}

void iCAX::View::CViewContentService::OnUnload()
{
    std::vector<std::shared_ptr<SViewInstanceState>> _Instances;
    std::map<SViewContentCacheKey, SViewContentCacheEntry> _Cache;
    {
        std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
        for (auto& [_, _pInstance] : m_pImpl->Instances)
        {
            _Instances.push_back(std::move(_pInstance));
        }
        m_pImpl->Instances.clear();
        _Cache.swap(m_pImpl->Cache);
    }

    for (const auto& _pInstance : _Instances)
    {
        ReleaseInstance(_pInstance);
    }
    for (auto& [_, _Entry] : _Cache)
    {
        if (_Entry.pRepository && _Entry.pEntityView)
        {
            _Entry.pRepository->ReleaseEntityView(_Entry.pEntityView);
        }
    }
}

void iCAX::View::CViewContentService::OnSceneTick(
    IN const iCAX::Application::IApplicationContext&,
    IN const iCAX::Product::IProductContext&,
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN iCAX::Project::ISceneContext& SceneContext_,
    IN const double&,
    IN const double&)
{
    std::vector<std::shared_ptr<SViewInstanceState>> _Instances;
    {
        std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
        for (const auto& [_, _pInstance] : m_pImpl->Instances)
        {
            if (_pInstance->SceneID == SceneContext_.GetSceneID()
                && !_pInstance->bClosed.load())
            {
                _Instances.push_back(_pInstance);
            }
        }
    }

    for (const auto& _pInstance : _Instances)
    {
        const bool _bContentChanged = RebuildInstanceContent(_pInstance, SceneContext_);
        SynchronizeInstanceOutputs(_pInstance, ProjectContext_, SceneContext_);
        if (_bContentChanged)
        {
            const auto _Content = CopyInstanceContent(_pInstance);
            SceneContext_.GetBackendSDOEndpoint().SendText(
                0,
                kViewContentChangedEvent,
                iCAX::Interaction::ESDOFrameKind::Event,
                MakeContentChangedPayload(_Content));
        }
    }
}

iCAX::View::SViewContent iCAX::View::CViewContentService::OpenView(
    IN iCAX::Project::IProjectContext& Project_,
    IN iCAX::Project::ISceneContext& Scene_,
    IN const ViewDefinitionID& DefinitionID_,
    IN const iCAX::Data::ObjectMap& Context_)
{
    const auto _Definition = m_pImpl->Registry.GetDefinition(DefinitionID_);
    if (!_Definition)
    {
        throw std::invalid_argument("View definition is not registered: " + DefinitionID_);
    }

    auto _pState = std::make_shared<SViewInstanceState>();
    _pState->InstanceID = iCAX::Data::GenerateNewUUID();
    _pState->SceneID = Scene_.GetSceneID();
    _pState->Definition = *_Definition;
    _pState->Context = Context_;
    _pState->pRepository = &Scene_.Database();
    _pState->pEntityView = Scene_.Database().CreateEntityView(
        _Definition->EntityWhere,
        Context_);

    const auto _pObserver = std::make_shared<CViewInstanceObserver>(_pState);
    _pState->pEntityViewObserver = _pObserver;
    _pState->pEntityView->AddObserver(_pObserver);
    if (_Definition->pPresentationProvider)
    {
        _pState->pRepositoryObserver = _pObserver;
        _pState->pRepository->AddObserver(_pObserver);
    }

    try
    {
        const SViewInstanceInfo _Info{
            _pState->InstanceID,
            DefinitionID_,
            Context_,
        };
        for (const auto& _pProvider : _Definition->OutputProviders)
        {
            if (!_pProvider)
            {
                throw std::logic_error("View output provider cannot be null");
            }
            auto _pSession = _pProvider->Open(Project_, Scene_, _Info);
            if (!_pSession)
            {
                throw std::logic_error("View output provider returned a null session");
            }
            _pState->OutputSessions.push_back(std::move(_pSession));
        }

        RebuildInstanceContent(_pState, Scene_);
        {
            std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
            const auto [_Iter, _bInserted] =
                m_pImpl->Instances.emplace(_pState->InstanceID, _pState);
            if (!_bInserted)
            {
                throw std::logic_error("Generated duplicate View instance id");
            }
        }
        SynchronizeInstanceOutputs(_pState, Project_, Scene_);
        return CopyInstanceContent(_pState);
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
            m_pImpl->Instances.erase(_pState->InstanceID);
        }
        ReleaseInstance(_pState);
        throw;
    }
}

iCAX::View::SViewContent iCAX::View::CViewContentService::GetContent(
    IN iCAX::Project::ISceneContext& Scene_,
    IN const ViewInstanceID& InstanceID_)
{
    std::shared_ptr<SViewInstanceState> _pState;
    {
        std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
        const auto _Iter = m_pImpl->Instances.find(InstanceID_);
        if (_Iter == m_pImpl->Instances.end())
        {
            throw std::invalid_argument(
                "View instance does not exist: " + iCAX::Data::to_string(InstanceID_));
        }
        _pState = _Iter->second;
    }
    if (_pState->SceneID != Scene_.GetSceneID())
    {
        throw std::invalid_argument("View instance does not belong to the current scene");
    }

    RebuildInstanceContent(_pState, Scene_);
    return CopyInstanceContent(_pState);
}

bool iCAX::View::CViewContentService::CloseView(
    IN iCAX::Project::IProjectContext& Project_,
    IN iCAX::Project::ISceneContext& Scene_,
    IN const ViewInstanceID& InstanceID_)
{
    (void)Project_;

    std::shared_ptr<SViewInstanceState> _pState;
    {
        std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
        const auto _Iter = m_pImpl->Instances.find(InstanceID_);
        if (_Iter == m_pImpl->Instances.end())
        {
            return false;
        }
        if (_Iter->second->SceneID != Scene_.GetSceneID())
        {
            throw std::invalid_argument("View instance does not belong to the current scene");
        }
        _pState = std::move(_Iter->second);
        m_pImpl->Instances.erase(_Iter);
    }
    ReleaseInstance(_pState);
    return true;
}

iCAX::View::SViewContent iCAX::View::CViewContentService::GetContent(
    IN iCAX::Project::ISceneContext& Scene_,
    IN const ViewDefinitionID& DefinitionID_,
    IN const iCAX::Data::ObjectMap& Context_)
{
    const auto _Definition = m_pImpl->Registry.GetDefinition(DefinitionID_);
    if (!_Definition)
    {
        throw std::invalid_argument("View definition is not registered: " + DefinitionID_);
    }

    const SViewContentCacheKey _Key{
        iCAX::Data::to_string(Scene_.GetSceneID()),
        DefinitionID_,
        MakeContextKey(Context_),
    };

    std::shared_ptr<iCAX::Database::IEntityView> _pEntityView;
    {
        std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
        _pEntityView = m_pImpl->Cache[_Key].pEntityView;
    }
    if (!_pEntityView)
    {
        auto _pCreatedView = Scene_.Database().CreateEntityView(
            _Definition->EntityWhere,
            Context_);
        {
            std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
            auto& _Entry = m_pImpl->Cache[_Key];
            if (!_Entry.pEntityView)
            {
                _Entry.pRepository = &Scene_.Database();
                _Entry.pEntityView = _pCreatedView;
            }
            _pEntityView = _Entry.pEntityView;
        }
        if (_pEntityView != _pCreatedView)
        {
            Scene_.Database().ReleaseEntityView(_pCreatedView);
        }
    }

    const auto _nEntityViewRevision = _pEntityView->GetRevision();
    if (!_Definition->pPresentationProvider)
    {
        std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
        const auto& _Entry = m_pImpl->Cache.at(_Key);
        if (_Entry.Content.nRevision != 0
            && _Entry.nEntityViewRevision == _nEntityViewRevision)
        {
            return _Entry.Content;
        }
    }

    SViewContent _Built;
    _Built.DefinitionID = DefinitionID_;
    for (const auto& _EntityID : _pEntityView->GetEntityIDs())
    {
        SViewObject _Object;
        _Object.EntityID = _EntityID;
        if (_Definition->pPresentationProvider)
        {
            _Object.Presentation = _Definition->pPresentationProvider->Resolve(
                Scene_,
                _EntityID,
                Context_);
        }
        _Built.Objects.push_back(std::move(_Object));
    }

    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    auto& _Entry = m_pImpl->Cache[_Key];
    if (_Entry.Content.nRevision == 0)
    {
        _Built.nRevision = 1;
    }
    else if (SameObjects(_Entry.Content.Objects, _Built.Objects))
    {
        _Built.nRevision = _Entry.Content.nRevision;
    }
    else
    {
        _Built.nRevision = _Entry.Content.nRevision + 1;
    }
    _Entry.nEntityViewRevision = _nEntityViewRevision;
    _Entry.Content = std::move(_Built);
    return _Entry.Content;
}

bool iCAX::View::CViewContentService::RegisterDefinition(
    IN SViewDefinition Definition_)
{
    return m_pImpl->Registry.Register(std::move(Definition_));
}
