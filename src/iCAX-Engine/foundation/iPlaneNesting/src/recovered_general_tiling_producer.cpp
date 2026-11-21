#include "recovered_general_tiling_producer.hpp"
#include <bit>
namespace study::recovered {
namespace {
int32_t integer(double value){return value>=-0x1p31&&value<0x1p31?int32_t(value):INT32_MIN;}
TilingCandidate repetition(std::span<const AmoTilingPatternResult> items){
    std::vector<TilingPattern> patterns;std::vector<TilingRepetitionCounts> counts;
    for(const auto &item:items){patterns.push_back(*item.pattern);TilingRepetitionCounts count;count.commonX=item.descriptor.repeatX;count.commonY=item.descriptor.repeatY;
        for(size_t member=0;member<2;++member){count.extraX[member]=item.descriptor.extra[member][0];count.extraY[member]=item.descriptor.extra[member][1];}counts.push_back(count);}
    return selectGeneralTilingRepetition(patterns,counts);
}
void appendExpanded(GeneralTilingControlState &state,TilingCandidate candidate){
    if(expandTilingCandidate(candidate).members.size()>=2)state.expanded.push_back(std::move(candidate));
}
Box layoutBounds(std::span<const TilingMember> members){
    Box box{};bool empty=true;
    for(const auto &member:members){const auto b=member.box;const Box normalized{std::min(b.x0,b.x1),std::min(b.y0,b.y1),std::max(b.x0,b.x1),std::max(b.y0,b.y1)};
        if(empty){box=normalized;empty=false;}else {if(normalized.x0<box.x0)box.x0=normalized.x0;if(normalized.y0<box.y0)box.y0=normalized.y0;if(box.x1<normalized.x1)box.x1=normalized.x1;if(box.y1<normalized.y1)box.y1=normalized.y1;}}
    return box;
}
}
TilingPattern generalTilingLayoutPattern(std::vector<TilingMember> members,double gap){
    TilingPattern result;result.members=std::move(members);result.style=1;
    double highX=-INFINITY,highY=-INFINITY,lowX=INFINITY,lowY=INFINITY,maximumGap=-INFINITY;
    for(const auto &member:result.members){for(const Point p:{Point{member.box.x1,member.box.y1},Point{member.box.x0,member.box.y0}}){highX=std::max(highX,p.x);highY=std::max(highY,p.y);lowX=std::min(lowX,p.x);lowY=std::min(lowY,p.y);}maximumGap=std::max(maximumGap,gap);}
    result.u={(highX-lowX)+maximumGap,0.};result.v={0.,(highY-lowY)+maximumGap};
    for(auto &matrix:result.commonCuts)matrix.assign(result.members.size(),std::vector<double>(result.members.size(),0.));
    return result;
}
void generalTilingProduce(GeneralTilingControlState &state,const GeneralTilingControlInput &input,GeneralTilingControlHost &host){
    if(!host.zeroVariantAvailable()||host.stopped())return;
    bool opposite=false,reflected=false;
    for(const auto &orientation:input.orientations){
        const std::array one{orientation};
        opposite=opposite||equivalentOrientation(one,{18000000,0,0},input.symmetryX,input.symmetryY).found;
        reflected=reflected||equivalentOrientation(one,{-36000000,0,0},input.symmetryX,input.symmetryY).found;
    }
    const std::array zero{PlacementOrientation{}};
    const bool oppositeIsZero=equivalentOrientation(zero,{18000000,0,0},input.symmetryX,input.symmetryY).found;
    const auto usable=input.sheet.usable();
    const auto cacheFirst=[&](const std::vector<AmoTilingPatternResult> &items){
        if(!items.empty())state.models.push_back(items.front().pattern);
        if(host.hasSupplementalProvider()&&!items.empty()){host.hasSupplementalProvider();host.addSupplementalPattern(items.front().pattern);}
    };
    if(!oppositeIsZero&&(opposite||reflected)){
        const bool useReflection=!opposite||(reflected&&input.preferReflection228);
        auto items=host.orientationPatterns({},useReflection?PlacementOrientation{-36000000,0,0}:PlacementOrientation{18000000,0,0});
        cacheFirst(items);auto candidate=repetition(items);
        if(tilingCandidateValid(candidate)){
            const auto box=tilingCandidateBounds(candidate);
            const bool tooLarge=usable.x<(box.x1-box.x0)*1.00001||usable.y<(box.y1-box.y0)*1.00001;
            const bool hasProvider=host.hasSupplementalProvider();
            if(input.expand&&(!hasProvider||tooLarge))appendExpanded(state,std::move(candidate));
        }
    }
    auto items=host.orientationPatterns({},{});cacheFirst(items);auto candidate=repetition(items);
    if(!input.expand)return;
    std::vector<TilingMember> layout;
    if(host.hasSupplementalProvider()){
        const int32_t templates=integer(input.templateLimit678);
        if(templates>0){for(auto &value:host.supplementalTemplates(templates,usable))state.models.push_back(std::move(value));std::reverse(state.models.begin(),state.models.end());}
        EscherProviderRequest request;request.width=usable.x;request.height=usable.y;request.useCountCache=true;
        layout=host.supplementalLayout(request);
        const int32_t trials=integer(input.layoutTrials688);
        if(trials>0){state.models.push_back(std::make_shared<TilingPattern>(generalTilingLayoutPattern(layout,input.gap)));
            Random random(uint32_t(integer(input.seed178))+1993U);
            for(int32_t n=0;n<trials-1;++n){
                EscherProviderRequest sampled;sampled.width=(random.unit()+0.)*usable.x;sampled.height=(random.unit()+0.)*usable.y;
                auto produced=host.supplementalLayout(sampled);
                if(!produced.empty())state.models.push_back(std::make_shared<TilingPattern>(generalTilingLayoutPattern(std::move(produced),input.gap)));
            }
        }
    }
    const double value=input.optional?0.:input.value;double criterionDensity=0.;
    if(tilingCandidateValid(candidate)){
        const auto box=tilingCandidateBounds(candidate);criterionDensity=(double(tilingCandidateSize(candidate))*value)/((box.y1-box.y0)*(box.x1-box.x0));
    }
    const auto box=layoutBounds(layout);const double layoutDensity=(double(layout.size())*value)/((box.y1-box.y0)*(box.x1-box.x0));
    if(layoutDensity<criterionDensity)appendExpanded(state,std::move(candidate));
}
}
