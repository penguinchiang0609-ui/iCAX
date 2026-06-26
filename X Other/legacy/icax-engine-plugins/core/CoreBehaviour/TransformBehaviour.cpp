#include "pch.h"
#include "TransformBehaviour.h"
#include "../CoreComponent/HierarchyComponent.h"
#include "HierarchyBehaviour.h"
#include "../CoreComponent/TransformComponent.h"

namespace
{
    constexpr int kBehaviourActionSuccess = 0;
    constexpr int kBehaviourActionFailed = -1;
}

//! 构造函数
iCAX::Core::TransformBehaviour::TransformBehaviour()
{
}

//! 析构函数
iCAX::Core::TransformBehaviour::~TransformBehaviour()
{
}

//! 获取组件类型名称
std::string iCAX::Core::TransformBehaviour::GetComponentClass() const
{
    return TransformComponent::S_ClassName;
}

//! 创建之后触发
void iCAX::Core::TransformBehaviour::OnAwake(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
}

//! 创建之后下一帧触发
void iCAX::Core::TransformBehaviour::OnStart(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
    auto& _Component = dynamic_cast<TransformComponent&>(Component_);
    if (auto _pEntity = _Component.GetEntity())
    {
        if (auto _pHiearchy = _pEntity->GetComponent<HierarchyComponent>())
        {
            _Component.m_Hierarchy = _pHiearchy;
        }
    }
}

//! 修改Enable状态时触发
void iCAX::Core::TransformBehaviour::OnEnable(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
}

//! 每一帧预触发
void iCAX::Core::TransformBehaviour::OnPreUpdate(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_)
{
}

//! 每一帧触发
void iCAX::Core::TransformBehaviour::OnUpdate(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_)
{
}

//! 每一帧后触发
void iCAX::Core::TransformBehaviour::OnPostUpdate(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_)
{
}

//! 禁用时触发
void iCAX::Core::TransformBehaviour::OnDisable(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
}

//! 销毁时触发
void iCAX::Core::TransformBehaviour::OnDestroy(
    IN const iCAX::Behaviour::CComponentDestroyInfo& DestroyInfo_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
}

//! 组件数据修改前触发
void iCAX::Core::TransformBehaviour::OnModifying(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Data::PropertySet& NewValues_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
}

//! 组件数据修改后触发
void iCAX::Core::TransformBehaviour::OnModified(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Data::PropertySet& NewValues_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
    auto& _Component = dynamic_cast<TransformComponent&>(Component_);
    if (NewValues_.contains(TransformComponent::PropertyName_Translation) || NewValues_.contains(TransformComponent::PropertyName_Rotation))
    {
        _Component.m_Local2WorldMatrix = iCAX::Math::Tranform3::Translate(_Component.GetTranslation().XYZ()) * _Component.GetRotation().ToTrsf3();
    }
}

//! 变换
int iCAX::Core::Tranform(IN OUT void* pContext_, IN const void* IParam_, OUT void* OParam_)
{
    iCAX::Database::CComponentBase* _pComponent = reinterpret_cast<iCAX::Database::CComponentBase*>(pContext_);
    auto _pTransform = reinterpret_cast<const iCAX::Math::Tranform3*>(IParam_);
    if (_pComponent == nullptr || _pTransform == nullptr)
    {
        return kBehaviourActionFailed;
    }

    auto& _Component = dynamic_cast<iCAX::Core::TransformComponent&>(*_pComponent);
    auto _Tuple = (*_pTransform * _Component.m_Local2WorldMatrix).Decompose();
    if (!_Component.SetTranslation(std::get<0>(_Tuple)))
    {
        return kBehaviourActionFailed;
    }
    if (!_Component.SetRotation(std::get<1>(_Tuple)))
    {
        return kBehaviourActionFailed;
    }

    return kBehaviourActionSuccess;
}

