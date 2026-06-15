#pragma once
#include "pch.h"
#include <string>
#include <vector>
#include <tuple>
#include "Data/uuid.h"
#include "Data/Variant.h"
#include "../Database/IRepositoryEvent.h"

namespace iCAX
{
    namespace Tracker
    {
        /*
        * @brief 事务
        * @remark 事务不提供对domain的增删操作
        */
        class ITransaction
        {
        public:
            /*
            * @brief 构造函数
            */
            ITransaction() = default;

            /*
            * @brief 析构函数
            */
            virtual ~ITransaction() = default;

            //!< 禁用
            ITransaction(IN const ITransaction&) = delete;
            ITransaction& operator=(IN const ITransaction&) = delete;
            ITransaction(IN const ITransaction&&) = delete;
            ITransaction& operator=(IN const ITransaction&&) = delete;

        public:
            /*
            * @brief 创建实体
            * @param [in] DomainID_
            * @param [in] EntityID_
            */
            virtual void CreateEntity(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_) = 0;

            /*
            * @brief 释放实体
            * @param [in] DomainID_
            * @param [in] EntityID_
            */
            virtual void DisposeEntity(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_) = 0;

            /*
            * @brief 附加组件
            * @param [in] DomainID_
            * @param [in] EntityID_ 实体
            * @param [in] ComponentType_ 组件类型
            * @param [in] Properties_ 组件值
            */
            virtual void AttachComponent(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const iCAX::Data::PropertySet& Properties_) = 0;

            /*
            * @brief 移除组件8
            * @param [in] DomainID_
            * @param [in] EntityID_ 实体ID
            * @param [in] strClassName_ 组件类型
            */
            virtual void DetachComponent(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_) = 0;

            /*
            * @brief 修改组件
            * @param [in] DomainID_
            * @param [in] EntityID_ 实体ID
            * @param [in] strClassName_ 组件类型
            * @param [in] Properties_ 组件值
            */
            virtual void ModifyComponent(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const iCAX::Data::PropertySet& Properties_) = 0;
        };
    }
}