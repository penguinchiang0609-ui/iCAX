#include "PythonTemplateHost.h"
#include "StandardJsonCodec.h"

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    struct _object;
    using PyObject = _object;
    using PySsize = std::intptr_t;
    using PyGilState = int;

    constexpr int kPyFileInput = 257;
    constexpr int kPyEvalInput = 258;

    template<typename TFunction>
    TFunction ResolvePythonFunction(HMODULE Module_, const char* pName_)
    {
        const auto _Address = GetProcAddress(Module_, pName_);
        if (!_Address)
            throw std::runtime_error(std::string("embedded Python runtime is missing ") + pName_);
        return reinterpret_cast<TFunction>(_Address);
    }

    std::string PathToUTF8(const std::filesystem::path& Path_)
    {
        const auto _Text = Path_.u8string();
        return { reinterpret_cast<const char*>(_Text.data()), _Text.size() };
    }

    std::filesystem::path CanonicalFile(const std::filesystem::path& Path_, const char* pName_)
    {
        std::error_code _Error;
        if (!std::filesystem::is_regular_file(Path_, _Error))
            throw std::runtime_error(std::string(pName_) + " was not found: " + PathToUTF8(Path_));
        const auto _Canonical = std::filesystem::weakly_canonical(Path_, _Error);
        if (_Error)
            throw std::runtime_error(std::string("failed to resolve ") + pName_);
        return _Canonical;
    }

    std::filesystem::path FindStandardLibraryArchive(const std::filesystem::path& RuntimeHome_)
    {
        std::error_code _Error;
        for (std::filesystem::directory_iterator _Iterator(RuntimeHome_, _Error), _End;
            !_Error && _Iterator != _End; _Iterator.increment(_Error))
        {
            if (!_Iterator->is_regular_file(_Error))
            {
                _Error.clear();
                continue;
            }
            const auto _Name = _Iterator->path().filename().wstring();
            if (_Iterator->path().extension() == L".zip" && _Name.starts_with(L"python"))
                return _Iterator->path();
            _Error.clear();
        }
        throw std::runtime_error("embedded Python standard-library archive was not found");
    }

    struct SPythonApi final
    {
        using TSetWidePath = void(__cdecl*)(const wchar_t*);
        using TInitializeEx = void(__cdecl*)(int);
        using TIsInitialized = int(__cdecl*)();
        using TEvalSaveThread = void* (__cdecl*)();
        using TGilEnsure = PyGilState(__cdecl*)();
        using TGilRelease = void(__cdecl*)(PyGilState);
        using TImportAddModule = PyObject* (__cdecl*)(const char*);
        using TModuleGetDict = PyObject* (__cdecl*)(PyObject*);
        using TDictSetItemString = int(__cdecl*)(PyObject*, const char*, PyObject*);
        using TUnicodeFromStringAndSize = PyObject* (__cdecl*)(const char*, PySsize);
        using TRunStringFlags = PyObject* (__cdecl*)(const char*, int, PyObject*, PyObject*, void*);
        using TUnicodeAsUTF8AndSize = const char* (__cdecl*)(PyObject*, PySsize*);
        using TObjectStr = PyObject* (__cdecl*)(PyObject*);
        using TErrFetch = void(__cdecl*)(PyObject**, PyObject**, PyObject**);
        using TErrNormalizeException = void(__cdecl*)(PyObject**, PyObject**, PyObject**);
        using TDecRef = void(__cdecl*)(PyObject*);

        TSetWidePath SetPythonHome = nullptr;
        TSetWidePath SetPath = nullptr;
        TInitializeEx InitializeEx = nullptr;
        TIsInitialized IsInitialized = nullptr;
        TEvalSaveThread EvalSaveThread = nullptr;
        TGilEnsure GilEnsure = nullptr;
        TGilRelease GilRelease = nullptr;
        TImportAddModule ImportAddModule = nullptr;
        TModuleGetDict ModuleGetDict = nullptr;
        TDictSetItemString DictSetItemString = nullptr;
        TUnicodeFromStringAndSize UnicodeFromStringAndSize = nullptr;
        TRunStringFlags RunStringFlags = nullptr;
        TUnicodeAsUTF8AndSize UnicodeAsUTF8AndSize = nullptr;
        TObjectStr ObjectStr = nullptr;
        TErrFetch ErrFetch = nullptr;
        TErrNormalizeException ErrNormalizeException = nullptr;
        TDecRef DecRef = nullptr;

        void Load(HMODULE Module_)
        {
            SetPythonHome = ResolvePythonFunction<TSetWidePath>(Module_, "Py_SetPythonHome");
            SetPath = ResolvePythonFunction<TSetWidePath>(Module_, "Py_SetPath");
            InitializeEx = ResolvePythonFunction<TInitializeEx>(Module_, "Py_InitializeEx");
            IsInitialized = ResolvePythonFunction<TIsInitialized>(Module_, "Py_IsInitialized");
            EvalSaveThread = ResolvePythonFunction<TEvalSaveThread>(Module_, "PyEval_SaveThread");
            GilEnsure = ResolvePythonFunction<TGilEnsure>(Module_, "PyGILState_Ensure");
            GilRelease = ResolvePythonFunction<TGilRelease>(Module_, "PyGILState_Release");
            ImportAddModule = ResolvePythonFunction<TImportAddModule>(Module_, "PyImport_AddModule");
            ModuleGetDict = ResolvePythonFunction<TModuleGetDict>(Module_, "PyModule_GetDict");
            DictSetItemString = ResolvePythonFunction<TDictSetItemString>(Module_, "PyDict_SetItemString");
            UnicodeFromStringAndSize = ResolvePythonFunction<TUnicodeFromStringAndSize>(
                Module_, "PyUnicode_FromStringAndSize");
            RunStringFlags = ResolvePythonFunction<TRunStringFlags>(Module_, "PyRun_StringFlags");
            UnicodeAsUTF8AndSize = ResolvePythonFunction<TUnicodeAsUTF8AndSize>(
                Module_, "PyUnicode_AsUTF8AndSize");
            ObjectStr = ResolvePythonFunction<TObjectStr>(Module_, "PyObject_Str");
            ErrFetch = ResolvePythonFunction<TErrFetch>(Module_, "PyErr_Fetch");
            ErrNormalizeException = ResolvePythonFunction<TErrNormalizeException>(
                Module_, "PyErr_NormalizeException");
            DecRef = ResolvePythonFunction<TDecRef>(Module_, "Py_DecRef");
        }
    };

    struct SEmbeddedPythonRuntime final
    {
        std::mutex Mutex;
        HMODULE Module = nullptr;
        std::filesystem::path LibraryPath;
        std::wstring PythonHome;
        std::wstring SearchPath;
        SPythonApi Api;
    };

    SEmbeddedPythonRuntime& ProcessRuntime()
    {
        static SEmbeddedPythonRuntime _Runtime;
        return _Runtime;
    }

    void EnsureRuntimeInitialized(const std::filesystem::path& RuntimeLibrary_)
    {
        auto& _Runtime = ProcessRuntime();
        std::scoped_lock _Lock(_Runtime.Mutex);
        const auto _Library = CanonicalFile(RuntimeLibrary_, "embedded Python runtime library");
        if (_Runtime.Module)
        {
            if (_Runtime.LibraryPath != _Library)
                throw std::runtime_error("a different embedded Python runtime is already active");
            return;
        }

        const auto _Home = _Library.parent_path();
        const auto _StandardLibrary = FindStandardLibraryArchive(_Home);
        const auto _Module = LoadLibraryExW(
            _Library.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!_Module)
            throw std::runtime_error("failed to load embedded Python runtime library");

        try
        {
            _Runtime.Api.Load(_Module);
            _Runtime.LibraryPath = _Library;
            _Runtime.PythonHome = _Home.wstring();
            _Runtime.SearchPath = _StandardLibrary.wstring() + L";" + _Home.wstring();
            _Runtime.Api.SetPythonHome(_Runtime.PythonHome.c_str());
            _Runtime.Api.SetPath(_Runtime.SearchPath.c_str());
            _Runtime.Api.InitializeEx(0);
            if (!_Runtime.Api.IsInitialized())
                throw std::runtime_error("embedded Python runtime initialization failed");
            (void)_Runtime.Api.EvalSaveThread();
            _Runtime.Module = _Module;
        }
        catch (...)
        {
            FreeLibrary(_Module);
            _Runtime.Module = nullptr;
            _Runtime.LibraryPath.clear();
            _Runtime.PythonHome.clear();
            _Runtime.SearchPath.clear();
            _Runtime.Api = {};
            throw;
        }
    }

    class CPythonGilGuard final
    {
    public:
        explicit CPythonGilGuard(const SPythonApi& Api_)
            : Api(Api_), State(Api.GilEnsure())
        {
        }

        ~CPythonGilGuard()
        {
            Api.GilRelease(State);
        }

        CPythonGilGuard(const CPythonGilGuard&) = delete;
        CPythonGilGuard& operator=(const CPythonGilGuard&) = delete;

    private:
        const SPythonApi& Api;
        PyGilState State;
    };

    std::string PythonObjectText(const SPythonApi& Api_, PyObject* pObject_)
    {
        if (!pObject_) return {};
        auto* _TextObject = Api_.ObjectStr(pObject_);
        if (!_TextObject) return {};
        PySsize _Size = 0;
        const auto _Text = Api_.UnicodeAsUTF8AndSize(_TextObject, &_Size);
        std::string _Result;
        if (_Text && _Size >= 0) _Result.assign(_Text, static_cast<std::size_t>(_Size));
        Api_.DecRef(_TextObject);
        return _Result;
    }

    std::string ConsumePythonError(const SPythonApi& Api_)
    {
        PyObject* _Type = nullptr;
        PyObject* _Value = nullptr;
        PyObject* _Traceback = nullptr;
        Api_.ErrFetch(&_Type, &_Value, &_Traceback);
        Api_.ErrNormalizeException(&_Type, &_Value, &_Traceback);
        auto _Message = PythonObjectText(Api_, _Value ? _Value : _Type);
        if (_Traceback) Api_.DecRef(_Traceback);
        if (_Value) Api_.DecRef(_Value);
        if (_Type) Api_.DecRef(_Type);
        return _Message.empty() ? "unknown embedded Python error" : _Message;
    }

    void SetPythonString(
        const SPythonApi& Api_, PyObject* pDictionary_, const char* pName_, const std::string& Value_)
    {
        auto* _Value = Api_.UnicodeFromStringAndSize(
            Value_.data(), static_cast<PySsize>(Value_.size()));
        if (!_Value)
            throw std::runtime_error("failed to allocate embedded Python input string");
        const auto _Set = Api_.DictSetItemString(pDictionary_, pName_, _Value);
        Api_.DecRef(_Value);
        if (_Set != 0)
            throw std::runtime_error("failed to set embedded Python input value");
    }

    std::uint64_t ToUInt64(const iCAX::Data::Variant& Value_)
    {
        if (Value_.Is<unsigned long long>()) return Value_.To<unsigned long long>();
        if (Value_.Is<long long>()) return static_cast<std::uint64_t>(Value_.To<long long>());
        if (Value_.Is<unsigned int>()) return Value_.To<unsigned int>();
        if (Value_.Is<int>()) return static_cast<std::uint64_t>(Value_.To<int>());
        if (Value_.Is<double>()) return static_cast<std::uint64_t>(Value_.To<double>());
        throw std::invalid_argument("template requestId must be an integer");
    }
}

using iCAX::Data::ObjectMap;
using iCAX::Data::Variant;

class iCAX::TemplateRuntime::CPythonTemplateHost::CImpl final
{
public:
    explicit CImpl(SPythonTemplateHostOptions Options_)
        : Options(std::move(Options_))
    {
        if (Options.PythonRuntimeLibrary.empty() || Options.WorkerScript.empty())
            throw std::invalid_argument("embedded Python runtime paths cannot be empty");
    }

    ObjectMap Invoke(const ObjectMap& Request_)
    {
        std::scoped_lock _Lock(Mutex);
        EnsureStarted();
        auto& _Runtime = ProcessRuntime();
        CPythonGilGuard _Gil(_Runtime.Api);

        auto* _MainModule = _Runtime.Api.ImportAddModule("__main__");
        auto* _Globals = _MainModule ? _Runtime.Api.ModuleGetDict(_MainModule) : nullptr;
        if (!_Globals)
            throw std::runtime_error("embedded Python main module is unavailable");

        const auto _RequestID = NextRequestID++;
        auto _Envelope = Request_;
        _Envelope["requestId"] = static_cast<unsigned long long>(_RequestID);
        SetPythonString(
            _Runtime.Api, _Globals, "__icax_template_request_json",
            CStandardJsonCodec::Serialize(Variant(_Envelope)));

        auto* _ResponseObject = _Runtime.Api.RunStringFlags(
            "__import__('icax_template_embedded').invoke(__icax_template_request_json)",
            kPyEvalInput, _Globals, _Globals, nullptr);
        if (!_ResponseObject)
            throw std::runtime_error("embedded Python template invocation failed: "
                + ConsumePythonError(_Runtime.Api));

        PySsize _ResponseSize = 0;
        const auto _ResponseData = _Runtime.Api.UnicodeAsUTF8AndSize(
            _ResponseObject, &_ResponseSize);
        if (!_ResponseData || _ResponseSize < 0)
        {
            _Runtime.Api.DecRef(_ResponseObject);
            throw std::runtime_error("embedded Python template returned a non-text response");
        }
        const std::string _ResponseText(
            _ResponseData, static_cast<std::size_t>(_ResponseSize));
        _Runtime.Api.DecRef(_ResponseObject);

        const auto _ResponseValue = CStandardJsonCodec::Parse(_ResponseText);
        if (!_ResponseValue.Is<ObjectMap>())
            throw std::runtime_error("embedded Python template returned a non-object response");
        const auto _Response = _ResponseValue.To<ObjectMap>();
        const auto _ID = _Response.find("requestId");
        if (_ID == _Response.end() || ToUInt64(_ID->second) != _RequestID)
            throw std::runtime_error("embedded Python template response requestId does not match");
        const auto _OK = _Response.find("ok");
        if (_OK == _Response.end() || !_OK->second.Is<bool>())
            throw std::runtime_error("embedded Python template response has no valid ok flag");
        if (!_OK->second.To<bool>())
        {
            std::string _Message = "embedded Python template evaluation failed";
            if (const auto _Error = _Response.find("error");
                _Error != _Response.end() && _Error->second.Is<ObjectMap>())
            {
                const auto _ErrorObject = _Error->second.To<ObjectMap>();
                if (const auto _Text = _ErrorObject.find("message");
                    _Text != _ErrorObject.end() && _Text->second.Is<std::string>())
                {
                    _Message = _Text->second.To<std::string>();
                }
            }
            throw std::runtime_error(_Message);
        }
        const auto _Result = _Response.find("result");
        if (_Result == _Response.end() || !_Result->second.Is<ObjectMap>())
            throw std::runtime_error("embedded Python template response has no result object");
        return _Result->second.To<ObjectMap>();
    }

    bool IsRunning() const noexcept
    {
        return Started;
    }

    void Stop() noexcept
    {
        std::scoped_lock _Lock(Mutex);
        Started = false;
    }

private:
    void EnsureStarted()
    {
        if (Started) return;
        EnsureRuntimeInitialized(Options.PythonRuntimeLibrary);
        const auto _Worker = CanonicalFile(Options.WorkerScript, "Python template worker");
        (void)CanonicalFile(
            _Worker.parent_path() / "icax_template_embedded.py",
            "embedded Python template bridge");

        auto& _Runtime = ProcessRuntime();
        CPythonGilGuard _Gil(_Runtime.Api);
        auto* _MainModule = _Runtime.Api.ImportAddModule("__main__");
        auto* _Globals = _MainModule ? _Runtime.Api.ModuleGetDict(_MainModule) : nullptr;
        if (!_Globals)
            throw std::runtime_error("embedded Python main module is unavailable");
        SetPythonString(
            _Runtime.Api, _Globals, "__icax_template_runtime_path",
            PathToUTF8(_Worker.parent_path()));
        auto* _BootstrapResult = _Runtime.Api.RunStringFlags(
            "import sys\n"
            "if __icax_template_runtime_path not in sys.path:\n"
            "    sys.path.insert(0, __icax_template_runtime_path)\n"
            "import icax_template_embedded\n",
            kPyFileInput, _Globals, _Globals, nullptr);
        if (!_BootstrapResult)
            throw std::runtime_error("embedded Python template bridge failed to load: "
                + ConsumePythonError(_Runtime.Api));
        _Runtime.Api.DecRef(_BootstrapResult);
        Started = true;
    }

    SPythonTemplateHostOptions Options;
    mutable std::mutex Mutex;
    bool Started = false;
    std::uint64_t NextRequestID = 1;
};

iCAX::TemplateRuntime::CPythonTemplateHost::CPythonTemplateHost(
    SPythonTemplateHostOptions Options_)
    : m_pImpl(std::make_unique<CImpl>(std::move(Options_)))
{
}

iCAX::TemplateRuntime::CPythonTemplateHost::~CPythonTemplateHost() = default;

iCAX::Data::ObjectMap iCAX::TemplateRuntime::CPythonTemplateHost::Invoke(
    const iCAX::Data::ObjectMap& Request_)
{
    return m_pImpl->Invoke(Request_);
}

bool iCAX::TemplateRuntime::CPythonTemplateHost::IsRunning() const noexcept
{
    return m_pImpl->IsRunning();
}

void iCAX::TemplateRuntime::CPythonTemplateHost::Stop() noexcept
{
    m_pImpl->Stop();
}
