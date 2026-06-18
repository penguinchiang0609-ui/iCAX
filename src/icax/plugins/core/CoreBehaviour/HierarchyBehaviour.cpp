#include "pch.h"
#include "HierarchyBehaviour.h"

namespace
{
    constexpr int kBehaviourActionSuccess = 0;
    constexpr int kBehaviourActionFailed = -1;
}

//! 构造函数
iCAX::Core::HierarchyBehaviour::HierarchyBehaviour()
{
}

//! 析构函数
iCAX::Core::HierarchyBehaviour::~HierarchyBehaviour()
{
}

//! 获取组件类型名称
std::string iCAX::Core::HierarchyBehaviour::GetComponentClass() const
{
    return HierarchyComponent::S_ClassName;
}

//! 创建之后触发
void iCAX::Core::HierarchyBehaviour::OnAwake(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 创建之后下一帧触发
void iCAX::Core::HierarchyBehaviour::OnStart(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<HierarchyComponent&>(Component_);
    auto& _Repository = Context_.GetDatabase();
    {
        //! 缓存父组件
        if (auto _pEntity = _Repository.GetEntity(_Component.GetParentID()))
        {
            if (auto _pComponent = _pEntity->GetComponent<HierarchyComponent>())
            {
                _Component.m_ParentComponent = _pComponent;
            }
        }
        //! 缓存子组件集
        _Component.m_ChildrenComponents.clear();
        for (auto _ChildID : _Component.GetChildrenIDs())
        {
            if (auto _pEntity = _Repository.GetEntity(_ChildID))
            {
                if (auto _pComponent = _pEntity->GetComponent<HierarchyComponent>())
                {
                    _Component.m_ChildrenComponents.emplace(_pComponent);
                }
            }
        }
    }
}

//! 修改Enable状态时触发
void iCAX::Core::HierarchyBehaviour::OnEnable(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 每一帧预触发
void iCAX::Core::HierarchyBehaviour::OnPreUpdate(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
}

//! 每一帧触发
void iCAX::Core::HierarchyBehaviour::OnUpdate(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
}

//! 每一帧后触发
void iCAX::Core::HierarchyBehaviour::OnPostUpdate(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
}

//! 禁用时触发
void iCAX::Core::HierarchyBehaviour::OnDisable(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
}

//! 销毁时触发
void iCAX::Core::HierarchyBehaviour::OnDestroy(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<HierarchyComponent&>(Component_);
    auto& _Repository = Context_.GetDatabase();
    for (auto& _pChild : _Component.m_ChildrenComponents)
    {
        if (auto _pComponent = _pChild.lock())
        {
            if (auto _pEntity = _pComponent->GetEntity())
            {
                _Repository.DeleteEntity(_pEntity->GetID());
            }
        }
    }
}

//! 组件数据修改前触发
void iCAX::Core::HierarchyBehaviour::OnModifing(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
}

//! 组件数据修改后触发
void iCAX::Core::HierarchyBehaviour::OnModified(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
    auto& _Component = dynamic_cast<HierarchyComponent&>(Component_);
    auto& _Repository = Context_.GetDatabase();
    {
        //! 修改了父组件ID，则重新缓存父组件
        if (NewValues_.contains(HierarchyComponent::PropertyName_ParentID))
        {
            if (auto _pEntity = _Repository.GetEntity(_Component.GetParentID()))
            {
                if (auto _pComponent = _pEntity->GetComponent<HierarchyComponent>())
                {
                    _Component.m_ParentComponent = _pComponent;
                }
            }
        }
        //! 修改了子组件ID集，则重新更新子组件集
        if (NewValues_.contains(HierarchyComponent::PropertyName_ChildrenIDs))
        {
            _Component.m_ChildrenComponents.clear();
            for (auto _ChildID : _Component.GetChildrenIDs())
            {
                if (auto _pEntity = _Repository.GetEntity(_ChildID))
                {
                    if (auto _pComponent = _pEntity->GetComponent<HierarchyComponent>())
                    {
                        _Component.m_ChildrenComponents.emplace(_pComponent);
                    }
                }
            }
        }
    }
}

//! 增加子节点
int iCAX::Core::AddHierarchyChild(IN OUT void* pContext_, IN const void* IParam_, OUT void* OParam_)
{
    iCAX::Database::CComponentBase* _pComponent = reinterpret_cast<iCAX::Database::CComponentBase*>(pContext_);
    auto _ppEntity = reinterpret_cast<const std::shared_ptr<iCAX::Database::IEntity>*>(IParam_);
    if (_pComponent == nullptr || _ppEntity == nullptr)
    {
        return kBehaviourActionFailed;
    }

    std::shared_ptr<iCAX::Database::IEntity> _pChildEntity = *_ppEntity;
    auto& _Component = dynamic_cast<HierarchyComponent&>(*_pComponent);
    if (_pChildEntity)
    {
        if (auto _pEntity = _Component.GetEntity())
        {
            auto _pChild = _pChildEntity->HasComponent<HierarchyComponent>() ? _pChildEntity->GetComponent<HierarchyComponent>() : _pChildEntity->AddComponent<HierarchyComponent>();
            _pChild->SetParentID(_pEntity->GetID());
            auto _vecIDs = _Component.GetChildrenIDs();
            _vecIDs.emplace(_pChildEntity->GetID());
            _Component.SetChildrenIDs(_vecIDs);
        }
    }
    return kBehaviourActionSuccess;
}

//! 移除子节点
int iCAX::Core::RemoveHierarchyChild(IN OUT void * pContext_, IN const void * IParam_, OUT void * OParam_)
{
    iCAX::Database::CComponentBase* _pComponent = reinterpret_cast<iCAX::Database::CComponentBase*>(pContext_);
    auto _ppEntity = reinterpret_cast<const std::shared_ptr<iCAX::Database::IEntity>*>(IParam_);
    if (_pComponent == nullptr || _ppEntity == nullptr)
    {
        return kBehaviourActionFailed;
    }

    std::shared_ptr<iCAX::Database::IEntity> _pChildEntity = *_ppEntity;
    auto& _Component = dynamic_cast<HierarchyComponent&>(*_pComponent);

    if (_pChildEntity)
    {
        if (auto _pEntity = _Component.GetEntity())
        {
            auto _pChild = _pChildEntity->HasComponent<HierarchyComponent>() ? _pChildEntity->GetComponent<HierarchyComponent>() : _pChildEntity->AddComponent<HierarchyComponent>();
            _pChild->SetParentID({});
            auto _vecIDs = _Component.GetChildrenIDs();
            for (auto _Ite = _vecIDs.begin(); _Ite != _vecIDs.end(); _Ite++)
            {
                if (*_Ite == _pChildEntity->GetID())
                {
                    _vecIDs.erase(_Ite);
                    break;
                }
            }
            _Component.SetChildrenIDs(_vecIDs);
        }
    }

    return kBehaviourActionSuccess;
}

