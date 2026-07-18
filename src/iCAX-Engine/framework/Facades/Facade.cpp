#include "pch.h"
#include "Facade.h"

#include <stdexcept>
#include <utility>

iCAX::Interaction::CFacade::CFacade(IN std::string strName_)
    : m_strName(std::move(strName_))
{
    if (!IsValidFacadeName(m_strName))
    {
        throw std::invalid_argument("Facade name must match [A-Z][A-Za-z0-9_]*: " + m_strName);
    }

    m_nCode = InteractionNameHash32(m_strName);
    if (m_nCode == 0)
    {
        throw std::invalid_argument("Facade code cannot be zero: " + m_strName);
    }
}

const std::string& iCAX::Interaction::CFacade::GetName() const
{
    return m_strName;
}

uint32_t iCAX::Interaction::CFacade::GetCode() const
{
    return m_nCode;
}

bool iCAX::Interaction::CFacade::HasMethod(IN uint32_t nMethodCode_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_mapMethods.find(nMethodCode_) != m_mapMethods.end();
}

std::vector<iCAX::Interaction::CFacadeMethod> iCAX::Interaction::CFacade::GetMethods() const
{
    std::vector<CFacadeMethod> _Methods;
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    _Methods.reserve(m_mapMethods.size());
    for (const auto& [_Code, _Record] : m_mapMethods)
    {
        (void)_Code;
        _Methods.push_back(_Record.Method);
    }
    return _Methods;
}

iCAX::Interaction::CFacadeResult iCAX::Interaction::CFacade::Invoke(
    IN const CFacadeCall& Call_,
    IN iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    if (Call_.Method.nFacadeCode != m_nCode)
    {
        CFacadeResult _Result;
        _Result.nStatus = EFacadeCallStatus::InvalidCall;
        _Result.strError = "Facade call does not belong to " + m_strName + ": " + FormatFacadeMethod(Call_.Method);
        return _Result;
    }

    MethodFunc _Func;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        const auto _Iter = m_mapMethods.find(Call_.Method.nMethodCode);
        if (_Iter == m_mapMethods.end())
        {
            CFacadeResult _Result;
            _Result.nStatus = EFacadeCallStatus::MethodNotFound;
            _Result.strError = "Facade method not found: " + FormatFacadeMethod(Call_.Method);
            return _Result;
        }
        _Func = _Iter->second.Func;
    }

    return _Func(Call_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
}

bool iCAX::Interaction::CFacade::ExposeMethod(IN std::string strMethodName_, IN MethodFunc Func_)
{
    if (!IsValidMethodName(strMethodName_))
    {
        throw std::invalid_argument("Facade method name must match [A-Z][A-Za-z0-9_]*: " + strMethodName_);
    }
    ValidateMethodFunc(Func_);

    auto _Method = MakeFacadeMethod(m_strName, strMethodName_);
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    const auto _Iter = m_mapMethods.find(_Method.nMethodCode);
    if (_Iter != m_mapMethods.end())
    {
        if (_Iter->second.Method.strMethodName != _Method.strMethodName)
        {
            throw std::runtime_error(
                "Facade method code collision: " +
                FormatFacadeMethod(_Iter->second.Method) + " vs " + FormatFacadeMethod(_Method));
        }
        return false;
    }

    m_mapMethods.emplace(_Method.nMethodCode, MethodRecord{ std::move(_Method), std::move(Func_) });
    return true;
}

void iCAX::Interaction::CFacade::ValidateMethodFunc(IN const MethodFunc& Func_)
{
    if (!Func_)
    {
        throw std::invalid_argument("Facade method function cannot be empty");
    }
}
