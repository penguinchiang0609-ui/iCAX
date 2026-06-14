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
        * @brief 模态指令
        * @remark
        *   1、平面选择：G17/G18/G19
        *   2、坐标模式：G90/G91
        *   3、刀具补偿：G40/G41/G42
        *   4、工件坐标系：G54~G59
        */
        class _GCODECORE_EXP NcModalComponent : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(NcModalComponent);

            //!< 命令名称
            DECLARED_ICAX_FIELD(NcModalComponent, std::string, CmdName, "", [](const std::string& lhs_, const std::string& rhs_) {return lhs_ == rhs_; },
                [](const std::string& _lhs) {return _lhs; }, [](const PropertyValue& _lhs) {return _lhs.To<std::string>(); });
            //!< 命令值
            DECLARED_ICAX_FIELD(NcModalComponent, PropertyValue, CmdParams, {}, [](const PropertyValue& lhs_, const PropertyValue& rhs_) {return lhs_ == rhs_; },
                [](const PropertyValue& _lhs) {return _lhs; }, [](const PropertyValue& _lhs) {return _lhs; });
        };
    }
}