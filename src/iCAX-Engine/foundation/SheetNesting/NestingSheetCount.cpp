#include "SheetNestingSheetCount.h"
#include "NestingConstraintKernel.h"
#include "NestingSheetCountSchedule.h"
#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingContactPrecision.h"
#include "NestingAxisSafety.h"
#include "absl/numeric/int128.h"
#include <iomanip>
#include <optional>

namespace iCAX::SheetNesting
{
namespace
{
    using namespace ConstraintKernel;
    // Bounds every coordinate before arithmetic. With <=128 vertices/copies,
    // normalized widths <=2e6, primitive normals <=2e6 and stock <=1e6,
    // signed 64-bit areas/projections/linear-domain products stay below 1e16.
    // Only squared gap comparisons need the explicit 128-bit path below.
    constexpr Integer MaxGridCoordinate=1000000;
    struct Normal
    {
        Integer X=0,Y=0;
        bool operator<(const Normal& b)const{return std::tie(X,Y)<std::tie(b.X,b.Y);}
    };
    struct Version
    {
        Ring Points;
        Integer OriginX=0,OriginY=0,Width=0,Height=0;
        std::set<Normal> Normals;
    };
    struct InteriorRect{Integer X=0,Y=0,Width=0,Height=0;};
    struct Prepared
    {
        std::size_t Original=0;
        Integer Material2=0;
        bool HalfTurnClosed=false;
        std::array<Version,4> Versions;
        std::array<std::optional<InteriorRect>,4> Interior;
        std::vector<Integer> Quarters;
    };
    struct Instance{std::size_t Type=0,Copy=0;};
    using Identity = SheetCountPartition::Identity;
    using IdentityPair = std::pair<Identity, Identity>;

    bool SameRing(const std::vector<Point>& first, const std::vector<Point>& second)
    {
        if (first.size() != second.size()) return false;
        for (std::size_t i = 0; i < first.size(); ++i)
            if (first[i].X != second[i].X || first[i].Y != second[i].Y) return false;
        return true;
    }

    bool SameShape(const Shape& first, const Shape& second)
    {
        if (first.Radius != second.Radius || first.Center.X != second.Center.X || first.Center.Y != second.Center.Y ||
            first.Holes.size() != second.Holes.size() || !SameRing(first.Outer, second.Outer)) return false;
        for (std::size_t i = 0; i < first.Holes.size(); ++i)
            if (!SameRing(first.Holes[i], second.Holes[i])) return false;
        return true;
    }

    bool SameHintTask(const Problem& current, const Problem& snapshot)
    {
        if (current.PartGap != snapshot.PartGap || current.EdgeMargin != snapshot.EdgeMargin ||
            current.Tolerance != snapshot.Tolerance || current.AllowCommonEdges != snapshot.AllowCommonEdges ||
            current.Parts.size() != snapshot.Parts.size() || current.Stocks.size() != snapshot.Stocks.size()) return false;
        std::map<std::string, const Part*> parts;
        std::map<std::string, const Stock*> stocks;
        for (const auto& part : snapshot.Parts)
            if (!parts.emplace(part.ID, &part).second) return false;
        for (const auto& stock : snapshot.Stocks)
            if (!stocks.emplace(stock.ID, &stock).second) return false;
        for (const auto& part : current.Parts)
        {
            const auto found = parts.find(part.ID);
            if (found == parts.end()) return false;
            const auto& original = *found->second;
            if (part.Quantity != original.Quantity || part.Material != original.Material ||
                part.FreeRotation != original.FreeRotation || part.AllowedAnglesDegrees != original.AllowedAnglesDegrees ||
                !SameShape(part.Geometry, original.Geometry)) return false;
        }
        for (const auto& stock : current.Stocks)
        {
            const auto found = stocks.find(stock.ID);
            if (found == stocks.end()) return false;
            const auto& original = *found->second;
            if (stock.Width != original.Width || stock.Height != original.Height || stock.Cost != original.Cost ||
                stock.Material != original.Material || stock.Quantity > original.Quantity) return false;
        }
        return true;
    }

    bool HintPartitionMatches(const SheetCountPartition& partition, const Result& source)
    {
        using Group = std::set<Identity>;
        std::set<Group> declared, actual;
        for (const auto& group : partition.Groups) declared.emplace(group.begin(), group.end());
        for (const auto& board : source.Layouts)
        {
            Group group;
            for (const auto& pose : board.Placements) group.emplace(pose.PartID, pose.Instance);
            actual.insert(std::move(group));
        }
        return declared == actual;
    }

    struct PlanHint
    {
        Integer Bin = 0, X = 0, Y = 0;
        int Rotation = -1;
        bool Position = false, Rounded = false, CanonicalRotation = false, Donor = false;
    };

    void CheckSettings(const SheetCountSettings& settings)
    {
        if(settings.CoordinateScale<1||settings.CoordinateScale>1000000||settings.Workers<1||settings.Workers>64||
            !std::isfinite(settings.SecondsPerBoardCount)||settings.SecondsPerBoardCount<=0||settings.MaxConflicts<1||settings.MaxInstances<1||settings.MaxInstances>128)
            throw std::invalid_argument("Invalid cold sheet-count settings");
    }

    bool CheckPartition(const Problem& problem, const SheetCountPartition& partition,
        std::size_t count, std::vector<IdentityPair>& mutablePairs, std::string& why)
    {
        if (partition.Groups.empty())
        {
            why = "partition has no original groups";
            return false;
        }
        std::map<std::string, std::size_t> quantities;
        for (const auto& part : problem.Parts) quantities.emplace(part.ID, part.Quantity);
        std::set<Identity> seen;
        for (const auto& group : partition.Groups)
        {
            if (group.empty())
            {
                why = "partition contains an empty original group";
                return false;
            }
            for (const auto& identity : group)
            {
                const auto part = quantities.find(identity.first);
                if (part == quantities.end() || identity.second >= part->second || !seen.insert(identity).second)
                {
                    why = "partition contains an unknown, duplicate or out-of-range original instance";
                    return false;
                }
            }
            for (std::size_t member = 1; member < group.size(); ++member)
                mutablePairs.emplace_back(group.front(), group[member]);
        }
        if (seen.size() != count)
        {
            why = "partition does not cover the complete original demand";
            return false;
        }
        return true;
    }
    Integer Dot(IPoint p,Normal n){return p.X*n.X+p.Y*n.Y;}
    Normal MakeNormal(IPoint a,IPoint b)
    {
        Normal n{a.Y-b.Y,b.X-a.X};const auto d=std::gcd(std::abs(n.X),std::abs(n.Y));
        if(d){n.X/=d;n.Y/=d;}if(n.X<0||(n.X==0&&n.Y<0)){n.X=-n.X;n.Y=-n.Y;}return n;
    }
    Integer ProjectedGap(Integer gap,Normal n)
    {
        // The final comparisons are exact, including an integral hypotenuse.
        const absl::int128 required=absl::int128(gap)*gap*(absl::int128(n.X)*n.X+absl::int128(n.Y)*n.Y);
        Integer value=static_cast<Integer>(std::ceil(static_cast<long double>(gap)*std::hypot(static_cast<long double>(n.X),static_cast<long double>(n.Y))));
        while(absl::int128(value)*value<required)++value;
        while(value>0&&absl::int128(value-1)*(value-1)>=required)--value;
        return value;
    }
    Integer FloorDiv(Integer numerator,Integer positiveDenominator)
    {const auto q=numerator/positiveDenominator,r=numerator%positiveDenominator;return q-Integer(r<0);}
    Integer CeilDiv(Integer numerator,Integer positiveDenominator)
    {const auto q=numerator/positiveDenominator,r=numerator%positiveDenominator;return q+Integer(r>0);}
    std::optional<InteriorRect> BuildInterior(const Version& version,const std::function<bool()>& stop)
    {
        // A rectangle is inside a convex ring iff all its corners satisfy
        // every edge half-plane. Candidate-band sampling only controls the
        // strength of a redundant constraint; no original geometry is lost.
        const auto ring=CanonicalRing(version.Points);std::vector<Integer> levels;
        for(const auto p:ring)levels.push_back(p.Y);
        std::sort(levels.begin(),levels.end());levels.erase(std::unique(levels.begin(),levels.end()),levels.end());
        if(levels.size()>17)
        {
            std::vector<Integer> sampled;for(std::size_t i=0;i<17;++i)sampled.push_back(levels[i*(levels.size()-1)/16]);
            levels=std::move(sampled);
        }
        auto rows=levels;
        for(std::size_t i=1;i<levels.size();++i)
        {rows.push_back((levels[i-1]+levels[i])/2);rows.push_back((levels[i-1]+levels[i]+1)/2);}
        std::sort(rows.begin(),rows.end());rows.erase(std::unique(rows.begin(),rows.end()),rows.end());
        std::optional<InteriorRect> best;Integer bestArea=0;
        for(std::size_t low=0;low<rows.size();++low)for(std::size_t high=low+1;high<rows.size();++high)
        {
            if(stop())return std::nullopt;const auto y0=rows[low],y1=rows[high];Integer left=0,right=version.Width;bool valid=true;
            for(std::size_t e=0;e<ring.size()&&valid;++e)
            {
                const auto a=ring[e],b=ring[(e+1)%ring.size()];const auto dx=b.X-a.X,dy=b.Y-a.Y;
                for(const auto y:{y0,y1})
                {
                    // CCW interior: dy*x <= dx*y + dy*ax - dx*ay.
                    const auto numerator=dx*y+dy*a.X-dx*a.Y;
                    if(dy>0)right=std::min(right,FloorDiv(numerator,dy));
                    else if(dy<0)left=std::max(left,CeilDiv(-numerator,-dy));
                    else if(numerator<0){valid=false;break;}
                }
            }
            if(!valid||right<=left)continue;const auto area=(right-left)*(y1-y0);
            if(area>bestArea){bestArea=area;best=InteriorRect{left,y0,right-left,y1-y0};}
        }
        return best;
    }
    bool CheckInterior(const Version& version,const InteriorRect& rect)
    {
        if(rect.Width<=0||rect.Height<=0)return false;const auto ring=CanonicalRing(version.Points);
        for(const auto x:{rect.X,rect.X+rect.Width})for(const auto y:{rect.Y,rect.Y+rect.Height})
            if(!Inside(ring,{x,y}))return false;
        return true;
    }
    bool Prepare(const Part& part,std::size_t index,Integer scale,bool interior,const std::function<bool()>& stop,Prepared& type,std::string& why)
    {
        if(part.FreeRotation||part.Geometry.Radius!=0||!part.Geometry.Holes.empty())
        {why="unsupported free rotation, circle or holes: "+part.ID;return false;}
        Ring original;
        for(const auto point:part.Geometry.Outer)
        {IPoint p;if(!Lossless(point.X,scale,p.X)||!Lossless(point.Y,scale,p.Y)){why="original vertices are not lossless at coordinate scale: "+part.ID;return false;}
            if(std::abs(p.X)>MaxGridCoordinate||std::abs(p.Y)>MaxGridCoordinate){why="unsupported original arithmetic scope: "+part.ID;return false;}original.push_back(p);}
        if(original.size()>1&&original.front()==original.back())original.pop_back();
        if(original.size()<3||original.size()>128){why="unsupported vertex count: "+part.ID;return false;}
        const auto area=std::abs(Area2(original));
        if(area<=0||std::abs(Area2(Hull(original)))!=area)
        {why="unsupported nonconvex original: "+part.ID;return false;}
        type.Original=index;type.Material2=area;
        const auto angles=part.AllowedAnglesDegrees.empty()?std::vector<double>{0}:part.AllowedAnglesDegrees;
        for(const auto angle:angles)
        {
            const int q=Quarter(angle);if(q<0){why="unsupported non-quarter original angle: "+part.ID;return false;}
            if(std::find(type.Quarters.begin(),type.Quarters.end(),q)==type.Quarters.end())type.Quarters.push_back(q);
        }
        std::sort(type.Quarters.begin(),type.Quarters.end());
        type.HalfTurnClosed=std::all_of(type.Quarters.begin(),type.Quarters.end(),[&](Integer q)
            {return std::find(type.Quarters.begin(),type.Quarters.end(),(q+2)%4)!=type.Quarters.end();});
        for(int q=0;q<4;++q)
        {
            auto& v=type.Versions[q];Integer lowX=std::numeric_limits<Integer>::max(),lowY=lowX,highX=-lowX,highY=-lowX;
            for(const auto p:original)
            {
                IPoint r;switch(q){case 0:r=p;break;case 1:r={-p.Y,p.X};break;case 2:r={-p.X,-p.Y};break;default:r={p.Y,-p.X};}
                v.Points.push_back(r);lowX=std::min(lowX,r.X);lowY=std::min(lowY,r.Y);highX=std::max(highX,r.X);highY=std::max(highY,r.Y);
            }
            v.OriginX=lowX;v.OriginY=lowY;v.Width=highX-lowX;v.Height=highY-lowY;
            for(auto& p:v.Points){p.X-=lowX;p.Y-=lowY;}
            for(std::size_t i=0;i<v.Points.size();++i)
            {const auto n=MakeNormal(v.Points[i],v.Points[(i+1)%v.Points.size()]);if(n.X||n.Y)v.Normals.insert(n);}
        }
        std::set<Ring> materialDirections;
        type.Quarters.erase(std::remove_if(type.Quarters.begin(),type.Quarters.end(),[&](Integer q)
            {return !materialDirections.insert(CanonicalRing(type.Versions[q].Points)).second;}),type.Quarters.end());
        if(interior)
        {
            const auto rect=BuildInterior(type.Versions[0],stop);
            if(stop()){why="budget during exact interior-rectangle preparation";return false;}
            if(rect)
            {
                const auto [x,y,w,h]=*rect;const auto W=type.Versions[0].Width,H=type.Versions[0].Height;
                type.Interior={InteriorRect{x,y,w,h},InteriorRect{H-y-h,x,h,w},
                    InteriorRect{W-x-w,H-y-h,w,h},InteriorRect{y,W-x-w,h,w}};
                for(int q=0;q<4;++q)if(!CheckInterior(type.Versions[q],*type.Interior[q]))
                    throw std::logic_error("Exact interior rectangle escaped original quarter-turn material");
            }
        }
        return true;
    }

    class SheetCountStrategy final:public IStrategy
    {
        const SheetCountSettings Settings;
        const std::optional<SheetCountPartition> Partition;
        const std::optional<SheetCountCurrentTaskHint> CurrentTaskHint;
    public:
        explicit SheetCountStrategy(SheetCountSettings settings,
            std::optional<SheetCountPartition> partition = std::nullopt,
            std::optional<SheetCountCurrentTaskHint> currentTaskHint = std::nullopt)
            : Settings(settings), Partition(std::move(partition)), CurrentTaskHint(std::move(currentTaskHint)) {}
        Result Run(const Problem& problem,const Options& options)override
        {
            const auto started=Clock::now();Result result;result.State=Status::Failed;
            for(const auto& p:problem.Parts)if(p.Quantity)result.Remaining.push_back({p.ID,p.Quantity});
            bool cancelled=false;std::size_t plans=0,unknown=0,revisits=0,interiorTypes=0,skippedInteriorTypes=0;std::int64_t conflicts=0;
            bool partitionExclusionAdded = false, partitionChanged = false;
            bool hintSourceValidated = false, firstCompleteHandoff = false;
            std::size_t hintedInstances=0,positionHints=0,roundedHints=0,canonicalHints=0,donorHints=0;
            const char* diagnostic = Partition ? "sheet-count.perturb" : CurrentTaskHint ? "sheet-count.repair" : "sheet-count.cold";
            std::optional<Detail::SheetCountSchedule> scheduling;
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-started).count();};
            const auto pollCancel=[&]{if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;return cancelled;};
            const auto stop=[&]{return pollCancel()||!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<=0||elapsed()>=options.TimeLimitSeconds;};
            const auto finish=[&](const std::string& reason)
            {
                if(cancelled)result.State=Status::Cancelled;
                result.Stats.ElapsedSeconds=elapsed();
                if(result.Stats.PreparationSeconds>=0)
                    result.Stats.PlacementSeconds=std::max(0.,result.Stats.ElapsedSeconds-result.Stats.PreparationSeconds-std::max(0.,result.Stats.VerificationSeconds));
                std::ostringstream note;note<<diagnostic<<": "<<reason<<"; fixed-count-plans="<<plans<<"; unknown-counts="<<unknown
                    <<"; revisit-plans="<<revisits<<"; unresolved-counts="<<(scheduling?scheduling->UnresolvedCount():0)
                    <<"; solver-conflicts="<<conflicts<<"; coordinate-scale="<<Settings.CoordinateScale
                    <<"; interior-rectangles="<<Settings.EnableInteriorRectangles<<"; interior-rectangle-types="<<interiorTypes<<"; interior-skipped-types="<<skippedInteriorTypes
                    <<"; fixed-partition-count="<<(Partition?Partition->Groups.size():0)
                    <<"; partition-change-constraint="<<partitionExclusionAdded<<"; partition-change="<<partitionChanged
                    <<"; consolidation-objective="<<Partition.has_value()
                    <<"; stop-after-first-complete="<<Settings.StopAfterFirstComplete<<"; first-complete-handoff="<<firstCompleteHandoff
                    <<"; current-task-hint="<<CurrentTaskHint.has_value()<<"; hint-source-validated="<<hintSourceValidated
                    <<"; hint-hard-locks=0; hinted-instances="<<hintedInstances<<"; hinted-position-instances="<<positionHints
                    <<"; rounded-hint-instances="<<roundedHints<<"; canonical-rotation-hints="<<canonicalHints<<"; donor-hint-instances="<<donorHints
                    <<"; current-problem-only; prior-layout-input=0; hint-layout-input="<<CurrentTaskHint.has_value()
                    <<"; prior-task-layout-input=0; common-edge-objective=0; timeout-is-not-infeasible; conservative-projection-lattice";
                result.Diagnostics.push_back(note.str());return result;
            };
            try
            {
                if(stop())return finish("cancelled-or-budget-before-model");
                const auto errors=Detail::CheckProblem(problem);
                if(!errors.empty()){result.State=Status::InvalidInput;result.Diagnostics.insert(result.Diagnostics.end(),errors.begin(),errors.end());return finish("invalid original input");}
                std::size_t count=0;for(const auto& p:problem.Parts)
                {if(p.Quantity>Settings.MaxInstances-count)return finish("unsupported original instance count");count+=p.Quantity;}
                std::vector<IdentityPair> mutablePairs;
                if (Partition)
                {
                    std::string why;
                    if (!CheckPartition(problem, *Partition, count, mutablePairs, why))
                    {
                        result.State = Status::InvalidInput;
                        return finish("invalid original partition: " + why);
                    }
                }
                if(count>options.MaxPlacementAttempts)return finish("insufficient complete-original validation budget");
                if (CurrentTaskHint)
                {
                    if (!SameHintTask(problem, CurrentTaskHint->Input))
                    {
                        result.State = Status::InvalidInput;
                        return finish("invalid current-task hint: original input differs beyond inventory reduction");
                    }
                    const auto verification = elapsed();
                    const auto checked = Validate(CurrentTaskHint->Input, CurrentTaskHint->Layout);
                    result.Stats.VerificationSeconds = elapsed() - verification;
                    result.Stats.PlacementAttempts += count;
                    if (!checked.Valid || !checked.Complete || checked.PlacedCount != count ||
                        (Partition && !HintPartitionMatches(*Partition, CurrentTaskHint->Layout)))
                    {
                        result.State = Status::InvalidInput;
                        result.Diagnostics.insert(result.Diagnostics.end(), checked.Errors.begin(), checked.Errors.end());
                        return finish("invalid current-task hint: source layout or current partition failed original validation");
                    }
                    hintSourceValidated = true;
                }
                if(!count){result.State=Status::Complete;result.Remaining.clear();result.GeometryValidated=true;return finish("empty demand complete");}
                const Stock* stock=nullptr;
                for(const auto& s:problem.Stocks)if(s.Quantity)
                {if(stock)return finish("unsupported multiple stocked types");stock=&s;}
                if(!stock){result.State=Status::Partial;return finish("no available stock");}
                if (CurrentTaskHint && std::any_of(CurrentTaskHint->Layout.Layouts.begin(), CurrentTaskHint->Layout.Layouts.end(),
                    [&](const Layout& board) { return board.StockID != stock->ID; }))
                    return finish("unsupported current-task hint from multiple stock cohorts");
                if (Partition && Partition->Groups.size() > stock->Quantity)
                {
                    result.State = Status::InvalidInput;
                    return finish("invalid original partition: group count exceeds available stock quantity");
                }
                if (Partition && mutablePairs.empty())
                {
                    result.State = Status::Partial;
                    return finish("unsupported original partition: noMutablePair");
                }
                const auto scale=Settings.CoordinateScale;Integer W=0,H=0,margin=0,gap=0;
                if(!Lossless(stock->Width,scale,W)||!Lossless(stock->Height,scale,H)||!Lossless(problem.EdgeMargin,scale,margin)||!Lossless(problem.PartGap,scale,gap))
                    return finish("stock or process parameters are not lossless at coordinate scale");
                if(W>MaxGridCoordinate||H>MaxGridCoordinate||margin>MaxGridCoordinate||gap>MaxGridCoordinate)
                    return finish("unsupported stock/process arithmetic scope");
                if(W<=2*margin||H<=2*margin){result.State=Status::Partial;return finish("no usable stock interior");}
                std::vector<Prepared> types;std::vector<Instance> instances;Integer material2=0;
                for(std::size_t pi=0;pi<problem.Parts.size();++pi)
                {
                    if(stop())return finish("budget during original preparation");const auto& p=problem.Parts[pi];if(!p.Quantity)continue;
                    if(p.Material!=stock->Material)return finish("unsupported incompatible original material");
                    Prepared type;std::string why;if(!Prepare(p,pi,scale,Settings.EnableInteriorRectangles,stop,type,why))return finish(why);
                    if(Settings.EnableInteriorRectangles){if(type.Interior[0])++interiorTypes;else ++skippedInteriorTypes;}
                    if(std::none_of(type.Quarters.begin(),type.Quarters.end(),[&](Integer q){return type.Versions[q].Width<=W-2*margin&&type.Versions[q].Height<=H-2*margin;}))
                    {result.State=Status::Partial;return finish("no allowed original orientation fits usable stock: "+p.ID);}
                    for(std::size_t copy=0;copy<p.Quantity;++copy){instances.push_back({types.size(),copy});material2+=type.Material2;}
                    types.push_back(std::move(type));
                }
                std::stable_sort(instances.begin(),instances.end(),[&](auto a,auto b){return types[a.Type].Material2>types[b.Type].Material2;});
                std::vector<std::pair<std::size_t, std::size_t>> changedPairs;
                std::map<Identity, std::size_t> variableIndices;
                if (Partition || CurrentTaskHint)
                {
                    for (std::size_t i = 0; i < instances.size(); ++i)
                    {
                        const auto& instance = instances[i];
                        const auto& part = problem.Parts[types[instance.Type].Original];
                        variableIndices.emplace(Identity{part.ID, instance.Copy}, i);
                    }
                }
                for (const auto& pair : mutablePairs)
                    changedPairs.emplace_back(variableIndices.at(pair.first), variableIndices.at(pair.second));
                const Integer capacity=2*(W-2*margin)*(H-2*margin);
                const auto lower=static_cast<std::size_t>((material2+capacity-1)/capacity);
                auto upper=std::min(stock->Quantity,count);
                if(lower>upper){result.State=Status::Partial;return finish("original material exceeds available stock area");}
                if(Settings.EnableNfpInitializer&&!Partition&&!CurrentTaskHint&&!stop())
                {
                    auto initialOptions=options;initialOptions.EnableBasicCompaction=false;
                    initialOptions.TimeLimitSeconds=std::min(10.,std::max(0.,options.TimeLimitSeconds-elapsed())*.15);
                    initialOptions.MaxPlacementAttempts=(options.MaxPlacementAttempts-result.Stats.PlacementAttempts)/4;
                    initialOptions.Cancel=pollCancel;
                    if(initialOptions.TimeLimitSeconds>0&&initialOptions.MaxPlacementAttempts>=count)
                    {
                        // This phase needs a complete upper bound quickly;
                        // diversification belongs to the ensuing count search.
                        PlaneStrategySettings initialSettings;initialSettings.Kind=PlaneStrategyKind::NfpAllSpecifications;initialSettings.NfpTrials=1;
                        initialSettings.EnableLayoutRefinement=false;
                        auto initial=MakePlaneStrategyFactory(initialSettings)()->Run(problem,initialOptions);
                        const auto initialWork=std::min(initial.Stats.PlacementAttempts,initialOptions.MaxPlacementAttempts);
                        result.Stats.PlacementAttempts+=initialWork;
                        const auto originalCheck=Validate(problem,initial);
                        if(originalCheck.Valid&&originalCheck.Complete)
                        {
                            if(Settings.StopAfterFirstComplete)
                            {
                                const auto work=result.Stats.PlacementAttempts;result=std::move(initial);
                                result.Stats.PlacementAttempts=work;firstCompleteHandoff=true;
                                result.Diagnostics.push_back("sheet-count.cold: native current-input NFP initializer; external-layout-input=0; original-pose-locks=0");
                                return finish("first newly validated complete native initializer handed off for current-task guided repair");
                            }
                            auto rest=options;rest.TimeLimitSeconds=std::max(0.,options.TimeLimitSeconds-elapsed());
                            rest.MaxPlacementAttempts-=std::min(rest.MaxPlacementAttempts,result.Stats.PlacementAttempts);rest.Cancel=pollCancel;
                            auto settings=Settings;settings.EnableNfpInitializer=false;
                            auto improved=SheetCountStrategy(settings,std::nullopt,SheetCountCurrentTaskHint{problem,initial}).Run(problem,rest);
                            const auto checked=Validate(problem,improved);
                            const auto prefixAttempts=result.Stats.PlacementAttempts;
                            if(checked.Valid&&checked.Complete)result=std::move(improved);
                            else result=std::move(initial);
                            result.Stats.PlacementAttempts=std::min(options.MaxPlacementAttempts,prefixAttempts+
                                (checked.Valid&&checked.Complete?result.Stats.PlacementAttempts:std::size_t{}));
                            result.Diagnostics.push_back("sheet-count.cold: native current-input NFP initializer; external-layout-input=0; original-pose-locks=0");
                            return finish("native-initializer-and-fixed-count-search");
                        }
                    }
                }
                if (CurrentTaskHint && !Partition && CurrentTaskHint->Layout.Layouts.size() <= stock->Quantity &&
                    count <= options.MaxPlacementAttempts - result.Stats.PlacementAttempts)
                {
                    auto incumbent = CurrentTaskHint->Layout;
                    for (std::size_t board = 0; board < incumbent.Layouts.size(); ++board)
                        incumbent.Layouts[board].StockInstance = board;
                    const auto verification = elapsed();
                    const auto checked = Validate(problem, incumbent);
                    result.Stats.VerificationSeconds = std::max(0., result.Stats.VerificationSeconds) + elapsed() - verification;
                    result.Stats.PlacementAttempts += count;
                    if (checked.Valid && checked.Complete)
                    {
                        result.Layouts = std::move(incumbent.Layouts);
                        result.State = Status::Complete;
                        result.Remaining.clear();
                        result.GeometryValidated = true;
                        upper = std::min(upper, result.Layouts.size());
                        result.Diagnostics.push_back("sheet-count.repair: validated current-task incumbent retained; boards=" +
                            std::to_string(result.Layouts.size()) + "; hint-poses-published-only-after-original-validation=1");
                        if (upper < lower)
                            return finish("checked current-task incumbent precedes the conservative lattice area range");
                    }
                }
                result.Diagnostics.push_back(std::string(diagnostic)+": area-lower-bound="+std::to_string(lower)+"; stock-count-upper-bound="+std::to_string(upper));
                if (Partition)
                {
                    const auto fixed = Partition->Groups.size();
                    result.Diagnostics.push_back("sheet-count.perturb: fixed-count=" + std::to_string(fixed) +
                        "; mutable-pairs=" + std::to_string(changedPairs.size()) + "; minimum-count-proof=0");
                    if (fixed < lower)
                    {
                        result.State = Status::Partial;
                        return finish("original material exceeds fixed perturbation stock area");
                    }
                }
                ConstraintCompletionSettings solverSettings;solverSettings.CoordinateScale=scale;solverSettings.Workers=Settings.Workers;
                solverSettings.MaxConflicts=Settings.MaxConflicts;solverSettings.MaxInstances=Settings.MaxInstances;
                solverSettings.CompleteSeparationHints = CurrentTaskHint.has_value();
                std::map<Identity, const Placement*> sourcePoses;
                std::vector<Integer> sourceAreas;
                std::vector<std::size_t> sourceOrder;
                if (CurrentTaskHint)
                {
                    const auto& boards = CurrentTaskHint->Layout.Layouts;
                    sourceAreas.resize(boards.size());
                    sourceOrder.resize(boards.size());
                    std::iota(sourceOrder.begin(), sourceOrder.end(), 0);
                    for (std::size_t board = 0; board < boards.size(); ++board)
                        for (const auto& pose : boards[board].Placements)
                        {
                            const Identity identity{pose.PartID, pose.Instance};
                            sourcePoses.emplace(identity, &pose);
                            sourceAreas[board] += types[instances[variableIndices.at(identity)].Type].Material2;
                        }
                    std::stable_sort(sourceOrder.begin(), sourceOrder.end(), [&](std::size_t a, std::size_t b)
                    {
                        if (sourceAreas[a] != sourceAreas[b]) return sourceAreas[a] > sourceAreas[b];
                        return boards[a].StockInstance < boards[b].StockInstance;
                    });
                }
                const auto buildHints = [&](std::size_t boards)
                {
                    std::vector<std::optional<PlanHint>> hints(count);
                    if (!CurrentTaskHint) return hints;
                    const auto makeHint = [&](std::size_t index, const Placement& source, std::size_t board, bool donor)
                    {
                        PlanHint hint;
                        hint.Bin = static_cast<Integer>(board);
                        hint.Donor = donor;
                        const auto& type = types[instances[index].Type];
                        const int quarter = Quarter(source.Transform.AngleDegrees);
                        if (quarter < 0) return hint;
                        const auto material = CanonicalRing(type.Versions[quarter].Points);
                        for (const auto allowed : type.Quarters)
                            if (CanonicalRing(type.Versions[allowed].Points) == material)
                            {
                                hint.Rotation = static_cast<int>(allowed);
                                break;
                            }
                        if (hint.Rotation < 0) return hint;
                        hint.CanonicalRotation = hint.Rotation != quarter;
                        if (donor) return hint;
                        const auto& original = type.Versions[quarter];
                        const auto& canonical = type.Versions[hint.Rotation];
                        const long double x = static_cast<long double>(source.Transform.X) * scale + original.OriginX;
                        const long double y = static_cast<long double>(source.Transform.Y) * scale + original.OriginY;
                        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > MaxGridCoordinate * 3. ||
                            std::abs(y) > MaxGridCoordinate * 3.) return hint;
                        hint.X = static_cast<Integer>(std::llround(x));
                        hint.Y = static_cast<Integer>(std::llround(y));
                        if (hint.X < margin || hint.Y < margin || hint.X + canonical.Width > W - margin ||
                            hint.Y + canonical.Height > H - margin) return hint;
                        Integer exactX = 0, exactY = 0;
                        hint.Rounded = !Lossless(source.Transform.X, scale, exactX) || !Lossless(source.Transform.Y, scale, exactY);
                        hint.Position = true;
                        return hint;
                    };
                    std::vector<Integer> projectedAreas(boards);
                    const auto retained = std::min(boards, sourceOrder.size());
                    for (std::size_t board = 0; board < retained; ++board)
                    {
                        const auto original = sourceOrder[board];
                        projectedAreas[board] = sourceAreas[original];
                        for (const auto& pose : CurrentTaskHint->Layout.Layouts[original].Placements)
                        {
                            const auto index = variableIndices.at({pose.PartID, pose.Instance});
                            hints[index] = makeHint(index, pose, board, false);
                        }
                    }
                    std::vector<std::size_t> donors;
                    for (std::size_t board = retained; board < sourceOrder.size(); ++board)
                        for (const auto& pose : CurrentTaskHint->Layout.Layouts[sourceOrder[board]].Placements)
                            donors.push_back(variableIndices.at({pose.PartID, pose.Instance}));
                    std::stable_sort(donors.begin(), donors.end(), [&](std::size_t a, std::size_t b)
                        { return types[instances[a].Type].Material2 > types[instances[b].Type].Material2; });
                    for (const auto index : donors)
                    {
                        const auto area = types[instances[index].Type].Material2;
                        std::optional<std::size_t> fitting;
                        for (std::size_t board = 0; board < boards; ++board)
                            if (projectedAreas[board] + area <= capacity &&
                                (!fitting || projectedAreas[board] > projectedAreas[*fitting])) fitting = board;
                        const auto board = fitting.value_or(static_cast<std::size_t>(
                            std::min_element(projectedAreas.begin(), projectedAreas.end()) - projectedAreas.begin()));
                        const auto& instance = instances[index];
                        const Identity identity{problem.Parts[types[instance.Type].Original].ID, instance.Copy};
                        hints[index] = makeHint(index, *sourcePoses.at(identity), board, true);
                        projectedAreas[board] += area;
                    }
                    std::vector<std::size_t> order(boards), labels(boards);
                    std::iota(order.begin(), order.end(), 0);
                    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
                        { return projectedAreas[a] > projectedAreas[b]; });
                    for (std::size_t label = 0; label < boards; ++label) labels[order[label]] = label;
                    for (auto& hint : hints) if (hint) hint->Bin = static_cast<Integer>(labels[hint->Bin]);
                    return hints;
                };
                const auto prepared=elapsed();
                result.Stats.PreparationSeconds=prepared;result.Stats.VerificationSeconds=std::max(0.,result.Stats.VerificationSeconds);
                scheduling.emplace(Partition?Partition->Groups.size():lower,Partition?Partition->Groups.size():upper);
                while(const auto next=scheduling->Next(!stop(),cancelled))
                {
                    const auto plan=*next;const auto boards=plan.Boards;
                    if (!Partition && CurrentTaskHint && result.GeometryValidated && boards == result.Layouts.size())
                    {
                        result.Diagnostics.push_back("sheet-count.repair: target=" + std::to_string(boards) +
                            "; validated-current-task-incumbent=1; solver-rebuild=0");
                        scheduling->Observe(plan, Detail::SheetCountOutcome::Complete);
                        if (scheduling->AtAreaLowerBound()) return finish("current-task incumbent at material-area lower bound");
                        continue;
                    }
                    if(count>options.MaxPlacementAttempts-result.Stats.PlacementAttempts)
                        return finish("complete-original validation budget exhausted; checked incumbent retained");
                    ++plans;++result.Stats.CandidatePatterns;const auto planStart=elapsed();Sat::CpModelBuilder cp;
                    if(plan.Revisit)++revisits;
                    struct Variable{Sat::IntVar X,Y,Rotation,Bin;std::vector<Sat::BoolVar> In;};std::vector<Variable> vars;
                    for(std::size_t i=0;i<count;++i)
                    {
                        if(stop())return finish("budget during model variables");const auto& type=types[instances[i].Type];std::vector<Integer> allowed,widths,heights;
                        for(int q=0;q<4;++q)
                        {const auto& v=type.Versions[q];widths.push_back(v.Width);heights.push_back(v.Height);
                            if(v.Width<=W-2*margin&&v.Height<=H-2*margin&&std::find(type.Quarters.begin(),type.Quarters.end(),q)!=type.Quarters.end())allowed.push_back(q);}
                        Variable v;v.X=cp.NewIntVar(operations_research::Domain(margin,W-margin));v.Y=cp.NewIntVar(operations_research::Domain(margin,H-margin));
                        v.Rotation=cp.NewIntVar(operations_research::Domain::FromValues(allowed));v.Bin=cp.NewIntVar(operations_research::Domain(0,boards-1));
                        auto width=cp.NewIntVar(operations_research::Domain(*std::min_element(widths.begin(),widths.end()),*std::max_element(widths.begin(),widths.end())));
                        auto height=cp.NewIntVar(operations_research::Domain(*std::min_element(heights.begin(),heights.end()),*std::max_element(heights.begin(),heights.end())));
                        cp.AddElement(v.Rotation,widths,width);cp.AddElement(v.Rotation,heights,height);
                        cp.AddLessOrEqual(v.X+width,W-margin);cp.AddLessOrEqual(v.Y+height,H-margin);Sat::LinearExpr bin;
                        for(std::size_t b=0;b<boards;++b){v.In.push_back(cp.NewBoolVar());bin+=Sat::LinearExpr(v.In.back())*static_cast<Integer>(b);}
                        cp.AddExactlyOne(v.In);cp.AddEquality(v.Bin,bin);vars.push_back(std::move(v));
                        // The ordinary constructor may order interchangeable
                        // copies. Partition exclusion distinguishes their IDs,
                        // so that symmetry constraint is disabled in that mode.
                        if(!Partition&&!CurrentTaskHint&&i&&instances[i-1].Type==instances[i].Type)
                            cp.AddLessOrEqual(vars[i-1].Bin,vars[i].Bin);
                    }
                    if (Partition)
                    {
                        std::vector<Sat::BoolVar> changed;
                        for (const auto& [anchor, member] : changedPairs)
                        {
                            const auto separated = cp.NewBoolVar();
                            cp.AddNotEqual(vars[anchor].Bin, vars[member].Bin).OnlyEnforceIf(separated);
                            changed.push_back(separated);
                        }
                        cp.AddBoolOr(changed);
                        partitionExclusionAdded = true;
                    }
                    // A complete-layout half turn preserves a rectangular
                    // stock, area-sorted board labels and copy-bin ordering.
                    // Apply only when every original angle domain is closed
                    // under +180 degrees; never also anchor this part to bin0.
                    if(!CurrentTaskHint&&std::all_of(types.begin(),types.end(),[](const Prepared& p){return p.HalfTurnClosed;}))
                        cp.AddLessOrEqual(vars.front().Rotation,1);
                    std::vector<Sat::IntVar> boardAreas;
                    for(std::size_t b=0;b<boards;++b)
                    {
                        auto area=cp.NewIntVar(operations_research::Domain(1,capacity));boardAreas.push_back(area);Sat::LinearExpr sum;
                        for(std::size_t i=0;i<count;++i)sum+=Sat::LinearExpr(vars[i].In[b])*types[instances[i].Type].Material2;
                        cp.AddEquality(area,sum);if(b)cp.AddGreaterOrEqual(boardAreas[b-1],area);
                    }
                    // Fixed-count perturbations concentrate original material
                    // into preceding sheets, leaving the least-filled sheet
                    // easier to eliminate in the following reduction phase.
                    if (Partition) cp.Minimize(boardAreas.back());
                    if(Settings.EnableInteriorRectangles)
                    {
                        struct InteriorVariable{Sat::IntVar X,Y,Width,Height,Right,Top;};std::vector<std::optional<InteriorVariable>> rectangles(count);
                        for(std::size_t i=0;i<count;++i)
                        {
                            if(stop())return finish("budget during interior rectangle variables");const auto& type=types[instances[i].Type];if(!type.Interior[0])continue;
                            const auto tableVar=[&](int field)
                            {
                                std::vector<Integer> values;
                                for(const auto& rect:type.Interior)
                                    values.push_back(field==0?rect->X:field==1?rect->Y:field==2?rect->Width:rect->Height);
                                auto v=cp.NewIntVar(operations_research::Domain(*std::min_element(values.begin(),values.end()),*std::max_element(values.begin(),values.end())));
                                cp.AddElement(vars[i].Rotation,values,v);return v;
                            };
                            const auto offsetX=tableVar(0),offsetY=tableVar(1),width=tableVar(2),height=tableVar(3);
                            const auto x=cp.NewIntVar(operations_research::Domain(margin,W-margin)),y=cp.NewIntVar(operations_research::Domain(margin,H-margin));
                            const auto right=cp.NewIntVar(operations_research::Domain(margin,W-margin)),top=cp.NewIntVar(operations_research::Domain(margin,H-margin));
                            cp.AddEquality(x,vars[i].X+offsetX);cp.AddEquality(y,vars[i].Y+offsetY);
                            cp.AddEquality(right,x+width);cp.AddEquality(top,y+height);
                            rectangles[i]=InteriorVariable{x,y,width,height,right,top};
                        }
                        for(std::size_t b=0;b<boards;++b)
                        {
                            auto overlap=cp.AddNoOverlap2D();
                            for(std::size_t i=0;i<count;++i)if(rectangles[i])
                            {
                                if(stop())return finish("budget during redundant interior packing model");const auto& r=*rectangles[i];
                                const auto alongX=cp.NewOptionalIntervalVar(r.X,r.Width,r.Right,vars[i].In[b]);
                                const auto alongY=cp.NewOptionalIntervalVar(r.Y,r.Height,r.Top,vars[i].In[b]);
                                overlap.AddRectangle(alongX,alongY);
                            }
                        }
                    }
                    std::map<std::tuple<std::size_t,Integer,Integer,bool,int>,Sat::IntVar> features;
                    const auto feature=[&](std::size_t i,Normal n,bool high,int kind)
                    {
                        const auto key=std::make_tuple(i,n.X,n.Y,high,kind);const auto found=features.find(key);if(found!=features.end())return found->second;
                        std::vector<Integer> table;
                        for(const auto& v:types[instances[i].Type].Versions)
                        {
                            Integer support=high?std::numeric_limits<Integer>::min():std::numeric_limits<Integer>::max();
                            for(const auto p:v.Points){const auto projection=Dot(p,n);support=high?std::max(support,projection):std::min(support,projection);}
                            Integer low=std::numeric_limits<Integer>::max(),top=std::numeric_limits<Integer>::min();
                            for(const auto p:v.Points)if(Dot(p,n)==support){const auto t=-n.Y*p.X+n.X*p.Y;low=std::min(low,t);top=std::max(top,t);}
                            table.push_back(kind==0?support:kind==1?low:kind==2?top:Integer(top>low));
                        }
                        auto var=cp.NewIntVar(operations_research::Domain(*std::min_element(table.begin(),table.end()),*std::max_element(table.begin(),table.end())));
                        cp.AddElement(vars[i].Rotation,table,var);features.emplace(key,var);return var;
                    };
                    for(std::size_t i=0;i<count;++i)for(std::size_t j=0;j<i;++j)
                    {
                        if(stop())return finish("budget during original separation model");auto same=cp.NewBoolVar();
                        cp.AddEquality(vars[i].Bin,vars[j].Bin).OnlyEnforceIf(same);cp.AddNotEqual(vars[i].Bin,vars[j].Bin).OnlyEnforceIf(same.Not());
                        std::set<Normal> normals;for(const auto ti:{instances[i].Type,instances[j].Type})for(const auto& v:types[ti].Versions)normals.insert(v.Normals.begin(),v.Normals.end());
                        std::vector<Sat::BoolVar> clause{same.Not()};
                        for(const auto n:normals)for(const auto [a,b]:{std::pair(i,j),std::pair(j,i)})
                        {
                            const auto distance=Sat::LinearExpr(vars[b].X-vars[a].X)*n.X+Sat::LinearExpr(vars[b].Y-vars[a].Y)*n.Y+
                                feature(b,n,false,0)-feature(a,n,true,0);
                            auto separation=cp.NewBoolVar();cp.AddGreaterOrEqual(distance,ProjectedGap(gap,n)).OnlyEnforceIf(separation);clause.push_back(separation);
                            if(problem.AllowCommonEdges&&gap>0)
                            {
                                auto shared=cp.NewBoolVar();clause.push_back(shared);cp.AddEquality(distance,0).OnlyEnforceIf(shared);
                                cp.AddEquality(feature(a,n,true,3),1).OnlyEnforceIf(shared);cp.AddEquality(feature(b,n,false,3),1).OnlyEnforceIf(shared);
                                const auto ta=Sat::LinearExpr(vars[a].X)*(-n.Y)+Sat::LinearExpr(vars[a].Y)*n.X;
                                const auto tb=Sat::LinearExpr(vars[b].X)*(-n.Y)+Sat::LinearExpr(vars[b].Y)*n.X;
                                cp.AddGreaterOrEqual(ta+feature(a,n,true,2),tb+feature(b,n,false,1)+1).OnlyEnforceIf(shared);
                                cp.AddGreaterOrEqual(tb+feature(b,n,false,2),ta+feature(a,n,true,1)+1).OnlyEnforceIf(shared);
                            }
                        }
                        cp.AddBoolOr(clause);
                    }
                    // Cold construction asks only for a complete feasible
                    // fixed-count layout. Perturbations improve their material
                    // concentration objective throughout this local run budget.
                    if (CurrentTaskHint)
                    {
                        const auto hints = buildHints(boards);
                        std::size_t planInstances=0,planPositions=0,planRounded=0,planCanonical=0,planDonors=0;
                        for (std::size_t i = 0; i < count; ++i) if (hints[i])
                        {
                            const auto& hint = *hints[i];
                            cp.AddHint(vars[i].Bin, hint.Bin);
                            for (std::size_t board = 0; board < boards; ++board)
                                cp.AddHint(vars[i].In[board], static_cast<Integer>(board) == hint.Bin);
                            if (hint.Rotation >= 0) cp.AddHint(vars[i].Rotation, hint.Rotation);
                            if (hint.Position)
                            {
                                cp.AddHint(vars[i].X, hint.X);
                                cp.AddHint(vars[i].Y, hint.Y);
                                ++planPositions;
                            }
                            ++planInstances;
                            planRounded += hint.Rounded;
                            planCanonical += hint.CanonicalRotation;
                            planDonors += hint.Donor;
                        }
                        hintedInstances += planInstances;
                        positionHints += planPositions;
                        roundedHints += planRounded;
                        canonicalHints += planCanonical;
                        donorHints += planDonors;
                        std::ostringstream hintNote;
                        hintNote << diagnostic << ": hint-plan-target=" << boards
                            << "; current-task-hint=1; hint-source-validated=1; hint-hard-locks=0"
                            << "; hinted-instances=" << planInstances << "; hinted-position-instances=" << planPositions
                            << "; rounded-hint-instances=" << planRounded << "; canonical-rotation-hints=" << planCanonical
                            << "; donor-hint-instances=" << planDonors
                            << "; hint-conflict-limit=" << std::min<std::int64_t>(100000, Settings.MaxConflicts);
                        result.Diagnostics.push_back(hintNote.str());
                    }
                    if(stop())return finish("budget before fixed-count solve");
                    const auto left=options.TimeLimitSeconds-elapsed();
                    double seconds = left;
                    if (!Partition)
                        seconds = plan.Revisit ? left / static_cast<double>(plan.PendingInRound) :
                            std::min(Settings.SecondsPerBoardCount, left);
                    auto planOptions=options;planOptions.Seed=options.Seed+plans-1;
                    auto response=SolveBounded(cp.Build(),solverSettings,planOptions,seconds,pollCancel,stop,!Partition.has_value());conflicts+=response.num_conflicts();
                    std::ostringstream run;run<<std::setprecision(17)<<diagnostic<<": target="<<boards<<"; revisit="<<plan.Revisit<<"; round="<<plan.Round
                        <<"; solver-seed="<<(planOptions.Seed&0x7fffffff)<<"; solver-budget="<<seconds
                        <<"; status="<<Sat::CpSolverStatus_Name(response.status())<<"; seconds="<<elapsed()-planStart;
                    if (Partition)
                    {
                        const double areaUnits = 2. * scale * scale;
                        run << "; consolidation-objective=1; local-tail-bound-mm2=" << response.best_objective_bound() / areaUnits;
                        if (response.status() == Sat::CpSolverStatus::FEASIBLE || response.status() == Sat::CpSolverStatus::OPTIMAL)
                            run << "; local-tail-objective-mm2=" << response.objective_value() / areaUnits;
                    }
                    result.Diagnostics.push_back(run.str());
                    if(response.status()==Sat::CpSolverStatus::UNKNOWN)
                    {
                        ++unknown;
                        if(Partition)return finish("fixed-count perturbation unresolved within budget");
                        scheduling->Observe(plan,Detail::SheetCountOutcome::Unknown);continue;
                    }
                    if(response.status()==Sat::CpSolverStatus::INFEASIBLE)
                    {
                        if(Partition)return finish("no changed partition on the conservative fixed-count lattice");
                        scheduling->Observe(plan,Detail::SheetCountOutcome::InfeasibleOnLattice);continue;
                    }
                    if(response.status()!=Sat::CpSolverStatus::FEASIBLE&&response.status()!=Sat::CpSolverStatus::OPTIMAL)
                        return finish("solver did not return a usable fixed-count status; checked incumbent retained");
                    Result candidate;candidate.State=Status::Complete;
                    for(std::size_t b=0;b<boards;++b)candidate.Layouts.push_back({stock->ID,b,stock->Width,stock->Height,{},0});
                    for(std::size_t i=0;i<count;++i)
                    {
                        const auto& type=types[instances[i].Type];const auto& part=problem.Parts[type.Original];const auto& v=vars[i];
                        const auto quarter=Sat::SolutionIntegerValue(response,v.Rotation),board=Sat::SolutionIntegerValue(response,v.Bin);
                        const auto& shape=type.Versions[quarter];const double x=double(Sat::SolutionIntegerValue(response,v.X)-shape.OriginX)/scale,y=double(Sat::SolutionIntegerValue(response,v.Y)-shape.OriginY)/scale;
                        const bool gapFiller = CurrentTaskHint ? sourcePoses.at({part.ID, instances[i].Copy})->GapFiller : false;
                        candidate.Layouts[board].Placements.push_back({part.ID,instances[i].Copy,{x,y,double(quarter*90)},0,gapFiller});
                        candidate.Layouts[board].MaterialArea+=double(type.Material2)/(2.*scale*scale);
                    }
                    if(Settings.EnableRoundoffRefinement&&problem.PartGap>0&&!problem.AllowCommonEdges)
                    {
                        bool nominal=true;
                        for(const auto& layout:candidate.Layouts)
                        {
                            std::vector<AxisSafety::Geometry> originals;
                            for(const auto& q:layout.Placements)
                            {
                                const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),[&](const Part& value){return value.ID==q.PartID;});
                                auto geometry=AxisSafety::Make(part->Geometry,q.Transform);
                                nominal&=AxisSafety::Containment(geometry,layout.Width,layout.Height,problem.EdgeMargin)==AxisSafety::Verdict::Safe;
                                for(const auto& fixed:originals)nominal&=AxisSafety::Pair(geometry,fixed,problem.PartGap,false,problem.Tolerance)==AxisSafety::Verdict::Safe;
                                originals.push_back(std::move(geometry));
                            }
                        }
                        if(!nominal)
                        {
                            ContactPrecisionOptions precision;precision.Clearance=1e-7;precision.MaxAxisShift=2e-4;
                            precision.TimeLimitSeconds=std::min(.5,std::max(0.,options.TimeLimitSeconds-elapsed()));precision.Cancel=pollCancel;
                            const auto adjusted=RefineContactPrecision(problem,candidate,precision);
                            if(adjusted.State==ContactPrecisionStatus::Applied||adjusted.State==ContactPrecisionStatus::AlreadySeparated)
                            {candidate=adjusted.Output;candidate.Diagnostics.push_back("sheet-count: original-pose roundoff repaired within bounded micro-translations; nominal positive gaps verified");}
                            else
                                candidate.Diagnostics.push_back("sheet-count: bounded nominal roundoff repair unavailable; original input validation tolerance remains authoritative: "+adjusted.Diagnostic);
                        }
                    }
                    const auto before=elapsed();const auto check=Validate(problem,candidate);result.Stats.VerificationSeconds+=elapsed()-before;
                    result.Stats.PlacementAttempts+=count;
                    if(!check.Valid||!check.Complete||check.PlacedCount!=count)
                    {result.Diagnostics.insert(result.Diagnostics.end(),check.Errors.begin(),check.Errors.end());return finish("fixed-count candidate failed original validation");}
                    if (Partition)
                    {
                        partitionChanged = std::any_of(changedPairs.begin(), changedPairs.end(), [&](const auto& pair)
                        {
                            return Sat::SolutionIntegerValue(response, vars[pair.first].Bin) !=
                                Sat::SolutionIntegerValue(response, vars[pair.second].Bin);
                        });
                        if (!partitionChanged) return finish("fixed-count candidate failed original partition exclusion");
                    }
                    candidate.GeometryValidated=true;candidate.Diagnostics=std::move(result.Diagnostics);candidate.Stats=result.Stats;result=std::move(candidate);
                    if (Partition) return finish("validated complete fixed-count partition perturbation; minimum-count-proof=0");
                    scheduling->Observe(plan,Detail::SheetCountOutcome::Complete);
                    result.Stats.PreparationSeconds=prepared;result.Stats.PlacementSeconds=elapsed()-prepared-result.Stats.VerificationSeconds;
                    if (!Partition && Settings.StopAfterFirstComplete)
                    {
                        firstCompleteHandoff = true;
                        return finish("validated first complete candidate handed off for current-task guided repair");
                    }
                    if(scheduling->AtAreaLowerBound())return finish("complete at material-area lower bound");
                }
                result.Stats.PlacementSeconds=elapsed()-prepared-result.Stats.VerificationSeconds;
                if(result.GeometryValidated)return finish(scheduling->UnresolvedCount()?
                    "complete incumbent retained; smaller unknown counts remain unresolved":"complete incumbent retained; smaller counts rejected by conservative lattice model only");
                return finish("no complete candidate within fixed-count budget; no global infeasibility claim");
            }
            catch(const std::exception& error){return finish(std::string("failed safely: ")+error.what());}
            catch(...){return finish("failed safely with nonstandard exception");}
        }
    };
}
StrategyFactory MakeSheetCountStrategy(SheetCountSettings settings)
{
    CheckSettings(settings);
    return [settings]{return std::make_unique<SheetCountStrategy>(settings);};
}
StrategyFactory MakeSheetCountRepairStrategy(SheetCountCurrentTaskHint currentTaskHint, SheetCountSettings settings)
{
    CheckSettings(settings);
    return [settings, currentTaskHint = std::move(currentTaskHint)]
    {
        return std::make_unique<SheetCountStrategy>(settings, std::nullopt, currentTaskHint);
    };
}
StrategyFactory MakeSheetCountPerturbationStrategy(SheetCountPartition partition, SheetCountSettings settings)
{
    CheckSettings(settings);
    return [settings, partition = std::move(partition)]
    {
        return std::make_unique<SheetCountStrategy>(settings, partition);
    };
}
StrategyFactory MakeSheetCountPerturbationStrategy(SheetCountPartition partition,
    SheetCountCurrentTaskHint currentTaskHint, SheetCountSettings settings)
{
    CheckSettings(settings);
    return [settings, partition = std::move(partition), currentTaskHint = std::move(currentTaskHint)]
    {
        return std::make_unique<SheetCountStrategy>(settings, partition, currentTaskHint);
    };
}
}
