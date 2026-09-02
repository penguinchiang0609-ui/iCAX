#include "pch.h"
#include "ISceneContext.h"

iCAX::Project::ISceneContext::ISceneContext() = default;

iCAX::Project::ISceneContext::~ISceneContext() = default;

bool iCAX::Project::ISceneContext::HasViews() const
{
    return false;
}

iCAX::View::CViewSet&
iCAX::Project::ISceneContext::Views() const
{
    throw std::logic_error("Scene View set is not available");
}
