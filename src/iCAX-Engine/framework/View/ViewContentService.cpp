#include "pch.h"

#include "ViewContentService.h"

#include "Data/VariantSerializer.h"
#include "Database/IRepository.h"
#include "ProjectContext/ISceneContext.h"

#include <tuple>

namespace
{
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
}

struct iCAX::View::CViewContentService::SImpl final
{
    CViewDefinitionRegistry Registry;
    std::mutex Mutex;
    std::map<SViewContentCacheKey, SViewContentCacheEntry> Cache;
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
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    for (auto& [_, _Entry] : m_pImpl->Cache)
    {
        if (_Entry.pRepository && _Entry.pEntityView)
        {
            _Entry.pRepository->ReleaseEntityView(_Entry.pEntityView);
        }
    }
    m_pImpl->Cache.clear();
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
