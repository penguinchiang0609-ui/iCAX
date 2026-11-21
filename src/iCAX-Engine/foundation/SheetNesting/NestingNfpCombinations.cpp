#include "NestingNfpCombinations.h"
#include "NestingNfpContactSamples.h"
#include "NestingClearancePlacement.h"
#include "NestingPairSummary.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct CombinationPreparationSummary
    {
        using Clock=std::chrono::steady_clock;
        const NfpCombinationOptions& Settings;
        const std::size_t& Attempts;
        const std::size_t BeginAttempts;
        const Clock::time_point Started=Clock::now();
        Options Observation;
        std::size_t RegionRequests=0,UnavailableRegions=0,Enumerated=0,Proposals=0;
        std::size_t CrossPartRegionRequests=0,CrossPartBeamChoices=0;
        std::size_t Certified=0,Retained=0,NestedAttempts=0,NestedCalls=0;
        std::size_t SelfCoverageSlots=0,SelfCoverageSpecifications=0;
        std::size_t BasicSeeds=0,BasicQueries=0,BasicExact=0,BasicRetained=0;
        bool CoverageTruncated=false;
        std::size_t PreparationFamilies=0;
        double RemainingPreparationSecondsAtStart=-1,SelfTimeAllowanceSeconds=-1,ReservedGeneralTimeSeconds=-1;
        double HighestRetainedUtilization=0;
        bool Stopped=false;
        const char* ExitReason="complete";
        CombinationPreparationSummary(const NfpCombinationOptions& options,const std::size_t& attempts)
            :Settings(options),Attempts(attempts),BeginAttempts(attempts)
        {
            Observation.ConstructionTrace=options.ConstructionTrace;
            Observation.ConstructionTraceRoute=options.ConstructionTraceRoute;
            Observation.ConstructionTraceScope=options.ConstructionTraceScope;
            Observation.ConstructionObservation=options.ConstructionObservation;
        }
        ~CombinationPreparationSummary() noexcept
        {
            if(!Observation.ConstructionTrace)return;
            try
            {
                Stopped=Stopped||Attempts>=Settings.MaxAttempts||(Settings.Stop&&Settings.Stop());
                const auto charged=Attempts>=BeginAttempts?Attempts-BeginAttempts:0;
                ObserveConstruction(Observation,ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event)
                {
                    event.Source="preparation.combination-summary";
                    event.Scope+=Settings.SelfPairsOnly?"/nfp-self":"/nfp-general";
                    event.Accepted=Retained>0;event.SearchTruncated=Stopped||CoverageTruncated;event.Attempts=charged;
                    event.Reason=ExitReason;
                    event.Measures={{"selfPairsOnly",Settings.SelfPairsOnly?1.:0.},
                        {"samePartOnly",Settings.SamePartOnly?1.:0.},
                        {"expandAssemblies",Settings.ExpandAssemblies?1.:0.},
                        {"orientationProposals",static_cast<double>(Settings.OrientationProposals)},
                        {"preparationFamilies",static_cast<double>(PreparationFamilies)},
                        {"remainingPreparationSecondsAtStart",RemainingPreparationSecondsAtStart},
                        {"selfTimeAllowanceSeconds",SelfTimeAllowanceSeconds},
                        {"reservedGeneralTimeSeconds",ReservedGeneralTimeSeconds},
                        {"basicCoverageSeeds",static_cast<double>(BasicSeeds)},
                        {"basicCoverageQueries",static_cast<double>(BasicQueries)},
                        {"basicCoverageExactAttempts",static_cast<double>(BasicExact)},
                        {"basicCoverageRetained",static_cast<double>(BasicRetained)},
                        {"coverageTruncated",CoverageTruncated?1.:0.},
                        {"wallSeconds",std::chrono::duration<double>(Clock::now()-Started).count()},
                        {"pairRegionRequests",static_cast<double>(RegionRequests)},
                        {"crossPartRegionRequests",static_cast<double>(CrossPartRegionRequests)},
                        {"crossPartBeamChoices",static_cast<double>(CrossPartBeamChoices)},
                        {"pairRegionUnavailable",static_cast<double>(UnavailableRegions)},
                        {"enumeratedContactSamples",static_cast<double>(Enumerated)},
                        {"uniqueProposals",static_cast<double>(Proposals)},
                        {"exactAttempts",static_cast<double>(charged-std::min(charged,NestedAttempts))},
                        {"attemptsCharged",static_cast<double>(charged)},
                        {"nestedSelfAttempts",static_cast<double>(NestedAttempts)},
                        {"nestedSelfBuilds",static_cast<double>(NestedCalls)},
                        {"wallIncludesNestedSelf",NestedCalls?1.:0.},
                        {"certifiedCandidates",static_cast<double>(Certified)},
                        {"retainedPatterns",static_cast<double>(Retained)},
                        {"selfCoverageSlots",static_cast<double>(SelfCoverageSlots)},
                        {"selfCoverageSpecifications",static_cast<double>(SelfCoverageSpecifications)},
                        {"highestRetainedUtilization",HighestRetainedUtilization},
                        {"stopped",Stopped?1.:0.}};
                });
            }
            catch(...){if(Observation.ConstructionObservation)
                {Observation.ConstructionObservation->Failed=true;++Observation.ConstructionObservation->Omitted;}}
        }
    };
    struct State
    {
        Pattern Value;
        std::vector<std::size_t> Demand;
        double Fill=0,SavedFraction=0;
        unsigned AnchorSides=0;
        bool Vertical=false;
        double LongSpan=0,BridgeAnchorArea=0;
        std::vector<std::size_t> BridgeAnchors;
        std::vector<Point> Carrier;
    };
    double Utilization(const Pattern& pattern)
    {return pattern.Width>0&&pattern.Height>0?pattern.MaterialArea/(pattern.Width*pattern.Height):0;}
    bool SameDemand(const Pattern& a,const Pattern& b)
    {
        if(a.Members.size()!=b.Members.size())return false;
        for(const auto& member:a.Members)
        {
            const auto count=[&](const Pattern& pattern)
            {return std::count_if(pattern.Members.begin(),pattern.Members.end(),
                [&](const Member& item){return item.PartIndex==member.PartIndex;});};
            if(count(a)!=count(b))return false;
        }
        return true;
    }
    bool Denser(const State& a,const State& b)
    {return a.Fill>b.Fill;}
    bool SameStoredGeometry(const Pattern& a,const Pattern& b)
    {
        if(a.Members.size()!=b.Members.size()||a.Members.size()>4)return false;
        using Key=std::tuple<std::size_t,std::uint64_t,std::uint64_t,std::uint64_t,bool>;
        std::array<Key,4> first{},second{};
        const auto fill=[](const Pattern& pattern,auto& keys)
        {
            for(std::size_t i=0;i<pattern.Members.size();++i)
            {const auto& member=pattern.Members[i];keys[i]={member.PartIndex,
                std::bit_cast<std::uint64_t>(member.Transform.X),std::bit_cast<std::uint64_t>(member.Transform.Y),
                std::bit_cast<std::uint64_t>(member.Transform.AngleDegrees),member.Transform.Mirrored};}
            std::sort(keys.begin(),keys.begin()+pattern.Members.size());
        };
        fill(a,first);fill(b,second);return first==second;
    }
    void NormalizeLight(Pattern& pattern,const Problem& p)
    {
        double x0=std::numeric_limits<double>::infinity(),y0=x0,x1=-x0,y1=-x0;pattern.MaterialArea=0;
        for(const auto& member:pattern.Members)
        {
            const auto b=Bounds(p.Parts[member.PartIndex].Geometry,member.Transform);
            x0=std::min(x0,b.X);y0=std::min(y0,b.Y);x1=std::max(x1,b.X+b.Width);y1=std::max(y1,b.Y+b.Height);
            pattern.MaterialArea+=Area(p.Parts[member.PartIndex].Geometry);
        }
        for(auto& member:pattern.Members){member.Transform.X-=x0;member.Transform.Y-=y0;member.Transform.AngleDegrees=NormalizeAngle(member.Transform.AngleDegrees);}
        pattern.Width=x1-x0;pattern.Height=y1-y0;
    }
    std::vector<Point> Hull(const Pattern& pattern,const Problem& p)
    {
        std::vector<Point> points;
        for(const auto& member:pattern.Members)
            for(auto q:p.Parts[member.PartIndex].Geometry.Outer)points.push_back(Transform(q,member.Transform));
        std::sort(points.begin(),points.end(),[](Point a,Point b){return std::tie(a.X,a.Y)<std::tie(b.X,b.Y);});
        points.erase(std::unique(points.begin(),points.end(),[](Point a,Point b){return a.X==b.X&&a.Y==b.Y;}),points.end());
        if(points.size()<3)return {};
        const auto cross=[](Point a,Point b,Point c)
        {return (static_cast<long double>(b.X)-a.X)*(c.Y-a.Y)-(static_cast<long double>(b.Y)-a.Y)*(c.X-a.X);};
        std::vector<Point> hull;
        for(auto q:points){while(hull.size()>1&&cross(hull[hull.size()-2],hull.back(),q)<=0)hull.pop_back();hull.push_back(q);}
        const auto lower=hull.size();
        for(auto it=points.rbegin()+1;it!=points.rend();++it)
        {while(hull.size()>lower&&cross(hull[hull.size()-2],hull.back(),*it)<=0)hull.pop_back();hull.push_back(*it);}
        if(hull.size()>1)hull.pop_back();return hull;
    }
    bool FilledBasic(ShapeKind kind)
    {return kind==ShapeKind::Rectangle||kind==ShapeKind::Parallelogram;}
    bool Clear(const Problem& p,const Member& a,const Member& b,bool* geometryConflict=nullptr)
    {
        if(!PoseAllowed(p.Parts[a.PartIndex],a.Transform,p.Tolerance)||!PoseAllowed(p.Parts[b.PartIndex],b.Transform,p.Tolerance))
        {if(geometryConflict)*geometryConflict=true;return false;}
        const auto& first=p.Parts[a.PartIndex].Geometry;
        const auto& second=p.Parts[b.PartIndex].Geometry;
        if(ShapesConflict(first,a.Transform,second,b.Transform,p.PartGap,p.Tolerance,p.AllowCommonEdges))
        {if(geometryConflict)*geometryConflict=true;return false;}
        const auto verdict=AxisSafety::Pair(AxisSafety::Make(first,a.Transform),AxisSafety::Make(second,b.Transform),
            p.PartGap,p.AllowCommonEdges,p.Tolerance);
        return verdict==AxisSafety::Verdict::Safe;
    }
    void EvaluateNormalized(State& state,const Problem& p,bool recognize)
    {
        state.Carrier.clear();state.Value.Kind=ShapeKind::Irregular;
        const auto& pattern=state.Value;
        state.Fill=Utilization(pattern);
        double separate=0;for(const auto& member:pattern.Members)
        {const auto box=Bounds(p.Parts[member.PartIndex].Geometry,member.Transform);separate+=box.Width*box.Height;}
        state.SavedFraction=separate>0?1-pattern.Width*pattern.Height/separate:0;
        const auto dominant=std::max_element(pattern.Members.begin(),pattern.Members.end(),[&](const Member& a,const Member& b)
        {return Area(p.Parts[a.PartIndex].Geometry)<Area(p.Parts[b.PartIndex].Geometry);});
        const auto anchor=Bounds(p.Parts[dominant->PartIndex].Geometry,dominant->Transform);
        const bool vertical=anchor.Height>=anchor.Width;state.AnchorSides=0;state.BridgeAnchors.clear();
        state.Vertical=vertical;state.LongSpan=vertical?anchor.Height:anchor.Width;
        state.BridgeAnchorArea=Area(p.Parts[dominant->PartIndex].Geometry);
        for(std::size_t index=0;index<pattern.Members.size();++index)
        {
            const auto& member=pattern.Members[index];
            const auto box=Bounds(p.Parts[member.PartIndex].Geometry,member.Transform);
            const bool left=box.X<anchor.X-p.Tolerance,right=box.X+box.Width>anchor.X+anchor.Width+p.Tolerance;
            const bool low=box.Y<anchor.Y-p.Tolerance,high=box.Y+box.Height>anchor.Y+anchor.Height+p.Tolerance;
            if(vertical){if(left)state.AnchorSides|=1;if(right)state.AnchorSides|=2;if(low)state.AnchorSides|=4;if(high)state.AnchorSides|=8;}
            else{if(low)state.AnchorSides|=1;if(high)state.AnchorSides|=2;if(left)state.AnchorSides|=4;if(right)state.AnchorSides|=8;}
            const auto area=Area(p.Parts[member.PartIndex].Geometry);
            if(area<Area(p.Parts[dominant->PartIndex].Geometry)&&(vertical?(left||right):(low||high)))
            {state.BridgeAnchors.push_back(index);state.BridgeAnchorArea=std::min(state.BridgeAnchorArea,area);}
        }
        // Clearance contours never certify filled material. Positive-gap
        // groups retain their original-material AABB density and geometry.
        if(p.PartGap>0||!recognize||pattern.Members.size()<2)return;
        auto hull=Hull(pattern,p);if(hull.size()!=4)return;
        Shape carrier;carrier.Outer=hull;
        if(!FilledBasic(Classify(carrier,p.Tolerance).Kind))return;
        const auto carrierArea=Area(carrier);double perimeter=0;
        for(std::size_t i=0;i<hull.size();++i)perimeter+=std::hypot(hull[i].X-hull[(i+1)%hull.size()].X,hull[i].Y-hull[(i+1)%hull.size()].Y);
        // The hull is a cheap rejection only. A void, disconnected group or
        // overlap must still pass NormalizePattern's original-material union.
        if(carrierArea<=0||std::abs(carrierArea-pattern.MaterialArea)>perimeter*p.Tolerance)return;
        const auto verified=AssessNfpCombinationCarrier(p,pattern);
        if(!verified.Filled)return;
        state.Value.Kind=verified.Kind;state.Carrier=verified.Boundary;
    }
    void Evaluate(State& state,const Problem& p,bool recognize)
    {NormalizeLight(state.Value,p);EvaluateNormalized(state,p,recognize);}
    std::string Signature(const State& state)
    {
        std::string s;for(std::size_t i=0;i<state.Demand.size();++i)if(state.Demand[i])s+=std::to_string(i)+":"+std::to_string(state.Demand[i])+";";
        // Reflection families share the same physical demand. Keep their
        // bounded geometry variants distinct without creating another part.
        std::map<std::size_t,std::size_t> mirrored;
        for(const auto& member:state.Value.Members)if(member.Transform.Mirrored)++mirrored[member.PartIndex];
        for(const auto& [part,count]:mirrored)s+="mirror:"+std::to_string(part)+":"+std::to_string(count)+";";
        return s;
    }
    std::string GeometryKey(const State& state,double tolerance)
    {
        std::vector<std::tuple<std::size_t,long long,long long,long long,bool>> values;
        const auto step=std::max(tolerance,1e-7);
        for(const auto& m:state.Value.Members)values.emplace_back(m.PartIndex,std::llround(m.Transform.X/step),
            std::llround(m.Transform.Y/step),std::llround(NormalizeAngle(m.Transform.AngleDegrees)*1e6),m.Transform.Mirrored);
        std::sort(values.begin(),values.end());std::ostringstream out;
        for(const auto& [i,x,y,a,mirrored]:values)out<<i<<':'<<x<<':'<<y<<':'<<a<<':'<<mirrored<<';';
        return out.str();
    }
}
NfpCarrierAssessment AssessNfpCombinationCarrier(const Problem& p,const Pattern& pattern)
{
    NfpCarrierAssessment result;if(pattern.Members.empty())return result;
    std::vector<std::size_t> used(p.Parts.size());std::string material;bool first=true;
    for(const auto& member:pattern.Members)
    {
        if(member.PartIndex>=p.Parts.size())return result;
        const auto& part=p.Parts[member.PartIndex];
        if(++used[member.PartIndex]>part.Quantity||!PoseAllowed(part,member.Transform,p.Tolerance))return result;
        if(first){material=part.Material;first=false;}else if(material!=part.Material)return result;
    }
    auto actual=pattern;NormalizePattern(actual,p);
    result.Area=actual.Width*actual.Height;
    result.Fill=result.Area>0?actual.MaterialArea/result.Area:0;
    if(actual.Members.size()<2||!FilledBasic(actual.Kind))return result;
    result.Boundary=Hull(actual,p);if(result.Boundary.size()!=4){result.Boundary.clear();return result;}
    Shape carrier;carrier.Outer=result.Boundary;result.Area=Area(carrier);
    if(result.Area<=0){result.Boundary.clear();return result;}
    result.Kind=actual.Kind;result.Fill=1;result.Filled=true;return result;
}
std::vector<Pattern> BuildNfpCombinationPatterns(const Problem& p,NfpCache& cache,
    const std::vector<NfpPreparedShape>& prepared,const NfpCombinationOptions& o,std::size_t& attempts)
{
    CombinationPreparationSummary preparation(o,attempts);
    std::vector<Pattern> output;
    std::set<std::size_t> selfCoverageSpecifications;
    if(!std::isfinite(p.PartGap)||p.PartGap<0||p.Stocks.size()!=1||prepared.size()!=p.Parts.size()||o.MaxMembers<2||!o.MaxResults||!o.MaxStates||!o.MaxAttempts)
    {preparation.ExitReason="unsupported-combination-query";return output;}
    const auto remainingSeconds=[&]
    {
        if(!o.RemainingPreparationSeconds)return std::numeric_limits<double>::infinity();
        const auto value=o.RemainingPreparationSeconds();return std::isfinite(value)?std::max(0.,value):0.;
    };
    const auto stopped=[&]
    {const bool value=attempts>=o.MaxAttempts||(o.Stop&&o.Stop())||remainingSeconds()<=0;
        preparation.Stopped|=value;return value;};
    if(stopped())return output;
    bool eligibleSelf=false,eligibleMixed=false,eligibleRepeated=false;std::set<std::string> activeMaterials;
    for(std::size_t i=0;i<p.Parts.size();++i)if(p.Parts[i].Quantity&&prepared[i])
    {
        if(!o.SamePartOnly&&!activeMaterials.insert(p.Parts[i].Material).second)eligibleMixed=true;
        if(p.Parts[i].Quantity>=2)
        {
            eligibleRepeated=true;
            const auto kind=Classify(p.Parts[i].Geometry,p.Tolerance).Kind;
            eligibleSelf|=kind!=ShapeKind::Rectangle&&kind!=ShapeKind::Parallelogram;
        }
    }
    if(o.SamePartOnly&&!eligibleRepeated)
    {preparation.ExitReason="no-repeatable-original-demand";return output;}
    const bool prepareSelf=o.PrepareSelfPairs&&!o.SelfPairsOnly&&eligibleSelf&&o.MaxAttempts>=32&&o.MaxResults>=4;
    const bool generalFamily=o.SamePartOnly?o.ExpandAssemblies&&eligibleRepeated:eligibleMixed||!prepareSelf;
    preparation.PreparationFamilies=o.SelfPairsOnly?1:static_cast<std::size_t>(prepareSelf)+
        static_cast<std::size_t>(generalFamily);
    if(o.RemainingPreparationSeconds)preparation.RemainingPreparationSecondsAtStart=remainingSeconds();
    if(prepareSelf)
    {
        auto self=o;self.PrepareSelfPairs=false;self.SelfPairsOnly=true;self.MaxMembers=2;
        self.MaxResults=std::max<std::size_t>(1,o.MaxResults/4);
        self.MaxAttempts=std::min<std::size_t>(512,o.MaxAttempts/4);
        if(o.RemainingPreparationSeconds)
        {
            // Divide only between families that can actually run. The self
            // child gets a local lease; unused time immediately remains with
            // general construction. A local expiry never cancels its sibling.
            const auto available=remainingSeconds();
            preparation.SelfTimeAllowanceSeconds=available/preparation.PreparationFamilies;
            preparation.ReservedGeneralTimeSeconds=available-preparation.SelfTimeAllowanceSeconds;
            const auto remainingFloor=preparation.ReservedGeneralTimeSeconds;
            self.RemainingPreparationSeconds=[remaining=o.RemainingPreparationSeconds,remainingFloor]
            {
                const auto value=remaining();return std::isfinite(value)?std::max(0.,value-remainingFloor):0.;
            };
            self.Stop=[parent=o.Stop,remaining=o.RemainingPreparationSeconds,remainingFloor]
            {
                if(parent&&parent())return true;
                const auto value=remaining();return !std::isfinite(value)||value<=remainingFloor;
            };
        }
        ++preparation.NestedCalls;
        std::size_t used=0;output=BuildNfpCombinationPatterns(p,cache,prepared,self,used);attempts+=used;
        preparation.NestedAttempts+=used;preparation.Retained=output.size();
        preparation.HighestRetainedUtilization=output.empty()?0:Utilization(output.front());
        // This bounded preparation lane already paid for useful repeating
        // originals. Preserve its actual (at most one-quarter) pose slots as
        // search coverage, rather than letting unrelated easy pairs erase it.
        for(const auto& pattern:output)if(pattern.Members.size()==2&&
            pattern.Members[0].PartIndex==pattern.Members[1].PartIndex)
            selfCoverageSpecifications.insert(pattern.Members[0].PartIndex);
        preparation.SelfCoverageSlots=output.size();
        preparation.SelfCoverageSpecifications=selfCoverageSpecifications.size();
        if(stopped())return output;
    }
    Options observation;observation.ConstructionTrace=o.ConstructionTrace;
    observation.ConstructionTraceRoute=o.ConstructionTraceRoute;observation.ConstructionTraceScope=o.ConstructionTraceScope;
    observation.ConstructionObservation=o.ConstructionObservation;
    PairPreparationSummary summary(p,observation,o.SelfPairsOnly?"nfp-self":"nfp-general",stopped);
    const auto observeCertified=[&](const Pattern& pattern)
    {
        ObserveConstruction(observation,ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
        {
            event.Source="preparation.certified-pattern";event.Accepted=true;
            event.Reason="immutable-original-members-certified";event.Attempts=attempts;
            event.PartIndex=pattern.Members.empty()?std::numeric_limits<std::size_t>::max():pattern.Members.front().PartIndex;
            event.Width=pattern.Width;event.Height=pattern.Height;event.MaterialArea=pattern.MaterialArea;
            event.Fill=pattern.Width>0&&pattern.Height>0?pattern.MaterialArea/(pattern.Width*pattern.Height):0;
            event.Measures={{"memberCount",static_cast<double>(pattern.Members.size())},
                {"patternKind",static_cast<double>(pattern.Kind)},{"immutableStoredCoordinates",1.}};
            for(const auto& member:pattern.Members)event.Members.push_back({member.PartIndex,
                std::numeric_limits<std::size_t>::max(),member.Transform,0});
        });
    };
    const auto coveredSelf=[&](const Pattern& pattern)
    {return pattern.Members.size()==2&&pattern.Members[0].PartIndex==pattern.Members[1].PartIndex&&
        selfCoverageSpecifications.contains(pattern.Members[0].PartIndex);};
    const auto retainCertified=[&](const Pattern& pattern)
    {
        // The reservoir never grows past MaxResults. Compare only stored
        // metadata and at most four original members: no geometry rebuild,
        // count reward or saved-envelope proxy participates in retention.
        const double fill=Utilization(pattern);
        std::size_t same=0,worstSame=output.size();
        for(std::size_t i=0;i<output.size();++i)if(SameDemand(pattern,output[i]))
        {
            if(SameStoredGeometry(pattern,output[i]))return;
            ++same;worstSame=i;
        }
        if(coveredSelf(pattern))
        {
            // These fixed coverage slots compete by utilization within their
            // original demand. Replacing a pose never grants extra slots or
            // a quality bonus against mixed candidates at actual placement.
            if(!same||fill<=Utilization(output[worstSame]))return;
            output.erase(output.begin()+worstSame);
        }
        else if(same>=2)
        {
            if(fill<=Utilization(output[worstSame]))return;
            output.erase(output.begin()+worstSame);
        }
        else if(output.size()>=o.MaxResults)
        {
            auto worst=output.size();
            while(worst&&coveredSelf(output[worst-1]))--worst;
            if(!worst||fill<=Utilization(output[worst-1]))return;
            output.erase(output.begin()+worst-1);
        }
        const auto position=std::find_if(output.begin(),output.end(),[&](const Pattern& retained)
            {return fill>Utilization(retained);});
        output.insert(position,pattern);
        preparation.Retained=output.size();
        preparation.HighestRetainedUtilization=Utilization(output.front());
    };
    std::unique_ptr<ClearanceNfpPlacement> clearance;
    if(p.PartGap>0)
    {
        // The caller owns the immutable geometry cache for the complete
        // construction; this local service must not take ownership of it.
        auto shared=std::shared_ptr<NfpCache>(&cache,[](NfpCache*){});
        clearance=std::make_unique<ClearanceNfpPlacement>(p,p.Stocks[0],std::move(shared),stopped);
        if(!clearance->Ready())
        {
            preparation.ExitReason="clearance-preparation-unavailable";
            for(const auto& pattern:output)summary.Retain(pattern);
            return output;
        }
    }
    const auto normalizeAndRepair=[&](State& candidate,std::size_t attemptEnd,const auto& localStop,
        bool certifiedBeforeNormalize=false)
    {
        if(o.SamePartOnly&&!HasSinglePartType(candidate.Value.Members))return false;
        const auto normalize=[&](State& value)
        {
            NormalizePattern(value.Value,p);
            const auto normalizedKind=value.Value.Kind;
            // Evaluation must not translate a second time after the stored
            // normalized coordinates have been certified.
            EvaluateNormalized(value,p,o.RecognizeFilledBasicCarriers);
            if(!FilledBasic(value.Value.Kind))value.Value.Kind=normalizedKind;
        };
        const auto within=[&](const State& value)
        {return value.Value.Width>0&&value.Value.Height>0&&
            value.Value.Width<=p.Stocks[0].Width-2*p.EdgeMargin+p.Tolerance&&
            value.Value.Height<=p.Stocks[0].Height-2*p.EdgeMargin+p.Tolerance;};
        using Conflict=std::pair<std::size_t,std::size_t>;
        const auto failures=[&](const State& value,std::vector<Conflict>& conflicts)
        {
            bool ordinaryClear=true;
            for(std::size_t i=0;i<value.Value.Members.size();++i)
            for(std::size_t j=0;j<i;++j)
            {
                if(o.Stop&&o.Stop())return false;
                bool geometryConflict=false;
                if(!Clear(p,value.Value.Members[i],value.Value.Members[j],&geometryConflict))conflicts.emplace_back(i,j);
                ordinaryClear=ordinaryClear&&!geometryConflict;
            }
            return ordinaryClear;
        };
        std::array<Member,4> certifiedMembers;
        const auto certifiedCount=candidate.Value.Members.size();
        certifiedBeforeNormalize=certifiedBeforeNormalize&&certifiedCount<=certifiedMembers.size();
        if(certifiedBeforeNormalize)std::copy(candidate.Value.Members.begin(),candidate.Value.Members.end(),certifiedMembers.begin());
        normalize(candidate);if(!within(candidate))return false;
        if(certifiedBeforeNormalize&&candidate.Value.Members.size()==certifiedCount)
        {
            // The main growth path has a certified parent plus safePose's
            // complete new-to-parent checks. Reuse that proof only for the
            // identical stored coordinates, including every binary64 bit.
            // Normalization shifts, repair nodes and repeated pairs retain
            // the strict full-pair checks below.
            const auto same=[](double a,double b)
            {return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
            bool unchanged=true;
            for(std::size_t i=0;i<certifiedCount&&unchanged;++i)
            {
                const auto& before=certifiedMembers[i];const auto& after=candidate.Value.Members[i];
                unchanged=before.PartIndex==after.PartIndex&&same(before.Transform.X,after.Transform.X)&&
                    same(before.Transform.Y,after.Transform.Y)&&same(before.Transform.AngleDegrees,after.Transform.AngleDegrees)&&
                    before.Transform.Mirrored==after.Transform.Mirrored;
            }
            if(unchanged)return !(o.Stop&&o.Stop());
        }
        std::vector<Conflict> conflicts;
        const auto ordinaryClear=failures(candidate,conflicts);
        if(conflicts.empty())return ordinaryClear;
        if(!ordinaryClear||localStop()||candidate.Value.Members.size()>4)return false;
        const auto begin=attempts;
        const auto remaining=std::min(o.MaxAttempts,attemptEnd)-std::min(attempts,std::min(o.MaxAttempts,attemptEnd));
        const auto limit=std::min<std::size_t>(32,std::max<std::size_t>(1,remaining/8));
        double scale=std::max({1.,candidate.Value.Width,candidate.Value.Height});
        for(const auto& member:candidate.Value.Members)scale=std::max({scale,std::abs(member.Transform.X),std::abs(member.Transform.Y)});
        const double step=std::max({4*p.Tolerance,4e-8,512*std::numeric_limits<double>::epsilon()*scale});
        struct Node{State Value;std::vector<Conflict> Conflicts;std::size_t Depth=0;};
        std::vector<Node> queue{{candidate,conflicts,0}};
        std::set<std::string> visited;
        const auto exactKey=[](const State& value)
        {
            std::ostringstream key;key.precision(17);
            for(const auto& member:value.Value.Members)key<<member.PartIndex<<':'<<member.Transform.X<<':'<<member.Transform.Y<<':'
                <<member.Transform.AngleDegrees<<':'<<member.Transform.Mirrored<<';';
            return key.str();
        };
        visited.insert(exactKey(candidate));
        const auto budgetStop=[&]{return localStop()||attempts>=std::min(o.MaxAttempts,attemptEnd)||attempts-begin>=limit;};
        while(!queue.empty()&&!budgetStop())
        {
            std::stable_sort(queue.begin(),queue.end(),[](const Node& a,const Node& b)
            {return std::tuple(a.Conflicts.size(),a.Depth)<std::tuple(b.Conflicts.size(),b.Depth);});
            auto node=std::move(queue.front());queue.erase(queue.begin());
            const auto [first,second]=node.Conflicts.front();
            std::array<std::vector<Point>,2> moves;
            const std::array<std::size_t,2> movingIndices{first,second};
            for(std::size_t order=0;order<2&&!budgetStop();++order)
            {
                const auto movingIndex=movingIndices[order],fixedIndex=movingIndices[1-order];
                const auto& moving=node.Value.Value.Members[movingIndex];const auto& fixed=node.Value.Value.Members[fixedIndex];
                NfpBuildOptions build;build.Cancel=budgetStop;
                if(o.RemainingPreparationSeconds)build.TimeLimitSeconds=std::min(build.TimeLimitSeconds,remainingSeconds());
                ++preparation.RegionRequests;
                if(fixed.PartIndex!=moving.PartIndex)++preparation.CrossPartRegionRequests;
                const auto lookup=clearance?clearance->ContactRegion(fixed.PartIndex,fixed.Transform.AngleDegrees,
                    moving.PartIndex,moving.Transform.AngleDegrees,build,false,fixed.Transform.Mirrored,moving.Transform.Mirrored):
                    cache.Get(p.Parts[fixed.PartIndex].Geometry,fixed.Transform.AngleDegrees,p.Parts[moving.PartIndex].Geometry,
                        moving.Transform.AngleDegrees,build,fixed.Transform.Mirrored,moving.Transform.Mirrored);
                if(lookup.State!=NfpStatus::Ready||!lookup.Region)++preparation.UnavailableRegions;
                struct Contact{Point Position,Normal;double Distance=0;};std::vector<Contact> contacts;
                const auto normal=[](Point a,Point b)
                {const auto dx=b.X-a.X,dy=b.Y-a.Y,length=std::hypot(dx,dy);return length>0?Point{dy/length,-dx/length}:Point{};};
                if(lookup.State==NfpStatus::Ready&&lookup.Region)
                for(const auto& ring:lookup.Region->Loops)for(std::size_t i=0;i<ring.size()&&!budgetStop();++i)
                {
                    auto a=Transform(ring[i],lookup.Transform),b=Transform(ring[(i+1)%ring.size()],lookup.Transform);
                    a.X+=fixed.Transform.X;a.Y+=fixed.Transform.Y;b.X+=fixed.Transform.X;b.Y+=fixed.Transform.Y;
                    const double dx=b.X-a.X,dy=b.Y-a.Y,length2=dx*dx+dy*dy;
                    if(length2<=0)continue;
                    const auto t=std::clamp(((moving.Transform.X-a.X)*dx+(moving.Transform.Y-a.Y)*dy)/length2,0.,1.);
                    Point position{a.X+t*dx,a.Y+t*dy};const auto distance=std::hypot(position.X-moving.Transform.X,position.Y-moving.Transform.Y);
                    if(distance>8*step)continue;
                    auto outward=normal(a,b);
                    if(t<1e-9)
                    {
                        auto previous=Transform(ring[(i+ring.size()-1)%ring.size()],lookup.Transform);
                        previous.X+=fixed.Transform.X;previous.Y+=fixed.Transform.Y;
                        const auto adjacent=normal(previous,a);outward.X+=adjacent.X;outward.Y+=adjacent.Y;
                    }
                    else if(1-t<1e-9)
                    {
                        auto following=Transform(ring[(i+2)%ring.size()],lookup.Transform);
                        following.X+=fixed.Transform.X;following.Y+=fixed.Transform.Y;
                        const auto adjacent=normal(b,following);outward.X+=adjacent.X;outward.Y+=adjacent.Y;
                    }
                    const auto length=std::hypot(outward.X,outward.Y);if(length<=0)continue;
                    outward.X/=length;outward.Y/=length;contacts.push_back({position,outward,distance});
                }
                std::stable_sort(contacts.begin(),contacts.end(),[](const Contact& a,const Contact& b){return a.Distance<b.Distance;});
                if(contacts.size()>3)contacts.resize(3);
                for(const double factor:{1.,4.})for(const auto& contact:contacts)for(const double side:{1.,-1.})
                    moves[order].push_back({contact.Position.X-moving.Transform.X+side*factor*step*contact.Normal.X,
                        contact.Position.Y-moving.Transform.Y+side*factor*step*contact.Normal.Y});
                // Axis events remain useful when a contact vertex or its
                // quantized segment cannot supply a unique free-side normal.
                for(const double factor:{1.,4.})
                {moves[order].push_back({factor*step,0});moves[order].push_back({-factor*step,0});
                    moves[order].push_back({0,factor*step});moves[order].push_back({0,-factor*step});}
            }
            const auto nodeBegin=attempts;
            const auto nodeAllowance=std::max<std::size_t>(2,(limit-(attempts-begin))/
                std::max<std::size_t>(1,candidate.Value.Members.size()-node.Depth));
            // Reserve repair work for a queued continuation; a plateau move
            // may release one contact before the next member can be separated.
            for(std::size_t round=0;!budgetStop()&&attempts-nodeBegin<nodeAllowance;++round)
            {
                bool any=false;
                for(std::size_t order=0;order<2&&!budgetStop()&&attempts-nodeBegin<nodeAllowance;++order)
                {
                    if(round>=moves[order].size())continue;any=true;
                    const auto shift=moves[order][round];if(std::hypot(shift.X,shift.Y)>12*step)continue;
                    auto trial=node.Value;auto& member=trial.Value.Members[movingIndices[order]];
                    member.Transform.X+=shift.X;member.Transform.Y+=shift.Y;
                    ++attempts;normalize(trial);if(!within(trial)||!visited.insert(exactKey(trial)).second)continue;
                    std::vector<Conflict> remainingConflicts;
                    if(!failures(trial,remainingConflicts))continue;
                    if(remainingConflicts.empty()){candidate=std::move(trial);return true;}
                    if(node.Depth+1<candidate.Value.Members.size()&&remainingConflicts.size()<=node.Conflicts.size()&&queue.size()<8)
                        queue.push_back({std::move(trial),std::move(remainingConflicts),node.Depth+1});
                }
                if(!any)break;
            }
        }
        return false;
    };
    const auto usefulPair=[&](const State& state)
    {
        if(state.Value.Members.size()!=2)return false;
        const auto scale=std::max(1.,state.Value.Width+state.Value.Height);
        // Admission asks whether the two original posed carriers interlock,
        // independently of whether they share an inventory specification.
        // Final ranking still compares actual net-material utilization only.
        return state.SavedFraction>std::max(1e-10,8*p.Tolerance/scale);
    };
    const auto mirroredProposals=[&](std::size_t index)
    {return p.Parts[index].AllowMirror&&o.OrientationProposals==OrientationProposalPolicy::AllowedReflections;};
    std::vector<std::vector<Pose>> angles(p.Parts.size());
    for(std::size_t i=0;i<p.Parts.size();++i)if(p.Parts[i].Quantity&&prepared[i])
    {
        if(o.SamePartOnly&&p.Parts[i].Quantity<2)
        {if(auto* record=summary.For(i))record->SkipReason="insufficient-self-demand";continue;}
        angles[i]=CandidateOrientations(p.Parts[i],p.Tolerance,o.OrientationProposals);
        if(auto* record=summary.For(i))record->PlannedAngles=angles[i].size();
        if(o.SelfPairsOnly)
        {
            const auto kind=Classify(p.Parts[i].Geometry,p.Tolerance).Kind;
            if(p.Parts[i].Quantity<2||kind==ShapeKind::Rectangle||kind==ShapeKind::Parallelogram)
            {if(auto* record=summary.For(i))record->SkipReason=p.Parts[i].Quantity<2?"insufficient-self-demand":"filled-basic-original";
                angles[i].clear();continue;}
            // A pair's contacts depend on its relative angle. Start with the
            // smallest individual carrier and its legal opposite first, then
            // legal relative quarter turns. A reflected quarter turn is not
            // equivalent to mirroring the whole already-built pair. This
            // remains at most eight poses, not every free-angle sample.
            const auto originalBox=Bounds(p.Parts[i].Geometry);
            const double areaQuantum=std::max(1.,originalBox.Width*originalBox.Height)*1e-10;
            std::stable_sort(angles[i].begin(),angles[i].end(),[&](Pose a,Pose b)
            {
                const auto x=Bounds(p.Parts[i].Geometry,a),y=Bounds(p.Parts[i].Geometry,b);
                const auto ax=std::llround(x.Width*x.Height/areaQuantum),ay=std::llround(y.Width*y.Height/areaQuantum);
                if(ax!=ay)return ax<ay;
                if(x.Height!=y.Height)return x.Height<y.Height;
                return std::tie(a.AngleDegrees,a.Mirrored)<std::tie(b.AngleDegrees,b.Mirrored);
            });
            std::vector<Pose> firsts,relatives;
            for(const bool mirrored:{false,true})
            {
                if(mirrored&&!mirroredProposals(i))continue;
                const auto found=std::find_if(angles[i].begin(),angles[i].end(),[&](Pose pose){return pose.Mirrored==mirrored;});
                if(found==angles[i].end())continue;
                firsts.push_back(*found);
            }
            for(const double turn:{180.,90.,270.})for(auto relative:firsts)
            {
                relative.AngleDegrees=NormalizeAngle(relative.AngleDegrees+turn);
                if(PoseAllowed(p.Parts[i],relative,p.Tolerance))relatives.push_back(relative);
            }
            angles[i]=std::move(firsts);angles[i].insert(angles[i].end(),relatives.begin(),relatives.end());
        }
    }
    std::vector<Pattern> basicCertificates;
    if(!o.SamePartOnly&&!o.SelfPairsOnly&&!stopped())
    {
        // Establish useful mixed originals before the beam's expensive angle /
        // contact refinements. One compact seed per legal parity gets a nearby
        // demand; rounds visit one moving pose per seed and certify it now.
        // Similarity orders work only. The immutable result pool still uses U.
        struct ShapeWork{double Area=0,Long=0,Short=0,Perimeter=0;std::size_t Vertices=0;};
        std::vector<ShapeWork> work(p.Parts.size());std::map<std::string,std::size_t> materialSpecifications;
        for(std::size_t i=0;i<p.Parts.size();++i)if(!angles[i].empty())
        {
            const auto box=Bounds(p.Parts[i].Geometry);auto& value=work[i];value.Area=Area(p.Parts[i].Geometry);
            value.Long=std::max(box.Width,box.Height);value.Short=std::min(box.Width,box.Height);
            const auto& ring=p.Parts[i].Geometry.Outer;value.Vertices=ring.size();
            ++materialSpecifications[p.Parts[i].Material];
            for(std::size_t k=0;k<ring.size();++k)value.Perimeter+=std::hypot(ring[k].X-ring[(k+1)%ring.size()].X,
                ring[k].Y-ring[(k+1)%ring.size()].Y);
        }
        const auto difference=[](double a,double b){return std::abs(a-b)/std::max({1e-30,a,b});};
        const auto similarity=[&](std::size_t a,std::size_t b)
        {return std::tuple(difference(work[a].Area,work[b].Area),difference(work[a].Long,work[b].Long),
            difference(work[a].Short,work[b].Short),difference(work[a].Perimeter,work[b].Perimeter),
            work[a].Vertices*work[b].Vertices);};
        const auto compact=[&](std::size_t i,bool mirrored)->std::optional<Pose>
        {
            std::optional<Pose> best;std::tuple<long long,long long,long long,double> key;
            const double areaUnit=std::max(1.,work[i].Long*work[i].Short)*1e-10;
            const double linearUnit=std::max(p.Tolerance,std::max(1.,work[i].Long)*1e-10);
            for(const auto pose:angles[i])if(pose.Mirrored==mirrored)
            {
                const auto box=Bounds(p.Parts[i].Geometry,pose);
                const auto candidate=std::tuple(std::llround(box.Width*box.Height/areaUnit),
                    std::llround(box.Height/linearUnit),std::llround(box.Width/linearUnit),pose.AngleDegrees);
                if(!best||candidate<key){best=pose;key=candidate;}
            }
            return best;
        };
        struct SeedDescriptor{std::size_t Part;Pose Orientation;};std::vector<SeedDescriptor> descriptors;
        for(std::size_t i=0;i<p.Parts.size()&&!stopped();++i)
            if(!angles[i].empty()&&materialSpecifications[p.Parts[i].Material]>1)
                for(const bool reflected:{false,true})if(const auto orientation=compact(i,reflected))
                    descriptors.push_back({i,*orientation});
        // Bound anchors before allocating their N-entry demand vectors or
        // scanning N possible partners. Descriptors contain no search state.
        std::stable_sort(descriptors.begin(),descriptors.end(),[&](const auto& a,const auto& b)
        {return std::tuple(-work[a.Part].Area,a.Part,a.Orientation.Mirrored)<
            std::tuple(-work[b.Part].Area,b.Part,b.Orientation.Mirrored);});
        if(descriptors.size()>o.MaxStates){descriptors.resize(o.MaxStates);preparation.CoverageTruncated=true;}
        struct BasicSeed{State Value;std::size_t Partner;std::vector<Pose> Moving;};
        std::vector<BasicSeed> seeds;seeds.reserve(descriptors.size());
        for(const auto& descriptor:descriptors)
        {
            if(stopped())break;const auto i=descriptor.Part;const auto orientation=descriptor.Orientation;
            std::optional<std::size_t> partner;
            for(std::size_t j=0;j<p.Parts.size();++j)
                if(j!=i&&!angles[j].empty()&&p.Parts[j].Material==p.Parts[i].Material&&
                    (!partner||similarity(i,j)<similarity(i,*partner)))partner=j;
            if(!partner)continue;
                BasicSeed seed;seed.Partner=*partner;seed.Value.Demand.resize(p.Parts.size());seed.Value.Demand[i]=1;
                seed.Value.Value.Members={{i,orientation}};Evaluate(seed.Value,p,o.RecognizeFilledBasicCarriers);
                std::array<std::vector<Pose>,2> families;
                for(const bool movingMirror:{false,true})
                {
                    auto& family=families[movingMirror?1:0];
                    const auto add=[&](Pose pose)
                    {
                        if(pose.Mirrored!=movingMirror||!PoseAllowed(p.Parts[*partner],pose,p.Tolerance))return;
                        if(std::none_of(family.begin(),family.end(),[&](Pose old)
                            {const auto delta=std::abs(NormalizeAngle(old.AngleDegrees)-NormalizeAngle(pose.AngleDegrees));
                                return std::min(delta,360-delta)<=p.Tolerance;}))family.push_back(pose);
                    };
                    const auto first=std::find_if(angles[*partner].begin(),angles[*partner].end(),
                        [&](Pose pose){return pose.Mirrored==movingMirror;});
                    if(first==angles[*partner].end())continue;add(*first);
                    const auto target=NormalizeAngle(orientation.AngleDegrees+180);
                    const auto angleDistance=[&](Pose pose)
                    {const auto delta=std::abs(NormalizeAngle(pose.AngleDegrees)-target);return std::min(delta,360-delta);};
                    const auto opposite=std::min_element(angles[*partner].begin(),angles[*partner].end(),[&](Pose a,Pose b)
                    {return std::tuple(a.Mirrored!=movingMirror,angleDistance(a),a.AngleDegrees)<
                        std::tuple(b.Mirrored!=movingMirror,angleDistance(b),b.AngleDegrees);});
                    if(opposite!=angles[*partner].end())add(*opposite);
                    if(const auto tight=compact(*partner,movingMirror))add(*tight);
                    const auto reverse=NormalizeAngle(first->AngleDegrees+180);
                    for(const auto pose:angles[*partner])if(pose.Mirrored==movingMirror)
                    {const auto delta=std::abs(NormalizeAngle(pose.AngleDegrees)-reverse);
                        if(std::min(delta,360-delta)<=p.Tolerance){add(pose);break;}}
                }
                for(std::size_t rank=0;;++rank)
                {bool any=false;for(const auto& family:families)if(rank<family.size())
                    {seed.Moving.push_back(family[rank]);any=true;}if(!any)break;}
                if(!seed.Moving.empty())seeds.push_back(std::move(seed));
        }
        std::stable_sort(seeds.begin(),seeds.end(),[&](const BasicSeed& a,const BasicSeed& b)
        {return similarity(a.Value.Value.Members[0].PartIndex,a.Partner)<similarity(b.Value.Value.Members[0].PartIndex,b.Partner);});
        preparation.BasicSeeds=seeds.size();
        std::size_t pending=0;for(const auto& seed:seeds)pending+=seed.Moving.size();
        struct BasicProposal{Pose Transform;double Fill,Envelope;Point Outward;};
        const auto better=[](const BasicProposal& a,const BasicProposal& b)
        {return std::tuple(-a.Fill,a.Envelope,a.Transform.AngleDegrees,a.Transform.Mirrored,a.Transform.Y,a.Transform.X)<
            std::tuple(-b.Fill,b.Envelope,b.Transform.AngleDegrees,b.Transform.Mirrored,b.Transform.Y,b.Transform.X);};
        for(std::size_t round=0;pending&&!stopped();++round)
        for(const auto& seed:seeds)
        {
            if(stopped())break;if(round>=seed.Moving.size())continue;
            const auto allowance=std::max<std::size_t>(1,(o.MaxAttempts-attempts)/pending);--pending;
            const auto end=std::min(o.MaxAttempts,attempts+allowance);
            const auto localStop=[&]{return stopped()||attempts>=end;};
            const auto& fixed=seed.Value.Value.Members[0];const auto moving=seed.Moving[round];const auto j=seed.Partner;
            NfpBuildOptions build;build.Cancel=localStop;
            if(o.RemainingPreparationSeconds)build.TimeLimitSeconds=std::min(build.TimeLimitSeconds,remainingSeconds());
            ++preparation.RegionRequests;++preparation.BasicQueries;
            if(fixed.PartIndex!=j)++preparation.CrossPartRegionRequests;
            ObserveConstruction(observation,ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
            {event.Source="preparation.basic-pair";event.Reason="similar-demand-compact-seed-bounded-boundary-events";
                event.Members={{fixed.PartIndex,std::numeric_limits<std::size_t>::max(),fixed.Transform,0},
                    {j,std::numeric_limits<std::size_t>::max(),moving,0}};});
            const auto nfp=clearance?clearance->ContactRegion(fixed.PartIndex,fixed.Transform.AngleDegrees,j,moving.AngleDegrees,
                build,false,fixed.Transform.Mirrored,moving.Mirrored):cache.Get(p.Parts[fixed.PartIndex].Geometry,
                    fixed.Transform.AngleDegrees,p.Parts[j].Geometry,moving.AngleDegrees,build,fixed.Transform.Mirrored,moving.Mirrored);
            if(nfp.State!=NfpStatus::Ready||!nfp.Region){++preparation.UnavailableRegions;continue;}
            const auto movingBox=Bounds(p.Parts[j].Geometry,moving);std::vector<BasicProposal> proposals;
            const auto keep=std::max<std::size_t>(8,std::min<std::size_t>(128,allowance));
            std::set<std::pair<long long,long long>> seenPositions;const auto unit=std::max(1e-8,p.Tolerance*.1);
            for(const auto& loop:nfp.Region->Loops)
            {
                const auto stride=std::max<std::size_t>(1,(loop.size()+255)/256);
                if(stride>1)preparation.CoverageTruncated=true;
                for(std::size_t edge=0;edge<loop.size()&&!localStop();edge+=stride)
                {
                    const auto a=Transform(loop[edge],nfp.Transform),b=Transform(loop[(edge+1)%loop.size()],nfp.Transform);
                    std::vector<double> cuts{0,1};
                    const auto cut=[&](double value,double first,double last)
                    {if(std::abs(last-first)<=p.Tolerance)return;const auto t=(value-first)/(last-first);if(t>0&&t<1)cuts.push_back(t);};
                    cut(-movingBox.X,a.X+fixed.Transform.X,b.X+fixed.Transform.X);
                    cut(seed.Value.Value.Width-movingBox.X-movingBox.Width,a.X+fixed.Transform.X,b.X+fixed.Transform.X);
                    cut(-movingBox.Y,a.Y+fixed.Transform.Y,b.Y+fixed.Transform.Y);
                    cut(seed.Value.Value.Height-movingBox.Y-movingBox.Height,a.Y+fixed.Transform.Y,b.Y+fixed.Transform.Y);
                    const auto length=std::hypot(b.X-a.X,b.Y-a.Y);const Point outward=length>0?Point{(b.Y-a.Y)/length,(a.X-b.X)/length}:Point{};
                    for(const auto t:cuts)
                    {
                        if(localStop())break;++preparation.Enumerated;
                        const Pose pose{a.X+(b.X-a.X)*t+fixed.Transform.X,a.Y+(b.Y-a.Y)*t+fixed.Transform.Y,moving.AngleDegrees,moving.Mirrored};
                        if(!seenPositions.emplace(std::llround(pose.X/unit),std::llround(pose.Y/unit)).second)continue;
                        ++preparation.Proposals;
                        const auto x=movingBox.X+pose.X,y=movingBox.Y+pose.Y;
                        const auto width=std::max(seed.Value.Value.Width,x+movingBox.Width)-std::min(0.,x);
                        const auto height=std::max(seed.Value.Value.Height,y+movingBox.Height)-std::min(0.,y);
                        const auto material=seed.Value.Value.MaterialArea+work[j].Area,envelope=width*height;
                        if(width<=0||height<=0||width>p.Stocks[0].Width-2*p.EdgeMargin+p.Tolerance||
                            height>p.Stocks[0].Height-2*p.EdgeMargin+p.Tolerance||
                            material>envelope+8*p.Tolerance*(width+height)+64*std::numeric_limits<double>::epsilon()*std::max(material,envelope))continue;
                        const auto fill=material/envelope;if(fill<o.MinimumIntermediateFill)continue;
                        proposals.push_back({pose,fill,envelope,outward});
                        if(proposals.size()>2*keep){std::stable_sort(proposals.begin(),proposals.end(),better);proposals.resize(keep);}
                    }
                }
            }
            std::stable_sort(proposals.begin(),proposals.end(),better);
            for(const auto& proposal:proposals)
            {
                if(localStop())break;auto pose=proposal.Transform;bool safe=false;
                for(const double factor:{0.,1.,4.})
                {
                    if(localStop())break;pose=proposal.Transform;
                    const auto retreat=std::max({4*p.Tolerance,4e-8,512*std::numeric_limits<double>::epsilon()*
                        std::max({1.,seed.Value.Value.Width,seed.Value.Value.Height,std::abs(pose.X),std::abs(pose.Y)})});
                    pose.X+=factor*retreat*proposal.Outward.X;pose.Y+=factor*retreat*proposal.Outward.Y;
                    ++attempts;++preparation.BasicExact;if(Clear(p,fixed,{j,pose})){safe=true;break;}
                }
                if(!safe)continue;auto candidate=seed.Value;candidate.Value.Members.push_back({j,pose});++candidate.Demand[j];
                if(!normalizeAndRepair(candidate,end,localStop,true))continue;
                ++preparation.Certified;
                if(candidate.Fill<o.MinimumIntermediateFill||!(candidate.Fill>=o.MinimumFinalFill||usefulPair(candidate)))continue;
                basicCertificates.push_back(candidate.Value);
                observeCertified(candidate.Value);retainCertified(candidate.Value);
                break; // Further poses/angles remain in the ordinary expansion.
            }
        }
        if(pending)preparation.CoverageTruncated=true;
    }
    std::vector<State> beam;
    for(std::size_t round=0;(o.ExpandAssemblies||o.SelfPairsOnly)&&!stopped();++round)
    {
        bool any=false;
        for(std::size_t i=0;i<angles.size()&&beam.size()<o.MaxStates;++i)
        for(std::size_t parity=0;parity<(mirroredProposals(i)?2u:1u)&&beam.size()<o.MaxStates;++parity)
        {
            const auto ai=mirroredProposals(i)?2*round+parity:round;
            if(ai>=angles[i].size())continue;
            if(o.SelfPairsOnly&&round>0)continue;
            any=true;State s;s.Demand.resize(p.Parts.size());s.Demand[i]=1;s.Value.Members.push_back({i,angles[i][ai]});
            Evaluate(s,p,o.RecognizeFilledBasicCarriers);if(s.Value.Width<=0||s.Value.Height<=0)continue;
            beam.push_back(std::move(s));
        }
        if(!any||beam.size()>=o.MaxStates||o.SelfPairsOnly)break;
    }
    std::set<std::string> seen;
    // Leave a small explicit budget for repeating a dense pair as a 2-by-2 unit.
    const auto repeatReserve=std::min<std::size_t>(128,o.MaxAttempts/10);
    const auto mainLimit=o.MaxAttempts-repeatReserve;
    for(std::size_t depth=2;depth<=std::min<std::size_t>(4,o.MaxMembers)&&!beam.empty()&&!stopped();++depth)
    {
        const auto maxDepth=std::min<std::size_t>(4,o.MaxMembers);
        // Reserve work for later depths: exhausting the entire budget on
        // pairs prevents sparse intermediate states from ever closing.
        const auto stageBudget=(mainLimit>attempts?mainLimit-attempts:0)/(maxDepth-depth+1);
        const auto stageEnd=attempts+std::max<std::size_t>(1,stageBudget);
        const auto stageStopped=[&]{return stopped()||attempts>=mainLimit||attempts>=stageEnd;};
        std::vector<State> next,finalCandidates;
        for(const auto& state:beam)
        {
            if(stageStopped())break;
            // Give each seed a chance, and try its own specification first.
            // Otherwise early input specifications consume the entire stage.
            const auto stateEnd=std::min(stageEnd,attempts+std::max<std::size_t>(1,stageBudget/beam.size()));
            const auto stateStopped=[&]{return stageStopped()||attempts>=stateEnd;};
            const auto first=state.Value.Members.front().PartIndex;
            const auto material=p.Parts[first].Material;
            struct NextSpec{std::size_t Index;bool Filler,Bridge;double Area;};std::vector<NextSpec> choices;
            const double gapArea=state.Value.Width*state.Value.Height-state.Value.MaterialArea;
            std::size_t joinedLarge=0;
            if(!state.BridgeAnchors.empty())for(const auto& member:state.Value.Members)
            {
                const auto& part=p.Parts[member.PartIndex];const auto box=Bounds(part.Geometry,member.Transform);
                if(Area(part.Geometry)>state.BridgeAnchorArea&&
                    std::abs((state.Vertical?box.Height:box.Width)-state.LongSpan)<=p.Tolerance)++joinedLarge;
            }
            for(std::size_t pass=0;pass<((o.SamePartOnly||o.SelfPairsOnly)?1:p.Parts.size());++pass)
            {
                const auto j=(first+pass)%p.Parts.size();
                if((o.SamePartOnly||o.SelfPairsOnly)&&j!=first)continue;
                if(!prepared[j]||p.Parts[j].Material!=material||state.Demand[j]>=p.Parts[j].Quantity)continue;
                const auto area=Area(p.Parts[j].Geometry);bool filler=false,bridge=false;
                if(state.Value.Members.size()>=2&&state.Carrier.empty()&&area<=gapArea+p.Tolerance)
                    for(const auto orientation:angles[j])
                    {
                        const auto box=Bounds(p.Parts[j].Geometry,orientation);
                        if(box.Width<=state.Value.Width+p.Tolerance&&box.Height<=state.Value.Height+p.Tolerance){filler=true;break;}
                    }
                if(state.Carrier.empty()&&!state.BridgeAnchors.empty()&&joinedLarge==1&&area>state.BridgeAnchorArea)
                    for(const auto orientation:angles[j])
                    {
                        const auto box=Bounds(p.Parts[j].Geometry,orientation);
                        if(std::abs((state.Vertical?box.Height:box.Width)-state.LongSpan)<=p.Tolerance){bridge=true;break;}
                    }
                if(j!=first)++preparation.CrossPartBeamChoices;
                choices.push_back({j,filler,bridge,area});
            }
            // Existing empty space is only a proposal priority. A small part
            // still needs a real NFP pose and original-material legality.
            std::stable_sort(choices.begin(),choices.end(),[&](const auto& a,const auto& b)
            {if(depth==2&&preparation.NestedCalls&&(a.Index==first)!=(b.Index==first))return a.Index!=first;
                if(a.Bridge!=b.Bridge)return a.Bridge;if(a.Filler!=b.Filler)return a.Filler;
                if((a.Filler||a.Bridge)&&a.Area!=b.Area)return a.Area>b.Area;return false;});
            std::size_t remainingWeight=0;for(const auto& c:choices)remainingWeight+=c.Bridge?4:c.Filler?2:1;
            for(const auto choice:choices)
            {
                if(stateStopped())break;const auto j=choice.Index;
                const bool selfPair=state.Value.Members.size()==1&&state.Value.Members[0].PartIndex==j;
                auto* pairRecord=selfPair?summary.For(j):nullptr;
                PairPreparationSummary::WorkTime pairTime(pairRecord);if(pairRecord)++pairRecord->Entered;
                const std::size_t weight=choice.Bridge?4:choice.Filler?2:1;
                const auto partEnd=attempts+std::max<std::size_t>(1,(stateEnd-attempts)*weight/remainingWeight);
                remainingWeight-=weight;
                const auto partStopped=[&]{return stateStopped()||attempts>=partEnd;};
                std::vector<std::size_t> anchors(state.Value.Members.size());std::iota(anchors.begin(),anchors.end(),0);
                if(choice.Bridge)std::stable_sort(anchors.begin(),anchors.end(),[&](auto a,auto b)
                {return (std::find(state.BridgeAnchors.begin(),state.BridgeAnchors.end(),a)!=state.BridgeAnchors.end())>
                    (std::find(state.BridgeAnchors.begin(),state.BridgeAnchors.end(),b)!=state.BridgeAnchors.end());});
                else if(choice.Filler)std::stable_sort(anchors.begin(),anchors.end(),[&](auto a,auto b)
                {return Area(p.Parts[state.Value.Members[a].PartIndex].Geometry)>
                    Area(p.Parts[state.Value.Members[b].PartIndex].Geometry);});
                struct Proposal{Pose Transform;double Fill,Envelope;Point Outward;};
                std::vector<Proposal> ranked;
                const auto proposalLess=[](const Proposal& a,const Proposal& b)
                {return std::tuple(-a.Fill,a.Envelope,a.Transform.AngleDegrees,a.Transform.Mirrored,a.Transform.X,a.Transform.Y)<
                    std::tuple(-b.Fill,b.Envelope,b.Transform.AngleDegrees,b.Transform.Mirrored,b.Transform.X,b.Transform.Y);};
                const auto retained=std::max<std::size_t>(8,std::min<std::size_t>(128,partEnd-attempts));
                struct ContactPool{std::vector<Proposal> First,Best;};
                std::map<std::pair<std::size_t,std::size_t>,ContactPool> contacts;
                const auto contactGroups=angles[j].size()*anchors.size();
                const auto groupStride=std::max<std::size_t>(1,contactGroups/retained+(contactGroups%retained!=0));
                const auto groupQuota=std::max<std::size_t>(2,std::min<std::size_t>(16,
                    retained/std::max<std::size_t>(1,std::min(retained,contactGroups))));
                const auto firstQuota=(groupQuota+1)/2,bestQuota=groupQuota-firstQuota;
                // Every allowed relative angle contributes cheap contact events
                // before any exact validation consumes this specification's
                // allowance. A one-attempt allowance must not mean first angle,
                // first NFP edge or first encountered parallel rectangle.
                for(std::size_t ai=0;ai<angles[j].size();++ai)
                {
                    if(partStopped())break;
                    const auto orientation=angles[j][ai];const auto angle=orientation.AngleDegrees;
                    const auto movingBounds=Bounds(p.Parts[j].Geometry,orientation);
                    for(std::size_t anchorRank=0;anchorRank<anchors.size();++anchorRank)
                    {
                        if(partStopped())break;
                        const auto anchorIndex=anchors[anchorRank];
                        const auto& anchor=state.Value.Members[anchorIndex];
                        PairPreparationSummary::Angle(pairRecord,anchor.Transform.AngleDegrees,angle);
                        if(p.Parts[anchor.PartIndex].AllowMirror||p.Parts[j].AllowMirror)
                            ObserveConstruction(observation,ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
                            {
                                event.Source="preparation.orientation-pair";event.Reason="bounded-contact-region-query";
                                event.PartIndex=j;event.Members={{anchor.PartIndex,std::numeric_limits<std::size_t>::max(),anchor.Transform,0},
                                    {j,std::numeric_limits<std::size_t>::max(),orientation,0}};
                                event.Measures={{"stationaryMirrored",anchor.Transform.Mirrored?1.:0.},
                                    {"movingMirrored",orientation.Mirrored?1.:0.}};
                            });
                        if(pairRecord)++pairRecord->Queries;
                        NfpBuildOptions build;build.Cancel=partStopped;
                        if(o.RemainingPreparationSeconds)build.TimeLimitSeconds=std::min(build.TimeLimitSeconds,remainingSeconds());
                        ++preparation.RegionRequests;
                        if(anchor.PartIndex!=j)++preparation.CrossPartRegionRequests;
                        const auto nfp=clearance?
                            clearance->ContactRegion(anchor.PartIndex,anchor.Transform.AngleDegrees,j,angle,build,false,
                                anchor.Transform.Mirrored,orientation.Mirrored):
                            cache.Get(p.Parts[anchor.PartIndex].Geometry,anchor.Transform.AngleDegrees,p.Parts[j].Geometry,
                                angle,build,anchor.Transform.Mirrored,orientation.Mirrored);
                        if(nfp.State!=NfpStatus::Ready||!nfp.Region)
                        {++preparation.UnavailableRegions;if(pairRecord)++pairRecord->QueryFailed;continue;}
                        const auto vertexDifferences=PrepareNfpVertexPairDifferences(
                            p.Parts[anchor.PartIndex].Geometry.Outer,
                            {0,0,anchor.Transform.AngleDegrees,anchor.Transform.Mirrored},
                            p.Parts[j].Geometry.Outer,orientation,2048,partStopped);
                        if(!vertexDifferences.Complete)break;
                        std::set<std::pair<long long,long long>> proposals;
                        const double proposalUnit=std::max(1e-8,p.Tolerance*.1);
                        // Bound contact enumeration independently of exact
                        // attempts, and sample the complete loop when a large
                        // NFP has more edges than this optional contact search.
                        for(int contactPass=0;contactPass<3&&!partStopped();++contactPass)
                        for(const auto& loop:nfp.Region->Loops)for(std::size_t edge=0;edge<loop.size();++edge)
                        {
                            if(partStopped())break;
                            constexpr std::size_t maxEdges=256;
                            if(loop.size()>maxEdges&&edge%((loop.size()+maxEdges-1)/maxEdges))continue;
                            const auto a=Transform(loop[edge],nfp.Transform);
                            const auto b=Transform(loop[(edge+1)%loop.size()],nfp.Transform);
                            std::vector<double> cuts;
                            // Add every bounding-box regime change along this
                            // NFP segment for the entire existing assembly.
                            const auto addCut=[&](double v,double start,double end)
                            {
                                if(std::abs(end-start)<=p.Tolerance)return;
                                const auto t=(v-start)/(end-start);if(t>0&&t<1)cuts.push_back(t);
                            };
                            if(contactPass==0)
                            {
                                addCut(-movingBounds.X,a.X+anchor.Transform.X,b.X+anchor.Transform.X);
                                addCut(state.Value.Width-movingBounds.X-movingBounds.Width,a.X+anchor.Transform.X,b.X+anchor.Transform.X);
                                addCut(-movingBounds.Y,a.Y+anchor.Transform.Y,b.Y+anchor.Transform.Y);
                                addCut(state.Value.Height-movingBounds.Y-movingBounds.Height,a.Y+anchor.Transform.Y,b.Y+anchor.Transform.Y);
                            }
                            else if(contactPass==1)cuts={0,.5};
                            // The best contact may lie inside an NFP edge, where
                            // vertices align along an axis, rather than at its end.
                            if(contactPass==2&&!AppendNfpVertexPairIntervalCuts(vertexDifferences,a,b,p.Tolerance,cuts,partStopped))break;
                            std::sort(cuts.begin(),cuts.end());
                            cuts.erase(std::unique(cuts.begin(),cuts.end(),[](double x,double y){return std::abs(x-y)<1e-9;}),cuts.end());
                            for(const auto t:cuts)
                            {
                            if(partStopped())break;
                            ++preparation.Enumerated;
                            const Pose pose{a.X+(b.X-a.X)*t+anchor.Transform.X,a.Y+(b.Y-a.Y)*t+anchor.Transform.Y,angle,orientation.Mirrored};
                            if(!proposals.emplace(std::llround(pose.X/proposalUnit),std::llround(pose.Y/proposalUnit)).second)continue;
                            ++preparation.Proposals;
                            if(pairRecord)++pairRecord->Generated;
                            const auto x=movingBounds.X+pose.X,y=movingBounds.Y+pose.Y;
                            const auto width=std::max(state.Value.Width,x+movingBounds.Width)-std::min(0.,x);
                            const auto height=std::max(state.Value.Height,y+movingBounds.Height)-std::min(0.,y);
                            if(width<=0||height<=0||width>p.Stocks[0].Width-2*p.EdgeMargin+p.Tolerance||
                                height>p.Stocks[0].Height-2*p.EdgeMargin+p.Tolerance)
                            {if(pairRecord)++pairRecord->StockRejected;continue;}
                            const auto envelope=width*height;
                            const auto totalMaterialArea=state.Value.MaterialArea+choice.Area;
                            // Disjoint original material cannot exceed its
                            // enclosing AABB. Reject impossible high-fill
                            // proposals before they crowd valid bridge/contact
                            // alternatives out of the bounded ranking pool.
                            // This is only a necessary geometric filter; exact
                            // original-material safety below remains mandatory.
                            const double areaGuard=8*p.Tolerance*(width+height)*(state.Value.Members.size()+1)+
                                64*std::numeric_limits<double>::epsilon()*std::max(envelope,totalMaterialArea);
                            if(totalMaterialArea>envelope+areaGuard){if(pairRecord)++pairRecord->EnvelopeRejected;continue;}
                            const auto fill=totalMaterialArea/envelope;
                            // A zero-gap assembly can fill a slanted basic
                            // carrier even when its AABB density is low. Its
                            // original-material union is assessed after safety.
                            if(fill<o.MinimumIntermediateFill&&(p.PartGap>0||!o.RecognizeFilledBasicCarriers))
                            {if(pairRecord)++pairRecord->FillRejected;continue;}
                            const auto normal=[](Point start,Point end)
                            {
                                const auto dx=end.X-start.X,dy=end.Y-start.Y,length=std::hypot(dx,dy);
                                return length>0?Point{dy/length,-dx/length}:Point{};
                            };
                            Point outward=normal(a,b);
                            // NFP material is on the left of oriented outer /
                            // hole edges. At a vertex, the adjacent outward
                            // normals provide a retreat into the free sector.
                            if(t<1e-9)
                            {
                                const auto previous=Transform(loop[(edge+loop.size()-1)%loop.size()],nfp.Transform);
                                const auto adjacent=normal(previous,a);outward.X+=adjacent.X;outward.Y+=adjacent.Y;
                            }
                            else if(1-t<1e-9)
                            {
                                const auto following=Transform(loop[(edge+2)%loop.size()],nfp.Transform);
                                const auto adjacent=normal(b,following);outward.X+=adjacent.X;outward.Y+=adjacent.Y;
                            }
                            const auto normalLength=std::hypot(outward.X,outward.Y);
                            if(normalLength>0){outward.X/=normalLength;outward.Y/=normalLength;}
                            const Proposal proposal{pose,fill,envelope,outward};
                            ranked.push_back(proposal);
                            if(ranked.size()>2*retained)
                            {std::sort(ranked.begin(),ranked.end(),proposalLess);ranked.resize(retained);}
                            // Preserve bounded opportunities for different
                            // relative angles and attachment anchors. Sparse
                            // contacts that can later bridge another original
                            // retain their bounded exploration opportunities.
                            // A short encounter prefix supplements each pool's
                            // best geometric proposals; final safety is unchanged.
                            const auto group=ai*anchors.size()+anchorRank;
                            if(group%groupStride==0||group+1==contactGroups)
                            {
                                auto& pool=contacts[{ai,anchorRank}];
                                if(pool.First.size()<firstQuota)pool.First.push_back(proposal);
                                pool.Best.push_back(proposal);
                                if(pool.Best.size()>2*bestQuota)
                                {std::sort(pool.Best.begin(),pool.Best.end(),proposalLess);pool.Best.resize(bestQuota);}
                            }
                            }
                        }
                    }
                }
                std::sort(ranked.begin(),ranked.end(),proposalLess);
                std::vector<Proposal> ordered;
                // The global best still receives the first validation. Thus a
                // tiny allowance compares all angles before spending its sole
                // attempt, while larger allowances retain anchor diversity.
                const auto remainingAllowance=partEnd>attempts?partEnd-attempts:0;
                // Clearance already supplies a conservative separation margin.
                // Preserve the best-scoring positive-gap contacts before using
                // a small remainder for angle/anchor exploration; otherwise a
                // two-attempt branch can lose its global second choice entirely.
                const auto rankedPrefix=p.PartGap>0?std::min(ranked.size(),
                    std::min(remainingAllowance,std::max<std::size_t>(2,
                        (remainingAllowance/4)*3+(remainingAllowance%4)*3/4))):
                    std::min<std::size_t>(1,ranked.size());
                if(mirroredProposals(j)&&rankedPrefix>=2)
                {
                    // Reserve an actual first look at both parities before a
                    // same-parity prefix spends a small exact-trial budget.
                    ordered.push_back(ranked.front());
                    const auto opposite=std::find_if(ranked.begin(),ranked.end(),[&](const auto& proposal)
                        {return proposal.Transform.Mirrored!=ranked.front().Transform.Mirrored;});
                    if(opposite!=ranked.end())ordered.push_back(*opposite);
                    for(std::size_t i=1;i<rankedPrefix&&ordered.size()<rankedPrefix;++i)ordered.push_back(ranked[i]);
                }
                else ordered.insert(ordered.end(),ranked.begin(),ranked.begin()+rankedPrefix);
                for(auto& [key,pool]:contacts)
                {(void)key;std::sort(pool.Best.begin(),pool.Best.end(),proposalLess);if(pool.Best.size()>bestQuota)pool.Best.resize(bestQuota);}
                for(std::size_t rank=0;rank<std::max(firstQuota,bestQuota);++rank)
                    for(const auto& [key,pool]:contacts)
                    {
                        (void)key;
                        if(rank<pool.First.size())ordered.push_back(pool.First[rank]);
                        if(rank<pool.Best.size())ordered.push_back(pool.Best[rank]);
                    }
                ordered.insert(ordered.end(),ranked.begin(),ranked.end());
                std::set<std::tuple<long long,long long,long long,bool>> tested;
                std::set<std::tuple<long long,long long,long long,bool>> strictOnly;
                const auto unit=std::max(1e-8,p.Tolerance*.1);
                const auto poseKey=[&](Pose pose)
                {return std::tuple(std::llround(NormalizeAngle(pose.AngleDegrees)*1e6),std::llround(pose.X/unit),std::llround(pose.Y/unit),pose.Mirrored);};
                const auto safePose=[&](Pose pose,bool& ordinaryClear)
                {
                    ordinaryClear=false;if(partStopped()||!PoseAllowed(p.Parts[j],pose,p.Tolerance))return false;
                    ++attempts;bool conflict=false,geometryConflict=false;
                    if(pairRecord){++pairRecord->Exact;PairPreparationSummary::ValidatedAngle(pairRecord,pose.AngleDegrees);}
                    for(const auto& member:state.Value.Members)
                    {
                        if(!Clear(p,member,{j,pose},&geometryConflict))conflict=true;
                        if(geometryConflict)break;
                    }
                    ordinaryClear=!geometryConflict;
                    if(conflict&&pairRecord)++pairRecord->ConflictRejected;
                    return !conflict;
                };
                for(const auto& proposal:ordered)
                {
                    if(partStopped())break;
                    auto pose=proposal.Transform;const auto keyBeforeRetreat=poseKey(pose);
                    bool safe=false,ordinaryClear=false;
                    if(tested.insert(keyBeforeRetreat).second)
                    {
                        safe=safePose(pose,ordinaryClear);
                        if(!safe&&ordinaryClear)strictOnly.insert(keyBeforeRetreat);
                    }
                    else if(!strictOnly.contains(keyBeforeRetreat))continue;
                    if(!safe)
                    {
                        if(!strictOnly.contains(keyBeforeRetreat)||
                            std::hypot(proposal.Outward.X,proposal.Outward.Y)==0)continue;
                        double scale=std::max({1.,state.Value.Width,state.Value.Height,std::abs(pose.X),std::abs(pose.Y)});
                        for(const auto& member:state.Value.Members)scale=std::max({scale,std::abs(member.Transform.X),std::abs(member.Transform.Y)});
                        // The proposal clipper uses eight decimal places. A
                        // bounded outward adjustment resolves its boundary
                        // quantization/binary64 roundoff; nominal PartGap and
                        // original geometry remain unchanged and are rechecked.
                        const double retreat=std::max({4*p.Tolerance,4e-8,
                            512*std::numeric_limits<double>::epsilon()*scale});
                        for(const double factor:{1.,4.})
                        {
                            if(partStopped())break;
                            pose=proposal.Transform;pose.X+=proposal.Outward.X*retreat*factor;pose.Y+=proposal.Outward.Y*retreat*factor;
                            if(!tested.insert(poseKey(pose)).second)continue;
                            safe=safePose(pose,ordinaryClear);
                            if(safe)break;
                        }
                        if(!safe)continue;
                    }
                    State candidate=state;candidate.Value.Members.push_back({j,pose});++candidate.Demand[j];
                    if(!normalizeAndRepair(candidate,partEnd,partStopped,true))
                    {if(pairRecord)++pairRecord->NormalizationRejected;continue;}
                    ++preparation.Certified;
                    if(selfPair)summary.Legal(j,candidate.Value);
                    if(candidate.Value.Width>p.Stocks[0].Width-2*p.EdgeMargin+p.Tolerance||
                        candidate.Value.Height>p.Stocks[0].Height-2*p.EdgeMargin+p.Tolerance)continue;
                    if(candidate.Fill<o.MinimumIntermediateFill){if(pairRecord)++pairRecord->FillRejected;continue;}
                    const auto key=GeometryKey(candidate,p.Tolerance);if(!seen.insert(key).second){if(pairRecord)++pairRecord->DuplicateRejected;continue;}
                    // Any two originals can form a useful tighter carrier
                    // below the dense-assembly threshold. Inventory identity
                    // does not change admission, material area or ranking.
                    if(candidate.Fill>=o.MinimumFinalFill||usefulPair(candidate))
                    {finalCandidates.push_back(candidate);observeCertified(finalCandidates.back().Value);}
                    if(depth<o.MaxMembers)next.push_back(std::move(candidate));
                }
            }
        }
        // Keep the most compact real geometry for each inventory signature,
        // not merely the first candidate encountered on an NFP edge.
        std::stable_sort(finalCandidates.begin(),finalCandidates.end(),Denser);
        if(o.SelfPairsOnly)
        {
            const auto originalCount=finalCandidates.size();
            for(std::size_t i=0;i<originalCount&&!stopped();++i)
            {
                const auto original=finalCandidates[i];if(!usefulPair(original))continue;
                const auto pi=original.Value.Members.front().PartIndex;
                std::vector<State> variants;
                for(const auto orientation:CandidateOrientations(p.Parts[pi],p.Tolerance,o.OrientationProposals))
                {
                    if(stopped())break;
                    auto first=original.Value.Members.front().Transform;first.X=first.Y=0;
                    const auto turn=ComposePose(orientation,InversePose(first));
                    if(!turn.Mirrored&&(turn.AngleDegrees<=p.Tolerance||360-turn.AngleDegrees<=p.Tolerance))continue;
                    auto variant=original;bool legal=true;
                    for(auto& member:variant.Value.Members)
                    {
                        member.Transform=ComposePose(turn,member.Transform);
                        legal&=!member.Transform.Mirrored||mirroredProposals(member.PartIndex);
                        legal&=PoseAllowed(p.Parts[member.PartIndex],member.Transform,p.Tolerance);
                    }
                    if(!legal)continue;
                    Evaluate(variant,p,o.RecognizeFilledBasicCarriers);
                    variants.push_back(std::move(variant));
                }
                std::stable_sort(variants.begin(),variants.end(),Denser);
                std::size_t retained=0;
                for(auto& variant:variants)
                {
                    if(stopped()||retained>=4)break;
                    ++attempts;
                    if(!normalizeAndRepair(variant,o.MaxAttempts,stopped))continue;
                    ++preparation.Certified;
                    if(!seen.insert(GeometryKey(variant,p.Tolerance)).second)continue;
                    finalCandidates.push_back(std::move(variant));++retained;
                    observeCertified(finalCandidates.back().Value);
                }
            }
            std::stable_sort(finalCandidates.begin(),finalCandidates.end(),Denser);
        }
        for(const auto& candidate:finalCandidates)
        {
            // Search termination cannot invalidate these already certified
            // stored coordinates. Bounded publication only copies them; it
            // performs no new geometry or board commit. The caller retains
            // its cancellation gate before consuming/committing templates.
            retainCertified(candidate.Value);
        }
        std::stable_sort(next.begin(),next.end(),Denser);
        std::map<std::string,std::vector<std::size_t>> survivors;std::vector<std::string> signatures;
        for(std::size_t i=0;i<next.size();++i)
        {
            const auto key=Signature(next[i]);auto& ids=survivors[key];
            if(ids.empty())signatures.push_back(key);
            ids.push_back(i);
        }
        for(auto& [key,ids]:survivors)
        {
            (void)key;std::vector<std::size_t> variants{ids.front()};
            // An outward small anchor can connect two larger originals that
            // should not touch each other. Preserve both short-axis sides,
            // rather than keeping only the smallest intermediate AABBs.
            for(unsigned side:{1u,2u})
            {
                if((next[variants.front()].AnchorSides&side)&&!(next[variants.front()].AnchorSides&12u))continue;
                std::size_t chosen=next.size();
                for(auto i:ids)if((next[i].AnchorSides&side)&&!(next[i].AnchorSides&12u)){chosen=i;break;}
                if(chosen==next.size())for(auto i:ids)if(next[i].AnchorSides&side){chosen=i;break;}
                if(chosen<next.size()&&std::find(variants.begin(),variants.end(),chosen)==variants.end())variants.push_back(chosen);
            }
            for(auto i:ids)
            {if(variants.size()>=4)break;if(std::find(variants.begin(),variants.end(),i)==variants.end())variants.push_back(i);}
            ids=std::move(variants);
        }
        beam.clear();
        // Give each inventory its best real geometry before spending more
        // states on variants of an already dense group. Useful partial groups
        // then get a chance to be filled by unlike originals at the next depth.
        for(std::size_t rank=0;rank<4&&beam.size()<o.MaxStates;++rank)
        {
            for(const auto& key:signatures)
            {
                const auto& ids=survivors[key];if(ids.size()>rank)beam.push_back(std::move(next[ids[rank]]));
                if(beam.size()>=o.MaxStates)break;
            }
        }
    }
    if(o.MaxMembers>=4)
    {
        // Bounded publication may replace/reorder an earlier result. Keep a
        // small immutable snapshot so each original pair is explored once.
        const auto initial=output;
        for(const auto& pair:initial)
        {
            if(stopped())break;
            if(pair.Members.size()!=2)continue;
            const double gap=p.PartGap>0?p.PartGap+std::max(1e-7,4*p.Tolerance):0;
            std::vector<Point> shifts{{pair.Width+gap,0},{0,pair.Height+gap}};
            if(p.PartGap==0&&o.RecognizeFilledBasicCarriers&&FilledBasic(pair.Kind))
            {
                const auto hull=Hull(pair,p);
                if(hull.size()==4)shifts={{hull[1].X-hull[0].X,hull[1].Y-hull[0].Y},
                    {hull[3].X-hull[0].X,hull[3].Y-hull[0].Y}};
            }
            std::vector<std::size_t> demand(p.Parts.size());
            for(const auto& m:pair.Members)demand[m.PartIndex]+=2;
            bool enough=true;for(std::size_t i=0;i<demand.size();++i)if(demand[i]>p.Parts[i].Quantity)enough=false;
            if(!enough)continue;
            for(const auto shift:shifts)
            {
                if(stopped())break;
                ++attempts;auto repeated=pair;
                for(auto member:pair.Members)
                {
                    member.Transform.X+=shift.X;member.Transform.Y+=shift.Y;
                    repeated.Members.push_back(member);
                }
                State evaluated;evaluated.Value=std::move(repeated);
                if(!normalizeAndRepair(evaluated,o.MaxAttempts,stopped))continue;
                ++preparation.Certified;
                if(evaluated.Fill<o.MinimumFinalFill)continue;
                // The certificate precedes publication, so a search stop
                // observed now must not discard this immutable result either.
                observeCertified(evaluated.Value);
                retainCertified(evaluated.Value);
            }
        }
    }
    if(!o.ExpandAssemblies&&!o.SelfPairsOnly)
    {
        preparation.ExitReason=o.SamePartOnly?"compact-self-pairs-and-self-repeat-cells":
            "compact-neighbour-coverage-and-pair-repeat-cells";
        preparation.CoverageTruncated=true;
    }
    for(const auto& pattern:output)
    {
        summary.Retain(pattern);
        if(std::any_of(basicCertificates.begin(),basicCertificates.end(),[&](const auto& certified)
            {return SameStoredGeometry(pattern,certified);}))++preparation.BasicRetained;
    }
    return output;
}
}

