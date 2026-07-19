#include "pch.h"
#include "Universe.h"
#include "BehaviourDispatcher.h"
#include "IBehaviourRegistry.h"
#include "Task/Coroutine.h"


//!< 构造函数
iCAX::Behaviour::CUniverse::CUniverse(IN std::shared_ptr<IBehaviourRegistry> pRegistry_)
    : m_ID(iCAX::Data::GenerateNewUUID())
    , m_pDispatcher()
    , m_pRegistry(std::move(pRegistry_))
    , m_pEngineTaskScheduler(std::make_shared<iCAX::Tasks::EventLoopTaskScheduler>())
    , m_pCoroutineRuntime(std::make_shared<iCAX::Coroutines::CCoroutineRuntime>(m_pEngineTaskScheduler))
{
    if (!m_pRegistry)
    {
        throw std::invalid_argument("Universe behaviour registry cannot be null");
    }

    m_pDispatcher = std::make_shared<CBehaviourDispatcher>(m_pRegistry, m_pCoroutineRuntime);
}

//!< 析构函数
iCAX::Behaviour::CUniverse::~CUniverse()
{
    Cleanup(true);
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
void iCAX::Behaviour::CUniverse::Tick(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_)
{
    if (m_pDispatcher != nullptr)
    {
        iCAX::Tasks::CurrentTaskSchedulerScope _SchedulerScope(m_pEngineTaskScheduler);
        m_pEngineTaskScheduler->RunAll();
        m_pDispatcher->Tick(
            ApplicationContext_,
            ProductContext_,
            ProjectContext_,
            SceneContext_,
            nDeltaTime_,
            nTotalTime_,
            [this, nDeltaTime_, nTotalTime_]()
            {
                m_pCoroutineRuntime->Tick(nDeltaTime_, nTotalTime_);
            });
    }
}

//! 帧后交换PDO双缓冲
void iCAX::Behaviour::CUniverse::PostSwapPDO()
{
}

//! 清空
void iCAX::Behaviour::CUniverse::Cleanup(IN const bool& bForced_)
{
    if (bForced_)
    {
        if (m_pDispatcher)
        {
            m_pDispatcher->Shutdown();
        }
        m_pDispatcher = nullptr;
        m_pCoroutineRuntime = nullptr;
    }
}

//! 是否有效
bool iCAX::Behaviour::CUniverse::IsValid() const
{
    return m_pDispatcher != nullptr;
}

//! 修改前事件
void iCAX::Behaviour::CUniverse::OnRepositoryChanging(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
    IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (m_pDispatcher)
    {
        m_pDispatcher->OnRepositoryChanging(ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, Args_);
    }
}

//! 更改后事件
void iCAX::Behaviour::CUniverse::OnRepositoryChanged(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
    IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (m_pDispatcher)
    {
        m_pDispatcher->OnRepositoryChanged(ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, Args_);
    }
}

bool iCAX::Behaviour::CUniverse::BindBehaviourByIndex(IN const std::type_index& nType_)
{
    return m_pDispatcher ? m_pDispatcher->BindBehaviour(nType_) : false;
}

bool iCAX::Behaviour::CUniverse::HasBindBehaviourByIndex(IN const std::type_index& nType_) const
{
    return m_pDispatcher ? m_pDispatcher->HasBehaviour(nType_) : false;
}

void iCAX::Behaviour::CUniverse::UnbindBehaviourByIndex(IN const std::type_index& nType_)
{
    if (m_pDispatcher)
    {
        m_pDispatcher->UnbindBehaviour(nType_);
    }
}

void iCAX::Behaviour::CUniverse::PauseFrameUpdateByIndex(IN const std::type_index& nType_)
{
    if (m_pDispatcher)
    {
        m_pDispatcher->PauseFrameUpdate(nType_);
    }
}

bool iCAX::Behaviour::CUniverse::IsFrameUpdatePausedByIndex(IN const std::type_index& nType_) const
{
    return m_pDispatcher ? m_pDispatcher->IsFrameUpdatePaused(nType_) : false;
}

void iCAX::Behaviour::CUniverse::ResumeFrameUpdateByIndex(IN const std::type_index& nType_)
{
    if (m_pDispatcher)
    {
        m_pDispatcher->ResumeFrameUpdate(nType_);
    }
}

std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> iCAX::Behaviour::CUniverse::GetFrameUpdatePausedBehaviours() const
{
    return m_pDispatcher ? m_pDispatcher->GetFrameUpdatePaused() : std::vector<std::shared_ptr<CBehaviourBase>>{};
}

std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> iCAX::Behaviour::CUniverse::GetAllBehaviours() const
{
    return m_pDispatcher ? m_pDispatcher->GetAll() : std::vector<std::shared_ptr<CBehaviourBase>>{};
}

iCAX::Tasks::TaskSchedulerPtr iCAX::Behaviour::CUniverse::GetEngineTaskScheduler() const
{
    return m_pEngineTaskScheduler;
}
