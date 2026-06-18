#pragma once
#include "Core.h"
#include "Data/uuid.h"
#include "Database/ComponentBase.h"
#include "Database/ComponentHelper.h"
#include "Database/IEntity.h"
#include "Data/PropertyBag.h"

namespace iCAX
{
    namespace Core
    {
        /**
        * @brief 项目配置组件
        * @details
        *   绑定到仓储本体实体上
        *   全局都可以访问到
        */
        class _CORECOMPONENT_EXP ProjectSettingComponent final : public iCAX::Database::CComponentBase
        {
            //!< 构造函数以及重载函数声明
            //!< 构造函数以及重载函数声明
            DECLARE_ICAX_COMPONENT(ProjectSettingComponent, CComponentBase);
            DECLARE_ICAX_COMPONENT_CREATOR(ProjectSettingComponent);
            DECLARED_ICAX_FIELD(ProjectSettingComponent, iCAX::Data::PropertyBag, Setting, iCAX::Data::PropertyBag(), [](const iCAX::Data::PropertyBag& lhs_, const iCAX::Data::PropertyBag& rhs_) {return lhs_.GetProperties() == rhs_.GetProperties(); },
                [](const iCAX::Data::PropertyBag& _lhs) {return _lhs.GetProperties(); }, [](const iCAX::Data::PropertyValue& _lhs) {return _lhs.To<iCAX::Data::PropertySet>(); });

        };
    }
}
