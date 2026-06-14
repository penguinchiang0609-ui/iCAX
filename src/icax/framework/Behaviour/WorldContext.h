#pragma once
#include "System.h"
#include "IWorldContext.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 世界上下文
        */
        class CWorldContext : public IWorldContext
        {
        public:
            /*
            * @brief 构造函数
            */
            CWorldContext();

            /*
            * @brief 析构函数
            */
            virtual ~CWorldContext();


        public:
            /*
            * @brief 设置数据
            * @param [in] nKey_ 项ID
            * @param [in] pData_
            */
            virtual void SetData(IN const uint32_t& nKey_, IN uint8_t* pData_) override;

            /*
            * @brief 获取数据
            * @param [in] nKey_
            * @return uint8_t*
            */
            virtual uint8_t* GetData(IN const uint32_t& nKey_) override;

            /*
            * @brief 移除
            * @param [in] nKey_
            */
            virtual void Remove(IN const uint32_t& nKey_) override;

            private:
                std::unordered_map<uint32_t, uint8_t*> m_mapContext;
        };
    }
}