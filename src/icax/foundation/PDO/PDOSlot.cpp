#include "pch.h"
#include "PDOSlot.h"

//! 构造函数
iCAX::PDO::CPDOSlot::CPDOSlot(IN const PDODecl& Decl_)
    : m_Decl(Decl_)
{
    m_pDataArray[0] = new uint8_t[m_Decl.nPayloadSize];
    m_pDataArray[1] = new uint8_t[m_Decl.nPayloadSize];

    m_nWriteIndex.store(0);
    m_nReadIndex.store(1);

    m_nSlotStatus[0].store(ESlotStatus::Empty);
    m_nSlotStatus[1].store(ESlotStatus::Empty);
}

//! 析构函数
iCAX::PDO::CPDOSlot::~CPDOSlot()
{
    delete[] m_pDataArray[0];
    delete[] m_pDataArray[1];
}

//! 获取头部信息
const iCAX::PDO::PDODecl& iCAX::PDO::CPDOSlot::GetHeader() const
{
    return m_Decl;
}

//! 获取写数据
void* iCAX::PDO::CPDOSlot::GetWriteData()
{
    return m_pDataArray[m_nWriteIndex.load(std::memory_order_relaxed)];
}

//! 标记写结束
void iCAX::PDO::CPDOSlot::MarkWriteReady()
{
    uint8_t wi = m_nWriteIndex.load(std::memory_order_relaxed);
    m_nSlotStatus[wi].store(ESlotStatus::Ready, std::memory_order_release);
}

//! 获取读数据
const void* iCAX::PDO::CPDOSlot::GetReadData() const
{
    return m_pDataArray[m_nReadIndex.load(std::memory_order_acquire)];
}

//! 切换双缓冲
void iCAX::PDO::CPDOSlot::SwapBuffersIfReady()
{
    uint8_t wi = m_nWriteIndex.load(std::memory_order_acquire);
    uint8_t ri = m_nReadIndex.load(std::memory_order_acquire);

    if (m_nSlotStatus[wi].load(std::memory_order_acquire) == ESlotStatus::Ready)
    {
        // 交换索引
        m_nWriteIndex.store(ri, std::memory_order_release);
        m_nReadIndex.store(wi, std::memory_order_release);

        // 新写 buffer 重置状态
        m_nSlotStatus[ri].store(ESlotStatus::Empty, std::memory_order_relaxed);
    }
}