#include "pch.h"
#include "ResourcePool.h"

#include "BinaryResource.h"
#include "FlatBufferResource.h"

#include <array>
#include <atomic>
#include <chrono>
#include <fstream>

namespace
{
    using namespace iCAX::Resource;

    std::atomic<uint64_t> g_nResourcePoolStorageSequence = 0;

    std::filesystem::path CreateVersionStorageDirectory(
        IN const CResourceVersionStorageOptions& Options_)
    {
        std::error_code _Error;
        auto _Root = Options_.TemporaryRootDirectory;
        if (_Root.empty())
        {
            _Root = std::filesystem::temp_directory_path(_Error);
            if (_Error)
            {
                return {};
            }
            _Root /= "iCAX";
            _Root /= "ResourceVersions";
        }

        std::filesystem::create_directories(_Root, _Error);
        if (_Error)
        {
            return {};
        }

        const auto _nTimestamp = static_cast<uint64_t>(
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count());
        for (uint32_t _nAttempt = 0; _nAttempt < 64; ++_nAttempt)
        {
            const auto _nSequence =
                g_nResourcePoolStorageSequence.fetch_add(
                    1,
                    std::memory_order_relaxed);
            auto _Directory = _Root /
                ("pool-" + std::to_string(_nTimestamp) + "-" +
                    std::to_string(_nSequence));

            _Error.clear();
            if (std::filesystem::create_directory(_Directory, _Error))
            {
                return _Directory;
            }
            if (_Error &&
                _Error != std::errc::file_exists)
            {
                return {};
            }
        }
        return {};
    }

    std::optional<std::filesystem::path> WriteColdPayload(
        IN const std::filesystem::path& Directory_,
        IN const uint64_t nFileSequence_,
        IN const std::vector<uint8_t>& Bytes_)
    {
        if (Directory_.empty())
        {
            return std::nullopt;
        }

        auto _FinalPath = Directory_ /
            ("version-" + std::to_string(nFileSequence_) + ".bin");
        auto _TemporaryPath = _FinalPath;
        _TemporaryPath += ".tmp";

        try
        {
            std::ofstream _Stream(
                _TemporaryPath,
                std::ios::binary | std::ios::trunc);
            if (!_Stream)
            {
                return std::nullopt;
            }
            if (!Bytes_.empty())
            {
                _Stream.write(
                    reinterpret_cast<const char*>(Bytes_.data()),
                    static_cast<std::streamsize>(Bytes_.size()));
            }
            _Stream.close();
            if (!_Stream)
            {
                std::error_code _RemoveError;
                std::filesystem::remove(_TemporaryPath, _RemoveError);
                return std::nullopt;
            }

            std::error_code _RenameError;
            std::filesystem::rename(
                _TemporaryPath,
                _FinalPath,
                _RenameError);
            if (_RenameError)
            {
                std::error_code _RemoveError;
                std::filesystem::remove(_TemporaryPath, _RemoveError);
                return std::nullopt;
            }
            return _FinalPath;
        }
        catch (...)
        {
            std::error_code _RemoveError;
            std::filesystem::remove(_TemporaryPath, _RemoveError);
            return std::nullopt;
        }
    }

    std::optional<std::vector<uint8_t>> ReadColdPayload(
        IN const std::filesystem::path& Path_)
    {
        std::error_code _Error;
        const auto _nFileSize = std::filesystem::file_size(Path_, _Error);
        if (_Error ||
            _nFileSize >
                static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))
        {
            return std::nullopt;
        }

        try
        {
            std::ifstream _Stream(Path_, std::ios::binary);
            if (!_Stream)
            {
                return std::nullopt;
            }

            std::vector<uint8_t> _Bytes(
                static_cast<size_t>(_nFileSize));
            if (!_Bytes.empty())
            {
                _Stream.read(
                    reinterpret_cast<char*>(_Bytes.data()),
                    static_cast<std::streamsize>(_Bytes.size()));
            }
            if (!_Stream && !_Bytes.empty())
            {
                return std::nullopt;
            }
            return _Bytes;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    template <typename TUnsigned>
    void AppendUnsigned(
        IN OUT std::vector<uint8_t>& Bytes_,
        IN TUnsigned nValue_)
    {
        static_assert(std::is_unsigned_v<TUnsigned>);
        for (size_t _nIndex = 0;
            _nIndex < sizeof(TUnsigned);
            ++_nIndex)
        {
            Bytes_.push_back(static_cast<uint8_t>(
                nValue_ >> (_nIndex * 8)));
        }
    }

    template <typename TUnsigned>
    bool ReadUnsigned(
        IN std::span<const uint8_t> Bytes_,
        IN OUT size_t& nOffset_,
        OUT TUnsigned& nValue_)
    {
        static_assert(std::is_unsigned_v<TUnsigned>);
        if (nOffset_ > Bytes_.size() ||
            Bytes_.size() - nOffset_ < sizeof(TUnsigned))
        {
            return false;
        }

        nValue_ = 0;
        for (size_t _nIndex = 0;
            _nIndex < sizeof(TUnsigned);
            ++_nIndex)
        {
            nValue_ |= static_cast<TUnsigned>(
                Bytes_[nOffset_ + _nIndex])
                << (_nIndex * 8);
        }
        nOffset_ += sizeof(TUnsigned);
        return true;
    }

    void AppendString(
        IN OUT std::vector<uint8_t>& Bytes_,
        IN const std::string& strValue_)
    {
        AppendUnsigned<uint64_t>(
            Bytes_,
            static_cast<uint64_t>(strValue_.size()));
        Bytes_.insert(
            Bytes_.end(),
            strValue_.begin(),
            strValue_.end());
    }

    bool ReadString(
        IN std::span<const uint8_t> Bytes_,
        IN OUT size_t& nOffset_,
        OUT std::string& strValue_)
    {
        uint64_t _nSize = 0;
        if (!ReadUnsigned(Bytes_, nOffset_, _nSize) ||
            _nSize > Bytes_.size() - nOffset_)
        {
            return false;
        }
        strValue_.assign(
            reinterpret_cast<const char*>(
                Bytes_.data() + nOffset_),
            static_cast<size_t>(_nSize));
        nOffset_ += static_cast<size_t>(_nSize);
        return true;
    }

    std::optional<std::vector<uint8_t>> SerializeBinaryResource(
        IN const std::shared_ptr<void>& pResource_)
    {
        const auto _pBinary =
            std::static_pointer_cast<CBinaryResource>(pResource_);
        if (!_pBinary)
        {
            return std::nullopt;
        }

        std::vector<uint8_t> _Bytes{
            'I', 'C', 'A', 'X', 'B', 'I', 'N', '1'
        };
        AppendString(_Bytes, _pBinary->SourcePath);
        AppendString(_Bytes, _pBinary->DisplayName);
        AppendString(_Bytes, _pBinary->FileExtension);
        AppendUnsigned<uint64_t>(
            _Bytes,
            static_cast<uint64_t>(_pBinary->Metadata.size()));
        for (const auto& _Pair : _pBinary->Metadata)
        {
            AppendString(_Bytes, _Pair.first);
            AppendString(_Bytes, _Pair.second);
        }
        AppendUnsigned<uint64_t>(
            _Bytes,
            _pBinary->nVersion);
        AppendUnsigned<uint64_t>(
            _Bytes,
            static_cast<uint64_t>(_pBinary->Content.size()));
        _Bytes.insert(
            _Bytes.end(),
            _pBinary->Content.begin(),
            _pBinary->Content.end());
        return _Bytes;
    }

    std::shared_ptr<void> DeserializeBinaryResource(
        IN const std::span<const uint8_t> Bytes_)
    {
        constexpr std::array<uint8_t, 8> _Magic{
            'I', 'C', 'A', 'X', 'B', 'I', 'N', '1'
        };
        if (Bytes_.size() < _Magic.size() ||
            !std::equal(
                _Magic.begin(),
                _Magic.end(),
                Bytes_.begin()))
        {
            return nullptr;
        }

        size_t _nOffset = _Magic.size();
        auto _pBinary = std::make_shared<CBinaryResource>();
        if (!ReadString(
                Bytes_,
                _nOffset,
                _pBinary->SourcePath) ||
            !ReadString(
                Bytes_,
                _nOffset,
                _pBinary->DisplayName) ||
            !ReadString(
                Bytes_,
                _nOffset,
                _pBinary->FileExtension))
        {
            return nullptr;
        }

        uint64_t _nMetadataCount = 0;
        if (!ReadUnsigned(
                Bytes_,
                _nOffset,
                _nMetadataCount) ||
            _nMetadataCount >
                static_cast<uint64_t>(
                    Bytes_.size() - _nOffset))
        {
            return nullptr;
        }
        for (uint64_t _nIndex = 0;
            _nIndex < _nMetadataCount;
            ++_nIndex)
        {
            std::string _Key;
            std::string _Value;
            if (!ReadString(Bytes_, _nOffset, _Key) ||
                !ReadString(Bytes_, _nOffset, _Value))
            {
                return nullptr;
            }
            _pBinary->Metadata.emplace(
                std::move(_Key),
                std::move(_Value));
        }

        uint64_t _nContentSize = 0;
        if (!ReadUnsigned(
                Bytes_,
                _nOffset,
                _pBinary->nVersion) ||
            !ReadUnsigned(
                Bytes_,
                _nOffset,
                _nContentSize) ||
            _nContentSize != Bytes_.size() - _nOffset)
        {
            return nullptr;
        }
        _pBinary->Content.assign(
            Bytes_.begin() + _nOffset,
            Bytes_.end());
        return std::static_pointer_cast<void>(_pBinary);
    }
}

iCAX::Resource::CResourcePool::CResourcePool()
{
    InitializeVersionStorage();
}

iCAX::Resource::CResourcePool::CResourcePool(
    IN const CResourceVersionStorageOptions& VersionStorageOptions_)
    : m_VersionStorageOptions(VersionStorageOptions_)
{
    InitializeVersionStorage();
}

iCAX::Resource::CResourcePool::~CResourcePool()
{
    if (!m_VersionStorageOptions.bCleanupOnDestroy ||
        m_VersionStorageDirectory.empty())
    {
        return;
    }

    std::error_code _Error;
    std::filesystem::remove_all(
        m_VersionStorageDirectory,
        _Error);
}
void iCAX::Resource::CResourcePool::Register(IN const CResourceInfo& Info_)
{
    ValidateKey(Info_.Key);

    auto _Info = NormalizeInfo(Info_.Key, Info_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(_Info.Key);
    if (_Ite == m_mapResources.end())
    {
        CResourceRecord _Record;
        _Record.Info = std::move(_Info);
        const auto _Key = _Record.Info.Key;
        m_mapVersionHighWaterMarks[_Key] = (std::max)(
            m_mapVersionHighWaterMarks[_Key],
            _Record.Info.nVersion);
        m_mapResources.emplace(_Key, std::move(_Record));
        return;
    }

    if (_Info.nVersion == 0)
    {
        _Info.nVersion = _Ite->second.Info.nVersion;
    }
    _Ite->second.Info = std::move(_Info);
    m_mapVersionHighWaterMarks[_Ite->first] = (std::max)(
        m_mapVersionHighWaterMarks[_Ite->first],
        _Ite->second.Info.nVersion);
}

bool iCAX::Resource::CResourcePool::TryRegister(IN const CResourceInfo& Info_)
{
    ValidateKey(Info_.Key);

    CResourceRecord _Record;
    _Record.Info = NormalizeInfo(Info_.Key, Info_);
    const auto _Key = _Record.Info.Key;

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    const auto _Result =
        m_mapResources.emplace(_Key, std::move(_Record));
    if (_Result.second)
    {
        m_mapVersionHighWaterMarks[_Key] = (std::max)(
            m_mapVersionHighWaterMarks[_Key],
            _Result.first->second.Info.nVersion);
    }
    return _Result.second;
}

void iCAX::Resource::CResourcePool::SetUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN const std::type_info& RuntimeType_, IN const CResourceInfo& Info_)
{
    SetUntyped(Key_, std::move(pResource_), std::type_index(RuntimeType_), Info_);
}

void iCAX::Resource::CResourcePool::SetUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN std::type_index RuntimeType_, IN const CResourceInfo& Info_)
{
    ValidateKey(Key_);
    if (!pResource_)
    {
        throw std::invalid_argument("Resource pointer cannot be null");
    }

    CResourceRecord _Record;
    _Record.Info = NormalizeInfo(Key_, Info_);
    _Record.RuntimeType = RuntimeType_;
    _Record.pResource = std::move(pResource_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite != m_mapResources.end() && _Record.Info.nVersion == 0)
    {
        _Record.Info.nVersion = _Ite->second.Info.nVersion;
    }
    if (_Ite != m_mapResources.end() &&
        _Ite->second.Info.nVersion != _Record.Info.nVersion)
    {
        ArchiveRecordLocked(Key_, _Ite->second);
    }
    m_mapVersionHighWaterMarks[Key_] = (std::max)(
        m_mapVersionHighWaterMarks[Key_],
        _Record.Info.nVersion);
    m_mapResources[Key_] = std::move(_Record);
}

bool iCAX::Resource::CResourcePool::TryAddUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN const std::type_info& RuntimeType_, IN const CResourceInfo& Info_)
{
    return TryAddUntyped(Key_, std::move(pResource_), std::type_index(RuntimeType_), Info_);
}

bool iCAX::Resource::CResourcePool::TryAddUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN std::type_index RuntimeType_, IN const CResourceInfo& Info_)
{
    ValidateKey(Key_);
    if (!pResource_)
    {
        throw std::invalid_argument("Resource pointer cannot be null");
    }

    CResourceRecord _Record;
    _Record.Info = NormalizeInfo(Key_, Info_);
    _Record.RuntimeType = RuntimeType_;
    _Record.pResource = std::move(pResource_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    const auto _Result =
        m_mapResources.emplace(Key_, std::move(_Record));
    if (_Result.second)
    {
        m_mapVersionHighWaterMarks[Key_] = (std::max)(
            m_mapVersionHighWaterMarks[Key_],
            _Result.first->second.Info.nVersion);
    }
    return _Result.second;
}

std::shared_ptr<void> iCAX::Resource::CResourcePool::GetUntyped(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end() || !_Ite->second.pResource)
    {
        return nullptr;
    }
    return _Ite->second.pResource;
}

std::shared_ptr<void> iCAX::Resource::CResourcePool::GetUntyped(IN const CResourceKey& Key_, IN const std::type_info& ExpectedRuntimeType_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end()
        || !_Ite->second.pResource
        || !_Ite->second.RuntimeType.has_value()
        || _Ite->second.RuntimeType.value() != std::type_index(ExpectedRuntimeType_))
    {
        return nullptr;
    }
    return _Ite->second.pResource;
}

std::shared_ptr<void>
iCAX::Resource::CResourcePool::GetUntyped(
    IN const CResourceKey& Key_,
    IN const uint64_t nVersion_) const
{
    const auto _Snapshot = GetSnapshot(Key_, nVersion_);
    return _Snapshot
        ? _Snapshot->pResource
        : nullptr;
}

std::shared_ptr<void>
iCAX::Resource::CResourcePool::GetUntyped(
    IN const CResourceKey& Key_,
    IN const uint64_t nVersion_,
    IN const std::type_info& ExpectedRuntimeType_) const
{
    const auto _Snapshot = GetSnapshot(Key_, nVersion_);
    if (!_Snapshot ||
        !_Snapshot->pResource ||
        !_Snapshot->RuntimeType ||
        *_Snapshot->RuntimeType !=
            std::type_index(ExpectedRuntimeType_))
    {
        return nullptr;
    }
    return _Snapshot->pResource;
}

bool iCAX::Resource::CResourcePool::Contains(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    return m_mapResources.find(Key_) != m_mapResources.end();
}

bool iCAX::Resource::CResourcePool::ContainsVersion(
    IN const CResourceKey& Key_,
    IN const uint64_t nVersion_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    const auto _Current = m_mapResources.find(Key_);
    if (_Current != m_mapResources.end() &&
        _Current->second.Info.nVersion == nVersion_)
    {
        return true;
    }

    const auto _History = m_mapArchivedVersions.find(Key_);
    return _History != m_mapArchivedVersions.end() &&
        _History->second.find(nVersion_) !=
            _History->second.end();
}

bool iCAX::Resource::CResourcePool::HasObject(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    return _Ite != m_mapResources.end() && _Ite->second.pResource != nullptr;
}

bool iCAX::Resource::CResourcePool::Unload(IN const CResourceKey& Key_)
{
    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end())
    {
        return false;
    }

    _Ite->second.pResource.reset();
    _Ite->second.RuntimeType.reset();
    return true;
}

bool iCAX::Resource::CResourcePool::Remove(IN const CResourceKey& Key_)
{
    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    const bool _bHadCurrent = m_mapResources.erase(Key_) > 0;
    const bool _bHadHistory =
        m_mapArchivedVersions.find(Key_) !=
        m_mapArchivedVersions.end();
    DeleteArchivedFilesLocked(Key_);
    m_mapArchivedVersions.erase(Key_);
    m_mapVersionHighWaterMarks.erase(Key_);
    return _bHadCurrent || _bHadHistory;
}

void iCAX::Resource::CResourcePool::Clear()
{
    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    m_mapResources.clear();
    m_mapArchivedVersions.clear();
    m_mapVersionHighWaterMarks.clear();

    if (!m_VersionStorageDirectory.empty())
    {
        std::error_code _Error;
        std::filesystem::remove_all(
            m_VersionStorageDirectory,
            _Error);
        _Error.clear();
        std::filesystem::create_directories(
            m_VersionStorageDirectory,
            _Error);
    }
}

size_t iCAX::Resource::CResourcePool::Count() const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    return m_mapResources.size();
}

std::optional<iCAX::Resource::CResourceInfo> iCAX::Resource::CResourcePool::GetInfo(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end())
    {
        return std::nullopt;
    }
    return _Ite->second.Info;
}

std::optional<iCAX::Resource::CResourceInfo>
iCAX::Resource::CResourcePool::GetInfo(
    IN const CResourceKey& Key_,
    IN const uint64_t nVersion_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    const auto _Current = m_mapResources.find(Key_);
    if (_Current != m_mapResources.end() &&
        _Current->second.Info.nVersion == nVersion_)
    {
        return _Current->second.Info;
    }

    const auto _History = m_mapArchivedVersions.find(Key_);
    if (_History == m_mapArchivedVersions.end())
    {
        return std::nullopt;
    }
    const auto _Version = _History->second.find(nVersion_);
    if (_Version == _History->second.end())
    {
        return std::nullopt;
    }
    return _Version->second.Info;
}

uint64_t iCAX::Resource::CResourcePool::GetVersion(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end())
    {
        return 0;
    }
    return _Ite->second.Info.nVersion;
}

uint64_t iCAX::Resource::CResourcePool::Touch(IN const CResourceKey& Key_)
{
    ValidateKey(Key_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end())
    {
        return 0;
    }

    if (_Ite->second.Info.nVersion == (std::numeric_limits<uint64_t>::max)())
    {
        throw std::overflow_error("Resource version overflow");
    }
    ++_Ite->second.Info.nVersion;
    m_mapVersionHighWaterMarks[Key_] = (std::max)(
        m_mapVersionHighWaterMarks[Key_],
        _Ite->second.Info.nVersion);
    return _Ite->second.Info.nVersion;
}

bool iCAX::Resource::CResourcePool::UpdateInfo(IN const CResourceKey& Key_, IN const CResourceInfo& Info_)
{
    ValidateKey(Key_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end())
    {
        return false;
    }
    auto _Info = NormalizeInfo(Key_, Info_);
    if (_Info.nVersion == 0)
    {
        _Info.nVersion = _Ite->second.Info.nVersion;
    }
    _Ite->second.Info = std::move(_Info);
    m_mapVersionHighWaterMarks[Key_] = (std::max)(
        m_mapVersionHighWaterMarks[Key_],
        _Ite->second.Info.nVersion);
    return true;
}

std::vector<iCAX::Resource::CResourceKey> iCAX::Resource::CResourcePool::GetKeys() const
{
    std::vector<CResourceKey> _Keys;

    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _Pair : m_mapResources)
    {
        _Keys.push_back(_Pair.first);
    }
    return _Keys;
}

std::vector<iCAX::Resource::CResourceInfo> iCAX::Resource::CResourcePool::GetInfos() const
{
    std::vector<CResourceInfo> _Infos;

    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _Pair : m_mapResources)
    {
        _Infos.push_back(_Pair.second.Info);
    }
    return _Infos;
}

std::string iCAX::Resource::CResourcePool::GetRuntimeTypeName(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end() || !_Ite->second.RuntimeType.has_value())
    {
        return std::string();
    }
    return _Ite->second.RuntimeType->name();
}

std::optional<iCAX::Resource::CResourceSnapshot>
iCAX::Resource::CResourcePool::GetSnapshot(
    IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    const auto _Iter = m_mapResources.find(Key_);
    if (_Iter == m_mapResources.end())
    {
        return std::nullopt;
    }

    CResourceSnapshot _Snapshot;
    _Snapshot.Info = _Iter->second.Info;
    _Snapshot.RuntimeType = _Iter->second.RuntimeType;
    _Snapshot.pResource = _Iter->second.pResource;
    return _Snapshot;
}

std::optional<iCAX::Resource::CResourceSnapshot>
iCAX::Resource::CResourcePool::GetSnapshot(
    IN const CResourceKey& Key_,
    IN const uint64_t nVersion_) const
{
    CResourceSnapshot _Snapshot;
    std::filesystem::path _ColdStoragePath;
    CResourceVersionCodec _Codec;

    {
        std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
        const auto _Current = m_mapResources.find(Key_);
        if (_Current != m_mapResources.end() &&
            _Current->second.Info.nVersion == nVersion_)
        {
            _Snapshot.Info = _Current->second.Info;
            _Snapshot.RuntimeType =
                _Current->second.RuntimeType;
            _Snapshot.pResource =
                _Current->second.pResource;
            return _Snapshot;
        }

        const auto _History =
            m_mapArchivedVersions.find(Key_);
        if (_History == m_mapArchivedVersions.end())
        {
            return std::nullopt;
        }
        const auto _Version =
            _History->second.find(nVersion_);
        if (_Version == _History->second.end())
        {
            return std::nullopt;
        }

        _Snapshot.Info = _Version->second.Info;
        _Snapshot.RuntimeType =
            _Version->second.RuntimeType;
        _Snapshot.pResource =
            _Version->second.pResidentResource;
        if (!_Snapshot.pResource)
        {
            _Snapshot.pResource =
                _Version->second.CachedResource.lock();
        }
        if (_Snapshot.pResource ||
            _Version->second.ColdStoragePath.empty() ||
            !_Snapshot.RuntimeType)
        {
            return _Snapshot;
        }

        const auto _CodecIter =
            m_mapVersionCodecs.find(
                *_Snapshot.RuntimeType);
        if (_CodecIter == m_mapVersionCodecs.end() ||
            !_CodecIter->second.IsValid())
        {
            return _Snapshot;
        }
        _ColdStoragePath =
            _Version->second.ColdStoragePath;
        _Codec = _CodecIter->second;
    }

    const auto _Bytes =
        ReadColdPayload(_ColdStoragePath);
    if (!_Bytes)
    {
        return _Snapshot;
    }

    try
    {
        _Snapshot.pResource =
            _Codec.Deserialize(*_Bytes);
    }
    catch (...)
    {
        _Snapshot.pResource.reset();
    }
    if (!_Snapshot.pResource)
    {
        return _Snapshot;
    }

    {
        std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
        const auto _History =
            m_mapArchivedVersions.find(Key_);
        if (_History != m_mapArchivedVersions.end())
        {
            const auto _Version =
                _History->second.find(nVersion_);
            if (_Version != _History->second.end() &&
                _Version->second.ColdStoragePath ==
                    _ColdStoragePath)
            {
                _Version->second.CachedResource =
                    _Snapshot.pResource;
            }
        }
    }
    return _Snapshot;
}

std::vector<uint64_t>
iCAX::Resource::CResourcePool::GetVersions(
    IN const CResourceKey& Key_) const
{
    std::vector<uint64_t> _Versions;
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);

    const auto _History =
        m_mapArchivedVersions.find(Key_);
    if (_History != m_mapArchivedVersions.end())
    {
        _Versions.reserve(
            _History->second.size() + 1);
        for (const auto& _Version :
            _History->second)
        {
            _Versions.push_back(_Version.first);
        }
    }

    const auto _Current = m_mapResources.find(Key_);
    if (_Current != m_mapResources.end() &&
        std::find(
            _Versions.begin(),
            _Versions.end(),
            _Current->second.Info.nVersion) ==
            _Versions.end())
    {
        _Versions.push_back(
            _Current->second.Info.nVersion);
        std::sort(
            _Versions.begin(),
            _Versions.end());
    }
    return _Versions;
}

bool iCAX::Resource::CResourcePool::RegisterVersionCodec(
    IN const std::type_info& RuntimeType_,
    IN CResourceVersionCodec Codec_,
    IN const bool bReplaceExisting_)
{
    if (!Codec_.IsValid())
    {
        throw std::invalid_argument(
            "Resource version codec must provide serialize and deserialize");
    }

    const auto _RuntimeType =
        std::type_index(RuntimeType_);
    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    const auto _Existing =
        m_mapVersionCodecs.find(_RuntimeType);
    if (_Existing != m_mapVersionCodecs.end() &&
        !bReplaceExisting_)
    {
        return false;
    }
    m_mapVersionCodecs[_RuntimeType] =
        std::move(Codec_);
    return true;
}

bool iCAX::Resource::CResourcePool::IsVersionCold(
    IN const CResourceKey& Key_,
    IN const uint64_t nVersion_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    const auto _History =
        m_mapArchivedVersions.find(Key_);
    if (_History == m_mapArchivedVersions.end())
    {
        return false;
    }
    const auto _Version =
        _History->second.find(nVersion_);
    return _Version != _History->second.end() &&
        !_Version->second.ColdStoragePath.empty() &&
        !_Version->second.pResidentResource;
}

iCAX::Resource::CResourceVersionStorageStats
iCAX::Resource::CResourcePool::GetVersionStorageStats() const
{
    CResourceVersionStorageStats _Stats;
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _History :
        m_mapArchivedVersions)
    {
        for (const auto& _Version :
            _History.second)
        {
            ++_Stats.nArchivedVersionCount;
            if (!_Version.second.ColdStoragePath.empty())
            {
                ++_Stats.nColdVersionCount;
                _Stats.nColdBytes +=
                    _Version.second.nStoredSize;
            }
            if (_Version.second.pResidentResource)
            {
                ++_Stats.nResidentVersionCount;
                _Stats.nResidentBytes +=
                    _Version.second.Info.nSize;
            }
        }
    }
    return _Stats;
}

std::filesystem::path
iCAX::Resource::CResourcePool::GetVersionStorageDirectory() const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    return m_VersionStorageDirectory;
}

bool iCAX::Resource::CResourcePool::DiscardVersion(
    IN const CResourceKey& Key_,
    IN const uint64_t nVersion_)
{
    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    const auto _Current = m_mapResources.find(Key_);
    if (_Current != m_mapResources.end() &&
        _Current->second.Info.nVersion == nVersion_)
    {
        return false;
    }

    const auto _History =
        m_mapArchivedVersions.find(Key_);
    if (_History == m_mapArchivedVersions.end())
    {
        return false;
    }
    const auto _Version =
        _History->second.find(nVersion_);
    if (_Version == _History->second.end())
    {
        return false;
    }

    if (!_Version->second.ColdStoragePath.empty())
    {
        std::error_code _Error;
        std::filesystem::remove(
            _Version->second.ColdStoragePath,
            _Error);
    }
    _History->second.erase(_Version);
    if (_History->second.empty())
    {
        m_mapArchivedVersions.erase(_History);
    }
    return true;
}

iCAX::Resource::EResourceMutationResult
iCAX::Resource::CResourcePool::PutUntypedVersioned(
    IN const CResourceKey& Key_,
    IN std::shared_ptr<void> pResource_,
    IN const std::type_info& RuntimeType_,
    IN const CResourceInfo& Info_,
    IN const EResourceVersionCondition Condition_,
    IN const uint64_t nExpectedVersion_,
    OUT CResourceInfo* pStoredInfo_)
{
    ValidateKey(Key_);
    if (!pResource_)
    {
        throw std::invalid_argument("Resource pointer cannot be null");
    }

    auto _Info = NormalizeInfo(Key_, Info_);
    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    const auto _Iter = m_mapResources.find(Key_);
    const bool _bExists = _Iter != m_mapResources.end();
    const uint64_t _nCurrentVersion =
        _bExists ? _Iter->second.Info.nVersion : 0;

    switch (Condition_)
    {
    case EResourceVersionCondition::MustExist:
        if (!_bExists)
        {
            return EResourceMutationResult::PreconditionFailed;
        }
        break;
    case EResourceVersionCondition::MustNotExist:
        if (_bExists)
        {
            return EResourceMutationResult::PreconditionFailed;
        }
        break;
    case EResourceVersionCondition::VersionMatches:
        if (!_bExists || _nCurrentVersion != nExpectedVersion_)
        {
            return EResourceMutationResult::PreconditionFailed;
        }
        break;
    case EResourceVersionCondition::None:
    default:
        break;
    }

    const auto _HighWater =
        m_mapVersionHighWaterMarks.find(Key_);
    const uint64_t _nHighestVersion =
        _HighWater == m_mapVersionHighWaterMarks.end()
        ? _nCurrentVersion
        : (std::max)(
            _nCurrentVersion,
            _HighWater->second);
    if (_nHighestVersion == (std::numeric_limits<uint64_t>::max)())
    {
        throw std::overflow_error("Resource version overflow");
    }
    _Info.nVersion = _nHighestVersion + 1;

    CResourceRecord _Record;
    _Record.Info = std::move(_Info);
    _Record.RuntimeType = std::type_index(RuntimeType_);
    _Record.pResource = std::move(pResource_);

    const auto _Result = _bExists
        ? EResourceMutationResult::Replaced
        : EResourceMutationResult::Created;
    if (_bExists)
    {
        ArchiveRecordLocked(Key_, _Iter->second);
    }
    m_mapVersionHighWaterMarks[Key_] =
        _Record.Info.nVersion;
    m_mapResources[Key_] = std::move(_Record);
    if (pStoredInfo_)
    {
        *pStoredInfo_ = m_mapResources.at(Key_).Info;
    }
    return _Result;
}

iCAX::Resource::EResourceMutationResult
iCAX::Resource::CResourcePool::RemoveVersioned(
    IN const CResourceKey& Key_,
    IN const EResourceVersionCondition Condition_,
    IN const uint64_t nExpectedVersion_)
{
    ValidateKey(Key_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    const auto _Iter = m_mapResources.find(Key_);
    const bool _bExists = _Iter != m_mapResources.end();

    switch (Condition_)
    {
    case EResourceVersionCondition::MustExist:
        if (!_bExists)
        {
            return EResourceMutationResult::PreconditionFailed;
        }
        break;
    case EResourceVersionCondition::MustNotExist:
        if (_bExists)
        {
            return EResourceMutationResult::PreconditionFailed;
        }
        break;
    case EResourceVersionCondition::VersionMatches:
        if (!_bExists || _Iter->second.Info.nVersion != nExpectedVersion_)
        {
            return EResourceMutationResult::PreconditionFailed;
        }
        break;
    case EResourceVersionCondition::None:
    default:
        break;
    }

    if (!_bExists)
    {
        return EResourceMutationResult::NotFound;
    }
    ArchiveRecordLocked(Key_, _Iter->second);
    m_mapVersionHighWaterMarks[Key_] = (std::max)(
        m_mapVersionHighWaterMarks[Key_],
        _Iter->second.Info.nVersion);
    m_mapResources.erase(_Iter);
    return EResourceMutationResult::Removed;
}

std::vector<iCAX::Resource::CResourceInfo> iCAX::Resource::CResourcePool::GetManifest(IN bool bIncludeRuntimeOnly_) const
{
    std::vector<CResourceInfo> _Infos;

    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _Pair : m_mapResources)
    {
        if (bIncludeRuntimeOnly_ || _Pair.second.Info.IsPersistent())
        {
            _Infos.push_back(_Pair.second.Info);
        }
    }
    return _Infos;
}

void iCAX::Resource::CResourcePool::ValidateKey(IN const CResourceKey& Key_)
{
    if (!Key_.IsValid())
    {
        throw std::invalid_argument("Resource source cannot be empty");
    }
}

iCAX::Resource::CResourceInfo iCAX::Resource::CResourcePool::NormalizeInfo(IN const CResourceKey& Key_, IN const CResourceInfo& Info_)
{
    auto _Info = Info_;
    _Info.Key = Key_;
    if (_Info.Source.empty())
    {
        _Info.Source = Key_.Source;
    }
    return _Info;
}

void iCAX::Resource::CResourcePool::ArchiveRecordLocked(
    IN const CResourceKey& Key_,
    IN const CResourceRecord& Record_)
{
    CArchivedResourceRecord _Archived;
    _Archived.Info = Record_.Info;
    _Archived.RuntimeType = Record_.RuntimeType;

    if (Record_.pResource &&
        Record_.RuntimeType.has_value())
    {
        const auto _Codec =
            m_mapVersionCodecs.find(
                *Record_.RuntimeType);
        if (_Codec != m_mapVersionCodecs.end() &&
            _Codec->second.IsValid() &&
            !m_VersionStorageDirectory.empty())
        {
            std::optional<std::vector<uint8_t>> _Bytes;
            try
            {
                _Bytes =
                    _Codec->second.Serialize(
                        Record_.pResource);
            }
            catch (...)
            {
                _Bytes.reset();
            }

            if (_Bytes)
            {
                const auto _Path = WriteColdPayload(
                    m_VersionStorageDirectory,
                    ++m_nArchiveFileSequence,
                    *_Bytes);
                if (_Path)
                {
                    _Archived.ColdStoragePath = *_Path;
                    _Archived.nStoredSize =
                        static_cast<uint64_t>(
                            _Bytes->size());
                    _Archived.CachedResource =
                        Record_.pResource;
                }
            }
        }

        if (_Archived.ColdStoragePath.empty())
        {
            // 可靠性优先：没有编解码器、序列化失败或磁盘不可写时保留内存对象。
            _Archived.pResidentResource =
                Record_.pResource;
        }
    }

    auto& _Versions =
        m_mapArchivedVersions[Key_];
    const auto _Existing =
        _Versions.find(Record_.Info.nVersion);
    if (_Existing != _Versions.end() &&
        !_Existing->second.ColdStoragePath.empty())
    {
        std::error_code _Error;
        std::filesystem::remove(
            _Existing->second.ColdStoragePath,
            _Error);
    }
    _Versions[Record_.Info.nVersion] =
        std::move(_Archived);
}

void iCAX::Resource::CResourcePool::DeleteArchivedFilesLocked(
    IN const CResourceKey& Key_) noexcept
{
    const auto _History =
        m_mapArchivedVersions.find(Key_);
    if (_History == m_mapArchivedVersions.end())
    {
        return;
    }

    for (const auto& _Version :
        _History->second)
    {
        if (_Version.second.ColdStoragePath.empty())
        {
            continue;
        }
        std::error_code _Error;
        std::filesystem::remove(
            _Version.second.ColdStoragePath,
            _Error);
    }
}

void iCAX::Resource::CResourcePool::InitializeVersionStorage()
{
    CResourceVersionCodec _FlatBufferCodec;
    _FlatBufferCodec.Serialize =
        [](const std::shared_ptr<void>& pResource_)
        -> std::optional<std::vector<uint8_t>>
        {
            const auto _pFlatBuffer =
                std::static_pointer_cast<
                    CFlatBufferResource>(pResource_);
            if (!_pFlatBuffer)
            {
                return std::nullopt;
            }
            const auto _Bytes = _pFlatBuffer->Bytes();
            return std::vector<uint8_t>(
                _Bytes.begin(),
                _Bytes.end());
        };
    _FlatBufferCodec.Deserialize =
        [](const std::span<const uint8_t> Bytes_)
        -> std::shared_ptr<void>
        {
            return std::static_pointer_cast<void>(
                std::make_shared<CFlatBufferResource>(
                    std::vector<uint8_t>(
                        Bytes_.begin(),
                        Bytes_.end())));
        };
    m_mapVersionCodecs.emplace(
        std::type_index(typeid(CFlatBufferResource)),
        std::move(_FlatBufferCodec));

    CResourceVersionCodec _BinaryCodec;
    _BinaryCodec.Serialize = SerializeBinaryResource;
    _BinaryCodec.Deserialize = DeserializeBinaryResource;
    m_mapVersionCodecs.emplace(
        std::type_index(typeid(CBinaryResource)),
        std::move(_BinaryCodec));

    m_VersionStorageDirectory =
        CreateVersionStorageDirectory(
            m_VersionStorageOptions);
}
