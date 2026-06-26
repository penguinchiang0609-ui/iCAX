#include "pch.h"
#include "PDOHub.h"

#include <atomic>
#include <chrono>
#include <format>
#include <stdexcept>

namespace
{
    std::atomic<uint64_t> g_nPDOHubArenaSequence = 1;

    std::wstring MakeAnonymousSharedPDOArenaName()
    {
        const auto _Now = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto _Sequence = g_nPDOHubArenaSequence.fetch_add(1, std::memory_order_relaxed);
        return L"Local\\iCAX.PDO.Hub." + std::to_wstring(_Now) + L"." + std::to_wstring(_Sequence);
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
    Clear();

    m_strArenaName = MakeAnonymousSharedPDOArenaName();
    m_pArena = CSharedPDOArena::Create(m_strArenaName, Descs_);

    for (const auto& _Item : Descs_)
    {
        auto _Slot = m_pArena->GetSlot(_Item.nID);
        m_mapSlots[_Item.nID] = std::make_shared<CSharedPDOSlot>(_Slot);
    }
}

//! 释放所有槽
void iCAX::PDO::CPDOHub::Clear()
{
    m_mapSlots.clear();
    m_pArena.reset();
    m_strArenaName.clear();
}

//! 获取槽
iCAX::PDO::IPDOSlot& iCAX::PDO::CPDOHub::GetSlot(IN const PDOID& nPDOID_)
{
    auto _Ite = m_mapSlots.find(nPDOID_);
    if (_Ite == m_mapSlots.end())
    {
        throw std::runtime_error(std::format("iCAX::PDO::PDOHub::GetSlot {}", nPDOID_));
    }
    return *_Ite->second;
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
    return m_pArena ? m_pArena->GetDeclarations() : std::vector<PDODecl>{};
}

std::shared_ptr<iCAX::PDO::CSharedPDOArena> iCAX::PDO::CPDOHub::GetSharedArena() const noexcept
{
    return m_pArena;
}

//! 交换内发槽的双缓冲
void iCAX::PDO::CPDOHub::SwapInSlot()
{
    for (auto& [_, _pSlot] : m_mapSlots)
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
    for (auto& [_, _pSlot] : m_mapSlots)
    {
        if (_pSlot->GetHeader().eDirection == kDirection2External)
        {
            _pSlot->SwapBuffersIfReady();
        }
    }
}
