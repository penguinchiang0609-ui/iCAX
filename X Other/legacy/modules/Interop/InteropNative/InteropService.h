#pragma once
#include "Interop.h"
#include "IInteropService.h"
#include "Services/ServicesHelper.h"

#include "flatbuffers/allocator.h"

namespace iCAX
{
    namespace Interop
    {
        using ManagedCallbackFn = int(*)(IN const std::string& strBehaviourName_, IN const std::string& strMethodName_, IN void* pContext_, const uint8_t* pReqBuffer_, int nReqBufferSize_, uint8_t* pRespBuffer_, int nRespMaxSize, int& nRespActualSize_, std::string& strError_);

        /*
        * @brief C++与CSharp通信服务
        */
        class CInteropService : public IInteropService
        {
        public:
            /*
            * @brief 构造函数
            */
            CInteropService();

            /*
            * @brief 析构函数
            */
            virtual ~CInteropService();

        public://! IService成员
            /*
            * @brief 加载
            * @details
            *   服务需要的相关资源初始化，保证就绪
            */
            virtual void OnLoad() override;

            /*
            * @brief 卸载
            * @details
            *   释放服务占用的资源
            */
            virtual void OnUnload() override;

        public:
            /*
            * @brief 调用本地方法
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
                OUT std::string& strError_) override;

            /*
            * @brief 调用托管方法
            * @param [in] strBehaviourName_
            * @param [in] strMethodName_
            * @param [in] pContext_
            * @param [in] reqBuilderFn_ 请求构造器
            * @param [in] FailedFn_ 失败处理函数（消息处理失败）
            * @param [in] SuccessFn_ 成功处理函数（消息处理成功）
            * @details
            *   C++在调用之前将数据写入SyncBuffer的pRequestBuffer地址中。调用后从SyncBuffer的pResponseBuffer地址读取结果
            */
            virtual void InvokeManaged(IN const std::string& strBehaviourName_
                , IN const std::string& strMethodName_
                , IN void* pContext_
                , IN std::function<void(flatbuffers::FlatBufferBuilder&)> reqBuilderFn_
                , std::function<void(int,const std::string&)> FailedFn_
                , std::function<void(const uint8_t*, int)> SuccessFn_) override;

            /*
            * @brief 绑定C#回调，用于C++调用CSharp方法
            * @param [in] Callback_
            */
            void BindManagedCallback(IN ManagedCallbackFn Callback_);

        private:
            ManagedCallbackFn m_ManagedFn;
            AUTO_REGIST_SERVICE(IInteropService, CInteropService);
        };
    }
}