#include "pch.h"
#include "Universe.h"
#include "World.h"
#include "Timer.h"

//!< 构造函数
iCAX::Behaviour::CUniverse::CUniverse(IN std::shared_ptr<IUniverseContext> pContext_)
{
    m_pContext = pContext_;
}

//!< 析构函数
iCAX::Behaviour::CUniverse::~CUniverse()
{
}

void iCAX::Behaviour::CUniverse::Initialize()
{
    m_pContext->GetDatabase().AddObserver(shared_from_this());
    m_pRootWorld = std::make_shared<CWorld>(shared_from_this(), m_pContext->GetDatabase().GetID());
}

//!< 获取宇宙ID
iCAX::Data::uuid iCAX::Behaviour::CUniverse::GetID() const
{
    return m_pContext->GetDatabase().GetID();
}

//! 帧前交换PDO双缓冲
void iCAX::Behaviour::CUniverse::PreSwapPDO()
{
    m_pRootWorld->PreSwapPDO();
}

//!< 每帧执行
void iCAX::Behaviour::CUniverse::Tick()
{
    dynamic_cast<CTimer&>(m_pContext->GetTimer()).Tick();//! 此处强转为CTimer，然后更新
    if (m_pRootWorld != nullptr)
    {
        m_pRootWorld->Tick(*m_pContext, m_pContext->GetTimer().GetDeltaTime(), m_pContext->GetTimer().GetTime());
    }
    m_pContext->GetDatabase().ResetComponentChangedFlag();
}

//! 帧后交换PDO双缓冲
void iCAX::Behaviour::CUniverse::PostSwapPDO()
{
    m_pRootWorld->PostSwapPDO();
}

//!< 获取根世界
iCAX::Behaviour::IWorld& iCAX::Behaviour::CUniverse::GetRootWorld()
{
    return *m_pRootWorld;
}

//!< 获取根世界
const iCAX::Behaviour::IWorld& iCAX::Behaviour::CUniverse::GetRootWorld() const
{
    return *m_pRootWorld;
}

//!< 获取环境
iCAX::Behaviour::IUniverseContext& iCAX::Behaviour::CUniverse::GetContext() const
{
    return *m_pContext.get();
}

//! 清空
void iCAX::Behaviour::CUniverse::Cleanup(IN const bool& bFaorced_)
{
    m_pRootWorld->Cleanup();

    if (bFaorced_)
    {
        m_pRootWorld = nullptr;
    }
}

//! 是否有效
bool iCAX::Behaviour::CUniverse::IsValid() const
{
    return m_pRootWorld != nullptr;
}

//! 修改前事件
void iCAX::Behaviour::CUniverse::OnRepositoryChanging(IN void* pSender_, IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    m_pRootWorld->OnRepositoryChanging(GetContext(), Args_.nType, Args_.DomainID, Args_.pComponent, Args_.NewProperties);
}

//! 更改后事件
void iCAX::Behaviour::CUniverse::OnRepositoryChanged(IN void* pSender_, IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    m_pRootWorld->OnRepositoryChanged(GetContext(), Args_.nType, Args_.DomainID, Args_.pComponent, Args_.NewProperties);
}
