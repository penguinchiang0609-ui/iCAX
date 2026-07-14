#pragma once

#include "ComponentValueHelpers.h"

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief CAM 当前拓扑选择组件。
        * @details
        *   选择态是运行时 UI/交互数据，保存在 Database 中便于行为、命令和服务统一读取；
        *   字段是 NonPersistent + Observable，不进入项目文件、快速保存日志和撤销还原。
        */
        class CSelectionComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CSelectionComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CSelectionComponent)

            DECLARED_ICAX_OBSERVABLE_FIELD(CSelectionComponent, std::string, HoverKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CSelectionComponent, unsigned long long, HoverID, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CSelectionComponent, iCAX::Data::uuid, HoverEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CSelectionComponent, std::string, SelectedKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CSelectionComponent, unsigned long long, SelectedID, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CSelectionComponent, iCAX::Data::uuid, SelectedEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CSelectionComponent, std::string, SelectedLabel, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        };
    }
}
