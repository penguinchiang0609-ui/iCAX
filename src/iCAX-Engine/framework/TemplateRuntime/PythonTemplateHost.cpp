#include "PythonTemplateHost.h"
#include "StandardJsonCodec.h"
#include "TemplateContracts.h"

#include <Windows.h>

#include <atomic>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    std::wstring QuoteArgument(const std::wstring& Value_)
    {
        std::wstring _Result = L"\"";
        std::size_t _Backslashes = 0;
        for (const auto _Character : Value_)
        {
            if (_Character == L'\\')
            {
                ++_Backslashes;
                continue;
            }
            if (_Character == L'\"')
            {
                _Result.append(_Backslashes * 2 + 1, L'\\');
                _Result.push_back(L'\"');
                _Backslashes = 0;
                continue;
            }
            _Result.append(_Backslashes, L'\\');
            _Backslashes = 0;
            _Result.push_back(_Character);
        }
        _Result.append(_Backslashes * 2, L'\\');
        _Result.push_back(L'\"');
        return _Result;
    }

    std::uint64_t ToUInt64(const iCAX::Data::Variant& Value_)
    {
        if (Value_.Is<unsigned long long>()) return Value_.To<unsigned long long>();
        if (Value_.Is<long long>()) return static_cast<std::uint64_t>(Value_.To<long long>());
        if (Value_.Is<unsigned int>()) return Value_.To<unsigned int>();
        if (Value_.Is<int>()) return static_cast<std::uint64_t>(Value_.To<int>());
        if (Value_.Is<double>()) return static_cast<std::uint64_t>(Value_.To<double>());
        throw std::invalid_argument("template worker requestId must be an integer");
    }

    DWORD WaitMilliseconds(std::chrono::milliseconds Duration_)
    {
        if (Duration_.count() <= 0)
            throw std::invalid_argument("Python template host timeout must be positive");
        return static_cast<DWORD>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(Duration_.count()),
            static_cast<std::uint64_t>((std::numeric_limits<DWORD>::max)() - 1)));
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
        if (Options.PythonExecutable.empty() || Options.WorkerScript.empty())
            throw std::invalid_argument("Python template host paths cannot be empty");
        (void)WaitMilliseconds(Options.RequestTimeout);
        (void)WaitMilliseconds(Options.ShutdownTimeout);
    }

    ~CImpl()
    {
        Stop();
    }

    ObjectMap Invoke(const ObjectMap& Request_)
    {
        std::scoped_lock _Lock(Mutex);
        EnsureStarted();
        const auto _RequestID = NextRequestID++;
        auto _Envelope = Request_;
        _Envelope["requestId"] = static_cast<unsigned long long>(_RequestID);
        const auto _Payload = CStandardJsonCodec::Serialize(Variant(_Envelope)) + "\n";
        try
        {
            WriteAll(_Payload);
            const auto _ResponseText = ReadLineUntilEvent();
            const auto _ResponseValue = CStandardJsonCodec::Parse(_ResponseText);
            if (!_ResponseValue.Is<ObjectMap>())
                throw std::runtime_error("template worker returned a non-object response");
            const auto _Response = _ResponseValue.To<ObjectMap>();
            const auto _ID = _Response.find("requestId");
            if (_ID == _Response.end() || ToUInt64(_ID->second) != _RequestID)
                throw std::runtime_error("template worker response requestId does not match");
            const auto _OK = _Response.find("ok");
            if (_OK == _Response.end() || !_OK->second.Is<bool>())
                throw std::runtime_error("template worker response has no valid ok flag");
            if (!_OK->second.To<bool>())
            {
                std::string _Message = "template worker evaluation failed";
                if (const auto _Error = _Response.find("error"); _Error != _Response.end() && _Error->second.Is<ObjectMap>())
                {
                    const auto _ErrorObject = _Error->second.To<ObjectMap>();
                    if (const auto _Text = _ErrorObject.find("message"); _Text != _ErrorObject.end() && _Text->second.Is<std::string>())
                        _Message = _Text->second.To<std::string>();
                }
                throw std::runtime_error(_Message);
            }
            const auto _Result = _Response.find("result");
            if (_Result == _Response.end() || !_Result->second.Is<ObjectMap>())
                throw std::runtime_error("template worker response has no result object");
            return _Result->second.To<ObjectMap>();
        }
        catch (...)
        {
            StopProcess();
            throw;
        }
    }

    bool IsRunning() const noexcept
    {
        if (!Process) return false;
        return WaitForSingleObject(Process, 0) == WAIT_TIMEOUT;
    }

    void Stop() noexcept
    {
        std::scoped_lock _Lock(Mutex);
        if (!Process) return;
        try
        {
            ObjectMap _Shutdown;
            _Shutdown["protocol"] = std::string(iCAX::TemplateRuntime::kTemplateProtocol);
            _Shutdown["protocolVersion"] = static_cast<unsigned long long>(iCAX::TemplateRuntime::kTemplateProtocolVersion);
            _Shutdown["operation"] = std::string("shutdown");
            _Shutdown["requestId"] = static_cast<unsigned long long>(NextRequestID++);
            WriteAll(CStandardJsonCodec::Serialize(Variant(_Shutdown)) + "\n");
        }
        catch (...) {}
        if (WaitForSingleObject(Process, WaitMilliseconds(Options.ShutdownTimeout)) == WAIT_TIMEOUT)
            TerminateProcess(Process, 1);
        StopProcess();
    }

private:
    void EnsureStarted()
    {
        if (IsRunning()) return;
        StopProcess();
        if (!std::filesystem::is_regular_file(Options.PythonExecutable))
            throw std::runtime_error("Python executable was not found: " + Options.PythonExecutable.string());
        if (!std::filesystem::is_regular_file(Options.WorkerScript))
            throw std::runtime_error("Python template worker was not found: " + Options.WorkerScript.string());

        SECURITY_ATTRIBUTES _Security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        HANDLE _ChildStdInRead = nullptr;
        HANDLE _ChildStdOutWrite = nullptr;
        HANDLE _ChildStdErrWrite = nullptr;
        if (!CreatePipe(&_ChildStdInRead, &StdInWrite, &_Security, 0)
            || !CreatePipe(&StdOutRead, &_ChildStdOutWrite, &_Security, 0)
            || !CreatePipe(&StdErrRead, &_ChildStdErrWrite, &_Security, 0))
        {
            CloseHandleIfValid(_ChildStdInRead);
            CloseHandleIfValid(_ChildStdOutWrite);
            CloseHandleIfValid(_ChildStdErrWrite);
            StopProcess();
            throw std::runtime_error("failed to create Python template worker pipes");
        }
        SetHandleInformation(StdInWrite, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(StdOutRead, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(StdErrRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW _Startup{};
        _Startup.cb = sizeof(_Startup);
        _Startup.dwFlags = STARTF_USESTDHANDLES;
        _Startup.hStdInput = _ChildStdInRead;
        _Startup.hStdOutput = _ChildStdOutWrite;
        _Startup.hStdError = _ChildStdErrWrite;
        PROCESS_INFORMATION _Process{};
        auto _Command = QuoteArgument(Options.PythonExecutable.wstring())
            + L" -I -X utf8 -u " + QuoteArgument(Options.WorkerScript.wstring());
        std::vector<wchar_t> _CommandLine(_Command.begin(), _Command.end());
        _CommandLine.push_back(L'\0');
        const auto _WorkingDirectory = Options.WorkingDirectory.empty()
            ? Options.WorkerScript.parent_path() : Options.WorkingDirectory;
        const auto _Created = CreateProcessW(
            Options.PythonExecutable.c_str(), _CommandLine.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr,
            _WorkingDirectory.empty() ? nullptr : _WorkingDirectory.c_str(),
            &_Startup, &_Process);
        CloseHandleIfValid(_ChildStdInRead);
        CloseHandleIfValid(_ChildStdOutWrite);
        CloseHandleIfValid(_ChildStdErrWrite);
        if (!_Created)
        {
            StopProcess();
            throw std::runtime_error("failed to start Python template worker");
        }
        Process = _Process.hProcess;
        CloseHandleIfValid(_Process.hThread);
        ReadBuffer.clear();
    }

    void WriteAll(const std::string& Text_)
    {
        std::size_t _Offset = 0;
        while (_Offset < Text_.size())
        {
            DWORD _Written = 0;
            const auto _Chunk = static_cast<DWORD>(std::min<std::size_t>(Text_.size() - _Offset, 64 * 1024));
            if (!WriteFile(StdInWrite, Text_.data() + _Offset, _Chunk, &_Written, nullptr) || _Written == 0)
                throw std::runtime_error("failed to write to Python template worker");
            _Offset += _Written;
        }
        FlushFileBuffers(StdInWrite);
    }

    std::string ReadLine()
    {
        for (;;)
        {
            if (const auto _Newline = ReadBuffer.find('\n'); _Newline != std::string::npos)
            {
                auto _Line = ReadBuffer.substr(0, _Newline);
                ReadBuffer.erase(0, _Newline + 1);
                return _Line;
            }
            char _Buffer[4096];
            DWORD _Read = 0;
            if (!ReadFile(StdOutRead, _Buffer, sizeof(_Buffer), &_Read, nullptr) || _Read == 0)
            {
                std::string _Error;
                if (StdErrRead)
                {
                    DWORD _Available = 0;
                    while (PeekNamedPipe(StdErrRead, nullptr, 0, nullptr, &_Available, nullptr) && _Available > 0)
                    {
                        const auto _Size = static_cast<DWORD>(std::min<DWORD>(_Available, sizeof(_Buffer)));
                        if (!ReadFile(StdErrRead, _Buffer, _Size, &_Read, nullptr) || _Read == 0) break;
                        _Error.append(_Buffer, _Read);
                    }
                }
                throw std::runtime_error(_Error.empty()
                    ? "Python template worker closed its output pipe"
                    : "Python template worker stopped: " + _Error);
            }
            ReadBuffer.append(_Buffer, _Read);
            if (ReadBuffer.size() > 64 * 1024 * 1024)
                throw std::runtime_error("Python template worker response exceeds 64 MiB");
        }
    }

    std::string ReadLineUntilEvent()
    {
        HANDLE _ResponseEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!_ResponseEvent)
            throw std::runtime_error("failed to create Python template response event");
        std::string _Response;
        std::exception_ptr _ReadError;
        std::thread _Reader([this, &_Response, &_ReadError, _ResponseEvent]() {
            try
            {
                _Response = ReadLine();
            }
            catch (...)
            {
                _ReadError = std::current_exception();
            }
            SetEvent(_ResponseEvent);
        });

        const HANDLE _Events[]{ _ResponseEvent, Process };
        const auto _Wait = WaitForMultipleObjects(
            static_cast<DWORD>(std::size(_Events)), _Events, FALSE,
            WaitMilliseconds(Options.RequestTimeout));
        if (_Wait == WAIT_TIMEOUT)
        {
            // Closing a terminated worker's inherited stdout handle releases the
            // synchronous reader.  The reader is joined before any pipe is reused.
            TerminateProcess(Process, 2);
            _Reader.join();
            CloseHandle(_ResponseEvent);
            throw std::runtime_error("Python template worker response deadline exceeded");
        }
        if (_Wait != WAIT_OBJECT_0 && _Wait != WAIT_OBJECT_0 + 1)
        {
            TerminateProcess(Process, 3);
            _Reader.join();
            CloseHandle(_ResponseEvent);
            throw std::runtime_error("failed while waiting for Python template worker events");
        }

        // A process-exit event can race with the final pipe delivery.  Joining is
        // event-driven here: EOF follows closure of the child's inherited handle.
        _Reader.join();
        CloseHandle(_ResponseEvent);
        if (_ReadError) std::rethrow_exception(_ReadError);
        return _Response;
    }

    static void CloseHandleIfValid(HANDLE& Handle_) noexcept
    {
        if (Handle_ && Handle_ != INVALID_HANDLE_VALUE) CloseHandle(Handle_);
        Handle_ = nullptr;
    }

    void StopProcess() noexcept
    {
        CloseHandleIfValid(StdInWrite);
        CloseHandleIfValid(StdOutRead);
        CloseHandleIfValid(StdErrRead);
        CloseHandleIfValid(Process);
        ReadBuffer.clear();
    }

    SPythonTemplateHostOptions Options;
    mutable std::mutex Mutex;
    HANDLE Process = nullptr;
    HANDLE StdInWrite = nullptr;
    HANDLE StdOutRead = nullptr;
    HANDLE StdErrRead = nullptr;
    std::string ReadBuffer;
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
