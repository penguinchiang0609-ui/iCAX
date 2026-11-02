#pragma once
#include "SheetNesting.h"
namespace iCAX::SheetNesting {
// Optional, bounded search over zero-gap polygon domains. It never proves infeasibility.
enum class DomainOrderPolicy { FreeArea, FewestPlacements, AreaFirst, TallFirst };
StrategyFactory MakeDomainSearchFactory(DomainOrderPolicy policy=DomainOrderPolicy::FreeArea);
namespace Detail {
struct PlacementDomainCandidate {Pose Transform;std::vector<std::size_t> Blockers;};
struct PlacementDomainQueryResult {std::vector<PlacementDomainCandidate> Candidates;std::size_t Attempts=0;bool Truncated=false;};
class IPlacementDomainProbe {
public:
    virtual ~IPlacementDomainProbe()=default;
    virtual PlacementDomainQueryResult Find(const Problem&,const Layout&,std::size_t partIndex,
        std::size_t maxBlockers,const Options&)=0;
};
// A reusable job-local probe shares pure geometry cache, never layout state.
std::unique_ptr<IPlacementDomainProbe> MakePlacementDomainProbe();
// Diagnostic access to the same placement-domain enumeration used by the job.
std::vector<Pose> ProbeDomainCandidates(const Problem&,const Layout&,std::size_t partIndex,const Options&);
}
}
