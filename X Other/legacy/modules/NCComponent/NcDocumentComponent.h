#pragma once

#include "NCComponent.h"
#include "../../../../../02 Framework/Database/Component.h"
#include "../../../../../02 Framework/Database/ComponentHelper.h"


namespace iCAX
{
    namespace GCode
    {
        /*
        * @brief NC文档组件
        */
        class _GCODECORE_EXP NcDocumentComponent : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(NcDocumentComponent);
        };
    }
}