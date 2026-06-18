#include "pch.h"
#include "Universe.h"
#include "Timer.h"

//!< 构造函数
iCAX::Behaviour::CUniverse::CUniverse()
    : m_ID(iCAX::Data::GenerateNewUUID())
    , m_pDispatcher()
{
}

//!< 析构函数
iCAX::Behaviour::CUniverse::~CUniverse()
{
}

void iCAX::Behaviour::CUniverse::Initialize()
{
    m_pDispatcher = std::make_shared<CBehaviourDispatcher>();
}

//!< 获取宇宙ID
iCAX::Data::uuid iCAX::Behaviour::CUniverse::GetID() const
{
    return m_ID;
}

//! 帧前交换PDO双缓冲
void iCAX::Behaviour::CUniverse::PreSwapPDO()
{
}

//!< 每帧执行
void iCAX::Behaviour::CUniverse::Tick(IN const IUniverseContext& Context_)
{
    dynamic_cast<CTimer&>(Context_.GetTimer()).Tick();//! 此处强转为CTimer，然后更新
    if (m_pDispatcher != nullptr)
    {
        m_pDispatcher->Tick(Context_, Context_.GetTimer().GetDeltaTime(), Context_.GetTimer().GetTime());
    }
}

//! 帧后交换PDO双缓冲
void iCAX::Behaviour::CUniverse::PostSwapPDO()
{
}

//! 清空
void iCAX::Behaviour::CUniverse::Cleanup(IN const bool& bFaorced_)
{
    if (bFaorced_)
    {
        m_pDispatcher = nullptr;
    }
}

//! 是否有效
bool iCAX::Behaviour::CUniverse::IsValid() const
{
    return m_pDispatcher != nullptr;
}

//! 修改前事件
void iCAX::Behaviour::CUniverse::OnRepositoryChanging(
    IN const IUniverseContext& Context_,
    IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (!m_pDispatcher || !Args_.pComponent)
    {
        return;
    }

    if (Args_.nType == iCAX::Database::RepositoryEventArgs::kRemoveComponent)
    {
        m_pDispatcher->OnNotify(Context_, CBehaviourDispatcher::kDestroyComponent, *Args_.pComponent, Args_.PreviousProperties);
    }
    else if (Args_.nType == iCAX::Database::RepositoryEventArgs::kModifyComponent)
    {
        m_pDispatcher->OnNotify(Context_, CBehaviourDispatcher::kModifingComponent, *Args_.pComponent, Args_.NewProperties);
    }
}

//! 更改后事件
void iCAX::Behaviour::CUniverse::OnRepositoryChanged(
    IN const IUniverseContext& Context_,
    IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (!m_pDispatcher || !Args_.pComponent)
    {
        return;
    }

    switch (Args_.nType)
    {
    case iCAX::Database::RepositoryEventArgs::kAddComponent:
        m_pDispatcher->OnNotify(Context_, CBehaviourDispatcher::kAddComponent, *Args_.pComponent, Args_.NewProperties);
        break;
    case iCAX::Database::RepositoryEventArgs::kEnableComponent:
        m_pDispatcher->OnNotify(Context_, CBehaviourDispatcher::kEnableComponent, *Args_.pComponent, Args_.NewProperties);
        break;
    case iCAX::Database::RepositoryEventArgs::kDisableComponent:
        m_pDispatcher->OnNotify(Context_, CBehaviourDispatcher::kDisableComponent, *Args_.pComponent, Args_.NewProperties);
        break;
    case iCAX::Database::RepositoryEventArgs::kModifyComponent:
        m_pDispatcher->OnNotify(Context_, CBehaviourDispatcher::kModifiedComponent, *Args_.pComponent, Args_.NewProperties);
        break;
    default:
        break;
    }
}

bool iCAX::Behaviour::CUniverse::BindBehaviourByIndex(IN const std::type_index& nType_)
{
    return m_pDispatcher ? m_pDispatcher->Pushback(nType_) : false;
}

bool iCAX::Behaviour::CUniverse::HasBindBehaviourByIndex(IN const std::type_index& nType_) const
{
    return m_pDispatcher ? m_pDispatcher->HasBehaviour(nType_) : false;
}

void iCAX::Behaviour::CUniverse::UnbindBehaviourByIndex(IN const std::type_index& nType_)
{
    if (m_pDispatcher)
    {
        m_pDispatcher->UnregisterBehaviour(nType_);
    }
}

void iCAX::Behaviour::CUniverse::PauseBehaviourByIndex(IN const std::type_index& nType_)
{
    if (m_pDispatcher)
    {
        m_pDispatcher->Pause(nType_);
    }
}

bool iCAX::Behaviour::CUniverse::IsBehaviourPausedByIndex(IN const std::type_index& nType_) const
{
    return m_pDispatcher ? m_pDispatcher->IsPaused(nType_) : false;
}

void iCAX::Behaviour::CUniverse::ResumeBehaviourByIndex(IN const std::type_index& nType_)
{
    if (m_pDispatcher)
    {
        m_pDispatcher->Resume(nType_);
    }
}

std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> iCAX::Behaviour::CUniverse::GetPausedBehaviours() const
{
    return m_pDispatcher ? m_pDispatcher->GetPaused() : std::vector<std::shared_ptr<CBehaviourBase>>{};
}

std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> iCAX::Behaviour::CUniverse::GetAllBehaviours() const
{
    return m_pDispatcher ? m_pDispatcher->GetALL() : std::vector<std::shared_ptr<CBehaviourBase>>{};
}
