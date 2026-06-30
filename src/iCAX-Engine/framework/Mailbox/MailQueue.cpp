#include "pch.h"
#include "MailQueue.h"
#include "MailPayload.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
    constexpr size_t kMailPayloadAlignment = 8;

    bool _AddWouldOverflow(IN size_t nLeft_, IN size_t nRight_) noexcept
    {
        return nLeft_ > (std::numeric_limits<size_t>::max)() - nRight_;
    }
}

iCAX::Mail::CMailQueue::CMailQueue()
{
    InitializeStorage(CMailQueueCreateInfo{});
}

iCAX::Mail::CMailQueue::CMailQueue(IN const CMailQueueCreateInfo& CreateInfo_)
{
    InitializeStorage(CreateInfo_);
}

iCAX::Mail::CMailQueue::~CMailQueue()
{
    Clear();
}

iCAX::Mail::CMailQueue::CMailQueue(CMailQueue&& Other_) noexcept
{
    std::lock_guard<std::mutex> _Lock(Other_.m_Mutex);
    m_CreateInfo = Other_.m_CreateInfo;
    m_Records = std::move(Other_.m_Records);
    m_FreeRecordIndices = std::move(Other_.m_FreeRecordIndices);
    m_PendingRecordIndices = std::move(Other_.m_PendingRecordIndices);
    m_PayloadStorage = std::move(Other_.m_PayloadStorage);
    m_FreePayloadBlocks = std::move(Other_.m_FreePayloadBlocks);
}

iCAX::Mail::CMailQueue& iCAX::Mail::CMailQueue::operator=(CMailQueue&& Other_) noexcept
{
    if (this == &Other_)
    {
        return *this;
    }

    std::scoped_lock _Lock(m_Mutex, Other_.m_Mutex);
    for (const auto _RecordIndex : m_PendingRecordIndices)
    {
        ReleaseRecordNoLock(_RecordIndex);
    }
    m_PendingRecordIndices.clear();

    m_CreateInfo = Other_.m_CreateInfo;
    m_Records = std::move(Other_.m_Records);
    m_FreeRecordIndices = std::move(Other_.m_FreeRecordIndices);
    m_PendingRecordIndices = std::move(Other_.m_PendingRecordIndices);
    m_PayloadStorage = std::move(Other_.m_PayloadStorage);
    m_FreePayloadBlocks = std::move(Other_.m_FreePayloadBlocks);
    return *this;
}

void iCAX::Mail::CMailQueue::Enqueue(IN const Mail& Mail_)
{
    if (!TryEnqueue(Mail_))
    {
        throw std::runtime_error("mail queue preallocated storage is full");
    }
}

bool iCAX::Mail::CMailQueue::TryEnqueue(IN const Mail& Mail_)
{
    return TryEnqueuePayload(Mail_.Header, Mail_.Payload.pData, Mail_.Payload.nSize);
}

void iCAX::Mail::CMailQueue::EnqueuePayload(
    IN const MailHeader& Header_,
    IN const void* pPayload_,
    IN size_t nPayloadSize_)
{
    if (!TryEnqueuePayload(Header_, pPayload_, nPayloadSize_))
    {
        throw std::runtime_error("mail queue preallocated storage is full");
    }
}

bool iCAX::Mail::CMailQueue::TryEnqueuePayload(
    IN const MailHeader& Header_,
    IN const void* pPayload_,
    IN size_t nPayloadSize_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return TryEnqueuePayloadNoLock(Header_, pPayload_, nPayloadSize_);
}

std::vector<iCAX::Mail::Mail> iCAX::Mail::CMailQueue::Drain()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return DrainNoLock();
}

void iCAX::Mail::CMailQueue::Clear()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    for (const auto _RecordIndex : m_PendingRecordIndices)
    {
        ReleaseRecordNoLock(_RecordIndex);
    }
    m_PendingRecordIndices.clear();
}

size_t iCAX::Mail::CMailQueue::GetPendingCount() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_PendingRecordIndices.size();
}

size_t iCAX::Mail::CMailQueue::GetFreeRecordCount() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_FreeRecordIndices.size();
}

size_t iCAX::Mail::CMailQueue::GetFreePayloadBytes() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    size_t _Total = 0;
    for (const auto& _Block : m_FreePayloadBlocks)
    {
        if (_AddWouldOverflow(_Total, _Block.nSize))
        {
            return (std::numeric_limits<size_t>::max)();
        }
        _Total += _Block.nSize;
    }
    return _Total;
}

void iCAX::Mail::CMailQueue::ReleaseMailPayloadLease(
    IN size_t nLeaseIndex_,
    IN uint32_t nLeaseGeneration_) noexcept
{
    try
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        if (nLeaseIndex_ >= m_Records.size())
        {
            return;
        }

        const auto& _Record = m_Records[nLeaseIndex_];
        if (_Record.eState != ERecordState::Leased || _Record.nGeneration != nLeaseGeneration_)
        {
            return;
        }
        ReleaseRecordNoLock(nLeaseIndex_);
    }
    catch (...)
    {
    }
}

void iCAX::Mail::CMailQueue::InitializeStorage(IN const CMailQueueCreateInfo& CreateInfo_)
{
    if (CreateInfo_.nMaxMailCount == 0)
    {
        throw std::invalid_argument("mail queue max mail count cannot be zero");
    }
    if (CreateInfo_.nMaxPayloadBytesPerMail > CreateInfo_.nPayloadPoolBytes
        && CreateInfo_.nPayloadPoolBytes != 0)
    {
        throw std::invalid_argument("mail queue max payload bytes per mail exceeds payload pool size");
    }

    m_CreateInfo = CreateInfo_;
    m_Records.clear();
    m_Records.resize(m_CreateInfo.nMaxMailCount);
    m_FreeRecordIndices.clear();
    m_FreeRecordIndices.reserve(m_Records.size());
    for (size_t _Index = m_Records.size(); _Index > 0; --_Index)
    {
        m_FreeRecordIndices.push_back(static_cast<uint32_t>(_Index - 1));
    }

    m_PendingRecordIndices.clear();
    m_PendingRecordIndices.reserve(m_Records.size());
    m_PayloadStorage.assign(m_CreateInfo.nPayloadPoolBytes, 0);
    m_FreePayloadBlocks.clear();
    m_FreePayloadBlocks.reserve(m_Records.size() + 1);
    if (!m_PayloadStorage.empty())
    {
        m_FreePayloadBlocks.push_back(CFreeBlock{ 0, m_PayloadStorage.size() });
    }
}

bool iCAX::Mail::CMailQueue::TryEnqueuePayloadNoLock(
    IN const MailHeader& Header_,
    IN const void* pPayload_,
    IN size_t nPayloadSize_)
{
    if (nPayloadSize_ > 0 && pPayload_ == nullptr)
    {
        throw std::invalid_argument("mail payload data is null");
    }
    if (nPayloadSize_ > m_CreateInfo.nMaxPayloadBytesPerMail)
    {
        throw std::runtime_error("mail payload exceeds max payload bytes per mail");
    }
    if (m_FreeRecordIndices.empty())
    {
        return false;
    }

    CFreeBlock _PayloadBlock;
    if (nPayloadSize_ > 0)
    {
        const auto _PayloadBlockSize = AlignPayloadSize(nPayloadSize_);
        if (!TryAllocatePayloadBlockNoLock(_PayloadBlockSize, _PayloadBlock))
        {
            return false;
        }
    }

    const auto _RecordIndex = m_FreeRecordIndices.back();
    m_FreeRecordIndices.pop_back();

    auto& _Record = m_Records[_RecordIndex];
    _Record.Header = Header_;
    _Record.nPayloadOffset = _PayloadBlock.nOffset;
    _Record.nPayloadSize = nPayloadSize_;
    _Record.nPayloadBlockSize = _PayloadBlock.nSize;
    ++_Record.nGeneration;
    if (_Record.nGeneration == 0)
    {
        ++_Record.nGeneration;
    }
    _Record.eState = ERecordState::Pending;

    if (nPayloadSize_ > 0)
    {
        std::memcpy(m_PayloadStorage.data() + _Record.nPayloadOffset, pPayload_, nPayloadSize_);
    }
    m_PendingRecordIndices.push_back(_RecordIndex);
    return true;
}

std::vector<iCAX::Mail::Mail> iCAX::Mail::CMailQueue::DrainNoLock()
{
    std::vector<Mail> _Mails;
    _Mails.reserve(m_PendingRecordIndices.size());
    auto _pLeaseOwner = TryGetLeaseOwnerNoLock();

    for (const auto _RecordIndex : m_PendingRecordIndices)
    {
        auto& _Record = m_Records[_RecordIndex];
        if (_Record.eState != ERecordState::Pending)
        {
            continue;
        }

        Mail _Mail;
        _Mail.Header = _Record.Header;
        _Mail.Payload.nSize = _Record.nPayloadSize;
        if (_Record.nPayloadSize > 0)
        {
            if (_pLeaseOwner)
            {
                _Mail.Payload.pData = m_PayloadStorage.data() + _Record.nPayloadOffset;
                _Mail.Payload.pLease = _pLeaseOwner;
                _Mail.Payload.nLeaseIndex = _RecordIndex;
                _Mail.Payload.nLeaseGeneration = _Record.nGeneration;
                _Record.eState = ERecordState::Leased;
            }
            else
            {
                auto _Payload = std::make_unique<uint8_t[]>(_Record.nPayloadSize);
                std::memcpy(_Payload.get(), m_PayloadStorage.data() + _Record.nPayloadOffset, _Record.nPayloadSize);
                _Mail.Payload.pData = _Payload.release();
                ReleaseRecordNoLock(_RecordIndex);
            }
        }
        else if (_pLeaseOwner)
        {
            _Mail.Payload.pLease = _pLeaseOwner;
            _Mail.Payload.nLeaseIndex = _RecordIndex;
            _Mail.Payload.nLeaseGeneration = _Record.nGeneration;
            _Record.eState = ERecordState::Leased;
        }
        else
        {
            ReleaseRecordNoLock(_RecordIndex);
        }
        _Mails.push_back(std::move(_Mail));
    }

    m_PendingRecordIndices.clear();
    return _Mails;
}

std::shared_ptr<iCAX::Mail::IMailPayloadLease> iCAX::Mail::CMailQueue::TryGetLeaseOwnerNoLock() noexcept
{
    try
    {
        return shared_from_this();
    }
    catch (...)
    {
        return {};
    }
}

bool iCAX::Mail::CMailQueue::TryAllocatePayloadBlockNoLock(IN size_t nSize_, OUT CFreeBlock& Block_)
{
    Block_ = {};
    for (auto _Iter = m_FreePayloadBlocks.begin(); _Iter != m_FreePayloadBlocks.end(); ++_Iter)
    {
        if (_Iter->nSize < nSize_)
        {
            continue;
        }

        Block_ = CFreeBlock{ _Iter->nOffset, nSize_ };
        _Iter->nOffset += nSize_;
        _Iter->nSize -= nSize_;
        if (_Iter->nSize == 0)
        {
            m_FreePayloadBlocks.erase(_Iter);
        }
        return true;
    }
    return false;
}

void iCAX::Mail::CMailQueue::ReturnPayloadBlockNoLock(IN CFreeBlock Block_)
{
    if (Block_.nSize == 0)
    {
        return;
    }

    m_FreePayloadBlocks.push_back(Block_);
    std::sort(m_FreePayloadBlocks.begin(), m_FreePayloadBlocks.end(), [](const CFreeBlock& LHS_, const CFreeBlock& RHS_) {
        return LHS_.nOffset < RHS_.nOffset;
        });

    std::vector<CFreeBlock> _Merged;
    _Merged.reserve(m_FreePayloadBlocks.size());
    for (const auto& _Block : m_FreePayloadBlocks)
    {
        if (_Merged.empty() || _Merged.back().nOffset + _Merged.back().nSize < _Block.nOffset)
        {
            _Merged.push_back(_Block);
            continue;
        }

        auto& _Last = _Merged.back();
        const auto _LastEnd = _Last.nOffset + _Last.nSize;
        const auto _BlockEnd = _Block.nOffset + _Block.nSize;
        if (_BlockEnd > _LastEnd)
        {
            _Last.nSize = _BlockEnd - _Last.nOffset;
        }
    }
    m_FreePayloadBlocks = std::move(_Merged);
}

void iCAX::Mail::CMailQueue::ReleaseRecordNoLock(IN size_t nRecordIndex_) noexcept
{
    if (nRecordIndex_ >= m_Records.size())
    {
        return;
    }

    auto& _Record = m_Records[nRecordIndex_];
    if (_Record.eState == ERecordState::Free)
    {
        return;
    }

    if (_Record.nPayloadBlockSize > 0)
    {
        ReturnPayloadBlockNoLock(CFreeBlock{ _Record.nPayloadOffset, _Record.nPayloadBlockSize });
    }

    _Record.Header = MailHeader{};
    _Record.nPayloadOffset = 0;
    _Record.nPayloadSize = 0;
    _Record.nPayloadBlockSize = 0;
    _Record.eState = ERecordState::Free;
    m_FreeRecordIndices.push_back(static_cast<uint32_t>(nRecordIndex_));
}

size_t iCAX::Mail::CMailQueue::AlignPayloadSize(IN size_t nSize_) noexcept
{
    if (nSize_ == 0)
    {
        return 0;
    }
    return (nSize_ + kMailPayloadAlignment - 1) / kMailPayloadAlignment * kMailPayloadAlignment;
}
