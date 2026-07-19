#include "pch.h"
#include "PDOHub.h"


namespace
{
    std::atomic<uint64_t> g_nPDOHubArenaSequence = 1;

    std::wstring MakeAnonymousSharedPDOArenaName()
    {
        const auto _Now = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto _Sequence = g_nPDOHubArenaSequence.fetch_add(1, std::memory_order_relaxed);
        return L"Local\\iCAX.PDO.Hub." + std::to_wstring(_Now) + L"." + std::to_wstring(_Sequence);
    }

    constexpr uint64_t kPDOHubAlignment = 64;

    uint64_t AlignUp(IN uint64_t nValue_, IN uint64_t nAlignment_)
    {
        return (nValue_ + nAlignment_ - 1) / nAlignment_ * nAlignment_;
    }

    uint64_t RequiredPayloadBlockSize(IN const iCAX::PDO::PDODecl& Decl_)
    {
        if (Decl_.nPayloadSize <= 0)
        {
            throw std::invalid_argument("PDO payload size must be positive");
        }
        const uint64_t _PayloadSize = AlignUp(static_cast<uint64_t>(Decl_.nPayloadSize), kPDOHubAlignment);
        if (_PayloadSize > (std::numeric_limits<uint64_t>::max)() / iCAX::PDO::kSharedPDOBufferCount)
        {
            throw std::overflow_error("PDO payload block size overflows");
        }
        return _PayloadSize * iCAX::PDO::kSharedPDOBufferCount;
    }
}

//! 构造函数
iCAX::PDO::CPDOHub::CPDOHub()
{
}

//! 析构函数
iCAX::PDO::CPDOHub::~CPDOHub()
{
    Clear();
}

//! 初始化槽
void iCAX::PDO::CPDOHub::Intialize(IN std::vector<PDODecl> Descs_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    Clear();

    m_strArenaName = MakeAnonymousSharedPDOArenaName();
    m_pArena = CSharedPDOArena::Create(m_strArenaName, Descs_);

    for (const auto& _Item : Descs_)
    {
        auto _Slot = m_pArena->GetSlot(_Item.nID);
        m_mapSlots[_Item.nID] = std::make_shared<CSharedPDOSlot>(_Slot);
    }
    RebuildFreeBlocks();
}

void iCAX::PDO::CPDOHub::Intialize(IN const CPDOHubCreateInfo& CreateInfo_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    Clear();

    m_strArenaName = MakeAnonymousSharedPDOArenaName();

    CSharedPDOArena::CCreateInfo _ArenaInfo;
    _ArenaInfo.nArenaSize = CreateInfo_.nArenaSize;
    _ArenaInfo.nSlotCapacity = CreateInfo_.nSlotCapacity;
    _ArenaInfo.InitialDeclarations = CreateInfo_.InitialDeclarations;
    m_pArena = CSharedPDOArena::Create(m_strArenaName, _ArenaInfo);

    for (const auto& _Item : m_pArena->GetDeclarations())
    {
        auto _Slot = m_pArena->GetSlot(_Item.nID);
        m_mapSlots[_Item.nID] = std::make_shared<CSharedPDOSlot>(_Slot);
    }
    RebuildFreeBlocks();
}

//! 释放所有槽
void iCAX::PDO::CPDOHub::Clear()
{
    m_mapSlots.clear();
    m_FreeBlocks.clear();
    m_pArena.reset();
    m_strArenaName.clear();
}

//! 获取槽
iCAX::PDO::IPDOSlot& iCAX::PDO::CPDOHub::GetSlot(IN const PDOID& nPDOID_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Ite = m_mapSlots.find(nPDOID_);
    if (_Ite == m_mapSlots.end())
    {
        throw std::runtime_error(std::format("iCAX::PDO::PDOHub::GetSlot {}", nPDOID_));
    }
    return *_Ite->second;
}

bool iCAX::PDO::CPDOHub::HasSlot(IN const PDOID& nPDOID_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_mapSlots.find(nPDOID_) != m_mapSlots.end();
}

iCAX::PDO::IPDOSlot& iCAX::PDO::CPDOHub::AllocateSlot(IN const PDODecl& Decl_)
{
    return AllocateSlot(Decl_, CPDOHubAllocationCallbacks{});
}

iCAX::PDO::IPDOSlot& iCAX::PDO::CPDOHub::AllocateSlot(
    IN const PDODecl& Decl_,
    IN const CPDOHubAllocationCallbacks& Callbacks_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (!m_pArena)
    {
        throw std::logic_error("PDOHub is not initialized");
    }
    if (m_mapSlots.find(Decl_.nID) != m_mapSlots.end())
    {
        throw std::runtime_error(std::format("Duplicate PDO slot id: {}", Decl_.nID));
    }

    const auto _RequiredBlockSize = RequiredPayloadBlockSize(Decl_);
    CFreeBlock _Block;
    if (!TryTakeFreeBlock(_RequiredBlockSize, _Block))
    {
        if (GetTotalFreeBytesNoLock() >= _RequiredBlockSize)
        {
            if (Callbacks_.OnDefragmentBegin)
            {
                Callbacks_.OnDefragmentBegin();
            }

            std::vector<CPDOHubDefragMove> _Moves;
            try
            {
                _Moves = DefragmentNoLock();
            }
            catch (...)
            {
                const auto _OriginalException = std::current_exception();
                if (Callbacks_.OnDefragmentEnd)
                {
                    try
                    {
                        Callbacks_.OnDefragmentEnd(_Moves);
                    }
                    catch (...)
                    {
                    }
                }
                std::rethrow_exception(_OriginalException);
            }

            if (Callbacks_.OnDefragmentEnd)
            {
                Callbacks_.OnDefragmentEnd(_Moves);
            }
        }
        if (!TryTakeFreeBlock(_RequiredBlockSize, _Block))
        {
            throw std::runtime_error("PDOHub does not have enough contiguous shared memory for slot");
        }
    }
    try
    {
        auto _Slot = m_pArena->AllocateSlot(Decl_, _Block.nOffset, _Block.nSize);
        auto _pSlot = std::make_shared<CSharedPDOSlot>(_Slot);
        auto [_Iter, _Inserted] = m_mapSlots.emplace(Decl_.nID, _pSlot);
        if (!_Inserted)
        {
            throw std::runtime_error(std::format("Duplicate PDO slot id: {}", Decl_.nID));
        }
        return *_Iter->second;
    }
    catch (...)
    {
        ReturnFreeBlock(_Block);
        throw;
    }
}

bool iCAX::PDO::CPDOHub::FreeSlot(IN const PDOID& nPDOID_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (!m_pArena)
    {
        throw std::logic_error("PDOHub is not initialized");
    }

    auto _Iter = m_mapSlots.find(nPDOID_);
    if (_Iter == m_mapSlots.end())
    {
        return false;
    }

    const auto& _SharedHeader = static_cast<CSharedPDOSlot&>(*_Iter->second).GetSharedHeader();
    CFreeBlock _Block{ _SharedHeader.nPayloadBlockOffset, _SharedHeader.nPayloadBlockSize };
    if (!m_pArena->FreeSlot(nPDOID_))
    {
        return false;
    }
    m_mapSlots.erase(_Iter);
    ReturnFreeBlock(_Block);
    return true;
}

const std::wstring& iCAX::PDO::CPDOHub::GetSharedArenaName() const noexcept
{
    return m_strArenaName;
}

uint64_t iCAX::PDO::CPDOHub::GetSharedArenaSize() const noexcept
{
    return m_pArena ? m_pArena->GetArenaSize() : 0;
}

std::vector<iCAX::PDO::PDODecl> iCAX::PDO::CPDOHub::GetPDODeclarations() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_pArena ? m_pArena->GetDeclarations() : std::vector<PDODecl>{};
}

std::shared_ptr<iCAX::PDO::CSharedPDOArena> iCAX::PDO::CPDOHub::GetSharedArena() const noexcept
{
    return m_pArena;
}

//! 交换内发槽的双缓冲
void iCAX::PDO::CPDOHub::SwapInSlot()
{
    std::vector<std::shared_ptr<CSharedPDOSlot>> _Slots;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        _Slots.reserve(m_mapSlots.size());
        for (const auto& [_, _pSlot] : m_mapSlots)
        {
            _Slots.push_back(_pSlot);
        }
    }
    for (auto& _pSlot : _Slots)
    {
        if (_pSlot->GetHeader().eDirection == kDirection2Inner)
        {
            _pSlot->SwapBuffersIfReady();
        }
    }
}

//! 交换外发槽的双缓冲
void iCAX::PDO::CPDOHub::SwapOutSlot()
{
    std::vector<std::shared_ptr<CSharedPDOSlot>> _Slots;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        _Slots.reserve(m_mapSlots.size());
        for (const auto& [_, _pSlot] : m_mapSlots)
        {
            _Slots.push_back(_pSlot);
        }
    }
    for (auto& _pSlot : _Slots)
    {
        if (_pSlot->GetHeader().eDirection == kDirection2External)
        {
            _pSlot->SwapBuffersIfReady();
        }
    }
}

iCAX::PDO::CPDOHub::CFreeBlock iCAX::PDO::CPDOHub::TakeFreeBlock(IN uint64_t nSize_)
{
    CFreeBlock _Block;
    if (TryTakeFreeBlock(nSize_, _Block))
    {
        return _Block;
    }
    throw std::runtime_error("PDOHub does not have enough contiguous shared memory for slot");
}

bool iCAX::PDO::CPDOHub::TryTakeFreeBlock(IN uint64_t nSize_, OUT CFreeBlock& Block_)
{
    Block_ = {};
    for (auto _Iter = m_FreeBlocks.begin(); _Iter != m_FreeBlocks.end(); ++_Iter)
    {
        if (_Iter->nSize < nSize_)
        {
            continue;
        }

        CFreeBlock _Result{ _Iter->nOffset, nSize_ };
        _Iter->nOffset += nSize_;
        _Iter->nSize -= nSize_;
        if (_Iter->nSize == 0)
        {
            m_FreeBlocks.erase(_Iter);
        }
        Block_ = _Result;
        return true;
    }
    return false;
}

void iCAX::PDO::CPDOHub::ReturnFreeBlock(IN CFreeBlock Block_)
{
    if (Block_.nSize == 0)
    {
        return;
    }

    m_FreeBlocks.push_back(Block_);
    std::sort(m_FreeBlocks.begin(), m_FreeBlocks.end(), [](const CFreeBlock& LHS_, const CFreeBlock& RHS_) {
        return LHS_.nOffset < RHS_.nOffset;
        });

    std::vector<CFreeBlock> _Merged;
    _Merged.reserve(m_FreeBlocks.size());
    for (const auto& _Block : m_FreeBlocks)
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
    m_FreeBlocks = std::move(_Merged);
}

uint64_t iCAX::PDO::CPDOHub::GetTotalFreeBytesNoLock() const
{
    uint64_t _Total = 0;
    for (const auto& _Block : m_FreeBlocks)
    {
        if (_Total > (std::numeric_limits<uint64_t>::max)() - _Block.nSize)
        {
            throw std::overflow_error("PDOHub free block total size overflows");
        }
        _Total += _Block.nSize;
    }
    return _Total;
}

uint64_t iCAX::PDO::CPDOHub::GetLargestFreeBlockBytesNoLock() const
{
    uint64_t _Largest = 0;
    for (const auto& _Block : m_FreeBlocks)
    {
        _Largest = (std::max)(_Largest, _Block.nSize);
    }
    return _Largest;
}

std::vector<iCAX::PDO::CPDOHubDefragMove> iCAX::PDO::CPDOHub::DefragmentNoLock()
{
    if (!m_pArena)
    {
        throw std::logic_error("PDOHub is not initialized");
    }

    std::vector<std::pair<PDOID, CFreeBlock>> _UsedBlocks;
    _UsedBlocks.reserve(m_mapSlots.size());
    for (const auto& [_PDOID, _pSlot] : m_mapSlots)
    {
        const auto& _SharedHeader = static_cast<CSharedPDOSlot&>(*_pSlot).GetSharedHeader();
        _UsedBlocks.push_back({
            _PDOID,
            CFreeBlock{ _SharedHeader.nPayloadBlockOffset, _SharedHeader.nPayloadBlockSize }
            });
    }
    std::sort(_UsedBlocks.begin(), _UsedBlocks.end(), [](const auto& LHS_, const auto& RHS_) {
        return LHS_.second.nOffset < RHS_.second.nOffset;
        });

    std::vector<CPDOHubDefragMove> _Moves;
    auto _FinishDefrag = [this]()
        {
            m_pArena->EndDefragment();
        };

    m_pArena->BeginDefragment();
    try
    {
        const auto& _Header = m_pArena->GetHeader();
        const uint64_t _PayloadEnd = _Header.nPayloadOffset + _Header.nPayloadSize;
        uint64_t _Cursor = _Header.nPayloadOffset;
        for (const auto& [_PDOID, _Block] : _UsedBlocks)
        {
            _Cursor = AlignUp(_Cursor, kPDOHubAlignment);
            if (_Cursor > _PayloadEnd || _Block.nSize > _PayloadEnd - _Cursor)
            {
                throw std::logic_error("PDOHub defragmentation target range exceeds arena payload");
            }

            if (_Block.nOffset != _Cursor)
            {
                const auto _MoveResult = m_pArena->MoveSlotPayload(_PDOID, _Cursor);
                _Moves.push_back(CPDOHubDefragMove{
                    _MoveResult.nPDOID,
                    _MoveResult.nOldPayloadBlockOffset,
                    _MoveResult.nNewPayloadBlockOffset,
                    _MoveResult.nPayloadBlockSize
                    });
            }
            _Cursor += _Block.nSize;
        }
        _FinishDefrag();
    }
    catch (...)
    {
        _FinishDefrag();
        throw;
    }

    RebuildFreeBlocks();
    return _Moves;
}

void iCAX::PDO::CPDOHub::RebuildFreeBlocks()
{
    m_FreeBlocks.clear();
    if (!m_pArena)
    {
        return;
    }

    const auto& _Header = m_pArena->GetHeader();
    std::vector<CFreeBlock> _UsedBlocks;
    for (const auto& _Decl : m_pArena->GetDeclarations())
    {
        const auto _Slot = m_pArena->GetSlot(_Decl.nID);
        const auto& _SharedHeader = _Slot.GetSharedHeader();
        _UsedBlocks.push_back(CFreeBlock{ _SharedHeader.nPayloadBlockOffset, _SharedHeader.nPayloadBlockSize });
    }
    std::sort(_UsedBlocks.begin(), _UsedBlocks.end(), [](const CFreeBlock& LHS_, const CFreeBlock& RHS_) {
        return LHS_.nOffset < RHS_.nOffset;
        });

    uint64_t _Cursor = _Header.nPayloadOffset;
    const uint64_t _PayloadEnd = _Header.nPayloadOffset + _Header.nPayloadSize;
    for (const auto& _Block : _UsedBlocks)
    {
        if (_Block.nOffset > _Cursor)
        {
            m_FreeBlocks.push_back(CFreeBlock{ _Cursor, _Block.nOffset - _Cursor });
        }
        _Cursor = (std::max)(_Cursor, _Block.nOffset + _Block.nSize);
    }
    if (_Cursor < _PayloadEnd)
    {
        m_FreeBlocks.push_back(CFreeBlock{ _Cursor, _PayloadEnd - _Cursor });
    }
}
