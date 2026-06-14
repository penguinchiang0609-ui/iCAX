#pragma once

#include "NCComponent.h"
#include "../Database/Component.h"
#include "../Database/ComponentHelper.h"
#include "../../01 Foundation/Data/uuid.h"

namespace iCAX
{
    namespace GCode
    {
        /*
        * @brief 后指令组件
        */
        class _GCODECORE_EXP NcPostAuxComponent : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(NcPostAuxComponent);

            //!< 辅助指令列表
            DECLARED_ICAX_FIELD(NcPostAuxComponent, std::vector<iCAX::Data::uuid>, Auxes, std::vector<iCAX::Data::uuid>(), [](const std::vector<iCAX::Data::uuid>& lhs_, const std::vector<iCAX::Data::uuid>& rhs_) {return lhs_ == rhs_; },
                [](const std::vector<iCAX::Data::uuid>& _lhs)
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
                    std::vector<iCAX::Data::uuid> _vecContents;
                    PropertyArray _Array = _lhs.To<PropertyArray>();
                    for (const auto& _str : _Array)
                    {
                        _vecContents.push_back(_str.To<iCAX::Data::uuid>());
                    }
                    return _vecContents;
                });
        };
    }
}