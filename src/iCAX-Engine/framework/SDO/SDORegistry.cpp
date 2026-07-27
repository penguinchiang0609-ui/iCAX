#include "pch.h"
#include "SDORegistry.h"


bool iCAX::Interaction::CSDORegistry::Register(IN std::shared_ptr<ISDO> pSDO_)
{
    Validate(pSDO_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    const auto _Code = pSDO_->GetCode();
    const auto _Iter = m_mapSDO.find(_Code);
    if (_Iter != m_mapSDO.end())
    {
        ValidateCodeCollision(*_Iter->second, *pSDO_);
        return false;
    }
    m_mapSDO.emplace(_Code, std::move(pSDO_));
    return true;
}

bool iCAX::Interaction::CSDORegistry::Has(IN uint32_t nSDOCode_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_mapSDO.find(nSDOCode_) != m_mapSDO.end();
}

std::shared_ptr<iCAX::Interaction::ISDO> iCAX::Interaction::CSDORegistry::Find(IN uint32_t nSDOCode_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    const auto _Iter = m_mapSDO.find(nSDOCode_);
    return _Iter == m_mapSDO.end() ? nullptr : _Iter->second;
}

std::vector<uint32_t> iCAX::Interaction::CSDORegistry::GetCodes() const
{
    std::vector<uint32_t> _Codes;
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    _Codes.reserve(m_mapSDO.size());
    for (const auto& [_Code, _SDO] : m_mapSDO)
    {
        (void)_SDO;
        _Codes.push_back(_Code);
    }
    return _Codes;
}

std::vector<iCAX::Interaction::CSDOMethod> iCAX::Interaction::CSDORegistry::GetMethods() const
{
    std::vector<std::shared_ptr<ISDO>> _SDOs;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        _SDOs.reserve(m_mapSDO.size());
        for (const auto& [_Code, _SDO] : m_mapSDO)
        {
            (void)_Code;
            _SDOs.push_back(_SDO);
        }
    }

    std::vector<CSDOMethod> _Methods;
    for (const auto& _SDO : _SDOs)
    {
        auto _SDOMethods = _SDO->GetMethods();
        _Methods.insert(_Methods.end(), _SDOMethods.begin(), _SDOMethods.end());
    }
    return _Methods;
}

void iCAX::Interaction::CSDORegistry::Validate(IN const std::shared_ptr<ISDO>& pSDO_)
{
    if (!pSDO_)
    {
        throw std::invalid_argument("SDO cannot be null");
    }
    if (pSDO_->GetCode() == 0)
    {
        throw std::invalid_argument("SDO code cannot be zero");
    }
    if (!IsValidSDOName(pSDO_->GetName()))
    {
        throw std::invalid_argument("SDO name must match [A-Z][A-Za-z0-9_]*: " + pSDO_->GetName());
    }
}

void iCAX::Interaction::CSDORegistry::ValidateCodeCollision(
    IN const ISDO& ExistingSDO_,
    IN const ISDO& NewSDO_)
{
    if (ExistingSDO_.GetName() != NewSDO_.GetName())
    {
        throw std::runtime_error(
            "SDO code collision: " + ExistingSDO_.GetName() + " vs " + NewSDO_.GetName());
    }
}
