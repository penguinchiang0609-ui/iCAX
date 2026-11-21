#include "recovered_escher_boundary_length.hpp"
#include <bit>
#include <list>
namespace study::recovered {
namespace {
int32_t narrow(uint32_t value){return std::bit_cast<int32_t>(value);}
int32_t add32(int32_t a,int32_t b){return narrow(uint32_t(a)+uint32_t(b));}
int64_t add64(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub64(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul64(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
bool limited(const EscherProviderLengthLimits &limits){return (uint32_t(limits.flags30)&255U)!=0;}
void spend(EscherProviderLengthLimits &limits,double cost){if(limited(limits)){limits.target-=cost;limits.flags30=narrow((uint32_t(limits.flags30)&0xffffff00U)|1U);}}
int32_t trunc32(double value){return value>=double(INT32_MIN)&&value<double(INT32_MAX)+1.?int32_t(value):INT32_MIN;}
EscherIntegerBox bounds(const ConvolutionIntegerPolygon &rings){require(!rings.empty()&&!rings.front().empty(),PN2S_INVALID,"Escher length boundary requires nonempty rings");const auto first=rings.front().front();EscherIntegerBox box{first[0],first[1],first[0],first[1]};for(const auto &ring:rings)for(auto p:ring)for(size_t a=0;a<2;++a){box[a]=std::min(box[a],p[a]);box[a+2]=std::max(box[a+2],p[a]);}return box;}
void translate(ConvolutionIntegerPolygon &rings,EscherIntegerPoint delta){for(auto &ring:rings)for(auto &point:ring)for(size_t a=0;a<2;++a)point[a]=add64(point[a],delta[a]);}
std::vector<int32_t> keyOrder(std::span<const int32_t> keys){const auto hash=[](int32_t key){uint64_t value=0xcbf29ce484222325;for(size_t n=0;n<4;++n){value^=uint8_t(uint32_t(key)>>(n*8));value*=0x100000001b3;}return value;};
    std::set<int32_t> known;std::list<int32_t> order;using Iterator=std::list<int32_t>::iterator;std::vector<Iterator> buckets(8,order.end());
    const auto rehash=[&](size_t capacity){buckets.assign(capacity,order.end());for(auto at=order.begin();at!=order.end();){const auto next=std::next(at);auto &first=buckets[hash(*at)&(capacity-1)];if(first!=order.end())order.splice(first,order,at);first=at;at=next;}};
    for(const auto key:keys){if(known.contains(key))continue;const auto required=size_t(std::ceil(float(known.size()+1)));if(buckets.size()<required){auto desired=std::max<size_t>(8,required);if(buckets.size()<512)desired=std::max(desired,buckets.size()*8);rehash(std::bit_ceil(desired));}known.insert(key);auto &first=buckets[hash(key)&(buckets.size()-1)];first=order.insert(first,key);}return {order.begin(),order.end()};}
}
double escherBoundaryPlacementCost(const ConvolutionIntegerPolygon &part,const ConvolutionIntegerPolygon &sheet){
    uint32_t vertices=0,complexity=0,sheetVertices=0;
    for(const auto &ring:part){vertices+=uint32_t(ring.size());uint32_t concave=0;for(size_t n=0;n<ring.size();++n){const auto &before=ring[(n+ring.size()-1)%ring.size()],&point=ring[n],&after=ring[(n+1)%ring.size()];
            const auto cross=sub64(mul64(sub64(after[1],point[1]),sub64(point[0],before[0])),mul64(sub64(point[1],before[1]),sub64(after[0],point[0])));concave+=cross<0;}complexity+=concave+1;}
    for(const auto &ring:sheet)sheetVertices+=uint32_t(ring.size());
    const double first=std::log(double(sheetVertices))*.60549-6.71161,second=std::log(double(complexity))*.22031;
    return (std::exp(std::log(double(vertices))*.71731+(first-second))*1.61443-.13758)/100.;
}
EscherBoundarySelectionPlan escherBoundarySelectionPlan(int32_t interior,int32_t partial,int32_t vertices,int32_t sequence,const EscherProviderLengthLimits &limits){
    const auto count=std::max<int32_t>(1,partial);const double logInterior=std::log(double(add32(interior,1)));
    const double fuzzyTerm=std::log(double(std::max<int32_t>(1,count)))*.281875;
    double fuzzyCost=(std::exp(std::log(double(count))*1.117342+(-4.524789-logInterior*.054901)+fuzzyTerm)*1.12443-1.018)/100.;if(!(fuzzyCost>0.))fuzzyCost=0.;
    const double available=limited(limits)?limits.target:fuzzyCost+1.;const bool fuzzy=available>fuzzyCost*24.;
    const double sampleTerm=std::log(double(sequence))*.561059+(logInterior*.016507-2.170368);
    const double sampleCost=std::exp(std::log(double(vertices))*.29783+sampleTerm)/2500000.;
    double amount=(limited(limits)?limits.target:1.)/sampleCost;
    if(amount<=144.)amount=amount>1.?1.001:0.;else amount/=144.;if(fuzzy)amount=0.;
    const double attempts=125.>amount?125.:amount;return {fuzzyCost,sampleCost,attempts,fuzzy};
}
EscherBoundaryPhaseResult escherBoundaryLengthPhase(const EscherPreparedSinglePattern &pattern,const ConvolutionIntegerPolygon &sheet,
    bool special,uint32_t previousBest,double rotationDegrees,EscherProviderLengthLimits &limits,int32_t index,EscherBoundaryLengthServices &services){
    if(limited(limits)&&limits.target<0.)return {};
    const auto &descriptor=pattern.descriptor;const auto &candidates=special?descriptor.specialCandidates:descriptor.normalCandidates;
    if(index>=int64_t(candidates.size()))return {{},true};require(index>=0,PN2S_INVALID,"Escher length candidate index is negative");
    require(bool(services.placementMap),PN2S_UNSUPPORTED,"Escher boundary length requires the actual placement-map service");
    require(rotationDegrees==0.,PN2S_UNSUPPORTED,"Escher boundary wrapper supplies only its recovered zero rotation");
    const auto &candidate=candidates[size_t(index)];const auto &direction=candidate.basis[0];
    const double radians=std::atan2(double(direction[0])*0.-double(direction[1]),double(direction[1])*0.+double(direction[0]));
    auto quantized=int64_t(std::round(radians*57.29577951308232*10000.))%3600000;if(quantized<0)quantized+=3600000;
    const auto posed=escherRotateRecords(descriptor.records,int32_t(quantized),pattern.geometries);
    std::vector<EscherBoundaryTiledPolygon> maps;maps.reserve(posed.size());
    for(size_t member=0;member<posed.size();++member){double cost;
        if(services.placementCost)cost=services.placementCost(member,sheet);else {const auto *owner=reinterpret_cast<const EscherIntegerShapeRecord *>(posed[member].geometry);require(owner!=nullptr,PN2S_INVALID,"Escher length record has no shape owner");cost=escherBoundaryPlacementCost(owner->normal.normalContours(),sheet);}
        const double available=limited(limits)?limits.target:cost+2.;const bool cacheOnly=cost>available;
        auto placement=services.placementMap(member,posed[member],sheet,cacheOnly);if(cacheOnly&&!placement)return {};if(!cacheOnly)spend(limits,cost);
        require(placement.has_value(),PN2S_INVALID,"Escher placement-map service returned no computed polygon");
        for(auto &ring:*placement)std::reverse(ring.begin(),ring.end());translate(*placement,{sub64(0,posed[member].x),sub64(0,posed[member].y)});
        for(auto &ring:*placement)convolutionNormalizeIntegerRing(ring);convolutionRemoveEmptyIntegerRings(*placement);
        if(!placement->empty()){const auto box=bounds(*placement);if(box[0]<-0x80000000LL||box[1]<-0x80000000LL||box[2]>0x80000000LL||box[3]>0x80000000LL){if(services.diagnostic)services.diagnostic("placement map overflows 3");return {{},true};}}
        maps.push_back(escherBoundaryPeriodicMap(candidate.step,{candidate.width,0},*placement));}
    int32_t interior=0,partial=0;
    for(const auto &map:maps)for(const auto &row:map.rows)for(const auto &run:row.runs){const auto between=narrow(uint32_t(run.upper)-uint32_t(run.lower)-1U);interior=add32(interior,std::max<int32_t>(0,between));
        if(run.lower==run.upper){interior=add32(interior,run.lowerKind==-1);require(run.lowerKind==run.upperKind,PN2S_INVALID,"Escher zero-length boundary run has unequal endpoint classifications");}
        interior=add32(interior,int32_t(run.lowerKind==-1)+int32_t(run.upperKind==-1));
        if(run.lower==run.upper){if(run.lowerKind==1)partial=add32(partial,1);else if(run.upperKind==1)partial=add32(partial,1);}else partial=add32(partial,int32_t(run.lowerKind==1)+int32_t(run.upperKind==1));}
    if(add32(interior,partial)<=narrow(previousBest))return {{},true};
    struct Parent{size_t member,partial;};std::vector<Parent> regionParents,ringParents;std::vector<EscherWeightedBoundaryRegion> regions;ConvolutionIntegerPolygon rings;
    for(size_t member=0;member<maps.size();++member){const auto &map=maps[member];require(map.cells.size()==map.partials.size(),PN2S_INVALID,"Escher partial polygons and cells differ");
        for(size_t part=0;part<map.partials.size();++part){auto polygon=map.partials[part];const auto cell=map.cells[part];translate(polygon,{sub64(0,add64(mul64(cell[0],candidate.width),mul64(cell[1],candidate.step[0]))),sub64(0,mul64(cell[1],candidate.step[1]))});
            regions.push_back({polygon,1.});regionParents.push_back({member,part});for(auto &ring:polygon){rings.push_back(std::move(ring));ringParents.push_back({member,part});}}}
    int32_t vertices=0;for(const auto &ring:rings)vertices=add32(vertices,narrow(uint32_t(ring.size())));
    services.selectionSequence+=1;const auto plan=escherBoundarySelectionPlan(interior,partial,vertices,narrow(services.selectionSequence),limits);
    auto selection=escherSampleBoundaryRegions({candidate.width,0},candidate.step,trunc32(plan.attempts),regions);
    if(plan.fuzzy)selection=escherBoundaryBooleanSelect(rings,{},3,services.fuzzy,services.offset,services.diagnostic);
    spend(limits,plan.fuzzy?plan.fuzzyCost:plan.attempts*plan.sampleCost);
    if(double(narrow(previousBest))>=double(interior)+selection.weight)return {{},true};
    std::vector<std::vector<int32_t>> xs(maps.size()),ys(maps.size());for(size_t member=0;member<maps.size();++member){auto base=escherBoundaryFlattenRows(maps[member].rows);xs[member]=std::move(base[0]);ys[member]=std::move(base[1]);}
    if(!plan.fuzzy){for(const auto selected:selection.indices){require(selected>=0&&size_t(selected)<regionParents.size(),PN2S_INVALID,"Escher selected region index is out of range");const auto parent=regionParents[size_t(selected)];const auto cell=maps[parent.member].cells[parent.partial];xs[parent.member].push_back(cell[0]);ys[parent.member].push_back(cell[1]);}}
    else {std::vector<std::map<int32_t,int32_t>> counts(maps.size());std::vector<std::vector<int32_t>> keys(maps.size());
        for(const auto selected:selection.indices){require(selected>=0&&size_t(selected)<ringParents.size(),PN2S_INVALID,"Escher selected contour index is out of range");const auto parent=ringParents[size_t(selected)];const auto key=int32_t(parent.partial);keys[parent.member].push_back(key);auto &count=counts[parent.member][key];count=add32(count,convolutionIntegerOrientation(rings[size_t(selected)])==1?1:-1);}
        for(size_t member=0;member<maps.size();++member)for(const auto key:keyOrder(keys[member])){const auto count=counts[member][key];if(count<0&&services.diagnostic)services.diagnostic("we have a multi holed polygon inclusion");if(count>1&&services.diagnostic)services.diagnostic("we have a multi  inclusion");if(count>0){const auto cell=maps[member].cells[size_t(key)];xs[member].push_back(cell[0]);ys[member].push_back(cell[1]);}}}
    auto layout=escherBoundaryLayoutFromCoefficients(descriptor.records,candidate,xs,ys,selection.origin,pattern.geometries);
    if(services.diagnostic){const auto stock=bounds(sheet);for(size_t a=0;a<2;++a)if(sub64(std::max(stock[a+2],layout.bounds[a+2]),std::min(stock[a],layout.bounds[a]))>add64(sub64(stock[a+2],stock[a]),32))services.diagnostic(a==0?"out iof sheet  7":"out iof sheet  8");}
    return {std::move(layout),true};
}
EscherBoundaryPhaseProvider escherBoundaryLengthProvider(EscherBoundaryLengthServices &services){return [&services](const EscherPreparedSinglePattern &pattern,const ConvolutionIntegerPolygon &boundary,bool special,uint32_t previous,double rotation,uintptr_t,EscherProviderLengthLimits &limits,int32_t index){return escherBoundaryLengthPhase(pattern,boundary,special,previous,rotation,limits,index,services);};}
}
