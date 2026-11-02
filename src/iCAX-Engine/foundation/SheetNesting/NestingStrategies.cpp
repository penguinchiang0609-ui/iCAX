#include "NestingInternal.h"
#include "SheetNestingRuleStrategies.h"
#include "SheetNestingPipeline.h"
#include "NestingRectangleDirections.h"
#include <algorithm>
#include <chrono>
#include <numeric>
#include <stdexcept>

namespace iCAX::SheetNesting
{
    void StrategyRegistry::Register(std::string id, StrategyFactory factory)
    {
        if (id.empty() || !factory) throw std::invalid_argument("A strategy needs a nonempty ID and factory.");
        for (const auto& entry : Entries)
            if (entry.ID == id) throw std::invalid_argument("Duplicate strategy ID: " + id);
        Entries.push_back({ std::move(id), std::move(factory) });
    }
    std::vector<std::string> StrategyRegistry::IDs() const
    {
        std::vector<std::string> ids;
        for (const auto& entry : Entries) ids.push_back(entry.ID);
        return ids;
    }
    std::unique_ptr<IStrategy> StrategyRegistry::Create(const std::string& id) const
    {
        for (const auto& entry : Entries) if (entry.ID == id) return entry.Factory();
        throw std::invalid_argument("Unknown strategy ID: " + id);
    }
    namespace
    {
        class OrderedRectangleStrategy final : public IStrategy
        {
        public:
            explicit OrderedRectangleStrategy(Detail::RectScore score) : Score(score) {}
            Result Run(const Problem& p, const Options& o) override
            { return Detail::RunRectangleOrdered(p,o,Score); }
        private:
            Detail::RectScore Score;
        };
        enum class RectangleSort { Area, LongSide, ShortSide, Height, Width };
        class RectanglePartOrder final : public IPartOrder
        {
        public:
            explicit RectanglePartOrder(RectangleSort sort) : Sort(sort) {}
            std::vector<std::size_t> Order(const Problem& p, const Options& o) override
            {
                std::vector<std::size_t> indices(p.Parts.size());
                std::iota(indices.begin(),indices.end(),0);
                std::vector<std::pair<double,double>> keys(indices.size());
                const auto start=std::chrono::steady_clock::now();
                bool interrupted=false;
                const auto stop=[&]
                {
                    if(interrupted)return true;
                    interrupted=(o.Cancel&&o.Cancel()) || std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=o.TimeLimitSeconds;
                    return interrupted;
                };
                for (auto i:indices)
                {
                    if(stop())return indices;
                    const auto c=Detail::FindRectangleDirections(p.Parts[i],p.Tolerance,stop);
                    if(c.Interrupted)return indices;
                    if(!c.Supported)continue;
                    const auto& d=c.Directions.front();
                    double primary=c.Area;
                    if (Sort==RectangleSort::LongSide) primary=std::max(d.Width,d.Height);
                    if (Sort==RectangleSort::ShortSide) primary=std::min(d.Width,d.Height);
                    if (Sort==RectangleSort::Height) primary=d.Height;
                    if (Sort==RectangleSort::Width) primary=d.Width;
                    keys[i]={-primary,-c.Area};
                }
                std::stable_sort(indices.begin(),indices.end(),[&](auto a,auto b){return keys[a]<keys[b];});
                return indices;
            }
        private:
            RectangleSort Sort;
        };
        class RectangleCavityStrategy final : public IStrategy
        {
        public:
            explicit RectangleCavityStrategy(Detail::CavityOrder order=Detail::CavityOrder::Area,bool columns=false):Order(order),Columns(columns){}
            Result Run(const Problem& p,const Options& o) override { return Detail::RunRectangleCavity(p,o,Order,Columns); }
        private:
            Detail::CavityOrder Order;
            bool Columns;
        };
        class RectangleSkylineStrategy final : public IStrategy
        {
        public:
            Result Run(const Problem& p,const Options& o) override { return Detail::RunRectangleSkyline(p,o); }
        };
        class RectanglePinwheelStrategy final : public IStrategy
        {
        public:
            Result Run(const Problem& p,const Options& o) override { return Detail::RunRectanglePinwheel(p,o); }
        };
        class RectangleBlockLibraryStrategy final : public IStrategy
        {
        public:
            explicit RectangleBlockLibraryStrategy(Detail::BlockRootPolicy policy=Detail::BlockRootPolicy::RoleOrder):Policy(policy){}
            Result Run(const Problem& p,const Options& o) override { return Detail::RunRectangleBlockLibrary(p,o,Policy); }
        private:
            Detail::BlockRootPolicy Policy;
        };
        class RectangleBlockRolloutStrategy final : public IStrategy
        {
        public:
            explicit RectangleBlockRolloutStrategy(Detail::BlockRolloutPolicy policy=Detail::BlockRolloutPolicy::Score):Policy(policy){}
            Result Run(const Problem& p,const Options& o) override {return Detail::RunRectangleBlockRollout(p,o,Policy);}
        private:
            Detail::BlockRolloutPolicy Policy;
        };
        class RectangleBlockResidualStrategy final : public IStrategy
        {
        public:
            Result Run(const Problem& p,const Options& o)override{return Detail::RunRectangleBlockResidual(p,o);}
        };
        class RectangleBlockRepairStrategy final : public IStrategy
        {
        public:
            Result Run(const Problem& p,const Options& o)override{return Detail::RunRectangleBlockRepair(p,o);}
        };
        class BatchRuleStrategy final : public IStrategy
        {
        public:
            explicit BatchRuleStrategy(BatchFillTiming timing) : Timing(timing) {}
            Result Run(const Problem& input, const Options& options) override
            {
                return Detail::RunRules(input, options, Detail::RectScore::ShortSide, true, true, .25,
                    false, Detail::CombinationMode::Contacts, Detail::PatternOrder::BestFit,
                    Timing == BatchFillTiming::AfterBatch ? Detail::PatternBatchMode::AfterBatch : Detail::PatternBatchMode::OnStall);
            }
        private:
            BatchFillTiming Timing;
        };
        class BatchAreaFirstStrategy final : public IStrategy
        {
        public:
            Result Run(const Problem& input, const Options& options) override
            {
                return Detail::RunRules(input, options, Detail::RectScore::ShortSide, true, true, .25,
                    false, Detail::CombinationMode::Contacts, Detail::PatternOrder::BestFit,
                    Detail::PatternBatchMode::AreaFirst);
            }
        };
        class RuleStrategy final : public IStrategy
        {
        public:
            RuleStrategy(Detail::RectScore score, bool combinations, bool circles, double preparationShare = .75, bool batchHoles = false,
                Detail::CombinationMode mode = Detail::CombinationMode::Contacts, Detail::PatternOrder order = Detail::PatternOrder::DenseAnchor)
                : Score(score), Combinations(combinations), Circles(circles), PreparationShare(preparationShare), BatchHoles(batchHoles), Mode(mode), Order(order) {}
            Result Run(const Problem& input, const Options& options) override
            {
                auto settings=options;
                // Keep complete corner-chain units intact throughout this job.
                // Independent contact/composite jobs retain arbitrary gap filling.
                if(Mode==Detail::CombinationMode::CornerChains)settings.EnableGapFill=false;
                return Detail::RunRules(input, settings, Score, Combinations, Circles, PreparationShare, BatchHoles, Mode, Order);
            }
        private:
            Detail::RectScore Score;
            bool Combinations, Circles;
            double PreparationShare;
            bool BatchHoles;
            Detail::CombinationMode Mode;
            Detail::PatternOrder Order;
        };
        class MixedCircleStrategy final : public IStrategy
        {
        public:
            explicit MixedCircleStrategy(Detail::CircleOrder order) : Order(order) {}
            Result Run(const Problem& input, const Options& options) override
            { return Detail::RunCircleRules(input, options, Order); }
        private:
            Detail::CircleOrder Order;
        };
        class ParallelogramStrategy final : public IStrategy
        {
        public:
            Result Run(const Problem& input, const Options& options) override
            { return Detail::RunParallelogramRules(input, options); }
        };
        class UnifiedContactStrategy final : public IStrategy
        {
        public:
            explicit UnifiedContactStrategy(Detail::ContactOrder order) : Order(order) {}
            Result Run(const Problem& input, const Options& options) override
            { return Detail::RunContactRules(input, options, Order); }
        private:
            Detail::ContactOrder Order;
        };
        class RectangleShelfStrategy final : public IStrategy
        {
        public:
            explicit RectangleShelfStrategy(bool columns) : Columns(columns) {}
            Result Run(const Problem& input, const Options& options) override
            { return Detail::RunRectangleShelves(input, options, Columns); }
        private:
            bool Columns;
        };
        class RectanglePatternShelfStrategy final : public IStrategy
        {
        public:
            explicit RectanglePatternShelfStrategy(bool columns) : Columns(columns) {}
            Result Run(const Problem& input, const Options& options) override
            { return Detail::RunRectanglePatternShelves(input, options, Columns); }
        private:
            bool Columns;
        };
        class StripStrategy final : public IStrategy
        {
        public:
            explicit StripStrategy(bool width):Width(width){}
            Result Run(const Problem& input,const Options& options) override
            {return Detail::RunStripRules(input,options,Width);}
        private:
            bool Width;
        };
        class ParallelogramShelfStrategy final : public IStrategy
        {
        public:
            Result Run(const Problem& input,const Options& options) override
            {return Detail::RunParallelogramShelves(input,options);}
        };
        class CircleShelfStrategy final : public IStrategy
        {
        public:
            Result Run(const Problem& input,const Options& options) override
            {return Detail::RunCircleShelves(input,options);}
        };
        class SeamAssemblyStrategy final : public IStrategy
        {
        public:
            Result Run(const Problem& input,const Options& options) override
            {return Detail::RunSeamAssembly(input,options);}
        };
        class RectangleMergeStrategy final : public IStrategy
        {
        public:
            Result Run(const Problem& input,const Options& options) override
            {return Detail::RunRectangleMerge(input,options);}
        };
        class RectangleLookaheadStrategy final : public IStrategy
        {
        public:
            Result Run(const Problem& input,const Options& options) override
            {return Detail::RunRectangleLookahead(input,options);}
        };
        class MixedAssemblyStrategy final : public IStrategy
        {
        public:
            Result Run(const Problem& input,const Options& options) override
            {return Detail::RunMixedAssemblyRules(input,options);}
        };
    }
    StrategyFactory MakeBatchRuleFactory(BatchFillTiming timing)
    {
        if (timing != BatchFillTiming::OnStall && timing != BatchFillTiming::AfterBatch)
            throw std::invalid_argument("Unknown batch gap-fill timing.");
        return [timing] { return std::make_unique<BatchRuleStrategy>(timing); };
    }

    StrategyRegistry DefaultStrategies()
    {
        StrategyRegistry registry;
        registry.Register("rect.short-side", [] { return std::make_unique<RuleStrategy>(Detail::RectScore::ShortSide, false, false); });
        registry.Register("rect.area", [] { return std::make_unique<RuleStrategy>(Detail::RectScore::Area, false, false); });
        registry.Register("rect.long-side", [] { return std::make_unique<RuleStrategy>(Detail::RectScore::LongSide, false, false); });
        registry.Register("rules.composite", [] { return std::make_unique<RuleStrategy>(Detail::RectScore::Area, true, true); });
        registry.Register("rules.composite-quick", [] { return std::make_unique<RuleStrategy>(Detail::RectScore::Area, true, true, .25); });
        registry.Register("rules.circles", [] { return std::make_unique<RuleStrategy>(Detail::RectScore::ShortSide, false, true); });
        registry.Register("circles.mixed-large", [] { return std::make_unique<MixedCircleStrategy>(Detail::CircleOrder::LargeFirst); });
        registry.Register("circles.mixed-demand", [] { return std::make_unique<MixedCircleStrategy>(Detail::CircleOrder::RemainingArea); });
        registry.Register("circles.mixed-interleave", [] { return std::make_unique<MixedCircleStrategy>(Detail::CircleOrder::Interleave); });
        registry.Register("rules.parallelograms", [] { return std::make_unique<ParallelogramStrategy>(); });
        registry.Register("rules.contact-large", [] { return std::make_unique<UnifiedContactStrategy>(Detail::ContactOrder::LargeFirst); });
        registry.Register("rules.contact-interleave", [] { return std::make_unique<UnifiedContactStrategy>(Detail::ContactOrder::BalancedSpecifications); });
        registry.Register("rules.contact-low-left", [] { return std::make_unique<UnifiedContactStrategy>(Detail::ContactOrder::LowThenLeft); });
        registry.Register("rules.contact-scarce", [] { return std::make_unique<UnifiedContactStrategy>(Detail::ContactOrder::ScarceCandidates); });
        registry.Register("rect.shelves-row", [] { return std::make_unique<RectangleShelfStrategy>(false); });
        registry.Register("rect.shelves-column", [] { return std::make_unique<RectangleShelfStrategy>(true); });
        registry.Register("rect.pattern-row", [] { return std::make_unique<RectanglePatternShelfStrategy>(false); });
        registry.Register("rect.pattern-column", [] { return std::make_unique<RectanglePatternShelfStrategy>(true); });
        registry.Register("rules.contact-remnant", [] { return std::make_unique<UnifiedContactStrategy>(Detail::ContactOrder::PreserveRemnant); });
        registry.Register("rules.contact-balanced-remnant", [] { return std::make_unique<UnifiedContactStrategy>(Detail::ContactOrder::BalancedRemnant); });
        registry.Register("rules.strip-width", [] { return std::make_unique<StripStrategy>(true); });
        registry.Register("rules.strip-height", [] { return std::make_unique<StripStrategy>(false); });
        registry.Register("parallelograms.pattern-row", [] { return std::make_unique<ParallelogramShelfStrategy>(); });
        registry.Register("circles.square-voids", [] { return std::make_unique<MixedCircleStrategy>(Detail::CircleOrder::SquareThenHoles); });
        registry.Register("rules.hole-rectangles", [] { return std::make_unique<RuleStrategy>(Detail::RectScore::Area,false,false,.75,true); });
        registry.Register("rules.complement-chains", [] { return std::make_unique<RuleStrategy>(Detail::RectScore::Area,true,true,.75,false,Detail::CombinationMode::CornerChains); });
        registry.Register("circles.cell-rows", [] { return std::make_unique<CircleShelfStrategy>(); });
        registry.Register("rules.composite-anchor-fit", [] { return std::make_unique<RuleStrategy>(Detail::RectScore::ShortSide,true,true,.75,false,Detail::CombinationMode::Contacts,Detail::PatternOrder::AnchorFit); });
        registry.Register("rules.composite-fit", [] { return std::make_unique<RuleStrategy>(Detail::RectScore::ShortSide,true,true,.75,false,Detail::CombinationMode::Contacts,Detail::PatternOrder::BestFit); });
        registry.Register("rules.composite-closure", [] { return std::make_unique<RuleStrategy>(Detail::RectScore::Area,true,true,.75,false,Detail::CombinationMode::Closure); });
        registry.Register("rules.seam-assembly", [] {return std::make_unique<SeamAssemblyStrategy>();});
        registry.Register("rect.merge-tree", [] {return std::make_unique<RectangleMergeStrategy>();});
        registry.Register("rect.lookahead", [] {return std::make_unique<RectangleLookaheadStrategy>();});
        registry.Register("rules.composite-units", [] {return std::make_unique<MixedAssemblyStrategy>();});
        registry.Register("rules.batch-on-stall", MakeBatchRuleFactory(BatchFillTiming::OnStall));
        const auto ordered=[](Detail::RectScore score)
        { return [score]{return std::make_unique<OrderedRectangleStrategy>(score);}; };
        registry.Register("rect.maxrects-bl",ordered(Detail::RectScore::BottomLeft));
        const auto sorted=[&](const std::string& id,RectangleSort sort,Detail::RectScore score)
        {
            StrategyPipeline pipeline;pipeline.Construct=ordered(score);
            pipeline.Order={id,[sort]{return std::make_unique<RectanglePartOrder>(sort);}};
            registry.Register(id,MakePipelineFactory(std::move(pipeline)));
        };
        sorted("rect.maxrects-bl-area",RectangleSort::Area,Detail::RectScore::BottomLeft);
        sorted("rect.maxrects-bl-long",RectangleSort::LongSide,Detail::RectScore::BottomLeft);
        sorted("rect.maxrects-bl-short",RectangleSort::ShortSide,Detail::RectScore::BottomLeft);
        sorted("rect.maxrects-bl-height",RectangleSort::Height,Detail::RectScore::BottomLeft);
        sorted("rect.maxrects-bl-width",RectangleSort::Width,Detail::RectScore::BottomLeft);
        sorted("rect.maxrects-bssf-area",RectangleSort::Area,Detail::RectScore::ShortSide);
        registry.Register("rect.cavity-search",[]{return std::make_unique<RectangleCavityStrategy>();});
        registry.Register("rect.skyline-bl",[]{return std::make_unique<RectangleSkylineStrategy>();});
        registry.Register("rect.cavity-wide",[]{return std::make_unique<RectangleCavityStrategy>(Detail::CavityOrder::Width);});
        registry.Register("rect.cavity-column",[]{return std::make_unique<RectangleCavityStrategy>(Detail::CavityOrder::Area,true);});
        registry.Register("rect.cavity-column-height",[]{return std::make_unique<RectangleCavityStrategy>(Detail::CavityOrder::Height,true);});
        registry.Register("rect.pinwheel-merge",[]{return std::make_unique<RectanglePinwheelStrategy>();});
        registry.Register("rect.block-library",[]{return std::make_unique<RectangleBlockLibraryStrategy>();});
        registry.Register("rect.block-library-mrv",[]{return std::make_unique<RectangleBlockLibraryStrategy>(Detail::BlockRootPolicy::InventoryMrv);});
        registry.Register("rect.block-rollout",[]{return std::make_unique<RectangleBlockRolloutStrategy>();});
        registry.Register("rect.block-rollout-diverse",[]{return std::make_unique<RectangleBlockRolloutStrategy>(Detail::BlockRolloutPolicy::Diverse);});
        registry.Register("rect.block-residual",[]{return std::make_unique<RectangleBlockResidualStrategy>();});
        registry.Register("rect.block-repair",[]{return std::make_unique<RectangleBlockRepairStrategy>();});
        registry.Register("rules.batch-area-first", [] { return std::make_unique<BatchAreaFirstStrategy>(); });
        return registry;
    }
}
