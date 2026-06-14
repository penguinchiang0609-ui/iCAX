#pragma once

#include "NCComponent.h"
#include "../Database/Component.h"
#include "../Database/ComponentHelper.h"

namespace iCAX
{
    namespace GCode
    {
        enum NcMotionType
        {
            kNcRapid = 0,                 //!< 快速移动，即空移动
            kNcCut = 1,                   //!< 进给/切割
        };

        /*
        * @brief 路径组件
        */
        class _GCODECORE_EXP NcPathComponent : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(NcPathComponent);

            //!< 动作类型
            DECLARED_ICAX_FIELD(NcPathComponent, NcMotionType, MotionType, kNcRapid, [](const NcMotionType& lhs_, const NcMotionType& rhs_) {return lhs_ == rhs_; },
                [](const NcMotionType& _lhs) {return (int)_lhs; }, [](const PropertyValue& _lhs) {return (NcMotionType)_lhs.To<int>(); });

        };
    }
}