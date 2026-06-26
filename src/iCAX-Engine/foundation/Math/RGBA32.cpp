#include "pch.h"
#include "RGBA32.h"

//!< 默认构造函数，初始化为白色不透明
iCAX::Math::RGBA32::RGBA32()
    : m_Data{ 255, 255, 255, 255 }
{
}

//!< 默认构造函数，初始化为白色不透明
iCAX::Math::RGBA32::RGBA32(IN const iCAX::Data::Byte4& nColor_)
    : m_Data(nColor_)
{
}

//!< 构造函数，指定 RGBA 分量
iCAX::Math::RGBA32::RGBA32(IN const unsigned char& nRed_, IN const unsigned char& nGreen_, IN const unsigned char& nBlue_, IN const unsigned char& nAlpha_)
{
    m_Data[0] = nRed_;
    m_Data[1] = nGreen_;
    m_Data[2] = nBlue_;
    m_Data[3] = nAlpha_;
}

//!< 析构函数
iCAX::Math::RGBA32::~RGBA32()
{
}

//!< 红色分量
unsigned char& iCAX::Math::RGBA32::R()
{
    return m_Data[0];
}
//!< 红色分量
const unsigned char& iCAX::Math::RGBA32::R() const
{
    return m_Data[0];
}

//!< 绿色分量
unsigned char& iCAX::Math::RGBA32::G()
{
    return m_Data[1];
}
//!< 绿色分量
const unsigned char& iCAX::Math::RGBA32::G() const
{
    return m_Data[1];
}
//!< 蓝色分量
unsigned char& iCAX::Math::RGBA32::B()
{
    return m_Data[2];
}
//!< 蓝色分量
const unsigned char& iCAX::Math::RGBA32::B() const
{
    return m_Data[2];
}
//!< Alpha 分量
unsigned char& iCAX::Math::RGBA32::A()
{
    return m_Data[3];
}
//!< Alpha 分量
const unsigned char& iCAX::Math::RGBA32::A() const
{
    return m_Data[3];
}

//!< 整体 RGBA 数据
iCAX::Data::Byte4& iCAX::Math::RGBA32::RGBA() { return m_Data; }
//!< 整体 RGBA 数据
const iCAX::Data::Byte4& iCAX::Math::RGBA32::RGBA() const { return m_Data; }