#include "pch.h"
#include "FacadeCall.h"

bool iCAX::Interaction::CFacadeResult::IsOK() const noexcept
{
    return nStatus == EFacadeCallStatus::Ok;
}
