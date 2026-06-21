#pragma once

#include "FlatLaserCamComponentExport.h"

#include "Data/Variant.h"
#include "Database/ComponentBase.h"
#include "Database/ComponentHelper.h"
#include "Database/IEntity.h"

#include <string>

#define FLAT_LASER_STRING_FIELD(ownerType, name, defaultValue) \
    DECLARED_ICAX_FIELD(ownerType, std::string, name, std::string(defaultValue), \
        [](const std::string& lhs, const std::string& rhs) { return lhs == rhs; }, \
        [](const std::string& value) -> iCAX::Data::Variant { return iCAX::Data::Variant(value); }, \
        [](const iCAX::Data::PropertyValue& value) -> std::string { return value.To<std::string>(); })

#define FLAT_LASER_INT_FIELD(ownerType, name, defaultValue) \
    DECLARED_ICAX_FIELD(ownerType, int, name, defaultValue, \
        [](const int& lhs, const int& rhs) { return lhs == rhs; }, \
        [](const int& value) -> iCAX::Data::Variant { return iCAX::Data::Variant(value); }, \
        [](const iCAX::Data::PropertyValue& value) -> int { return value.To<int>(); })

#define FLAT_LASER_DOUBLE_FIELD(ownerType, name, defaultValue) \
    DECLARED_ICAX_FIELD(ownerType, double, name, defaultValue, \
        [](const double& lhs, const double& rhs) { return lhs == rhs; }, \
        [](const double& value) -> iCAX::Data::Variant { return iCAX::Data::Variant(value); }, \
        [](const iCAX::Data::PropertyValue& value) -> double { return value.To<double>(); })

#define FLAT_LASER_BOOL_FIELD(ownerType, name, defaultValue) \
    DECLARED_ICAX_FIELD(ownerType, bool, name, defaultValue, \
        [](const bool& lhs, const bool& rhs) { return lhs == rhs; }, \
        [](const bool& value) -> iCAX::Data::Variant { return iCAX::Data::Variant(value); }, \
        [](const iCAX::Data::PropertyValue& value) -> bool { return value.To<bool>(); })

#define FLAT_LASER_OBSERVABLE_DOUBLE_FIELD(ownerType, name, defaultValue) \
    DECLARED_ICAX_OBSERVABLE_FIELD(ownerType, double, name, defaultValue, \
        [](const double& lhs, const double& rhs) { return lhs == rhs; }, \
        [](const double& value) -> iCAX::Data::Variant { return iCAX::Data::Variant(value); }, \
        [](const iCAX::Data::PropertyValue& value) -> double { return value.To<double>(); })

namespace iCAX::FlatLaserCAM
{
    class _FLATLASERCAMCOMPONENT_EXP CFlatLaserProjectComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CFlatLaserProjectComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CFlatLaserProjectComponent)

        FLAT_LASER_STRING_FIELD(CFlatLaserProjectComponent, ProjectName, "Untitled")
        FLAT_LASER_STRING_FIELD(CFlatLaserProjectComponent, Unit, "mm")
        FLAT_LASER_STRING_FIELD(CFlatLaserProjectComponent, Author, "")
        FLAT_LASER_STRING_FIELD(CFlatLaserProjectComponent, MaterialLibraryName, "Default")
    };

    class _FLATLASERCAMCOMPONENT_EXP CMaterialComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CMaterialComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CMaterialComponent)

        FLAT_LASER_STRING_FIELD(CMaterialComponent, MaterialName, "Steel")
        FLAT_LASER_STRING_FIELD(CMaterialComponent, Grade, "Q235")
        FLAT_LASER_DOUBLE_FIELD(CMaterialComponent, Density, 7.85)
        FLAT_LASER_STRING_FIELD(CMaterialComponent, DefaultGas, "O2")
    };

    class _FLATLASERCAMCOMPONENT_EXP CSheetComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CSheetComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CSheetComponent)

        FLAT_LASER_STRING_FIELD(CSheetComponent, SheetName, "Sheet")
        FLAT_LASER_STRING_FIELD(CSheetComponent, MaterialName, "Steel")
        FLAT_LASER_DOUBLE_FIELD(CSheetComponent, Thickness, 1.0)
        FLAT_LASER_DOUBLE_FIELD(CSheetComponent, Width, 3000.0)
        FLAT_LASER_DOUBLE_FIELD(CSheetComponent, Height, 1500.0)
        FLAT_LASER_INT_FIELD(CSheetComponent, Quantity, 1)
        FLAT_LASER_BOOL_FIELD(CSheetComponent, IsRemnant, false)
    };

    class _FLATLASERCAMCOMPONENT_EXP CDrawingComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CDrawingComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CDrawingComponent)

        FLAT_LASER_STRING_FIELD(CDrawingComponent, SourcePath, "")
        FLAT_LASER_STRING_FIELD(CDrawingComponent, LayerName, "")
        FLAT_LASER_DOUBLE_FIELD(CDrawingComponent, Scale, 1.0)
        FLAT_LASER_BOOL_FIELD(CDrawingComponent, IsClosed, false)
    };

    class _FLATLASERCAMCOMPONENT_EXP CPartDefinitionComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CPartDefinitionComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CPartDefinitionComponent)

        FLAT_LASER_STRING_FIELD(CPartDefinitionComponent, PartName, "Part")
        FLAT_LASER_STRING_FIELD(CPartDefinitionComponent, DrawingEntityId, "")
        FLAT_LASER_INT_FIELD(CPartDefinitionComponent, Quantity, 1)
        FLAT_LASER_INT_FIELD(CPartDefinitionComponent, Priority, 0)
    };

    class _FLATLASERCAMCOMPONENT_EXP CPartInstanceComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CPartInstanceComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CPartInstanceComponent)

        FLAT_LASER_STRING_FIELD(CPartInstanceComponent, PartDefinitionId, "")
        FLAT_LASER_STRING_FIELD(CPartInstanceComponent, SheetId, "")
        FLAT_LASER_DOUBLE_FIELD(CPartInstanceComponent, X, 0.0)
        FLAT_LASER_DOUBLE_FIELD(CPartInstanceComponent, Y, 0.0)
        FLAT_LASER_DOUBLE_FIELD(CPartInstanceComponent, Rotation, 0.0)
        FLAT_LASER_BOOL_FIELD(CPartInstanceComponent, Mirrored, false)
    };

    class _FLATLASERCAMCOMPONENT_EXP CCutProcessComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CCutProcessComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CCutProcessComponent)

        FLAT_LASER_STRING_FIELD(CCutProcessComponent, ProcessName, "Default")
        FLAT_LASER_DOUBLE_FIELD(CCutProcessComponent, LaserPower, 1000.0)
        FLAT_LASER_DOUBLE_FIELD(CCutProcessComponent, CutSpeed, 1000.0)
        FLAT_LASER_DOUBLE_FIELD(CCutProcessComponent, PierceTime, 0.2)
        FLAT_LASER_DOUBLE_FIELD(CCutProcessComponent, GasPressure, 0.6)
        FLAT_LASER_DOUBLE_FIELD(CCutProcessComponent, LeadInLength, 1.0)
    };

    class _FLATLASERCAMCOMPONENT_EXP CNestingPlanComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CNestingPlanComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CNestingPlanComponent)

        FLAT_LASER_STRING_FIELD(CNestingPlanComponent, PlanName, "Nesting")
        FLAT_LASER_STRING_FIELD(CNestingPlanComponent, SheetId, "")
        FLAT_LASER_DOUBLE_FIELD(CNestingPlanComponent, Utilization, 0.0)
        FLAT_LASER_INT_FIELD(CNestingPlanComponent, PartCount, 0)
        FLAT_LASER_BOOL_FIELD(CNestingPlanComponent, IsLocked, false)
    };

    class _FLATLASERCAMCOMPONENT_EXP CToolpathComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CToolpathComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CToolpathComponent)

        FLAT_LASER_STRING_FIELD(CToolpathComponent, NestingPlanId, "")
        FLAT_LASER_STRING_FIELD(CToolpathComponent, ProcessId, "")
        FLAT_LASER_INT_FIELD(CToolpathComponent, PathCount, 0)
        FLAT_LASER_DOUBLE_FIELD(CToolpathComponent, TotalLength, 0.0)
        FLAT_LASER_STRING_FIELD(CToolpathComponent, GenerationState, "Empty")
    };

    class _FLATLASERCAMCOMPONENT_EXP CSimulationComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CSimulationComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CSimulationComponent)

        FLAT_LASER_STRING_FIELD(CSimulationComponent, ToolpathId, "")
        FLAT_LASER_STRING_FIELD(CSimulationComponent, ResultState, "NotRun")
        FLAT_LASER_INT_FIELD(CSimulationComponent, CollisionCount, 0)
        FLAT_LASER_OBSERVABLE_DOUBLE_FIELD(CSimulationComponent, Progress, 0.0)
    };

    class _FLATLASERCAMCOMPONENT_EXP CNCProgramComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CNCProgramComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CNCProgramComponent)

        FLAT_LASER_STRING_FIELD(CNCProgramComponent, ToolpathId, "")
        FLAT_LASER_STRING_FIELD(CNCProgramComponent, OutputPath, "")
        FLAT_LASER_STRING_FIELD(CNCProgramComponent, ControllerType, "Generic")
        FLAT_LASER_INT_FIELD(CNCProgramComponent, ProgramNumber, 1)
        FLAT_LASER_BOOL_FIELD(CNCProgramComponent, IsPosted, false)
    };
}

#undef FLAT_LASER_OBSERVABLE_DOUBLE_FIELD
#undef FLAT_LASER_BOOL_FIELD
#undef FLAT_LASER_DOUBLE_FIELD
#undef FLAT_LASER_INT_FIELD
#undef FLAT_LASER_STRING_FIELD

