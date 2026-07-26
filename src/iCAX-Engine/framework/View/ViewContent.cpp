#include "pch.h"
#include "ViewContent.h"


bool iCAX::View::CViewDefinitionRegistry::Register(IN SViewDefinition Definition_)
{
    if (Definition_.ID.empty())
    {
        throw std::invalid_argument("View definition id cannot be empty");
    }
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    const auto _ID = Definition_.ID;
    return m_Definitions.emplace(_ID, std::move(Definition_)).second;
}

bool iCAX::View::CViewDefinitionRegistry::HasDefinition(
    IN const ViewDefinitionID& DefinitionID_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Definitions.find(DefinitionID_) != m_Definitions.end();
}

std::optional<iCAX::View::SViewDefinition>
iCAX::View::CViewDefinitionRegistry::GetDefinition(
    IN const ViewDefinitionID& DefinitionID_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    const auto _Iter = m_Definitions.find(DefinitionID_);
    return _Iter == m_Definitions.end()
        ? std::nullopt
        : std::optional<SViewDefinition>(_Iter->second);
}

std::vector<iCAX::View::ViewDefinitionID>
iCAX::View::CViewDefinitionRegistry::ListDefinitionIDs() const
{
    std::vector<ViewDefinitionID> _IDs;
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    _IDs.reserve(m_Definitions.size());
    for (const auto& [_ID, _Definition] : m_Definitions)
    {
        (void)_Definition;
        _IDs.push_back(_ID);
    }
    return _IDs;
}
