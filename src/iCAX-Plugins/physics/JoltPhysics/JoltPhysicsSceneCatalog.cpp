#include "pch.h"
#include "JoltPhysicsSceneCatalog.h"


class iCAX::JoltPhysics::CJoltPhysicsSceneCatalog::Impl final
{
public:
    std::unordered_map<PhysicsSceneID, CJoltPhysicsScene> Scenes;
};

iCAX::JoltPhysics::CJoltPhysicsSceneCatalog::CJoltPhysicsSceneCatalog()
    : m_pImpl(std::make_unique<Impl>())
{
}

iCAX::JoltPhysics::CJoltPhysicsSceneCatalog::~CJoltPhysicsSceneCatalog()
{
    Clear();
}

iCAX::JoltPhysics::CJoltPhysicsScene& iCAX::JoltPhysics::CJoltPhysicsSceneCatalog::CreateScene(IN PhysicsSceneID nSceneID_)
{
    if (nSceneID_ == kInvalidPhysicsSceneID)
    {
        throw std::invalid_argument("Jolt physics scene id cannot be zero");
    }
    if (m_pImpl->Scenes.find(nSceneID_) != m_pImpl->Scenes.end())
    {
        throw std::invalid_argument("Jolt physics scene id already exists");
    }

    auto [_Iter, _Inserted] = m_pImpl->Scenes.emplace(nSceneID_, CJoltPhysicsScene());
    _Iter->second.Initialize(nSceneID_);
    return _Iter->second;
}

bool iCAX::JoltPhysics::CJoltPhysicsSceneCatalog::DestroyScene(IN PhysicsSceneID nSceneID_)
{
    const auto _Iter = m_pImpl->Scenes.find(nSceneID_);
    if (_Iter == m_pImpl->Scenes.end())
    {
        return false;
    }

    _Iter->second.Shutdown();
    m_pImpl->Scenes.erase(_Iter);
    return true;
}

iCAX::JoltPhysics::CJoltPhysicsScene* iCAX::JoltPhysics::CJoltPhysicsSceneCatalog::FindScene(IN PhysicsSceneID nSceneID_) noexcept
{
    const auto _Iter = m_pImpl->Scenes.find(nSceneID_);
    return _Iter == m_pImpl->Scenes.end() ? nullptr : &_Iter->second;
}

const iCAX::JoltPhysics::CJoltPhysicsScene* iCAX::JoltPhysics::CJoltPhysicsSceneCatalog::FindScene(IN PhysicsSceneID nSceneID_) const noexcept
{
    const auto _Iter = m_pImpl->Scenes.find(nSceneID_);
    return _Iter == m_pImpl->Scenes.end() ? nullptr : &_Iter->second;
}

std::vector<iCAX::JoltPhysics::PhysicsSceneID> iCAX::JoltPhysics::CJoltPhysicsSceneCatalog::ListSceneIDs() const
{
    std::vector<PhysicsSceneID> _IDs;
    _IDs.reserve(m_pImpl->Scenes.size());
    for (const auto& _Pair : m_pImpl->Scenes)
    {
        _IDs.push_back(_Pair.first);
    }
    return _IDs;
}

void iCAX::JoltPhysics::CJoltPhysicsSceneCatalog::Clear() noexcept
{
    for (auto& _Pair : m_pImpl->Scenes)
    {
        _Pair.second.Shutdown();
    }
    m_pImpl->Scenes.clear();
}
