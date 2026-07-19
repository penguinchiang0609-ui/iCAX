#include "pch.h"
#include "FacadeRegistry.h"


bool iCAX::Interaction::CFacadeRegistry::Register(IN std::shared_ptr<IFacade> pFacade_)
{
    Validate(pFacade_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    const auto _Code = pFacade_->GetCode();
    const auto _Iter = m_mapFacades.find(_Code);
    if (_Iter != m_mapFacades.end())
    {
        ValidateCodeCollision(*_Iter->second, *pFacade_);
        return false;
    }
    m_mapFacades.emplace(_Code, std::move(pFacade_));
    return true;
}

bool iCAX::Interaction::CFacadeRegistry::Has(IN uint32_t nFacadeCode_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_mapFacades.find(nFacadeCode_) != m_mapFacades.end();
}

std::shared_ptr<iCAX::Interaction::IFacade> iCAX::Interaction::CFacadeRegistry::Find(IN uint32_t nFacadeCode_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    const auto _Iter = m_mapFacades.find(nFacadeCode_);
    return _Iter == m_mapFacades.end() ? nullptr : _Iter->second;
}

std::vector<uint32_t> iCAX::Interaction::CFacadeRegistry::GetCodes() const
{
    std::vector<uint32_t> _Codes;
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    _Codes.reserve(m_mapFacades.size());
    for (const auto& [_Code, _Facade] : m_mapFacades)
    {
        (void)_Facade;
        _Codes.push_back(_Code);
    }
    return _Codes;
}

std::vector<iCAX::Interaction::CFacadeMethod> iCAX::Interaction::CFacadeRegistry::GetMethods() const
{
    std::vector<std::shared_ptr<IFacade>> _Facades;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        _Facades.reserve(m_mapFacades.size());
        for (const auto& [_Code, _Facade] : m_mapFacades)
        {
            (void)_Code;
            _Facades.push_back(_Facade);
        }
    }

    std::vector<CFacadeMethod> _Methods;
    for (const auto& _Facade : _Facades)
    {
        auto _FacadeMethods = _Facade->GetMethods();
        _Methods.insert(_Methods.end(), _FacadeMethods.begin(), _FacadeMethods.end());
    }
    return _Methods;
}

void iCAX::Interaction::CFacadeRegistry::Validate(IN const std::shared_ptr<IFacade>& pFacade_)
{
    if (!pFacade_)
    {
        throw std::invalid_argument("Facade cannot be null");
    }
    if (pFacade_->GetCode() == 0)
    {
        throw std::invalid_argument("Facade code cannot be zero");
    }
    if (!IsValidFacadeName(pFacade_->GetName()))
    {
        throw std::invalid_argument("Facade name must match [A-Z][A-Za-z0-9_]*: " + pFacade_->GetName());
    }
}

void iCAX::Interaction::CFacadeRegistry::ValidateCodeCollision(
    IN const IFacade& ExistingFacade_,
    IN const IFacade& NewFacade_)
{
    if (ExistingFacade_.GetName() != NewFacade_.GetName())
    {
        throw std::runtime_error(
            "Facade code collision: " + ExistingFacade_.GetName() + " vs " + NewFacade_.GetName());
    }
}
