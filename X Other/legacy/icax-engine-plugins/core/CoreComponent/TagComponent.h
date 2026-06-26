#pragma once
#include "Core.h"
#include "Database/ComponentBase.h"
#include "Database/ComponentHelper.h"

namespace iCAX
{
    namespace Core
    {
        /**
        * @brief 标记组件
        */
        class _CORECOMPONENT_EXP TagComponent final : public iCAX::Database::CComponentBase
        {
            //!< 构造函数以及重载函数声明
            //!< 构造函数以及重载函数声明
            DECLARE_ICAX_COMPONENT(TagComponent, CComponentBase);
            DECLARE_ICAX_COMPONENT_CREATOR(TagComponent);

            //!< 标记
            DECLARED_ICAX_FIELD(TagComponent, std::string, Tag, "",
                [](const std::string& lhs, const std::string& rhs) { return lhs == rhs; },
                [](const std::string& value) { return value; },
                [](const iCAX::Data::PropertyValue& value) { return value.To<std::string>(); });

            //!< 名称
            DECLARED_ICAX_FIELD(TagComponent, std::string, Name, "",
                [](const std::string& lhs, const std::string& rhs) { return lhs == rhs; },
                [](const std::string& value) { return value; },
                [](const iCAX::Data::PropertyValue& value) { return value.To<std::string>(); });

            //!< 类别
            DECLARED_ICAX_FIELD(TagComponent, std::string, Catalog, "",
                [](const std::string& lhs, const std::string& rhs) { return lhs == rhs; },
                [](const std::string& value) { return value; },
                [](const iCAX::Data::PropertyValue& value) { return value.To<std::string>(); });
        };
    }
}
