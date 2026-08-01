#include "pch.h"
#include "ISceneContext.h"

iCAX::Project::ISceneContext::ISceneContext() = default;

iCAX::Project::ISceneContext::~ISceneContext() = default;

bool iCAX::Project::ISceneContext::HasEntityViews() const
{
    return false;
}

iCAX::View::CEntityViewSet&
iCAX::Project::ISceneContext::EntityViews() const
{
    throw std::logic_error("Scene EntityView set is not available");
}
