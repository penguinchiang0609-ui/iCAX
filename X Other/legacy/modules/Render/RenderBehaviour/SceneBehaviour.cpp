#include "pch.h"
#include "SceneBehaviour.h"
#include "Services/ServiceProvider.h"
#include "../RenderService/IRenderService.h"
#include "Behaviour/IWorld.h"
#include "Behaviour/IUniverse.h"

//! 构造函数
iCAX::Render::CSceneBehaviour::CSceneBehaviour()
    : CBehaviourBase()
{
}

//! 析构函数
iCAX::Render::CSceneBehaviour::~CSceneBehaviour()
{
}

//! 获取组件类型名称
std::string iCAX::Render::CSceneBehaviour::GetComponentClass() const
{
    return SceneComponent::S_ClassName;
}

//! 创建之后触发
//! 变量初始化
void iCAX::Render::CSceneBehaviour::OnAwake(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 创建之后下一帧触发
//! 在此处根据SceneComponent的信息去RenderService构造渲染场景
void iCAX::Render::CSceneBehaviour::OnStart(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<SceneComponent&>(Component_);
    auto _pRenderService = iCAX::Services::GetGlobalServiceProvider()->Resolve<IRenderService>();
    _pRenderService->CreateScene(_Component.GetSceneID());
    _pRenderService->SetCanva(_Component.GetSceneID(), _Component, _Component.Version());
}

//! 修改Enable状态时触发
void iCAX::Render::CSceneBehaviour::OnEnable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 每一帧预触发
void iCAX::Render::CSceneBehaviour::OnPreUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
}

//! 每一帧触发
void iCAX::Render::CSceneBehaviour::OnUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<SceneComponent&>(Component_);
    auto _pRenderService = iCAX::Services::GetGlobalServiceProvider()->Resolve<IRenderService>();
    _pRenderService->SetCanva(_Component.GetSceneID(), _Component, _Component.Version());
}

//! 每一帧后触发
void iCAX::Render::CSceneBehaviour::OnPostUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<SceneComponent&>(Component_);
    auto _pRenderService = iCAX::Services::GetGlobalServiceProvider()->Resolve<IRenderService>();
    _pRenderService->RenderScene(_Component.GetSceneID());
}

//! 禁用时触发
void iCAX::Render::CSceneBehaviour::OnDisable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 销毁时触发
void iCAX::Render::CSceneBehaviour::OnDestroy(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<SceneComponent&>(Component_);
    auto _pRenderService = iCAX::Services::GetGlobalServiceProvider()->Resolve<IRenderService>();
    _pRenderService->DestoryScene(_Component.GetSceneID());
}

//! 组件数据修改前触发
void iCAX::Render::CSceneBehaviour::OnModifing(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
}

//! 组件数据修改后触发
void iCAX::Render::CSceneBehaviour::OnModified(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
}
