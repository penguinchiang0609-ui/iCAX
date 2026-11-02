#pragma once
#include "ComponentBase.h"
#include "Data/uuid.h"

namespace iCAX::Database
{
// A derived publication may create or update only components in this family.
// User definitions and labels deliberately do not inherit this capability.
class _DATABASE_EXP CDerivedComponentBase : public CComponentBase
{
public:
    using CComponentBase::CComponentBase;
    virtual iCAX::Data::uuid GetDerivedOwnerID() const = 0;
};
}
