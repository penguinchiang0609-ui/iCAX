#include "pch.h"
#include "NcPrimary.h"

//!< 构造函数
iCAX::NcData::ncPrimary::ncPrimary(IN const ncModal& Modal_)
    : m_nType(kPrimaryModal)
    , m_Modal(Modal_)
{
}

//!< 构造函数
iCAX::NcData::ncPrimary::ncPrimary(IN const ncPath& Path_)
    : m_nType(kPrimaryPath)
    , m_Path(Path_)
{
}

//!< 构造函数
iCAX::NcData::ncPrimary::ncPrimary(IN const std::vector<ncPrimary>& Primaries_)
    : m_nType(kPrimaryBlock)
    , m_pBlock(nullptr)
{
    m_pBlock = new ncBlock();
    m_pBlock->Primaries = Primaries_;
}

//!< 拷贝构造函数
iCAX::NcData::ncPrimary::ncPrimary(IN const ncPrimary& Right_)
    : m_nType(Right_.m_nType)
    , m_Path()
    , m_PreAuxes(Right_.m_PreAuxes)
    , m_PostAuxes(Right_.m_PostAuxes)
{
    switch (m_nType)
    {
    case iCAX::NcData::kPrimaryPath:
        m_Path = Right_.m_Path;
        break;
    case iCAX::NcData::kPrimaryModal:
        m_Modal = Right_.m_Modal;
        break;
    case iCAX::NcData::kPrimaryBlock:
        if (Right_.m_pBlock != nullptr)
        {
            m_pBlock = new ncBlock(*Right_.m_pBlock);
        }
        else
        {
            m_pBlock = nullptr;
        }
        break;
    default:
        break;
    }
}

//!< 赋值构造函数
iCAX::NcData::ncPrimary& iCAX::NcData::ncPrimary::operator=(IN const ncPrimary& Right_)
{
    m_PreAuxes = Right_.m_PreAuxes;
    m_PostAuxes = Right_.m_PostAuxes;

    switch (m_nType)
    {
    case iCAX::NcData::kPrimaryPath:
        m_Path = {};
        break;
    case iCAX::NcData::kPrimaryModal:
        m_Modal = {};
        break;
    case iCAX::NcData::kPrimaryBlock:
        if (m_pBlock != nullptr)
        {
            delete m_pBlock;
            m_pBlock = nullptr;
        }
        break;
    default:
        break;
    }
    m_nType = Right_.m_nType;
    switch (m_nType)
    {
    case iCAX::NcData::kPrimaryPath:
        m_Path = Right_.m_Path;
        break;
    case iCAX::NcData::kPrimaryModal:
        m_Modal = Right_.m_Modal;
        break;
    case iCAX::NcData::kPrimaryBlock:
        if (Right_.m_pBlock != nullptr)
        {
            m_pBlock = new ncBlock(*Right_.m_pBlock);
        }
        else
        {
            m_pBlock = nullptr;
        }
        break;
    default:
        break;
    }
    return *this;
}

//!< 析构函数
iCAX::NcData::ncPrimary::~ncPrimary()
{
    switch (m_nType)
    {
    case iCAX::NcData::kPrimaryPath:
        m_Path = {};
        break;
    case iCAX::NcData::kPrimaryModal:
        m_Modal = {};
        break;
    case iCAX::NcData::kPrimaryBlock:
        if (m_pBlock != nullptr)
        {
            delete m_pBlock;
            m_pBlock = nullptr;
        }
        break;
    default:
        break;
    }
}

//!< 获取类型
iCAX::NcData::ncPrimaryType iCAX::NcData::ncPrimary::GetType() const
{
    return m_nType;
}

//!< 获取模态指令
iCAX::NcData::ncModal& iCAX::NcData::ncPrimary::GetModal()
{
    return m_Modal;
}

//!< 获取模态指令
const iCAX::NcData::ncModal& iCAX::NcData::ncPrimary::GetModal() const
{
    return m_Modal;
}

//!< 获取路径
iCAX::NcData::ncPath& iCAX::NcData::ncPrimary::GetPath()
{
    return m_Path;
}

//!< 获取路径
const iCAX::NcData::ncPath& iCAX::NcData::ncPrimary::GetPath() const
{
    return m_Path;
}

//!< 获取块
iCAX::NcData::ncBlock* const iCAX::NcData::ncPrimary::GetBlock()
{
    return m_pBlock;
}

//!< 获取块
const iCAX::NcData::ncBlock* const iCAX::NcData::ncPrimary::GetBlock() const
{
    return m_pBlock;
}

//!< 获取前置辅助元素列表
std::vector<iCAX::NcData::ncAux>& iCAX::NcData::ncPrimary::GetPreAuxes()
{
    return m_PreAuxes;
}

//!< 获取前置辅助元素列表
const std::vector<iCAX::NcData::ncAux>& iCAX::NcData::ncPrimary::GetPreAuxes() const
{
    return m_PreAuxes;
}

//!< 获取后置辅助元素列表
std::vector<iCAX::NcData::ncAux>& iCAX::NcData::ncPrimary::GetPostAuxes()
{
    return m_PostAuxes;
}

//!< 获取后置辅助元素列表
const std::vector<iCAX::NcData::ncAux>& iCAX::NcData::ncPrimary::GetPostAuxes() const
{
    return m_PostAuxes;
}

