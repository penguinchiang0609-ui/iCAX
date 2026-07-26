#include "pch.h"

#include "EntityUpdate.h"

#include <set>

namespace
{
    void ValidateComponentUpdate(
        IN const iCAX::Database::SComponentUpdate& Component_)
    {
        using namespace iCAX::Database;

        if (Component_.ComponentClass.empty())
        {
            throw std::invalid_argument(
                "Entity update component class cannot be empty");
        }
        for (const auto& [PropertyName_, Operand_] : Component_.Properties)
        {
            if (PropertyName_.empty())
            {
                throw std::invalid_argument(
                    "Entity update property name cannot be empty");
            }
            if (Operand_.Type == EEntityValueOperandType::Parameter
                && Operand_.ParameterName.empty())
            {
                throw std::invalid_argument(
                    "Entity update parameter name cannot be empty");
            }
        }

        switch (Component_.Type)
        {
        case EComponentUpdateType::Modify:
            if (Component_.Properties.empty() && !Component_.Enabled.has_value())
            {
                throw std::invalid_argument(
                    "Modify component update must change properties or enabled state");
            }
            break;
        case EComponentUpdateType::Add:
            if (!Component_.Enabled.has_value())
            {
                throw std::invalid_argument(
                    "Add component update requires an initial enabled state");
            }
            break;
        case EComponentUpdateType::Remove:
            if (!Component_.Properties.empty() || Component_.Enabled.has_value())
            {
                throw std::invalid_argument(
                    "Remove component update cannot define properties or enabled state");
            }
            break;
        default:
            throw std::invalid_argument("Unknown component update type");
        }
    }
}

iCAX::Database::SComponentUpdate
iCAX::Database::CEntityUpdateBuilder::ModifyComponent(
    IN std::string ComponentClass_,
    IN std::map<std::string, SEntityValueOperand> Properties_,
    IN std::optional<bool> Enabled_)
{
    SComponentUpdate _Result;
    _Result.Type = EComponentUpdateType::Modify;
    _Result.ComponentClass = std::move(ComponentClass_);
    _Result.Properties = std::move(Properties_);
    _Result.Enabled = Enabled_;
    return _Result;
}

iCAX::Database::SComponentUpdate
iCAX::Database::CEntityUpdateBuilder::AddComponent(
    IN std::string ComponentClass_,
    IN std::map<std::string, SEntityValueOperand> InitialProperties_,
    IN const bool bEnabled_)
{
    SComponentUpdate _Result;
    _Result.Type = EComponentUpdateType::Add;
    _Result.ComponentClass = std::move(ComponentClass_);
    _Result.Properties = std::move(InitialProperties_);
    _Result.Enabled = bEnabled_;
    return _Result;
}

iCAX::Database::SComponentUpdate
iCAX::Database::CEntityUpdateBuilder::RemoveComponent(
    IN std::string ComponentClass_)
{
    SComponentUpdate _Result;
    _Result.Type = EComponentUpdateType::Remove;
    _Result.ComponentClass = std::move(ComponentClass_);
    return _Result;
}

iCAX::Database::SEntityUpdate
iCAX::Database::CEntityUpdateBuilder::Build(
    IN std::vector<SComponentUpdate> Components_)
{
    if (Components_.empty())
    {
        throw std::invalid_argument(
            "Entity update must contain at least one component update");
    }

    std::set<std::string> _ComponentClasses;
    for (const auto& _Component : Components_)
    {
        ValidateComponentUpdate(_Component);
        if (!_ComponentClasses.insert(_Component.ComponentClass).second)
        {
            throw std::invalid_argument(
                "Entity update contains duplicate component operation: "
                + _Component.ComponentClass);
        }
    }
    return { std::move(Components_) };
}

iCAX::Database::SEntityUpdate
iCAX::Database::CEntityUpdateBuilder::Build(
    IN std::initializer_list<SComponentUpdate> Components_)
{
    return Build(std::vector<SComponentUpdate>(Components_));
}
