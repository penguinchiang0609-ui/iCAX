#include "FiveAxisCAM.h"
#include "Data/VariantSerializer.h"
#include "ProductContext/IProductContext.h"
#include "SDO/SDO.h"
#include "SDO/SDORegistrationCatalog.h"
#include "ApplicationContext/IApplicationContext.h"
#include <filesystem>

namespace
{
    using Map = iCAX::Data::ObjectMap;
    using Handler = Map(*)(iCAX::Project::ISceneContext&, const Map&);
    template<Handler Function> iCAX::Interaction::CInvocationResult Dispatch(
        const iCAX::Interaction::CInvocation& request, const iCAX::Application::IApplicationContext& application,
        iCAX::Product::IProductContext* product, iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext* scene)
    {
        if (!scene || !product || product->GetProductID() != "icax.five-axis-cam")
            throw std::invalid_argument("CAM_SCENE_CHANNEL_REQUIRED: use the five-axis product Scene channel");
        Map payload;
        if (!request.Payload.empty()) payload = iCAX::Data::VariantSerializer::Deserialize(
            std::string(request.Payload.begin(), request.Payload.end())).To<Map>();
        iCAX::FiveAxisCAM::BindFixtureLibraryStore(*scene,product->GetUserDataStore());
        const auto& capabilities=product->GetDefinition().Capabilities;
        if(const auto c=capabilities.find("machineDefinition");c!=capabilities.end()){
            const auto capability=c->second.To<Map>();
            if(const auto defaults=capability.find("defaults");defaults!=capability.end()){
                iCAX::Data::VariantArray heads;
                for(const auto& value:defaults->second.To<iCAX::Data::VariantArray>()){
                    auto item=value.To<Map>();const auto raw=item.at("sourcePath").To<std::string>();
                    const auto path=std::filesystem::path(std::u8string(raw.begin(),raw.end()));
                    if(path.empty()||path.is_absolute())throw std::invalid_argument("内置机床路径必须相对于安装目录。");
                    for(const auto& part:path)if(part=="..")throw std::invalid_argument("内置机床路径不能越出安装目录。");
                    const auto install=application.GetPaths().InstallDirectory;
                    const auto full=(std::filesystem::path(std::u8string(install.begin(),install.end()))/path).u8string();
                    item["sourcePath"]=std::string(full.begin(),full.end());heads.emplace_back(std::move(item));
                }
                if(const auto cut=capabilities.find("cuttingHead");cut!=capabilities.end()){
                    const auto declaration=cut->second.To<Map>();
                    if(const auto list=declaration.find("defaults");list!=declaration.end())for(const auto& value:list->second.To<iCAX::Data::VariantArray>()){
                        auto item=value.To<Map>();const auto raw=item.at("sourcePath").To<std::string>();const auto path=std::filesystem::path(std::u8string(raw.begin(),raw.end()));
                        if(path.empty()||path.is_absolute())throw std::invalid_argument("内置切割头路径必须相对于安装目录。");
                        for(const auto& part:path)if(part=="..")throw std::invalid_argument("内置切割头路径不能越出安装目录。");
                        const auto install=application.GetPaths().InstallDirectory;const auto full=(std::filesystem::path(std::u8string(install.begin(),install.end()))/path).u8string();
                        item["sourcePath"]=std::string(full.begin(),full.end());item["standaloneHead"]=true;heads.emplace_back(std::move(item));
                    }
                }
                iCAX::FiveAxisCAM::BindBuiltinHeadSources(*scene,heads);
            }
        }
        const auto resultPayload = [&] {
            if constexpr (Function == &iCAX::FiveAxisCAM::MountMachine) {
                // Recovery sources come from this installed product's declarations,
                // never a display name or an arbitrary frontend file path.
                Map sources;const auto& capabilities=product->GetDefinition().Capabilities;
                if(const auto c=capabilities.find("machineDefinition");c!=capabilities.end()){
                    const auto capability=c->second.To<Map>();
                    if(const auto d=capability.find("defaults");d!=capability.end())for(const auto& value:d->second.To<iCAX::Data::VariantArray>()){
                        const auto item=value.To<Map>();if(!item.contains("id")||!item.contains("sourcePath"))continue;
                        const auto utf8Path=[](const std::string& text){return std::filesystem::path(std::u8string(text.begin(),text.end()));};
                        iCAX::Data::VariantArray paths{item.at("sourcePath")},resolvedPaths;
                        if(item.contains("endpointCompatibilitySources"))for(const auto& path:item.at("endpointCompatibilitySources").To<iCAX::Data::VariantArray>())paths.push_back(path);
                        for(const auto& value:paths){
                            const auto path=utf8Path(value.To<std::string>());
                            if(path.empty()||path.is_absolute())continue;bool safe=true;for(const auto& part:path)if(part=="..")safe=false;if(!safe)continue;
                            const auto resolved=(utf8Path(application.GetPaths().InstallDirectory)/path).u8string();
                            resolvedPaths.emplace_back(std::string(resolved.begin(),resolved.end()));
                        }
                        sources[item.at("id").To<std::string>()]=resolvedPaths;
                    }
                }
                return iCAX::FiveAxisCAM::MountMachineWithTemplates(*scene,payload,sources);
            } else return Function(*scene,payload);
        }();
        const auto json = iCAX::Data::VariantSerializer::Serialize(resultPayload);
        iCAX::Interaction::CInvocationResult result;
        result.nCallID = request.nCallID; result.Method = request.Method;
        result.Payload.assign(json.begin(), json.end());
        return result;
    }
    Map Read(iCAX::Project::ISceneContext& scene, const Map&) { return iCAX::FiveAxisCAM::ReadStateProjection(scene); }
    class CFiveAxisSceneSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CFiveAxisSceneSDO() : CSDO("CamScene")
        {
            ExposeMethod("ReadState", &Dispatch<Read>);
            ExposeMethod("ResolvePickReference", &Dispatch<iCAX::FiveAxisCAM::ResolvePickReference>);
            ExposeMethod("ReadSnapFeatures", &Dispatch<iCAX::FiveAxisCAM::ReadSnapFeatures>);
            ExposeMethod("QueryCommand", &Dispatch<iCAX::FiveAxisCAM::QueryCommand>);
        }
    };
    class CFiveAxisWorkpieceSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CFiveAxisWorkpieceSDO() : CSDO("CamWorkpiece")
        {
            ExposeMethod("ListFiles", &Dispatch<iCAX::FiveAxisCAM::ListWorkpieceFiles>);
            ExposeMethod("PreviewFile", &Dispatch<iCAX::FiveAxisCAM::PreviewWorkpieceFile>);
            ExposeMethod("QueryFilePreview", &Dispatch<iCAX::FiveAxisCAM::QueryWorkpieceFilePreview>);
            ExposeMethod("ReleaseFilePreview", &Dispatch<iCAX::FiveAxisCAM::ReleaseWorkpieceFilePreview>);
            ExposeMethod("Import", &Dispatch<iCAX::FiveAxisCAM::ImportWorkpiece>);
            ExposeMethod("Scale", &Dispatch<iCAX::FiveAxisCAM::ScaleWorkpiece>);
            ExposeMethod("Activate", &Dispatch<iCAX::FiveAxisCAM::ActivateWorkpiece>);
            ExposeMethod("Remove", &Dispatch<iCAX::FiveAxisCAM::RemoveWorkpiece>);
            ExposeMethod("Rename", &Dispatch<iCAX::FiveAxisCAM::RenameWorkpiece>);
            ExposeMethod("Copy", &Dispatch<iCAX::FiveAxisCAM::CopyWorkpiece>);
            ExposeMethod("SetLocked", &Dispatch<iCAX::FiveAxisCAM::SetWorkpieceLocked>);

            ExposeMethod("SetEnabled", &Dispatch<iCAX::FiveAxisCAM::SetWorkpieceEnabled>);
            ExposeMethod("QueryTask", &Dispatch<iCAX::FiveAxisCAM::QueryTask>);
            ExposeMethod("CancelTask", &Dispatch<iCAX::FiveAxisCAM::CancelTask>);
            ExposeMethod("Inspect", &Dispatch<iCAX::FiveAxisCAM::InspectWorkpiece>);
        }
    };
    class CFiveAxisSimulationSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CFiveAxisSimulationSDO() : CSDO("CamSimulation") {
            ExposeMethod("ConfigureMachine", &Dispatch<iCAX::FiveAxisCAM::ConfigureMachineSimulation>);
            ExposeMethod("MountMachine", &Dispatch<iCAX::FiveAxisCAM::MountMachine>);
            ExposeMethod("CheckMachinePlacement", &Dispatch<iCAX::FiveAxisCAM::CheckMachinePlacement>);
            ExposeMethod("InspectMachineCollision", &Dispatch<iCAX::FiveAxisCAM::InspectMachineCollision>);
            ExposeMethod("SetMachinePlacement", &Dispatch<iCAX::FiveAxisCAM::SetMachinePlacement>);
            ExposeMethod("ReadMachine", &Dispatch<iCAX::FiveAxisCAM::ReadMachineSimulation>);
            ExposeMethod("ConfigureMachinePost", &Dispatch<iCAX::FiveAxisCAM::ConfigureMachinePost>);
            ExposeMethod("ExportMachineProgram", &Dispatch<iCAX::FiveAxisCAM::ExportMachineProgramProjection>);
            ExposeMethod("BuildMachine", &Dispatch<iCAX::FiveAxisCAM::BuildMachineSimulationProjection>);
            ExposeMethod("PlanMachine", &Dispatch<iCAX::FiveAxisCAM::PlanMachineMotion>);
            ExposeMethod("ReadMachinePlan", &Dispatch<iCAX::FiveAxisCAM::ReadMachineMotionPlanProjection>);
            ExposeMethod("ReadMachineAttempt", &Dispatch<iCAX::FiveAxisCAM::ReadMachineMotionAttempt>);
            ExposeMethod("CheckMachineMove", &Dispatch<iCAX::FiveAxisCAM::CheckMachineMove>);
            ExposeMethod("EndMachinePoseEdit", &Dispatch<iCAX::FiveAxisCAM::EndMachinePoseEdit>);
            ExposeMethod("BeginMachinePoseEdit", &Dispatch<iCAX::FiveAxisCAM::BeginMachinePoseEdit>);
            ExposeMethod("MoveMachineIK", &Dispatch<iCAX::FiveAxisCAM::MoveMachineIK>);
            ExposeMethod("MoveMachine", &Dispatch<iCAX::FiveAxisCAM::MoveMachine>);
            ExposeMethod("PrepareMachines", &Dispatch<iCAX::FiveAxisCAM::PrepareMachines>);
            ExposeMethod("StartMachineJog", &Dispatch<iCAX::FiveAxisCAM::StartMachineJog>);
            ExposeMethod("StopMachineJog", &Dispatch<iCAX::FiveAxisCAM::StopMachineJog>);
            ExposeMethod("CheckCollision", &Dispatch<iCAX::FiveAxisCAM::CheckSimulationCollision>);
            ExposeMethod("AutoRepairCollision", &Dispatch<iCAX::FiveAxisCAM::AutoRepairCollision>);
            ExposeMethod("StartPlayback", &Dispatch<iCAX::FiveAxisCAM::StartPlaybackCollision>);
            ExposeMethod("QueryPlayback", &Dispatch<iCAX::FiveAxisCAM::QueryPlaybackCollision>);
            ExposeMethod("QueryTask", &Dispatch<iCAX::FiveAxisCAM::QueryTask>);
            ExposeMethod("CancelTask", &Dispatch<iCAX::FiveAxisCAM::CancelTask>);
        }
    };
    static_assert(iCAX::Interaction::IsStatelessSDOType<CFiveAxisSimulationSDO>);
    class CFiveAxisHeadSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CFiveAxisHeadSDO() : CSDO("CamHead") {
            ExposeMethod("ListBuiltins", &Dispatch<iCAX::FiveAxisCAM::ListBuiltinHeads>);
            ExposeMethod("Import", &Dispatch<iCAX::FiveAxisCAM::ImportHead>);
            ExposeMethod("Configure", &Dispatch<iCAX::FiveAxisCAM::ConfigureHead>);
            ExposeMethod("Instance", &Dispatch<iCAX::FiveAxisCAM::ManageHeadInstance>);
            ExposeMethod("Configurations", &Dispatch<iCAX::FiveAxisCAM::HeadConfigurationLibrary>);
            ExposeMethod("QueryTask", &Dispatch<iCAX::FiveAxisCAM::QueryTask>);
            ExposeMethod("CancelTask", &Dispatch<iCAX::FiveAxisCAM::CancelTask>);
        }
    };
    static_assert(iCAX::Interaction::IsStatelessSDOType<CFiveAxisHeadSDO>);
    class CFiveAxisFixtureSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CFiveAxisFixtureSDO() : CSDO("CamFixture")
        {
            ExposeMethod("LibraryList", &Dispatch<iCAX::FiveAxisCAM::ReadFixtureLibrary>);
            ExposeMethod("UnitUpdate", &Dispatch<iCAX::FiveAxisCAM::UpdateFixtureUnit>);
            ExposeMethod("UnitRemove", &Dispatch<iCAX::FiveAxisCAM::RemoveFixtureUnit>);
            ExposeMethod("UnitCopy", &Dispatch<iCAX::FiveAxisCAM::CopyFixtureUnit>);
            ExposeMethod("LibraryImport", &Dispatch<iCAX::FiveAxisCAM::ImportFixtureLibrary>);
            ExposeMethod("LibrarySave", &Dispatch<iCAX::FiveAxisCAM::SaveFixtureLibrary>);
            ExposeMethod("LibraryUpdate", &Dispatch<iCAX::FiveAxisCAM::UpdateFixtureLibrary>);
            ExposeMethod("LibraryUse", &Dispatch<iCAX::FiveAxisCAM::UseFixtureLibrary>);
            ExposeMethod("Import", &Dispatch<iCAX::FiveAxisCAM::ImportFixture>);
            ExposeMethod("Update", &Dispatch<iCAX::FiveAxisCAM::UpdateFixture>);
            ExposeMethod("PartProfile", &Dispatch<iCAX::FiveAxisCAM::ReadFixturePartProfileProjection>);
            ExposeMethod("ReadDesign", &Dispatch<iCAX::FiveAxisCAM::ReadFixtureDesignDetails>);
            ExposeMethod("QuerySupportFaces", &Dispatch<iCAX::FiveAxisCAM::QueryFixtureSupportFaces>);
            ExposeMethod("QueryParts", &Dispatch<iCAX::FiveAxisCAM::QueryFixtureParts>);
            ExposeMethod("PartDetails", &Dispatch<iCAX::FiveAxisCAM::ReadFixturePartDetails>);
            ExposeMethod("PartUpdate", &Dispatch<iCAX::FiveAxisCAM::UpdateFixturePart>);
            ExposeMethod("Remove", &Dispatch<iCAX::FiveAxisCAM::RemoveFixture>);
            ExposeMethod("Check", &Dispatch<iCAX::FiveAxisCAM::CheckFixtures>);
            ExposeMethod("SetRecipe", &Dispatch<iCAX::FiveAxisCAM::SetFixtureRecipe>);
            ExposeMethod("FindSupportFaces", &Dispatch<iCAX::FiveAxisCAM::FindFixtureSupportFaces>);
            ExposeMethod("ReadSupportSelection", &Dispatch<iCAX::FiveAxisCAM::ReadFixtureSupportSelection>);
            ExposeMethod("ReadCheckDetails", &Dispatch<iCAX::FiveAxisCAM::ReadFixtureCheckDetails>);
            ExposeMethod("SuggestLayout", &Dispatch<iCAX::FiveAxisCAM::SuggestFixtureLayout>);
            ExposeMethod("Generate", &Dispatch<iCAX::FiveAxisCAM::GenerateFixture>);
            ExposeMethod("Confirm", &Dispatch<iCAX::FiveAxisCAM::ConfirmFixture>);
            ExposeMethod("Breakdown", &Dispatch<iCAX::FiveAxisCAM::BreakdownFixture>);
            ExposeMethod("Export", &Dispatch<iCAX::FiveAxisCAM::ExportFixture>);
            ExposeMethod("QueryTask", &Dispatch<iCAX::FiveAxisCAM::QueryTask>);
            ExposeMethod("CancelTask", &Dispatch<iCAX::FiveAxisCAM::CancelTask>);
        }
    };
    static_assert(iCAX::Interaction::IsStatelessSDOType<CFiveAxisFixtureSDO>);
    class CFiveAxisSetupSDO final : public iCAX::Interaction::CSDO {
    public: CFiveAxisSetupSDO():CSDO("CamSetup") {
        ExposeMethod("Manage", &Dispatch<iCAX::FiveAxisCAM::ManageSetups>);
        ExposeMethod("Copy", &Dispatch<iCAX::FiveAxisCAM::CopySetup>);
        ExposeMethod("SetSequence", &Dispatch<iCAX::FiveAxisCAM::SetSetupSequence>);
        ExposeMethod("ExportProgram", &Dispatch<iCAX::FiveAxisCAM::ExportSetupProgram>);
        ExposeMethod("SetLocked", &Dispatch<iCAX::FiveAxisCAM::SetSetupLocked>);
        ExposeMethod("SetRegistrationPlan", &Dispatch<iCAX::FiveAxisCAM::SetRegistrationPlan>);
        ExposeMethod("ExportRegistrationPlan", &Dispatch<iCAX::FiveAxisCAM::ExportRegistrationPlan>);
    }};
    class CFiveAxisPlacementSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CFiveAxisPlacementSDO() : CSDO("CamPlacement")
        {
            ExposeMethod("SetTransform", &Dispatch<iCAX::FiveAxisCAM::SetPlacement>);
            ExposeMethod("SolveRegistration", &Dispatch<iCAX::FiveAxisCAM::SolveRegistration>);
            ExposeMethod("ReadRegistration", &Dispatch<iCAX::FiveAxisCAM::ReadRegistration>);
            ExposeMethod("ResolveReference", &Dispatch<iCAX::FiveAxisCAM::ResolveRegistrationReference>);
            ExposeMethod("ReadReferenceFaces", &Dispatch<iCAX::FiveAxisCAM::ReadRegistrationFaces>);
            ExposeMethod("ApplyRegistration", &Dispatch<iCAX::FiveAxisCAM::ApplyRegistration>);
        }
    };
    static_assert(iCAX::Interaction::IsStatelessSDOType<CFiveAxisSceneSDO>);
    static_assert(iCAX::Interaction::IsStatelessSDOType<CFiveAxisWorkpieceSDO>);
    static_assert(iCAX::Interaction::IsStatelessSDOType<CFiveAxisPlacementSDO>);
    class CFiveAxisCutSDO final:public iCAX::Interaction::CSDO
    {
    public:
        CFiveAxisCutSDO():CSDO("CamCut")
        {
            ExposeMethod("QuerySources",&Dispatch<iCAX::FiveAxisCAM::QueryCutSources>);
            ExposeMethod("FindRetainedFaces",&Dispatch<iCAX::FiveAxisCAM::FindCutRetainedFaces>);
            ExposeMethod("ReadRetainedSelection",&Dispatch<iCAX::FiveAxisCAM::ReadCutRetainedSelection>);
            ExposeMethod("ResolveFace",&Dispatch<iCAX::FiveAxisCAM::ResolveCutFace>);
            ExposeMethod("ResolveEdge",&Dispatch<iCAX::FiveAxisCAM::ResolveCutEdge>);
            ExposeMethod("ExtractCandidates",&Dispatch<iCAX::FiveAxisCAM::ExtractCutCandidates>);
            ExposeMethod("QueryCandidates",&Dispatch<iCAX::FiveAxisCAM::QueryCutCandidates>);
            ExposeMethod("Create",&Dispatch<iCAX::FiveAxisCAM::CreateCuts>);
            ExposeMethod("Read",&Dispatch<iCAX::FiveAxisCAM::ReadCutDefinitions>);
            ExposeMethod("SetStart",&Dispatch<iCAX::FiveAxisCAM::SetCutStart>);
            ExposeMethod("ResolveStart",&Dispatch<iCAX::FiveAxisCAM::ResolveCutStart>);
            ExposeMethod("ResolveSplitPoint",&Dispatch<iCAX::FiveAxisCAM::ResolveCutSplitPoint>);
            ExposeMethod("Split",&Dispatch<iCAX::FiveAxisCAM::SplitCuts>);
            ExposeMethod("Merge",&Dispatch<iCAX::FiveAxisCAM::MergeCuts>);
            ExposeMethod("Transform",&Dispatch<iCAX::FiveAxisCAM::TransformCuts>);
            ExposeMethod("SetStandardHoleDimensions",&Dispatch<iCAX::FiveAxisCAM::SetStandardHoleDimensions>);
            ExposeMethod("ManageTree",&Dispatch<iCAX::FiveAxisCAM::ManageCutTree>);
            ExposeMethod("SetType",&Dispatch<iCAX::FiveAxisCAM::SetCutType>);
            ExposeMethod("QueryStandardHole",&Dispatch<iCAX::FiveAxisCAM::QueryStandardHole>);
            ExposeMethod("Reverse",&Dispatch<iCAX::FiveAxisCAM::ReverseCuts>);
            ExposeMethod("SetCommands",&Dispatch<iCAX::FiveAxisCAM::SetCutCommands>);
            ExposeMethod("Rename",&Dispatch<iCAX::FiveAxisCAM::RenameCut>);
            ExposeMethod("ManageLayers",&Dispatch<iCAX::FiveAxisCAM::ManageCutLayers>);
            ExposeMethod("QueryLayers",&Dispatch<iCAX::FiveAxisCAM::QueryCutLayers>);
            ExposeMethod("ReadToolAxis",&Dispatch<iCAX::FiveAxisCAM::ReadToolAxis>);
            ExposeMethod("ReadToolAxisSamples",&Dispatch<iCAX::FiveAxisCAM::ReadToolAxisSamples>);
            ExposeMethod("SetToolAxis",&Dispatch<iCAX::FiveAxisCAM::SetToolAxis>);
            ExposeMethod("ListToolAxisPoints",&Dispatch<iCAX::FiveAxisCAM::ListToolAxisPoints>);
            ExposeMethod("ResolveToolAxisPoint",&Dispatch<iCAX::FiveAxisCAM::ResolveToolAxisPoint>);
            ExposeMethod("EditToolAxisPoint",&Dispatch<iCAX::FiveAxisCAM::EditToolAxisPoint>);
            ExposeMethod("SetRetainedSide",&Dispatch<iCAX::FiveAxisCAM::SetCutRetainedSide>);
        }
    };
    class CFiveAxisPlanSDO final:public iCAX::Interaction::CSDO
    {
    public:
        CFiveAxisPlanSDO():CSDO("CamPlan")
        {
            ExposeMethod("Generate",&Dispatch<iCAX::FiveAxisCAM::GenerateToolpaths>);
            ExposeMethod("SetOrder",&Dispatch<iCAX::FiveAxisCAM::SetToolpathOrder>);
            ExposeMethod("QueryOrder",&Dispatch<iCAX::FiveAxisCAM::QueryToolpathOrder>);
            ExposeMethod("BeginOrder",&Dispatch<iCAX::FiveAxisCAM::BeginToolpathOrder>);
            ExposeMethod("QueryStatus",&Dispatch<iCAX::FiveAxisCAM::QueryTask>);
            ExposeMethod("CancelTask",&Dispatch<iCAX::FiveAxisCAM::CancelTask>);
        }
    };
    static_assert(iCAX::Interaction::IsStatelessSDOType<CFiveAxisCutSDO>);
    static_assert(iCAX::Interaction::IsStatelessSDOType<CFiveAxisPlanSDO>);
    class CFiveAxisRapidSDO final:public iCAX::Interaction::CSDO
    {
    public:
        CFiveAxisRapidSDO():CSDO("CamRapid"){
            ExposeMethod("Configure",&Dispatch<iCAX::FiveAxisCAM::ConfigureRapids>);
            ExposeMethod("CheckPath",&Dispatch<iCAX::FiveAxisCAM::CheckRapidPath>);
            ExposeMethod("Preview",&Dispatch<iCAX::FiveAxisCAM::PreviewRapid>);
            ExposeMethod("ReadEditPath",&Dispatch<iCAX::FiveAxisCAM::ReadRapidEditPath>);
            ExposeMethod("EditPath",&Dispatch<iCAX::FiveAxisCAM::EditRapidPath>);
            ExposeMethod("EditPolicy",&Dispatch<iCAX::FiveAxisCAM::EditRapidPolicy>);
            ExposeMethod("Generate",&Dispatch<iCAX::FiveAxisCAM::GenerateRapids>);
            ExposeMethod("Apply",&Dispatch<iCAX::FiveAxisCAM::ApplyRapid>);
            ExposeMethod("QueryStatus",&Dispatch<iCAX::FiveAxisCAM::QueryTask>);
            ExposeMethod("CancelTask",&Dispatch<iCAX::FiveAxisCAM::CancelTask>);
        }
    };
    static_assert(iCAX::Interaction::IsStatelessSDOType<CFiveAxisRapidSDO>);
    class CFiveAxisProcessSDO final:public iCAX::Interaction::CSDO
    {
    public:
        CFiveAxisProcessSDO():CSDO("CamProcess")
        {
            ExposeMethod("Prepare",&Dispatch<iCAX::FiveAxisCAM::PrepareCutPolicies>);
            ExposeMethod("Read",&Dispatch<iCAX::FiveAxisCAM::ReadCutPolicies>);
            ExposeMethod("PreviewLeads",&Dispatch<iCAX::FiveAxisCAM::PreviewCutLeads>);
            ExposeMethod("PreviewCompensation",&Dispatch<iCAX::FiveAxisCAM::PreviewCutCompensation>);
            ExposeMethod("BeginPreview",&Dispatch<iCAX::FiveAxisCAM::BeginPathPreview>);
            ExposeMethod("QueryPreview",&Dispatch<iCAX::FiveAxisCAM::QueryPathPreview>);
            ExposeMethod("Update",&Dispatch<iCAX::FiveAxisCAM::UpdateCutProcess>);
            ExposeMethod("Assign",&Dispatch<iCAX::FiveAxisCAM::AssignCutProcess>);
            ExposeMethod("UpdatePose",&Dispatch<iCAX::FiveAxisCAM::UpdateCutPose>);
            ExposeMethod("Clear",&Dispatch<iCAX::FiveAxisCAM::ClearCutProcess>);
            ExposeMethod("RestoreInherited",&Dispatch<iCAX::FiveAxisCAM::RestoreCutProcess>);
            ExposeMethod("Compile",&Dispatch<iCAX::FiveAxisCAM::CompileCutProcess>);
            ExposeMethod("QueryCompiled",&Dispatch<iCAX::FiveAxisCAM::QueryCompiledProcess>);
        }
    };
    static_assert(iCAX::Interaction::IsStatelessSDOType<CFiveAxisProcessSDO>);
}
ICAX_REGISTER_SDO(CFiveAxisSceneSDO)
ICAX_REGISTER_SDO(CFiveAxisHeadSDO)
ICAX_REGISTER_SDO(CFiveAxisSimulationSDO)
ICAX_REGISTER_SDO(CFiveAxisFixtureSDO)
ICAX_REGISTER_SDO(CFiveAxisWorkpieceSDO)
ICAX_REGISTER_SDO(CFiveAxisPlacementSDO)
ICAX_REGISTER_SDO(CFiveAxisSetupSDO)
ICAX_REGISTER_SDO(CFiveAxisCutSDO)
ICAX_REGISTER_SDO(CFiveAxisPlanSDO)
ICAX_REGISTER_SDO(CFiveAxisRapidSDO)
ICAX_REGISTER_SDO(CFiveAxisProcessSDO)
