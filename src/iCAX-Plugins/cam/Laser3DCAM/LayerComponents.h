#pragma once

#include "ComponentValueHelpers.h"

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief CAM 切割图层组件。
        * @details
        *   CuttingLayer 面向切割系统和加工工艺分组，不负责 UI 显隐和颜色。
        *   一条 Path 通过 CuttingLayerID 归属到一个 CuttingLayer，导出或下发时可按该层选择切割系统工艺。
        */
        class CCuttingLayerComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CCuttingLayerComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CCuttingLayerComponent)

            DECLARED_ICAX_FIELD(CCuttingLayerComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCuttingLayerComponent, std::string, CuttingProcessID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCuttingLayerComponent, std::string, CuttingProcessName, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCuttingLayerComponent, bool, Enabled, true, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_FIELD(CCuttingLayerComponent, unsigned long long, OutputOrder, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        };

        /*
        * @brief CAM 显示图层组件。
        * @details
        *   VisibleLayer 面向前端显示、隐藏、颜色和选择过滤，不表达切割系统工艺。
        *   一条 Path 通过 VisibleLayerID 归属到一个 VisibleLayer。
        */
        class CVisibleLayerComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CVisibleLayerComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CVisibleLayerComponent)

            DECLARED_ICAX_FIELD(CVisibleLayerComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CVisibleLayerComponent, std::string, Color, std::string("#2F80ED"), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CVisibleLayerComponent, bool, Visible, true, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_FIELD(CVisibleLayerComponent, bool, Locked, false, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_FIELD(CVisibleLayerComponent, unsigned long long, Order, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        };
    }
}
