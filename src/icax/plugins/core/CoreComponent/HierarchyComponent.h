#pragma once
#include "Core.h"
#include "Data/uuid.h"
#include "Database/ComponentBase.h"
#include "Database/ComponentHelper.h"
#include "Database/IDomain.h"
#include "Database/IEntity.h"
#include "Data/CommonFunction.h"



namespace iCAX
{
    namespace Core
    {
        /**
        * @brief 层次组件
        * @details
        *   1、域中各个实体之间的父子关系
        *   2、域中所有实体都必须有该组件
        *   3、实体在创建后第一时间需要赋父子关系
        */
        class _CORECOMPONENT_EXP HierarchyComponent final : public iCAX::Database::CComponentBase
        {
            //!< 构造函数以及重载函数声明
            DECLARE_ICAX_COMPONENT(HierarchyComponent, CComponentBase);
            DECLARE_ICAX_COMPONENT_CREATOR(HierarchyComponent);
            //!< 父ID
            DECLARED_ICAX_FIELD(HierarchyComponent, iCAX::Data::uuid, ParentID, iCAX::Data::uuid(), [](const iCAX::Data::uuid& lhs_, const iCAX::Data::uuid& rhs_) {return lhs_ == rhs_; },
                [](const iCAX::Data::uuid& _lhs) {return _lhs; }, [](const iCAX::Data::PropertyValue& _lhs) {return _lhs.To<iCAX::Data::uuid>(); });

            //!< 子物体ID
            DECLARED_ICAX_FIELD(HierarchyComponent, std::unordered_set<iCAX::Data::uuid>, ChildrenIDs, std::unordered_set<iCAX::Data::uuid>(), [](const std::unordered_set<iCAX::Data::uuid>& lhs_, const std::unordered_set<iCAX::Data::uuid>& rhs_) {return lhs_ == rhs_; },
                [](const std::unordered_set<iCAX::Data::uuid>& _lhs)
                {
                    iCAX::Data::PropertyArray _Array;
                    for (const auto& _ID : _lhs)
                    {
                        _Array.push_back(_ID);
                    }
                    return _Array;
                }, 
                [](const iCAX::Data::PropertyValue& _lhs)
                {
                    std::unordered_set<iCAX::Data::uuid> _vecIDs;
                    iCAX::Data::PropertyArray _Array = _lhs.To<PropertyArray>();
                    for (const auto& _ID : _Array)
                    {
                        _vecIDs.emplace(_ID.To<uuid>());
                    }
                    return _vecIDs;
                });

        public://!! 以下成员修改不触发组件版本更新
            //! 父组件
            DECLARED_ICAX_RUNTIME_FIELD(std::weak_ptr<HierarchyComponent>, ParentComponent, {});
            //! 子组件列表
            using ChildrenSet = std::unordered_set<std::weak_ptr<HierarchyComponent>, WeakPtrHash<HierarchyComponent>, WeakPtrEqual<HierarchyComponent>>;
            DECLARED_ICAX_RUNTIME_FIELD(ChildrenSet, ChildrenComponents, {});
        };

    }
}
