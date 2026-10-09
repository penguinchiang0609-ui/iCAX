#include "pch.h"
#include "CadDocumentImportService.h"
#include "Services/ServicesHelper.h"
#include <chrono>
#include <string_view>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <objbase.h>

namespace
{
    constexpr std::uintmax_t kMaximumDocumentBytes = 32 * 1024 * 1024;
    constexpr DWORD kConversionTimeoutMs = 30000;
    constexpr std::string_view kConverterVersion = "LibreDWG 0.14.8601";

    class CHandle final
    {
    public:
        explicit CHandle(HANDLE Value_ = nullptr) : Value(Value_) {}
        ~CHandle() { if (Value && Value != INVALID_HANDLE_VALUE) ::CloseHandle(Value); }
        CHandle(const CHandle&) = delete;
        CHandle& operator=(const CHandle&) = delete;
        HANDLE Value;
    };

    std::string ReadBoundedFile(const std::filesystem::path& Path_)
    {
        const auto _Size = std::filesystem::file_size(Path_);
        const auto _Modified = std::filesystem::last_write_time(Path_);
        if (!_Size || _Size > kMaximumDocumentBytes)
            throw std::invalid_argument("CAD图纸文件大小须在32MB内");
        std::ifstream _File(Path_, std::ios::binary);
        std::string _Bytes(static_cast<std::size_t>(_Size), '\0');
        _File.read(_Bytes.data(), static_cast<std::streamsize>(_Bytes.size()));
        if (!_File || std::filesystem::file_size(Path_) != _Size
            || std::filesystem::last_write_time(Path_) != _Modified)
            throw std::runtime_error("读取期间CAD文件发生变化，请重新导入");
        return _Bytes;
    }

    bool IsDxf(std::string_view Bytes_)
    {
        if (Bytes_.starts_with(std::string_view("AutoCAD Binary DXF\r\n\032\0", 22)))
            return Bytes_.size() > 22;
        if (Bytes_.find('\0') != std::string_view::npos) return false;
        if (Bytes_.starts_with("\xef\xbb\xbf")) Bytes_.remove_prefix(3);
        // Validate the document envelope, leaving entity parsing and text
        // codepage interpretation to the neutral DXF consumer.
        std::istringstream _Lines(std::string(Bytes_.substr(0, 65536)));
        std::string _Code, _Value;
        const auto _Trim = [](std::string& Text_) {
            const auto _First = Text_.find_first_not_of(" \t\r\n");
            if (_First == std::string::npos) { Text_.clear(); return; }
            Text_ = Text_.substr(_First, Text_.find_last_not_of(" \t\r\n") - _First + 1);
        };
        while (std::getline(_Lines, _Code) && std::getline(_Lines, _Value))
        {
            _Trim(_Code); _Trim(_Value);
            if (_Code == "999") continue;
            return _Code == "0" && _Value == "SECTION";
        }
        return false;
    }

    std::string Trim(std::string Text_)
    {
        const auto _First = Text_.find_first_not_of(" \t\r\n");
        return _First == std::string::npos ? std::string()
            : Text_.substr(_First, Text_.find_last_not_of(" \t\r\n") - _First + 1);
    }

    std::string Upper(std::string Text_)
    {
        Text_ = Trim(std::move(Text_));
        for (auto& _Character : Text_) _Character = static_cast<char>(std::toupper(static_cast<unsigned char>(_Character)));
        return Text_;
    }

    bool IsTextCode(int Code_)
    {
        return (Code_ >= 0 && Code_ <= 9) || (Code_ >= 100 && Code_ <= 102) || Code_ == 105
            || (Code_ >= 300 && Code_ <= 369) || (Code_ >= 390 && Code_ <= 399)
            || (Code_ >= 410 && Code_ <= 419) || (Code_ >= 430 && Code_ <= 439)
            || (Code_ >= 470 && Code_ <= 481) || Code_ == 999 || (Code_ >= 1000 && Code_ <= 1009);
    }

    template<typename TValue> TValue BinaryValue(std::string_view Bytes_, std::size_t& Offset_)
    {
        if (Offset_ > Bytes_.size() || sizeof(TValue) > Bytes_.size() - Offset_)
            throw std::invalid_argument("二进制DXF数据被截断");
        TValue _Value{}; std::memcpy(&_Value, Bytes_.data() + Offset_, sizeof(_Value));
        Offset_ += sizeof(_Value); return _Value;
    }

    template<typename TValue> std::string NumericText(TValue Value_)
    {
        char _Buffer[128]{};
        std::to_chars_result _Result;
        if constexpr (std::is_floating_point_v<TValue>)
        {
            if (!std::isfinite(Value_)) throw std::invalid_argument("CAD数值不是有限值");
            _Result = std::to_chars(_Buffer, _Buffer + sizeof(_Buffer), Value_,
                std::chars_format::general, std::numeric_limits<TValue>::max_digits10);
        }
        else _Result = std::to_chars(_Buffer, _Buffer + sizeof(_Buffer), Value_);
        if (_Result.ec != std::errc()) throw std::invalid_argument("CAD数值无法解码");
        return {_Buffer, _Result.ptr};
    }

    std::vector<iCAX::CAD::SCadGroup> DecodeGroups(std::string_view Bytes_, bool Binary_)
    {
        std::vector<iCAX::CAD::SCadGroup> _Groups;
        if (!Binary_)
        {
            if (Bytes_.starts_with("\xef\xbb\xbf")) Bytes_.remove_prefix(3);
            std::istringstream _Lines{std::string(Bytes_)};
            std::string _CodeText, _Value;
            while (std::getline(_Lines, _CodeText))
            {
                if (Trim(_CodeText).empty() && _Lines.peek() == EOF) break;
                if (!std::getline(_Lines, _Value)) throw std::invalid_argument("DXF组数据缺少值");
                _CodeText = Trim(_CodeText);
                int _Code = -1;
                const auto _Parsed = std::from_chars(_CodeText.data(), _CodeText.data() + _CodeText.size(), _Code);
                if (_Parsed.ec != std::errc() || _Parsed.ptr != _CodeText.data() + _CodeText.size() || _Code < 0 || _Code > 1071)
                    throw std::invalid_argument("DXF组码无效");
                if (!_Value.empty() && _Value.back() == '\r') _Value.pop_back();
                _Groups.push_back({_Code, std::move(_Value)});
                if (_Groups.size() > 2000000) throw std::invalid_argument("CAD图纸记录过多");
            }
            return _Groups;
        }
        std::size_t _Offset = 22;
        while (_Offset < Bytes_.size())
        {
            const int _Code = BinaryValue<std::uint16_t>(Bytes_, _Offset);
            std::string _Value;
            if ((_Code >= 310 && _Code <= 319) || _Code == 1004)
            {
                const auto _Length = BinaryValue<std::uint8_t>(Bytes_, _Offset);
                if (_Length > Bytes_.size() - _Offset) throw std::invalid_argument("二进制DXF数据被截断");
                static constexpr char _Hex[] = "0123456789ABCDEF";
                for (unsigned _Index = 0; _Index < _Length; ++_Index)
                { const auto _Byte = static_cast<unsigned char>(Bytes_[_Offset++]); _Value += _Hex[_Byte >> 4]; _Value += _Hex[_Byte & 15]; }
            }
            else if (IsTextCode(_Code))
            {
                const auto _End = Bytes_.find('\0', _Offset);
                if (_End == std::string_view::npos) throw std::invalid_argument("二进制DXF文字被截断");
                _Value = Bytes_.substr(_Offset, _End - _Offset); _Offset = _End + 1;
            }
            else if ((_Code >= 10 && _Code <= 59) || (_Code >= 110 && _Code <= 149)
                || (_Code >= 210 && _Code <= 239) || (_Code >= 460 && _Code <= 469) || (_Code >= 1010 && _Code <= 1059))
                _Value = NumericText(BinaryValue<double>(Bytes_, _Offset));
            else if ((_Code >= 60 && _Code <= 79) || (_Code >= 170 && _Code <= 179)
                || (_Code >= 270 && _Code <= 289) || (_Code >= 370 && _Code <= 389)
                || (_Code >= 400 && _Code <= 409) || (_Code >= 1060 && _Code <= 1070))
                _Value = NumericText(BinaryValue<std::int16_t>(Bytes_, _Offset));
            else if ((_Code >= 90 && _Code <= 99) || (_Code >= 420 && _Code <= 429)
                || (_Code >= 440 && _Code <= 459) || _Code == 1071)
                _Value = NumericText(BinaryValue<std::int32_t>(Bytes_, _Offset));
            else if (_Code >= 160 && _Code <= 169) _Value = NumericText(BinaryValue<std::int64_t>(Bytes_, _Offset));
            else if (_Code >= 290 && _Code <= 299)
            { const auto _Boolean = BinaryValue<std::uint8_t>(Bytes_, _Offset);
              if (_Boolean > 1) throw std::invalid_argument("二进制DXF布尔值无效"); _Value = NumericText(_Boolean); }
            else throw std::invalid_argument("二进制DXF组码无效");
            _Groups.push_back({_Code, std::move(_Value)});
            if (_Groups.size() > 2000000) throw std::invalid_argument("CAD图纸记录过多");
            if (_Code == 0 && _Groups.back().Value == "EOF")
            { if (_Offset != Bytes_.size()) throw std::invalid_argument("二进制DXF结束标记后存在数据"); break; }
        }
        return _Groups;
    }

    std::string DecodeText(const std::string& Text_, UINT Codepage_)
    {
        if (Text_.empty()) return {};
        const auto _Flags = Codepage_ == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
        const auto _Count = ::MultiByteToWideChar(Codepage_, _Flags, Text_.data(), static_cast<int>(Text_.size()), nullptr, 0);
        if (!_Count) throw std::invalid_argument("CAD文字编码无效");
        std::wstring _Wide(static_cast<std::size_t>(_Count), L'\0');
        ::MultiByteToWideChar(Codepage_, _Flags, Text_.data(), static_cast<int>(Text_.size()), _Wide.data(), _Count);
        // DXF Unicode escapes are UTF-16 code units, including surrogate pairs.
        std::wstring _Decoded;
        for (std::size_t _Index = 0; _Index < _Wide.size(); ++_Index)
        {
            if (_Index + 6 < _Wide.size() && _Wide[_Index] == L'\\' && _Wide[_Index + 1] == L'U' && _Wide[_Index + 2] == L'+')
            {
                unsigned _Value = 0; bool _Valid = true;
                for (std::size_t _HexIndex = _Index + 3; _HexIndex <= _Index + 6; ++_HexIndex)
                {
                    const wchar_t _Character = _Wide[_HexIndex];
                    const int _Digit = _Character >= L'0' && _Character <= L'9' ? _Character - L'0'
                        : _Character >= L'A' && _Character <= L'F' ? _Character - L'A' + 10
                        : _Character >= L'a' && _Character <= L'f' ? _Character - L'a' + 10 : -1;
                    if (_Digit < 0) { _Valid = false; break; } _Value = (_Value << 4) | _Digit;
                }
                if (_Valid) { _Decoded += static_cast<wchar_t>(_Value); _Index += 6; continue; }
            }
            _Decoded += _Wide[_Index];
        }
        const auto _Bytes = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, _Decoded.data(), static_cast<int>(_Decoded.size()), nullptr, 0, nullptr, nullptr);
        if (!_Bytes) throw std::invalid_argument("CAD Unicode文字无效");
        std::string _Result(static_cast<std::size_t>(_Bytes), '\0');
        ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, _Decoded.data(), static_cast<int>(_Decoded.size()), _Result.data(), _Bytes, nullptr, nullptr);
        return _Result;
    }

    iCAX::CAD::SCadDocument DecodeDocument(const std::string& Bytes_)
    {
        const bool _Binary = Bytes_.starts_with(std::string_view("AutoCAD Binary DXF\r\n\032\0", 22));
        auto _Groups = DecodeGroups(Bytes_, _Binary);
        // Comments may also occur between a header key and its value.
        // Remove them before interpreting encoding or document structure.
        std::erase_if(_Groups, [](const auto& Group_) { return Group_.Code == 999; });
        UINT _Codepage = 1252;
        for (std::size_t _Index = 0; _Index + 1 < _Groups.size(); ++_Index)
        {
            if (_Groups[_Index].Code != 9) continue;
            const auto _Key = Upper(_Groups[_Index].Value);
            const auto _Value = Upper(_Groups[_Index + 1].Value);
            if (_Key == "$DWGCODEPAGE")
            {
                if (_Value == "UTF8" || _Value == "UTF-8") { _Codepage = CP_UTF8; continue; }
                const auto _Prefix = _Value.starts_with("ANSI_") ? 5 : _Value.starts_with("DOS") ? 3 : 0;
                unsigned _Page = 0;
                const auto _Parsed = std::from_chars(_Value.data() + _Prefix, _Value.data() + _Value.size(), _Page);
                if (!_Prefix || _Parsed.ec != std::errc() || _Parsed.ptr != _Value.data() + _Value.size() || !::IsValidCodePage(_Page))
                    throw std::invalid_argument("CAD图纸声明了不支持的文字编码");
                _Codepage = _Page;
            }
        }
        for (std::size_t _Index = 0; _Index + 1 < _Groups.size(); ++_Index)
            if (_Groups[_Index].Code == 9 && Upper(_Groups[_Index].Value) == "$ACADVER")
            {
                const auto _Version = Upper(_Groups[_Index + 1].Value);
                unsigned _Number = 0;
                if (_Version.starts_with("AC"))
                { const auto _Parsed = std::from_chars(_Version.data() + 2, _Version.data() + _Version.size(), _Number);
                  if (_Parsed.ec == std::errc() && _Parsed.ptr == _Version.data() + _Version.size() && _Number >= 1021) _Codepage = CP_UTF8; }
            }
        for (auto& _Group : _Groups) if (IsTextCode(_Group.Code)) _Group.Value = DecodeText(_Group.Value, _Codepage);
        iCAX::CAD::SCadDocument _Document; _Document.SourceFormat = _Binary ? "binary" : "ascii";
        std::string _Section, _HeaderKey;
        iCAX::CAD::SCadRecord* _Record = nullptr;
        bool _EOF = false;
        for (std::size_t _Index = 0; _Index < _Groups.size(); ++_Index)
        {
            auto& _Group = _Groups[_Index];
            const auto _Value = _Group.Code == 0 ? Upper(_Group.Value) : std::string();
            if (_Group.Code == 0 && _Value == "SECTION")
            {
                if (!_Section.empty() || _Index + 1 >= _Groups.size() || _Groups[_Index + 1].Code != 2)
                    throw std::invalid_argument("CAD文档段结构无效");
                _Section = Upper(_Groups[++_Index].Value); _HeaderKey.clear(); _Record = nullptr;
                if (_Section.empty()) throw std::invalid_argument("CAD文档段名称为空");
                if (_Section != "HEADER") _Document.Sections.try_emplace(_Section);
            }
            else if (_Group.Code == 0 && _Value == "ENDSEC")
            { if (_Section.empty()) throw std::invalid_argument("CAD文档段结构无效"); _Section.clear(); _Record = nullptr; }
            else if (_Group.Code == 0 && _Value == "EOF")
            {
                if (!_Section.empty()) throw std::invalid_argument("CAD文档段未结束");
                for (std::size_t _After = _Index + 1; _After < _Groups.size(); ++_After)
                    if (_Groups[_After].Code != 999) throw std::invalid_argument("CAD结束标记后存在无效记录");
                _EOF = true; break;
            }
            else if (_Section == "HEADER")
            {
                if (_Group.Code == 9) { _HeaderKey = Upper(_Group.Value); _Document.Header.try_emplace(_HeaderKey); }
                else if (!_HeaderKey.empty()) _Document.Header[_HeaderKey].push_back(std::move(_Group));
                else throw std::invalid_argument("CAD文档头变量无效");
            }
            else if (!_Section.empty())
            {
                if (_Group.Code == 0)
                { auto& _Records = _Document.Sections[_Section]; _Records.push_back({_Value, {}}); _Record = &_Records.back(); }
                else if (_Record) _Record->Values.push_back(std::move(_Group));
                else if (_Section == "THUMBNAILIMAGE")
                { auto& _Records = _Document.Sections[_Section]; _Records.push_back({_Section, {std::move(_Group)}}); _Record = &_Records.back(); }
                else throw std::invalid_argument("CAD文档记录无效");
            }
            else if (_Group.Code != 999) throw std::invalid_argument("CAD文档段外存在无效记录");
        }
        if (!_EOF || !_Document.Sections.contains("ENTITIES")) throw std::invalid_argument("CAD文档缺少实体段或结束标记");
        return _Document;
    }

    std::filesystem::path ConverterPath()
    {
        HMODULE _Module = nullptr;
        if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ConverterPath), &_Module))
            throw std::runtime_error("无法确定CAD导入模块位置");
        std::wstring _Path(32768, L'\0');
        const auto _Length = ::GetModuleFileNameW(_Module, _Path.data(), static_cast<DWORD>(_Path.size()));
        if (!_Length || _Length >= _Path.size()) throw std::runtime_error("CAD导入模块路径无效");
        _Path.resize(_Length);
        const auto _Converter = std::filesystem::path(_Path).parent_path()
            / "runtime/cad/libredwg/dwg2dxf.exe";
        for (const auto* _Required : {L"dwg2dxf.exe", L"libredwg-0.dll", L"libiconv-2.dll"})
            if (!std::filesystem::is_regular_file(_Converter.parent_path() / _Required))
                throw std::runtime_error("DWG导入组件未完整部署，请修复程序安装");
        return _Converter;
    }

    class CTemporaryDirectory final
    {
    public:
        CTemporaryDirectory()
        {
            GUID _ID{};
            if (FAILED(::CoCreateGuid(&_ID))) throw std::runtime_error("无法创建CAD转换临时目录");
            wchar_t _Name[64]{};
            ::StringFromGUID2(_ID, _Name, 64);
            Path = std::filesystem::temp_directory_path() / (std::wstring(L"icax-cad-") + _Name);
            if (!std::filesystem::create_directory(Path)) throw std::runtime_error("无法创建CAD转换临时目录");
        }
        ~CTemporaryDirectory()
        {
            // Only the three names owned by this invocation are ever removed.
            std::error_code _Error;
            for (const auto* _Name : {L"input.dwg", L"output.dxf", L"converter.log"})
                std::filesystem::remove(Path / _Name, _Error);
            std::filesystem::remove(Path, _Error);
        }
        std::filesystem::path Path;
    };

    std::string ConvertDwg(const std::string& Bytes_)
    {
        const auto _Converter = ConverterPath();
        CTemporaryDirectory _Temporary;
        {
            std::ofstream _Input(_Temporary.Path / "input.dwg", std::ios::binary);
            _Input.write(Bytes_.data(), static_cast<std::streamsize>(Bytes_.size()));
            _Input.close();
            if (!_Input) throw std::runtime_error("DWG临时文件写入失败");
        }
        SECURITY_ATTRIBUTES _Security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        CHandle _Log(::CreateFileW((_Temporary.Path / "converter.log").c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &_Security, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr));
        CHandle _Null(::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &_Security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        CHandle _Job(::CreateJobObjectW(nullptr, nullptr));
        if (_Log.Value == INVALID_HANDLE_VALUE || _Null.Value == INVALID_HANDLE_VALUE || !_Job.Value)
            throw std::runtime_error("DWG转换环境创建失败");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION _Limits{};
        _Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            | JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        _Limits.BasicLimitInformation.ActiveProcessLimit = 1;
        _Limits.ProcessMemoryLimit = 1024ULL * 1024 * 1024;
        if (!::SetInformationJobObject(_Job.Value, JobObjectExtendedLimitInformation, &_Limits, sizeof(_Limits)))
            throw std::runtime_error("DWG转换资源限制设置失败");

        SIZE_T _AttributeBytes = 0;
        ::InitializeProcThreadAttributeList(nullptr, 1, 0, &_AttributeBytes);
        std::vector<unsigned char> _AttributeStorage(_AttributeBytes);
        auto* _Attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(_AttributeStorage.data());
        if (!::InitializeProcThreadAttributeList(_Attributes, 1, 0, &_AttributeBytes))
            throw std::runtime_error("DWG转换进程设置失败");
        struct SAttributeCleanup final { LPPROC_THREAD_ATTRIBUTE_LIST Value; ~SAttributeCleanup() { ::DeleteProcThreadAttributeList(Value); } } _Cleanup{_Attributes};
        HANDLE _Inherited[]{_Log.Value, _Null.Value};
        if (!::UpdateProcThreadAttribute(_Attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            _Inherited, sizeof(_Inherited), nullptr, nullptr))
            throw std::runtime_error("DWG转换句柄设置失败");
        STARTUPINFOEXW _Startup{};
        _Startup.StartupInfo.cb = sizeof(_Startup);
        _Startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        _Startup.StartupInfo.wShowWindow = SW_HIDE;
        _Startup.StartupInfo.hStdOutput = _Log.Value;
        _Startup.StartupInfo.hStdError = _Log.Value;
        _Startup.StartupInfo.hStdInput = _Null.Value;
        _Startup.lpAttributeList = _Attributes;
        PROCESS_INFORMATION _Process{};
        // Relative ASCII file names work even when the user's temporary path
        // contains Unicode. CreateProcessW sets that directory directly.
        std::wstring _Command = L"\"" + _Converter.wstring() + L"\" -o output.dxf input.dwg";
        if (!::CreateProcessW(_Converter.c_str(), _Command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
            nullptr, _Temporary.Path.c_str(), &_Startup.StartupInfo, &_Process))
            throw std::runtime_error("DWG转换组件无法启动，请修复程序安装");
        CHandle _ProcessHandle(_Process.hProcess), _Thread(_Process.hThread);
        struct SProcessCleanup final
        {
            HANDLE Value;
            ~SProcessCleanup()
            {
                if (::WaitForSingleObject(Value, 0) == WAIT_TIMEOUT)
                { ::TerminateProcess(Value, 1); ::WaitForSingleObject(Value, INFINITE); }
            }
        } _ProcessCleanup{_ProcessHandle.Value};
        if (!::AssignProcessToJobObject(_Job.Value, _ProcessHandle.Value))
        {
            ::TerminateProcess(_ProcessHandle.Value, 1);
            ::WaitForSingleObject(_ProcessHandle.Value, INFINITE);
            throw std::runtime_error("DWG转换进程隔离失败");
        }
        if (::ResumeThread(_Thread.Value) == static_cast<DWORD>(-1))
            throw std::runtime_error("DWG转换组件启动失败");
        const auto _Start = std::chrono::steady_clock::now();
        while (::WaitForSingleObject(_ProcessHandle.Value, 50) == WAIT_TIMEOUT)
        {
            std::error_code _Error;
            const auto _Output = _Temporary.Path / "output.dxf";
            const bool _TooLarge = std::filesystem::exists(_Output, _Error)
                && std::filesystem::file_size(_Output, _Error) > kMaximumDocumentBytes;
            const auto _LogSize = std::filesystem::file_size(_Temporary.Path / "converter.log", _Error);
            if (_TooLarge || (!_Error && _LogSize > 4 * 1024 * 1024)
                || std::chrono::steady_clock::now() - _Start > std::chrono::milliseconds(kConversionTimeoutMs))
            {
                ::TerminateJobObject(_Job.Value, 1);
                ::WaitForSingleObject(_ProcessHandle.Value, INFINITE);
                throw std::runtime_error(_TooLarge ? "DWG转换后的图纸超过32MB" : "DWG转换超时或图纸过于复杂");
            }
        }
        DWORD _ExitCode = 1;
        if (!::GetExitCodeProcess(_ProcessHandle.Value, &_ExitCode) || _ExitCode != 0
            || !std::filesystem::is_regular_file(_Temporary.Path / "output.dxf"))
            throw std::invalid_argument("DWG图纸损坏或包含无法读取的数据");
        auto _Result = ReadBoundedFile(_Temporary.Path / "output.dxf");
        if (!IsDxf(_Result)) throw std::runtime_error("DWG转换未生成有效的DXF文档");
        return _Result;
    }

    class CCadDocumentImportService final : public iCAX::CAD::ICadDocumentImportService
    {
        AUTO_REGIST_SERVICE(iCAX::CAD::ICadDocumentImportService, CCadDocumentImportService)
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        iCAX::CAD::SCadExchangeDocument Load(const std::filesystem::path& SourcePath_) const override
        {
            auto _Extension = SourcePath_.extension().wstring();
            std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), ::towlower);
            if (!SourcePath_.is_absolute() || !std::filesystem::is_regular_file(SourcePath_)
                || (_Extension != L".dwg" && _Extension != L".dxf"))
                throw std::invalid_argument("请选择有效的DWG或DXF图纸文件");
            auto _Bytes = ReadBoundedFile(SourcePath_);
            iCAX::CAD::SCadExchangeDocument _Document;
            _Document.SourcePath = SourcePath_;
            _Document.SourceFormat = _Extension == L".dwg" ? "dwg" : "dxf";
            if (_Document.SourceFormat == "dxf")
            {
                if (!IsDxf(_Bytes)) throw std::invalid_argument("文件内容不是有效的DXF图纸");
                _Document.Document = DecodeDocument(_Bytes);
            }
            else
            {
                if (_Bytes.size() < 64 || !_Bytes.starts_with("AC10")
                    || !std::isdigit(static_cast<unsigned char>(_Bytes[4]))
                    || !std::isdigit(static_cast<unsigned char>(_Bytes[5])))
                    throw std::invalid_argument("文件内容不是有效的DWG图纸");
                _Document.SourceVersion = _Bytes.substr(0, 6);
                _Document.Document = DecodeDocument(ConvertDwg(_Bytes));
                _Document.ConverterVersion = kConverterVersion;
            }
            return _Document;
        }
    };
}
