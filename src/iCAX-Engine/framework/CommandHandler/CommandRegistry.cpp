#include "pch.h"
#include "CommandRegistry.h"

#include <stdexcept>
#include <utility>

bool iCAX::Command::CCommandRegistry::Register(IN std::shared_ptr<ICommandTarget> pCommandTarget_)
{
    ValidateCommandTarget(pCommandTarget_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    const auto _MainCode = pCommandTarget_->GetMainCode();
    auto _Iter = m_mapCommandTargets.find(_MainCode);
    if (_Iter != m_mapCommandTargets.end())
    {
        ValidateMainCommandCollision(*_Iter->second, *pCommandTarget_);
        return false;
    }
    m_mapCommandTargets.emplace(_MainCode, std::move(pCommandTarget_));
    return true;
}

bool iCAX::Command::CCommandRegistry::Has(IN uint32_t nMainCode_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_mapCommandTargets.find(nMainCode_) != m_mapCommandTargets.end();
}

std::shared_ptr<iCAX::Command::ICommandTarget> iCAX::Command::CCommandRegistry::Find(IN uint32_t nMainCode_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Ite = m_mapCommandTargets.find(nMainCode_);
    if (_Ite == m_mapCommandTargets.end())
    {
        return nullptr;
    }
    return _Ite->second;
}

std::vector<uint32_t> iCAX::Command::CCommandRegistry::GetMainCodes() const
{
    std::vector<uint32_t> _MainCodes;

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    for (const auto& _Pair : m_mapCommandTargets)
    {
        _MainCodes.push_back(_Pair.first);
    }
    return _MainCodes;
}

std::vector<iCAX::Command::CCommandRoute> iCAX::Command::CCommandRegistry::GetCommandRoutes() const
{
    std::vector<std::shared_ptr<ICommandTarget>> _CommandTargets;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        _CommandTargets.reserve(m_mapCommandTargets.size());
        for (const auto& _Pair : m_mapCommandTargets)
        {
            _CommandTargets.push_back(_Pair.second);
        }
    }

    std::vector<CCommandRoute> _Routes;
    for (const auto& _pCommandTarget : _CommandTargets)
    {
        auto _SubRoutes = _pCommandTarget->GetSubCommandRoutes();
        _Routes.insert(_Routes.end(), _SubRoutes.begin(), _SubRoutes.end());
    }
    return _Routes;
}

void iCAX::Command::CCommandRegistry::ValidateCommandTarget(IN const std::shared_ptr<ICommandTarget>& pCommandTarget_)
{
    if (!pCommandTarget_)
    {
        throw std::invalid_argument("Command target cannot be null");
    }
    if (pCommandTarget_->GetMainCode() == 0)
    {
        throw std::invalid_argument("Command target main code cannot be zero");
    }
    if (!IsValidCommandName(pCommandTarget_->GetMainName()))
    {
        throw std::invalid_argument(
            "Command target main name must match [A-Z][A-Za-z0-9_]*: " +
            pCommandTarget_->GetMainName());
    }
}

void iCAX::Command::CCommandRegistry::ValidateMainCommandCollision(
    IN const ICommandTarget& ExistingTarget_,
    IN const ICommandTarget& NewTarget_)
{
    if (ExistingTarget_.GetMainName() != NewTarget_.GetMainName())
    {
        throw std::runtime_error(
            "Command main code collision: " +
            ExistingTarget_.GetMainName() +
            " vs " +
            NewTarget_.GetMainName());
    }
}
