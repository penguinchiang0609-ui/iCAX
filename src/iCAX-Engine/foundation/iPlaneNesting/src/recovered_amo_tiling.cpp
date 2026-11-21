#include "recovered_amo_tiling.hpp"
#include "recovered_amo_sort.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t truncate64(double value){return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
int32_t truncate32(double value){return value>=-0x1p31&&value<0x1p31?int32_t(value):INT32_MIN;}
int32_t narrow(uint64_t value){return std::bit_cast<int32_t>(uint32_t(value));}
Box translated(AmoTilingBounds bounds,Point offset) {
    if(bounds.empty)return {};
    std::array<Point,4> points{{{bounds.box.x0,bounds.box.y0},{bounds.box.x0,bounds.box.y1},{bounds.box.x1,bounds.box.y0},{bounds.box.x1,bounds.box.y1}}};
    for(auto &point:points)point={point.y*0.+point.x*1.+offset.x,point.y*1.+point.x*0.+offset.y};
    Box box{points[0].x,points[0].y,points[0].x,points[0].y};
    for(const auto point:points){box.x0=std::min(box.x0,point.x);box.y0=std::min(box.y0,point.y);box.x1=std::max(box.x1,point.x);box.y1=std::max(box.y1,point.y);}return box;
}
}
AmoTilingDescriptor amoTilingResultDescriptor(const AmoTilingCandidate &candidate,std::array<uintptr_t,2> ids) {
    AmoTilingDescriptor descriptor;descriptor.translation=candidate.translation;descriptor.angle=candidate.angle;descriptor.period=candidate.period;
    descriptor.repeatX=candidate.repeatX;descriptor.repeatY=candidate.repeatY;
    for(size_t member=0;member<2;++member)descriptor.extra[member]={int32_t(candidate.extraX==ids[member]),int32_t(candidate.extraY==ids[member])};
    descriptor.coincidentAxis=ids[0]==ids[1]&&(std::fabs(candidate.translation.x-0.)<.001||std::fabs(candidate.translation.y-0.)<.001);return descriptor;
}
AmoTilingBounds amoTilingShapeBounds(const AmoTilingShape &shape,double angle) {
    const double radians=(angle*3.141592653589793)/180.;
    const double cosine=std::cos(radians),sine=std::sin(radians),negativeSine=-sine;
    const double norm=std::sqrt(negativeSine*negativeSine+cosine*cosine);
    const double m01=norm!=0?negativeSine/norm:1.,m11=norm!=0?cosine/norm:0.;
    AmoTilingBounds bounds;
    for(const auto &ring:shape.rings)for(const auto point:ring) {
        const Point transformed{point.y*m01+point.x*cosine+0.,point.y*m11+point.x*sine+0.};
        if(bounds.empty){bounds.empty=false;bounds.box={transformed.x,transformed.y,transformed.x,transformed.y};}
        else {if(transformed.x<bounds.box.x0)bounds.box.x0=transformed.x;if(transformed.y<bounds.box.y0)bounds.box.y0=transformed.y;if(bounds.box.x1<transformed.x)bounds.box.x1=transformed.x;if(bounds.box.y1<transformed.y)bounds.box.y1=transformed.y;}
    }
    return bounds;
}
std::array<const AmoTilingShape *,2> amoTilingMemberSources(const AmoTilingJob &job) {
    return {job.memberSources[0]?job.memberSources[0]:&job.members[0],job.memberSources[1]?job.memberSources[1]:&job.members[1]};
}
std::array<AmoTilingBounds,2> amoTilingMemberBounds(AmoTilingJob &job,const AmoTilingCandidate &candidate) {
    const auto sources=amoTilingMemberSources(job);
    std::array<AmoTilingBounds,2> bounds;
    if(candidate.angle==0)bounds={sources[0]->bounds,sources[1]->bounds};
    else {
        const auto [entry,inserted]=job.angleBounds.try_emplace(candidate.angle);
        if(inserted)entry->second={amoTilingShapeBounds(*sources[0],candidate.angle),amoTilingShapeBounds(*sources[1],candidate.angle)};
        bounds=entry->second;
    }
    bounds[1].box=translated(bounds[1],candidate.translation);return bounds;
}
AmoTilingAxisOccupancy amoTilingAxisOccupancy(double low,double high,double period,double extent) {
    require(std::isfinite(low)&&std::isfinite(high)&&std::isfinite(period)&&period>0,PN2S_INVALID,"Amo tiling occupancy requires a finite positive period");
    for(;low<0.;low+=period){require(low+period!=low,PN2S_INVALID,"Amo tiling occupancy phase cannot advance");high+=period;}
    for(;period<low;low-=period){require(low-period!=low,PN2S_INVALID,"Amo tiling occupancy phase cannot decrease");high-=period;}
    if(high<extent*1.00000001){const int64_t repeats=truncate64(std::floor((extent*1.00000001-high)/period));return {narrow(uint64_t(repeats)+1),double(uint32_t(repeats))*period+high};}
    return {};
}
AmoTilingOccupancy amoTilingRectangleOccupancy(AmoTilingBounds first,AmoTilingBounds second,Point period,Point extent) {
    Point low{};bool empty=true;
    for(const auto bounds:{first,second})if(!bounds.empty){const Point minimum{std::min(bounds.box.x0,bounds.box.x1),std::min(bounds.box.y0,bounds.box.y1)};if(empty){low=minimum;empty=false;}else{low.x=std::min(low.x,minimum.x);low.y=std::min(low.y,minimum.y);}}
    const auto a=translated(first,{-low.x,-low.y}),b=translated(second,{-low.x,-low.y});
    const auto ax=amoTilingAxisOccupancy(a.x0,a.x1,period.x,extent.x),ay=amoTilingAxisOccupancy(a.y0,a.y1,period.y,extent.y),
        bx=amoTilingAxisOccupancy(b.x0,b.x1,period.x,extent.x),by=amoTilingAxisOccupancy(b.y0,b.y1,period.y,extent.y);
    AmoTilingOccupancy result;result.count=narrow(uint32_t(ax.count)*uint32_t(ay.count)+uint32_t(bx.count)*uint32_t(by.count));
    result.repetitions.commonX=std::min(ax.count,bx.count);result.repetitions.commonY=std::min(ay.count,by.count);
    result.repetitions.extraX={int32_t(ax.count>bx.count),int32_t(bx.count>ax.count)};result.repetitions.extraY={int32_t(ay.count>by.count),int32_t(by.count>ay.count)};
    result.used={std::max(ax.used,bx.used),std::max(ay.used,by.used)};return result;
}
AmoTilingCandidatePtr amoTilingFamilyMidpoint(const AmoTilingCandidatePtr &first,const AmoTilingCandidatePtr &second) {
    auto candidate=std::make_shared<AmoTilingCandidate>();
    candidate->translation={(second->translation.x+first->translation.x)*.5,(second->translation.y+first->translation.y)*.5};candidate->angle=first->angle;return candidate;
}
const AmoTilingRank &amoTilingCandidateRank(AmoTilingRankCache &cache,const AmoTilingCandidatePtr &candidate) {
    require(bool(candidate),PN2S_INVALID,"Amo rank requires an evaluated candidate");
    const auto [entry,inserted]=cache.entries.try_emplace(candidate.get());
    if(inserted) {
        const double cost=candidate->cost/cache.tolerance,secondary=candidate->secondary/cache.tolerance;
        entry->second={candidate,{cost>=0x1p63?INT64_MAX:truncate64(cost),secondary>=0x1p63?INT64_MAX:truncate64(secondary),candidate->translation.x==0||candidate->translation.y==0}};
    }
    return entry->second.second;
}
bool amoTilingCandidateBetter(AmoTilingRankCache &cache,const AmoTilingCandidatePtr &first,const AmoTilingCandidatePtr &second) {
    const auto a=amoTilingCandidateRank(cache,first),b=amoTilingCandidateRank(cache,second);
    if(a.cost!=b.cost)return a.cost<b.cost;
    if(a.secondary!=b.secondary)return a.secondary<b.secondary;
    return a.axisAlignedTranslation&&!b.axisAlignedTranslation;
}
void amoTilingFamilySeeds(const AmoTilingSeedCallbacks &callbacks,const AmoTilingCandidatePtr &candidate) {
    candidate->period.y=callbacks.periodY(candidate);candidate->period.x=callbacks.periodX(candidate);callbacks.finalize(candidate);
}
AmoTilingCandidatePtr amoTilingBisect(AmoTilingSearchState &state,const AmoTilingFamilyCallbacks &callbacks,
    const AmoTilingCandidatePtr &first,const AmoTilingCandidatePtr &second,int32_t budget) {
    ++state.calls;if(budget<1)return {};
    auto midpoint=callbacks.midpoint(first,second);callbacks.evaluate(midpoint);
    const bool beatsFirst=amoTilingCandidateBetter(state.ranks,midpoint,first),beatsSecond=amoTilingCandidateBetter(state.ranks,midpoint,second);
    auto best=midpoint;const int32_t half=(budget-1)/2;
    if(beatsFirst) {auto next=amoTilingBisect(state,callbacks,midpoint,second,half);if(next&&(!best||amoTilingCandidateBetter(state.ranks,next,best)))best=next;}
    if(beatsSecond) {auto next=amoTilingBisect(state,callbacks,first,midpoint,half);if(next&&(!best||amoTilingCandidateBetter(state.ranks,next,best)))best=next;}
    return best;
}
AmoTilingCandidatePtr amoTilingOptimizeFamily(AmoTilingSearchState &state,const AmoTilingFamilyCallbacks &callbacks,
    std::span<const AmoTilingCandidatePtr> family,size_t selected) {
    require(!family.empty()&&selected<family.size(),PN2S_INVALID,"Amo optimization requires a selected member of a nonempty family");state.calls=0;
    const auto center=family[selected];
    auto left=amoTilingBisect(state,callbacks,family[(selected+family.size()-1)%family.size()],center,100);
    auto right=amoTilingBisect(state,callbacks,center,family[(selected+1)%family.size()],100);
    auto best=left?right?(amoTilingCandidateBetter(state.ranks,left,right)?left:right):left:right;
    if(!center)return best;if(!best)return center;
    return amoTilingCandidateBetter(state.ranks,center,best)?center:best;
}
AmoTilingCandidatePtr amoTilingImproveTranslation(AmoTilingJob &job,const AmoTilingCandidatePtr &source) {
    const auto area=[&](const AmoTilingCandidatePtr &candidate) {
        const auto members=amoTilingMemberBounds(job,*candidate);AmoTilingBounds envelope;
        for(const auto &member:members)if(!member.empty)for(const auto point:{Point{member.box.x0,member.box.y0},Point{member.box.x1,member.box.y1}}) {
            if(envelope.empty){envelope.empty=false;envelope.box={point.x,point.y,point.x,point.y};}
            else {envelope.box.x0=std::min(envelope.box.x0,point.x);envelope.box.y0=std::min(envelope.box.y0,point.y);envelope.box.x1=std::max(envelope.box.x1,point.x);envelope.box.y1=std::max(envelope.box.y1,point.y);}
        }
        return std::fabs(envelope.box.y1-envelope.box.y0)*std::fabs(envelope.box.x1-envelope.box.x0);
    };
    std::array<AmoTilingCandidatePtr,4> phases;
    for(size_t i=0;i<4;++i){auto candidate=std::make_shared<AmoTilingCandidate>();candidate->angle=source->angle;candidate->period=source->period;candidate->translation={source->translation.x+(i==0?source->period.x:i==1?-source->period.x:0.),source->translation.y+(i==2?source->period.y:i==3?-source->period.y:0.)};phases[i]=candidate;}
    const auto x=area(phases[1])<area(phases[0])?phases[1]:phases[0],y=area(phases[3])<area(phases[2])?phases[3]:phases[2];
    const auto shifted=area(y)<area(x)?y:x;
    return area(shifted)<area(source)?shifted:source;
}
void amoTilingSortCandidates(std::vector<AmoTilingCandidatePtr> &items,AmoTilingRankCache &cache,size_t depth) {
    const auto before=[&](const auto &a,const auto &b){return amoTilingCandidateBetter(cache,a,b);};
    detail::AmoCandidateSort(items,before).run(depth==SIZE_MAX?items.size():depth);
}
AmoTilingFamilies amoTilingPrepareAngles(const AmoTilingAngleRequest &request,const std::array<std::vector<double>,2> &edges,const std::function<AmoTilingFamilies(double)> &prepare) {
    AmoTilingFamilies output;
    for(const auto angle:amoTilingAngles(request,edges)){auto families=prepare(angle);for(auto &family:families)output.push_back(std::move(family));}
    return output;
}
void amoTilingCriterionSearch(AmoTilingSearchState &state,AmoTilingJob &job,const AmoTilingCriterionCallbacks &callbacks,std::vector<AmoTilingCandidatePtr> &output) {
    const auto families=callbacks.prepare();bool empty=true;double low=0,high=0;
    for(const auto member:amoTilingMemberSources(job))if(!member->bounds.empty)for(const double y:{member->bounds.box.y0,member->bounds.box.y1}) {
        if(empty){low=high=y;empty=false;}else{low=std::min(low,y);high=std::max(high,y);}
    }
    const double height=std::fabs(high-low)/1000.;state.ranks.tolerance=height*height;
    for(const auto &family:families)if(!family.empty()) {
        size_t selected=0;bool found=false;
        for(size_t i=0;i<family.size();++i){callbacks.family.evaluate(family[i]);if(!found||amoTilingCandidateBetter(state.ranks,family[i],family[selected])){selected=i;found=true;}}
        if(found)output.push_back(amoTilingImproveTranslation(job,amoTilingOptimizeFamily(state,callbacks.family,family,selected)));
    }
    for(const auto &candidate:output)callbacks.finalize(candidate);
    amoTilingSortCandidates(output,state.ranks);
}
std::vector<double> amoTilingAngles(const AmoTilingAngleRequest &request,const std::array<std::vector<double>,2> &edges) {
    require(std::isfinite(request.minimum)&&std::isfinite(request.maximum)&&std::isfinite(request.step)&&request.step>=0,PN2S_INVALID,"Amo angle enumeration requires finite limits and a nonnegative step");
    std::set<double> seen;std::vector<double> output;
    const auto append=[&](double angle) {
        double phase=angle;
        for(;request.maximum+.001<phase;phase+=-360.){}
        for(;phase<request.minimum-.001;phase+=360.){}
        if(!(request.minimum-.001<phase&&phase<request.maximum+.001))return;
        double key=angle;for(;key<0.;key+=360.){}for(;360.<key;key+=-360.){}
        const auto existing=seen.lower_bound(key-.001);if(existing!=seen.end()&&*existing<=key+.001)return;
        seen.insert(key);if(key<.001)seen.insert(key+360.);
        output.push_back(phase);
    };
    append(request.minimum);append(request.maximum);
    if(request.step!=0)for(double angle=request.minimum;angle<=request.maximum;angle+=request.step) {
        require(angle+request.step!=angle,PN2S_INVALID,"Amo angle enumeration cannot advance");append(angle);
    }
    if(request.shapeEdges)for(const auto &shape:edges)for(const double angle:shape){append(-angle);append(90.-angle);}
    return output;
}
void amoTilingFamilyRanges(const AmoTilingCriterion &criterion,std::array<uintptr_t,2> ids,
    std::array<AmoTilingBounds,2> members,AmoTilingCandidate &candidate) {
    AmoTilingBounds envelope;
    for(const auto &member:members)if(!member.empty) {
        const auto &box=member.box;
        for(const Point point:{Point{box.x0,box.y0},Point{box.x1,box.y1}}) {
            if(envelope.empty){envelope.empty=false;envelope.box={point.x,point.y,point.x,point.y};}
            else {envelope.box.x0=std::min(envelope.box.x0,point.x);envelope.box.y0=std::min(envelope.box.y0,point.y);envelope.box.x1=std::max(envelope.box.x1,point.x);envelope.box.y1=std::max(envelope.box.y1,point.y);}
        }
    }
    const double area=std::fabs(envelope.box.y1-envelope.box.y0)*std::fabs(envelope.box.x1-envelope.box.x0);
    if(criterion.width==0&&criterion.height==0) {
        candidate.cost=candidate.period.y*candidate.period.x;candidate.secondary=area;
        candidate.repeatX=candidate.repeatY=0;candidate.extraX=candidate.extraY=0;return;
    }
    if(criterion.width!=0&&criterion.height!=0&&std::min(criterion.firstDemand,criterion.secondDemand)==0) {
        const auto occupancy=amoTilingRectangleOccupancy(members[0],members[1],candidate.period,{criterion.width,criterion.height});
        if(occupancy.count==0){candidate.cost=std::numeric_limits<double>::max();candidate.secondary=0;}
        else {candidate.cost=(criterion.height*criterion.width)/double(uint32_t(occupancy.count));candidate.secondary=(occupancy.used.y*occupancy.used.x)/double(uint32_t(occupancy.count));}
        candidate.repeatX=occupancy.repetitions.commonX;candidate.repeatY=occupancy.repetitions.commonY;
        candidate.extraX=occupancy.repetitions.extraX[0]?ids[0]:occupancy.repetitions.extraX[1]?ids[1]:0;
        candidate.extraY=occupancy.repetitions.extraY[0]?ids[0]:occupancy.repetitions.extraY[1]?ids[1]:0;return;
    }
    amoTilingFamilyRepetitions(criterion,ids,members,candidate);candidate.secondary=area;
}
void amoTilingFamilyRepetitions(const AmoTilingCriterion &criterion,std::array<uintptr_t,2> ids,
    std::array<AmoTilingBounds,2> members,AmoTilingCandidate &candidate) {
    require(candidate.period.x>0&&candidate.period.y>0&&(criterion.width>0||criterion.height>0),PN2S_INVALID,"Amo repetition criterion requires a positive period and at least one bounded dimension");
    bool empty=true;Box envelope{};
    for(const auto &member:members)if(!member.empty){const Box b{std::min(member.box.x0,member.box.x1),std::min(member.box.y0,member.box.y1),std::max(member.box.x0,member.box.x1),std::max(member.box.y0,member.box.y1)};if(empty){envelope=b;empty=false;}else{envelope.x0=std::min(envelope.x0,b.x0);envelope.y0=std::min(envelope.y0,b.y0);envelope.x1=std::max(envelope.x1,b.x1);envelope.y1=std::max(envelope.y1,b.y1);}}
    const double envelopeWidth=std::fabs(envelope.x1-envelope.x0),envelopeHeight=std::fabs(envelope.y1-envelope.y0),width=criterion.width,height=criterion.height;
    const int32_t demand=std::min(criterion.firstDemand,criterion.secondDemand);
    if((width>0&&width<envelopeWidth)||(height>0&&height<envelopeHeight)){candidate.cost=std::numeric_limits<double>::max();candidate.repeatX=candidate.repeatY=0;return;}
    const uint32_t nx=uint32_t(truncate32((width-envelopeWidth)/candidate.period.x))+1U,ny=uint32_t(truncate32((height-envelopeHeight)/candidate.period.y))+1U;
    const double firstWidth=std::fabs(members[0].box.x1-members[0].box.x0),firstHeight=std::fabs(members[0].box.y1-members[0].box.y0),
        secondWidth=std::fabs(members[1].box.x1-members[1].box.x0),secondHeight=std::fabs(members[1].box.y1-members[1].box.y0);
    const double firstArea=firstWidth*firstHeight,secondArea=secondWidth*secondHeight;
    uintptr_t extraX=0,extraY=0;double fractionX=0,fractionY=0;
    if(width>0){const double front=double(nx)*candidate.period.x;if(members[1].box.x1<=members[0].box.x1){if(front+secondWidth<width){extraX=ids[1];fractionX=secondArea/(secondArea+firstArea);}}else if(front+firstWidth<width){extraX=ids[0];fractionX=firstArea/(secondArea+firstArea);}}
    if(height>0){const double front=double(ny)*candidate.period.y;if(members[1].box.y0<=members[0].box.y0){if(secondHeight+front<height){extraY=ids[1];fractionY=secondArea/(secondArea+firstArea);}}else if(firstHeight+front<height){extraY=ids[0];fractionY=firstArea/(secondArea+firstArea);}}
    if(width==0) {
        if(demand==0){candidate.cost=(height*candidate.period.x)/(double(ny)+fractionY);candidate.repeatX=1;candidate.repeatY=narrow(ny);candidate.extraX=0;}
        else {
            require(ny!=0,PN2S_INVALID,"Amo repetition bounded Y count is zero");const uint32_t columns=uint32_t(demand)/ny;double residue=double(uint32_t(demand)-columns*ny),partial=0;
            if(extraY){if(residue+1e-8<=double(columns)*fractionY){fractionY=0;extraY=0;}else residue-=double(columns)*fractionY;}
            if(residue>1e-8)partial=(((double(ny)-residue)*criterion.partialWeight+residue)/double(ny))*candidate.period.x;
            candidate.cost=((double(int64_t(columns)-1)*candidate.period.x+envelopeWidth+partial)*height)/((double(ny)+fractionY)*double(columns)+residue);
            candidate.repeatX=narrow(columns);candidate.repeatY=narrow(ny);candidate.extraX=0;
        }
        candidate.extraY=extraY;return;
    }
    if(height==0) {
        if(demand==0){candidate.cost=(width*candidate.period.y)/(double(nx)+fractionX);candidate.repeatY=1;}
        else {
            require(nx!=0,PN2S_INVALID,"Amo repetition bounded X count is zero");const uint32_t rows=uint32_t(demand)/nx;double residue=double(uint32_t(demand)-rows*nx),partial=0;
            if(extraX){if(residue+1e-8<=double(rows)*fractionX){fractionX=0;extraX=0;}else residue-=double(rows)*fractionX;}
            if(residue>1e-8)partial=(((double(nx)-residue)*criterion.partialWeight+residue)/double(nx))*candidate.period.y;
            candidate.cost=((double(int64_t(rows)-1)*candidate.period.y+envelopeHeight+partial)*width)/((double(nx)+fractionX)*double(rows)+residue);candidate.repeatY=narrow(rows);
        }
        candidate.repeatX=narrow(nx);candidate.extraX=extraX;candidate.extraY=0;return;
    }
    if(demand!=0) {
        const uint32_t extraCapacity=std::max(extraY?nx:0U,extraX?ny:0U);
        if(uint32_t(demand)<=extraCapacity+ny*nx) {
            require(nx!=0&&ny!=0,PN2S_INVALID,"Amo repetition bounded candidate has no cell");
            const uint32_t lowerX=uint32_t(uint32_t(demand)<ny)+uint32_t(demand)/ny,lowerY=uint32_t(uint32_t(demand)<nx)+uint32_t(demand)/nx;
            double best=std::numeric_limits<double>::max();uint32_t bestX=0,bestY=0;
            for(uint32_t x=lowerX;x<=nx;++x) {
                require(x!=0,PN2S_INVALID,"Amo repetition count divisor is zero");const uint32_t y=std::min(uint32_t(demand)/x,ny);if(y<lowerY)break;
                double filled=double(y*x),residue=double(demand)-filled,extraColumns=0,extraRows=0,partialX=0,partialY=0;
                if(residue>1e-8) {
                    const double capacityX=double(y)*fractionX;if(x==nx&&capacityX<residue){filled+=capacityX;residue-=capacityX;extraColumns=fractionX;}
                    const double capacityY=double(x)*fractionY;if(y==ny&&capacityY<residue){filled+=capacityY;residue-=capacityY;extraRows=fractionY;}
                    if(residue>1e-8) {
                        bool append=false;
                        if(x>=nx||double(y)<=residue){if(y<ny&&double(x)>residue){partialY=(((double(x)-residue)*criterion.partialWeight+residue)/double(x))*candidate.period.y;append=true;}}
                        else {partialX=(((double(y)-residue)*criterion.partialWeight+residue)/double(y))*candidate.period.x;append=true;}
                        if(append){filled+=residue;residue=0;}
                    }
                }
                double cost=((((double(y)+extraRows)-1.)*candidate.period.y+envelopeHeight+partialY)*(((double(x)+extraColumns)-1.)*candidate.period.x+envelopeWidth+partialX))/filled;
                if(residue>1e-8)cost+=((envelopeWidth-candidate.period.x)*(envelopeHeight-candidate.period.y))/double(demand);
                if(cost<best){best=cost;bestX=x;bestY=y;}
                if(x==UINT32_MAX)break;
            }
            candidate.cost=best;candidate.repeatX=narrow(bestX);candidate.repeatY=narrow(bestY);candidate.extraX=lowerX==nx?extraX:0;candidate.extraY=lowerY==ny?extraY:0;return;
        }
    }
    candidate.cost=((((((double(nx)+fractionX)-1.)*candidate.period.x+envelopeWidth)*(((double(ny)+fractionY)-1.)*candidate.period.y+envelopeHeight)+height*width)*.5)/(double(nx)+fractionX))/(double(ny)+fractionY);
    candidate.repeatX=narrow(nx);candidate.repeatY=narrow(ny);candidate.extraX=extraX;candidate.extraY=extraY;
}
}
