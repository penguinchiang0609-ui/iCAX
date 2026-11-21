#include "recovered_escher_equal_maximum_phase.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
uint64_t fitting(int64_t height,int64_t top,int64_t step){return height<top?0:uint64_t(sub(height,top)/step)+1;}
int32_t narrow32(uint64_t value){return std::bit_cast<int32_t>(uint32_t(value));}
}
EscherEqualMaximumRows escherSeedEqualMaximumRows(std::span<const uint64_t> counts,
    std::span<const EscherIntegerBox> boxes,int64_t columns,int64_t period,EscherIntegerPoint step,int64_t allowance){
    require(counts.size()==boxes.size(),PN2S_INVALID,"Escher maximum seed row/box count mismatch");
    EscherEqualMaximumRows result;result.members.resize(boxes.size());
    const auto add=[](int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));};
    const auto preceding=std::bit_cast<int64_t>(uint64_t(columns)-1);
    for(size_t member=0;member<boxes.size();++member){
        int64_t rawX=boxes[member][0],y=0;
        for(uint64_t index=0;index<counts[member];++index){
            const int64_t x=rawX>=period?add(rawX,std::bit_cast<int64_t>(uint64_t(0)-uint64_t(period))):rawX;
            const int64_t count=allowance<x?preceding:columns;
            // The original count write is a DWORD; padding stays independent
            // of the full-width count accumulated in the total.
            result.members[member].push_back({x,y,int64_t(uint32_t(count))});
            result.coordinates.push_back(x);result.total+=uint64_t(count);rawX=add(x,step[0]);y=add(y,step[1]);
        }
    }
    return result;
}
EscherMaximumPhaseSelection escherEqualMaximumPhase(size_t members,
    std::span<const EscherPeriodCandidate> candidates,EscherIntegerBox rectangle,bool special,
    EscherIntegerPoint axis,uint64_t previousBest,uint64_t limit,int32_t restriction){
    require(members>=1&&members<=2,PN2S_INVALID,"Escher equal maximum phase requires one or two members");
    EscherMaximumPhaseSelection selected;selected.count=previousBest;int64_t bestArea=0;
    const int64_t sheetWidth=sub(rectangle[2],rectangle[0]),sheetHeight=sub(rectangle[3],rectangle[1]);
    int32_t angle=(restriction%360)*10000;angle%=3600000;if(angle<0)angle+=3600000;
    const double increment=(double(angle)/10000.)*.017453292519943295;
    for(size_t index=0;index<candidates.size();++index){
        const auto &candidate=candidates[index];
        require(candidate.boxes.size()==members&&candidate.offsets.size()==members&&candidate.width>0&&candidate.step[1]>0,
            PN2S_INVALID,"Escher equal maximum candidate has incomplete geometry");
        if(axis[0]==0&&(special||candidate.step[0]==0))continue;
        if(restriction!=0){const auto &u=candidate.basis[0];const double bearing=std::atan2(double(sub(mul(axis[1],u[0]),mul(axis[0],u[1]))),double(add(mul(axis[1],u[1]),mul(axis[0],u[0]))));if(!(std::fabs(std::fmod(std::fabs(bearing)+.01,std::fabs(increment))-.01)<.001))continue;}
        const auto &first=candidate.boxes[0];
        const uint64_t columns=fitting(sheetWidth,sub(first[2],first[0]),candidate.width);
        const uint64_t preliminary=fitting(sheetHeight,sub(first[3],first[1]),candidate.step[1])*members;
        if(!columns||!preliminary||previousBest>columns*preliminary)continue;
        auto boxes=candidate.boxes;
        for(size_t member=0;member<members;++member)for(size_t coordinate=0;coordinate<4;++coordinate)boxes[member][coordinate]=add(boxes[member][coordinate],candidate.offsets[member][coordinate%2]);
        std::vector<uint64_t> counts(members),adjusted(members);uint64_t rowTotal=0;
        for(size_t member=0;member<members;++member){counts[member]=fitting(sheetHeight,boxes[member][3],candidate.step[1]);rowTotal+=counts[member];}
        bool extra=false;
        if(members==2){const size_t base=(boxes[0][0]==0&&boxes[0][1]==0)?0:1,other=base^1;
            if(counts[other]<counts[base]){adjusted[0]=adjusted[1]=counts[base];extra=true;}
            else {adjusted[other]=counts[other];adjusted[base]=fitting(add(sheetHeight,boxes[other][1]),boxes[base][3],candidate.step[1]);extra=adjusted[base]==counts[base]+1;}
        }else adjusted[0]=counts[0];
        if(!rowTotal||previousBest>columns*rowTotal)continue;
        const int64_t allowance=add(sub(sub(sheetWidth,mul(int64_t(columns-1),candidate.width)),first[2]),first[0]);
        auto seed=escherSeedEqualMaximumRows(adjusted,boxes,int64_t(columns),candidate.width,candidate.step,allowance);
        auto &rows=seed.members;auto coordinates=std::move(seed.coordinates);uint64_t total=seed.total;
        const bool swapped=extra&&(boxes[0][0]!=0||boxes[0][1]!=0);if(swapped)std::swap(rows.front(),rows.back());
        const size_t tail=members==2&&counts[0]!=counts[1]?1:0;
        require(!rows[0].empty()&&!rows[tail].empty(),PN2S_INVALID,"Escher equal maximum phase has no seam rows");
        bool keepFirst=true,ordinary=!extra;int64_t phase=0,gain=0;
        if(extra){
            const auto firstRow=rows[0].front(),lastRow=rows[tail].back();
            if(firstRow.x==lastRow.x&&firstRow.y==lastRow.y){
                total-=uint64_t(int64_t(narrow32(uint64_t(lastRow.count))));rows[tail].pop_back();std::swap(coordinates.front(),coordinates.back());coordinates.pop_back();ordinary=true;
            }else{
                total=std::max(total-uint64_t(int64_t(narrow32(uint64_t(lastRow.count)))),total-uint64_t(int64_t(narrow32(uint64_t(firstRow.count)))));
                const int32_t baseline=narrow32(uint64_t(mul(sub(1,int64_t(columns)),int64_t(rowTotal)))+total);
                int32_t best=baseline;int64_t negativePhase=0,boundaryFirst=firstRow.x,boundaryLast=lastRow.x;
                std::sort(coordinates.begin(),coordinates.end());const size_t inside=size_t(std::upper_bound(coordinates.begin(),coordinates.end(),allowance)-coordinates.begin());
                for(size_t at=0;at<inside;++at)coordinates.push_back(add(coordinates[at],candidate.width));
                size_t begin=0,end=inside?inside-1:0;
                while(end<coordinates.size()){
                    const auto left=coordinates[begin],right=coordinates[end];
                    if(allowance<sub(right,left)){
                        ++begin;if(boundaryFirst<coordinates[begin])boundaryFirst=add(boundaryFirst,candidate.width);if(boundaryLast<coordinates[begin])boundaryLast=add(boundaryLast,candidate.width);if(begin==end)++end;
                    }else{
                        const int32_t count=narrow32(end-begin+1);
                        const int32_t dropLast=narrow32(uint32_t(count)-uint32_t(left<=boundaryLast&&boundaryLast<=right));
                        const int32_t dropFirst=narrow32(uint32_t(count)-uint32_t(left<=boundaryFirst&&boundaryFirst<=right));
                        if(best<dropLast){negativePhase=sub(0,left);keepFirst=true;best=dropLast;}
                        if(best<dropFirst){negativePhase=sub(0,left);keepFirst=false;best=dropFirst;}
                        ++end;
                    }
                }
                phase=add(negativePhase,candidate.width);if(phase==candidate.width)phase=0;
                if(keepFirst)rows[tail].pop_back();else {std::swap(rows[0].front(),rows[0].back());rows[0].pop_back();}
                gain=narrow32(uint32_t(best)-uint32_t(baseline));
            }
        }
        if(ordinary&&!coordinates.empty()){
            std::sort(coordinates.begin(),coordinates.end());const size_t inside=size_t(std::upper_bound(coordinates.begin(),coordinates.end(),allowance)-coordinates.begin());
            for(size_t at=0;at<inside;++at)coordinates.push_back(add(coordinates[at],candidate.width));
            size_t begin=0,end=inside?inside-1:0;int64_t best=int64_t(inside),negativePhase=0;
            while(end<coordinates.size()){
                if(allowance<sub(coordinates[end],coordinates[begin]))++begin;
                else {const int64_t count=int64_t(end-begin+1);if(best<count){best=count;negativePhase=sub(0,coordinates[begin]);}++end;}
            }
            gain=sub(best,int64_t(inside));phase=negativePhase?add(negativePhase,candidate.width):0;
        }
        if(gain>0&&previousBest<total+uint64_t(gain)){
            total+=uint64_t(gain);
            for(auto &member:rows)for(auto &row:member){row.x=add(row.x,phase);if(row.x>=candidate.width)row.x=sub(row.x,candidate.width);row.count=int64_t(uint32_t(row.x<=allowance?columns:columns-1));}
        }
        int64_t area=sub(mul(candidate.basis[0][0],candidate.basis[1][1]),mul(candidate.basis[1][0],candidate.basis[0][1]));if(area<0)area=sub(0,area);
        if(total<previousBest||(total==previousBest&&area<=bestArea))continue;
        auto offsets=candidate.offsets;for(size_t member=0;member<members;++member)offsets[member][0]=sub(offsets[member][0],boxes[member][0]);
        if(!keepFirst){const int64_t y=boxes[swapped?0:1][1];for(auto &offset:offsets)offset[1]=sub(offset[1],y);}
        if(swapped)std::swap(rows.front(),rows.back());
        selected.candidate=index;selected.count=total;selected.rows=std::move(rows);selected.offsets=std::move(offsets);bestArea=area;previousBest=total;
        if(limit<total)break;
    }
    for(auto &offset:selected.offsets){offset[0]=add(offset[0],rectangle[0]);offset[1]=add(offset[1],rectangle[1]);}
    return selected;
}
EscherPatternLayout escherEqualMaximumPattern(std::span<const EscherPatternRecord> records,
    std::span<const EscherPeriodCandidate> candidates,EscherIntegerBox rectangle,bool special,
    EscherIntegerPoint axis,uint64_t previousBest,uint64_t limit,
    std::span<const EscherRecordGeometry> geometry,int32_t restriction){
    const auto selected=escherEqualMaximumPhase(records.size(),candidates,rectangle,special,axis,previousBest,limit,restriction);
    if(!selected.candidate)return {};
    return escherLayoutFromRows(records,candidates[*selected.candidate],selected.rows,selected.offsets,geometry);
}
}
