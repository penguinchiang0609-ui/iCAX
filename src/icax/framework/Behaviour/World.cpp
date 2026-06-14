#include "pch.h"
#include "World.h"
#include "Universe.h"
#include "BehaviourDispacther.h"
#include "WorldContext.h"

// Constructor for child world.
iCAX::Behaviour::CWorld::CWorld(std::weak_ptr<CWorld> pParent_, const iCAX::Data::uuid& ID_)
    : m_ID(ID_)
    , m_pUniverse()
    , m_pParent(pParent_)
    , m_Children()
    , m_bPause(false)
    , m_PDOHub()
{
    m_BehaviourDispatcher = std::make_shared<CBehaviourDispatcher>();
    m_Context = std::make_shared<CWorldContext>();
}

// Constructor for root world.
iCAX::Behaviour::CWorld::CWorld(std::weak_ptr<IUniverse> pUniverse_, const iCAX::Data::uuid& ID_)
    : m_ID(ID_)
    , m_pUniverse(pUniverse_)
    , m_pParent()
    , m_Children()
    , m_bPause(false)
    , m_PDOHub()
{
    m_BehaviourDispatcher = std::make_shared<CBehaviourDispatcher>();
    m_Context = std::make_shared<CWorldContext>();
}

iCAX::Behaviour::CWorld::~CWorld()
{
}

iCAX::Data::uuid iCAX::Behaviour::CWorld::GetID() const
{
    return m_ID;
}

void iCAX::Behaviour::CWorld::InitialziePDO(IN std::vector<iCAX::PDO::PDODecl> Descs_)
{
    m_PDOHub = iCAX::PDO::GeneratePDOHub(Descs_);
}

bool iCAX::Behaviour::CWorld::HasPDO() const
{
    return m_PDOHub != nullptr;
}

iCAX::PDO::IPDOHub& iCAX::Behaviour::CWorld::GetPDOHub()
{
    if (m_PDOHub == nullptr)
    {
        throw std::runtime_error(std::format("{} world has no pdo hub", iCAX::Data::to_string(m_ID)));
    }
    return *m_PDOHub;
}

void iCAX::Behaviour::CWorld::PreSwapPDO()
{
    if (m_PDOHub != nullptr)
    {
        m_PDOHub->SwapInSlot();
    }
}

void iCAX::Behaviour::CWorld::PostSwapPDO()
{
    if (m_PDOHub != nullptr)
    {
        m_PDOHub->SwapOutSlot();
    }
}

void iCAX::Behaviour::CWorld::Tick(IN const IUniverseContext& Context_, IN const double& nDeltaTime_, IN const double& nTotalTime_)
{
    if (!m_bPause)
    {
        m_BehaviourDispatcher->Tick(Context_, *this, nDeltaTime_, nTotalTime_);
    }

    for (auto _pWorld : m_Children)
    {
        _pWorld->Tick(Context_, nDeltaTime_, nTotalTime_);
    }
}

iCAX::Behaviour::IWorld& iCAX::Behaviour::CWorld::CreateChild(IN const iCAX::Data::uuid& ID_)
{
    auto _Ite = std::find_if(m_Children.begin(), m_Children.end(), [=](std::shared_ptr<CWorld> _pWorld)
        {
            return _pWorld->m_ID == ID_;
        });

    if (_Ite != m_Children.end())
    {
        throw;
    }

    auto _pWorld = std::make_shared<CWorld>(shared_from_this(), ID_);
    m_Children.push_back(_pWorld);
    return *_pWorld;
}

std::shared_ptr<iCAX::Behaviour::IWorld> iCAX::Behaviour::CWorld::GetChild(IN const iCAX::Data::uuid& ID_) const
{
    auto _Ite = std::find_if(m_Children.begin(), m_Children.end(), [&](std::shared_ptr<CWorld> _pWorld)
        {
            return _pWorld->m_ID == ID_;
        });

    if (_Ite != m_Children.end())
    {
        return *_Ite;
    }
    return nullptr;
}

std::vector<iCAX::Data::uuid> iCAX::Behaviour::CWorld::GetChildren()
{
    std::vector<iCAX::Data::uuid> _ChildrenIDs;
    for (auto& _pWorld : m_Children)
    {
        _ChildrenIDs.push_back(_pWorld->m_ID);
    }
    return _ChildrenIDs;
}

void iCAX::Behaviour::CWorld::DestoryChild(IN const iCAX::Data::uuid& ID_)
{
    auto _Ite = std::find_if(m_Children.begin(), m_Children.end(), [=](std::shared_ptr<CWorld> _pWorld)
        {
            return _pWorld->GetID() == ID_;
        });
    if (_Ite != m_Children.end())
    {
        (*_Ite)->Cleanup();
        m_Children.erase(_Ite);
    }
}

std::shared_ptr<iCAX::Behaviour::IUniverse> iCAX::Behaviour::CWorld::GetUniverse() const
{
    if (!m_pParent.expired())
    {
        return m_pParent.lock()->GetUniverse();
    }
    return m_pUniverse.lock();
}

std::shared_ptr<iCAX::Database::IDomain> iCAX::Behaviour::CWorld::GetDomain() const
{
    auto _pUniverse = GetUniverse();
    if (_pUniverse)
    {
        return _pUniverse->GetContext().GetDatabase().GetDomain(m_ID);
    }
    return nullptr;
}

std::shared_ptr<iCAX::Behaviour::IWorld> iCAX::Behaviour::CWorld::GetParentWorld() const
{
    return m_pParent.lock();
}

void iCAX::Behaviour::CWorld::Cleanup()
{
    auto _vecChildren = GetChildren();
    for (auto& _ID : _vecChildren)
    {
        DestoryChild(_ID);
    }
}

void iCAX::Behaviour::CWorld::Pause(IN const bool& bIncludeAllChildren_)
{
    m_bPause = true;
    if (bIncludeAllChildren_)
    {
        for (auto _pWorld : m_Children)
        {
            _pWorld->Pause(bIncludeAllChildren_);
        }
    }
}

void iCAX::Behaviour::CWorld::Resume(IN const bool& bIncludeAllChildren_)
{
    m_bPause = false;
    if (bIncludeAllChildren_)
    {
        for (auto _pWorld : m_Children)
        {
            _pWorld->Resume(bIncludeAllChildren_);
        }
    }
}

bool iCAX::Behaviour::CWorld::IsPaused() const
{
    return m_bPause;
}

bool iCAX::Behaviour::CWorld::BindBehaviourByIndex(IN const std::type_index& nType_)
{
    return m_BehaviourDispatcher->Pushback(nType_);
}

bool iCAX::Behaviour::CWorld::HasBindBehaviourByIndex(IN const std::type_index& nType_) const
{
    return m_BehaviourDispatcher->HasBehaviour(nType_);
}

void iCAX::Behaviour::CWorld::UnbindBehaviourByIndex(IN const std::type_index& nType_)
{
    return m_BehaviourDispatcher->UnregisterBehaviour(nType_);
}

void iCAX::Behaviour::CWorld::PauseBehaviourByIndex(IN const std::type_index& nType_)
{
    m_BehaviourDispatcher->Pause(nType_);
}

bool iCAX::Behaviour::CWorld::IsBehaviourPausedByIndex(IN const std::type_index& nType_) const
{
    return m_BehaviourDispatcher->IsPaused(nType_);
}

void iCAX::Behaviour::CWorld::ResumeBehaviourByIndex(IN const std::type_index& nType_)
{
    m_BehaviourDispatcher->Resume(nType_);
}

std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> iCAX::Behaviour::CWorld::GetPausedBehaviours() const
{
    return m_BehaviourDispatcher->GetPaused();
}

std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> iCAX::Behaviour::CWorld::GetAllBehaviours() const
{
    return m_BehaviourDispatcher->GetALL();
}

void iCAX::Behaviour::CWorld::OnRepositoryChanging(IN const IUniverseContext& Context_, IN const iCAX::Database::RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& nDomain_, IN const std::shared_ptr<iCAX::Database::CComponentBase> pComponent_, IN const iCAX::Data::PropertySet& Properties_)
{
    if (nDomain_ == m_ID)
    {
        if (nType_ == iCAX::Database::RepositoryEventArgs::kRemoveComponent)
        {
            m_BehaviourDispatcher->OnNotify(Context_, *this, CBehaviourDispatcher::kDestroyComponent, *pComponent_, {});
        }
        else if (nType_ == iCAX::Database::RepositoryEventArgs::kModifyComponent)
        {
            m_BehaviourDispatcher->OnNotify(Context_, *this, CBehaviourDispatcher::kModifingComponent, *pComponent_, Properties_);
        }
    }
    else
    {
        for (auto& _pChild : m_Children)
        {
            _pChild->OnRepositoryChanging(Context_, nType_, nDomain_, pComponent_, Properties_);
        }
    }
}

void iCAX::Behaviour::CWorld::OnRepositoryChanged(IN const IUniverseContext& Context_, IN const iCAX::Database::RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& nDomain_, IN const std::shared_ptr<iCAX::Database::CComponentBase> pComponent_, IN const iCAX::Data::PropertySet& Properties_)
{
    if (nDomain_ == m_ID)
    {
        if (nType_ == iCAX::Database::RepositoryEventArgs::kAddComponent)
        {
            m_BehaviourDispatcher->OnNotify(Context_, *this, CBehaviourDispatcher::kAddComponent, *pComponent_, {});
        }
        else if (nType_ == iCAX::Database::RepositoryEventArgs::kEnableComponent)
        {
            m_BehaviourDispatcher->OnNotify(Context_, *this, CBehaviourDispatcher::kEnableComponent, *pComponent_, {});
        }
        else if (nType_ == iCAX::Database::RepositoryEventArgs::kDisableComponent)
        {
            m_BehaviourDispatcher->OnNotify(Context_, *this, CBehaviourDispatcher::kDisableComponent, *pComponent_, {});
        }
        else if (nType_ == iCAX::Database::RepositoryEventArgs::kModifyComponent)
        {
            m_BehaviourDispatcher->OnNotify(Context_, *this, CBehaviourDispatcher::kModifiedComponent, *pComponent_, Properties_);
        }
    }
    else
    {
        for (auto& _pChild : m_Children)
        {
            _pChild->OnRepositoryChanged(Context_, nType_, nDomain_, pComponent_, Properties_);
        }
    }
}

iCAX::Behaviour::IWorldContext& iCAX::Behaviour::CWorld::GetContext()
{
    return *m_Context;
}