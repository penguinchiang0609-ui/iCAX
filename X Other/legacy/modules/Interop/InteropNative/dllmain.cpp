// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include "Engine/CAXEngine.h"
#include "IInteropService.h"

#include <map>
#include <string>
#include <algorithm>
#include <cstring> // memcpy
#include <memory>
#include <Data/CommonFunction.h>
#include "InteropService.h"
using namespace iCAX::Interop;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

#pragma pack(push, 8)
struct RequestHeader
{
    int EngineIDLength;
    char EngineID[64];
    int BehaviourLength;
    char Behaviour[128];
    int MethodLength;
    char Method[128];
    int nVersion = 0;//! 版本号

    void SetID(IN const std::string& str_)
    {
        std::string _str = LocalToUTF8(str_);
        EngineIDLength = std::min((int)_str.size(), 64 - 1);
        strncpy_s(EngineID, 64, _str.c_str(), EngineIDLength);
    }

    std::string GetID() const
    {
        return UTF8ToLocal(EngineID);
    }

    void SetBehaviour(IN const std::string& str_)
    {
        std::string _str = LocalToUTF8(str_);
        BehaviourLength = std::min((int)_str.size(), 64 - 1);
        strncpy_s(Behaviour, 128, _str.c_str(), BehaviourLength);
    }

    std::string GetBehaviour() const
    {
        return UTF8ToLocal(Behaviour);
    }

    void SetMethod(IN const std::string& str_)
    {
        std::string _str = LocalToUTF8(str_);
        MethodLength = std::min((int)_str.size(), 64 - 1);
        strncpy_s(Method, 128, _str.c_str(), MethodLength);
    }

    std::string GetMethod() const
    {
        return UTF8ToLocal(Method);
    }
};

struct ResponseHeader
{
    int ErrorTextLen;
    char ErrorText[1024];

    void SetError(IN const std::string& str_)
    {
        std::string _str = LocalToUTF8(str_);
        ErrorTextLen = std::min((int)_str.size(), 64 - 1);
        strncpy_s(ErrorText, 1024, _str.c_str(), ErrorTextLen);
    }

    std::string GetError() const
    {
        return UTF8ToLocal(ErrorText);
    }
};

#pragma pack(pop)

//! C#侧绑定的回调函数签名
using NetCallbackFn = int(*)(const RequestHeader* pRequestHeader_, const uint8_t* pReqBuffer_, int nReqBufferSize_,  ResponseHeader* pResponseHeader_, uint8_t* pRespBuffer_, int nRespMaxSize, int* pnRespActualSize_);

struct EngineInstance
{
    iCAX::Engine::CCAXEngine* pEngine = nullptr;
    NetCallbackFn ManagedFn = nullptr;
};

static std::unordered_map<std::string, EngineInstance> g_Engines;//! 此处不用考虑多线程。因为每个线程Engine的ID都会不同

int _ManagedCallback(IN const std::string& strBehaviourName_, IN const std::string& strMethodName_, IN void* pContext_, const uint8_t* pReqBuffer_, int nReqBufferSize_, uint8_t* pRespBuffer_, int nRespMaxSize, int& nRespActualSize_, std::string& strError_)
{
    std::string _strID = iCAX::Data::to_string(static_cast<iCAX::Behaviour::IUniverse*>(pContext_)->GetID());
    if (!g_Engines.contains(_strID))
    {
        strError_ = "context IUniverse not match to Engine";
        return -1;
    }
    auto& _Instance = g_Engines[_strID];
    if (_Instance.ManagedFn == nullptr)
    {
        strError_ = "managed callback not bind";
        return -1;
    }

    RequestHeader _ReqHeader{};
    _ReqHeader.SetID(_strID);
    _ReqHeader.SetMethod(strMethodName_);
    _ReqHeader.SetBehaviour(strBehaviourName_);
    ResponseHeader _RespHeader{};

    int _nCode = 0;
    try
    {
        _nCode = _Instance.ManagedFn(&_ReqHeader, pReqBuffer_, nReqBufferSize_, &_RespHeader, pRespBuffer_, nRespMaxSize, &nRespActualSize_);
    }
    catch (...)
    {
        // 不允许异常穿过边界，转成错误码
        strError_ = "ManagedFn threw exception";
        return -1;
    }

    if (_nCode != 0)
    {
        strError_ = _RespHeader.GetError();
    }
    return _nCode;
}

extern "C"
{
    /*
    * @brief 创建引擎
    */
    __declspec(dllexport) void Engine_Create(NetCallbackFn fn, char* strID)
    {
        try
        {
            //! 此处因为每次新建引擎，IUniverse的ID都会新生成UUID，所以此处不用处理strID已经存在的情形
            auto _ManagedFn = fn;
            auto _pEngine = new iCAX::Engine::CCAXEngine();
            _pEngine->Load();
            auto _pInterop = std::dynamic_pointer_cast<iCAX::Interop::CInteropService>(iCAX::Services::GetGlobalServiceProvider()->Resolve<iCAX::Interop::IInteropService>());
            if (_pInterop)
            {
                _pInterop->BindManagedCallback(_ManagedCallback);
            }
            std::string _strID = iCAX::Data::to_string(_pEngine->GetUniverse().GetID());
            g_Engines[_strID] = { _pEngine, _ManagedFn };

            _strID = LocalToUTF8(_strID);
            auto _nEngineIDLen = std::min((int)_strID.size(), 64 - 1);
            strncpy_s(strID, 64, _strID.c_str(), _nEngineIDLen);
            return;
        }
        catch (...)
        {
            // 捕获所有异常并返回 nullptr
            return;
        }
    }

    /*
    * @brief 引擎循环
    */
    __declspec(dllexport) void Engine_Loop(char* strID_)
    {
        std::string _strID = UTF8ToLocal(strID_);
        if (g_Engines.contains(_strID))
        {
            g_Engines[_strID].pEngine->MainLoop();
        }
    }

    /*
    * @brief 释放引擎
    */
    __declspec(dllexport) void Engine_Destory(char* strID_)
    {
        std::string _strID = UTF8ToLocal(strID_);
        if (!g_Engines.contains(_strID)) return;
        auto& _Instance = g_Engines[_strID];
        _Instance.pEngine->Unload();
        _Instance.ManagedFn = nullptr;
        delete _Instance.pEngine;
        _Instance.pEngine = nullptr;
        g_Engines.erase(_strID);
    }

    /*
    * @brief 调用本地方法
    */
    __declspec(dllexport) int InvokeNative(const RequestHeader* pReqHeader_, const uint8_t* pReqBuffer_, int nReqBufferSize_, ResponseHeader* pRespHeader_, uint8_t* pRespBuffer_, int nRespBufferMaxSize_, int* pnRespBufferActualSize_)
    {
        //! 入参检查
        if (!pReqHeader_ || !pRespHeader_ || !pReqBuffer_ || !pRespBuffer_) return -1;

        try
        {
            ZeroMemory(pRespHeader_->ErrorText, 1024);
            std::string _strID = pReqHeader_->GetID();
            if (!g_Engines.contains(_strID))
            {
                pRespHeader_->SetError("InvokeNative invalid engine pointer");
                return -1;
            }
            auto _pInterop = iCAX::Services::GetGlobalServiceProvider()->Resolve<iCAX::Interop::IInteropService>();
            if (_pInterop == nullptr)
            {
                pRespHeader_->SetError("interop service not available");
                return -1;
            }
            std::string _strError;
            int _nCode = _pInterop->InvokeNative(pReqHeader_->GetBehaviour(), pReqHeader_ ->GetMethod(),  & g_Engines[_strID].pEngine->GetUniverse(), pReqBuffer_, nReqBufferSize_, pRespBuffer_, nRespBufferMaxSize_, *pnRespBufferActualSize_, _strError);
            if (_nCode != 0)
            {
                pRespHeader_->SetError(_strError);
                return _nCode;
            }

            return 0;
        }
        catch (const std::exception& e)
        {
            pRespHeader_->SetError(e.what());
            return -1;
        }
        catch (...)
        {
            pRespHeader_->SetError("Unknown C++ exception");
            return -1;
        }
    }


} // extern "C"
