#pragma once

#include "ComponentValueHelpers.h"

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief 机床定义目录组件。
        * @details
        *   该组件挂在 Repository 的 MetaEntity 上，只维护本项目包含的机床定义 Entity 列表。
        *   目录不表达当前激活机床；加工大区或作业模型负责选择具体使用哪台机床。
        *   MachineDefinitionIDs 的元素为字符串 UUID，避免资源清单、项目文件和前端协议处理二进制 uuid。
        */
        class CMachineDefinitionCatalogComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CMachineDefinitionCatalogComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CMachineDefinitionCatalogComponent)

            DECLARED_ICAX_FIELD(CMachineDefinitionCatalogComponent, iCAX::Data::VariantArray, MachineDefinitionIDs, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
        };

        /*
        * @brief 机床定义索引组件。
        * @details
        *   该组件挂在独立 MachineDefinition Entity 上，保存外部可见的定义信息和机床描述资源句柄。
        *   完整机械结构数据仍然保存在 Scene.Resources() 的 CMachineDescriptionResource 中。
        */
        class CMachineDefinitionComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CMachineDefinitionComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CMachineDefinitionComponent)

            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, std::string, SourcePath, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, std::string, MachineResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, std::string, DescriptionFormat, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, std::string, DescriptionVersion, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, std::string, ModelName, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, bool, StaticModel, false, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, unsigned long long, LinkCount, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, unsigned long long, JointCount, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, unsigned long long, VisualCount, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, unsigned long long, CollisionCount, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, unsigned long long, IncludeCount, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_FIELD(CMachineDefinitionComponent, bool, Enabled, true, BoolEqual, ToBoolVariant, FromBoolVariant)
        };
    }
}
