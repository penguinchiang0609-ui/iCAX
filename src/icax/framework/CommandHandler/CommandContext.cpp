#include "pch.h"
#include "CommandContext.h"

#include <utility>

void iCAX::Command::CCommandContext::SetDependencyUntyped(IN const std::type_index& Type_, IN std::shared_ptr<void> pValue_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (pValue_)
    {
        m_mapDependencies[Type_] = std::move(pValue_);
    }
    else
    {
        m_mapDependencies.erase(Type_);
    }
}

std::shared_ptr<void> iCAX::Command::CCommandContext::GetDependencyUntyped(IN const std::type_index& Type_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Ite = m_mapDependencies.find(Type_);
    if (_Ite == m_mapDependencies.end())
    {
        return nullptr;
    }
    return _Ite->second;
}

bool iCAX::Command::CCommandContext::HasDependency(IN const std::type_index& Type_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_mapDependencies.find(Type_) != m_mapDependencies.end();
}

void iCAX::Command::CCommandContext::RemoveDependency(IN const std::type_index& Type_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_mapDependencies.erase(Type_);
}

void iCAX::Command::CCommandContext::Clear()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_mapDependencies.clear();
}
