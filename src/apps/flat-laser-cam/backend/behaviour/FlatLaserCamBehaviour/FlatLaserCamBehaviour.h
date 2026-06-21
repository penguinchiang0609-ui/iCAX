#pragma once

#include "FlatLaserCamBehaviourExport.h"

#include "Behaviour/BehaviourBase.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "FlatLaserCamComponent/FlatLaserCamComponents.h"

namespace iCAX::FlatLaserCAM
{
    class _FLATLASERCAMBEHAVIOUR_EXP CFlatLaserProjectBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        std::string GetComponentClass() const override;

    protected:
        void OnModified(
            IN iCAX::Database::CComponentBase& component,
            IN const iCAX::Data::PropertySet& newValues,
            IN const iCAX::Behaviour::IUniverseContext& context) override;

        AUTO_REGIST_BEHAVIOUR(CFlatLaserProjectBehaviour)
    };

    class _FLATLASERCAMBEHAVIOUR_EXP CNestingPlanBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        std::string GetComponentClass() const override;

    protected:
        void OnModified(
            IN iCAX::Database::CComponentBase& component,
            IN const iCAX::Data::PropertySet& newValues,
            IN const iCAX::Behaviour::IUniverseContext& context) override;

        AUTO_REGIST_BEHAVIOUR(CNestingPlanBehaviour)
    };

    class _FLATLASERCAMBEHAVIOUR_EXP CToolpathBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
    public:
        std::string GetComponentClass() const override;

    protected:
        void OnModified(
            IN iCAX::Database::CComponentBase& component,
            IN const iCAX::Data::PropertySet& newValues,
            IN const iCAX::Behaviour::IUniverseContext& context) override;

        AUTO_REGIST_BEHAVIOUR(CToolpathBehaviour)
    };
}

