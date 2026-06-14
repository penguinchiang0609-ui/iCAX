#pragma once
#include "Database.h"
#include "IEntity.h"
#include "ComponentBase.h"

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 组件合法性检查
        */
        class _DATABASE_EXP IChecker
        {
        public:
            /*
            * @brief 构造函数
            */
            IChecker() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IChecker() = default;

        public:
            /*
            * @brief 是否允许添加
            * @param [in] Entity_ 目标实体
            * @param [out] strError_ 错误信息
            * @return bool
            */
            template<typename T>
            bool AllowAttach(IN const IEntity& Entity_, OUT std::string& strError_)
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                return AllowAttachByName(Entity_, T::S_ClassName, strError_);
            }

            /*
            * @brief 是否允许添加
            * @param [in] Entity_ 目标实体
            * @param [in] strComponent_ 组件类型名称
            * @param [out] strError_ 错误信息
            * @return bool
            */
            virtual bool AllowAttachByName(IN const IEntity& Entity_, IN const std::string& strComponent_, OUT std::string& strError_) = 0;

            /*
            * @brief 是否允许添加
            * @param [in] Entity_ 目标实体
            * @param [out] strError_ 错误信息
            * @return bool
            */
            template<typename T>
            bool AllowRemove(IN const IEntity& Entity_, OUT std::string& strError_)
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                return AllowRemoveByName(Entity_, T::S_ClassName, strError_);
            }

            /*
            * @brief 是否允许移除组件
            * @param [in] Entity_ 目标实体
            * @param [in] strComponent_ 组件类型名称
            * @param [out] strError_ 错误信息
            * @return bool
            */
            virtual bool AllowRemoveByName(IN const IEntity& Entity_, IN const std::string& strComponent_, OUT std::string& strError_) = 0;

            /*
            * @brief 是否允许修改目标组件
            * @param [in] Component_
            * @param [in] Properties_ 待修改的属性名称与新值列表
            * @param [out] strError_ 错误信息
            * @return bool
            */
            virtual bool AllowModify(IN const CComponentBase& Component_, IN const PropertySet& Properties_, OUT std::string& strError_) = 0;
        };
    }
}