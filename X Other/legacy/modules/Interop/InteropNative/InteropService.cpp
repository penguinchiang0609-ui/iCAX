#include "pch.h"
#include "InteropService.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "Behaviour/IUniverse.h"

const int g_s_MAX_BUFFER_SIZE = 1024 * 1024 * 10;

//! 构造函数
iCAX::Interop::CInteropService::CInteropService()
    : m_ManagedFn(nullptr)
{
}

//! 析构函数
iCAX::Interop::CInteropService::~CInteropService()
{
    OnUnload();
} 

//! 加载
void iCAX::Interop::CInteropService::OnLoad()
{
}

//! 卸载
void iCAX::Interop::CInteropService::OnUnload()
{
}

//! 调用本地方法
int iCAX::Interop::CInteropService::InvokeNative(IN const std::string& strBehaviourName_, IN const std::string& strMethodName_, IN void* pContext_, IN const uint8_t* pReqBuffer_, IN int nReqBufferSize_, IN OUT uint8_t* pRespBuffer_, IN int nRespBufferMaxSize_, OUT int& nRespBufferActualSize_, OUT std::string& strError_)
{
    auto _pUniverse = reinterpret_cast<iCAX::Behaviour::IUniverse*>(pContext_);
    if (_pUniverse == nullptr)
    {
        strError_ = "NativeCallback: null universe context";
        return -1;
    }
    auto _pFn = iCAX::Behaviour::GetGlobalBehaviourRegistry()->ResolveRPC(strBehaviourName_, strMethodName_);
    if (_pFn.FnPtr == nullptr)
    {
        strError_ =  std::format("Behaviour {} method {} not exist", strBehaviourName_, strMethodName_);
        return -1;
    }
    if (_pFn.strSignature != "int(uint8_t*, int, iCAX::Behaviour::IUniverse*, flatbuffers::FlatBufferBuilder&, std::string&)")
    {
        strError_ = std::format("Behaviour {} method {} not interop signature", strBehaviourName_, strMethodName_);
        return -1;
    }

    flatbuffers::FlatBufferBuilder _respBuilder(1024);

    auto _pNativeFn = (int(*)(IN const uint8_t*, IN int, IN iCAX::Behaviour::IUniverse*, OUT flatbuffers::FlatBufferBuilder&, OUT std::string&))(_pFn.FnPtr);
    std::string _strError;
    int _nCode = _pNativeFn(pReqBuffer_, nReqBufferSize_, _pUniverse, _respBuilder, _strError);
    if (_nCode != 0)
    {
        strError_  = _strError;
        return _nCode;
    }
    else
    {
        uint8_t* _RespBuf = _respBuilder.GetBufferPointer();
        nRespBufferActualSize_ = static_cast<int32_t>(_respBuilder.GetSize());
        if (nRespBufferActualSize_ > nRespBufferMaxSize_)
        {
            strError_ = "response too large";
            return -1;
        }

        std::memcpy(pRespBuffer_, _RespBuf, nRespBufferActualSize_);
        return 0;
    }
}

//! 调用托管方法
void iCAX::Interop::CInteropService::InvokeManaged(IN const std::string& strBehaviourName_, IN const std::string& strMethodName_, IN void* pContext_, IN std::function<void(flatbuffers::FlatBufferBuilder&)> reqBuilderFn_, std::function<void(int, const std::string&)> FailedFn_, std::function<void(const uint8_t*, int)> SuccessFn_)
{
    if (m_ManagedFn)
    {
        //! 写入请求体
        flatbuffers::FlatBufferBuilder _reqBuilder(1024);
        reqBuilderFn_(_reqBuilder);
        uint8_t* _ReqBuf = _reqBuilder.GetBufferPointer();
        int32_t _nReqSize = static_cast<int32_t>(_reqBuilder.GetSize());
        std::string _strError;

        uint8_t* _pRespBuff = new uint8_t[g_s_MAX_BUFFER_SIZE];
        int _nActualResoSize = 0;
        int _nCode = m_ManagedFn(strBehaviourName_, strMethodName_, pContext_, _ReqBuf, _nReqSize, _pRespBuff, g_s_MAX_BUFFER_SIZE, _nActualResoSize, _strError);

        if (_nCode != 0)
        {
            FailedFn_(_nCode, _strError);
        }
        else
        {
            if (_nActualResoSize <= 0)
            {
                _strError = std::format("Invalid FlatBuffer size", strMethodName_);
                FailedFn_(-1, _strError);
            }
            else
            {
                SuccessFn_(_pRespBuff, _nActualResoSize);
            }
        }
    }
    else
    {
        std::string _strError = "no managed";
        FailedFn_(-1, _strError);
    }
}


//! 绑定C#回调，用于C++调用CSharp方法
void iCAX::Interop::CInteropService::BindManagedCallback(IN ManagedCallbackFn Callback_)
{
    m_ManagedFn = Callback_;
}
