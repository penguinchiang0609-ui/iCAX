#pragma once
#include "ProcedureCall.h"
#include <memory>
#include "PCEntry.h"
#include "Data/CommonFunction.h"

namespace iCAX
{
    namespace PC
    {
        /*
        * @brief 过程调用注册表
        */
        class _PC_EXP IPCRegistry
        {
        public:
            /*
            * @brief 构造函数
            */
            IPCRegistry() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IPCRegistry() = default;

        public:
            /*
            * @brief  注册
            * @param [in] Decl_
            * @param [in] Fn_
            */
            virtual bool Regist(IN const PCID& PCID_, IN PCFunc Fn_) = 0;

            /*
            * @brief 查询过程调用入口
            * @param [in] PCID_
            * @return PCEntry
            */
            virtual PCFunc Find(IN const PCID& PCID_) const = 0;
        };

        /*
        * @brief 获取过程调用注册表
        * @return std::shared_ptr<IPCRegistry>
        */
        _PC_EXP std::shared_ptr<IPCRegistry> GetGlobalPCRegistry();

#define AUTO_REGIST_PC(ModuleName, FnName)                          \
    struct AutoRegister_##ModuleName##_##FnName                     \
    {                                                               \
        AutoRegister_##ModuleName##_##FnName()                      \
        {                                                           \
            auto _PCID = iCAX::PC::MakePCID(#ModuleName, #FnName);  \
            iCAX::PC::GetGlobalPCRegistry()->Regist(_PCID, &FnName);\
        }                                                           \
    };                                                              \
    static AutoRegister_##ModuleName##_##FnName                     \
        g_AutoRegister_##ModuleName##_##FnName{};                  \
   

    }
}


