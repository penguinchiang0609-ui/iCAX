#pragma once
#include "Database/ComponentHelper.h"
#include <memory>

namespace iCAX::FiveAxisCAM
{
    template<class T> iCAX::Data::PropertyValue ToValue(const T& value) { return value; }
    template<class T> T FromValue(const iCAX::Data::PropertyValue& value) { return value.To<T>(); }
    template<class T> bool Equal(const T& a, const T& b) { return a == b; }
    using Text = std::string;
    using ID = iCAX::Data::uuid;
    using UInt = unsigned long long;
    using Map = iCAX::Data::ObjectMap;
    using Array = iCAX::Data::VariantArray;
    inline Map IdentityCoordinateFrame(){return {{"origin",Array{0.,0.,0.}},{"x",Array{1.,0.,0.}},{"y",Array{0.,1.,0.}},{"z",Array{0.,0.,1.}}};}
    inline Array IdentityRigidFrame(){return {1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.};}

#define FIVE_AXIS_FIELD(C, T, N, D) DECLARED_ICAX_FIELD(C, T, N, D, Equal<T>, ToValue<T>, FromValue<T>)
    struct SceneRuntime;
    class CFiveAxisSceneComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CFiveAxisSceneComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CFiveAxisSceneComponent)
    public:
        // Deliberately not a reflected property: no Task/receipt/epoch persists.
        std::shared_ptr<SceneRuntime> Runtime;
    };
    class CFiveAxisJobComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CFiveAxisJobComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CFiveAxisJobComponent)
        FIVE_AXIS_FIELD(CFiveAxisJobComponent, Text, Name, Text("激光切割作业"))
        // Import/setup history owns the fallback placement. Navigation persists
        // without creating or invalidating machining undo/redo history.
        FIVE_AXIS_FIELD(CFiveAxisJobComponent, ID, DefaultSetupID, ID())
        FIVE_AXIS_FIELD(CFiveAxisJobComponent, ID, DefaultPlacementID, ID())
        FIVE_AXIS_FIELD(CFiveAxisJobComponent, ID, IntentDocumentID, ID())
        // Project-owned head instances. Setups reference these immutable model
        // snapshots plus editable calibration; they do not duplicate a head.
        FIVE_AXIS_FIELD(CFiveAxisJobComponent, Map, HeadInstances, Map())
        FIVE_AXIS_FIELD(CFiveAxisJobComponent, Map, HeadAssignment, Map())
        // Frozen at project creation; never follows later user-library edits.
        FIVE_AXIS_FIELD(CFiveAxisJobComponent, Map, ProductHeadDefault, Map())
        FIVE_AXIS_FIELD(CFiveAxisJobComponent, Map, MachineTypeAssignment, Map())
        FIVE_AXIS_FIELD(CFiveAxisJobComponent, Map, ProductMachineTypeDefault, Map())
    private:
        ID m_ActiveSetupID;
        ID m_ActivePlacementID;
        struct RegisterActiveSetup {
            RegisterActiveSetup(){iCAX::Database::CMetaRegistrationCatalog::Register([](iCAX::Database::IMetaRegistry& registry){
                registry.RegistPropertyByName(CFiveAxisJobComponent::S_ClassName,"ActiveSetupID",
                    [](const void* value)->iCAX::Data::PropertyValue{return static_cast<const CFiveAxisJobComponent*>(value)->m_ActiveSetupID;},
                    [](void* value,const iCAX::Data::PropertyValue& property){static_cast<CFiveAxisJobComponent*>(value)->m_ActiveSetupID=property.To<ID>();},
                    iCAX::Database::EPropertyPersistence::Persistent,iCAX::Database::EPropertyChangePolicy::Observable);
                registry.RegistPropertyByName(CFiveAxisJobComponent::S_ClassName,"ActivePlacementID",
                    [](const void* value)->iCAX::Data::PropertyValue{return static_cast<const CFiveAxisJobComponent*>(value)->m_ActivePlacementID;},
                    [](void* value,const iCAX::Data::PropertyValue& property){static_cast<CFiveAxisJobComponent*>(value)->m_ActivePlacementID=property.To<ID>();},
                    iCAX::Database::EPropertyPersistence::Persistent,iCAX::Database::EPropertyChangePolicy::Observable);
            },this);}
        };
        inline static RegisterActiveSetup s_ActiveSetupRegistration{};
    public:
        const ID& GetActiveSetupID() const{return m_ActiveSetupID;}
        const ID& GetActivePlacementID() const{return m_ActivePlacementID;}
        FIVE_AXIS_FIELD(CFiveAxisJobComponent, UInt, InputRevision, 1ull)
    };
    class CFiveAxisSetupComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CFiveAxisSetupComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CFiveAxisSetupComponent)
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, ID, JobID, ID())
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, ID, IntentPlanID, ID())
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Text, Name, Text("装夹 1"))
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, UInt, Sequence, 1ull)
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, bool, Enabled, true)
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, bool, Locked, false)
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Map, RegistrationPlan, Map())
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, UInt, InputRevision, 1ull)
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Text, PlanValidity, Text("Missing"))
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Map, LastSuccessfulPlanRef, Map())
        // Machining order is editable input; exact generated geometry is a cache.
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Map, RapidSettings, Map())
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Map, RapidOverrides, Map())
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, ID, HeadInstanceID, ID())
        // HeadInstanceID is the resolved cache; this is the explicit override.
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Map, HeadAssignment, Map())
        // Abstract machining structure is independent of a physical machine.
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Map, MachineTypeAssignment, Map())
        // Shared assembly -> operation reference; part/fixture transforms stay local.
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Array, AssemblyTransform, IdentityRigidFrame())
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Map, MachineSetup, Map())
        // Rebuildable output is saved with the project but is not a user input
        // edit. Observable publication preserves existing Undo/Redo history.
    private:
        Map m_GeneratedToolpaths;
        Map m_GeneratedRapidPaths;
        Map m_SimulationCheckData;
        Map m_MachineMotionPlanData;
        struct RegisterGeneratedGeometry
        {
            RegisterGeneratedGeometry(){iCAX::Database::CMetaRegistrationCatalog::Register([](iCAX::Database::IMetaRegistry& registry){
                registry.RegistPropertyByName(CFiveAxisSetupComponent::S_ClassName,"GeneratedToolpaths",
                    [](const void* value)->iCAX::Data::PropertyValue{return static_cast<const CFiveAxisSetupComponent*>(value)->m_GeneratedToolpaths;},
                    [](void* value,const iCAX::Data::PropertyValue& property){static_cast<CFiveAxisSetupComponent*>(value)->m_GeneratedToolpaths=property.To<Map>();},
                    iCAX::Database::EPropertyPersistence::Persistent,iCAX::Database::EPropertyChangePolicy::Observable);
                registry.RegistPropertyByName(CFiveAxisSetupComponent::S_ClassName,"GeneratedRapidPaths",
                    [](const void* value)->iCAX::Data::PropertyValue{return static_cast<const CFiveAxisSetupComponent*>(value)->m_GeneratedRapidPaths;},
                    [](void* value,const iCAX::Data::PropertyValue& property){static_cast<CFiveAxisSetupComponent*>(value)->m_GeneratedRapidPaths=property.To<Map>();},
                    iCAX::Database::EPropertyPersistence::Persistent,iCAX::Database::EPropertyChangePolicy::Observable);
                registry.RegistPropertyByName(CFiveAxisSetupComponent::S_ClassName,"SimulationCheckData",
                    [](const void* value)->iCAX::Data::PropertyValue{return static_cast<const CFiveAxisSetupComponent*>(value)->m_SimulationCheckData;},
                    [](void* value,const iCAX::Data::PropertyValue& property){static_cast<CFiveAxisSetupComponent*>(value)->m_SimulationCheckData=property.To<Map>();},
                    iCAX::Database::EPropertyPersistence::Persistent,iCAX::Database::EPropertyChangePolicy::Observable);
                registry.RegistPropertyByName(CFiveAxisSetupComponent::S_ClassName,"MachineMotionPlanData",
                    [](const void* value)->iCAX::Data::PropertyValue{return static_cast<const CFiveAxisSetupComponent*>(value)->m_MachineMotionPlanData;},
                    [](void* value,const iCAX::Data::PropertyValue& property){static_cast<CFiveAxisSetupComponent*>(value)->m_MachineMotionPlanData=property.To<Map>();},
                    iCAX::Database::EPropertyPersistence::Persistent,iCAX::Database::EPropertyChangePolicy::Observable);
            },this);}
        };
        inline static RegisterGeneratedGeometry s_GeneratedGeometryRegistration{};
    public:
        const Map& GetGeneratedToolpaths() const{return m_GeneratedToolpaths;}
        const Map& GetGeneratedRapidPaths() const{return m_GeneratedRapidPaths;}
        const Map& GetSimulationCheckData() const{return m_SimulationCheckData;}
        const Map& GetMachineMotionPlanData() const{return m_MachineMotionPlanData;}
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Map, PlanningProfileData, Map())
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Map, FixtureCheck, Map())
        FIVE_AXIS_FIELD(CFiveAxisSetupComponent, Map, FixtureDesign, Map())
    private:
        Map m_FixtureBeamCheck;
        struct RegisterFixtureBeamCheck {
            RegisterFixtureBeamCheck(){iCAX::Database::CMetaRegistrationCatalog::Register([](iCAX::Database::IMetaRegistry& registry){
                registry.RegistPropertyByName(CFiveAxisSetupComponent::S_ClassName,"FixtureBeamCheck",
                    [](const void* value)->iCAX::Data::PropertyValue{return static_cast<const CFiveAxisSetupComponent*>(value)->m_FixtureBeamCheck;},
                    [](void* value,const iCAX::Data::PropertyValue& property){static_cast<CFiveAxisSetupComponent*>(value)->m_FixtureBeamCheck=property.To<Map>();},
                    iCAX::Database::EPropertyPersistence::Persistent,iCAX::Database::EPropertyChangePolicy::Observable);
            },this);}
        };
        inline static RegisterFixtureBeamCheck s_FixtureBeamCheckRegistration{};
    public:
        const Map& GetFixtureBeamCheck() const{return m_FixtureBeamCheck;}
    };
    class CFiveAxisWorkpieceComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CFiveAxisWorkpieceComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CFiveAxisWorkpieceComponent)
        FIVE_AXIS_FIELD(CFiveAxisWorkpieceComponent, Text, Name, Text())
        FIVE_AXIS_FIELD(CFiveAxisWorkpieceComponent, UInt, ListOrder, 0ull)
        FIVE_AXIS_FIELD(CFiveAxisWorkpieceComponent, Map, HeadAssignment, Map())
        FIVE_AXIS_FIELD(CFiveAxisWorkpieceComponent, Map, MachineTypeAssignment, Map())
        FIVE_AXIS_FIELD(CFiveAxisWorkpieceComponent, bool, Locked, false)
        
        FIVE_AXIS_FIELD(CFiveAxisWorkpieceComponent, bool, Enabled, true)
        FIVE_AXIS_FIELD(CFiveAxisWorkpieceComponent, Map, SourceRef, Map())
        FIVE_AXIS_FIELD(CFiveAxisWorkpieceComponent, Map, GeometryRef, Map())
        FIVE_AXIS_FIELD(CFiveAxisWorkpieceComponent, Map, PreviewRef, Map())
        // Authored coordinate parameters belong to the shared workpiece,
        // independently of transient alignment editors and setup registration.
        FIVE_AXIS_FIELD(CFiveAxisWorkpieceComponent, Map, CoordinateFrame, IdentityCoordinateFrame())
        FIVE_AXIS_FIELD(CFiveAxisWorkpieceComponent, Map, ImportMetadata, Map())
        FIVE_AXIS_FIELD(CFiveAxisWorkpieceComponent, UInt, InputRevision, 1ull)
    };
    class CFiveAxisPlacementComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CFiveAxisPlacementComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CFiveAxisPlacementComponent)
        FIVE_AXIS_FIELD(CFiveAxisPlacementComponent, ID, SetupID, ID())
        FIVE_AXIS_FIELD(CFiveAxisPlacementComponent, ID, WorkpieceID, ID())
        // Nil SetupID is the independent workpiece display before a user adds
        // its first setup. Transform then maps to Scene instead of Setup.
        // Row-major, right handed and rigid, lengths in mm.
        FIVE_AXIS_FIELD(CFiveAxisPlacementComponent, Array, Transform, Array())
        FIVE_AXIS_FIELD(CFiveAxisPlacementComponent, Map, RegistrationData, Map())
        FIVE_AXIS_FIELD(CFiveAxisPlacementComponent, Text, RegistrationMethod, Text())
        FIVE_AXIS_FIELD(CFiveAxisPlacementComponent, Text, ApplicationMode, Text("Pending"))
        FIVE_AXIS_FIELD(CFiveAxisPlacementComponent, UInt, InputRevision, 1ull)
    };
    class CFiveAxisFixtureUnitComponent final : public iCAX::Database::CComponentBase {
        DECLARE_ICAX_COMPONENT(CFiveAxisFixtureUnitComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CFiveAxisFixtureUnitComponent)
        FIVE_AXIS_FIELD(CFiveAxisFixtureUnitComponent, ID, SetupID, ID())
        FIVE_AXIS_FIELD(CFiveAxisFixtureUnitComponent, Text, Name, Text("夹具单元"))
        FIVE_AXIS_FIELD(CFiveAxisFixtureUnitComponent, Text, Kind, Text("Imported"))
        FIVE_AXIS_FIELD(CFiveAxisFixtureUnitComponent, Array, Transform, Array())
        FIVE_AXIS_FIELD(CFiveAxisFixtureUnitComponent, bool, Locked, false)
        FIVE_AXIS_FIELD(CFiveAxisFixtureUnitComponent, bool, Enabled, true)
        FIVE_AXIS_FIELD(CFiveAxisFixtureUnitComponent, Map, Design, Map())
        FIVE_AXIS_FIELD(CFiveAxisFixtureUnitComponent, UInt, InputRevision, 1ull)
    };
    class CFiveAxisFixtureComponent final : public iCAX::Database::CComponentBase
    {
        DECLARE_ICAX_COMPONENT(CFiveAxisFixtureComponent, CComponentBase)
        DECLARE_ICAX_COMPONENT_CREATOR(CFiveAxisFixtureComponent)
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, ID, SetupID, ID())
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, ID, UnitID, ID())
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, Text, Name, Text())
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, Map, SourceRef, Map())
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, Map, GeometryRef, Map())
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, Map, PreviewRef, Map())
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, Map, ImportMetadata, Map())
        // Row-major T_Setup_Fixture, independent of the workpiece registration.
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, Array, Transform, Array())
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, bool, Visible, true)
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, bool, Enabled, true)
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, bool, Locked, false)
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, bool, CollisionEnabled, true)
        FIVE_AXIS_FIELD(CFiveAxisFixtureComponent, UInt, InputRevision, 1ull)
    };
#undef FIVE_AXIS_FIELD
}
