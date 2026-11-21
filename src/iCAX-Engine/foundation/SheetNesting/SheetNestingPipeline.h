#pragma once
#include "SheetNesting.h"

namespace iCAX::SheetNesting
{
    // An input preference, not a replacement for a constructor's dynamic fit decisions.
    // Return every original Parts index exactly once, including zero-quantity entries.
    class IPartOrder
    {
    public:
        virtual ~IPartOrder() = default;
        virtual std::vector<std::size_t> Order(const Problem& input, const Options& options) = 0;
    };
    using PartOrderFactory = std::function<std::unique_ptr<IPartOrder>()>;

    class IPostProcessor
    {
    public:
        virtual ~IPostProcessor() = default;
        // Pipeline assessment validates the immutable original input/incumbent before this call.
        // Return ORIGINAL identities. Work statistics describe NEW work, not copied cumulative work.
        // Constructor-origin group/filler counts stay with the selected result's provenance.
        virtual Result Run(const Problem& input, const Result& incumbent, const Options& options) = 0;
    };
    using PostProcessorFactory = std::function<std::unique_ptr<IPostProcessor>()>;
    struct PartOrderStage { std::string ID; PartOrderFactory Factory; };
    struct PostProcessStage
    {
        std::string ID;
        PostProcessorFactory Factory;
        // Nonzero declares a validated precision repair that preserves each
        // placement's identity, sheet and angle within this axis shift bound.
        // Zero keeps the default strict quality-improvement acceptance rule.
        // Default basic-refinement execution additionally keeps the published
        // shared quality non-worsening; disabled raw mode retains the isolated
        // precision-repair acceptance contract.
        double MaxPrecisionAxisShift = 0;
        // Fraction of the remaining search time and attempt cap supplied to
        // this stage. Unused work remains available to following stages.
        double BudgetFraction = 1;
    };
    struct StrategyPipeline
    {
        StrategyFactory Construct;
        PartOrderStage Order; // Both fields empty means preserve input order.
        std::vector<PostProcessStage> PostProcessors;
        // A construction-first workflow refines only a complete original-valid
        // winner. Partial construction remains available with its unmet demand.
        bool RequireCompleteConstruction = false;
        // Fraction of remaining search time and attempts supplied to construction.
        // Unused work remains available to refinement. Range is (0, 1].
        double ConstructionBudgetFraction = 1;
    };

    // Independent initial constructors compete under one cooperative time and
    // attempt budget. Jobs run serially with fair remaining-budget shares and
    // fresh strategy instances. They use original validation and shared Quality.
    // This pure construction factory owns its budget and disables automatic
    // refinement. Compose it with RequireCompleteConstruction for the next phase.
    // Empty IDs select the registry's constructors. Composed refinement
    // strategies are diagnosed and skipped within this construction phase.
    StrategyFactory MakeConstructionPortfolioFactory(const StrategyRegistry& registry,
        std::vector<std::string> strategyIDs = {});

    struct ParallelConstructionSettings
    {
        // Zero selects automatic concurrency from hardware and scheduled routes.
        // Explicit worker counts are 1..32. Extra routes share the same deadline.
        std::size_t Workers = 0;
        // Called serially on the Run caller thread for a validated, complete,
        // strictly better incumbent. The reference is valid only during the call.
        // Callback failures are diagnosed and disable that callback only.
        std::function<void(const JobResult&)> OnImprovement;
        // Optional caller-thread proof that a complete improvement meets the
        // caller's target. Success stops other routes, not as user cancellation.
        std::function<bool(const Problem&, const JobResult&)> StopWhen;
        // False preserves fixed attempt shares and the common remaining deadline.
        // True assigns each starting route a remaining-time slice according to
        // unfinished routes/workers, and leases attempts from a shared pool.
        // Completed routes return their unused lease for queued constructors.
        bool FairScheduling = false;
        // Caller-supplied product strategy metadata; empty preserves generic
        // scheduling. Listed input route IDs receive ContinuousConstructionTime
        // before allocation and may use the shared remaining wall deadline.
        // The scheduler needs no algorithm-specific branches; route eligibility,
        // total attempt limits and cancellation rules remain unchanged.
        std::vector<std::string> ContinuousTimeRoutes;
    };

    // Bounded parallel initial construction with fresh independent strategies.
    // Factories, original Cancel polling and both callbacks run on the caller.
    // Workers are released together. The default uses fixed global attempt
    // shares and the remaining common wall deadline. FairScheduling assigns
    // bounded local slices (except caller-declared continuous-time routes) and
    // returns unused attempt leases to queued routes;
    // both modes keep the same global wall deadline and total attempt cap.
    // Worker cancellation reads only a latched atomic flag. All workers are joined;
    // constructors and callbacks must cooperate for a wall deadline to be hard.
    // Composed refinement strategies are skipped and outer basic refinement is off.
    // Only original Cancel confirmation makes the final state Cancelled; deadline
    // or StopWhen retains the best validated complete/partial result normally.
    StrategyFactory MakeParallelConstructionPortfolioFactory(const StrategyRegistry& registry,
        std::vector<std::string> strategyIDs = {}, ParallelConstructionSettings settings = {});

    // Register the returned factory in StrategyRegistry. Each job creates fresh stage objects.
    // One cooperative time/attempt budget and latched cancellation span all stages.
    // With Options::EnableBasicCompaction, one reserved pool settles the valid
    // constructor result and every explicit postprocessor result before final
    // shared-quality acceptance. The outer executor owns nested constructors;
    // it cannot observe iterations hidden inside a postprocessor's Run method.
    // Postprocessing uses options.Ranking and the same validation/measurement as RunPortfolio.
    StrategyFactory MakePipelineFactory(StrategyPipeline definition);

    // Optional rect.refill stage; never added to DefaultStrategies automatically.
    // Empty one rectangle-only donor into fixed conservative target bounds, then Validate originals.
    // Complete assessed incumbents only; finite scope/budget failures retain the incumbent.
    PostProcessStage MakeRectangleRefillStage();

    // Optional forward refill of original polygons. Uses actual NFP-safe
    // contact positions, may detach construction groups, and concentrates
    // material into fuller compatible boards before eliminating empty tails.
    // Unsupported shapes/gap/budget retain the incumbent; not auto-registered.
    // Positive gaps use conservative private fixed-obstacle contours, with
    // original nominal distance checks. Up to 64 compatible polygon specs.
    PostProcessStage MakeNfpRefillStage();

    // Optional bounded relocation; never registered by DefaultStrategies.
    // Relocates same-specification circles to a side column, then tries moving
    // an existing group between sheets and compacting its source. Pipeline
    // validates and compares every returned candidate with the incumbent.
    PostProcessStage MakeCircleRelocationStage();
}
