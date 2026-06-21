#include "pch.h"
#include "FlatLaserCamBehaviour.h"

namespace iCAX::FlatLaserCAM
{
    std::string CFlatLaserProjectBehaviour::GetComponentClass() const
    {
        return CFlatLaserProjectComponent::S_ClassName;
    }

    void CFlatLaserProjectBehaviour::OnModified(
        IN iCAX::Database::CComponentBase& component,
        IN const iCAX::Data::PropertySet& newValues,
        IN const iCAX::Behaviour::IUniverseContext& context)
    {
        (void)component;
        (void)newValues;
        (void)context;
    }

    std::string CNestingPlanBehaviour::GetComponentClass() const
    {
        return CNestingPlanComponent::S_ClassName;
    }

    void CNestingPlanBehaviour::OnModified(
        IN iCAX::Database::CComponentBase& component,
        IN const iCAX::Data::PropertySet& newValues,
        IN const iCAX::Behaviour::IUniverseContext& context)
    {
        (void)component;
        (void)newValues;
        (void)context;
    }

    std::string CToolpathBehaviour::GetComponentClass() const
    {
        return CToolpathComponent::S_ClassName;
    }

    void CToolpathBehaviour::OnModified(
        IN iCAX::Database::CComponentBase& component,
        IN const iCAX::Data::PropertySet& newValues,
        IN const iCAX::Behaviour::IUniverseContext& context)
    {
        (void)component;
        (void)newValues;
        (void)context;
    }
}

