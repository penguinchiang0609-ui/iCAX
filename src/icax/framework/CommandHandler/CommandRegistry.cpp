#include "pch.h"
#include "CommandRegistry.h"

#include <stdexcept>
#include <utility>

bool iCAX::Command::CCommandRegistry::Register(IN uint64_t nTypeCode_, IN std::shared_ptr<ICommandHandler> pHandler_)
{
    ValidateHandler(pHandler_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_mapHandlers.emplace(nTypeCode_, std::move(pHandler_)).second;
}

void iCAX::Command::CCommandRegistry::Set(IN uint64_t nTypeCode_, IN std::shared_ptr<ICommandHandler> pHandler_)
{
    ValidateHandler(pHandler_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_mapHandlers[nTypeCode_] = std::move(pHandler_);
}

bool iCAX::Command::CCommandRegistry::Unregister(IN uint64_t nTypeCode_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_mapHandlers.erase(nTypeCode_) > 0;
}

bool iCAX::Command::CCommandRegistry::Has(IN uint64_t nTypeCode_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_mapHandlers.find(nTypeCode_) != m_mapHandlers.end();
}

std::shared_ptr<iCAX::Command::ICommandHandler> iCAX::Command::CCommandRegistry::Find(IN uint64_t nTypeCode_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Ite = m_mapHandlers.find(nTypeCode_);
    if (_Ite == m_mapHandlers.end())
    {
        return nullptr;
    }
    return _Ite->second;
}

std::vector<uint64_t> iCAX::Command::CCommandRegistry::GetTypeCodes() const
{
    std::vector<uint64_t> _TypeCodes;

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    for (const auto& _Pair : m_mapHandlers)
    {
        _TypeCodes.push_back(_Pair.first);
    }
    return _TypeCodes;
}

void iCAX::Command::CCommandRegistry::Clear()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_mapHandlers.clear();
}

void iCAX::Command::CCommandRegistry::ValidateHandler(IN const std::shared_ptr<ICommandHandler>& pHandler_)
{
    if (!pHandler_)
    {
        throw std::invalid_argument("Command handler cannot be null");
    }
}
