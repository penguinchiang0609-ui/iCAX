#pragma once
#include "Interop.h"
#include <string>
#include "Services/IService.h"
#include "flatbuffers/flatbuffer_builder.h"
#include <exception>
#include <stdexcept>
#include <Data/CommonFunction.h>

using namespace iCAX::Services;


namespace iCAX
{
    namespace Interop
    {
        /*
        * @brief 与C#通信的服务
        */
        class _INTEROP_EXP IInteropService : public IService
        {
        public:
            /*
            * @brief 构造函数
            */
            IInteropService() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IInteropService() = default;

        public:
            /*
            * @brief 当被请求时
            * @param [in] strBehaviourName_
            * @param [in] strMethodName_
            * @param [in] pContext_
            * @param [in] pReqBuffer_
            * @param [in] nReqBufferSize_
            * @param [in/out] pRespBuffer_
            * @param [in] nRespoBufferMaxSize_
            * @param [out] nRespBufferActualSize_
            * @param [out] strError_
            * @return int
            */
            virtual int InvokeNative(
                IN const std::string& strBehaviourName_,
                IN const std::string& strMethodName_,
                IN void* pContext_,
                IN  const uint8_t* pReqBuffer_,
                IN int nReqBufferSize_,
                IN OUT uint8_t* pRespBuffer_,
                IN  int nRespoBufferMaxSize_,
                OUT int& nRespBufferActualSize_,
                OUT std::string& strError_) = 0;

            /*
            * @brief 调用托管方法
            * @param [in] strBehaviourName_
            * @param [in] strMethodName_
            * @param [in] pContext_
            * @param [in] reqBuilderFn_ 请求构造器
            * @param [in] ExceptionFn_ 异常处理函数（消息处理失败）
            * @param [in] SuccessFn_ 成功处理函数（消息处理成功）
            * @details
            *   C++在调用之前将数据写入SyncBuffer的pRequestBuffer地址中。调用后从SyncBuffer的pResponseBuffer地址读取结果
            */
            virtual void InvokeManaged(IN const std::string& strBehaviourName_,
                IN const std::string& strMethodName_,
                IN void* pContext_,
                IN std::function<void(flatbuffers::FlatBufferBuilder&)> reqBuilderFn_,
                std::function<void(int, const std::string&)> ExceptionFn_,
                std::function<void(const uint8_t*, int)> SuccessFn_) = 0;
        };
    }
}