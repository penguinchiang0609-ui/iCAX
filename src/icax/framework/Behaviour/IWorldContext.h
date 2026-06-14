#pragma once
#include "System.h"
#include <unordered_map>
#include <string>

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 世界上下文
        */
        class _SYSTEM_EXP IWorldContext
        {
        public:
            /*
            * @brief 构造函数
            */
            IWorldContext() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IWorldContext() = default;


        public:
            /*
            * @brief 设置数据
            * @param [in] nKey_ 项ID
            * @param [in] pData_
            */
            virtual void SetData(IN const uint32_t& nKey_, IN uint8_t* pData_) = 0;

            /*
            * @brief 获取数据
            * @param [in] nKey_
            * @return uint8_t*
            */
            virtual uint8_t* GetData(IN const uint32_t& nKey_) = 0;

            /*
            * @brief 移除
            * @param [in] nKey_
            */
            virtual void Remove(IN const uint32_t& nKey_) = 0;
        };
    }
}