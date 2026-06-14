#pragma once

#include "NCComponent.h"
#include "../Database/Component.h"
#include "../Database/ComponentHelper.h"

namespace iCAX
{
    namespace GCode
    {
        /*
        * @brief 字符串注释
        */
        class _GCODECORE_EXP NcStringComment : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(NcStringComment);

            //!< 属性字典
            DECLARED_ICAX_FIELD(NcStringComment, std::vector<std::string>, Contents, {}, [](const std::vector<std::string>& lhs_, const std::vector<std::string>& rhs_) {return lhs_ == rhs_; },
                [](const std::vector<std::string>& _lhs)
                {
                    PropertyArray _Array;
                    for (const auto& _str : _lhs)
                    {
                        _Array.push_back(_str);
                    }
                    return _Array;
                },
                [](const PropertyValue& _lhs)
                {
                    std::vector<std::string> _vecContents;
                    PropertyArray _Array = _lhs.To<PropertyArray>();
                    for (const auto& _str : _Array)
                    {
                        _vecContents.push_back(_str.To<std::string>());
                    }
                    return _vecContents;
                });
        };
    }
}