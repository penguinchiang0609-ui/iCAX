#include "pch.h"
#include "CommandTarget.h"

#include <stdexcept>
#include <utility>

iCAX::Command::CCommandTarget::CCommandTarget(IN std::string strMainName_)
    : m_strMainName(std::move(strMainName_))
{
    if (!IsValidCommandName(m_strMainName))
    {
        throw std::invalid_argument(
            "Command target main name must match [A-Z][A-Za-z0-9_]*: " +
            m_strMainName);
    }
    m_nMainCode = CommandHash32(m_strMainName);
    if (m_nMainCode == 0)
    {
        throw std::invalid_argument("Command target main code cannot be zero: " + m_strMainName);
    }
}

const std::string& iCAX::Command::CCommandTarget::GetMainName() const
{
    return m_strMainName;
}

uint32_t iCAX::Command::CCommandTarget::GetMainCode() const
{
    return m_nMainCode;
}

bool iCAX::Command::CCommandTarget::HasSubCommand(IN uint32_t nSubCode_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_mapSubCommands.find(nSubCode_) != m_mapSubCommands.end();
}

std::vector<iCAX::Command::CCommandRoute> iCAX::Command::CCommandTarget::GetSubCommandRoutes() const
{
    std::vector<CCommandRoute> _Routes;

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    _Routes.reserve(m_mapSubCommands.size());
    for (const auto& _Pair : m_mapSubCommands)
    {
        _Routes.push_back(_Pair.second.Route);
    }
    return _Routes;
}

bool iCAX::Command::CCommandTarget::Bind(IN std::string strSubName_, IN SubCommandFunc Func_)
{
    return RegisterSubCommand(std::move(strSubName_), std::move(Func_));
}

iCAX::Command::CCommandResponse iCAX::Command::CCommandTarget::Handle(
    IN const CCommandRequest& Request_,
    IN iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    if (Request_.Route.nMainCode != m_nMainCode)
    {
        CCommandResponse _Response;
        _Response.Route = Request_.Route;
        _Response.nStatus = ECommandStatusCode::NoHandler;
        _Response.strError = "Command main route does not match command target: " + FormatCommandRoute(Request_.Route);
        return _Response;
    }

    SubCommandFunc _Func;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        auto _Iter = m_mapSubCommands.find(Request_.Route.nSubCode);
        if (_Iter == m_mapSubCommands.end())
        {
            CCommandResponse _Response;
            _Response.Route = Request_.Route;
            _Response.nStatus = ECommandStatusCode::NoHandler;
            _Response.strError = "Sub command handler not found: " + FormatCommandRoute(Request_.Route);
            return _Response;
        }
        _Func = _Iter->second.Func;
    }

    return _Func(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
}

bool iCAX::Command::CCommandTarget::RegisterSubCommand(
    IN std::string strSubName_,
    IN SubCommandFunc Func_)
{
    if (!IsValidCommandName(strSubName_))
    {
        throw std::invalid_argument(
            "Sub command name must match [A-Z][A-Za-z0-9_]*: " +
            strSubName_);
    }
    ValidateSubCommandFunc(Func_);

    auto _Route = MakeCommandRoute(m_strMainName, strSubName_);

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iter = m_mapSubCommands.find(_Route.nSubCode);
    if (_Iter != m_mapSubCommands.end())
    {
        if (_Iter->second.Route.strSubName != _Route.strSubName)
        {
            throw std::runtime_error(
                "Sub command code collision: " +
                FormatCommandRoute(_Iter->second.Route) +
                " vs " +
                FormatCommandRoute(_Route));
        }
        return false;
    }

    m_mapSubCommands[_Route.nSubCode] = { std::move(_Route), std::move(Func_) };
    return true;
}

void iCAX::Command::CCommandTarget::ValidateSubCommandFunc(IN const SubCommandFunc& Func_)
{
    if (!Func_)
    {
        throw std::invalid_argument("Sub command function cannot be empty");
    }
}
