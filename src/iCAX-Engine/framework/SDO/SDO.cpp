#include "pch.h"
#include "SDO.h"


iCAX::Interaction::CSDO::CSDO(IN std::string strName_)
    : m_strName(std::move(strName_))
{
    if (!IsValidSDOName(m_strName))
    {
        throw std::invalid_argument("SDO name must match [A-Z][A-Za-z0-9_]*: " + m_strName);
    }

    m_nCode = InteractionNameHash32(m_strName);
    if (m_nCode == 0)
    {
        throw std::invalid_argument("SDO code cannot be zero: " + m_strName);
    }
}

const std::string& iCAX::Interaction::CSDO::GetName() const
{
    return m_strName;
}

uint32_t iCAX::Interaction::CSDO::GetCode() const
{
    return m_nCode;
}

bool iCAX::Interaction::CSDO::HasMethod(IN uint32_t nMethodCode_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_mapMethods.find(nMethodCode_) != m_mapMethods.end();
}

std::vector<iCAX::Interaction::CSDOMethod> iCAX::Interaction::CSDO::GetMethods() const
{
    std::vector<CSDOMethod> _Methods;
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    _Methods.reserve(m_mapMethods.size());
    for (const auto& [_Code, _Record] : m_mapMethods)
    {
        (void)_Code;
        _Methods.push_back(_Record.Method);
    }
    return _Methods;
}

iCAX::Interaction::CInvocationResult iCAX::Interaction::CSDO::Invoke(
    IN const CInvocation& Call_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    if (Call_.Method.nSDOCode != m_nCode)
    {
        CInvocationResult _Result;
        _Result.nStatus = EInvocationStatus::InvalidInvocation;
        _Result.strError = "SDO invocation does not belong to " + m_strName + ": " + FormatSDOMethod(Call_.Method);
        return _Result;
    }

    MethodFunc _Func;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        const auto _Iter = m_mapMethods.find(Call_.Method.nMethodCode);
        if (_Iter == m_mapMethods.end())
        {
            CInvocationResult _Result;
            _Result.nStatus = EInvocationStatus::MethodNotFound;
            _Result.strError = "SDO method not found: " + FormatSDOMethod(Call_.Method);
            return _Result;
        }
        _Func = _Iter->second.Func;
    }

    return _Func(Call_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
}

bool iCAX::Interaction::CSDO::ExposeMethod(IN std::string strMethodName_, IN MethodFunc Func_)
{
    if (!IsValidMethodName(strMethodName_))
    {
        throw std::invalid_argument("SDO method name must match [A-Z][A-Za-z0-9_]*: " + strMethodName_);
    }
    ValidateMethodFunc(Func_);

    auto _Method = MakeSDOMethod(m_strName, strMethodName_);
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    const auto _Iter = m_mapMethods.find(_Method.nMethodCode);
    if (_Iter != m_mapMethods.end())
    {
        if (_Iter->second.Method.strMethodName != _Method.strMethodName)
        {
            throw std::runtime_error(
                "SDO method code collision: " +
                FormatSDOMethod(_Iter->second.Method) + " vs " + FormatSDOMethod(_Method));
        }
        return false;
    }

    m_mapMethods.emplace(_Method.nMethodCode, MethodRecord{ std::move(_Method), std::move(Func_) });
    return true;
}

void iCAX::Interaction::CSDO::ValidateMethodFunc(IN const MethodFunc& Func_)
{
    if (!Func_)
    {
        throw std::invalid_argument("SDO method function cannot be empty");
    }
}
