#include "pch.h"
#include "CommandMessage.h"

bool iCAX::Command::CCommandResponse::IsOK() const
{
    return nStatus == ECommandStatusCode::Ok;
}
