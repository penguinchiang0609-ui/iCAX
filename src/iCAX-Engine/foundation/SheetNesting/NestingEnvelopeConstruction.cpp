#include "SheetNestingPlaneStrategies.h"
#include "NestingInternal.h"

namespace iCAX::SheetNesting
{
namespace
{
class EnvelopeInitial final : public IStrategy
{
public:
    bool OwnsRefinementLifecycle()const noexcept override{return true;}
    bool IsPureConstruction()const noexcept override{return true;}
    Result Run(const Problem& problem,const Options& options)override
    {return Detail::RunEnvelopeSkyline(problem,options);}
};
}
StrategyFactory MakePlaneEnvelopeInitialFactory()
{return []{return std::make_unique<EnvelopeInitial>();};}
}
