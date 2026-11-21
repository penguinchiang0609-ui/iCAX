#include "recovered_escher_maximum_phase.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
double unsignedDouble(uint64_t value){
    if(value<0x8000000000000000ULL)return double(int64_t(value));
    const double half=double(int64_t((value>>1)|(value&1)));return half+half;
}
int64_t cvtt64(double value){return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
uint64_t unsignedTruncate(double value){
    uint64_t offset=0;
    if(value>=0x1p63){value-=0x1p63;if(value<0x1p63)offset=0x8000000000000000ULL;}
    return uint64_t(cvtt64(value))+offset;
}
}
EscherMaximumPhaseSelection escherGeneralMaximumPhase(size_t members,
    std::span<const EscherPeriodCandidate> candidates,EscherIntegerBox rectangle,bool special,
    EscherIntegerPoint axis,uint64_t previousBest,uint64_t limit,int32_t restriction) {
    require(members>0,PN2S_INVALID,"Escher maximum phase requires members");
    EscherMaximumPhaseSelection selected;int64_t bestNegativeArea=0;
    const int64_t sheetWidth=sub(rectangle[2],rectangle[0]),sheetHeight=sub(rectangle[3],rectangle[1]);
    int32_t angle=(restriction%360)*10000;angle%=3600000;if(angle<0)angle+=3600000;
    const double increment=(double(angle)/10000.)*.017453292519943295;
    for(size_t index=0;index<candidates.size();++index){
        const auto &candidate=candidates[index];
        require(candidate.boxes.size()==members&&candidate.width>0&&candidate.step[1]>0,
            PN2S_INVALID,"Escher maximum phase candidate has incomplete geometry");
        if(axis[0]==0&&(special||candidate.step[0]==0))continue;
        if(restriction!=0){
            const auto &u=candidate.basis[0];
            const double bearing=std::atan2(double(sub(mul(axis[1],u[0]),mul(axis[0],u[1]))),
                double(add(mul(axis[1],u[1]),mul(axis[0],u[0]))));
            if(!(std::fabs(std::fmod(std::fabs(bearing)+.01,std::fabs(increment))-.01)<.001))continue;
        }
        for(uint32_t normalization=0;normalization<(members==2?2U:1U);++normalization){
            auto boxes=candidate.boxes;
            if(members==2)escherNormalizePairBoxes(normalization,{candidate.width,0},candidate.step,boxes);
            else {const auto x=boxes[0][0],y=boxes[0][1];for(size_t at=0;at<4;++at)boxes[0][at]=sub(boxes[0][at],at%2?y:x);}
            std::vector<std::vector<EscherRepeatRow>> rows(members);
            std::vector<std::vector<int64_t>> positive(members),negative(members);
            uint64_t total=0;bool rejected=false;
            for(size_t member=0;member<members&&!rejected;++member){
                const auto &box=boxes[member];const int64_t quotient=box[1]/candidate.step[1];
                const int64_t top=sub(box[3],mul(quotient,candidate.step[1]));
                const uint64_t count=sheetHeight<top?0:uint64_t(sub(sheetHeight,top)/candidate.step[1])+1;
                int64_t x=sub(box[0],mul(candidate.step[0],quotient))%candidate.width;if(x<0)x=add(x,candidate.width);
                const int64_t allowance=add(sub(box[0],box[2]),sheetWidth);
                if(allowance<0){rejected=true;break;}
                int64_t y=sub(0,mul(candidate.step[1],quotient));
                const uint64_t minimum=unsignedTruncate(unsignedDouble(previousBest)/unsignedDouble(uint64_t(members)*count+1));
                rows[member].resize(size_t(count));positive[member].resize(size_t(count));negative[member].resize(size_t(count));
                for(size_t row=0;row<size_t(count);++row){
                    if(x>=candidate.width)x=sub(x,candidate.width);
                    const uint64_t columns=allowance<x?0:uint64_t(sub(allowance,x)/candidate.width)+1;
                    if(columns<minimum){rejected=true;break;}
                    rows[member][row]={x,y,int64_t(uint32_t(columns))};
                    positive[member][row]=sub(candidate.width,x);
                    negative[member][row]=add(sub(mul(sub(1,int64_t(columns)),candidate.width),x),allowance);
                    total+=columns;x=add(x,candidate.step[0]);y=add(y,candidate.step[1]);
                }
            }
            if(rejected)continue;
            std::vector<EscherCandidateAreaIndex> events{{0,0,0}};
            for(const auto &member:negative)for(const auto x:member)events.push_back({x,-1,0});
            for(const auto &member:positive)for(const auto x:member)events.push_back({x,1,0});
            escherSortCandidateAreas(events);
            int32_t running=0,gain=0;int64_t phase=0;
            for(const auto &event:events){running=std::bit_cast<int32_t>(uint32_t(running)+uint32_t(event.index));if(gain<running){gain=running;phase=event.area;}}
            if(gain>0&&selected.count<total+uint64_t(gain)){
                total+=uint64_t(gain);
                for(size_t member=0;member<members;++member)for(size_t row=0;row<rows[member].size();++row){
                    auto &value=rows[member][row];value.x=add(value.x,phase);if(value.x>=candidate.width)value.x=sub(value.x,candidate.width);
                    if(negative[member][row]<phase)value.count=int64_t(uint32_t(value.count)-1U);
                    if(positive[member][row]<=phase)value.count=int64_t(uint32_t(value.count)+1U);
                }
            }
            int64_t determinant=sub(mul(candidate.basis[0][0],candidate.basis[1][1]),mul(candidate.basis[1][0],candidate.basis[0][1]));
            if(determinant<0)determinant=sub(0,determinant);const int64_t negativeArea=sub(0,determinant);
            if(total<selected.count || (total==selected.count && negativeArea<=bestNegativeArea))continue;
            selected.candidate=index;selected.normalization=normalization;selected.count=total;selected.rows=std::move(rows);selected.offsets.resize(members);
            for(size_t member=0;member<members;++member)selected.offsets[member]={add(sub(0,candidate.boxes[member][0]),rectangle[0]),add(sub(boxes[member][1],candidate.boxes[member][1]),rectangle[1])};
            bestNegativeArea=negativeArea;previousBest=total;
            if(limit<total)return selected;
        }
    }
    return selected;
}
EscherPatternLayout escherGeneralMaximumPattern(std::span<const EscherPatternRecord> records,
    std::span<const EscherPeriodCandidate> candidates,EscherIntegerBox rectangle,bool special,
    EscherIntegerPoint axis,uint64_t previousBest,uint64_t limit,
    std::span<const EscherRecordGeometry> geometry,int32_t restriction) {
    const auto selected=escherGeneralMaximumPhase(records.size(),candidates,rectangle,special,axis,previousBest,limit,restriction);
    if(!selected.candidate)return {};
    return escherLayoutFromRows(records,candidates[*selected.candidate],selected.rows,selected.offsets,geometry);
}
}
