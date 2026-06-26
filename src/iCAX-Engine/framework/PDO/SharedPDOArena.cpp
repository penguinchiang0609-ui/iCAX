#include "pch.h"
#include "SharedPDOArena.h"

#include <Windows.h>

#include <cstring>
#include <format>
#include <stdexcept>
#include <unordered_set>

namespace
{
    constexpr uint64_t kSharedPDOAlignment = 64;

    uint64_t AlignUp(IN uint64_t nValue_, IN uint64_t nAlignment_)
    {
        return (nValue_ + nAlignment_ - 1) / nAlignment_ * nAlignment_;
    }

    void ValidatePDODecl(IN const iCAX::PDO::PDODecl& Decl_)
    {
        if (Decl_.nID == 0)
        {
            throw std::invalid_argument("Shared PDO declaration id cannot be zero");
        }
        if (Decl_.nVersion == 0)
        {
            throw std::invalid_argument("Shared PDO declaration version cannot be zero");
        }
        if (Decl_.eDirection != iCAX::PDO::kDirection2Inner
            && Decl_.eDirection != iCAX::PDO::kDirection2External)
        {
            throw std::invalid_argument("Shared PDO declaration direction must have exactly one writer");
        }
        if (Decl_.nPayloadSize <= 0)
        {
            throw std::invalid_argument("Shared PDO declaration payload size must be positive");
        }
    }

    uint64_t CalculateArenaSize(IN const std::vector<iCAX::PDO::PDODecl>& Decls_)
    {
        uint64_t _Offset = AlignUp(sizeof(iCAX::PDO::SharedPDOArenaHeader), kSharedPDOAlignment);
        _Offset += AlignUp(sizeof(iCAX::PDO::SharedPDOSlotHeader) * Decls_.size(), kSharedPDOAlignment);

        for (const auto& _Decl : Decls_)
        {
            _Offset = AlignUp(_Offset, kSharedPDOAlignment);
            _Offset += AlignUp(static_cast<uint64_t>(_Decl.nPayloadSize), kSharedPDOAlignment)
                * iCAX::PDO::kSharedPDOBufferCount;
        }
        return _Offset;
    }

    DWORD HighDword(IN uint64_t nValue_)
    {
        return static_cast<DWORD>(nValue_ >> 32);
    }

    DWORD LowDword(IN uint64_t nValue_)
    {
        return static_cast<DWORD>(nValue_ & 0xFFFFFFFFull);
    }

    bool IsAligned(IN uint64_t nValue_)
    {
        return nValue_ % kSharedPDOAlignment == 0;
    }

    bool IsValidBufferIndex(IN long nIndex_)
    {
        return nIndex_ >= 0
            && nIndex_ < static_cast<long>(iCAX::PDO::kSharedPDOBufferCount);
    }

    bool IsValidReadyIndex(IN long nIndex_)
    {
        return nIndex_ == iCAX::PDO::kSharedPDOReadyNone || IsValidBufferIndex(nIndex_);
    }

    bool IsValidBufferState(IN long nState_)
    {
        return nState_ == iCAX::PDO::kSharedPDOBufferFree
            || nState_ == iCAX::PDO::kSharedPDOBufferPublished
            || nState_ == iCAX::PDO::kSharedPDOBufferWriting
            || nState_ == iCAX::PDO::kSharedPDOBufferReady
            || nState_ == iCAX::PDO::kSharedPDOBufferReading;
    }

    uint64_t ReadAtomicUInt64(IN volatile unsigned long long* pValue_)
    {
        return static_cast<uint64_t>(InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(const_cast<volatile unsigned long long*>(pValue_)),
            0,
            0));
    }

    void WriteAtomicUInt64(IN volatile unsigned long long* pValue_, IN uint64_t nValue_)
    {
        InterlockedExchange64(
            reinterpret_cast<volatile LONG64*>(pValue_),
            static_cast<LONG64>(nValue_));
    }

    bool IsValidDirection(IN uint32_t nDirection_)
    {
        return nDirection_ == iCAX::PDO::kDirection2Inner
            || nDirection_ == iCAX::PDO::kDirection2External;
    }

    bool AddWouldOverflow(IN uint64_t nLeft_, IN uint64_t nRight_)
    {
        return nLeft_ > UINT64_MAX - nRight_;
    }

    bool IsRangeInsideArena(
        IN uint64_t nOffset_,
        IN uint64_t nSize_,
        IN uint64_t nArenaSize_)
    {
        if (nSize_ == 0 || nOffset_ >= nArenaSize_)
        {
            return false;
        }
        if (AddWouldOverflow(nOffset_, nSize_))
        {
            return false;
        }
        return nOffset_ + nSize_ <= nArenaSize_;
    }

    uint64_t QueryMappedViewSize(IN const void* pBase_)
    {
        MEMORY_BASIC_INFORMATION _Info{};
        if (!pBase_ || VirtualQuery(pBase_, &_Info, sizeof(_Info)) == 0)
        {
            return 0;
        }
        return static_cast<uint64_t>(_Info.RegionSize);
    }

    std::string ToNarrow(IN const std::wstring& strText_)
    {
        if (strText_.empty())
        {
            return {};
        }

        const int _Length = WideCharToMultiByte(
            CP_UTF8,
            0,
            strText_.c_str(),
            static_cast<int>(strText_.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (_Length <= 0)
        {
            return "<invalid wide string>";
        }

        std::string _Result(static_cast<size_t>(_Length), '\0');
        (void)WideCharToMultiByte(
            CP_UTF8,
            0,
            strText_.c_str(),
            static_cast<int>(strText_.size()),
            _Result.data(),
            _Length,
            nullptr,
            nullptr);
        return _Result;
    }
}

iCAX::PDO::CSharedPDOSlot::CSharedPDOSlot(IN uint8_t* pArenaBase_, IN SharedPDOSlotHeader* pHeader_)
    : m_pArenaBase(pArenaBase_)
    , m_pHeader(pHeader_)
{
    if (m_pHeader)
    {
        m_Decl.nID = m_pHeader->nID;
        m_Decl.nVersion = m_pHeader->nVersion;
        m_Decl.eDirection = static_cast<PDODirection>(m_pHeader->nDirection);
        m_Decl.nPayloadSize = static_cast<int>(m_pHeader->nPayloadSize);
    }
}

bool iCAX::PDO::CSharedPDOSlot::IsValid() const noexcept
{
    return m_pArenaBase != nullptr && m_pHeader != nullptr;
}

uint32_t iCAX::PDO::CSharedPDOSlot::GetSequence() const noexcept
{
    if (!IsValid())
    {
        return 0;
    }
    return static_cast<uint32_t>(InterlockedCompareExchange(
        const_cast<volatile long*>(&m_pHeader->nSequence),
        0,
        0));
}

uint32_t iCAX::PDO::CSharedPDOSlot::GetPublishedIndex() const noexcept
{
    if (!IsValid())
    {
        return 0;
    }
    return static_cast<uint32_t>(InterlockedCompareExchange(
        const_cast<volatile long*>(&m_pHeader->nPublishedIndex),
        0,
        0));
}

uint32_t iCAX::PDO::CSharedPDOSlot::GetBufferState(IN uint32_t nIndex_) const
{
    RequireValid();
    if (nIndex_ >= kSharedPDOBufferCount)
    {
        throw std::out_of_range("Shared PDO buffer state index is out of range");
    }
    return static_cast<uint32_t>(InterlockedCompareExchange(
        &m_pHeader->nBufferState[nIndex_],
        0,
        0));
}

uint32_t iCAX::PDO::CSharedPDOSlot::GetReaderCount(IN uint32_t nIndex_) const
{
    RequireValid();
    if (nIndex_ >= kSharedPDOBufferCount)
    {
        throw std::out_of_range("Shared PDO reader count index is out of range");
    }
    return static_cast<uint32_t>(InterlockedCompareExchange(
        &m_pHeader->nReaderCount[nIndex_],
        0,
        0));
}

uint64_t iCAX::PDO::CSharedPDOSlot::GetBufferDataVersion(IN uint32_t nIndex_) const
{
    RequireValid();
    if (nIndex_ >= kSharedPDOBufferCount)
    {
        throw std::out_of_range("Shared PDO data version index is out of range");
    }
    return ReadAtomicUInt64(&m_pHeader->nBufferDataVersion[nIndex_]);
}

bool iCAX::PDO::CSharedPDOSlot::IsReadSnapshotValid(
    IN uint32_t nSequence_,
    IN uint32_t nPublishedIndex_) const noexcept
{
    if (!IsValid() || nPublishedIndex_ >= kSharedPDOBufferCount)
    {
        return false;
    }

    const auto _CurrentSequence = GetSequence();
    const auto _CurrentPublishedIndex = GetPublishedIndex();
    const auto _CurrentState = static_cast<uint32_t>(InterlockedCompareExchange(
        const_cast<volatile long*>(&m_pHeader->nBufferState[nPublishedIndex_]),
        0,
        0));

    return _CurrentSequence == nSequence_
        && _CurrentPublishedIndex == nPublishedIndex_
        && (_CurrentState == kSharedPDOBufferPublished
            || _CurrentState == kSharedPDOBufferReading);
}

uint64_t iCAX::PDO::CSharedPDOSlot::GetReadBufferOffset() const
{
    RequireValid();
    return m_pHeader->nBufferOffset[GetPublishedIndex()];
}

uint64_t iCAX::PDO::CSharedPDOSlot::GetWriteBufferOffset() const
{
    RequireValid();
    const auto _WriteIndex = static_cast<uint32_t>(InterlockedCompareExchange(
        &m_pHeader->nWriteIndex,
        0,
        0));
    return m_pHeader->nBufferOffset[_WriteIndex];
}

const iCAX::PDO::SharedPDOSlotHeader& iCAX::PDO::CSharedPDOSlot::GetSharedHeader() const
{
    RequireValid();
    return *m_pHeader;
}

const iCAX::PDO::PDODecl& iCAX::PDO::CSharedPDOSlot::GetHeader() const
{
    RequireValid();
    return m_Decl;
}

void* iCAX::PDO::CSharedPDOSlot::BeginWrite()
{
    RequireValid();
    for (;;)
    {
        void* _pWriteData = nullptr;
        if (TryBeginWrite(_pWriteData))
        {
            return _pWriteData;
        }
        SwitchToThread();
    }
}

bool iCAX::PDO::CSharedPDOSlot::TryBeginWrite(OUT void*& pWriteData_)
{
    RequireValid();
    pWriteData_ = nullptr;

    const auto _WriteIndex = static_cast<uint32_t>(InterlockedCompareExchange(
        &m_pHeader->nWriteIndex,
        0,
        0));
    if (!TryClaimWriteBufferOnce(_WriteIndex))
    {
        return false;
    }

    pWriteData_ = GetBufferAddress(_WriteIndex);
    return true;
}

bool iCAX::PDO::CSharedPDOSlot::TryBeginWriteIfNewer(
    IN uint64_t nDataVersion_,
    OUT void*& pWriteData_)
{
    RequireValid();
    pWriteData_ = nullptr;

    if (nDataVersion_ == 0)
    {
        throw std::invalid_argument("Shared PDO data version cannot be zero");
    }
    if (nDataVersion_ <= GetLatestDataVersion())
    {
        return false;
    }

    return TryBeginWrite(pWriteData_);
}

void iCAX::PDO::CSharedPDOSlot::MarkWriteReady(IN uint64_t nDataVersion_)
{
    RequireValid();
    if (nDataVersion_ == 0)
    {
        throw std::invalid_argument("Shared PDO data version cannot be zero");
    }
    if (nDataVersion_ <= GetLatestDataVersion())
    {
        throw std::logic_error("Shared PDO data version must be newer than the slot version");
    }

    const auto _WriteIndex = InterlockedCompareExchange(&m_pHeader->nWriteIndex, 0, 0);
    WriteAtomicUInt64(&m_pHeader->nBufferDataVersion[_WriteIndex], nDataVersion_);
    WriteAtomicUInt64(&m_pHeader->nLatestDataVersion, nDataVersion_);
    InterlockedExchange(&m_pHeader->nBufferState[_WriteIndex], kSharedPDOBufferReady);
    InterlockedExchange(&m_pHeader->nReadyIndex, _WriteIndex);
}

void iCAX::PDO::CSharedPDOSlot::CancelWrite()
{
    RequireValid();
    const auto _WriteIndex = InterlockedCompareExchange(&m_pHeader->nWriteIndex, 0, 0);
    if (_WriteIndex < 0 || _WriteIndex >= static_cast<long>(kSharedPDOBufferCount))
    {
        throw std::logic_error("Shared PDO write buffer index is invalid");
    }

    const auto _State = InterlockedCompareExchange(&m_pHeader->nBufferState[_WriteIndex], 0, 0);
    if (_State == kSharedPDOBufferWriting)
    {
        const auto _BufferDataVersion = GetBufferDataVersion(static_cast<uint32_t>(_WriteIndex));
        if (_BufferDataVersion > GetPublishedDataVersion())
        {
            InterlockedExchange(&m_pHeader->nBufferState[_WriteIndex], kSharedPDOBufferReady);
            InterlockedExchange(&m_pHeader->nReadyIndex, _WriteIndex);
            return;
        }

        InterlockedExchange(&m_pHeader->nBufferState[_WriteIndex], kSharedPDOBufferFree);
    }
}

const void* iCAX::PDO::CSharedPDOSlot::BeginRead(
    OUT uint32_t& nSequence_,
    OUT uint32_t& nPublishedIndex_,
    OUT uint64_t& nDataVersion_)
{
    RequireValid();

    for (;;)
    {
        const auto _Sequence = GetSequence();
        const auto _PublishedIndex = GetPublishedIndex();
        const auto _State = GetBufferState(_PublishedIndex);
        if (_State != kSharedPDOBufferPublished && _State != kSharedPDOBufferReading)
        {
            SwitchToThread();
            continue;
        }

        InterlockedIncrement(&m_pHeader->nReaderCount[_PublishedIndex]);
        const auto _CurrentSequence = GetSequence();
        const auto _CurrentPublishedIndex = GetPublishedIndex();
        const auto _CurrentState = GetBufferState(_PublishedIndex);
        if (_CurrentSequence == _Sequence
            && _CurrentPublishedIndex == _PublishedIndex
            && (_CurrentState == kSharedPDOBufferPublished || _CurrentState == kSharedPDOBufferReading))
        {
            InterlockedExchange(&m_pHeader->nBufferState[_PublishedIndex], kSharedPDOBufferReading);
            nSequence_ = _Sequence;
            nPublishedIndex_ = _PublishedIndex;
            nDataVersion_ = GetBufferDataVersion(_PublishedIndex);
            return GetBufferAddress(_PublishedIndex);
        }

        EndRead(_PublishedIndex);
        SwitchToThread();
    }
}

void iCAX::PDO::CSharedPDOSlot::EndRead(IN uint32_t nPublishedIndex_)
{
    RequireValid();
    if (nPublishedIndex_ >= kSharedPDOBufferCount)
    {
        throw std::out_of_range("Shared PDO read buffer index is out of range");
    }

    const auto _Remaining = InterlockedDecrement(&m_pHeader->nReaderCount[nPublishedIndex_]);
    if (_Remaining < 0)
    {
        InterlockedExchange(&m_pHeader->nReaderCount[nPublishedIndex_], 0);
        throw std::logic_error("Shared PDO read lease is not active");
    }
    if (_Remaining != 0)
    {
        return;
    }

    if (GetPublishedIndex() == nPublishedIndex_)
    {
        InterlockedExchange(&m_pHeader->nBufferState[nPublishedIndex_], kSharedPDOBufferPublished);
    }
    else
    {
        InterlockedExchange(&m_pHeader->nBufferState[nPublishedIndex_], kSharedPDOBufferFree);
    }
}

uint64_t iCAX::PDO::CSharedPDOSlot::GetPublishedDataVersion() const noexcept
{
    if (!IsValid())
    {
        return 0;
    }
    return ReadAtomicUInt64(&m_pHeader->nPublishedDataVersion);
}

uint64_t iCAX::PDO::CSharedPDOSlot::GetLatestDataVersion() const noexcept
{
    if (!IsValid())
    {
        return 0;
    }
    return ReadAtomicUInt64(&m_pHeader->nLatestDataVersion);
}

void iCAX::PDO::CSharedPDOSlot::SwapBuffersIfReady()
{
    RequireValid();
    const auto _ReadyIndex = InterlockedExchange(&m_pHeader->nReadyIndex, kSharedPDOReadyNone);
    if (_ReadyIndex == kSharedPDOReadyNone)
    {
        return;
    }

    const auto _OldPublishedIndex = InterlockedExchange(&m_pHeader->nPublishedIndex, _ReadyIndex);
    if (InterlockedCompareExchange(&m_pHeader->nReaderCount[_OldPublishedIndex], 0, 0) == 0)
    {
        InterlockedExchange(&m_pHeader->nBufferState[_OldPublishedIndex], kSharedPDOBufferFree);
    }
    else
    {
        InterlockedExchange(&m_pHeader->nBufferState[_OldPublishedIndex], kSharedPDOBufferReading);
    }
    InterlockedExchange(&m_pHeader->nBufferState[_ReadyIndex], kSharedPDOBufferPublished);
    WriteAtomicUInt64(&m_pHeader->nPublishedDataVersion, GetBufferDataVersion(static_cast<uint32_t>(_ReadyIndex)));
    InterlockedExchange(&m_pHeader->nWriteIndex, _OldPublishedIndex);
    InterlockedIncrement(&m_pHeader->nSequence);
}

uint8_t* iCAX::PDO::CSharedPDOSlot::GetBufferAddress(IN uint32_t nIndex_) const
{
    if (nIndex_ >= kSharedPDOBufferCount)
    {
        throw std::out_of_range("Shared PDO buffer index is out of range");
    }
    return m_pArenaBase + m_pHeader->nBufferOffset[nIndex_];
}

bool iCAX::PDO::CSharedPDOSlot::TryClaimWriteBufferOnce(IN uint32_t nIndex_) const
{
    if (nIndex_ >= kSharedPDOBufferCount)
    {
        throw std::out_of_range("Shared PDO write buffer index is out of range");
    }

    const auto _ReaderCount = InterlockedCompareExchange(
        const_cast<volatile long*>(&m_pHeader->nReaderCount[nIndex_]),
        0,
        0);
    const auto _State = InterlockedCompareExchange(
        const_cast<volatile long*>(&m_pHeader->nBufferState[nIndex_]),
        0,
        0);
    const auto _CurrentWriteIndex = InterlockedCompareExchange(
        const_cast<volatile long*>(&m_pHeader->nWriteIndex),
        0,
        0);
    if (_CurrentWriteIndex != static_cast<long>(nIndex_))
    {
        return false;
    }

    if (_ReaderCount != 0)
    {
        return false;
    }

    if (_State == kSharedPDOBufferFree)
    {
        return InterlockedCompareExchange(
            &m_pHeader->nBufferState[nIndex_],
            kSharedPDOBufferWriting,
            kSharedPDOBufferFree) == kSharedPDOBufferFree;
    }

    if (_State == kSharedPDOBufferReady)
    {
        const auto _ClaimedReadyIndex = InterlockedCompareExchange(
            &m_pHeader->nReadyIndex,
            kSharedPDOReadyNone,
            static_cast<long>(nIndex_));
        if (_ClaimedReadyIndex != static_cast<long>(nIndex_))
        {
            return false;
        }

        const auto _ClaimedState = InterlockedCompareExchange(
            &m_pHeader->nBufferState[nIndex_],
            kSharedPDOBufferWriting,
            kSharedPDOBufferReady);
        if (_ClaimedState == kSharedPDOBufferReady)
        {
            return true;
        }

        (void)InterlockedCompareExchange(
            &m_pHeader->nReadyIndex,
            static_cast<long>(nIndex_),
            kSharedPDOReadyNone);
        return false;
    }
    return false;
}

void iCAX::PDO::CSharedPDOSlot::RequireValid() const
{
    if (!IsValid())
    {
        throw std::logic_error("Shared PDO slot is not bound");
    }
}

std::shared_ptr<iCAX::PDO::CSharedPDOArena> iCAX::PDO::CSharedPDOArena::Create(
    IN std::wstring strName_,
    IN const std::vector<PDODecl>& Decls_)
{
    if (strName_.empty())
    {
        throw std::invalid_argument("Shared PDO arena name cannot be empty");
    }
    if (Decls_.empty())
    {
        throw std::invalid_argument("Shared PDO arena must contain at least one slot");
    }

    std::unordered_set<PDOID> _IDs;
    for (const auto& _Decl : Decls_)
    {
        ValidatePDODecl(_Decl);
        if (!_IDs.insert(_Decl.nID).second)
        {
            throw std::invalid_argument(std::format("Duplicate shared PDO id: {}", _Decl.nID));
        }
    }

    const auto _ArenaSize = CalculateArenaSize(Decls_);
    HANDLE _Mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        HighDword(_ArenaSize),
        LowDword(_ArenaSize),
        strName_.c_str());
    if (!_Mapping)
    {
        throw std::runtime_error("CreateFileMappingW failed for shared PDO arena: " + ToNarrow(strName_));
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(_Mapping);
        throw std::runtime_error("Shared PDO arena already exists: " + ToNarrow(strName_));
    }

    auto _pBase = static_cast<uint8_t*>(MapViewOfFile(_Mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (!_pBase)
    {
        CloseHandle(_Mapping);
        throw std::runtime_error("MapViewOfFile failed for shared PDO arena: " + ToNarrow(strName_));
    }

    auto _pArena = std::shared_ptr<CSharedPDOArena>(new CSharedPDOArena());
    _pArena->m_strName = std::move(strName_);
    _pArena->m_hMapping = _Mapping;
    _pArena->m_pBase = _pBase;
    _pArena->m_nArenaSize = _ArenaSize;
    _pArena->InitializeCreatedArena(Decls_);
    _pArena->ValidateOpenedArena();
    return _pArena;
}

std::shared_ptr<iCAX::PDO::CSharedPDOArena> iCAX::PDO::CSharedPDOArena::Open(IN std::wstring strName_)
{
    if (strName_.empty())
    {
        throw std::invalid_argument("Shared PDO arena name cannot be empty");
    }

    HANDLE _Mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, strName_.c_str());
    if (!_Mapping)
    {
        throw std::runtime_error("OpenFileMappingW failed for shared PDO arena: " + ToNarrow(strName_));
    }

    auto _pBase = static_cast<uint8_t*>(MapViewOfFile(_Mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (!_pBase)
    {
        CloseHandle(_Mapping);
        throw std::runtime_error("MapViewOfFile failed for shared PDO arena: " + ToNarrow(strName_));
    }

    auto _pArena = std::shared_ptr<CSharedPDOArena>(new CSharedPDOArena());
    _pArena->m_strName = std::move(strName_);
    _pArena->m_hMapping = _Mapping;
    _pArena->m_pBase = _pBase;
    _pArena->m_nArenaSize = QueryMappedViewSize(_pBase);
    _pArena->ValidateOpenedArena();
    _pArena->m_nArenaSize = _pArena->GetMutableHeader()->nArenaSize;
    return _pArena;
}

iCAX::PDO::CSharedPDOArena::~CSharedPDOArena()
{
    if (m_pBase)
    {
        UnmapViewOfFile(m_pBase);
        m_pBase = nullptr;
    }
    if (m_hMapping)
    {
        CloseHandle(static_cast<HANDLE>(m_hMapping));
        m_hMapping = nullptr;
    }
}

const std::wstring& iCAX::PDO::CSharedPDOArena::GetName() const noexcept
{
    return m_strName;
}

void* iCAX::PDO::CSharedPDOArena::GetBaseAddress() const noexcept
{
    return m_pBase;
}

uint64_t iCAX::PDO::CSharedPDOArena::GetArenaSize() const noexcept
{
    return m_nArenaSize;
}

const iCAX::PDO::SharedPDOArenaHeader& iCAX::PDO::CSharedPDOArena::GetHeader() const
{
    ValidateOpenedArena();
    return *GetMutableHeader();
}

std::vector<iCAX::PDO::PDODecl> iCAX::PDO::CSharedPDOArena::GetDeclarations() const
{
    ValidateOpenedArena();
    std::vector<PDODecl> _Decls;
    const auto& _Header = *GetMutableHeader();
    const auto* _Slots = GetSlotTable();
    _Decls.reserve(_Header.nSlotCount);
    for (uint32_t _Index = 0; _Index < _Header.nSlotCount; ++_Index)
    {
        const auto& _Slot = _Slots[_Index];
        _Decls.push_back(PDODecl{
            _Slot.nVersion,
            _Slot.nID,
            static_cast<PDODirection>(_Slot.nDirection),
            static_cast<int>(_Slot.nPayloadSize)
            });
    }
    return _Decls;
}

iCAX::PDO::CSharedPDOSlot iCAX::PDO::CSharedPDOArena::GetSlot(IN PDOID nID_) const
{
    ValidateOpenedArena();
    const auto& _Header = *GetMutableHeader();
    auto* _Slots = GetSlotTable();
    for (uint32_t _Index = 0; _Index < _Header.nSlotCount; ++_Index)
    {
        if (_Slots[_Index].nID == nID_)
        {
            return CSharedPDOSlot(m_pBase, &_Slots[_Index]);
        }
    }
    throw std::runtime_error(std::format("Shared PDO slot not found: {}", nID_));
}

void iCAX::PDO::CSharedPDOArena::InitializeCreatedArena(IN const std::vector<PDODecl>& Decls_)
{
    std::memset(m_pBase, 0, static_cast<size_t>(m_nArenaSize));

    auto* _Header = GetMutableHeader();
    _Header->nMagic = kSharedPDOArenaMagic;
    _Header->nVersion = kSharedPDOArenaVersion;
    _Header->nSlotCount = static_cast<uint32_t>(Decls_.size());
    _Header->nHeaderSize = sizeof(SharedPDOArenaHeader);
    _Header->nArenaSize = m_nArenaSize;
    _Header->nSlotTableOffset = AlignUp(sizeof(SharedPDOArenaHeader), kSharedPDOAlignment);
    _Header->nPayloadOffset = AlignUp(
        _Header->nSlotTableOffset + sizeof(SharedPDOSlotHeader) * Decls_.size(),
        kSharedPDOAlignment);

    auto* _Slots = GetSlotTable();
    uint64_t _PayloadOffset = _Header->nPayloadOffset;
    for (size_t _Index = 0; _Index < Decls_.size(); ++_Index)
    {
        const auto& _Decl = Decls_[_Index];
        auto& _Slot = _Slots[_Index];
        _Slot.nID = _Decl.nID;
        _Slot.nVersion = _Decl.nVersion;
        _Slot.nDirection = _Decl.eDirection;
        _Slot.nPayloadSize = static_cast<uint32_t>(_Decl.nPayloadSize);
        _Slot.nBufferState[0] = kSharedPDOBufferPublished;
        _Slot.nBufferState[1] = kSharedPDOBufferFree;
        _Slot.nReaderCount[0] = 0;
        _Slot.nReaderCount[1] = 0;
        _Slot.nPublishedIndex = 0;
        _Slot.nWriteIndex = 1;
        _Slot.nReadyIndex = kSharedPDOReadyNone;
        _Slot.nSequence = 0;
        _Slot.nBufferDataVersion[0] = 0;
        _Slot.nBufferDataVersion[1] = 0;
        _Slot.nPublishedDataVersion = 0;
        _Slot.nLatestDataVersion = 0;

        for (uint32_t _BufferIndex = 0; _BufferIndex < kSharedPDOBufferCount; ++_BufferIndex)
        {
            _PayloadOffset = AlignUp(_PayloadOffset, kSharedPDOAlignment);
            _Slot.nBufferOffset[_BufferIndex] = _PayloadOffset;
            _PayloadOffset += AlignUp(static_cast<uint64_t>(_Decl.nPayloadSize), kSharedPDOAlignment);
        }
    }
}

void iCAX::PDO::CSharedPDOArena::ValidateOpenedArena() const
{
    if (!m_pBase)
    {
        throw std::logic_error("Shared PDO arena is not mapped");
    }
    const auto* _Header = reinterpret_cast<const SharedPDOArenaHeader*>(m_pBase);
    if (_Header->nMagic != kSharedPDOArenaMagic)
    {
        throw std::runtime_error("Shared PDO arena magic is invalid");
    }
    if (_Header->nVersion != kSharedPDOArenaVersion)
    {
        throw std::runtime_error("Shared PDO arena version is not supported");
    }
    if (_Header->nHeaderSize != sizeof(SharedPDOArenaHeader))
    {
        throw std::runtime_error("Shared PDO arena header size is invalid");
    }
    if (_Header->nSlotCount == 0)
    {
        throw std::runtime_error("Shared PDO arena slot count is invalid");
    }
    if (_Header->nArenaSize == 0)
    {
        throw std::runtime_error("Shared PDO arena size is invalid");
    }
    if (m_nArenaSize != 0 && _Header->nArenaSize > m_nArenaSize)
    {
        throw std::runtime_error("Shared PDO arena size exceeds mapped view size");
    }
    if (_Header->nSlotTableOffset < sizeof(SharedPDOArenaHeader))
    {
        throw std::runtime_error("Shared PDO arena slot table offset is invalid");
    }
    if (!IsAligned(_Header->nSlotTableOffset))
    {
        throw std::runtime_error("Shared PDO arena slot table offset is not aligned");
    }
    if (_Header->nSlotCount > UINT64_MAX / sizeof(SharedPDOSlotHeader))
    {
        throw std::runtime_error("Shared PDO arena slot table size overflows");
    }
    const auto _SlotTableSize = static_cast<uint64_t>(sizeof(SharedPDOSlotHeader))
        * static_cast<uint64_t>(_Header->nSlotCount);
    if (!IsRangeInsideArena(_Header->nSlotTableOffset, _SlotTableSize, _Header->nArenaSize))
    {
        throw std::runtime_error("Shared PDO arena slot table range is invalid");
    }
    const auto _SlotTableEnd = _Header->nSlotTableOffset + _SlotTableSize;
    if (_Header->nPayloadOffset < _Header->nSlotTableOffset)
    {
        throw std::runtime_error("Shared PDO arena payload offset is invalid");
    }
    if (_Header->nPayloadOffset < _SlotTableEnd || _Header->nPayloadOffset > _Header->nArenaSize)
    {
        throw std::runtime_error("Shared PDO arena payload range is invalid");
    }
    if (!IsAligned(_Header->nPayloadOffset))
    {
        throw std::runtime_error("Shared PDO arena payload offset is not aligned");
    }

    std::unordered_set<PDOID> _IDs;
    const auto* _Slots = reinterpret_cast<const SharedPDOSlotHeader*>(m_pBase + _Header->nSlotTableOffset);
    for (uint32_t _Index = 0; _Index < _Header->nSlotCount; ++_Index)
    {
        const auto& _Slot = _Slots[_Index];
        if (_Slot.nID == 0 || !_IDs.insert(_Slot.nID).second)
        {
            throw std::runtime_error("Shared PDO arena slot id is invalid");
        }
        if (_Slot.nVersion == 0)
        {
            throw std::runtime_error("Shared PDO arena slot version is invalid");
        }
        if (!IsValidDirection(_Slot.nDirection))
        {
            throw std::runtime_error("Shared PDO arena slot direction is invalid");
        }
        if (_Slot.nPayloadSize == 0)
        {
            throw std::runtime_error("Shared PDO arena slot payload size is invalid");
        }
        if (!IsValidBufferIndex(_Slot.nPublishedIndex)
            || !IsValidBufferIndex(_Slot.nWriteIndex)
            || !IsValidReadyIndex(_Slot.nReadyIndex)
            || _Slot.nPublishedIndex == _Slot.nWriteIndex)
        {
            throw std::runtime_error("Shared PDO arena slot buffer index is invalid");
        }
        if (_Slot.nReaderCount[0] < 0 || _Slot.nReaderCount[1] < 0)
        {
            throw std::runtime_error("Shared PDO arena reader count is invalid");
        }
        if (_Slot.nLatestDataVersion < _Slot.nPublishedDataVersion)
        {
            throw std::runtime_error("Shared PDO arena latest data version is invalid");
        }
        if (_Slot.nBufferState[_Slot.nPublishedIndex] != kSharedPDOBufferPublished
            && _Slot.nBufferState[_Slot.nPublishedIndex] != kSharedPDOBufferReading)
        {
            throw std::runtime_error("Shared PDO arena published buffer state is invalid");
        }
        if (_Slot.nPublishedDataVersion != _Slot.nBufferDataVersion[_Slot.nPublishedIndex])
        {
            throw std::runtime_error("Shared PDO arena published data version is invalid");
        }
        if (_Slot.nReadyIndex != kSharedPDOReadyNone
            && _Slot.nBufferState[_Slot.nReadyIndex] != kSharedPDOBufferReady)
        {
            throw std::runtime_error("Shared PDO arena ready buffer state is invalid");
        }
        if (_Slot.nReadyIndex != kSharedPDOReadyNone
            && _Slot.nBufferDataVersion[_Slot.nReadyIndex] <= _Slot.nPublishedDataVersion)
        {
            throw std::runtime_error("Shared PDO arena ready data version is invalid");
        }
        if (_Slot.nReadyIndex != kSharedPDOReadyNone
            && _Slot.nLatestDataVersion < _Slot.nBufferDataVersion[_Slot.nReadyIndex])
        {
            throw std::runtime_error("Shared PDO arena latest ready data version is invalid");
        }

        for (uint32_t _BufferIndex = 0; _BufferIndex < kSharedPDOBufferCount; ++_BufferIndex)
        {
            if (!IsValidBufferState(_Slot.nBufferState[_BufferIndex]))
            {
                throw std::runtime_error("Shared PDO arena slot buffer state is invalid");
            }
            if (_Slot.nBufferState[_BufferIndex] == kSharedPDOBufferReading
                && _Slot.nReaderCount[_BufferIndex] <= 0)
            {
                throw std::runtime_error("Shared PDO arena reading buffer has no reader");
            }
            const auto _BufferOffset = _Slot.nBufferOffset[_BufferIndex];
            if (!IsAligned(_BufferOffset)
                || !IsRangeInsideArena(_BufferOffset, _Slot.nPayloadSize, _Header->nArenaSize))
            {
                throw std::runtime_error("Shared PDO arena slot buffer range is invalid");
            }
        }
        if (_Slot.nBufferOffset[0] == _Slot.nBufferOffset[1])
        {
            throw std::runtime_error("Shared PDO arena slot buffer offsets must be distinct");
        }
    }
}

iCAX::PDO::SharedPDOArenaHeader* iCAX::PDO::CSharedPDOArena::GetMutableHeader() const
{
    return reinterpret_cast<SharedPDOArenaHeader*>(m_pBase);
}

iCAX::PDO::SharedPDOSlotHeader* iCAX::PDO::CSharedPDOArena::GetSlotTable() const
{
    const auto* _Header = GetMutableHeader();
    return reinterpret_cast<SharedPDOSlotHeader*>(m_pBase + _Header->nSlotTableOffset);
}
