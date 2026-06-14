#pragma once

#include "NCComponent.h"
#include "../../../../../02 Framework/Database/Component.h"
#include "../../../../../02 Framework/Database/ComponentHelper.h"

using namespace iCAX::Database;

using namespace iCAX::Data;

namespace iCAX
{
    namespace GCode
    {
        /*
        * @brief 指令
        * @remark
        *   1、绑定在轨迹前/后
        *   2、M 指令
        *   3、主轴开/关：M03/M05 → 可附加到运动指令的前/后
        *   4、冷却液开/关：M08/M09 → 前/后动作
        *   5、换刀：M06 → 前动作（运动指令开始前换刀）
        */
        class _GCODECORE_EXP NcCommandComponent : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(NcCommandComponent);

            //!< 命令名称
            DECLARED_ICAX_FIELD(NcCommandComponent, std::string, CmdName, "", [](const std::string& lhs_, const std::string& rhs_) {return lhs_ == rhs_; },
                [](const std::string& _lhs) {return _lhs; }, [](const PropertyValue& _lhs) {return _lhs.To<std::string>(); });
            //!< 命令值
            DECLARED_ICAX_FIELD(NcCommandComponent, PropertyValue, CmdParams, {}, [](const PropertyValue& lhs_, const PropertyValue& rhs_) {return lhs_ == rhs_; },
                [](const PropertyValue& _lhs) {return _lhs; }, [](const PropertyValue& _lhs) {return _lhs; });
        };
    }
}