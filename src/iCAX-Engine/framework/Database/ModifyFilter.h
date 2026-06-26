#pragma once

#include "Database.h"
#include "Data/Variant.h"

#include <memory>
#include <string>

namespace iCAX
{
    namespace Database
    {
        class CComponentBase;
        class IEntity;

        /*
        * @brief Repository 写入过滤器
        * @details
        *   ModifyFilter 是正式写入管线的前置准入点。它不依赖 Changing 事件，
        *   也不修改数据，只在字段、组件或实体真正落地前调用 meta 中注册的 checker。
        */
        class _DATABASE_EXP CModifyFilter final
        {
        public:
            CModifyFilter() = delete;

            /*
            * @brief 判断是否允许向实体附加组件。
            * @param [in] Entity_ 目标实体。
            * @param [in] strComponentClass_ 待附加组件类型。
            * @param [out] strError_ 失败原因；成功时会被清空。
            * @return true 表示允许附加。
            */
            static bool AllowAttachComponent(IN const IEntity& Entity_, IN const std::string& strComponentClass_, OUT std::string& strError_);

            /*
            * @brief 判断是否允许从实体移除组件。
            * @param [in] Entity_ 目标实体。
            * @param [in] strComponentClass_ 待移除组件类型。
            * @param [out] strError_ 失败原因；成功时会被清空。
            * @return true 表示允许移除。
            */
            static bool AllowRemoveComponent(IN const IEntity& Entity_, IN const std::string& strComponentClass_, OUT std::string& strError_);

            /*
            * @brief 判断是否允许修改组件字段。
            * @param [in] Component_ 目标组件，仍处于修改前状态。
            * @param [in] Properties_ 本次操作准备写入的字段集合。
            * @param [out] strError_ 失败原因；成功时会被清空。
            * @return true 表示允许修改。
            */
            static bool AllowModifyComponent(IN const CComponentBase& Component_, IN const iCAX::Data::PropertySet& Properties_, OUT std::string& strError_);
        };
    }
}
