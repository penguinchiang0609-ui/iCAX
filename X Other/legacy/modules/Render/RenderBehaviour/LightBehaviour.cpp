#include "pch.h"
#include "LightBehaviour.h"
#include "../RenderComponent/LightComponent.h"
#include "Behaviour/IWorld.h"
#include "../RenderComponent/SceneComponent.h"
#include "../RenderService/IRenderService.h"
#include "Behaviour/Universe.h"
#include "CoreComponent/TransformComponent.h"

//! 构造函数
iCAX::Render::LightBehaviour::LightBehaviour()
    : CBehaviourBase()
{
}

//! 析构函数
iCAX::Render::LightBehaviour::~LightBehaviour()
{
}

//! 获取组件类型名称
std::string iCAX::Render::LightBehaviour::GetComponentClass() const
{
    return LightComponent::S_ClassName;
}

//! 创建之后触发
void iCAX::Render::LightBehaviour::OnAwake(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 创建之后下一帧触发
void iCAX::Render::LightBehaviour::OnStart(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<LightComponent&>(Component_);
    auto _vecEntities = World_.GetDomain()->FilterEntities([&](std::shared_ptr<iCAX::Database::IEntity> _pEntity)->bool
        {
            return _pEntity->HasComponent<SceneComponent>();
        });

    if (!_vecEntities.empty())
    {
        _Component.m_pScene = _vecEntities.front()->GetComponent<SceneComponent>();
    }

    _Component.m_pTransform = Component_.GetEntity()->GetComponent<TransformComponent>();
}

//! 修改Enable状态时触发
void iCAX::Render::LightBehaviour::OnEnable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 每一帧预触发
void iCAX::Render::LightBehaviour::OnPreUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
}

//! 每一帧触发
void iCAX::Render::LightBehaviour::OnUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<LightComponent&>(Component_);
    if (auto _pSecne = _Component.m_pScene.lock())
    {
        if (auto _pTranform = _Component.m_pTransform.lock())
        {
            auto _pRenderService = iCAX::Services::GetGlobalServiceProvider()->Resolve<IRenderService>();
            _pRenderService->SetLight(_pSecne->GetSceneID(), Component_.GetEntity()->GetID(), _Component, *_pTranform, _Component.Version() + _pTranform->Version());//! 因为每个组件的版本都是只会增加不会减少的，所以此处可以用和来表征
        }
    }
}

//! 每一帧后触发
void iCAX::Render::LightBehaviour::OnPostUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
}

//! 禁用时触发
void iCAX::Render::LightBehaviour::OnDisable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 销毁时触发
void iCAX::Render::LightBehaviour::OnDestroy(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<LightComponent&>(Component_);
    if (auto _pSecne = _Component.m_pScene.lock())
    {
        auto _pRenderService = iCAX::Services::GetGlobalServiceProvider()->Resolve<IRenderService>();
        _pRenderService->RemoveLight(_pSecne->GetSceneID(), Component_.GetEntity()->GetID());
    }
}

//! 组件数据修改前触发
void iCAX::Render::LightBehaviour::OnModifing(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
}

//! 组件数据修改后触发
void iCAX::Render::LightBehaviour::OnModified(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
}


