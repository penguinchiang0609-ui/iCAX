#include "pch.h"
#include "MeshFilterBehaviour.h"
#include "Behaviour/IWorld.h"
#include "Behaviour/IUniverse.h"
#include "../RenderComponent/SceneComponent.h"
#include "../RenderService/IRenderService.h"
//! 构造函数
iCAX::Render::CMeshFilterBehaviour::CMeshFilterBehaviour()
    : CBehaviourBase()
{
}

//! 析构函数
iCAX::Render::CMeshFilterBehaviour::~CMeshFilterBehaviour()
{
}

//! 获取组件类型名称
std::string iCAX::Render::CMeshFilterBehaviour::GetComponentClass() const
{
    return MeshFilterComponent::S_ClassName;
}

//! 创建之后触发
void iCAX::Render::CMeshFilterBehaviour::OnAwake(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 创建之后下一帧触发
void iCAX::Render::CMeshFilterBehaviour::OnStart(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<MeshFilterComponent&>(Component_);
    auto _vecEntities = World_.GetDomain()->FilterEntities([&](std::shared_ptr<iCAX::Database::IEntity> _pEntity)->bool
        {
            return _pEntity->HasComponent<SceneComponent>();
        });

    if (!_vecEntities.empty())
    {
        _Component.m_pScene = _vecEntities.front()->GetComponent<SceneComponent>();
    }

    _Component.m_pTransform = Component_.GetEntity()->GetComponent<TransformComponent>();
    _Component.m_pLayer = Component_.GetEntity()->GetComponent<LayerComponent>();
    _Component.m_pRender = Component_.GetEntity()->GetComponent<MeshRenderComponent>();
}

//! 修改Enable状态时触发
void iCAX::Render::CMeshFilterBehaviour::OnEnable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 每一帧预触发
void iCAX::Render::CMeshFilterBehaviour::OnPreUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<MeshFilterComponent&>(Component_);
    if (_Component.m_pScene.expired() || _Component.m_pTransform.expired() || _Component.m_pLayer.expired() || _Component.m_pRender.expired())
    {
        OnStart(World_, Component_, Context_);
    }
}

//! 每一帧触发
void iCAX::Render::CMeshFilterBehaviour::OnUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<MeshFilterComponent&>(Component_);
    if (auto _pSecne = _Component.m_pScene.lock())
    {
        if (auto _pTranform = _Component.m_pTransform.lock())
        {
            if (auto _pLayer = _Component.m_pLayer.lock())
            {
                if (auto _pMaterials = _Component.m_pRender.lock())
                {
                    auto _pRenderService = iCAX::Services::GetGlobalServiceProvider()->Resolve<IRenderService>();
                    _pRenderService->SetEntity(_pSecne->GetSceneID(), Component_.GetEntity()->GetID(), *_pTranform, *_pLayer, _Component, *_pMaterials, _pTranform->Version() + _pLayer->Version() + _Component.Version() + _pMaterials->Version());
                    return;
                }
            }
        }

        //! 走到这里，说明前面未能去更新渲染实体，必然是存在某个条件不满足，所以需要移除
        auto _pRenderService = iCAX::Services::GetGlobalServiceProvider()->Resolve<IRenderService>();
        _pRenderService->RemoveEntity(_pSecne->GetSceneID(), Component_.GetEntity()->GetID());
    }
}

//! 每一帧后触发
void iCAX::Render::CMeshFilterBehaviour::OnPostUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
}

//! 禁用时触发
void iCAX::Render::CMeshFilterBehaviour::OnDisable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 销毁时触发
void iCAX::Render::CMeshFilterBehaviour::OnDestroy(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<MeshFilterComponent&>(Component_);
    if (auto _pSecne = _Component.m_pScene.lock())
    {
        auto _pRenderService = iCAX::Services::GetGlobalServiceProvider()->Resolve<IRenderService>();
        _pRenderService->RemoveEntity(_pSecne->GetSceneID(), Component_.GetEntity()->GetID());
    }
}

//! 组件数据修改前触发
void iCAX::Render::CMeshFilterBehaviour::OnModifing(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
}

//! 组件数据修改后触发
void iCAX::Render::CMeshFilterBehaviour::OnModified(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
}
