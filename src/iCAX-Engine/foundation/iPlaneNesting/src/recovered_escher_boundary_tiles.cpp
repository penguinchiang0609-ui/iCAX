#include "recovered_escher_boundary_tiles.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
}
std::vector<EscherBoundaryIncidentDirection> escherBoundaryIncidentDirections(
    std::span<const ConvolutionIntegerSegment> edges,EscherIntegerPoint point){
    std::vector<EscherBoundaryIncidentDirection> result;
    for(const auto &edge:edges){
        if(edge[0]==point)result.push_back({sub(edge[1][0],point[0]),sub(edge[1][1],point[1]),1});
        else if(edge[1]==point)result.push_back({sub(edge[0][0],point[0]),sub(edge[0][1],point[1]),0});
    }
    return result;
}
bool escherBoundaryTilePointSide(std::span<const ConvolutionIntegerSegment> edges,EscherIntegerPoint point,bool upper){
    const auto directions=escherBoundaryIncidentDirections(edges,point);
    require(!directions.empty(),PN2S_INVALID,"Escher tile side requires an incident source edge");
    auto selected=directions.front();
    for(size_t index=1;index<directions.size();++index){const auto &next=directions[index];bool replace;
        if(upper){if(next.x==0)replace=next.y<0;else if(selected.x==0)replace=selected.y>0;
            else replace=mul(next.x,selected.y)<mul(selected.x,next.y);}
        else {if(selected.x==0)replace=selected.y<0;else if(next.x==0)replace=next.y>0;
            else replace=mul(next.y,selected.x)<mul(next.x,selected.y);}
        if(replace)selected=next;
    }
    return uint8_t(upper)==selected.outgoing;
}
EscherIntegerBox escherBoundaryRotateTileBox(EscherIntegerBox box){
    const EscherIntegerPoint first{sub(0,box[1]),box[0]},second{sub(0,box[3]),box[2]};
    return {std::min(first[0],second[0]),std::min(first[1],second[1]),std::max(first[0],second[0]),std::max(first[1],second[1])};
}
int32_t escherBoundaryTileSide(std::span<const ConvolutionIntegerSegment> edges,EscherIntegerBox box){
    require(!edges.empty(),PN2S_INVALID,"Escher tile classification requires nonempty edges");
    EscherIntegerPoint extreme{box[0],box[1]};for(const auto &edge:edges)for(const auto &point:edge)if(point>extreme)extreme=point;
    if(extreme[0]>=box[2]){
        std::map<EscherIntegerPoint,int32_t> counts;
        for(const auto &edge:edges){if(edge[0][0]==extreme[0]&&box[1]<edge[0][1]&&edge[0][1]<box[3]){auto &count=counts[edge[0]];count=std::bit_cast<int32_t>(uint32_t(count)+1U);}
            if(edge[1][0]==extreme[0]&&box[1]<edge[1][1]&&edge[1][1]<box[3]){auto &count=counts[edge[1]];count=std::bit_cast<int32_t>(uint32_t(count)-1U);}}
        for(const auto &[point,count]:counts)if(count!=0)return 1;
    }
    return escherBoundaryTilePointSide(edges,extreme,box[1]<extreme[1])?-1:0;
}
EscherBoundaryTileClassification escherBoundaryClassifyTile(std::span<const ConvolutionIntegerSegment> input,EscherIntegerBox box){
    require(!input.empty(),PN2S_INVALID,"Escher tile classification requires nonempty edges");
    std::vector<ConvolutionIntegerSegment> edges(input.begin(),input.end());EscherBoundaryTileClassification result;
    for(const size_t at:{3,1,2,0}){result.sides[at]=escherBoundaryTileSide(edges,box);
        for(auto &edge:edges)for(auto &point:edge)point={sub(0,point[1]),point[0]};box=escherBoundaryRotateTileBox(box);}
    const EscherIntegerBox interior{add(mul(box[0],2),1),add(mul(box[1],2),1),sub(mul(box[2],2),1),sub(mul(box[3],2),1)};
    for(const auto &edge:edges){const EscherIntegerPoint midpoint{add(edge[0][0],edge[1][0]),add(edge[0][1],edge[1][1])};
        if(interior[0]<=midpoint[0]&&midpoint[0]<=interior[2]&&interior[1]<=midpoint[1]&&midpoint[1]<=interior[3])return result;}
    const auto &edge=edges.front();const auto cross=sub(mul(sub(add(box[3],box[1])/2,edge[0][1]),sub(edge[1][0],edge[0][0])),
        mul(sub(add(box[2],box[0])/2,edge[0][0]),sub(edge[1][1],edge[0][1])));
    if(cross>0)result.inside=1;else result.outside=1;return result;
}
std::vector<std::array<int32_t,2>> escherBoundaryTileCoordinates(std::span<const std::array<int32_t,2>> cells,
    std::span<const EscherBoundaryTileClassification> classifications){
    require(cells.size()==classifications.size(),PN2S_INVALID,"Escher tile classifications do not match cells");
    std::vector<std::array<int32_t,2>> result;for(size_t index=0;index<cells.size();++index)
        if(!classifications[index].inside&&!classifications[index].outside)result.push_back(cells[index]);return result;
}
std::array<std::vector<int32_t>,2> escherBoundaryFlattenRows(std::span<const EscherBoundaryRow> rows){
    std::array<std::vector<int32_t>,2> result;
    for(const auto &row:rows)for(const auto &run:row.runs){const auto append=[&](int32_t value){result[0].push_back(row.column);result[1].push_back(value);};
        if(run.lowerKind==-1)append(run.lower);if(run.upperKind==-1)append(run.upper);
        for(int64_t value=std::bit_cast<int32_t>(uint32_t(run.lower)+1U);value<int64_t(run.upper);++value)append(int32_t(value));}
    return result;
}
std::vector<EscherBoundaryRun> escherBoundaryColumnRuns(std::span<const EscherBoundaryColumnEntry> source){
    std::vector<EscherBoundaryColumnEntry> entries(source.begin(),source.end());
    std::sort(entries.begin(),entries.end(),[](const auto &a,const auto &b){return a.position<b.position;});
    std::vector<EscherBoundaryRun> result;bool closed=true;int32_t lower=0,lowerKind=0;
    const auto add32=[](int32_t value,uint32_t amount){return std::bit_cast<int32_t>(uint32_t(value)+amount);};
    for(const auto &entry:entries){const auto &status=entry.status;const auto y=entry.position;const auto top=status.sides[0];
        const auto open=[&](){
            if(top==-1){lower=status.outside?add32(y,1):y;lowerKind=(!status.outside&&!status.inside)?1:-1;closed=false;}
            else if(!status.outside)result.push_back({y,1,y,1});
        };
        if(closed){open();continue;}
        if(lower==y&&!status.outside){
            if(status.inside){if(top==-1)continue;if(top==0||top==1){result.push_back({y,-1,y,-1});closed=true;continue;}}
            else {if(top==-1)lowerKind=1;else if(top==0||top==1){result.push_back({y,1,y,1});closed=true;continue;}}
        }
        if(!status.inside||(top!=-1&&uint32_t(top)>1)){
            const auto upper=add32(y,UINT32_MAX);result.push_back({lower,lowerKind,upper,lower==upper?lowerKind:-1});closed=true;open();
        }
        else if(top!=-1){result.push_back({lower,lowerKind,y,-1});closed=true;}
    }
    return result;
}
}
