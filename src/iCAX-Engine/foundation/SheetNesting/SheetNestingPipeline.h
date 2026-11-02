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
        double MaxPrecisionAxisShift = 0;
    };
    struct StrategyPipeline
    {
        StrategyFactory Construct;
        PartOrderStage Order; // Both fields empty means preserve input order.
        std::vector<PostProcessStage> PostProcessors;
    };

    // Register the returned factory in StrategyRegistry. Each job creates fresh stage objects.
    // One cooperative time/attempt budget and latched cancellation span all stages.
    // Postprocessing uses options.Ranking and the same validation/measurement as RunPortfolio.
    StrategyFactory MakePipelineFactory(StrategyPipeline definition);

    // Optional rect.refill stage; never added to DefaultStrategies automatically.
    // Empty one rectangle-only donor into fixed conservative target bounds, then Validate originals.
    // Complete assessed incumbents only; finite scope/budget failures retain the incumbent.
    PostProcessStage MakeRectangleRefillStage();

    // Optional bounded relocation; never registered by DefaultStrategies.
    // Relocates same-specification circles to a side column, then tries moving
    // an existing group between sheets and compacting its source. Pipeline
    // validates and compares every returned candidate with the incumbent.
    PostProcessStage MakeCircleRelocationStage();
}
