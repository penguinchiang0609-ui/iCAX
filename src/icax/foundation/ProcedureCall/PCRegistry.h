#pragma once
#include "ProcedureCall.h"
#include "PCEntry.h"
#include "IPCRegistry.h"
#include <unordered_map>


namespace iCAX
{
    namespace PC
    {
        /*
        * @brief 过程调用注册表
        */
        class CPCRegistry : public IPCRegistry
        {
        public:
            /*
            * @brief 构造函数
            */
            CPCRegistry();

            /*
            * @brief 析构函数
            */
            virtual ~CPCRegistry();

        public:
            /*
            * @brief  注册
            * @param [in] Decl_
            * @param [in] Fn_
            */
            virtual bool Regist(IN const PCID& PCID_, IN PCFunc Fn_) override;

            /*
            * @brief 查询过程调用入口
            * @param [in] PCID_
            * @return PCEntry
            */
            virtual PCFunc Find(IN const PCID& PCID_) const override;

        private:
            std::unordered_map<PCID, PCFunc> m_mapEntries;
        };
    }
}
