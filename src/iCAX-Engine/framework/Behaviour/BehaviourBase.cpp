#include "pch.h"
#include "BehaviourBase.h"
#include "IUniverse.h"

void iCAX::Behaviour::CBehaviourBase::Awake(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_)
{
    OnAwake(Component_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
}

void iCAX::Behaviour::CBehaviourBase::Start(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_)
{
    OnStart(Component_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
}

void iCAX::Behaviour::CBehaviourBase::Enable(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_)
{
    OnEnable(Component_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
}

void iCAX::Behaviour::CBehaviourBase::PreUpdate(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_)
{
    OnPreUpdate(Component_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, nDeltaTime_, nTotalTime_);
}

void iCAX::Behaviour::CBehaviourBase::Update(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_)
{
    OnUpdate(Component_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, nDeltaTime_, nTotalTime_);
}

void iCAX::Behaviour::CBehaviourBase::PostUpdate(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_)
{
    OnPostUpdate(Component_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, nDeltaTime_, nTotalTime_);
}

void iCAX::Behaviour::CBehaviourBase::Disable(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_)
{
    OnDisable(Component_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
}

void iCAX::Behaviour::CBehaviourBase::Destroy(
    IN const CComponentDestroyInfo& DestroyInfo_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_)
{
    OnDestroy(DestroyInfo_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
}

void iCAX::Behaviour::CBehaviourBase::DestroyImmediate(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_)
{
    OnDestroyImmediate(Component_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
}

void iCAX::Behaviour::CBehaviourBase::Modifying(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Data::PropertySet& NewValues_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_)
{
    OnModifying(Component_, NewValues_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
}

void iCAX::Behaviour::CBehaviourBase::Modified(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Data::PropertySet& NewValues_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_)
{
    OnModified(Component_, NewValues_, ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_);
}

void iCAX::Behaviour::CBehaviourBase::Detach()
{
    OnDetach();
}

void iCAX::Behaviour::CBehaviourBase::CancelCoroutine(
    IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_)
{
    if (auto _pRuntime = m_pCoroutineRuntime.lock())
    {
        _pRuntime->Cancel(Handle_);
    }
}

void iCAX::Behaviour::CBehaviourBase::CancelAllCoroutines(
    IN iCAX::Database::CComponentBase& Component_)
{
    auto _pRuntime = m_pCoroutineRuntime.lock();
    if (!_pRuntime)
    {
        return;
    }

    const auto _Found = m_ComponentCoroutines.find(&Component_);
    if (_Found == m_ComponentCoroutines.end())
    {
        return;
    }
    for (const auto& _Handle : _Found->second)
    {
        _pRuntime->Cancel(_Handle);
    }
    _Found->second.clear();
}

bool iCAX::Behaviour::CBehaviourBase::IsCoroutineRunning(
    IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_) const
{
    if (auto _pRuntime = m_pCoroutineRuntime.lock())
    {
        return _pRuntime->IsRunning(Handle_);
    }
    return false;
}

void iCAX::Behaviour::CBehaviourBase::BindCoroutineRuntime(
    IN const std::shared_ptr<iCAX::Coroutines::CCoroutineRuntime>& pRuntime_)
{
    m_pCoroutineRuntime = pRuntime_;
}

void iCAX::Behaviour::CBehaviourBase::UnbindCoroutineRuntime()
{
    m_pCoroutineRuntime.reset();
    ClearTrackedCoroutines();
}

void iCAX::Behaviour::CBehaviourBase::AttachComponentCoroutines(
    IN iCAX::Database::CComponentBase& Component_)
{
    if (m_ClosingCoroutineComponents.contains(&Component_))
    {
        throw std::logic_error("Cannot attach coroutines while the component is being destroyed");
    }
    m_ComponentCoroutines.try_emplace(&Component_);
}

void iCAX::Behaviour::CBehaviourBase::SetComponentCoroutinesEnabled(
    IN iCAX::Database::CComponentBase& Component_,
    IN bool Enabled_)
{
    if (m_ClosingCoroutineComponents.contains(&Component_))
    {
        return;
    }

    const auto _Found = m_ComponentCoroutines.find(&Component_);
    if (_Found == m_ComponentCoroutines.end())
    {
        AttachComponentCoroutines(Component_);
        return;
    }

    auto _pRuntime = m_pCoroutineRuntime.lock();
    if (!_pRuntime)
    {
        return;
    }
    for (const auto& _Handle : _Found->second)
    {
        if (Enabled_)
        {
            _pRuntime->Resume(_Handle);
        }
        else
        {
            _pRuntime->Pause(_Handle);
        }
    }
}

void iCAX::Behaviour::CBehaviourBase::DetachComponentCoroutines(
    IN iCAX::Database::CComponentBase& Component_)
{
    m_ClosingCoroutineComponents.emplace(&Component_);
    const auto _Found = m_ComponentCoroutines.find(&Component_);
    if (_Found == m_ComponentCoroutines.end())
    {
        return;
    }

    if (auto _pRuntime = m_pCoroutineRuntime.lock())
    {
        for (const auto& _Handle : _Found->second)
        {
            _pRuntime->Cancel(_Handle);
        }
    }
    m_ComponentCoroutines.erase(_Found);
}

void iCAX::Behaviour::CBehaviourBase::CompleteComponentCoroutineDetach(
    IN iCAX::Database::CComponentBase& Component_) noexcept
{
    m_ClosingCoroutineComponents.erase(&Component_);
}

void iCAX::Behaviour::CBehaviourBase::CancelAllTrackedCoroutines()
{
    if (auto _pRuntime = m_pCoroutineRuntime.lock())
    {
        for (const auto& [_pComponent, _Handles] : m_ComponentCoroutines)
        {
            for (const auto& _Handle : _Handles)
            {
                _pRuntime->Cancel(_Handle);
            }
        }
    }
    ClearTrackedCoroutines();
}

void iCAX::Behaviour::CBehaviourBase::ClearTrackedCoroutines() noexcept
{
    m_ComponentCoroutines.clear();
    m_ClosingCoroutineComponents.clear();
}
