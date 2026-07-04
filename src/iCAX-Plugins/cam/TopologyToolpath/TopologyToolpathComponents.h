#pragma once

#include "Database/ComponentHelper.h"

#include <string>

namespace iCAX
{
    namespace CAM
    {
        using iCAX::Database::CComponentBase;

        inline iCAX::Data::PropertyValue ToStringVariant(IN const std::string& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline std::string FromStringVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<std::string>();
        }

        inline iCAX::Data::PropertyValue ToUInt64Variant(IN const unsigned long long& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline unsigned long long FromUInt64Variant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<unsigned long long>();
        }

        inline bool StringEqual(IN const std::string& Lhs_, IN const std::string& Rhs_)
        {
            return Lhs_ == Rhs_;
        }

        inline bool UInt64Equal(IN const unsigned long long& Lhs_, IN const unsigned long long& Rhs_)
        {
            return Lhs_ == Rhs_;
        }

        /*
        * @brief CAM 导入模型引用组件。
        * @details
        *   该组件是项目级模型状态，挂在 Repository 的 meta entity 上。
        *   Database 只保存模型来源和资源 key，不直接保存 OCC/BRep/Mesh/拓扑索引等大对象。
        *   模型对象、拓扑索引、显示网格等运行期或大体量数据应进入 Project.Resources。
        */
        class CCAMModelComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CCAMModelComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CCAMModelComponent)

            DECLARED_ICAX_FIELD(CCAMModelComponent, std::string, SourcePath, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCAMModelComponent, std::string, ModelResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCAMModelComponent, std::string, TopologyResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCAMModelComponent, std::string, DisplayResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCAMModelComponent, unsigned long long, TopologyVersion, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        };

        /*
        * @brief CAM 当前拓扑选择组件。
        * @details
        *   选择态是运行时 UI/交互数据，保存在 Database 中便于行为、命令和服务统一读取；
        *   字段是 NonPersistent + Observable，不进入项目文件、快速保存日志和撤销还原。
        */
        class CCAMSelectionComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CCAMSelectionComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CCAMSelectionComponent)

            DECLARED_ICAX_OBSERVABLE_FIELD(CCAMSelectionComponent, std::string, HoverKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCAMSelectionComponent, unsigned long long, HoverID, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCAMSelectionComponent, std::string, SelectedKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCAMSelectionComponent, unsigned long long, SelectedID, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCAMSelectionComponent, std::string, SelectedLabel, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        };

        /*
        * @brief CAM 刀路列表组件。
        * @details
        *   当前阶段只表达“已确认的拓扑目标列表”，还不是完整工艺刀路实体。
        *   TargetsText 使用 VariantSerializer 文本保存一个目标数组，命令层负责封装读写语义；
        *   上层不应直接理解该文本格式。后续 Database 若支持强类型数组字段，可平滑替换该字段。
        */
        class CCAMToolpathListComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CCAMToolpathListComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CCAMToolpathListComponent)

            DECLARED_ICAX_FIELD(CCAMToolpathListComponent, std::string, TargetsText, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        };
    }
}
