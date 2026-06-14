#include "pch.h"
#include "VersionTable.h"

//! 获取版本
size_t iCAX::Database::VersionTable::GetVersion(IN const VersionKey& Key_) const
{
    auto _Ite = m_mapVersions.find(Key_);
    return (_Ite != m_mapVersions.end()) ? _Ite->second : 0;
}

//! 版本号+1
void iCAX::Database::VersionTable::BumpVersion(IN const VersionKey& Key_)
{
    m_mapVersions[Key_]++;
    m_setDirty.emplace(Key_);
}

//! 重置
void iCAX::Database::VersionTable::Reset(IN const VersionKey& Key_)
{
    m_mapVersions[Key_] = 1;
    m_setDirty.emplace(Key_);
}

//! 移除
void iCAX::Database::VersionTable::Remove(IN const VersionKey& Key_)
{
    m_mapVersions.erase(Key_);
    m_setDirty.emplace(Key_);
}

//! 清空
void iCAX::Database::VersionTable::Clear()
{
    m_mapVersions.clear();
    m_setDirty.clear();
}

//! 清空dirty标记
void iCAX::Database::VersionTable::ClearDirty()
{
    m_setDirty.clear();
}

//! 是否被污染
bool iCAX::Database::VersionTable::IsDirty(IN const VersionKey& Key_)
{
    return m_setDirty.find(Key_) != m_setDirty.end();
}


