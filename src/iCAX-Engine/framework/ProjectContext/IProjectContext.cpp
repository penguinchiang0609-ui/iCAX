#include "pch.h"
#include "IProjectContext.h"

#include <stdexcept>

iCAX::Project::IProjectContext::IProjectContext() = default;

iCAX::Project::IProjectContext::~IProjectContext() = default;

bool iCAX::Project::IProjectContext::HasPDOHub() const
{
    return false;
}

iCAX::PDO::IPDOHub& iCAX::Project::IProjectContext::PDOHub()
{
    throw std::logic_error("Project PDO hub is not configured");
}

const iCAX::PDO::IPDOHub& iCAX::Project::IProjectContext::PDOHub() const
{
    throw std::logic_error("Project PDO hub is not configured");
}
