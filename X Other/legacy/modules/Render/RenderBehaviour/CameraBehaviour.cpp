#include "pch.h"
#include "CameraBehaviour.h"
#include "Behaviour/IWorld.h"
#include "Behaviour/IUniverse.h"
#include "../RenderComponent/SceneComponent.h"
#include "../RenderService/IRenderService.h"
//! 构造函数
iCAX::Render::CCameraBehaviour::CCameraBehaviour()
    : CBehaviourBase()
{
}

//! 析构函数
iCAX::Render::CCameraBehaviour::~CCameraBehaviour()
{
}

//! 获取组件类型名称
std::string iCAX::Render::CCameraBehaviour::GetComponentClass() const
{
    return CameraComponent::S_ClassName;
}

//! 创建之后触发
void iCAX::Render::CCameraBehaviour::OnAwake(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 创建之后下一帧触发
void iCAX::Render::CCameraBehaviour::OnStart(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<CameraComponent&>(Component_);
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
void iCAX::Render::CCameraBehaviour::OnEnable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 每一帧预触发
void iCAX::Render::CCameraBehaviour::OnPreUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
}

//! 每一帧触发
void iCAX::Render::CCameraBehaviour::OnUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
    auto& _CameraComponent = dynamic_cast<CameraComponent&>(Component_);
    if (auto _pSecne = _CameraComponent.m_pScene.lock())
    {
        if (auto _pTranform = _CameraComponent.m_pTransform.lock())
        {
            auto _pRenderService = iCAX::Services::GetGlobalServiceProvider()->Resolve<IRenderService>();
            _pRenderService->SetMainCamera(_pSecne->GetSceneID(), _CameraComponent, *_pTranform, _CameraComponent.Version() + _pTranform->Version());//! 因为每个组件的版本都是只会增加不会减少的，所以此处可以用和来表征

        }
    }
}

//! 每一帧后触发
void iCAX::Render::CCameraBehaviour::OnPostUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
}

//! 禁用时触发
void iCAX::Render::CCameraBehaviour::OnDisable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 销毁时触发
void iCAX::Render::CCameraBehaviour::OnDestroy(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    auto& _CameraComponent = dynamic_cast<CameraComponent&>(Component_);
    if (auto _pSecne = _CameraComponent.m_pScene.lock())
    {
        auto _pRenderService = iCAX::Services::GetGlobalServiceProvider()->Resolve<IRenderService>();
        _pRenderService->RemoveMainCamera(_pSecne->GetSceneID());//! 因为每个组件的版本都是只会增加不会减少的，所以此处可以用和来表征
    }

}

//! 组件数据修改前触发
void iCAX::Render::CCameraBehaviour::OnModifing(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
}

//! 组件数据修改后触发
void iCAX::Render::CCameraBehaviour::OnModified(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
}
