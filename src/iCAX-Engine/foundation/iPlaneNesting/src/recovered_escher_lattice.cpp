#include "recovered_escher_lattice.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int32_t add32(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int32_t trunc32(double value){return !std::isfinite(value)||value>=0x1p31||value< -0x1p31?INT32_MIN:int32_t(value);}
int64_t quotient(int64_t a,int64_t b) {
    require(b!=0 && !(a==INT64_MIN&&b==-1),PN2S_INVALID,"Invalid Escher lattice division");return a/b;
}
double norm(EscherIntegerPoint p){const double x=double(p[0]),y=double(p[1]);return std::sqrt(y*y+x*x);}
int64_t cross(int64_t ax,int64_t ay,int64_t bx,int64_t by){return sub(mul(ax,by),mul(bx,ay));}
}
int32_t escherTolerantCeil(double value,double tolerance) {
    if(value<0)return std::bit_cast<int32_t>(0U-uint32_t(escherTolerantFloor(-value,tolerance)));
    const int32_t base=trunc32(value);return value-double(base)>tolerance?add32(base,1):base;
}
int32_t escherTolerantFloor(double value,double tolerance) {
    if(value<0)return std::bit_cast<int32_t>(0U-uint32_t(escherTolerantCeil(-value,tolerance)));
    const int32_t base=trunc32(value),next=add32(base,1);return double(next)-value>tolerance?base:next;
}
std::vector<EscherIntegerCoefficient> escherTriangleScanRow(std::array<double,2> from,std::array<double,2> to,
    int32_t row,double tolerance) {
    const double delta=to[0]-from[0],offset=double(row)-from[0];
    double lower=((to[1]-from[1])*offset)/delta+from[1];
    double upper=((offset-1.)*(to[1]-from[1]))/delta+from[1]+1.;
    if(delta<tolerance) {
        const double high=std::max(from[1],to[1]),low=std::min(from[1],to[1]);
        if(high<upper)upper=high;if(lower<low)lower=low;
    }
    std::vector<EscherIntegerCoefficient> output;
    int32_t column=escherTolerantCeil(lower,tolerance),difference=std::bit_cast<int32_t>(uint32_t(column)-uint32_t(row));
    const int32_t last=escherTolerantFloor(upper,tolerance);
    for(;column<=last;column=add32(column,1),difference=add32(difference,1)) {
        if(double(difference)<(from[1]-from[0])+tolerance && (to[1]-to[0])-tolerance<double(difference))
            output.push_back({row,column});
        require(column!=INT32_MAX,PN2S_INVALID,"Escher triangle coefficient range overflows");
    }
    return output;
}
std::vector<EscherIntegerCoefficient> escherCornerIntegerCoefficients(std::array<double,2> from,
    std::array<double,2> to,std::array<double,2> origin) {
    const double a=from[0]-origin[0],b=from[1]-origin[1],c=to[0]-origin[0],d=to[1]-origin[1];
    require(b<=d&&c<=a,PN2S_INVALID,"Escher corner coefficient directions are not ordered");
    std::vector<EscherIntegerCoefficient> output;
    const int32_t first=escherTolerantCeil(b),last=escherTolerantFloor(d+1.);
    for(int32_t row=first;row<=last;row=add32(row,1)) {
        if(b==d||a==c) {
            const int32_t left=escherTolerantCeil(c),right=escherTolerantFloor(a+1.);
            for(int32_t column=left;column<=right;column=add32(column,1)) {
                output.push_back({column,row});require(column!=INT32_MAX,PN2S_INVALID,"Escher corner coefficient range overflows");
            }
        } else {
            if(double(row)-b<1.001) {
                output.push_back({escherTolerantCeil(a),row});output.push_back({escherTolerantFloor(a+1.),row});
            }
            if(d<double(row)+.001&&double(row)<(d+1.)+.001) {
                output.push_back({escherTolerantCeil(c),row});output.push_back({escherTolerantFloor(c+1.),row});
            }
            const auto scanned=escherTriangleScanRow({b,a},{d,c},row);
            for(const auto &coefficient:scanned)output.push_back({coefficient[1],coefficient[0]});
        }
        require(row!=INT32_MAX,PN2S_INVALID,"Escher corner coefficient range overflows");
    }
    return output;
}
std::vector<EscherAnchorEvent> escherCornerEvents(EscherIntegerPoint u,EscherIntegerPoint v,
    EscherIntegerPoint origin,EscherIntegerPoint sheet) {
    const int64_t determinant=cross(u[0],u[1],v[0],v[1]);
    require(determinant!=0,PN2S_INVALID,"Escher corner lattice is singular");const double det=double(determinant);
    const std::array<double,2> x{(double(v[1])*double(sheet[0])-0.)/det,(0.-double(u[1])*double(sheet[0]))/det};
    const std::array<double,2> y{(0.-double(v[0])*double(sheet[1]))/det,(double(u[0])*double(sheet[1])+0.)/det};
    const std::array<double,2> base{double(cross(origin[0],origin[1],v[0],v[1]))/det,
        double(cross(u[0],u[1],origin[0],origin[1]))/det};
    auto coefficients=escherCornerIntegerCoefficients({0,0},x,base);
    const auto second=escherCornerIntegerCoefficients(y,{0,0},base);coefficients.insert(coefficients.end(),second.begin(),second.end());
    std::sort(coefficients.begin(),coefficients.end());coefficients.erase(std::unique(coefficients.begin(),coefficients.end()),coefficients.end());
    std::vector<EscherAnchorEvent> events;events.reserve(coefficients.size());
    for(const auto &coefficient:coefficients)events.push_back({
        add(add(mul(coefficient[1],v[0]),mul(coefficient[0],u[0])),origin[0]),
        add(add(mul(coefficient[1],v[1]),mul(coefficient[0],u[1])),origin[1]),coefficient[0],coefficient[1]});
    return escherFilterCornerEvents(u,v,sheet,events);
}
void escherBoundaryEvents(std::vector<EscherAnchorEvent> &positive,std::vector<EscherAnchorEvent> &negative,
    EscherIntegerBox box,EscherIntegerPoint sheet,EscherIntegerPoint u,EscherIntegerPoint v) {
    struct Boundary {EscherAnchorEvent event;bool upper;EscherIntegerPoint offset;};
    std::vector<Boundary> combined;combined.reserve(positive.size()+negative.size());
    for(const auto &event:negative)combined.push_back({event,false,{sub(event.x,box[0]),sub(event.y,box[1])}});
    for(const auto &event:positive)combined.push_back({event,true,{sub(event.x,box[2]),sub(event.y,box[3])}});
    std::stable_sort(combined.begin(),combined.end(),[](const auto &a,const auto &b){
        return std::tie(a.offset,a.upper,a.event.x,a.event.y,a.event.a,a.event.b)<
            std::tie(b.offset,b.upper,b.event.x,b.event.y,b.event.a,b.event.b);
    });
    positive.clear();negative.clear();
    const auto appendRectangle=[&](EscherIntegerPoint first,EscherIntegerPoint last,const EscherAnchorEvent &event) {
        positive.push_back({first[0],first[1],event.a,event.b});positive.push_back({last[0],last[1],event.a,event.b});
        negative.push_back({first[0],last[1],event.a,event.b});negative.push_back({last[0],first[1],event.a,event.b});
    };
    for(size_t index=0;index<combined.size();) {
        const auto &item=combined[index];const auto &event=item.event;
        if(index+1<combined.size()&&item.offset==combined[index+1].offset) {
            const auto &other=combined[index+1];
            require(!item.upper&&other.upper&&event.a==other.event.a&&event.b==other.event.b,PN2S_INVALID,
                "Escher opposite boundary events do not describe one lattice instance");
            const EscherIntegerPoint first{sub(other.event.x,sheet[0]),sub(other.event.y,sheet[1])};
            if(first[0]<=event.x&&first[1]<=event.y)appendRectangle(first,{add(event.x,1),add(event.y,1)},event);
            index+=2;
        } else {
            if(!item.upper) {
                if(add(item.offset[0],box[2])<=sheet[0]&&add(item.offset[1],box[3])<=sheet[1]) {
                    const int64_t x=add(event.x,1),y=add(event.y,1);
                    positive.push_back({x,y,event.a,event.b});negative.push_back({-1,y,event.a,event.b});negative.push_back({x,-1,event.a,event.b});
                }
            } else if(add(u[0],v[0])<=add(item.offset[0],box[0])&&add(u[1],v[1])<=add(item.offset[1],box[1]))
                positive.push_back({sub(event.x,sheet[0]),sub(event.y,sheet[1]),event.a,event.b});
            ++index;
        }
    }
}
EscherIntegerPoint escherLatticeAnchor(std::span<const EscherIntegerBox> boxes,EscherIntegerPoint u,
    EscherIntegerPoint v,EscherIntegerPoint sheet) {
    std::vector<EscherScanEvent> scans;
    for(const auto &box:boxes) {
        auto negative=escherCornerEvents(u,v,{box[0],box[1]},sheet);
        const EscherIntegerPoint far{add(add(sub(u[0],box[2]),v[0]),sheet[0]),
            add(add(sub(v[1],box[3]),u[1]),sheet[1])};
        const auto opposite=escherCornerEvents(u,v,far,sheet);
        std::vector<EscherAnchorEvent> positive;positive.reserve(opposite.size());
        for(const auto &event:opposite)positive.push_back({
            add(add(sub(u[0],event.x),v[0]),sheet[0]),add(add(sub(v[1],event.y),u[1]),sheet[1]),
            std::bit_cast<int32_t>(0U-uint32_t(event.a)),std::bit_cast<int32_t>(0U-uint32_t(event.b))});
        escherBoundaryEvents(positive,negative,box,sheet,u,v);
        for(const auto &[events,weight]:std::array<std::pair<const std::vector<EscherAnchorEvent>*,int32_t>,2>{{{&positive,1},{&negative,-1}}})
            for(const auto &event:*events)scans.push_back(escherClipAnchorScanEvent({event.x,event.y,0,0,weight,event.a,event.b},u,v));
    }
    return escherMaximumAnchor(std::move(scans)).point;
}
std::vector<std::vector<EscherIntegerPoint>> escherFitLatticePoints(EscherLatticeBasis basis,
    EscherIntegerPoint sheet,std::span<const EscherIntegerBox> boxes) {
    basis=escherReduceLatticeBasis(basis);const auto &u=basis[0];const auto &v=basis[1];
    const int64_t uv=mul(v[1],u[0]),vu=mul(u[1],v[0]);
    const double firstDistance=std::fabs(double(sub(uv,vu))/norm(u)),secondDistance=std::fabs(double(sub(vu,uv))/norm(v));
    const double separation=secondDistance<=firstDistance?secondDistance:firstDistance;
    require(std::isfinite(separation)&&separation>0,PN2S_INVALID,"Escher fitted lattice is singular");
    std::vector<EscherIntegerPoint> firstQuadrant,secondQuadrant;
    for(const auto &a:std::array<EscherIntegerPoint,3>{u,EscherIntegerPoint{0,0},EscherIntegerPoint{sub(0,u[0]),sub(0,u[1])}})
        for(const auto &b:std::array<EscherIntegerPoint,3>{v,EscherIntegerPoint{0,0},EscherIntegerPoint{sub(0,v[0]),sub(0,v[1])}}) {
            const EscherIntegerPoint point{add(a[0],b[0]),add(a[1],b[1])};if(point==EscherIntegerPoint{0,0})continue;
            if(point[0]>=0&&point[1]>=0)firstQuadrant.push_back(point);
            if(point[0]<=0&&point[1]>=0)secondQuadrant.push_back(point);
        }
    const bool reflected=firstQuadrant.size()<2;auto &selected=reflected?secondQuadrant:firstQuadrant;
    require(selected.size()>=2,PN2S_INVALID,"Escher fitted lattice has no ordered orthant basis");
    auto a=selected[0],b=selected[1];if(reflected){a[0]=sub(0,a[0]);b[0]=sub(0,b[0]);}
    if(cross(a[0],a[1],b[0],b[1])>0)std::swap(a,b);
    require(cross(a[0],a[1],b[0],b[1])<0,PN2S_INVALID,"Escher anchor basis has invalid orientation");
    EscherIntegerPoint anchor{};
    if(reflected) {
        std::vector<EscherIntegerBox> mirrored;mirrored.reserve(boxes.size());
        for(const auto &box:boxes)mirrored.push_back({sub(0,box[2]),box[1],sub(0,box[0]),box[3]});
        anchor=escherLatticeAnchor(mirrored,a,b,sheet);anchor[0]=sub(0,add(anchor[0],sheet[0]));
    } else anchor=escherLatticeAnchor(boxes,a,b,sheet);
    std::vector<std::vector<EscherIntegerPoint>> output;
    for(const auto &box:boxes) {
        const EscherIntegerPoint offset{sub(box[0],anchor[0]),sub(box[1],anchor[1])};
        const int64_t width=add(sub(box[0],box[2]),sheet[0]),height=add(sub(sheet[1],box[3]),box[1]);
        if(width<0||height<0)continue;
        const int32_t radius=trunc32((norm({width,height})+norm(offset))/separation+1.);
        std::vector<EscherIntegerPoint> points;
        const int32_t negative=std::bit_cast<int32_t>(0U-uint32_t(radius));
        for(int64_t ai=negative;ai<=radius;++ai)for(int64_t bi=negative;bi<=radius;++bi) {
            const int64_t x=add(add(mul(bi,v[0]),mul(ai,u[0])),offset[0]),y=add(add(mul(bi,v[1]),mul(ai,u[1])),offset[1]);
            if(x>=0&&x<=width&&y>=0&&y<=height)points.push_back({sub(x,box[0]),sub(y,box[1])});
        }
        output.push_back(std::move(points));
    }
    return output;
}
EscherPatternLayout escherLatticeLayout(std::span<const EscherPatternRecord> records,
    std::span<const EscherLatticeBasis> candidates,EscherIntegerBox rectangle,int32_t candidateLimit) {
    EscherPatternLayout result;if(candidateLimit==0)return result;
    std::vector<EscherIntegerBox> boxes;boxes.reserve(records.size());
    for(const auto &record:records)boxes.push_back({add(record.bounds[0],record.x),add(record.bounds[1],record.y),
        add(record.bounds[2],record.x),add(record.bounds[3],record.y)});
    const EscherIntegerPoint sheet{sub(rectangle[2],rectangle[0]),sub(rectangle[3],rectangle[1])};
    const size_t limit=candidateLimit<0?candidates.size():std::min(candidates.size(),size_t(candidateLimit));
    std::vector<std::vector<EscherIntegerPoint>> best;size_t bestIndex=0;int32_t bestCount=0;
    for(size_t index=0;index<limit;++index) {
        auto points=escherFitLatticePoints(candidates[index],sheet,boxes);int32_t count=0;
        for(const auto &row:points)count=add32(count,std::bit_cast<int32_t>(uint32_t(row.size())));
        if(bestCount<count){bestCount=count;bestIndex=index;best=std::move(points);}
    }
    if(best.empty())return result;
    for(size_t index=0;index<best.size();++index)for(const auto &point:best[index]) {
        auto record=records[index];record.x=add(add(record.x,rectangle[0]),point[0]);
        record.y=add(add(record.y,rectangle[1]),point[1]);result.primary.push_back(record);
    }
    if(result.primary.empty())return {};
    result.basis={candidates[bestIndex][0][0],candidates[bestIndex][0][1],candidates[bestIndex][1][0],candidates[bestIndex][1][1]};
    result.secondary.assign(records.begin(),records.end());
    for(auto &record:result.secondary){record.angle%=3600000;if(record.angle<0)record.angle+=3600000;}
    result.bounds=escherLayoutBounds(result.primary);return result;
}
EscherLatticeBasis escherReduceLatticeBasis(EscherLatticeBasis basis) {
    for(;;) {
        auto &u=basis[0];auto &v=basis[1];const double un=norm(u),vn=norm(v);
        const EscherIntegerPoint sum{add(u[0],v[0]),add(u[1],v[1])};const double sn=norm(sum);
        if(un>sn){u=sum;continue;}
        if(vn>sn){v=sum;continue;}
        const EscherIntegerPoint diff{sub(u[0],v[0]),sub(u[1],v[1])};const double dn=norm(diff);
        if(un>dn){u=diff;continue;}
        if(vn>dn){v=diff;continue;}
        return basis;
    }
}
EscherScanEvent escherClipAnchorScanEvent(EscherScanEvent event,EscherIntegerPoint u,EscherIntegerPoint v) {
    const int64_t sx=add(u[0],v[0]),sy=add(u[1],v[1]);
    if(sx<event.x||sy<event.y){event.weight=0;return event;}
    if(event.x<=0&&event.y<=0){event.x=0;event.y=0;return event;}
    if(cross(sx,sy,event.x,event.y)>0) {
        if(event.y<u[1]) {
            if(cross(u[0],u[1],event.x,event.y)>0)event.x=quotient(mul(u[0],event.y),u[1]);
        } else if(cross(v[0],sub(sy,u[1]),sub(event.x,u[0]),sub(event.y,u[1]))>0)
            event.x=add(quotient(mul(sub(event.y,u[1]),v[0]),v[1]),u[0]);
    } else {
        if(event.x<v[0]) {
            if(cross(sub(event.x,v[0]),sub(event.y,v[1]),v[0],v[1])>0)
                event.y=quotient(mul(v[1],event.x),v[0]);
        } else if(cross(sub(v[0],sx),sub(v[1],sy),sub(event.x,sx),sub(event.y,sy))>0)
            event.y=add(quotient(mul(sub(event.x,v[0]),u[1]),u[0]),v[1]);
    }
    return event;
}
EscherAnchorMaximum escherMaximumAnchor(std::vector<EscherScanEvent> events) {
    EscherAnchorMaximum result;if(events.empty())return result;
    std::stable_sort(events.begin(),events.end(),[](const auto &a,const auto &b){return std::tie(a.x,a.y)<std::tie(b.x,b.y);});
    size_t count=0;
    for(const auto &event:events) {
        if(count&&events[count-1].x==event.x&&events[count-1].y==event.y)
            events[count-1].weight=add32(events[count-1].weight,event.weight);
        else events[count++]=event;
    }
    events.resize(count);std::erase_if(events,[](const auto &event){return event.weight==0;});
    if(events.empty())return result;
    std::vector<int64_t> xs;for(auto &event:events) {
        if(xs.empty()||xs.back()!=event.x)xs.push_back(event.x);
        event.xIndex=int32_t(xs.size()-1);
    }
    std::stable_sort(events.begin(),events.end(),[](const auto &a,const auto &b){return a.y<b.y;});
    size_t leaves=1;while(leaves<events.size())leaves*=2;
    std::vector<int32_t> sums(leaves*2),maxima(leaves*2);
    for(size_t first=0;first<events.size();) {
        size_t end=first;
        while(end<events.size()&&events[end].y==events[first].y) {
            const auto &event=events[end++];size_t node=leaves-1+size_t(event.xIndex);
            sums[node]=add32(sums[node],event.weight);maxima[node]=std::max(0,sums[node]);
            while(node) {
                node=(node-1)/2;const size_t left=2*node+1,right=left+1;
                sums[node]=add32(sums[left],sums[right]);maxima[node]=std::max(maxima[left],add32(sums[left],maxima[right]));
            }
        }
        if(result.count<maxima[0]) {
            size_t node=0;while(node<leaves-1) {
                const size_t left=2*node+1;node=maxima[left]==maxima[node]?left:left+1;
            }
            const size_t xIndex=node-(leaves-1);
            require(xIndex<xs.size(),PN2S_INTERNAL,"Escher maximum anchor has no X coordinate");
            result.point={xs[xIndex],events[first].y};result.count=maxima[0];
        }
        first=end;
    }
    return result;
}
std::vector<EscherAnchorEvent> escherFilterCornerEvents(EscherIntegerPoint u,EscherIntegerPoint v,
    EscherIntegerPoint sheet,std::span<const EscherAnchorEvent> events) {
    std::vector<EscherAnchorEvent> output;const int64_t sx=add(u[0],v[0]),sy=add(u[1],v[1]);
    for(const auto &event:events) {
        if(event.x<0||event.y<0||(event.x>sx&&event.y>sy))continue;
        if(cross(u[0],u[1],event.x,sub(event.y,sheet[1]))>0)continue;
        if(cross(v[0],sub(sy,u[1]),sub(event.x,u[0]),sub(event.y,add(sheet[1],u[1])))>0)continue;
        const int64_t vx=add(v[0],sheet[0]);
        if(add(mul(sub(sheet[0],vx),sub(event.y,v[1])),mul(sub(event.x,vx),v[1]))>0)continue;
        const int64_t rx=add(add(sheet[0],u[0]),v[0]);
        if(cross(sub(vx,rx),sub(v[1],sy),sub(event.x,rx),sub(event.y,sy))>0)continue;
        output.push_back(event);
    }
    return output;
}
}
