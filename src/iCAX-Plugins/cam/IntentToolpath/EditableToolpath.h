#pragma once
#include "IntentToolpathExport.h"
#include "Data/Variant.h"
#include "GeometryData/GeometryData.h"
#include "GeometryData/CurveOffset3D.h"
#include "GeometryAdapter/GeometryAdapter.h"
#include <atomic>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace iCAX::Database { class IRepository; class ITransaction; }
namespace iCAX::CAM::Intent
{
using IntentId = iCAX::Data::uuid;
using IntentMap = iCAX::Data::ObjectMap;
using IntentArray = iCAX::Data::VariantArray;

struct SPortRef final
{
    IntentId OperationId, PortId;
    bool operator==(const SPortRef&) const = default;
    bool operator<(const SPortRef& other) const
    { return OperationId < other.OperationId || (OperationId == other.OperationId && PortId < other.PortId); }
};
struct SPathAnchor final
{
    SPortRef Port;
    std::string SegmentId;
    double Parameter = 0;
    std::uint64_t Occurrence = 0;
    std::string Side; // Before/After or explicit seam side; empty away from seams.
    std::string ObservedGeometryFingerprint;
};
struct SSelectedSpan final
{
    SPortRef Port;
    std::string SegmentId;
    iCAX::GeometryData::ParameterRange Range;
    std::uint64_t Occurrence = 0;
};
struct SPathRange final
{
    SPathAnchor Start, End;
    bool WrapsSeam = false;
    int TraversalDirection = 1;
    std::vector<SSelectedSpan> ExactSpans;
};
struct SSpanMapping final
{
    SPortRef Source;
    std::string SourceSegmentId;
    iCAX::GeometryData::ParameterRange SourceRange, OutputRange;
    std::string Precision = "Exact";
    double ErrorBound = 0;
    std::uint64_t Occurrence = 0;
    std::string SourceGeometryFingerprint;
};
struct SEditableSegment final
{
    std::string Id;
    std::variant<iCAX::GeometryData::CurveSegment2, iCAX::GeometryData::CurveSegment3,
        iCAX::GeometryData::DirectionalCurveOffset3> Curve;
    std::vector<SSpanMapping> Sources;
    std::string ConstructionRole;
    // Canonical uses the neutral GeometryAlgo parameter convention. CadNative
    // preserves the source CAD/offset backend's exact native domain and needs
    // an explicitly injected evaluator; it must never be read as normalized u.
    std::string Parameterization = "Canonical";
};
enum class EGeometryAuthority { Planar2D, SurfaceUV, Spatial3D };
struct SEditableGeometry final
{
    EGeometryAuthority Authority = EGeometryAuthority::Spatial3D;
    std::string FrameId = "Workpiece";
    iCAX::GeometryData::Placement3 Plane;
    std::optional<iCAX::GeometryData::Surface3> SupportSurface;
    std::vector<SEditableSegment> Segments;
    bool Closed = false;
    double Tolerance = 1e-7;
    std::string Meaning = "NominalBoundary";
};
struct SToolpathValue final
{
    SEditableGeometry Geometry;
    // Each extension channel has its own schema/version in its value. Examples:
    // pose, process, events, material, attribution, executionConstraints.
    IntentMap Channels;
};
struct SOperationInput final
{
    std::string Slot;
    SPortRef Source;
    bool Replace = true;
    std::vector<SPathRange> Ranges;
};
struct SFrozenPathInput final
{
    SOperationInput Binding;
    IntentId PathId;
    SToolpathValue Value;
    std::string Fingerprint;
};
struct SOutputCandidate final
{
    std::string RoleKey;
    std::vector<std::string> BoundaryKeys;
    // Inherit is legal only for a unique Replace chain, never for a copy.
    std::optional<std::size_t> InheritInput;
    SToolpathValue Value;
    std::string OrganizationInputSlot;
};
struct SOperationCandidate final
{
    std::vector<SOutputCandidate> Outputs;
    // Coverage for replaced inputs: mapped intervals are obtained from segment
    // Sources; removed/retained intervals must be explicit here.
    std::vector<SPathRange> ExcludedRanges;
    bool IntentionalEmpty = false;
    IntentMap Constraints;
    bool AllowRepeatedCoverage = false;
};
// Frozen on the document's owner thread. Implementations retain immutable CAD,
// BRep, service handles and version evidence; workers must never resolve live
// scene/repository context through this object. Fingerprint covers everything
// the algorithm may read and must change when that frozen context changes.
class _INTENT_TOOLPATH_EXP IToolpathOperationContext
{
public:
    virtual ~IToolpathOperationContext() = default;
    virtual std::string Fingerprint() const = 0;
};
struct SOperationEvaluation final
{
    IntentId OperationId;
    std::vector<SFrozenPathInput> Inputs;
    IntentMap Parameters, Context;
    std::shared_ptr<std::atomic_bool> Cancelled;
    std::shared_ptr<const IToolpathOperationContext> RuntimeContext;
};

// Injectable exact backend. Default implementation handles analytic curves and
// uses the neutral kernel adapter for spline evaluation. It never tessellates
// authority geometry. Products may extend material/surface validation here.
class _INTENT_TOOLPATH_EXP IToolpathGeometryServices
{
public:
    virtual ~IToolpathGeometryServices() = default;
    virtual iCAX::GeometryData::Point3 Evaluate(const SEditableGeometry&, const SEditableSegment&, double) const = 0;
    virtual void Validate(const SEditableGeometry&) const = 0;
    virtual void ValidateManufacturing(const std::string& typeId, const SOperationEvaluation&,
        const SOperationCandidate&) const = 0;
};
_INTENT_TOOLPATH_EXP std::shared_ptr<const IToolpathGeometryServices> CreateToolpathGeometryServices(
    std::shared_ptr<const iCAX::GeometryAdapter::IGeometryKernelAdapter> adapter = {});

struct SOperationType final
{
    std::string TypeId;
    std::uint32_t SchemaVersion = 1;
    std::string AlgorithmVersion = "1";
    std::size_t MinimumInputs = 1, MaximumInputs = 1;
    std::set<std::string> ReadChannels = {"geometry", "topology", "traversal", "provenance", "frame", "*"};
    std::set<std::string> WriteChannels;
    bool PropagateUnwrittenChannels = true;
    bool InheritExecutionBlock = true;
    IntentMap ParameterSchema;
    std::function<void(const IntentMap&)> ValidateParameters;
    std::function<IntentMap(const std::vector<SFrozenPathInput>&,const IntentMap&)> NormalizeParameters;
    std::function<void(const SOperationEvaluation&)> ValidateInput;
    std::function<SOperationCandidate(const SOperationEvaluation&, const IToolpathGeometryServices&)> Evaluate;
    std::function<void(const SOperationEvaluation&, const SOperationCandidate&)> ValidateResult;
    std::function<IntentMap(std::uint32_t, const IntentMap&)> UpgradeParameters;
    // Called only on the document owner thread. Failure invalidates this
    // operation without preventing unrelated graph branches from evaluating.
    std::function<std::shared_ptr<const IToolpathOperationContext>(const IntentMap& persistentContext)> FreezeContext;
    // An exact fixed output contract, usable while an upstream operation is
    // still pending. Empty means output roles are determined only by evaluation.
    // Variable topology must never advertise a guessed fixed role set.
    std::set<std::string> DeclaredOutputRoles;
    // Fixed roles retaining an input path identity declare that relationship
    // before evaluation, so pending inspection never guesses an output ID.
    std::map<std::string,std::size_t> DeclaredOutputIdentityInputs;
    // Derived computation revision, independent of the editable definition.
    std::string EvaluationRevision = "1";
};
class _INTENT_TOOLPATH_EXP CToolpathOperationRegistry final
{
public:
    CToolpathOperationRegistry();
    ~CToolpathOperationRegistry();
    void Register(SOperationType);
    void Unregister(const std::string& typeId);
    std::shared_ptr<const SOperationType> Find(const std::string& typeId) const;
    std::vector<SOperationType> Describe() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
_INTENT_TOOLPATH_EXP void RegisterStandardToolpathOperations(CToolpathOperationRegistry&);

struct SResolvedPath final
{
    IntentId PathId;
    SPortRef Port;
    std::string Name, UserNumber, Fingerprint;
    SToolpathValue Value;
    std::uint64_t LabelsVersion = 0;
    std::string GroupId;
    bool Participating = true, Locked = false;
    std::string ResultState = "Current";
    bool OwnLocked = false, OwnParticipating = true;
    std::string ParticipationReason;
    IntentMap Layers;
};
struct SIntentDiagnostic final
{
    IntentId OperationId;
    std::string Code, Message;
};
enum class EOutputCompleteness { Complete, Incomplete, Invalid };
struct SEffectiveToolpaths final
{
    EOutputCompleteness Status = EOutputCompleteness::Complete;
    bool IntentionalEmpty = false;
    std::vector<SResolvedPath> Paths;
    std::vector<SPortRef> Exclusions;
    std::vector<SIntentDiagnostic> Diagnostics;
    IntentMap ExecutionConstraints;
    IntentMap Organization;
    std::string Fingerprint;
    IntentMap DisplayConstraints;
    IntentMap Layers;
    std::string PresentationFingerprint;
};

enum class ESourcePathIdentityMode { AllocateNew, CreateWithId, AdoptExisting };
struct SSourcePathIdentity final
{
    ESourcePathIdentityMode Mode = ESourcePathIdentityMode::AllocateNew;
    IntentId PathEntityId;
    // Optional predeclared producer identity for exact point/range attachments
    // authored in the same host transaction as the source definition.
    IntentId SourceOperationId;
};
struct SPlanCloneSourceTransform final
{
    SToolpathValue Value;
    // Core verifies exact equality of all geometry fields except FrameId before
    // using this certificate to update saved parameter-identity evidence.
    bool ReframeOnly = false;
};
struct SPlanCloneTransform final
{
    IntentMap Evidence;
    std::map<IntentId,IntentId> ExternalIdentityRemap;
    std::function<SPlanCloneSourceTransform(IntentId oldOperation,IntentId newOperation,
        const SToolpathValue& remappedSource)> TransformSource;
    std::function<IntentMap(IntentId oldOperation,IntentId newOperation,const std::string& typeId,
        const IntentMap& remappedContext,const std::map<IntentId,IntentId>& idRemap)> TransformContext;
};

// The product contributes ordinary component facts to the same transaction as
// a fully preflighted toolpath command batch. Neither callback may write to the
// repository directly: Preflight only reads and Stage queues through its tx.
// Callbacks receive new command receipts only, in request order. Replayed
// commands retain their acknowledgements without repeating product writes.
struct SEditableToolpathHostEdit final
{
    std::string UndoName = "编辑刀路";
    bool RecordUndo = true;
    // Keys are CreateSource command IDs, deliberately outside untrusted command
    // JSON. Existing entities require explicit adoption and exclusive ownership.
    std::map<IntentId, SSourcePathIdentity> SourceIdentities;
    std::map<IntentId, SPlanCloneTransform> PlanClones;
    std::function<void(const std::vector<IntentMap>& newReceipts)> Preflight;
    std::function<void(iCAX::Database::ITransaction&, const std::vector<IntentMap>& newReceipts)> Stage;
};

// All Repository access and publication occur on the calling owner thread.
// Workers read frozen values only; independent graph branches run concurrently.
class _INTENT_TOOLPATH_EXP CToolpathGeneration final
{
public:
    ~CToolpathGeneration();
    void Cancel();
    bool IsReady() const;
private:
    friend class CEditableToolpathDocument;
    struct Impl;
    explicit CToolpathGeneration(std::shared_ptr<Impl>);
    std::shared_ptr<Impl> m_Impl;
};

class _INTENT_TOOLPATH_EXP CEditableToolpathDocument final
{
public:
    // Passing a nil DocumentId creates a new document. Existing documents are
    // read without changing their values or relying on external import files.
    CEditableToolpathDocument(std::shared_ptr<iCAX::Database::IRepository>, IntentId documentId,
        std::shared_ptr<CToolpathOperationRegistry>, std::shared_ptr<const IToolpathGeometryServices> = {});
    ~CEditableToolpathDocument();
    CEditableToolpathDocument(const CEditableToolpathDocument&) = delete;
    CEditableToolpathDocument& operator=(const CEditableToolpathDocument&) = delete;
    // Prepare a new document without publishing any entity. Its first command
    // batch creates the document together with the host's product facts.
    static std::unique_ptr<CEditableToolpathDocument> PrepareNewDocument(
        std::shared_ptr<iCAX::Database::IRepository>, IntentId documentId,
        std::shared_ptr<CToolpathOperationRegistry>, std::shared_ptr<const IToolpathGeometryServices> = {});
    IntentId Id() const;
    IntentId CreatePlan(const std::string& name);
    IntentMap Execute(const IntentMap& command);
    IntentMap Execute(const IntentMap& command, const SEditableToolpathHostEdit& host);
    // All commands are preflighted against one in-memory overlay, then their
    // precise component writes and product writes commit as one undo step.
    // No intermediate definitions or product entities become visible.
    std::vector<IntentMap> ExecuteBatch(const std::vector<IntentMap>& commands,
        const SEditableToolpathHostEdit& host = {});
    // Optional explicit preview. No identity/definition is published.
    SOperationCandidate Preview(const std::string& typeId, const IntentMap& parameters,
        const std::vector<SPortRef>& inputs, const IntentMap& context = {}) const;
    std::shared_ptr<CToolpathGeneration> GenerateAsync(IntentId planId, std::size_t concurrency = 0,
        const std::vector<IntentId>& forceOperations = {});
    // Nonblocking; returns false until ready. Every candidate is checked again
    // against its own read set before publication. Unrelated edits do not void it.
    bool Publish(const std::shared_ptr<CToolpathGeneration>&);
    SEffectiveToolpaths Resolve(IntentId planId) const;
    // Inspection includes explicitly excluded/disabled terminal paths and their
    // participation state; execution must consume Resolve instead.
    SEffectiveToolpaths InspectPlan(IntentId planId) const;
    // Reads any current dependency-graph port, including consumed intermediate
    // results. Stale cached geometry is never returned as a current value.
    SResolvedPath ReadPath(IntentId planId, SPortRef port) const;
    // Validates one plan snapshot for all requested ports. Results retain input
    // order (including duplicates); any invalid port rejects the whole read.
    std::vector<SResolvedPath> ReadPaths(IntentId planId, const std::vector<SPortRef>& ports) const;
    std::vector<SResolvedPath> QueryPaths(IntentId planId, std::size_t offset, std::size_t limit,
        const std::string& expectedFingerprint = {}) const;
    IntentMap QuerySummary(IntentId planId) const;
    IntentMap QueryPathSummaries(IntentId planId,std::size_t offset,std::size_t limit,
        const std::string& expectedFingerprint = {}) const;
    IntentMap ReadOperation(IntentId operationId) const;
    IntentMap ReadOperationDefinition(IntentId operationId) const;
    IntentMap ReadPlan(IntentId planId) const;
    // Plan organization/settings only; does not capture every operation result.
    IntentMap ReadPlanDefinition(IntentId planId) const;
    // Persistent document layer catalog and defaults; no geometry generation.
    IntentMap QueryLayers() const;
    // Terminal path layer/lock properties, read without evaluating geometry.
    IntentMap QueryLayerAssignments(IntentId planId) const;
    IntentMap ExportDocument() const;
    // Atomic database import; all references and exact values validated first.
    static IntentId ImportDocument(std::shared_ptr<iCAX::Database::IRepository>, const IntentMap&,
        std::shared_ptr<const IToolpathGeometryServices> = {});
private:
    CEditableToolpathDocument(std::shared_ptr<iCAX::Database::IRepository>, IntentId,
        std::shared_ptr<CToolpathOperationRegistry>, std::shared_ptr<const IToolpathGeometryServices>, bool deferred);
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

_INTENT_TOOLPATH_EXP IntentMap ToData(const SPortRef&);
_INTENT_TOOLPATH_EXP std::string ToolpathPortKey(const SPortRef&);
_INTENT_TOOLPATH_EXP SPortRef SourceToolpathPort(IntentId sourceOperationId);
_INTENT_TOOLPATH_EXP SPortRef DeclaredToolpathPort(IntentId operationId,const std::string& role);
_INTENT_TOOLPATH_EXP IntentMap ToData(const SOperationInput&);
_INTENT_TOOLPATH_EXP SPortRef PortFromData(const IntentMap&);
_INTENT_TOOLPATH_EXP IntentMap ToData(const SPathAnchor&);
_INTENT_TOOLPATH_EXP SPathAnchor AnchorFromData(const IntentMap&);
_INTENT_TOOLPATH_EXP IntentMap ToData(const SPathRange&);
_INTENT_TOOLPATH_EXP SPathRange RangeFromData(const IntentMap&);
_INTENT_TOOLPATH_EXP SPathRange CaptureToolpathRange(const SResolvedPath&,SPathAnchor start,
    SPathAnchor end,bool wrapsSeam=false,int traversalDirection=1);
_INTENT_TOOLPATH_EXP IntentMap ToData(const SToolpathValue&);
_INTENT_TOOLPATH_EXP SToolpathValue ToolpathFromData(const IntentMap&);
_INTENT_TOOLPATH_EXP std::string ToolpathFingerprint(const SToolpathValue&, const std::set<std::string>& channels = {"*"});
_INTENT_TOOLPATH_EXP SEditableGeometry MakeEditableGeometry(const iCAX::GeometryData::CompositeCurve3&,
    const std::string& stableSourceKey, const std::string& frame = "Workpiece", double tolerance = 1e-7);
_INTENT_TOOLPATH_EXP SEditableGeometry MakeEditableGeometry(const iCAX::GeometryData::CompositeCurve2&,
    const std::string& stableSourceKey, const iCAX::GeometryData::Placement3& plane, double tolerance = 1e-7);
}
