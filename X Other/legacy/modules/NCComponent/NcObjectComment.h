#pragma once

#include "NCComponent.h"
#include "../../../../../02 Framework/Database/Component.h"
#include "../../../../../02 Framework/Database/ComponentHelper.h"

namespace iCAX
{
    namespace GCode
    {
        /*
        * @brief 对象注释
        */
        class _GCODECORE_EXP NcObjectComment : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(NcObjectComment);

            //!< 排序索引
            DECLARED_ICAX_FIELD(NcObjectComment, int, SortIndex, 0, [](const int& lhs_, const int& rhs_) {return lhs_ == rhs_; },
                [](const int& _lhs) {return _lhs; }, [](const PropertyValue& _lhs) {return _lhs.To<int>(); });

            //!< 对象名称
            DECLARED_ICAX_FIELD(NcObjectComment, std::string, ObjectName, "", [](const std::string& lhs_, const std::string& rhs_) {return lhs_ == rhs_; },
                [](const std::string& _lhs) {return _lhs; }, [](const PropertyValue& _lhs) {return _lhs.To<std::string>(); });
            //!< 属性字典
            DECLARED_ICAX_FIELD(NcObjectComment, PropertySet, ObjectProperties, {}, [](const PropertySet& lhs_, const PropertySet& rhs_) {return lhs_ == rhs_; },
                [](const PropertySet& _lhs) {return _lhs; }, [](const PropertyValue& _lhs) {return _lhs.To<PropertySet>(); });
        };
    }
}