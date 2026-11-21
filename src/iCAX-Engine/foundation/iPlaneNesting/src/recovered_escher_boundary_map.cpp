#include "recovered_escher_boundary_map.hpp"
#include <bit>
#include <list>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int32_t increment(int32_t value){return std::bit_cast<int32_t>(uint32_t(value)+1U);}
EscherIntegerPoint endpoint(const EscherBoundaryBorderEndpoint &value){return value.edge[value.endpoint!=0?1:0];}
}
bool escherBoundaryBorderEndpointLess(const EscherBoundaryBorderEndpoint &a,const EscherBoundaryBorderEndpoint &b,
    int32_t axis,int32_t side){
    const auto first=endpoint(a),second=endpoint(b);const int64_t sign=axis==side?1:-1;
    const auto x=mul(first[size_t(1-axis)],sign),y=mul(second[size_t(1-axis)],sign);
    if(x!=y)return x<y;
    const auto transform=[&](EscherIntegerPoint point){if(axis!=0)point={sub(0,point[1]),point[0]};
        if(side!=axis)point={sub(0,point[0]),sub(0,point[1])};return point;};
    const auto reference=transform(first),oppositeA=transform(a.edge[a.endpoint!=0?0:1]),oppositeB=transform(b.edge[b.endpoint!=0?0:1]);
    const EscherIntegerPoint da{sub(oppositeA[0],reference[0]),sub(oppositeA[1],reference[1])},db{sub(oppositeB[0],reference[0]),sub(oppositeB[1],reference[1])};
    if(da[0]==0)return da[1]<0;if(db[0]==0)return db[1]>0;
    return mul(da[0],db[1])<mul(db[0],da[1]);
}
void escherBoundarySortBorderEndpoints(std::vector<EscherBoundaryBorderEndpoint> &values,int32_t axis,int32_t side){
    const auto less=[&](const auto &a,const auto &b){return escherBoundaryBorderEndpointLess(a,b,axis,side);};
    const auto median=[&](size_t a,size_t b,size_t c){if(less(values[b],values[a]))std::swap(values[b],values[a]);
        if(less(values[c],values[b])){std::swap(values[c],values[b]);if(less(values[b],values[a]))std::swap(values[b],values[a]);}};
    const auto partition=[&](size_t first,size_t last){const auto mid=first+(last-first)/2,back=last-1;
        if(back-first>40){const auto step=(last-first)/8;median(first,first+step,first+step*2);median(mid-step,mid,mid+step);median(back-step*2,back-step,back);median(first+step,mid,back-step);}else median(first,mid,back);
        auto lower=mid,upper=mid+1;
        while(first<lower&&!less(values[lower-1],values[lower])&&!less(values[lower],values[lower-1]))--lower;
        while(upper<last&&!less(values[upper],values[lower])&&!less(values[lower],values[upper]))++upper;
        auto next=upper,previous=lower;
        for(;;){for(;next<last;++next){if(less(values[lower],values[next]))continue;if(less(values[next],values[lower]))break;
                if(upper!=next)std::swap(values[upper],values[next]);++upper;}
            for(;first<previous;--previous){if(less(values[previous-1],values[lower]))continue;if(less(values[lower],values[previous-1]))break;
                if(--lower!=previous-1)std::swap(values[lower],values[previous-1]);}
            if(previous==first&&next==last)return std::array<size_t,2>{lower,upper};
            if(previous==first){if(upper!=next)std::swap(values[lower],values[upper]);++upper;std::swap(values[lower++],values[next++]);}
            else if(next==last){if(--previous!=--lower)std::swap(values[previous],values[lower]);std::swap(values[lower],values[--upper]);}
            else std::swap(values[next++],values[--previous]);}
    };
    const auto sift=[&](size_t first,size_t hole,size_t count,EscherBoundaryBorderEndpoint value){const auto top=hole,bottom=(count-1)/2;
        while(hole<bottom){auto child=hole*2+2;if(less(values[first+child],values[first+child-1]))--child;values[first+hole]=values[first+child];hole=child;}
        if(hole==bottom&&count%2==0){values[first+hole]=values[first+count-1];hole=count-1;}
        while(top<hole){const auto parent=(hole-1)/2;if(!less(values[first+parent],value))break;values[first+hole]=values[first+parent];hole=parent;}values[first+hole]=value;};
    const auto sort=[&](auto &&self,size_t first,size_t last,size_t ideal)->void{for(;;){
        if(last-first<=32){for(auto at=first+size_t(first!=last);at<last;++at){auto value=values[at];auto target=at;
            while(target>first&&less(value,values[target-1])){values[target]=values[target-1];--target;}values[target]=value;}return;}
        if(ideal==0){const auto count=last-first;for(auto at=count/2;at>0;--at)sift(first,at-1,count,values[first+at-1]);
            for(auto remaining=count;remaining>1;--remaining){const auto value=values[first+remaining-1];values[first+remaining-1]=values[first];sift(first,0,remaining-1,value);}return;}
        const auto middle=partition(first,last);ideal=(ideal>>1)+(ideal>>2);
        if(middle[0]-first<last-middle[1]){self(self,first,middle[0],ideal);first=middle[1];}else{self(self,middle[1],last,ideal);last=middle[0];}}};
    sort(sort,0,values.size(),values.size());
}
std::vector<ConvolutionIntegerSegment> escherBoundaryCloseInteriorTile(std::span<const ConvolutionIntegerSegment> edges,
    int64_t width,int64_t height,std::array<int32_t,2> cell){
    require(!edges.empty(),PN2S_INVALID,"Escher interior tile closure requires source edges");
    std::vector<ConvolutionIntegerSegment> result(edges.begin(),edges.end());
    const auto left=mul(cell[0],width),bottom=mul(cell[1],height),right=mul(increment(cell[0]),width),top=mul(increment(cell[1]),height);
    EscherIntegerPoint extreme{left,bottom};for(const auto &edge:edges)for(const auto &point:edge)extreme=std::max(extreme,point);
    if(escherBoundaryTilePointSide(edges,extreme,true)){
        // 7a4270 retains the selected extreme as its second corner.
        const std::array<EscherIntegerPoint,4> corners{EscherIntegerPoint{right,bottom},extreme,EscherIntegerPoint{left,top},EscherIntegerPoint{left,bottom}};
        for(size_t at=0;at<4;++at)result.push_back({corners[at],corners[(at+1)%4]});
    }
    return result;
}
std::vector<ConvolutionIntegerSegment> escherBoundaryCloseTileEdges(std::span<const ConvolutionIntegerSegment> edges,
    int64_t width,int64_t height,std::array<int32_t,2> cell){
    const std::array<int64_t,4> bounds{mul(increment(cell[0]),width),mul(cell[0],width),mul(increment(cell[1]),height),mul(cell[1],height)};
    std::array<std::vector<EscherBoundaryBorderEndpoint>,4> border;
    for(const auto &edge:edges)for(int32_t at=0;at<2;++at){bool found=false;
        for(size_t axis=0;axis<2&&!found;++axis)for(size_t side=0;side<2;++side)if(edge[size_t(at)][axis]==bounds[axis*2+side]){border[axis*2+side].push_back({edge,at});found=true;break;}}
    if(std::all_of(border.begin(),border.end(),[](const auto &entries){return entries.empty();}))return escherBoundaryCloseInteriorTile(edges,width,height,cell);
    for(size_t axis=0;axis<2;++axis)for(size_t side=0;side<2;++side)escherBoundarySortBorderEndpoints(border[axis*2+side],int32_t(axis),int32_t(side));
    const std::array<size_t,4> order{0,2,1,3};
    const std::array<EscherIntegerPoint,4> corners{EscherIntegerPoint{bounds[0],bounds[2]},EscherIntegerPoint{bounds[1],bounds[2]},EscherIntegerPoint{bounds[1],bounds[3]},EscherIntegerPoint{bounds[0],bounds[3]}};
    for(size_t at=0;at<4;++at){auto &current=border[order[at]];const auto &next=border[order[(at+1)%4]];const auto corner=corners[at];
        if((!current.empty()&&endpoint(current.back())==corner)||(!next.empty()&&endpoint(next.front())==corner))continue;
        current.push_back({ConvolutionIntegerSegment{corner,corner},2});current.push_back({ConvolutionIntegerSegment{corner,corner},2});
    }
    std::vector<EscherBoundaryBorderEndpoint> perimeter;for(const auto side:order)perimeter.insert(perimeter.end(),border[side].begin(),border[side].end());
    const auto first=std::find_if(perimeter.begin(),perimeter.end(),[](const auto &value){return value.endpoint==1;});
    require(first!=perimeter.end(),PN2S_INVALID,"Escher tile boundary has no entering segment");std::rotate(perimeter.begin(),first,perimeter.end());
    int32_t previousKind=1;for(auto &value:perimeter){if(value.endpoint==2)value.endpoint=1-previousKind;previousKind=value.endpoint;}
    std::vector<ConvolutionIntegerSegment> result;auto previous=endpoint(perimeter.front());bool active=false;
    for(const auto &value:perimeter){const auto point=endpoint(value);if(active){if(value.edge[0]!=previous)result.push_back({previous,point});active=false;}else active=true;previous=point;}
    result.insert(result.end(),edges.begin(),edges.end());return result;
}
std::vector<EscherBoundaryRow> escherBoundaryFilledRows(std::span<const EscherBoundaryTileClassification> classifications,
    std::span<const std::array<int32_t,2>> cells){
    require(classifications.size()==cells.size(),PN2S_INVALID,"Escher filled rows require matching cell statuses");
    const auto hash=[](int32_t value){uint64_t result=0xcbf29ce484222325;for(size_t n=0;n<4;++n){result^=uint8_t(uint32_t(value)>>(n*8));result*=0x100000001b3;}return result;};
    std::vector<int32_t> columns;std::vector<std::vector<EscherBoundaryColumnEntry>> entries;std::map<int32_t,size_t> indices;
    std::list<size_t> order;using Iterator=std::list<size_t>::iterator;std::vector<Iterator> buckets(8,order.end());
    const auto rehash=[&](size_t capacity){buckets.assign(capacity,order.end());for(auto at=order.begin();at!=order.end();){const auto next=std::next(at);auto &first=buckets[hash(columns[*at])&(capacity-1)];if(first!=order.end())order.splice(first,order,at);first=at;at=next;}};
    for(size_t at=0;at<cells.size();++at){const auto column=cells[at][0];auto found=indices.find(column);size_t index;
        if(found!=indices.end())index=found->second;else {const auto required=size_t(std::ceil(float(columns.size()+1)));if(buckets.size()<required){auto desired=std::max<size_t>(8,required);if(buckets.size()<512)desired=std::max(desired,buckets.size()*8);rehash(std::bit_ceil(desired));}
            index=columns.size();columns.push_back(column);entries.emplace_back();indices.emplace(column,index);auto &first=buckets[hash(column)&(buckets.size()-1)];first=order.insert(first,index);}
        entries[index].push_back({classifications[at],cells[at][1]});}
    std::vector<EscherBoundaryRow> result;for(const auto index:order)result.push_back({columns[index],escherBoundaryColumnRuns(entries[index])});return result;
}
ConvolutionIntegerPolygon escherBoundaryTraceTileEdges(std::span<const ConvolutionIntegerSegment> input){
    std::vector<ConvolutionIntegerSegment> canonical;std::map<ConvolutionIntegerSegment,int32_t> signedCounts;
    for(auto edge:input){require(edge[0]!=edge[1],PN2S_INVALID,"Escher tile tracing requires nonzero edges");const bool forward=edge[0]>edge[1];if(!forward)std::swap(edge[0],edge[1]);canonical.push_back(edge);
        auto &count=signedCounts[edge];count=std::bit_cast<int32_t>(uint32_t(count)+(forward?1U:UINT32_MAX));}
    std::vector<ConvolutionIntegerSegment> directed;std::map<ConvolutionIntegerSegment,int32_t> remaining;
    for(auto edge:convolutionIntegerEdgeOrder(canonical)){auto count=signedCounts[edge];if(!count)continue;if(count<0){std::swap(edge[0],edge[1]);count=std::bit_cast<int32_t>(0U-uint32_t(count));}directed.push_back(edge);remaining[edge]=count;}
    const auto ordered=convolutionIntegerEdgeOrder(directed);std::map<EscherIntegerPoint,ConvolutionIntegerRing> adjacency;
    for(const auto &edge:ordered)adjacency[edge[0]].push_back(edge[1]);
    const auto difference=[](EscherIntegerPoint a,EscherIntegerPoint b){return EscherIntegerPoint{sub(a[0],b[0]),sub(a[1],b[1])};};
    const auto primitive=[](EscherIntegerPoint value){const auto divisor=std::bit_cast<int64_t>(convolutionIntegerGcd(value));return EscherIntegerPoint{value[0]/divisor,value[1]/divisor};};
    const auto turn=[](EscherIntegerPoint a,EscherIntegerPoint b){const auto value=sub(mul(a[0],b[1]),mul(a[1],b[0]));return (value>0)-(value<0);};
    const auto choose=[&](EscherIntegerPoint back,EscherIntegerPoint best,EscherIntegerPoint candidate){const auto bc=turn(back,candidate),sc=turn(best,candidate);
        const auto boundary=bc==0&&sc==-1;if(candidate==back||candidate==best||back==best)return boundary;
        if(turn(back,best)==1)return bc==1?sc!=1:boundary;return bc==1||sc!=1?true:boundary;};
    ConvolutionIntegerPolygon result;
    for(size_t start=0;start<ordered.size();){if(remaining[ordered[start]]<=0){++start;continue;}
        std::vector<ConvolutionIntegerSegment> path{ordered[start]};std::map<ConvolutionIntegerSegment,size_t> visited{{ordered[start],0}};
        for(;;){const auto previous=path.back();const auto &neighbors=adjacency[previous[1]];if(neighbors.empty())break;
            const auto back=primitive(difference(previous[0],previous[1]));size_t first=0;while(first+1<neighbors.size()&&neighbors[first]==previous[0])++first;
            auto selected=neighbors[first],best=primitive(difference(selected,previous[1]));
            for(auto candidate:neighbors){if(candidate==previous[0])continue;const auto direction=primitive(difference(candidate,previous[1]));if(choose(back,best,direction)){selected=candidate;best=direction;}}
            const ConvolutionIntegerSegment next{previous[1],selected};if(remaining[next]==0)break;
            if(const auto found=visited.find(next);found!=visited.end()){ConvolutionIntegerRing ring;for(auto n=found->second;n<path.size();++n)ring.push_back(path[n][0]);if(ring.size()>2)result.push_back(std::move(ring));break;}
            visited.emplace(next,path.size());path.push_back(next);}
        for(const auto &edge:path)--remaining[edge];}
    return result;
}
EscherBoundaryTiledPolygon escherBoundaryTilePolygon(const ConvolutionIntegerPolygon &polygon,int64_t width,int64_t height){
    require(width>0&&height>0,PN2S_INVALID,"Escher tiling requires positive periods");
    require(!polygon.empty()&&!polygon.front().empty(),PN2S_INVALID,"Escher tiling requires a source polygon");
    auto lower=polygon.front().front(),upper=lower;std::vector<ConvolutionIntegerSegment> edges;
    for(const auto &ring:polygon)for(size_t at=0;at<ring.size();++at){const auto &point=ring[at];for(size_t axis=0;axis<2;++axis){lower[axis]=std::min(lower[axis],point[axis]);upper[axis]=std::max(upper[axis],point[axis]);}edges.push_back({point,ring[(at+1)%ring.size()]});}
    const auto narrow=[](int64_t value){return std::bit_cast<int32_t>(uint32_t(value));};
    const auto vertical=escherBoundaryGridLines(narrow(sub(lower[0]/width,1)),narrow(add(upper[0]/width,1)),width,lower[1],upper[1]);
    auto split=escherBoundaryGridSplit(edges,{});const bool cancel=split.size()!=edges.size();if(cancel)split=escherBoundaryCancelEdges(split);
    split=escherBoundaryGridSplit(split,vertical);
    auto horizontal=escherBoundaryGridLines(narrow(sub(lower[1]/height,1)),narrow(add(upper[1]/height,1)),height,lower[0],upper[0]);
    for(auto &edge:horizontal)for(auto &point:edge)std::swap(point[0],point[1]);split=escherBoundaryGridSplit(split,horizontal);if(cancel)split=escherBoundaryCancelEdges(split);
    auto groups=escherBoundaryCollectCells(split,width,height);std::vector<EscherBoundaryTileClassification> classifications;classifications.reserve(groups.cells.size());
    for(size_t at=0;at<groups.cells.size();++at){const auto cell=groups.cells[at];const EscherIntegerBox box{mul(cell[0],width),mul(cell[1],height),mul(increment(cell[0]),width),mul(increment(cell[1]),height)};
        classifications.push_back(escherBoundaryClassifyTile(groups.segments[at],box));}
    std::vector<std::vector<ConvolutionIntegerSegment>> closed;for(size_t at=0;at<groups.cells.size();++at)closed.push_back(escherBoundaryCloseTileEdges(groups.segments[at],width,height,groups.cells[at]));
    EscherBoundaryTiledPolygon result;result.rows=escherBoundaryFilledRows(classifications,groups.cells);
    for(size_t at=0;at<closed.size();++at)if(!classifications[at].inside&&!classifications[at].outside)result.partials.push_back(escherBoundaryTraceTileEdges(closed[at]));
    result.cells=escherBoundaryTileCoordinates(groups.cells,classifications);return result;
}
}
