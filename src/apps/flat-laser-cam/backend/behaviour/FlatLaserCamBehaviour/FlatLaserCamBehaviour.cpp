#include "pch.h"
#include "FlatLaserCamBehaviour.h"

namespace iCAX::FlatLaserCAM
{
    std::string CFlatLaserProjectBehaviour::GetComponentClass() const
    {
        return "CFlatLaserProjectComponent";
    }

    void CFlatLaserProjectBehaviour::OnModified(
        IN iCAX::Database::CComponentBase& component,
        IN const iCAX::Data::PropertySet& newValues,
        IN const iCAX::Application::IApplicationContext& application,
        IN const iCAX::Product::IProductContext& product,
        IN iCAX::Project::IProjectContext& project)
    {
        (void)component;
        (void)newValues;
        (void)application;
        (void)product;
        (void)project;
    }

    std::string CNestingPlanBehaviour::GetComponentClass() const
    {
        return "CNestingPlanComponent";
    }

    void CNestingPlanBehaviour::OnModified(
        IN iCAX::Database::CComponentBase& component,
        IN const iCAX::Data::PropertySet& newValues,
        IN const iCAX::Application::IApplicationContext& application,
        IN const iCAX::Product::IProductContext& product,
        IN iCAX::Project::IProjectContext& project)
    {
        (void)component;
        (void)newValues;
        (void)application;
        (void)product;
        (void)project;
    }

    std::string CToolpathBehaviour::GetComponentClass() const
    {
        return "CToolpathComponent";
    }

    void CToolpathBehaviour::OnModified(
        IN iCAX::Database::CComponentBase& component,
        IN const iCAX::Data::PropertySet& newValues,
        IN const iCAX::Application::IApplicationContext& application,
        IN const iCAX::Product::IProductContext& product,
        IN iCAX::Project::IProjectContext& project)
    {
        (void)component;
        (void)newValues;
        (void)application;
        (void)product;
        (void)project;
    }
}
