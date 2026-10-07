#include "pch.h"
#include "BatchExcelAutomation.h"
#include "TemplateRuntime/StandardJsonCodec.h"
#include "Data/UUID.h"
#include <array>
#include <bcrypt.h>
#include <chrono>
#include <fstream>
#include <thread>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    using iCAX::TemplateRuntime::CStandardJsonCodec;
    constexpr long long kStableMilliseconds = 2000;

    std::string Text(const ObjectMap& Value_, const char* Key_)
    {
        const auto _It = Value_.find(Key_);
        return _It != Value_.end() && _It->second.Is<std::string>() ? _It->second.To<std::string>() : "";
    }
    std::filesystem::path Path(const std::string& Value_)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(Value_.data()), Value_.size()));
    }
    std::string PathText(const std::filesystem::path& Value_)
    {
        const auto _Text = Value_.u8string();
        return { reinterpret_cast<const char*>(_Text.data()), _Text.size() };
    }
    long long Now()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    std::string UniqueID()
    {
        const auto _ID = iCAX::Data::GenerateNewUUID();
        std::ostringstream _Text; _Text << _ID; return _Text.str();
    }
    std::string PathKey(const std::filesystem::path& Value_)
    {
        auto _Value = std::filesystem::weakly_canonical(Value_).native();
        std::transform(_Value.begin(), _Value.end(), _Value.begin(), towlower);
        return PathText(std::filesystem::path(_Value));
    }
    class CStateBusy final : public std::runtime_error
    {
    public:
        CStateBusy() : std::runtime_error("Excel 自动处理记录正在使用，请稍后重试") {}
    };
    struct CStateLock final
    {
        HANDLE File = INVALID_HANDLE_VALUE;
        explicit CStateLock(const std::filesystem::path& Root_)
        {
            std::filesystem::create_directories(Root_);
            for (int _Attempt = 0; _Attempt < 20; ++_Attempt)
            {
                File = CreateFileW((Root_ / L"state.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                    0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (File != INVALID_HANDLE_VALUE) return;
                if (GetLastError() != ERROR_SHARING_VIOLATION)
                    throw std::runtime_error("无法访问 Excel 自动处理记录");
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            throw CStateBusy();
        }
        ~CStateLock() { if (File != INVALID_HANDLE_VALUE) CloseHandle(File); }
    };
    ObjectMap DefaultSettings()
    {
        return { {"enabled", false}, {"inputDirectory", std::string()},
            {"tempDirectory", std::string()}, {"outputDirectory", std::string()} };
    }
    ObjectMap Load(const std::filesystem::path& Root_)
    {
        const auto _File = Root_ / L"state.json";
        if (!std::filesystem::exists(_File))
            return { {"settings", DefaultSettings()}, {"observations", ObjectMap{}}, {"records", ObjectMap{}} };
        std::ifstream _Stream(_File, std::ios::binary);
        if (!_Stream) throw std::runtime_error("无法读取 Excel 自动处理记录");
        const std::string _Bytes((std::istreambuf_iterator<char>(_Stream)), {});
        return CStandardJsonCodec::Parse(_Bytes).To<ObjectMap>();
    }
    void Save(const std::filesystem::path& Root_, const ObjectMap& State_)
    {
        const auto _Temporary = Root_ / Path("state-" + UniqueID() + ".tmp");
        const auto _Bytes = CStandardJsonCodec::Serialize(State_);
        {
            std::ofstream _Stream(_Temporary, std::ios::binary | std::ios::trunc);
            _Stream.write(_Bytes.data(), static_cast<std::streamsize>(_Bytes.size()));
            _Stream.flush();
            if (!_Stream) throw std::runtime_error("无法写入 Excel 自动处理记录");
        }
        if (!MoveFileExW(_Temporary.c_str(), (Root_ / L"state.json").c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(_Temporary);
            throw std::runtime_error("无法保存 Excel 自动处理记录");
        }
    }
    ObjectMap& Map(ObjectMap& Value_, const char* Key_)
    {
        return std::get<ObjectMap>(Value_.at(Key_).m_Value);
    }
    std::string Stamp(const std::filesystem::path& File_)
    {
        return std::to_string(std::filesystem::file_size(File_)) + ":"
            + std::to_string(std::filesystem::last_write_time(File_).time_since_epoch().count());
    }
    std::string Digest(const std::filesystem::path& File_)
    {
        BCRYPT_ALG_HANDLE _Algorithm = nullptr;
        BCRYPT_HASH_HANDLE _Hash = nullptr;
        struct Cleanup final
        {
            BCRYPT_ALG_HANDLE& Algorithm; BCRYPT_HASH_HANDLE& Hash;
            ~Cleanup() { if (Hash) BCryptDestroyHash(Hash); if (Algorithm) BCryptCloseAlgorithmProvider(Algorithm, 0); }
        } _Cleanup{_Algorithm, _Hash};
        if (BCryptOpenAlgorithmProvider(&_Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("无法计算 Excel 文件指纹");
        DWORD _ObjectSize = 0, _Written = 0;
        if (BCryptGetProperty(_Algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&_ObjectSize), sizeof(_ObjectSize), &_Written, 0) < 0)
            throw std::runtime_error("无法计算 Excel 文件指纹");
        std::vector<unsigned char> _Object(_ObjectSize);
        if (BCryptCreateHash(_Algorithm, &_Hash, _Object.data(), _ObjectSize, nullptr, 0, 0) < 0)
            throw std::runtime_error("无法计算 Excel 文件指纹");
        std::ifstream _Stream(File_, std::ios::binary);
        if (!_Stream) throw std::runtime_error("Excel 文件暂时不可读取");
        std::array<unsigned char, 65536> _Buffer{};
        while (_Stream)
        {
            _Stream.read(reinterpret_cast<char*>(_Buffer.data()), _Buffer.size());
            if (_Stream.gcount() && BCryptHashData(_Hash, _Buffer.data(), static_cast<ULONG>(_Stream.gcount()), 0) < 0)
                throw std::runtime_error("无法计算 Excel 文件指纹");
        }
        if (!_Stream.eof()) throw std::runtime_error("Excel 文件读取失败");
        std::array<unsigned char, 32> _Result{};
        if (BCryptFinishHash(_Hash, _Result.data(), static_cast<ULONG>(_Result.size()), 0) < 0)
            throw std::runtime_error("无法计算 Excel 文件指纹");
        std::ostringstream _Hex;
        for (const auto _Byte : _Result) _Hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(_Byte);
        return _Hex.str();
    }
    std::string ProcessStart(HANDLE Process_)
    {
        FILETIME _Creation{}, _Exit{}, _Kernel{}, _User{};
        if (!GetProcessTimes(Process_, &_Creation, &_Exit, &_Kernel, &_User)) return "";
        return std::to_string((static_cast<unsigned long long>(_Creation.dwHighDateTime) << 32) | _Creation.dwLowDateTime);
    }
    bool OwnerAlive(const ObjectMap& Record_)
    {
        const auto _ID = std::stoul(Text(Record_, "ownerProcess"));
        const auto _Handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, static_cast<DWORD>(_ID));
        if (!_Handle) return GetLastError() == ERROR_ACCESS_DENIED;
        const auto _Alive = WaitForSingleObject(_Handle, 0) == WAIT_TIMEOUT
            && ProcessStart(_Handle) == Text(Record_, "ownerStart");
        CloseHandle(_Handle); return _Alive;
    }
    void RecoverInterrupted(ObjectMap& State_)
    {
        auto& _Records = Map(State_, "records");
        ObjectMap _Abandoned;
        for (auto _It = _Records.begin(); _It != _Records.end();)
        {
            auto& _Record = std::get<ObjectMap>(_It->second.m_Value);
            const auto _Status = Text(_Record, "status");
            if ((_Status == "claimed" || _Status == "archiving") && !OwnerAlive(_Record))
            {
                // A persisted move intent may have stopped before moving anything.
                // No scene work has started at this stage, so leave the original
                // eligible for a new claim while retaining any partial temp file.
                const auto _Original = Path(Text(_Record, "originalSourcePath"));
                std::error_code _Error;
                const auto _RetryMove = Text(_Record, "stage") == "moving"
                    && std::filesystem::is_regular_file(_Original, _Error);
                _Record["status"] = std::string("failed");
                _Record["error"] = std::string(_RetryMove
                    ? "上次移动 Excel 被中断；输入文件将重新领取"
                    : "上次自动处理被中断；该文件版本不会重复创建产品");
                _Record["completedAt"] = std::to_string(Now());
                _Record["reportPending"] = true;
                if (_RetryMove)
                {
                    _Abandoned[_It->first + "|abandoned|" + UniqueID()] = _Record;
                    _It = _Records.erase(_It);
                    continue;
                }
            }
            ++_It;
        }
        for (auto& [_Key, _Record] : _Abandoned) _Records[_Key] = std::move(_Record);
    }
    bool Enabled(const ObjectMap& Settings_)
    {
        return Settings_.at("enabled").To<bool>();
    }
    std::filesystem::path CheckedSource(const ObjectMap& Settings_, const std::string& Source_)
    {
        const auto _Input = Path(Text(Settings_, "inputDirectory"));
        const auto _Source = std::filesystem::weakly_canonical(Path(Source_));
        auto _Extension = _Source.extension().string();
        std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), tolower);
        if (!_Source.is_absolute() || PathKey(_Source.parent_path()) != PathKey(_Input)
            || _Extension != ".xlsx" || PathText(_Source.filename()).starts_with("~$"))
            throw std::invalid_argument("Excel 文件不属于当前侦听目录");
        return _Source;
    }
    std::string SafeStem(const std::filesystem::path& Source_)
    {
        auto _Text = Source_.stem().native();
        for (auto& _Character : _Text)
            if (_Character < 32 || std::wstring(L"<>:\"/\\|?*").find(_Character) != std::wstring::npos) _Character = L'_';
        while (!_Text.empty() && (_Text.back() == L' ' || _Text.back() == L'.')) _Text.pop_back();
        if (_Text.size() > 60) _Text.resize(60);
        return PathText(std::filesystem::path(_Text.empty() ? L"Excel" : _Text));
    }
    bool ContainsDirectory(const std::filesystem::path& Parent_, const std::filesystem::path& Child_)
    {
        const auto _Parent = Path(PathKey(Parent_));
        const auto _Child = Path(PathKey(Child_));
        auto _ParentIt = _Parent.begin(), _ChildIt = _Child.begin();
        for (; _ParentIt != _Parent.end(); ++_ParentIt, ++_ChildIt)
            if (_ChildIt == _Child.end() || *_ParentIt != *_ChildIt) return false;
        return true;
    }
    void CheckDistinctDirectories(const std::filesystem::path& Input_,
        const std::filesystem::path& Temp_, const std::filesystem::path& Output_)
    {
        const std::array<std::filesystem::path, 3> _Directories{Input_, Temp_, Output_};
        for (size_t _A = 0; _A < _Directories.size(); ++_A)
            for (size_t _B = _A + 1; _B < _Directories.size(); ++_B)
                if (!_Directories[_A].empty() && !_Directories[_B].empty()
                    && (ContainsDirectory(_Directories[_A], _Directories[_B])
                        || ContainsDirectory(_Directories[_B], _Directories[_A])))
                    throw std::invalid_argument("输入、临时和输出目录不能相同或互相包含");
    }
    struct CWorkbookReadLock final
    {
        HANDLE File = INVALID_HANDLE_VALUE;
        explicit CWorkbookReadLock(const std::filesystem::path& Source_)
        {
            // Deny writers throughout fingerprinting and relocation. Readers may
            // coexist, but their own sharing flags must also allow the move.
            File = CreateFileW(Source_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (File == INVALID_HANDLE_VALUE)
                throw std::runtime_error("Excel 文件仍在写入或使用中，请等待下一次扫描");
        }
        ~CWorkbookReadLock() { if (File != INVALID_HANDLE_VALUE) CloseHandle(File); }
    };
    void MoveWorkbook(const std::filesystem::path& Source_, const std::filesystem::path& Temp_)
    {
        if (!MoveFileExW(Source_.c_str(), Temp_.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("无法将 Excel 移到临时目录；输入文件已保留，请等待重试");
        // Cross-volume moves may copy successfully but fail to remove a locked
        // original. Such a result must never authorize creation of scene objects.
        if (std::filesystem::exists(Source_) || !std::filesystem::is_regular_file(Temp_))
            throw std::runtime_error("Excel 尚未完整移到临时目录；不会开始处理，请等待重试");
    }
    ObjectMap MoveRecord(const std::filesystem::path& Source_, const std::filesystem::path& Temp_,
        const std::string& Token_, const char* Status_)
    {
        return {{"fileToken", Token_}, {"sourcePath", PathText(Temp_)}, {"originalSourcePath", PathText(Source_)},
            {"status", std::string(Status_)}, {"stage", std::string("moving")},
            {"ownerProcess", std::to_string(GetCurrentProcessId())},
            {"ownerStart", ProcessStart(GetCurrentProcess())}, {"startedAt", std::to_string(Now())}};
    }
    void ArchiveDuplicate(const std::filesystem::path& Root_, ObjectMap& State_, const ObjectMap& Settings_,
        const std::filesystem::path& Source_, const std::string& RecordKey_, const std::string& Token_,
        const std::string& Stamp_)
    {
        CWorkbookReadLock _ReadLock(Source_);
        if (Stamp(Source_) != Stamp_ || Digest(Source_) != Token_)
            throw std::invalid_argument("Excel 文件正在变化，请等待下一次扫描");
        const auto _JobID = UniqueID();
        const auto _JobDirectory = Path(Text(Settings_, "tempDirectory")) / Path("duplicate-" + _JobID);
        std::filesystem::create_directories(_JobDirectory);
        const auto _Temp = _JobDirectory / Source_.filename();
        const auto _ArchiveKey = RecordKey_ + "|duplicate|" + _JobID;
        auto _Record = MoveRecord(Source_, _Temp, Token_, "archiving");
        _Record["duplicateOf"] = RecordKey_;
        auto& _Records = Map(State_, "records");
        _Records[_ArchiveKey] = _Record;
        Save(Root_, State_);
        try
        {
            MoveWorkbook(Source_, _Temp);
            _Record["stage"] = std::string("archived");
            if (Digest(_Temp) != Token_) throw std::invalid_argument("移入临时目录的 Excel 指纹发生变化；不会重复创建产品");
            _Record["status"] = std::string("duplicate");
            _Record["completedAt"] = std::to_string(Now());
            _Records[_ArchiveKey] = _Record;
            Map(State_, "observations").erase(PathKey(Source_));
            Save(Root_, State_);
        }
        catch (const std::exception& _Exception)
        {
            _Record["status"] = std::string("failed");
            _Record["error"] = std::string(_Exception.what());
            _Record["completedAt"] = std::to_string(Now());
            _Records[_ArchiveKey] = _Record;
            Save(Root_, State_);
            throw;
        }
    }
}

iCAX::TubeDesigner::CBatchExcelAutomation::CBatchExcelAutomation(std::filesystem::path UserDataDirectory_)
{
    if (UserDataDirectory_.empty()) throw std::runtime_error("应用未提供用户数据目录");
    m_Root = std::move(UserDataDirectory_) / L"icax.tube-designer" / L"batch-excel-automation";
}

ObjectMap iCAX::TubeDesigner::CBatchExcelAutomation::GetSettings() const
{
    CStateLock _Lock(m_Root); auto _State = Load(m_Root);
    RecoverInterrupted(_State); Save(m_Root, _State);
    return { {"settings", _State.at("settings")} };
}

ObjectMap iCAX::TubeDesigner::CBatchExcelAutomation::SaveSettings(const ObjectMap& Settings_) const
{
    const auto _Enabled = Settings_.contains("enabled") && Settings_.at("enabled").Is<bool>()
        ? Settings_.at("enabled").To<bool>() : false;
    auto _Input = Path(Text(Settings_, "inputDirectory"));
    auto _Temp = Path(Text(Settings_, "tempDirectory"));
    auto _Output = Path(Text(Settings_, "outputDirectory"));
    if (_Enabled && (_Input.empty() || _Temp.empty() || _Output.empty()))
        throw std::invalid_argument("启用自动处理需要设置输入、临时和输出目录");
    for (const auto& _Directory : {_Input, _Temp, _Output})
        if (!_Directory.empty() && !_Directory.is_absolute()) throw std::invalid_argument("Excel 自动处理目录必须是绝对路径");
    if (!_Input.empty())
    {
        if (_Enabled && !std::filesystem::is_directory(_Input)) throw std::invalid_argument("侦听目录不存在");
        _Input = std::filesystem::weakly_canonical(_Input);
    }
    if (!_Temp.empty()) _Temp = std::filesystem::weakly_canonical(_Temp);
    if (!_Output.empty()) _Output = std::filesystem::weakly_canonical(_Output);
    CheckDistinctDirectories(_Input, _Temp, _Output);
    if (_Enabled)
    {
        std::filesystem::create_directories(_Temp);
        std::filesystem::create_directories(_Output);
    }
    CStateLock _Lock(m_Root); auto _State = Load(m_Root);
    _State["settings"] = ObjectMap{{"enabled", _Enabled}, {"inputDirectory", PathText(_Input)},
        {"tempDirectory", PathText(_Temp)}, {"outputDirectory", PathText(_Output)}};
    Save(m_Root, _State); return {{"settings", _State.at("settings")}};
}

ObjectMap iCAX::TubeDesigner::CBatchExcelAutomation::Scan() const
{
    CStateLock _Lock(m_Root); auto _State = Load(m_Root); RecoverInterrupted(_State);
    const auto _Settings = _State.at("settings").To<ObjectMap>();
    VariantArray _Files, _Errors;
    for (auto& [_Key, _Value] : Map(_State, "records"))
    {
        auto& _Record = std::get<ObjectMap>(_Value.m_Value);
        if (_Record.contains("reportPending") && _Record.at("reportPending").To<bool>())
        {
            _Errors.emplace_back(ObjectMap{{"sourcePath", Text(_Record, "sourcePath")}, {"error", Text(_Record, "error")}});
            _Record["reportPending"] = false;
        }
    }
    if (Enabled(_Settings))
    {
        if (Text(_Settings, "tempDirectory").empty()) throw std::invalid_argument("请先设置 Excel 自动处理的临时目录");
        auto& _Observations = Map(_State, "observations");
        const auto& _Records = Map(_State, "records");
        std::error_code _Error;
        std::filesystem::directory_iterator _Entries(Path(Text(_Settings, "inputDirectory")), _Error);
        if (_Error) throw std::runtime_error("无法读取 Excel 侦听目录");
        for (const auto& _Entry : _Entries)
        {
            const auto _Source = _Entry.path();
            auto _Extension = _Source.extension().string();
            std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), tolower);
            if (_Extension != ".xlsx" || PathText(_Source.filename()).starts_with("~$") || !_Entry.is_regular_file() || _Entry.is_symlink()) continue;
            try
            {
                const auto _Key = PathKey(_Source); const auto _Stamp = Stamp(_Source);
                auto _It = _Observations.find(_Key);
                if (_It == _Observations.end() || Text(_It->second.To<ObjectMap>(), "stamp") != _Stamp)
                {
                    _Observations[_Key] = ObjectMap{{"stamp", _Stamp}, {"seenAt", std::to_string(Now())}};
                    continue;
                }
                auto _Observation = _It->second.To<ObjectMap>();
                const auto _Elapsed = Now() - std::stoll(Text(_Observation, "seenAt"));
                if (_Elapsed < 0) { _Observation["seenAt"] = std::to_string(Now()); _It->second = _Observation; continue; }
                if (_Elapsed < kStableMilliseconds) continue;
                const auto _Token = Digest(_Source);
                if (Stamp(_Source) != _Stamp) { _Observations.erase(_Key); continue; }
                if (const auto _Record = _Records.find(_Key + "|" + _Token); _Record != _Records.end()
                    && Text(_Record->second.To<ObjectMap>(), "fileToken") == _Token)
                {
                    ArchiveDuplicate(m_Root, _State, _Settings, _Source, _Key + "|" + _Token, _Token, _Stamp);
                    continue;
                }
                _Observation["fileToken"] = _Token; _It->second = _Observation;
                _Files.emplace_back(ObjectMap{{"sourcePath", PathText(_Source)}, {"sourceName", PathText(_Source.filename())}, {"fileToken", _Token}});
            }
            catch (const std::exception& _Exception)
            {
                _Errors.emplace_back(ObjectMap{{"sourcePath", PathText(_Source)}, {"error", std::string(_Exception.what())}});
            }
        }
    }
    Save(m_Root, _State);
    return {{"settings", _Settings}, {"files", std::move(_Files)}, {"errors", std::move(_Errors)}};
}

ObjectMap iCAX::TubeDesigner::CBatchExcelAutomation::Claim(const ObjectMap& Request_, const TWorkbookValidator& Validate_) const
{
    std::unique_ptr<CStateLock> _Lock;
    try { _Lock = std::make_unique<CStateLock>(m_Root); }
    catch (const CStateBusy&)
    {
        return {{"claimed", false}, {"reason", std::string("其他窗口正在领取 Excel 自动处理任务")}};
    }
    auto _State = Load(m_Root); RecoverInterrupted(_State);
    const auto _Settings = _State.at("settings").To<ObjectMap>();
    if (!Enabled(_Settings)) throw std::invalid_argument("Excel 自动处理尚未启用");
    if (Text(_Settings, "tempDirectory").empty()) throw std::invalid_argument("请先设置 Excel 自动处理的临时目录");
    const auto _Source = CheckedSource(_Settings, Text(Request_, "sourcePath"));
    const auto _Key = PathKey(_Source), _Token = Text(Request_, "fileToken");
    const auto _RecordKey = _Key + "|" + _Token;
    auto& _Records = Map(_State, "records"); const auto& _Observations = Map(_State, "observations");
    if (const auto _Record = _Records.find(_RecordKey); _Record != _Records.end()
        && Text(_Record->second.To<ObjectMap>(), "fileToken") == _Token)
        return {{"claimed", false}, {"reason", std::string("该文件版本已经处理或正在处理")}};
    std::error_code _SourceError;
    if (!std::filesystem::is_regular_file(_Source, _SourceError))
        return {{"claimed", false}, {"reason", std::string("Excel 输入文件已移走或不存在")}};
    if (_Token.size() != 64 || !_Observations.contains(_Key)) throw std::invalid_argument("Excel 文件尚未稳定");
    const auto _Observation = _Observations.at(_Key).To<ObjectMap>();
    if (Text(_Observation, "fileToken") != _Token || Text(_Observation, "stamp") != Stamp(_Source)
        || Now() - std::stoll(Text(_Observation, "seenAt")) < kStableMilliseconds)
        return {{"claimed", false}, {"error", std::string("Excel 文件正在变化，请等待下一次扫描")},
            {"sourcePath", PathText(_Source)}, {"originalSourcePath", PathText(_Source)}, {"fileToken", _Token}};
    const auto _JobID = UniqueID();
    const auto _JobDirectory = Path(Text(_Settings, "tempDirectory")) / Path(_JobID);
    const auto _Temp = _JobDirectory / _Source.filename();
    std::unique_ptr<CWorkbookReadLock> _ReadLock;
    try
    {
        _ReadLock = std::make_unique<CWorkbookReadLock>(_Source);
        if (Stamp(_Source) != Text(_Observation, "stamp") || Digest(_Source) != _Token)
            throw std::invalid_argument("Excel 文件正在变化，请等待下一次扫描");
        std::filesystem::create_directories(_JobDirectory);
    }
    catch (const std::exception& _Exception)
    {
        return {{"claimed", false}, {"error", std::string(_Exception.what())},
            {"sourcePath", PathText(_Source)}, {"originalSourcePath", PathText(_Source)}, {"fileToken", _Token}};
    }
    auto _Record = MoveRecord(_Source, _Temp, _Token, "claimed");
    _Records[_RecordKey] = _Record;
    // Persist ownership before relocating the only input file. A crash or a
    // subsequent state-write failure cannot make an already moved job eligible
    // for scene creation a second time.
    Save(m_Root, _State);
    const auto _Target = Path(Text(_Settings, "outputDirectory")) / Path(SafeStem(_Source) + "-" + _JobID);
    bool _Moved = false;
    try
    {
        MoveWorkbook(_Source, _Temp);
        _Moved = true;
        _Record["stage"] = std::string("validating");
        _Records[_RecordKey] = _Record;
        Save(m_Root, _State);
        if (Digest(_Temp) != _Token)
            throw std::invalid_argument("移入临时目录的 Excel 指纹发生变化；不会开始处理");
        Validate_(_Temp);
        std::filesystem::create_directories(_Target.parent_path());
        if (!std::filesystem::create_directory(_Target))
            throw std::runtime_error("无法建立独立的 Excel 自动处理输出目录");
        _Record["stage"] = std::string("processing");
        _Record["targetDirectory"] = PathText(_Target);
        _Records[_RecordKey] = _Record;
        Map(_State, "observations").erase(_Key);
        Save(m_Root, _State);
    }
    catch (const std::exception& _Exception)
    {
        _Record["status"] = std::string("failed"); _Record["error"] = std::string(_Exception.what());
        _Record["completedAt"] = std::to_string(Now());
        if (!_Moved)
        {
            // Nothing has reached validation or scene creation. A failed move
            // must keep the input eligible for a retry; retain any temp residue
            // under a separate record instead of suppressing this workbook.
            _Records[_RecordKey + "|move-failed|" + _JobID] = _Record;
            _Records.erase(_RecordKey);
        }
        else _Records[_RecordKey] = _Record;
        Save(m_Root, _State);
        return {{"claimed", false}, {"error", std::string(_Exception.what())},
            {"sourcePath", PathText(_Moved ? _Temp : _Source)}, {"originalSourcePath", PathText(_Source)}, {"fileToken", _Token}};
    }
    return {{"claimed", true}, {"sourcePath", PathText(_Temp)}, {"originalSourcePath", PathText(_Source)},
        {"fileToken", _Token}, {"sourceName", PathText(_Source.filename())}, {"targetDirectory", PathText(_Target)}};
}

ObjectMap iCAX::TubeDesigner::CBatchExcelAutomation::Complete(const ObjectMap& Request_) const
{
    CStateLock _Lock(m_Root); auto _State = Load(m_Root);
    const auto _Token = Text(Request_, "fileToken"), _Status = Text(Request_, "status");
    const auto _Key = PathKey(Path(Text(Request_, "sourcePath"))) + "|" + _Token;
    auto& _Records = Map(_State, "records");
    if (_Status != "succeeded" && _Status != "failed") throw std::invalid_argument("Excel 自动处理结果无效");
    if (!_Records.contains(_Key)) throw std::invalid_argument("Excel 自动处理任务不存在");
    auto& _Record = std::get<ObjectMap>(_Records.at(_Key).m_Value);
    if (Text(_Record, "fileToken") != _Token || Text(_Record, "status") != "claimed"
        || Text(_Record, "ownerProcess") != std::to_string(GetCurrentProcessId()) || Text(_Record, "ownerStart") != ProcessStart(GetCurrentProcess()))
        throw std::invalid_argument("Excel 自动处理任务不属于当前进程");
    _Record["status"] = _Status; _Record["error"] = Text(Request_, "error"); _Record["completedAt"] = std::to_string(Now());
    _Record["outputs"] = Request_.contains("outputs") && Request_.at("outputs").Is<VariantArray>() ? Request_.at("outputs") : Variant(VariantArray{});
    Save(m_Root, _State);
    return {{"completed", true}, {"status", _Status}};
}
