#include "pch.h"
#include "FunctionCommandHandler.h"

#include <stdexcept>
#include <utility>

iCAX::Command::CFunctionCommandHandler::CFunctionCommandHandler(IN HandlerFunc Func_)
    : m_Func(std::move(Func_))
{
    if (!m_Func)
    {
        throw std::invalid_argument("Command handler function cannot be empty");
    }
}

iCAX::Command::CCommandResponse iCAX::Command::CFunctionCommandHandler::Handle(IN const CCommandRequest& Request_, IN ICommandContext& Context_)
{
    return m_Func(Request_, Context_);
}
