#include "FiveAxisCAM.h"
#include "FiveAxisComponents.h"
#include "FiveAxisOrdering.h"
#include "FiveAxisKeyposeSchedule.h"
#include "FiveAxisMachinePlanPolicy.h"
#include "FiveAxisIntentScene.h"
#include "FiveAxisIntentOperations.h"
#include "FiveAxisIntentPoseField.h"
#include "OpenCascadeResourceImport/CadBRepImportService.h"
#include "OpenCascadeResourceImport/CadBRepScaleService.h"
#include "OpenCascadeResourceImport/CadBRepTransformService.h"
#include "GeometryServices/BRepDisplayService.h"
#include "OpenCascadeResourceImport/RigidRegistrationService.h"
#include "OpenCascadeResourceImport/CadReferenceService.h"
#include "OpenCascadeResourceImport/CadSnapFeatureService.h"
#include "OpenCascadeResourceImport/CurveEditingService.h"
#include "OpenCascadeResourceImport/CadClearanceService.h"
#include "OpenCascadeResourceImport/SectionPlateService.h"
#include "OpenCascadeResourceImport/OpenCascadeTaskExecution.h"
#include "OpenCascadeResourceImport/SectionPlateEditingService.h"
#include "OpenCascadeResourceImport/TubeCadJson.h"
#include "OpenCascadeResourceImport/CadDocumentImportService.h"
#include "TemplateRuntime/StandardJsonCodec.h"
#include <fstream>
#include <bit>
#include <span>
#include <ShlObj.h>
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#include <sstream>
#include <iomanip>
#include <numbers>
#include <cstring>
#include "../../cam/CutRecognition/CutRecognitionService.h"
#include "../../cam/IntentToolpath/IntentToolpath.h"
#include "../../cam/IntentToolpath/EditableToolpathPreview.h"
#include "../../cam/Machine/MachineResources.h"
#include "../../cam/Machine/MachineDescriptionLoader.h"
#include "../../cam/Machine/MachinePrimitiveMesh.h"
#include "../../cam/Machine/MachineInstanceComponents.h"
#include "../../cam/Machine/MachineMotion.h"
#include "../../cam/Machine/MachineKeyposes.h"
#include "../../cam/Machine/MachinePoseIK.h"
#include "../../cam/Machine/MachineSweep.h"
#include "../../cam/Machine/MachineTransformConstraints.h"
#include "GeometryServices/CollisionDistanceService.h"
#include "GeometryServices/CollisionMeshVolumes.h"
#include "GeometryServices/MachineMotionPlanningService.h"
#include "RenderData/RenderData.h"
#include "../../cam/ProcessPlanning/ProcessPlanningService.h"
#include "GeometryServices/BoundaryLeadService.h"
#include "GeometryServices/OffsetCurveTraversal.h"
#include "GeometryServices/SurfaceTravelService.h"
#include "GeometryServices/RapidPathGeometryService.h"
#include "GeometryServices/RapidPathPlanningService.h"
#include "GeometryServices/RigidMotionCollisionService.h"
#include "Data/VariantSerializer.h"
#include "ProjectFile/ProjectDocument.h"
#include "Resources/BinaryResource.h"
#include "Resources/FlatBufferResource.h"
#include <flatbuffers/flatbuffers.h>
#include "Generated/MachineIKTarget_generated.h"
#include "Generated/MachineIKResult_generated.h"
#include "Generated/MachineJogControl_generated.h"
#include "Generated/MachineJogResult_generated.h"
#include "Generated/TaskProgress_generated.h"
#include "PDO/IPDOHub.h"
#include "PDO/PDOLease.h"
#include "Resources/ResourceLibrary.h"
#include "RenderInteraction/RenderInteractionComponents.h"
#include "RenderInteraction/RenderResourceAdapter.h"
#include "Services/ServiceProvider.h"
#include "Transform/TransformComponent.h"
#include "Transform/Transform.h"
#include "Task/Task.h"
#include "GeometryData/BRepPersistence.h"
#include <Windows.h>
#include <mmsystem.h>
#pragma comment(lib,"winmm.lib")
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <set>
#include <type_traits>
#include <tuple>
#include "FiveAxisRapidParallel.h"

namespace iCAX::FiveAxisCAM
{
    using Scene = iCAX::Project::ISceneContext;
    using Repository = iCAX::Database::IRepository;
    using Variant = iCAX::Data::Variant;
    using Ref = iCAX::Resource::CResourceReference;
    struct ImportResult
    {
        ImportResult() = default;
        explicit ImportResult(iCAX::CAD::SCadBRepImportResult&& value) : Import(std::move(value)) {}
        iCAX::CAD::SCadBRepImportResult Import;
        iCAX::GeometryData::BRepDisplayEdges DisplayEdges;
        struct HeadVisual {
            iCAX::CAM::SMachineVisualData Declaration;
            std::shared_ptr<iCAX::CAD::SCadBRepImportResult> Geometry;
            double UnitScale=1000.;
        };
        std::shared_ptr<iCAX::CAM::CMachineDescriptionResource> HeadDescription;
        std::vector<std::vector<HeadVisual>> HeadVisuals;
        std::size_t HeadTool=0;
        bool HeadCalibrated=false;
    };
    struct PlateGenerationResult
    {
        std::vector<iCAX::Geometry::SectionPlate> Plates;
        std::vector<iCAX::GeometryData::BRepDisplayEdges> PlateDisplayEdges;
        iCAX::GeometryData::BRepModel DisplayGeometry;
        iCAX::GeometryData::BRepDisplayEdges DisplayEdges;
        double DisplayDeflection = .0025;
        Map CutClearance;
        std::vector<Ref> Dependencies;
    };
    namespace
    {
        template<class T> std::shared_ptr<T> Component(const std::shared_ptr<iCAX::Database::IEntity>& entity)
        { return entity ? std::dynamic_pointer_cast<T>(entity->GetComponent(T::S_ClassName)) : nullptr; }
        template<class T> std::shared_ptr<T> Require(Repository& db, const ID& id)
        {
            auto result = Component<T>(db.GetEntity(id));
            if (!result) throw std::invalid_argument("CAM_OBJECT_NOT_FOUND: object is outside this scene or has the wrong kind");
            return result;
        }
        std::string String(const Map& value, const std::string& key)
        {
            auto found = value.find(key);
            if (found == value.end() || !found->second.Is<Text>())
                throw std::invalid_argument("CAM_FIELD_REQUIRED: " + key);
            return found->second.To<Text>();
        }
        ID Identifier(const Map& value, const std::string& key)
        {
            auto parsed = ID::from_string(String(value, key));
            if (!parsed || parsed->is_nil()) throw std::invalid_argument("CAM_INVALID_ID: " + key);
            return *parsed;
        }
        ID OptionalIdentifier(const Map& value,const std::string& key)
        {
            if(!value.contains(key)||String(value,key).empty())return {};
            const auto parsed=ID::from_string(String(value,key));
            if(!parsed)throw std::invalid_argument("CAM_INVALID_ID: "+key);
            return *parsed;
        }
        Text OptionalID(const ID& id){return id.is_nil()?Text():iCAX::Data::to_string(id);}
        UInt Integer(const Map& value, const std::string& key)
        {
            const auto text = String(value, key);
            UInt number = 0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
            if (result.ec != std::errc() || result.ptr != text.data() + text.size())
                throw std::invalid_argument("CAM_INVALID_REVISION: " + key);
            return number;
        }
        UInt Next(UInt revision)
        {
            if (revision == std::numeric_limits<UInt>::max()) throw std::overflow_error("CAM_REVISION_EXHAUSTED");
            return revision + 1;
        }
        double FiniteNumber(const Variant& value, const char* error)
        {
            // The SDK encodes small whole JS numbers as int, including 0 and 1
            // in coordinates and matrices. Accept numeric Variant alternatives
            // without treating bool, char or numeric strings as measurements.
            const double number = std::visit([error](const auto& item) -> double {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>)
                    return static_cast<double>(item);
                else throw std::invalid_argument(error);
            }, value.m_Value);
            if (!std::isfinite(number)) throw std::invalid_argument(error);
            return number;
        }
        Array Identity() { return {1.,0.,0.,0., 0.,1.,0.,0., 0.,0.,1.,0., 0.,0.,0.,1.}; }
        Map StoredRef(const Ref& reference)
        { return iCAX::ProjectFile::MakeResourceReferenceValue({reference.URL, reference.nVersion}).To<Map>(); }
        Map StoredRef(const Map& value){return value;}
        Map WireRef(const Map& reference)
        {
            if (reference.empty()) return {};
            if(reference.contains("revision")&&reference.contains("value"))return reference;
            const auto ref = iCAX::ProjectFile::TryGetResourceReferenceValue(reference);
            if (!ref) throw std::runtime_error("CAM_INVALID_RESOURCE_REFERENCE");
            return {{"url", ref->URL}, {"version", std::to_string(ref->nVersion)}};
        }
        #include "FiveAxisEngineeringData.inc"
        void Commit(Repository& db, iCAX::Database::ITransaction& tx)
        {
            std::string error;
            if (!db.CommitTransaction(tx, error)) throw std::runtime_error("CAM_COMMIT_FAILED: " + error);
        }
        template<class Action> void Transaction(Repository& db, const std::string& name, Action action, bool undo = true)
        {
            auto scope = undo ? db.BeginUndoCommand(name) : nullptr;
            auto& tx = db.BeginTransaction(name);
            try { action(tx); }
            catch (...) { db.CancelTransaction(tx); throw; }
            // Commit consumes the transaction, including when it rolls back.
            Commit(db, tx);
            if (scope) scope->End();
        }
    }

    struct CompilationSnapshot;
    struct GenerationSnapshot;
    struct RecognitionInput : iCAX::CAM::Recognition::SRecognitionInput {Map PlanningData;};
    struct GeneratedGeometry;
    struct RapidSnapshot;
    struct SimulationCollisionSnapshot;
    struct CollisionRepairSnapshot;
    struct CollisionRepairResult;
    struct MachinePlanSnapshot;
    struct CollisionRepairProgress final { std::mutex Mutex;Map State; };
    struct PlaybackCollisionProgress
    {
        std::atomic_size_t Through{0};
        std::mutex Mutex;
        std::map<std::size_t,iCAX::GeometryServices::RigidMotionCollisionSegment> Results;
        Map HeadCoverage;
        std::map<std::size_t,Map> HeadPoses;
    };
    struct SimulationCollisionProgress final
    {
        std::mutex Mutex;
        std::vector<std::size_t> SegmentIndices;
        std::size_t Completed=0,FullSegmentCount=0;
        std::size_t KeypointsCompleted=0,KeypointCount=0;
        bool CheckingKeypoints=false;
        std::size_t WorkerCount=1;
        std::set<std::size_t> Active;
        std::map<std::size_t,iCAX::GeometryServices::RigidMotionCollisionSegment> KeypointResults;
        std::optional<std::size_t> Current,LastCompleted;
        Map Location;
    };
    struct FixtureGenerationModel final {
        Map Assembly;
        Map SourcePreview;
    };
    struct ImportTask final
    {
        Text TaskID = iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
        Text CommandID, Payload, Status = "Running", Error, Kind="Import",TerminationReason;
        ID SetupID;
        UInt Sequence = 0;
        Map Result;
        std::shared_ptr<std::atomic_bool> Cancelled = std::make_shared<std::atomic_bool>(false);
        std::shared_ptr<std::atomic_uint> Progress = std::make_shared<std::atomic_uint>(0);
        iCAX::Tasks::Task<std::shared_ptr<ImportResult>> Worker;
        iCAX::Tasks::Task<std::shared_ptr<iCAX::CAM::Recognition::SRecognitionResult>> RecognitionWorker;
        std::shared_ptr<const iCAX::FiveAxisCAM::RecognitionInput> RecognitionInput;
        std::shared_ptr<iCAX::CAM::Recognition::SRecognitionResult> RecognitionResult;
        ID PlacementID;
        Text DraftID,DraftDigest,ConsumedByCommandID;
        UInt DraftRevision=0;
        Ref CandidateSet;
        ID IntentID;
        std::vector<ID> IntentEditOperations,IntentEditSetups;
        std::shared_ptr<const CompilationSnapshot> CompilationInput;
        iCAX::Tasks::Task<std::shared_ptr<Map>> CompilationWorker;
        std::shared_ptr<const Map> CompiledModel;
        iCAX::Tasks::Task<std::shared_ptr<std::vector<iCAX::Geometry::SClearancePair>>> ClearanceWorker;
        double ClearanceMm = 0;
        UInt SetupRevision = 0;
        Text ClearanceInputKey;
        Map FixtureRecipe;
        bool FixtureDraft=false,FixtureNewUnit=false;
        std::shared_ptr<FixtureGenerationModel> FixtureModel;
        Map SupportSelection;
        Map RetainedFaceSelection;
        ID FixtureUnitID;
        std::vector<std::pair<Ref,bool>> FixtureDraftResources;
        iCAX::Tasks::Task<std::shared_ptr<PlateGenerationResult>> PlateWorker;
        iCAX::Tasks::Task<std::shared_ptr<std::vector<std::uint64_t>>> SupportFaceWorker;
        iCAX::Tasks::Task<std::shared_ptr<std::vector<std::uint64_t>>> RetainedFaceWorker;
        std::shared_ptr<const GenerationSnapshot> GenerationInput;
        iCAX::Tasks::Task<std::shared_ptr<GeneratedGeometry>> GenerationWorker;
        std::shared_ptr<RapidSnapshot> RapidInput;
        iCAX::Tasks::Task<std::shared_ptr<std::vector<iCAX::GeometryServices::RapidPathPlanningResult>>> RapidWorker;
        std::shared_ptr<const SimulationCollisionSnapshot> SimulationInput;
        iCAX::Tasks::Task<std::shared_ptr<iCAX::GeometryServices::RigidMotionCollisionResult>> SimulationWorker;
        std::shared_ptr<PlaybackCollisionProgress> PlaybackProgress;
        std::shared_ptr<SimulationCollisionProgress> CollisionProgress;
        std::shared_ptr<const CollisionRepairSnapshot> RepairInput;
        std::shared_ptr<const CollisionRepairResult> RepairCandidate;
        std::shared_ptr<CollisionRepairProgress> RepairProgress;
        iCAX::Tasks::Task<std::shared_ptr<CollisionRepairResult>> RepairWorker;
        std::shared_ptr<const MachinePlanSnapshot> MachinePlanInput;
        std::shared_ptr<CollisionRepairProgress> MachinePlanProgress;
        mutable std::shared_ptr<void> ProgressPDO;
        iCAX::Tasks::Task<std::shared_ptr<Map>> MachinePlanWorker;
        std::shared_ptr<const Map> MachinePlanModel;
        Map OrderRequest;
        iCAX::Tasks::Task<std::shared_ptr<Map>> OrderWorker;
    };
    struct FilePreviewTask final
    {
        Text TaskID = iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
        Text Status = "Running", Error;
        Map Result;
        std::shared_ptr<std::atomic_bool> Cancelled = std::make_shared<std::atomic_bool>(false);
        iCAX::Tasks::Task<std::shared_ptr<Map>> Worker;
    };
    struct SceneRuntime final
    {
        std::shared_ptr<iCAX::CAM::Intent::CToolpathOperationRegistry> IntentRegistry;
        std::shared_ptr<const iCAX::CAM::Intent::IToolpathGeometryServices> IntentGeometry;
        std::unique_ptr<iCAX::CAM::Intent::CEditableToolpathDocument> IntentDocument;
        std::map<ID,std::shared_ptr<iCAX::CAM::Intent::CToolpathGeneration>> IntentGenerations;
        std::map<ID,Text> IntentGenerationTasks;
        std::map<Text,std::shared_ptr<const CFrozenFiveAxisIntentModel>> IntentModels;
        std::map<ID,std::pair<Text,std::shared_ptr<const CFiveAxisIntentOperationContext>>> IntentContexts;
        bool PreparingIntents = false;
        bool IntentInputsChanged = false;
        bool DeferredPoseInputsChanged = false;
        std::map<ID,iCAX::CAM::Intent::SEffectiveToolpaths> IntentInspections;
        struct OrderStructure final {
            UInt Sequence=0;
            bool Valid=false;
            Array Order,Groups;
            // Identity and participation only; no curves, poses or samples.
            std::map<ID,iCAX::CAM::Intent::SResolvedPath> Paths;
        };
        std::map<ID,OrderStructure> OrderStructures;
        std::map<ID,iCAX::CAM::Intent::SEffectiveToolpaths> IntentResolutions;
        std::map<std::pair<ID,iCAX::CAM::Intent::SPortRef>,iCAX::CAM::Intent::SResolvedPath> IntentPortPaths;
        std::map<ID,Map> IntentDefinitions;
        struct PoseAuthoringSnapshot final {
            UInt Sequence=0;
            iCAX::CAM::Intent::SResolvedPath Terminal,Authoring;
        };
        std::map<ID,PoseAuthoringSnapshot> PoseAuthoringSnapshots;
        std::map<Text,Map> PosePointPositions;
        std::shared_ptr<iCAX::Application::IProductUserDataStore> FixtureLibraryStore;
        Array BuiltinHeadSources;
        const Text Epoch = iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
        UInt Sequence = 0;
        std::map<Text, ImportTask> Tasks;
        std::map<Text, FilePreviewTask> FilePreviews;
        struct PathPreview final { FilePreviewTask Task; UInt Sequence=0; Text Key; Map DeferredRequest;std::set<ID> WaitingPlans; };
        std::map<Text,PathPreview> PathPreviews;
        std::map<Text, std::pair<Text, Map>> Receipts;
        std::map<ID,Text> LatestExtraction;
        std::map<ID,Text> LatestCompilation;
        std::map<ID,Text> LatestGeneration;
        std::map<ID,std::pair<Text,Ref>> PathSamples;
        std::map<Text,Map> RapidEditingGeometry;
        std::map<Text,std::shared_ptr<const iCAX::Geometry::SCadSnapFeatures>> SnapFeatures;
        std::map<Text,Ref> SnapCatalogResources;
        struct CollisionMeshCacheEntry {
            std::shared_ptr<const iCAX::GeometryServices::CollisionMesh> Mesh;
            UInt LastUsed=0;
        };
        std::map<Text,CollisionMeshCacheEntry> CollisionMeshes;
        std::map<Text,std::array<double,6>> RapidWorkpieceBounds;
        UInt CollisionMeshClock=0,CollisionMeshHits=0,CollisionMeshMisses=0;
        double CollisionMeshPreparationMs=0;
        UInt CollisionMeshDiskHits=0,CollisionMeshDiskMisses=0;
        double CollisionMeshRefinementMs=0;
        std::map<Text,std::vector<iCAX::GeometryServices::CollisionBox>> HeadCollisionBoxes;
        struct MachineCollisionCacheEntry {
            Text Key;
            iCAX::GeometryServices::CollisionDistanceModelRef Model;
            std::shared_ptr<const iCAX::GeometryData::BRepModel> Geometry;
            double Radius=0;
            std::array<double,3> Min{},Max{};
            std::shared_ptr<const iCAX::GeometryServices::CollisionMesh> Mesh;
            std::vector<std::pair<Map,iCAX::GeometryServices::CollisionDistanceModelRef>> DiagnosticParts;
        };
        // Scene-owned immutable geometry. Joint positions do not invalidate it.
        std::map<Text,MachineCollisionCacheEntry> MachineCollisionCache;
        std::shared_ptr<iCAX::GeometryServices::ICollisionDistanceService> MachineCollisionService;
        // Immutable resource-channel projections, bounded to plan/attempt/program per setup.
        std::map<Text,std::pair<Text,Ref>> MachineWireResources;
        UInt MachineConfigurationSequence=0;
        bool JogCommitting=false;
        Text JogToken;
        Map JogStatus;
        Array JogTimings;
        std::chrono::steady_clock::time_point JogLease;
        std::function<void(Scene&,double)> JogTick;
        std::unique_ptr<iCAX::Database::IRepositoryUndoScope> JogUndo;
        std::shared_ptr<void> JogCache;
        std::map<Text,Map> JogTerminal;
        Text PoseEditToken;
        std::unique_ptr<iCAX::Database::IRepositoryUndoScope> PoseEditUndo;
        std::shared_ptr<void> PoseEditCache;
        std::vector<UInt> MachineInteractionRetiredSlots;
        std::vector<UInt> TaskProgressRetiredSlots;
        std::map<Text,Map> PoseEditTerminal;
        std::shared_ptr<iCAX::Database::IRepositoryEventListener> Observer;
        ~SceneRuntime() {
            for (auto& [_, preview] : PathPreviews) preview.Task.Cancelled->store(true);
            for (auto& [_, generation] : IntentGenerations) generation->Cancel();
            IntentGenerations.clear();
            IntentDocument.reset();
            for (auto& [_, task] : Tasks) task.Cancelled->store(true);
            for (auto& [_, task] : FilePreviews) task.Cancelled->store(true);
        }
    };
    namespace
    {
        void InitializeIntentDocument(Scene&);
        void PumpIntentGenerations(Scene&);
        void PumpIntentExecution(Scene&,SceneRuntime&,ImportTask&);
        void PumpIntentEdit(Scene&,SceneRuntime&,ImportTask&);
        void PumpIntentCompilation(Scene&,SceneRuntime&,ImportTask&);
        Map EditIntentPosePolicies(Scene&,const Map&);
        void QueueIntentGeneration(Scene&,const ID&,std::size_t concurrency=0);
        Map CutMetadataPatch(Scene&,const ID&,bool);
        Map CutDefinitionsProjection(Scene&,const Array&);
        std::vector<Map> CommitIntentHostCommands(Scene&,const std::vector<Map>&,
            const std::function<void(iCAX::Database::ITransaction&,const std::vector<Map>&)>&,const Text&,const std::set<ID>&);
        iCAX::CAM::Intent::CEditableToolpathDocument& IntentDocument(Scene&);
        ID IntentPlan(Scene&,const ID&);
        const std::vector<iCAX::CAM::Intent::SResolvedPath>& IntentPaths(Scene&,const ID&);
        iCAX::CAM::Intent::SResolvedPath ReadIntentPath(Scene&,const ID&);
        ID IntentSourceOperation(Scene&,const iCAX::CAM::Intent::SPortRef&);
        std::shared_ptr<const CFiveAxisIntentOperationContext> FreezeIntentContext(Scene&,const ID&);
        void FinishExtraction(Scene&,SceneRuntime&,ImportTask&);
        void FinishCompilation(Scene&,SceneRuntime&,ImportTask&);
        void FinishGeneration(Scene&,SceneRuntime&,ImportTask&);
        void FinishOrdering(Scene&,SceneRuntime&,ImportTask&);
        void FinishRapids(Scene&,SceneRuntime&,ImportTask&);
        void FinishSimulationCollision(Scene&,SceneRuntime&,ImportTask&);
        void FinishPlaybackCollision(Scene&,SceneRuntime&,ImportTask&);
        void FinishCollisionRepair(Scene&,SceneRuntime&,ImportTask&);
        void FinishMachinePlan(Scene&,SceneRuntime&,ImportTask&);
        Map ProjectMachinePlanReport(Scene&,const ID&,const Map&,const Text& attempt={});
        Ref PublishMachineWireResource(Scene&,const ID&,const Text&,const Text&,const Map&,const std::vector<Ref>&);
        Ref PublishQueryDocument(Scene&,const ID&,const Text&,const Text&,const Map&);
        void DiscardQueryDocument(Scene&,const ID&,const Text&);
        Map FixtureDesignSummary(const Map&);
        void SetupIdle(Scene&);
        void ApplyRapidState(Scene&,const ID&,Map&);
        void FinishFixtureCheck(Scene&,SceneRuntime&,ImportTask&);
        void FinishFixtureGeneration(Scene&,SceneRuntime&,ImportTask&);
        void FinishFixtureSupportFaces(Scene&,SceneRuntime&,ImportTask&);
        void FinishCutRetainedFaces(Scene&,SceneRuntime&,ImportTask&);
        Map PublishScale(Scene&,const ImportTask&,ImportResult&&);
        Map DefaultPlanningProfile(Scene&);
        Array PrepareDefaultFixtures(Scene&);
        Map PublishFixtureLibraryImport(Scene&,const Ref&,const Ref&,const Text&);
        void AttachLibraryFixtures(iCAX::Database::ITransaction&,const ID&,const Array&);
        class MutationObserver final : public iCAX::Database::IRepositoryEventListener
        {
            std::weak_ptr<SceneRuntime> m_Runtime;
        public:
            explicit MutationObserver(const std::shared_ptr<SceneRuntime>& runtime) : m_Runtime(runtime) {}
            void OnRepositoryChanging(void*, const iCAX::Database::RepositoryEventArgs&) override {}
            void OnRepositoryChanged(void*, const iCAX::Database::RepositoryEventArgs& event) override
            {
                // Runtime render/transform projection updates are not editable
                // input. All product-domain facts (including Undo/Redo) are.
                auto relevant = [](const auto& e) { return e.strClassName.starts_with("CFiveAxis")||e.strClassName.starts_with("CIntent")||e.strClassName=="CCutIntentComponent"
                    ||e.strClassName=="CEditableDocumentComponent"||e.strClassName=="CEditableOperationComponent"||e.strClassName=="CEditablePlanComponent"||e.strClassName=="CEditablePathLabelsComponent"
                    ||e.strClassName=="CMachineCollisionComponent"; };
                bool changed = relevant(event);
                if (event.pBatch) for (const auto& record : event.pBatch->Records) changed |= relevant(record);
                if (auto runtime = m_Runtime.lock()) {
                    if (changed) ++runtime->Sequence;
                    const auto invalidatesProjection=[](const auto& e){return e.strClassName.starts_with("CEditable")||e.strClassName.starts_with("CFiveAxis");};
                    bool projectionChanged=invalidatesProjection(event);
                    if(event.pBatch)for(const auto& record:event.pBatch->Records)projectionChanged|=invalidatesProjection(record);
                    if(projectionChanged){runtime->IntentInspections.clear();runtime->IntentResolutions.clear();runtime->IntentPortPaths.clear();
                        // Result publication can change path identities without
                        // changing the editable-input sequence.
                        for(auto& [_,structure]:runtime->OrderStructures)structure.Valid=false;}
                    const auto definitionChanged=[](const auto& e){return e.strClassName.empty()||e.strClassName=="CEditableOperationComponent";};
                    bool definitions=!event.pBatch&&definitionChanged(event);
                    if(event.pBatch)for(const auto& record:event.pBatch->Records)definitions|=definitionChanged(record);
                    if(definitions)runtime->IntentDefinitions.clear();
                    const auto intentChanged=[](const auto& e){
                        if(e.strClassName=="CEditableOperationComponent")return true;
                        if(e.strClassName!="CEditablePlanComponent")return false;
                        if(e.PreviousProperties.contains("Definition")&&e.NewProperties.contains("Definition"))
                        {auto before=e.PreviousProperties.at("Definition").template To<Map>(),after=e.NewProperties.at("Definition").template To<Map>();
                         for(const auto* field:{"pathLayers","operationLayers","processingLayers","executionConstraints"}){before.erase(field);after.erase(field);}return before!=after;}
                        return true;};
                    bool intent=intentChanged(event);
                    if(event.pBatch)for(const auto& record:event.pBatch->Records)intent|=intentChanged(record);
                    const auto localPose=[](const auto& e){
                        if(e.strClassName!="CEditableOperationComponent")return false;
                        for(const auto* properties:{&e.NewProperties,&e.PreviousProperties})if(properties->contains("Definition")){
                            const auto definition=properties->at("Definition").template To<Map>();
                            if(definition.contains("typeId")&&definition.at("typeId").template To<Text>()=="fiveaxis.pose.delta")return true;
                        }return false;
                    };
                    bool deferred=localPose(event);
                    if(event.pBatch)for(const auto& record:event.pBatch->Records)deferred|=localPose(record);
                    runtime->DeferredPoseInputsChanged|=intent&&deferred;
                    runtime->IntentInputsChanged|=intent;
                    const auto machineChanged=[](const auto& e){return e.strClassName.starts_with("CMachine");};
                    bool machine=machineChanged(event);
                    if(event.pBatch)for(const auto& record:event.pBatch->Records)machine|=machineChanged(record);
                    if(machine&&!runtime->JogCommitting)++runtime->MachineConfigurationSequence;
                }
            }
        };
        std::shared_ptr<SceneRuntime> Runtime(Scene& scene)
        {
            if (scene.IsStopRequested()) throw std::runtime_error("CAM_SCENE_CLOSING");
            auto root = Component<CFiveAxisSceneComponent>(scene.Database().GetMetaEntity());
            if (!root) throw std::runtime_error("CAM_SCENE_NOT_INITIALIZED");
            if (!root->Runtime)
            {
                root->Runtime = std::make_shared<SceneRuntime>();
                root->Runtime->Observer = std::make_shared<MutationObserver>(root->Runtime);
                scene.Database().AddObserver(root->Runtime->Observer);
            }
            return root->Runtime;
        }
        void CheckExpected(const SceneRuntime& runtime, const Map& request)
        {
            const auto expected = request.at("expected").To<Map>();
            if (String(expected, "epoch") != runtime.Epoch || Integer(expected, "mutationSequence") != runtime.Sequence)
                throw std::invalid_argument("CAM_REVISION_CONFLICT: refresh scene state before retrying");
        }
        void CheckCapacity(const SceneRuntime& runtime)
        {
            if (runtime.Receipts.size() + runtime.Tasks.size() >= 2048)
                throw std::runtime_error("CAM_COMMAND_BUDGET: save and reopen the project");
        }
        bool WorkpieceLocked(Scene& scene,const ID& workpieceID) {
            if(Require<CFiveAxisWorkpieceComponent>(scene.Database(),workpieceID)->GetLocked())return true;
            for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))
                if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetWorkpieceID()==workpieceID&&!p->GetSetupID().is_nil())
                    if(Require<CFiveAxisSetupComponent>(scene.Database(),p->GetSetupID())->GetLocked())return true;
            return false;
        }
        void RequireWorkpieceUnlocked(Scene& scene,const ID& workpieceID) {
            if(WorkpieceLocked(scene,workpieceID))throw std::invalid_argument("CAM_WORKPIECE_LOCKED: 此几何关联的装夹已锁定，请先解锁相关装夹。");
        }
        void RequireSetupUnlocked(Scene& scene,const ID& setupID) {
            if(Require<CFiveAxisSetupComponent>(scene.Database(),setupID)->GetLocked())throw std::invalid_argument("CAM_SETUP_LOCKED: 请先解锁当前装夹姿态。");
        }
        void RequireMachiningSetup(Scene& scene,const ID& setupID) {
            if(!Require<CFiveAxisSetupComponent>(scene.Database(),setupID)->GetEnabled())throw std::invalid_argument("CAM_SETUP_DISABLED: 请先启用装夹。");
            bool found=false;
            for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))
                if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetSetupID()==setupID){
                    found=true;if(!Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID())->GetEnabled())throw std::invalid_argument("CAM_WORKPIECE_DISABLED: 请先启用工件。");
                }
            if(!found)throw std::invalid_argument("CAM_WORKPIECE_REQUIRED: 请先导入工件。");
        }
        bool SetupWorkpieceEnabled(Scene& scene,const ID& setupID)
        {
            for(const auto& entity:scene.Database().FilterEntities([](auto){return true;}))
                if(const auto placement=Component<CFiveAxisPlacementComponent>(entity);placement&&placement->GetSetupID()==setupID)
                    if(!Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID())->GetEnabled())return false;
            return true;
        }
        void RequireSetupWorkpieceEnabled(Scene& scene,const ID& setupID)
        {
            if(!Require<CFiveAxisSetupComponent>(scene.Database(),setupID)->GetEnabled())throw std::invalid_argument("CAM_SETUP_DISABLED: 请先启用装夹。");
            if(!SetupWorkpieceEnabled(scene,setupID))throw std::invalid_argument("CAM_WORKPIECE_DISABLED: 工件已禁用，请先启用工件。");
        }
        void AttachTaskProgressPDO(Scene&,const SceneRuntime&,const ImportTask&,Map&);
        Variant WireJsonValue(const Variant& value){
            // Match the SDO wire representation, also inside source anchors.
            if(value.Is<ID>())return iCAX::Data::to_string(value.To<ID>());
            if(value.Is<Map>()){Map object;for(const auto& [key,child]:std::get<Map>(value.m_Value))object.emplace(key,WireJsonValue(child));return object;}
            if(value.Is<Array>()){Array array;for(const auto& child:std::get<Array>(value.m_Value))array.emplace_back(WireJsonValue(child));return array;}
            return value;
        }
        Map TaskTelemetry(const SceneRuntime& runtime, const ImportTask& task)
        {
            Map result{{"epoch", runtime.Epoch}, {"taskId", task.TaskID}, {"commandId", task.CommandID},
                    {"status", task.Status}, {"kind",task.Kind},{"terminationReason",task.TerminationReason}, {"error", task.Error},{"progress",std::to_string(task.Progress->load())}};
            if(task.CollisionProgress){const auto& p=*task.CollisionProgress;std::lock_guard lock(task.CollisionProgress->Mutex);
                Map progress{{"completedSegmentCount",std::to_string(p.Completed)},{"totalSegmentCount",std::to_string(p.SegmentIndices.size())},{"fullSegmentCount",std::to_string(p.FullSegmentCount)}};
                progress["stage"]=Text(p.CheckingKeypoints?"Keypoints":"Intervals");progress["completedKeypointCount"]=std::to_string(p.KeypointsCompleted);progress["totalKeypointCount"]=std::to_string(p.KeypointCount);
                progress["workerCount"]=std::to_string(p.WorkerCount);progress["activeTaskCount"]=std::to_string(p.Active.size());
                if(!p.Location.empty())progress["location"]=p.Location;
                if(p.Current&&task.Status=="Running"&&!task.Cancelled->load()&&task.Sequence==runtime.Sequence)progress["currentSegmentIndex"]=std::to_string(*p.Current);
                if(p.LastCompleted)progress["lastCompletedSegmentIndex"]=std::to_string(*p.LastCompleted);
                result["collisionProgress"]=progress;
            }
            if(task.RepairProgress){std::lock_guard lock(task.RepairProgress->Mutex);result["repairProgress"]=task.RepairProgress->State;}
            if(task.MachinePlanProgress){std::lock_guard lock(task.MachinePlanProgress->Mutex);result["machineProgress"]=task.MachinePlanProgress->State;}
            return result;
        }
        Map TaskState(Scene& scene,const SceneRuntime& runtime,const ImportTask& task)
        {
            auto result=TaskTelemetry(runtime,task);
            result["result"]=task.Result;
            if(task.Status=="Running")result["result"]=Map{};
            AttachTaskProgressPDO(scene,runtime,task,result);
            return result;
        }
#include "FiveAxisTaskProgressPDO.inc"
        void Invalidate(iCAX::Database::ITransaction& tx, Repository& db, const ID& setupID)
        {
            if(setupID.is_nil()){
                const auto root=db.GetMetaEntity();const auto job=Require<CFiveAxisJobComponent>(db,root->GetID());
                tx.ModifyComponent(root->GetID(),CFiveAxisJobComponent::S_ClassName,{{"InputRevision",Next(job->GetInputRevision())}});return;
            }
            const auto setup = Require<CFiveAxisSetupComponent>(db, setupID);
            const auto job = Require<CFiveAxisJobComponent>(db, setup->GetJobID());
            tx.ModifyComponent(setupID, CFiveAxisSetupComponent::S_ClassName,
                {{"InputRevision", Next(setup->GetInputRevision())}, {"PlanValidity", Text(setup->GetLastSuccessfulPlanRef().empty() ? "Missing" : "Stale")}});
            tx.ModifyComponent(setup->GetJobID(), CFiveAxisJobComponent::S_ClassName, {{"InputRevision", Next(job->GetInputRevision())}});
        }
        template<class T> Ref Publish(Scene& scene, std::shared_ptr<T> value, const std::string& type, const std::string& name,
            const std::vector<Ref>& dependencies = {})
        {
            auto& resources = scene.Resources();
            const auto url = resources.AllocateResourceURL();
            iCAX::Resource::CResourceInfo info;
            info.Name = name; info.ResourceTypeID = type; info.nVersion = 1;
            info.nSchemaVersion = type == "geometry.brep" ? 2 : 1;
            info.nMinimumReaderVersion = info.nSchemaVersion;
            info.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
            info.Dependencies = dependencies;
            resources.Set<T>(url, std::move(value), info);
            return {url, resources.GetVersion(url)};
        }
        #include "FiveAxisHeadInstanceData.inc"
        Map HeadCollisionState(Scene&,const Map&);
        Map BuiltinHeadCollisionMetadata(Scene&,const Text&);
        Map BoundBuiltinHeadAssembly(Scene&,const Map&);
        Map BoundHeadSnapshot(Scene&,const Map&);
        Map EffectiveHeadDefinition(Scene& scene,const CFiveAxisSetupComponent& setup) {
            auto head=HeadDefinition(scene,setup);
            const auto configuration=HeadMachineConfiguration(scene,setup.GetMachineSetup());
            const auto instance=ProjectHeadInstance(scene,setup);
            if(configuration.empty()&&!instance.empty()&&!instance.at("binding").To<Map>().empty()){
                auto binding=instance.at("binding").To<Map>();
                if(scene.Database().GetEntity(Identifier(binding,"machineId"))){binding["tcp"]=head.at("tcpLocalPosition");binding["beam"]=head.at("beamLocalDirection");head=BoundHeadSnapshot(scene,binding);}
            }
            if(!configuration.empty()){
                const auto tool=scene.Database().GetEntity(Identifier(configuration,"toolId"));
                const auto collision=Component<iCAX::CAM::CMachineCollisionComponent>(tool);
                head["collisionBoxes"]=collision?collision->GetCollisions():Array{};
                head["collisionSource"]=Text("MachineInstance");
                head["collisionMachineId"]=configuration.at("machineId");
                head["collisionToolId"]=configuration.at("toolId");
                head["tcpLocalPosition"]=configuration.at("tcp");head["beamLocalDirection"]=configuration.at("beam");head["calibrated"]=true;
                const auto element=Component<iCAX::CAM::CMachineElementComponent>(tool);
                head["machineHeadName"]=element?element->GetName():Text("机床切割头");
                const auto render=Component<iCAX::RenderInteraction::CRenderInstanceComponent>(tool);
                if(render&&!render->GetGeometryResourceID().empty())head["machinePreviewRef"]=StoredRef({render->GetGeometryResourceID(),scene.Resources().GetVersion(render->GetGeometryResourceID())});
                head.erase("assembly");const auto assembly=BoundBuiltinHeadAssembly(scene,configuration);if(!assembly.empty())head["assembly"]=assembly;
            }else if(head.contains("builtinDefinitionId")){
                if(!head.contains("collisionBoxes"))for(const auto& [key,value]:BuiltinHeadCollisionMetadata(scene,String(head,"builtinDefinitionId")))head[key]=value;
                head["collisionSource"]=Text("MachineDefinition");
            }
            return head;
        }
        Map HeadState(Scene& scene,const CFiveAxisSetupComponent& setup) {
            auto head=EffectiveHeadDefinition(scene,setup);
            auto instance=ProjectHeadInstance(scene,setup);if(!instance.empty()){
                const auto model=instance.at("model").To<Map>();if(model.contains("assembly")){auto source=model.at("assembly").To<Map>();auto nodes=source.at("nodes").To<Array>();for(auto& v:nodes){auto n=v.To<Map>();for(const auto* key:{"previewRef","geometryRef","collisionBoxes"})n.erase(key);v=n;}source["nodes"]=nodes;head["sourceAssembly"]=source;}
                instance.erase("model");head["instance"]=instance;}
            head["collisionModel"]=HeadCollisionState(scene,head);
            for(const auto* name:{"sourceRef","geometryRef","previewRef","machinePreviewRef"})
                if(head.contains(name))head[name]=WireRef(head.at(name).To<Map>());
            if(head.contains("assembly")){auto assembly=head.at("assembly").To<Map>();auto nodes=assembly.at("nodes").To<Array>();for(auto& value:nodes){auto node=value.To<Map>();for(const auto* name:{"geometryRef","previewRef"})if(node.contains(name))node[name]=WireRef(node.at(name).To<Map>());value=node;}assembly["nodes"]=nodes;head["assembly"]=assembly;}
            return head;
        }
        ID ActivePlacement(Scene& scene,const CFiveAxisJobComponent& job) {
            for(const auto& id:{job.GetActivePlacementID(),job.GetDefaultPlacementID()})
                if(Component<CFiveAxisPlacementComponent>(scene.Database().GetEntity(id)))return id;
            for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))
                if(Component<CFiveAxisPlacementComponent>(e))return e->GetID();
            return {};
        }
        ID ActiveSetup(Scene& scene,const CFiveAxisJobComponent& job) {
            const auto p=Component<CFiveAxisPlacementComponent>(scene.Database().GetEntity(ActivePlacement(scene,job)));
            return p?p->GetSetupID():ID();
        }
        UInt NextSetupSequence(Scene& scene) {
            UInt last=0;
            for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))
                if(const auto s=Component<CFiveAxisSetupComponent>(e))last=std::max(last,s->GetSequence());
            return Next(last);
        }
        #include "FiveAxisHeadImport.inc"
        Map PublishImport(Scene& scene, const ID& requestedSetupID, ImportResult&& prepared, const Text& kind)
        {
            const bool fixture=(kind=="FixtureImport"||kind=="FixtureLibraryImport"), head=kind=="HeadImport";
            if(head&&prepared.HeadDescription)return PublishImportedHeadAssembly(scene,requestedSetupID,prepared);
            auto& candidate = prepared.Import;
            auto initialTransform = Identity();
            if (!fixture && !head) {
                const auto& bounds = candidate.Bounds;
                if (bounds.Empty) throw std::runtime_error("CAM_WORKPIECE_BOUNDS_MISSING");
                initialTransform[3] = -(bounds.Min.X / 2. + bounds.Max.X / 2.);
                initialTransform[7] = -(bounds.Min.Y / 2. + bounds.Max.Y / 2.);
                initialTransform[11] = -(bounds.Min.Z / 2. + bounds.Max.Z / 2.);
            }
            auto& db = scene.Database();
            const auto jobID = db.GetMetaEntity()->GetID();
            const auto job = Require<CFiveAxisJobComponent>(db, jobID);
            const ID setupID = fixture||head?requestedSetupID:ID();
            UInt listOrder=0;
            for(const auto& e:db.FilterEntities([](auto e){return e->HasComponent(CFiveAxisWorkpieceComponent::S_ClassName);}))
                listOrder=std::max(listOrder,Component<CFiveAxisWorkpieceComponent>(e)->GetListOrder());
            const auto source = std::make_shared<iCAX::Resource::CBinaryResource>();
            source->DisplayName = candidate.DisplayName;
            source->FileExtension = candidate.FormatId == "cad.step" ? ".step" : candidate.FormatId=="geometry.stl"?".stl":candidate.FormatId=="geometry.obj"?".obj":".iges";
            source->Content = std::move(candidate.SourceBytes);
            source->Metadata = {{"sha256", candidate.SourceSha256}, {"formatId", candidate.FormatId}};
            const auto sourceRef = Publish(scene, source, "resource.binary", candidate.DisplayName);
            const auto geometryRef = Publish(scene, std::make_shared<iCAX::GeometryData::BRepModel>(std::move(candidate.Geometry)),
                "geometry.brep", candidate.DisplayName, {sourceRef});
            const auto transientPreview = iCAX::RenderInteraction::EnsureFrontendBRepGeometryResource(scene.Resources(), geometryRef.URL, prepared.DisplayEdges);
            const auto preview = scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(transientPreview.URL, transientPreview.nVersion);
            if (!preview) throw std::runtime_error("CAM_PREVIEW_MISSING");
            // Publish a persistent display resource with its exact BRep dependency.
            auto previewInfo = *scene.Resources().GetInfo(transientPreview.URL, transientPreview.nVersion);
            previewInfo.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
            previewInfo.ResourceTypeID = iCAX::Resource::CFlatBufferResource::kResourceTypeName;
            previewInfo.Name = candidate.DisplayName;
            previewInfo.Key = {}; previewInfo.ResourceID = {};
            const Ref previewRef{scene.Resources().AllocateResourceURL(), 1};
            scene.Resources().Set(previewRef.URL, std::make_shared<iCAX::Resource::CFlatBufferResource>(*preview), previewInfo);
            if(kind=="FixtureLibraryImport")return PublishFixtureLibraryImport(scene,geometryRef,previewRef,candidate.DisplayName);
            const ID workpieceID = iCAX::Data::GenerateNewUUID(), placementID = iCAX::Data::GenerateNewUUID();
            Array units;
            for (const auto& unit : candidate.SourceLengthUnits) units.emplace_back(unit);
            Map metadata{{"formatId", candidate.FormatId}, {"sourceSha256", candidate.SourceSha256}, {"sourceLengthUnits", units},
                {"geometryLengthUnit", Text("mm")}, {"transferredRoots", static_cast<UInt>(candidate.TransferredRoots)},
                {"validBRep", candidate.IsValidBRep}};
            if(head) {
                const auto setup=Require<CFiveAxisSetupComponent>(db,setupID);
                const auto job=Require<CFiveAxisJobComponent>(db,setup->GetJobID());
                Map definition{{"kind",Text("Imported")},{"motionType",Text("Rigid")},{"rotaryAxisCount",0},{"name",candidate.DisplayName},{"calibrated",false},
                    {"sourceRef",StoredRef(sourceRef)},{"geometryRef",StoredRef(geometryRef)},{"previewRef",StoredRef(previewRef)},
                    {"importMetadata",metadata},{"tcpLocalPosition",Array{0.,0.,0.}},{"beamLocalDirection",Array{0.,0.,-1.}}};
                Transaction(db,"导入切割头",[&](auto& tx){
                    AttachProjectHead(scene,tx,setupID,definition);
                });
                return {{"setupId",iCAX::Data::to_string(setupID)}};
            }
            const auto workpieceFileName=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(candidate.DisplayName.data()),candidate.DisplayName.size())).stem().u8string();
            const Text workpieceName(reinterpret_cast<const char*>(workpieceFileName.data()),workpieceFileName.size());
            const auto publish=[&](iCAX::Database::ITransaction& tx,const std::vector<Map>& receipts) {
                if (fixture) {
                    tx.CreateEntity(workpieceID);tx.AttachComponent(workpieceID,CFiveAxisFixtureUnitComponent::S_ClassName,{{"SetupID",setupID},{"Name",candidate.DisplayName},{"Kind",Text("Imported")},{"Transform",Identity()}});
                    tx.CreateEntity(placementID);
                    tx.AttachComponent(placementID, CFiveAxisFixtureComponent::S_ClassName,
                        {{"SetupID", setupID}, {"UnitID",workpieceID}, {"Name", candidate.DisplayName}, {"SourceRef", StoredRef(sourceRef)},
                         {"GeometryRef", StoredRef(geometryRef)}, {"PreviewRef", StoredRef(previewRef)},
                         {"ImportMetadata", metadata}, {"Transform", Identity()}});
                } else {
                tx.CreateEntity(workpieceID);
                tx.AttachComponent(workpieceID, CFiveAxisWorkpieceComponent::S_ClassName,
                    {{"Name", workpieceName}, {"ListOrder", Next(listOrder)}, {"SourceRef", StoredRef(sourceRef)}, {"GeometryRef", StoredRef(geometryRef)},
                     {"PreviewRef", StoredRef(previewRef)}, {"ImportMetadata", metadata}});
                tx.CreateEntity(placementID);
                tx.AttachComponent(placementID, CFiveAxisPlacementComponent::S_ClassName,
                    {{"SetupID", setupID}, {"WorkpieceID", workpieceID}, {"Transform", initialTransform}});
                }
                tx.AttachComponent(placementID, iCAX::Transform::CTransformComponent::S_ClassName,
                    {{"PositionX", initialTransform[3]}, {"PositionY", initialTransform[7]}, {"PositionZ", initialTransform[11]}});
                tx.AttachComponent(placementID, iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
                    {{"GeometryResourceID", previewRef.URL}, {"GeometryResourceVersion", static_cast<UInt>(previewRef.nVersion)}});
                if (!fixture) tx.ModifyComponent(jobID,CFiveAxisJobComponent::S_ClassName,{{"DefaultSetupID",ID()},{"ActiveSetupID",ID()},
                    {"DefaultPlacementID",placementID},{"ActivePlacementID",placementID},{"InputRevision",Next(job->GetInputRevision())}});
                else Invalidate(tx, db, setupID);
            };
            Transaction(db,fixture?"导入工装":"导入加工工件",[&](auto& tx){publish(tx,{});});
            if (fixture) return {{"unitId",iCAX::Data::to_string(workpieceID)},{"fixtureId", iCAX::Data::to_string(placementID)}, {"setupId", iCAX::Data::to_string(setupID)}};
            return {{"workpieceId", iCAX::Data::to_string(workpieceID)}, {"placementId", iCAX::Data::to_string(placementID)},
                    {"setupId", OptionalID(setupID)}};
        }
        std::array<double, 16> RigidMatrix(const Array& values)
        {
            if (values.size() != 16) throw std::invalid_argument("CAM_PLACEMENT_MATRIX: expected 16 numbers");
            std::array<double, 16> m{};
            for (size_t i = 0; i < m.size(); ++i)
            {
                m[i] = FiniteNumber(values[i], "CAM_PLACEMENT_MATRIX: expected finite numbers");
            }
            constexpr double eps = 1e-8;
            if (std::abs(m[12])+std::abs(m[13])+std::abs(m[14])+std::abs(m[15]-1) > eps)
                throw std::invalid_argument("CAM_PLACEMENT_MATRIX: expected affine transform");
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
            {
                double dot = 0;
                for (int k = 0; k < 3; ++k) dot += m[k*4+i]*m[k*4+j];
                if (std::abs(dot - (i == j ? 1. : 0.)) > eps)
                    throw std::invalid_argument("CAM_PLACEMENT_MATRIX: scale and shear are forbidden");
            }
            const double det = m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
            if (std::abs(det-1) > eps) throw std::invalid_argument("CAM_PLACEMENT_MATRIX: reflection is forbidden");
            return m;
        }
        void ProjectPlacement(Scene& scene, const ID& id, const Array& transform)
        {
            auto m = RigidMatrix(transform);
            auto& db = scene.Database();
            const auto entity = db.GetEntity(id);
            ID setupID, parentID;
            if (const auto placement = Component<CFiveAxisPlacementComponent>(entity)) setupID = placement->GetSetupID();
            else if (const auto fixture = Component<CFiveAxisFixtureComponent>(entity)) setupID = fixture->GetSetupID();
            if (const auto setup = Component<CFiveAxisSetupComponent>(db.GetEntity(setupID)); setup && !setup->GetMachineSetup().empty())
            {
                const auto& configuration = setup->GetMachineSetup();
                const auto supportID = Identifier(configuration, "supportId");
                const auto support = db.GetEntity(supportID);
                // The domain transform remains in setup coordinates. Project it
                // into the machine's support frame so native Transform motion,
                // undo/redo and reopened scenes all use the same parent chain.
                if (Component<iCAX::Transform::CTransformComponent>(support))
                {
                    parentID = supportID;
                    m = iCAX::CAM::Motion::Multiply(RigidMatrix(configuration.at("placement").To<Array>()), m);
                }
            }
            std::string parentError;
            if (!iCAX::Transform::SetTransformParent(db, id, parentID, parentError)) throw std::runtime_error(parentError);
            const double pitch = std::asin(std::clamp(-m[8], -1., 1.));
            const bool gimbal = std::abs(std::cos(pitch)) < 1e-8;
            const double yaw = gimbal ? std::atan2(-m[1], m[5]) : std::atan2(m[4],m[0]);
            const double roll = gimbal ? 0. : std::atan2(m[9], m[10]);
            auto component = Require<iCAX::Transform::CTransformComponent>(db, id);
            const iCAX::Data::PropertySet properties{{"PositionX", m[3]}, {"PositionY", m[7]}, {"PositionZ", m[11]},
                {"YawRadians", yaw}, {"PitchRadians", pitch}, {"RollRadians", roll}};
            for (const auto& [name, value] : properties)
            {
                if (component->GetProperty(name) == value) continue;
                std::string error;
                if (!component->SetProperty(name, value, error)) throw std::runtime_error(error);
            }
        }
    }

    namespace {
        void ProjectSetupPlacements(Scene& scene,const ID& setupID){
            for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){
                if(const auto p=Component<CFiveAxisPlacementComponent>(entity);p&&p->GetSetupID()==setupID)ProjectPlacement(scene,entity->GetID(),p->GetTransform());
                if(const auto f=Component<CFiveAxisFixtureComponent>(entity);f&&f->GetSetupID()==setupID)ProjectPlacement(scene,entity->GetID(),f->GetTransform());
            }
        }
    }
    void InitializeScene(Scene& scene)
    {
        auto& db = scene.Database();
        auto root = db.GetMetaEntity();
        if (!root) throw std::runtime_error("CAM_SCENE_META_MISSING");
        if (!Component<CFiveAxisJobComponent>(root))
        {
            Transaction(db, "初始化五轴作业", [&](auto& tx) {
                if (!Component<CFiveAxisSceneComponent>(root)) tx.AttachComponent(root->GetID(), CFiveAxisSceneComponent::S_ClassName);
                tx.AttachComponent(root->GetID(), CFiveAxisJobComponent::S_ClassName, {});
            }, false);
        }
        (void)Runtime(scene);
        InitializeIntentDocument(scene);
        PumpTasks(scene);
    }
    void PumpTaskProgressPDO(Scene& scene){const auto runtime=Runtime(scene);PumpTaskProgressPDOImpl(scene,*runtime);}
    void ReleaseTaskProgressPDO(Scene& scene,const std::shared_ptr<SceneRuntime>& runtime){if(runtime)ReleaseTaskProgressPDOImpl(scene,*runtime);}
    void ReleaseIntentToolpaths(Scene&,const std::shared_ptr<SceneRuntime>& runtime)
    {
        if(!runtime)return;
        for(auto& [_,task]:runtime->Tasks)
            if(task.Kind=="Generate"||task.Kind=="ProcessCompile"||task.Kind=="IntentEdit"||task.Kind=="IntentRefresh")
            {task.Cancelled->store(true);if(task.Status=="Running")task.Status="Cancelled";}
        for(auto& [_,generation]:runtime->IntentGenerations)generation->Cancel();
        runtime->IntentGenerations.clear();
        for(const auto& [_,id]:runtime->IntentGenerationTasks)if(runtime->Tasks.contains(id)&&runtime->Tasks.at(id).Status=="Running")runtime->Tasks.at(id).Status="Cancelled";
        runtime->IntentGenerationTasks.clear();
        runtime->IntentDocument.reset();runtime->IntentRegistry.reset();runtime->IntentGeometry.reset();
        runtime->IntentContexts.clear();runtime->IntentModels.clear();
        runtime->OrderStructures.clear();runtime->IntentInspections.clear();runtime->IntentResolutions.clear();runtime->IntentPortPaths.clear();runtime->IntentDefinitions.clear();runtime->PoseAuthoringSnapshots.clear();runtime->PosePointPositions.clear();
    }
    void PumpTasks(Scene& scene)
    {
        const auto runtime = Runtime(scene);
        PumpIntentGenerations(scene);
        for (auto& [_, task] : runtime->Tasks)
        {
            if (task.Status != "Running") continue;
            if(task.Kind=="IntentRefresh")continue;
            if(task.Kind=="Order")
            {
                if(task.Sequence!=runtime->Sequence)task.Cancelled->store(true);
                if(task.OrderWorker.IsCompleted())FinishOrdering(scene,*runtime,task);
                continue;
            }
            if(task.Kind=="Extract")
            {
                if(task.RecognitionWorker.IsCompleted())FinishExtraction(scene,*runtime,task);
                continue;
            }
            if(task.Kind=="ProcessCompile")
            {
                if(!task.CompilationInput){PumpIntentCompilation(scene,*runtime,task);continue;}
                if(task.CompilationWorker.IsCompleted())FinishCompilation(scene,*runtime,task);
                continue;
            }
            if(task.Kind=="Generate")
            {
                if(!task.GenerationInput){PumpIntentExecution(scene,*runtime,task);continue;}
                if(task.GenerationWorker.IsCompleted())FinishGeneration(scene,*runtime,task);
                continue;
            }
            if(task.Kind=="IntentEdit")
            {PumpIntentEdit(scene,*runtime,task);continue;}
            if(task.Kind=="RapidPreview"||task.Kind=="RapidGenerate"||task.Kind=="RapidCheck")
            {
                if(task.RapidWorker.IsCompleted())FinishRapids(scene,*runtime,task);
                continue;
            }
            if(task.Kind=="FixtureCheck")
            {
                if(task.ClearanceWorker.IsCompleted())FinishFixtureCheck(scene,*runtime,task);
                continue;
            }
            if(task.Kind=="FixtureSupportFaces") {
                if(task.SupportFaceWorker.IsCompleted())FinishFixtureSupportFaces(scene,*runtime,task);
                continue;
            }
            if(task.Kind=="CutRetainedFaces") {
                if(task.RetainedFaceWorker.IsCompleted())FinishCutRetainedFaces(scene,*runtime,task);
                continue;
            }
            if(task.Kind=="FixtureGenerate") {
                if(task.PlateWorker.IsCompleted())FinishFixtureGeneration(scene,*runtime,task);
                continue;
            }
            if(task.Kind=="SimulationCollision")
            {
                if(task.SimulationWorker.IsCompleted())FinishSimulationCollision(scene,*runtime,task);
                continue;
            }
            if(task.Kind=="AutoRepairCollision") {
                if(task.RepairCandidate||task.RepairWorker.IsCompleted())FinishCollisionRepair(scene,*runtime,task);
                continue;
            }
            if(task.Kind=="MachinePlan") {
                if(task.MachinePlanWorker.IsCompleted())FinishMachinePlan(scene,*runtime,task);
                continue;
            }
            if(task.Kind=="PlaybackCollision")
            {
                if(task.Sequence!=runtime->Sequence)task.Cancelled->store(true);
                if(task.SimulationWorker.IsCompleted())FinishPlaybackCollision(scene,*runtime,task);
                continue;
            }
            if(!task.Worker.IsCompleted())continue;
            try
            {
                auto candidate = task.Worker.Result(); // Only completed tasks; never blocks Scene.
                if (task.Cancelled->load()) task.Status = "Cancelled";
                else if (task.Sequence != runtime->Sequence) task.Status = "Superseded";
                else
                {
                    task.Result = task.Kind=="Scale" ? PublishScale(scene,task,std::move(*candidate))
                        : PublishImport(scene, task.SetupID, std::move(*candidate), task.Kind);
                    task.Status = "Completed";
                }
            }
            catch (const iCAX::Tasks::TaskCanceledException&) { task.Status = "Cancelled"; }
            catch (const std::exception& error) {
                // CAD cancellation can surface as a standard exception before
                // Result() returns. Preserve the requested terminal state.
                if(task.Cancelled->load()){task.Status="Cancelled";task.Error.clear();}
                else{task.Status="Failed";task.Error=error.what();}
            }
            task.Worker = {}; // release neutral geometry and source bytes after terminal receipt
        }
        for (const auto& entity : scene.Database().FilterEntities([](auto e) { return e->HasComponent(CFiveAxisPlacementComponent::S_ClassName); }))
            ProjectPlacement(scene, entity->GetID(), Component<CFiveAxisPlacementComponent>(entity)->GetTransform());
        for (const auto& entity : scene.Database().FilterEntities([](auto e) { return e->HasComponent(CFiveAxisFixtureComponent::S_ClassName); })) {
            const auto fixture=Component<CFiveAxisFixtureComponent>(entity);
            ProjectPlacement(scene,entity->GetID(),fixture->GetTransform());
            auto render=Require<iCAX::RenderInteraction::CRenderInstanceComponent>(scene.Database(),entity->GetID());
            const bool visible=fixture->GetVisible()&&fixture->GetEnabled();
            if(render->GetVisible()!=visible && !render->SetVisible(visible))
                throw std::runtime_error("CAM_FIXTURE_VISIBILITY_FAILED");
        }
    }
    static Map ImportModel(Scene& scene, const Map& request, const Text& kind)
    {
        const auto runtime = Runtime(scene);
        const auto commandID = iCAX::Data::to_string(Identifier(request, "commandId"));
        const auto payload = kind+":" + iCAX::Data::VariantSerializer::Serialize(request);
        for (const auto& [_, task] : runtime->Tasks) if (task.CommandID == commandID)
        {
            if (task.Payload != payload) throw std::invalid_argument("CAM_COMMAND_REUSED: commandId has a different payload");
            return TaskState(scene,*runtime, task);
        }
        if (runtime->Receipts.contains(commandID)) throw std::invalid_argument("CAM_COMMAND_REUSED");
        CheckExpected(*runtime, request);
        CheckCapacity(*runtime);
        // Rapid previews own immutable snapshots and never publish scene state.
        // They must not block import while a cancelled calculation winds down.
        for (const auto& [_, task] : runtime->Tasks) if (task.Status == "Running" && task.Kind != "RapidPreview") throw std::runtime_error("CAM_IMPORT_BUSY");
        const auto setupID = OptionalIdentifier(request, "setupId");
        if(kind=="FixtureImport"||kind=="HeadImport")RequireSetupWorkpieceEnabled(scene,setupID);
        auto service = scene.Services().Resolve<iCAX::CAD::ICadBRepImportService>();
        if (!service) throw std::runtime_error("CAM_IMPORT_SERVICE_UNAVAILABLE");
        ImportTask task;
        task.Kind=kind;
        task.CommandID = commandID; task.Payload = payload; task.SetupID = setupID; task.Sequence = runtime->Sequence;
        iCAX::CAD::SCadBRepImportRequest input;
        const auto sourcePath = String(request, "sourcePath");
        input.SourcePath = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(sourcePath.data()), sourcePath.size()));
        input.IsCancelled = [cancelled = task.Cancelled] { return cancelled->load(); };
        const auto displayService = scene.Services().Resolve<iCAX::GeometryServices::IBRepDisplayService>();
        if (!displayService) throw std::runtime_error("CAM_DISPLAY_SERVICE_UNAVAILABLE");
        task.Worker = iCAX::Tasks::Run([service, displayService, input, kind] {
            const auto text=input.SourcePath.u8string();const Text path(text.begin(),text.end());
            if(kind=="HeadImport"&&iCAX::CAM::IsSupportedMachineDescriptionPath(path))return PrepareImportedHeadAssembly(service,input);
            auto result = std::make_shared<ImportResult>(service->Load(input));
            if(kind!="HeadImport"&&!result->Import.IsValidBRep)throw std::invalid_argument("CAM_IMPORT_BREP_REQUIRED: 工件和夹具必须具有 CAD 曲面或实体拓扑。");
            iCAX::GeometryServices::BRepDisplayOptions options;
            options.IsCancelled = input.IsCancelled;
            result->DisplayEdges = displayService->BuildEdges(result->Import.Geometry, options);
            return result;
        });
        const auto id = task.TaskID;
        runtime->Tasks.emplace(id, std::move(task));
        return TaskState(scene,*runtime, runtime->Tasks.at(id));
    }
    Map ImportWorkpiece(Scene& scene,const Map& request) { return ImportModel(scene,request,"Import"); }
    Map ImportFixture(Scene& scene,const Map& request) { return ImportModel(scene,request,"FixtureImport"); }
    Map ImportFixtureLibrary(Scene& scene,const Map& request) { return ImportModel(scene,request,"FixtureLibraryImport"); }
    Map ImportHead(Scene& scene,const Map& request) { return ImportModel(scene,request,"HeadImport"); }
    Map QueryTask(Scene& scene, const Map& request)
    {
        PumpTasks(scene);
        const auto runtime = Runtime(scene);
        if (String(request, "epoch") != runtime->Epoch) throw std::invalid_argument("CAM_EPOCH_CHANGED");
        const auto it = runtime->Tasks.find(String(request, "taskId"));
        if (it == runtime->Tasks.end()) throw std::invalid_argument("CAM_TASK_NOT_FOUND");
        auto result=TaskState(scene,*runtime,it->second);
        if(it->second.Status!="Running")RetireTaskProgressPDO(scene,*runtime,it->second);
        return result;
    }
    Map CancelTask(Scene& scene, const Map& request)
    {
        const auto runtime = Runtime(scene);
        if (String(request, "epoch") != runtime->Epoch) throw std::invalid_argument("CAM_EPOCH_CHANGED");
        const auto it = runtime->Tasks.find(String(request, "taskId"));
        if (it == runtime->Tasks.end()) throw std::invalid_argument("CAM_TASK_NOT_FOUND");
        if (it->second.Status == "Running") it->second.Cancelled->store(true);
        else if(it->second.FixtureDraft&&it->second.ConsumedByCommandID.empty()){
            it->second.Cancelled->store(true);it->second.Status="Cancelled";it->second.Result.clear();
            for(auto ref=it->second.FixtureDraftResources.rbegin();ref!=it->second.FixtureDraftResources.rend();++ref)
                scene.Resources().DiscardRuntimeResource(ref->first.URL);
            it->second.FixtureDraftResources.clear();
            it->second.FixtureModel.reset();
        }
        if(it->second.Kind=="IntentRefresh")
        {
            const auto plan=IntentPlan(scene,it->second.SetupID);
            if(runtime->IntentGenerationTasks.contains(plan)&&runtime->IntentGenerationTasks.at(plan)==it->second.TaskID)
                if(const auto generation=runtime->IntentGenerations.find(plan);generation!=runtime->IntentGenerations.end())generation->second->Cancel();
        }
        if(request.contains("releaseProgress")&&request.at("releaseProgress").To<bool>())AbandonTaskProgressPDO(scene,*runtime,it->second);
        return TaskState(scene,*runtime, it->second);
    }
    Map SetPlacement(Scene& scene, const Map& request)
    {
        const auto runtime = Runtime(scene);
        const auto commandID = iCAX::Data::to_string(Identifier(request, "commandId"));
        const auto payload = Text("SetTransform:") + iCAX::Data::VariantSerializer::Serialize(request);
        if (const auto found = runtime->Receipts.find(commandID); found != runtime->Receipts.end())
        {
            if (found->second.first != payload) throw std::invalid_argument("CAM_COMMAND_REUSED");
            return found->second.second;
        }
        for (const auto& [_, task] : runtime->Tasks) if (task.CommandID == commandID) throw std::invalid_argument("CAM_COMMAND_REUSED");
        CheckExpected(*runtime, request);
        CheckCapacity(*runtime);
        const auto placementID = Identifier(request, "placementId");
        const auto placement = Require<CFiveAxisPlacementComponent>(scene.Database(), placementID);
        RequireSetupUnlocked(scene,placement->GetSetupID());
        const auto setupID = Identifier(request, "setupId");
        if (placement->GetSetupID() != setupID) throw std::invalid_argument("CAM_SETUP_MISMATCH");
        const auto matrix = RigidMatrix(request.at("transform").To<Array>());
        Array transform;
        for (double v : matrix) transform.emplace_back(v == 0. ? 0. : v);
        Transaction(scene.Database(), "工件定位", [&](auto& tx) {
            tx.ModifyComponent(placementID, CFiveAxisPlacementComponent::S_ClassName,
                {{"Transform", transform}, {"RegistrationData", Map()}, {"RegistrationMethod", Text()},
                 {"ApplicationMode", Text("Pending")}, {"InputRevision", Next(placement->GetInputRevision())}});
            Invalidate(tx, scene.Database(), setupID);
        });
        ProjectPlacement(scene, placementID, transform);
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
        const auto job=Require<CFiveAxisJobComponent>(scene.Database(),setup->GetJobID());
        // Placement authoring only persists its rigid transform. Return the
        // committed revision without reading or evaluating any machining graph.
        Map patch{{"expected",Map{{"epoch",runtime->Epoch},{"mutationSequence",std::to_string(runtime->Sequence)}}},
            {"inputRevision",std::to_string(job->GetInputRevision())},
            {"placement",Map{{"id",iCAX::Data::to_string(placementID)},{"transform",transform},
                {"registrationRef",Map()},{"registrationSummary",Map()},{"registrationMethod",Text()},{"applicationMode",Text("Pending")},{"inputRevision",std::to_string(placement->GetInputRevision())}}},
            {"setup",Map{{"id",iCAX::Data::to_string(setupID)},{"inputRevision",std::to_string(setup->GetInputRevision())},{"planValidity",setup->GetPlanValidity()}}}};
        Map result{{"placementId", iCAX::Data::to_string(placementID)}, {"inputRevision", std::to_string(placement->GetInputRevision())},{"statePatch",patch}};
        runtime->Receipts.emplace(commandID, std::make_pair(payload, result));
        return result;
    }
    namespace {double RegistrationNumber(const Variant&);iCAX::Geometry::RegistrationPoint RegistrationPoint(const Variant&);Ref ReadWireRef(const Map&);}
    #include "FiveAxisIntentRuntime.inc"
    #include "FiveAxisIntentAccess.inc"
    #include "FiveAxisFilePreview.inc"
    #include "FiveAxisScaling.inc"
    #include "FiveAxisRegistration.inc"
    #include "FiveAxisFixtures.inc"
    #include "FiveAxisFixtureUnits.inc"
    #include "FiveAxisFixtureLibrary.inc"
    #include "FiveAxisFixtureDrawing.inc"
    #include "FiveAxisFixtureDesign.inc"
    #include "FiveAxisFixtureParts.inc"
    #include "FiveAxisFixtureQueries.inc"
    #include "FiveAxisRecognition.inc"
    #include "FiveAxisPolicies.inc"
    #include "FiveAxisIntentPreview.inc"
    #include "FiveAxisCutCreate.inc"
    #include "FiveAxisCutTreeState.inc"
    #include "FiveAxisCutEditing.inc"
    #include "FiveAxisStandardHoles.inc"
    #include "FiveAxisCutSplitting.inc"
    #include "FiveAxisLeads.inc"
    #include "FiveAxisIntentBranchEditing.inc"
    #include "FiveAxisPathEditing.inc"
    #include "FiveAxisProcessEditing.inc"
    namespace {Text OrderStrategy(const Map&);}
    #include "FiveAxisOrderOptions.inc"
    #include "FiveAxisPlanGeneration.inc"
    #include "FiveAxisOrderingTask.inc"
    #include "FiveAxisPathPreviewTasks.inc"
    #include "FiveAxisProcessCompilation.inc"
    #include "FiveAxisToolAxis.inc"
    #include "FiveAxisToolAxisPoints.inc"
    #include "FiveAxisToolAxisSamples.inc"
    #include "FiveAxisCutTree.inc"
    #include "FiveAxisCollisionModels.inc"
    #include "FiveAxisRapidPlanning.inc"
    #include "FiveAxisFixtureCutClearance.inc"
    #include "FiveAxisSimulationCollision.inc"
    #include "FiveAxisCollisionRepair.inc"
    #include "FiveAxisMachineSimulation.inc"
    #include "FiveAxisHeadCollisionCheck.inc"
    #include "FiveAxisBuiltinHeads.inc"
    #include "FiveAxisHeadInstances.inc"
    #include "FiveAxisHeadConfigurationLibrary.inc"

    #include "FiveAxisWorkpieceRemoval.inc"
    #include "FiveAxisSetups.inc"
    #include "FiveAxisMachineProgram.inc"
    #include "FiveAxisPreparation.inc"
    #include "FiveAxisIntentScene.inc"
    Map ActivateWorkpiece(Scene& scene,const Map& request) {
        CutFields(request,{"commandId","expected","placementId"});
        const auto runtime=Runtime(scene);
        const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
        const auto payload=Text("ActivateWorkpiece:")+iCAX::Data::VariantSerializer::Serialize(request);
        if(const auto prior=runtime->Receipts.find(commandID);prior!=runtime->Receipts.end()) {
            if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");
            return prior->second.second;
        }
        for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==commandID)throw std::invalid_argument("CAM_COMMAND_REUSED");
        CheckExpected(*runtime,request);CheckCapacity(*runtime);
        for(const auto& [_,task]:runtime->Tasks) if(task.Status=="Running"&&task.Kind!="RapidPreview")
            throw std::runtime_error("CAM_WORKPIECE_BUSY");
        const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(request,"placementId"));
        const auto jobID=scene.Database().GetMetaEntity()->GetID();
        const auto job=Require<CFiveAxisJobComponent>(scene.Database(),jobID);
        const bool changed=ActivePlacement(scene,*job)!=Identifier(request,"placementId");
        if(changed) Transaction(scene.Database(),"切换加工工件",[&](auto& tx){
            tx.ModifyComponent(jobID,CFiveAxisJobComponent::S_ClassName,{{"ActiveSetupID",placement->GetSetupID()},{"ActivePlacementID",Identifier(request,"placementId")}});
        },false);
        Map result{{"changed",changed},{"placementId",request.at("placementId")},{"setupId",iCAX::Data::to_string(placement->GetSetupID())}};
        runtime->Receipts.emplace(commandID,std::make_pair(payload,result));
        return result;
    }

    Map ConfigureHead(Scene& scene,const Map& request) {
        CutFields(request,{"commandId","expected","setupId","kind","builtinDefinitionId","tcpLocalPosition","beamLocalDirection"});
        return RapidMutation(scene,request,"ConfigureHead:",[&](const ID& id,const auto& setup){
            RequireSetupUnlocked(scene,id);if(!setup->GetMachineSetup().empty())throw std::invalid_argument("CAM_HEAD_MACHINE_BOUND");
            const auto kind=String(request,"kind");auto instance=ProjectHeadInstance(scene,*setup);
            if(kind=="Builtin"){
                auto head=request.contains("builtinDefinitionId")?BuiltinHeadDefinition(scene,String(request,"builtinDefinitionId")):Map{{"kind",Text("Builtin")},{"motionType",Text("Rigid")},{"rotaryAxisCount",UInt(0)},{"name",Text("内置示意头")},{"calibrated",true},{"tcpLocalPosition",Array{0.,0.,0.}},{"beamLocalDirection",Array{0.,0.,-1.}}};
                if(request.contains("builtinDefinitionId")){
                    const auto source=BuiltinHeadSource(scene,String(request,"builtinDefinitionId"));if(!source.contains("headRoot"))throw std::invalid_argument("CAM_HEAD_BUILTIN_REQUIRED: 请选择切割头，不能选择整机。");
                    auto kind=String(source,"kind");if(kind=="4+1")kind="A";if(kind=="3+2")kind="Fixed";
                    head["name"]=source.contains("standaloneHead")?source.at("name"):Variant(kind=="Fixed"?Text("固定切割头"):kind+" 切割头");
                    if(head.contains("assembly")){auto assembly=head.at("assembly").To<Map>();assembly["workpieceAxes"]=Array{};assembly["kind"]=kind;head["assembly"]=assembly;}
                    head["requiresMachineSimulation"]=false;
                }
                ID created;Transaction(scene.Database(),"添加工程刀头",[&](auto& tx){created=AttachProjectHead(scene,tx,id,head);});return Map{{"changed",true},{"instanceId",iCAX::Data::to_string(created)}};
            }
            if(kind!="Imported"||instance.empty()||String(instance.at("model").To<Map>(),"kind")!="Imported")throw std::invalid_argument("CAM_HEAD_MODEL_REQUIRED");
            auto parameters=instance.at("parameters").To<Map>();parameters["tcpMode"]=Text("Coordinates");parameters["tcp"]=request.at("tcpLocalPosition");parameters["beam"]=request.at("beamLocalDirection");
            parameters=NormalizeHeadParameters(instance.at("model").To<Map>(),parameters);const bool changed=instance.at("parameters")!=parameters||!instance.contains("calibrationConfirmed")||!instance.at("calibrationConfirmed").To<bool>();
            if(changed){instance["parameters"]=parameters;instance["calibrationConfirmed"]=true;WriteHeadInstance(scene,id,instance,true,"标定切割头");}return Map{{"changed",changed},{"instanceId",instance.at("id")}};
        });
    }

    Map IntentToolpathState(Scene& scene,const IntentCut& f)
    {
            const auto id=iCAX::Data::to_string(f->GetID());const auto& path=f->Path;
            const auto& feature=f->Projection.Metadata.Feature;const auto& pose=f->Projection.Metadata.Pose;
            const auto process=f->GetProcessData();
            const auto assignment=path.ResultState=="Current"?f->Projection.Metadata.Process:IntentAuthoredProcessForRecovery(scene,path,f->Projection.Metadata.Process);
            const bool deferredPose=path.ResultState!="Current"&&String(IntentOperationDefinition(scene,path.Port.OperationId),"typeId")=="fiveaxis.pose.delta";
            // Pose authoring leaves the contour unchanged. Reopening a pending
            // pose must retain that contour for further editing without making
            // its old interpolated directions an executable/current result.
            const auto display=deferredPose?ReadAxisAuthoringPath(scene,path.PathId):path;
            const auto preview=IntentPreviewPoints(scene,display);
            const auto first=IntentCutStartPoint(scene,display);
            const auto design=IntentDesignPath(scene,display);
            const auto start=feature.contains("start")?feature.at("start"):Variant(Map{});
            return Map{{"id",id},{"entityId",id},{"name",path.Name},{"number",path.UserNumber},
                {"enabled",CutEnabledForView(scene,*f)},{"ownEnabled",path.OwnParticipating},{"locked",path.Locked},{"ownLocked",path.OwnLocked},{"groupId",path.GroupId},{"layers",path.Layers},
                {"beforeCommands",f->GetBeforeCommands()},{"afterCommands",f->GetAfterCommands()},{"sourceState",path.ResultState},{"poseDeferred",deferredPose},
                {"setupId",iCAX::Data::to_string(f->GetSetupID())},{"placementId",iCAX::Data::to_string(f->GetPlacementID())},{"sourceKey",f->GetSourceKey()},
                {"curveRef",Map{{"revision",path.Fingerprint}}},{"sourceRecipeRef",EngineeringIdentity(f->GetSourceData())},{"featureRef",EngineeringIdentity(f->GetDefinitionData())},
                {"processRef",EngineeringIdentity(process)},{"poseRef",EngineeringIdentity(f->GetPoseData())},{"kerfWidthMm",RegistrationNumber(assignment.at("kerf").To<Map>().at("widthMm"))},
                {"hasTracePreview",String(assignment.at("leadIn").To<Map>(),"kind")!="None"||String(assignment.at("leadOut").To<Map>(),"kind")!="None"||RegistrationNumber(assignment.at("overcutLengthMm"))>0||RegistrationNumber(assignment.at("gapLengthMm"))>0},
                {"machiningKind",feature.at("machiningKind")},{"geometryKind",feature.at("geometryKind")},{"boundaryRole",feature.at("boundaryRole")},
                {"processEligibility",Text(path.ResultState=="Current"?"NeedsProcessValidation":"PendingGeneration")},{"previewPoints",preview},{"startPoint",first},
                {"split",!design.Value.Geometry.Segments.empty()&&!design.Value.Geometry.Closed},{"cutType",Text(String(pose,"mode")=="FixedDirection"?"StandardHole":"Contour")},
                {"canBeStandardHole",feature.contains("standardHole")},{"standardHole",IntentStandardHoleView(path)},{"reversed",IntentReversed(scene,path.Port)},{"retainedSide",feature.at("retainedSide")},{"start",start},
                {"intentPort",iCAX::CAM::Intent::ToData(path.Port)},{"inputRevision",std::to_string(f->GetInputRevision())}};
            }
    Map IntentToolpathState(Scene& scene,const ID& intentID)
    {return IntentToolpathState(scene,ReadIntentCut(scene,intentID));}
    Map BuildSceneState(Scene& scene,bool fixtureSummaries)
    {
        // A restored scene can receive its first SDO read before Awake.
        InitializeIntentDocument(scene);
        const auto runtime = Runtime(scene);
        const auto root = scene.Database().GetMetaEntity();
        const auto job = Require<CFiveAxisJobComponent>(scene.Database(), root->GetID());
        Array setups, placements,intents,fixtures,fixtureUnits,workpieces,headInstances;
        for(const auto& [key,value]:job->GetHeadInstances()){auto instance=value.To<Map>();instance.erase("model");Array used;for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))if(const auto s=Component<CFiveAxisSetupComponent>(e);s&&iCAX::Data::to_string(s->GetHeadInstanceID())==key)used.emplace_back(iCAX::Data::to_string(e->GetID()));instance["setupIds"]=used;headInstances.emplace_back(instance);}
        std::map<Text,UInt> workpieceOrder;
        for (const auto& e : scene.Database().FilterEntities([](auto) { return true; }))
        {
            const auto id = iCAX::Data::to_string(e->GetID());
            if (auto s = Component<CFiveAxisSetupComponent>(e)) {
                PrimeIntentDesignPaths(scene,e->GetID());
                const auto generated=GeneratedGeometryState(scene,e->GetID());
                setups.emplace_back(Map{{"id", id}, {"name", s->GetName()}, {"sequence",s->GetSequence()}, {"enabled",s->GetEnabled()}, {"locked",s->GetLocked()}, {"registrationPlan",RegistrationPlanState(scene,e->GetID())},
                {"inputRevision", std::to_string(s->GetInputRevision())}, {"planValidity", s->GetPlanValidity()},{"planningProfileRef",EngineeringIdentity(s->GetPlanningProfileData())},
                {"fixtureCheck",FixtureCheckState(scene,e->GetID(),s->GetFixtureCheck())},{"fixtureDesign",ProjectFixtureDesignState(scene,e->GetID(),s->GetFixtureDesign(),fixtureSummaries)},{"headInstanceId",OptionalID(s->GetHeadInstanceID())},{"headModel",HeadState(scene,*s)},{"machineSetup",HeadMachineConfiguration(scene,s->GetMachineSetup())},{"machineMotionPlanRef",EngineeringIdentity(s->GetMachineMotionPlanData())},{"rapidSettings",RapidSettings(scene,e->GetID())},{"cutGroups",IntentCutGroups(scene,e->GetID())},{"definitionOrder",CutTreeOrder(scene,e->GetID(),&generated)},{"generatedGeometry",generated},{"simulationCheck",SimulationCollisionState(scene,e->GetID())}});
            }
            if(auto u=Component<CFiveAxisFixtureUnitComponent>(e)) {
                Array members;for(const auto& part:scene.Database().FilterEntities([](auto){return true;}))if(const auto f=Component<CFiveAxisFixtureComponent>(part);f&&f->GetUnitID()==e->GetID())members.emplace_back(iCAX::Data::to_string(part->GetID()));
                fixtureUnits.emplace_back(Map{{"id",id},{"setupId",iCAX::Data::to_string(u->GetSetupID())},{"name",u->GetName()},{"kind",u->GetKind()},{"transform",u->GetTransform()},{"locked",u->GetLocked()},{"enabled",u->GetEnabled()},{"partIds",members},{"design",ProjectFixtureDesignState(scene,u->GetSetupID(),u->GetDesign(),fixtureSummaries)},{"inputRevision",std::to_string(u->GetInputRevision())}});
            }
            if (auto f = Component<CFiveAxisFixtureComponent>(e)) fixtures.emplace_back(Map{{"id",id},{"unitId",iCAX::Data::to_string(f->GetUnitID())},{"name",f->GetName()},
                {"setupId",iCAX::Data::to_string(f->GetSetupID())},{"sourceRef",WireRef(f->GetSourceRef())},
                {"geometryRef",WireRef(f->GetGeometryRef())},{"previewRef",WireRef(f->GetPreviewRef())},{"importMetadata",f->GetImportMetadata()},
                {"transform",f->GetTransform()},{"enabled",f->GetEnabled()},{"locked",f->GetLocked()},{"visible",f->GetVisible()},{"collisionEnabled",f->GetCollisionEnabled()},
                {"inputRevision",std::to_string(f->GetInputRevision())}});
            if (auto w = Component<CFiveAxisWorkpieceComponent>(e))
            {
                workpieceOrder[id]=w->GetListOrder();
                workpieces.emplace_back(Map{{"id", id}, {"name", w->GetName()},
                    {"locked",WorkpieceLocked(scene,e->GetID())},{"enabled",w->GetEnabled()},
                    {"sourceRef", WireRef(w->GetSourceRef())}, {"geometryRef", WireRef(w->GetGeometryRef())}, {"previewRef", WireRef(w->GetPreviewRef())},
                    {"importMetadata", w->GetImportMetadata()}, {"coordinateFrame",w->GetCoordinateFrame()}, {"inputRevision", std::to_string(w->GetInputRevision())}});
            }
            if (auto p = Component<CFiveAxisPlacementComponent>(e)) placements.emplace_back(Map{{"id", id},
                {"setupId", OptionalID(p->GetSetupID())}, {"workpieceId", iCAX::Data::to_string(p->GetWorkpieceID())},
                {"transform", p->GetTransform()}, {"registrationRef", EngineeringIdentity(p->GetRegistrationData())},
                {"registrationMethod", p->GetRegistrationMethod()}, {"applicationMode", p->GetApplicationMode()},
                {"registrationSummary", RegistrationSummary(scene,p->GetRegistrationData())},
                {"inputRevision", std::to_string(p->GetInputRevision())}});
        }
        for(const auto& f:IntentCuts(scene))intents.emplace_back(IntentToolpathState(scene,f));
        std::sort(setups.begin(),setups.end(),[](const Variant& a,const Variant& b){const auto x=a.To<Map>(),y=b.To<Map>();const auto sx=x.at("sequence").To<UInt>(),sy=y.at("sequence").To<UInt>();return sx==sy?String(x,"id")<String(y,"id"):sx<sy;});
        std::sort(workpieces.begin(),workpieces.end(),[&](const Variant& a,const Variant& b){return workpieceOrder.at(String(a.To<Map>(),"id"))<workpieceOrder.at(String(b.To<Map>(),"id"));});
        std::sort(placements.begin(),placements.end(),[&](const Variant& a,const Variant& b){return workpieceOrder.at(String(a.To<Map>(),"workpieceId"))<workpieceOrder.at(String(b.To<Map>(),"workpieceId"));});
        std::sort(intents.begin(),intents.end(),[](const Variant& a,const Variant& b){const auto x=a.To<Map>(),y=b.To<Map>();return String(x,"setupId")!=String(y,"setupId")?String(x,"setupId")<String(y,"setupId"):std::stoull(String(x,"number"))<std::stoull(String(y,"number"));});
        Array intentGenerationTasks,intentGenerationErrors;
        for(const auto& [_,taskId]:runtime->IntentGenerationTasks)if(runtime->Tasks.contains(taskId))
        {const auto& task=runtime->Tasks.at(taskId);if(task.Status=="Running")intentGenerationTasks.emplace_back(TaskState(scene,*runtime,task));
         else if(task.Status=="Failed")intentGenerationErrors.emplace_back(TaskState(scene,*runtime,task));}
        return {{"layers",IntentDocument(scene).QueryLayers()},{"intentGenerationTasks",intentGenerationTasks},{"intentGenerationErrors",intentGenerationErrors},{"expected", Map{{"epoch", runtime->Epoch}, {"mutationSequence", std::to_string(runtime->Sequence)}}},
                {"jobId", iCAX::Data::to_string(root->GetID())}, {"activeSetupId", OptionalID(ActiveSetup(scene,*job))},{"activePlacementId",OptionalID(ActivePlacement(scene,*job))},
                {"inputRevision", std::to_string(job->GetInputRevision())}, {"headInstances",headInstances},{"setups", setups}, {"workpieces", workpieces}, {"placements", placements},{"intentToolpaths",intents},{"fixtures",fixtures},{"fixtureUnits",fixtureUnits}};
    }
    Map ReadState(Scene& scene){return BuildSceneState(scene,false);}
    #include "FiveAxisStateProjection.inc"
    #include "FiveAxisJobProgram.inc"
    Map InspectWorkpiece(Scene& scene, const Map& request)
    {
        const auto workpiece = Require<CFiveAxisWorkpieceComponent>(scene.Database(), Identifier(request, "workpieceId"));
        const auto ref = iCAX::ProjectFile::TryGetResourceReferenceValue(workpiece->GetGeometryRef());
        if (!ref) throw std::runtime_error("CAM_GEOMETRY_MISSING");
        const auto geometry = scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref->URL, ref->nVersion);
        if (!geometry) throw std::runtime_error("CAM_GEOMETRY_MISSING");
        const auto& metadata = workpiece->GetImportMetadata();
        const bool valid = metadata.contains("validBRep") && metadata.at("validBRep").To<bool>();
        return {{"cadInspection", Map{{"status", Text(valid ? "Ready" : "Warning")},
            {"message", Text("导入版本的精确几何结构检查；制造适用性尚未验证")},
            {"geometryRef", WireRef(workpiece->GetGeometryRef())},
            {"solidCount", static_cast<UInt>(geometry->Solids.size())}, {"faceCount", static_cast<UInt>(geometry->Faces.size())},
            {"edgeCount", static_cast<UInt>(geometry->Edges.size())}}}};
    }
}
