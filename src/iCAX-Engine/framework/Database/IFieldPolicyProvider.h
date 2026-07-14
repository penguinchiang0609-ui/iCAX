#pragma once

#include "Database.h"
#include "Data/Variant.h"

#include <string>

namespace iCAX
{
    namespace Database
    {
        class CComponentBase;
        class IEntity;

        /*
        * @brief 字段实例编辑策略
        * @details
        *   Attribute 描述字段类型层面的静态信息；SFieldEditPolicy 描述某个实体实例上的某个字段当前如何编辑。
        *   UI 只理解这个通用结构，不需要知道具体产品、组件或运动副类型。
        */
        struct _DATABASE_EXP SFieldEditPolicy final
        {
            bool bVisible = true;                     //!< 是否在通用属性面板中显示。
            bool bEditable = true;                    //!< 当前实例上该字段是否允许编辑。
            bool bHasRange = false;                   //!< 是否提供数值范围。
            double dMin = 0.0;                        //!< 最小值，bHasRange 为 true 时有效。
            double dMax = 0.0;                        //!< 最大值，bHasRange 为 true 时有效。
            double dStep = 1.0;                       //!< UI 推荐步长。
            int nPrecision = -1;                      //!< UI 推荐显示小数位；不改变字段本身的数值类型或计算精度。
            std::string strUnit;                      //!< 单位，如 mm、deg。
            std::string strReason;                    //!< 不可编辑或受限的原因。
            std::string strControlKind = "number";    //!< 控件类型，如 number、text、enum、checkbox。
            iCAX::Data::VariantArray Options;         //!< 枚举选项或其他通用候选值。
            iCAX::Data::ObjectMap Metadata;           //!< 额外通用元数据，避免为少量 UI 表达继续扩接口。
        };

        /*
        * @brief 字段实例编辑策略提供者
        * @details
        *   Provider 的注册按组件类型组织，但查询时会传入 Entity、Component 和 PropertyName。
        *   因此同一字段在不同实体实例上可以返回不同的编辑策略。
        */
        class _DATABASE_EXP IFieldPolicyProvider
        {
        public:
            IFieldPolicyProvider() = default;
            virtual ~IFieldPolicyProvider() = default;

            /*
            * @brief 尝试返回实例字段编辑策略。
            * @param [in] Entity_ 字段所属实体。
            * @param [in] Component_ 字段所属组件。
            * @param [in] strPropertyName_ 字段名称。
            * @param [out] Policy_ 输出的通用编辑策略。
            * @return true 表示该 Provider 已处理该字段；false 表示继续交给后续 Provider 或默认策略。
            */
            virtual bool TryGetFieldPolicy(
                IN const IEntity& Entity_,
                IN const CComponentBase& Component_,
                IN const std::string& strPropertyName_,
                OUT SFieldEditPolicy& Policy_) const = 0;
        };
    }
}
