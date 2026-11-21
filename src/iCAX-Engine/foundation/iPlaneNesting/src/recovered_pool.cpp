#include "recovered_pool.hpp"
#include <bit>
#include <cmath>
#include <algorithm>
namespace study::recovered {
namespace {
int64_t truncate64(double v){if(!std::isfinite(v)||v>=9223372036854775808.||v<-9223372036854775808.)return INT64_MIN;return static_cast<int64_t>(v);}
int32_t truncate32(double v){if(!std::isfinite(v)||v>=2147483648.||v<-2147483648.)return INT32_MIN;return static_cast<int32_t>(v);}
double roundedInput(double v){return v>=0.?v+.5:v-.5;}
bool belowEqualUnordered(double a,double b){return !(a>b);}
int64_t subtract64(int64_t a,int64_t b){return std::bit_cast<int64_t>(static_cast<uint64_t>(a)-static_cast<uint64_t>(b));}
}
bool metricBetter(double a,double b) {
    constexpr double epsilon=1e-9,ratio=1.0001;
    if(std::abs(a-0.)<epsilon && std::abs(b-0.)<epsilon)return false;
    if(std::abs(b-0.)<epsilon && !(std::abs(0.-a)<epsilon) && a>0.)return true;
    if(!(a>b))return false;
    const double q=a/b;
    if(std::abs(ratio-q)<epsilon)return false;
    return q>ratio;
}
int candidateTie(const CandidateMetrics &a,const CandidateMetrics &b) {
    if(a.correctedLength==b.correctedLength) {
        if(a.auxiliary==b.auxiliary) {
            if(a.integerMetric==b.integerMetric)return 2;
            return a.integerMetric>=b.integerMetric;
        }
        return belowEqualUnordered(a.auxiliary,b.auxiliary);
    }
    const double q=(double(a.integerMetric)/1000.+1e-20)/(double(b.integerMetric)/1000.+1e-20);
    const double lower=belowEqualUnordered(.9999,q)?q:.9999;
    const double ratio=belowEqualUnordered(q,1.0001)?lower:1.0001;
    return belowEqualUnordered(double(b.correctedLength)/1000000.,(double(a.correctedLength)/1000000.)*ratio);
}
bool candidateLess(const CandidateMetrics &a,const CandidateMetrics &b,const std::function<int32_t()> &difference,bool useSourceId) {
    if(metricBetter(a.metric,b.metric))return true;
    if(metricBetter(b.metric,a.metric))return false;
    const int tie=candidateTie(a,b);if(tie!=2)return tie==0;
    const auto aid=useSourceId?a.sourceId:0,bid=useSourceId?b.sourceId:0;
    if(aid==bid && a.trial==b.trial)return false;
    if(difference()<100)return false;
    return aid==bid?a.trial<b.trial:aid<bid;
}
int32_t candidateInteger(const std::vector<CandidateIntegerTerm> &terms,double sheet78) {
    int32_t largest=0;
    for(auto t:terms)largest=std::max(largest,std::bit_cast<int32_t>(uint32_t(t.placement50)+uint32_t(t.objectF8)));
    return std::bit_cast<int32_t>(uint32_t(largest)-uint32_t(truncate32(roundedInput(sheet78*1000.))));
}
int64_t candidateLength(const CandidateLengthInput &v) {
    int64_t result=subtract64(v.maximumRight,truncate64(roundedInput(v.trim70*1000000.)));
    if(v.config3c) {
        const double rectangle=v.height10*v.width08;
        result=truncate64(roundedInput(((rectangle-v.quantity3cc260())/v.height10)*1000000.));
    }
    const double correction=((v.quantity3cbe50()/v.config10)/v.height10)*1000000.;
    return subtract64(result,truncate64(roundedInput(correction)));
}
int64_t rawCandidateLength(int64_t right,double trim){return subtract64(right,truncate64(roundedInput(trim*1000000.)));}
int64_t referenceCandidateLength(int64_t corrected,double field14e8,double factor) {
    const double adjustment=(((1.-field14e8)*factor)*(double(corrected)/1000000.))*1000000.;
    return subtract64(corrected,truncate64(roundedInput(adjustment)));
}
bool expectedCandidateBetter(const CandidateMetrics &first, double firstShake,
    const CandidateMetrics &second, double secondShake, double measuredGain) {
    if (metricBetter(first.metric, second.metric)) return true;
    if (metricBetter(second.metric, first.metric)) return false;
    const double firstFactor = (firstShake * 4.) * measuredGain + measuredGain;
    const double secondFactor = (secondShake * 4.) * measuredGain + measuredGain;
    return referenceCandidateLength(first.correctedLength, firstShake, firstFactor) <
           referenceCandidateLength(second.correctedLength, secondShake, secondFactor);
}
double measuredShakerGain(const ShakerGainInput &input) {
    const double threshold = ((input.target14b8 - input.queue20) + input.placed10) * .9999999;
    if (threshold > input.placed10) return 0.;
    const double original = double(input.originalLength10) / 1e6;
    const double current = double(rawCandidateLength(input.maximumRight, input.trim70)) / 1e6;
    return (original - current) / original;
}
double medianShakerGain(std::span<const double> gains) {
    if (gains.empty()) return 0.;
    std::vector<double> sorted(gains.begin(), gains.end());
    std::sort(sorted.begin(), sorted.end());
    return sorted[sorted.size() / 2];
}
bool useExpectedShakerGain(double gain) { return gain > .0001; }
void evaluateCorrectedLength(CorrectedLengthCache &cache,const CandidateLengthInput &input,const std::function<void()> &refreshStrip) {
    if(input.config3c)refreshStrip();
    const auto length=candidateLength(input);cache.valid=true;cache.value=length;
}
std::vector<StripColumn> stripColumns(std::span<const int32_t> heights,int32_t previous) {
    if(heights.size()>size_t(INT32_MAX))throw std::length_error("strip column count exceeds original field");
    std::vector<StripColumn> out;
    for(size_t i=0;i<heights.size();++i)if(heights[i]!=previous){out.push_back({int32_t(i),previous});previous=heights[i];}
    if(double(previous)/1000.>0.)out.push_back({int32_t(heights.size()),previous});
    return out;
}
int64_t stripAggregate(std::span<const StripColumn> columns) {
    if(columns.empty())return 0;
    if(columns.size()>size_t(INT32_MAX))throw std::length_error("strip pair count exceeds original field");
    double single=0.;
    for(auto c:columns){const double v=(double(c[1])/1000.)*double(c[0]);single=v>single?v:single;}
    double best=single;
    for(size_t i=0;i+1<columns.size();++i) {
        const auto a=columns[i];const double base=(double(a[1])/1000.)*double(a[0]);
        if(best>single+base)continue;
        for(size_t j=i+1;j<columns.size();++j) {
            const auto b=columns[j];const int32_t dx=std::bit_cast<int32_t>(uint32_t(b[0])-uint32_t(a[0]));
            const double v=(double(b[1])/1000.)*double(dx)+base;best=v>best?v:best;
        }
    }
    return truncate64(roundedInput(best*1000.));
}
void refreshStripQuantities(StripQuantityCache &cache,std::span<const int32_t> heights,int32_t initial,int64_t field80,int64_t pitch) {
    const double a=(double(field80)/1000.)*(double(pitch)/1000000.);
    const int64_t aggregate=stripAggregate(stripColumns(heights,initial));
    const double b=(double(pitch)/1000000.)*(double(aggregate)/1000.);
    cache.field88=a;cache.field90=b;cache.valid=true;
}
bool processEdgeActive(const ProcessEdge &e) {
    if(e.first.expired() || e.second.expired())return false;
    auto a=e.first.lock(),b=e.second.lock();
    return a && b && a->group08 && a->group08==b->group08;
}
bool processEdgeOwned(const ProcessEdge &e,const ProcessPlacement *p){return e.first.lock().get()==p;}
double candidateAuxiliary(const ProcessPlacement &p) {
    double sum=0.;for(const auto &e:p.edges)if(processEdgeActive(e)&&processEdgeOwned(e,&p))sum+=e.field20;return sum;
}
void linkCommonCut(const std::shared_ptr<ProcessPlacement> &first,const std::shared_ptr<ProcessPlacement> &second,double quantity) {
    if(!first || !second)throw std::invalid_argument("common cut requires two live placements");
    const ProcessEdge edge{first,second,quantity};
    first->edges.push_back(edge);second->edges.push_back(edge);
}
double copyPlacementProcesses(const ProcessPlacement &source,const std::shared_ptr<ProcessPlacement> &target,
    const PlacementCopyLookup &lookup,const std::function<void()> &missingPlacement) {
    if(!target || !lookup)throw std::invalid_argument("process copy requires destination and lookup");
    if(&source==target.get())throw std::invalid_argument("process copy requires a distinct destination snapshot");
    target->edges.reserve(source.edges.size());
    double total=0.;
    for(const auto &edge:source.edges) {
        if(!processEdgeActive(edge) || !processEdgeOwned(edge,&source))continue;
        const auto other=edge.second.lock();
        if(!other)throw std::runtime_error("process endpoint expired during copy");
        const auto copied=lookup(other->instance40);
        if(!copied)continue;
        if(!*copied){if(missingPlacement)missingPlacement();continue;}
        // ADD scalar double retains the destination NaN payload before link.
        if(std::isnan(total))total=std::bit_cast<double>(std::bit_cast<uint64_t>(total)|0x8000000000000ull);
        else if(std::isnan(edge.field20))total=std::bit_cast<double>(std::bit_cast<uint64_t>(edge.field20)|0x8000000000000ull);
        else total+=edge.field20;
        linkCommonCut(target,*copied,edge.field20);
    }
    return total;
}
std::array<double,3> candidateProcessQuantities(const std::vector<std::shared_ptr<ProcessPlacement>> &placements) {
    std::array<double,3> out{};
    for(const auto &p:placements) {
        const double value=(p->optional?0.:p->geometry60)-candidateAuxiliary(*p);
        const bool leader=p->group58? (p->group58->members.empty() || p->group58->members.front().lock().get()==p.get())
                                   : (!p->groupFirst || p->groupFirst->lock().get()==p.get());
        if(leader) {
            out[0]=value+out[0];out[2]=candidateAuxiliary(*p)+out[2];
        }else out[1]=value+out[1];
    }return out;
}
void copyPlacementRelations(const ProcessPlacement &source,const std::shared_ptr<ProcessPlacement> &target,
    const PlacementCopyLookup &lookup) {
    if(!target || !lookup)throw std::invalid_argument("relation copy requires destination and lookup");
    if(!source.group58 || source.group58->members.empty())return;
    std::shared_ptr<PlacementGroup> group;
    for(const auto &member:source.group58->members) {
        const auto existing=member.lock();
        if(!existing)throw std::invalid_argument("source relation group contains a missing placement");
        const auto copied=lookup(existing->instance40);
        if(copied && *copied && (*copied)->group58){group=(*copied)->group58;break;}
    }
    if(!group){group=std::make_shared<PlacementGroup>();group->members.resize(source.group58->members.size());}
    target->group58=group;
    const auto &members=source.group58->members;
    for(size_t i=0;i<members.size();++i)if(members[i].lock().get()==&source) {
        if(i>=group->members.size())throw std::invalid_argument("copied relation group has inconsistent size");
        group->members[i]=target;break;
    }
}
double candidateQuantity260(int64_t right,double trim,double width,double height,bool mode,StripQuantityCache &cache,const std::function<void(StripQuantityCache &)> &refresh) {
    const double length=double(subtract64(right,truncate64(roundedInput(trim*1000000.))))/1000000.;
    const double base=(width-length)*height;
    if(!mode)return base;
    if(!cache.valid)refresh(cache);
    const double field90=cache.field90;
    if(!cache.valid)refresh(cache);
    return (cache.field88*.05+field90*.94)+base*.01;
}
double candidateQuantityE50(const std::array<double,3> &q,double informer20,double config18,double config20) {
    return config18*informer20-((config18*q[0]+0.)+q[1]*config20);
}
LayoutGroups layoutGroups(const LayoutSignature &v) {
    if(v.placements.size()>size_t(INT32_MAX))throw std::length_error("layout group count overflow");
    LayoutGroups out;out.starts.resize(v.typeCount);out.counts.resize(v.typeCount);
    for(auto p:v.placements){if(p.type>=v.typeCount)throw std::invalid_argument("layout type index");++out.counts[p.type];}
    for(size_t i=1;i<v.typeCount;++i)out.starts[i]=out.starts[i-1]+out.counts[i-1];
    auto cursor=out.starts;out.points.resize(v.placements.size());
    for(auto p:v.placements)out.points[cursor[p.type]++]={double(p.x48)/1000000.,double(p.y50)/1000.};
    return out;
}
double layoutGroupDistance(LayoutPoints &a,LayoutPoints &b) {
    if(a.size()!=b.size())throw std::invalid_argument("layout group size mismatch");
    // Coordinates originate in signed integer fields. Thus no NaN/non-strict
    // comparator cases occur here; identical keys are identical coordinates.
    for(const auto *points:{&a,&b})for(auto p:*points)if(!std::isfinite(p[0])||!std::isfinite(p[1]))throw std::invalid_argument("nonfinite layout coordinate");
    auto less=[](auto a,auto b){return a[0]<b[0] || (a[0]==b[0] && a[1]<b[1]);};
    std::sort(a.begin(),a.end(),less);std::sort(b.begin(),b.end(),less);
    double sum=0.;
    for(size_t i=0;i<a.size();++i){const double dy=b[i][1]-a[i][1],dx=b[i][0]-a[i][0];sum+=dy*dy+dx*dx;}
    return sum;
}
int32_t layoutDifference(const LayoutSignature &a,const LayoutSignature &b) {
    if(a.placementCount!=b.placementCount || a.sheetIdentity!=b.sheetIdentity)return 100;
    double scale=a.height10/1000.;
    const double length=double(subtract64(a.maximumRight,truncate64(roundedInput(a.trim70*1000000.))))/1000000.;
    if(length>0.)scale*=length;
    if(scale<=0.)scale=.1;
    auto ga=layoutGroups(a),gb=layoutGroups(b);double sum=0.;
    for(auto type:a.typeOrder) {
        if(type>=ga.counts.size() || type>=gb.counts.size())throw std::invalid_argument("layout catalog index");
        if(ga.counts[type]!=gb.counts[type])return 100;
        if(ga.counts[type]) {
            LayoutPoints pa(ga.points.begin()+ga.starts[type],ga.points.begin()+ga.starts[type]+ga.counts[type]);
            LayoutPoints pb(gb.points.begin()+gb.starts[type],gb.points.begin()+gb.starts[type]+gb.counts[type]);
            sum+=layoutGroupDistance(pa,pb)/scale;if(sum>100.)return 100;
        }
    }
    return truncate32(sum);
}
bool relatedCandidates(const LayoutSignature &first, uintptr_t secondSheet,
    double sheetWidth, double sheetHeight, double secondMetric18,
    std::span<const RelatedPlacement> secondPlacements) {
    if (first.sheetIdentity != secondSheet) return false;
    size_t count = secondPlacements.size();
    if (count > 1) {
        double quantity = 0.;
        for (size_t index = 0; index < count; ++index) {
            const auto &part = secondPlacements[index];
            const double value = part.optional3c1 ? 0. : part.quantity220 < 0. ? part.area58 : part.quantity220;
            quantity = quantity + value;
            if (secondMetric18 * .8 < quantity) { count = index; break; }
        }
    }
    auto groups = layoutGroups(first);
    const double distanceLimit = (sheetWidth < sheetHeight ? sheetWidth : sheetHeight) / 10.;
    for (size_t index = 0; index < count; ++index) {
        const auto &part = secondPlacements[index].placement;
        if (part.type >= groups.counts.size()) throw std::invalid_argument("related candidate type index");
        const auto remaining = groups.counts[part.type];
        if (remaining == 0) return false;
        const size_t begin = size_t(groups.starts[part.type]), end = begin + size_t(remaining);
        const double x = double(part.x48) / 1e6, y = double(part.y50) / 1000.;
        const auto squaredForSelection = [&](size_t slot) {
            const double dy = y - groups.points[slot][1], dx = x - groups.points[slot][0];
            return dy * dy + dx * dx;
        };
        size_t selected = begin;
        for (size_t slot = begin + 1; slot < end; ++slot)
            if (squaredForSelection(slot) < squaredForSelection(selected)) selected = slot;
        const auto &point = groups.points[selected];
        const double dx = point[0] - x, dy = point[1] - y;
        if (distanceLimit < std::sqrt(dx * dx + dy * dy)) return false;
        std::swap(groups.points[selected], groups.points[end - 1]);
        --groups.counts[part.type];
    }
    return true;
}
}
