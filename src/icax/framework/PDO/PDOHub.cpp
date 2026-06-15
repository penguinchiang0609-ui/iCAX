#include "pch.h"
#include "PDOHub.h"
#include "PDOSlot.h"

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
    for (const PDODecl& _Item : Descs_)
    {
        auto _pSlot = std::make_shared<CPDOSlot>(_Item);
        m_mapSlots[_Item.nID] = _pSlot;
    }
}

//! 释放所有槽
void iCAX::PDO::CPDOHub::Clear()
{
    m_mapSlots.clear();
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

//! 交换内发槽的双缓冲
void iCAX::PDO::CPDOHub::SwapInSlot()
{
    for (auto& [_, _pSlot] : m_mapSlots)
    {
        if ((_pSlot->GetHeader().eDirection & kDirection2Inner) != kDirectionNil)
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
        if ((_pSlot->GetHeader().eDirection & kDirection2Externer) != kDirectionNil)
        {
            _pSlot->SwapBuffersIfReady();
        }
    }
}
