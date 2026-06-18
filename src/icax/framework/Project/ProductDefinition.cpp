#include "pch.h"
#include "ProductDefinition.h"

bool iCAX::Project::CProductDefinition::IsValid() const
{
    return !ProductID.empty();
}

std::string iCAX::Project::CProductDefinition::GetDisplayNameOrID() const
{
    if (!DisplayName.empty())
    {
        return DisplayName;
    }
    return ProductID;
}
