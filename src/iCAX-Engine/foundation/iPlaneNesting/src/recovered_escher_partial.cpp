#include "recovered_escher_partial.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int32_t narrow(uint64_t value){return std::bit_cast<int32_t>(uint32_t(value));}
}
EscherPartialPhaseSelection escherPartialPatternPhase(size_t members,
    std::span<const EscherPeriodCandidate> candidates,EscherIntegerBox rectangle,
    int64_t outerArea,int32_t restriction){
    require(members>=1&&members<=2,PN2S_INVALID,"Escher partial phase requires one or two members");
    EscherPartialPhaseSelection result;
    const int64_t sheetWidth=sub(rectangle[2],rectangle[0]),sheetHeight=sub(rectangle[3],rectangle[1]);
    int32_t angle=(restriction%360)*10000;angle%=3600000;if(angle<0)angle+=3600000;
    const double increment=(double(angle)/10000.)*.017453292519943295;
    for(size_t index=0;index<candidates.size();++index){
        const auto &candidate=candidates[index];
        require(candidate.boxes.size()==members&&candidate.width>0&&candidate.step[1]>0,
            PN2S_INVALID,"Escher partial candidate has incomplete geometry");
        if(restriction>=11){const auto &u=candidate.basis[0];const double bearing=std::atan2(double(u[0])*0.-double(u[1]),double(u[1])*0.+double(u[0]));if(!(std::fabs(std::fmod(std::fabs(bearing)+.01,std::fabs(increment))-.01)<.001))continue;}
        for(uint32_t normalization=0;normalization<(members==2?4U:1U);++normalization){
            auto boxes=candidate.boxes;
            if(members==2)escherNormalizePairBoxes(normalization,{candidate.width,0},candidate.step,boxes);
            else {const auto x=boxes[0][0],y=boxes[0][1];for(size_t at=0;at<4;++at)boxes[0][at]=sub(boxes[0][at],at%2?y:x);}
            std::vector<std::vector<EscherRepeatRow>> rows(members);bool rejected=false;
            for(size_t member=0;member<members;++member){
                const auto &box=boxes[member];const int64_t quotient=box[1]/candidate.step[1],shift=mul(quotient,candidate.step[1]);
                const int64_t top=sub(box[3],shift);const uint64_t count=sheetHeight<top?0:uint64_t(sub(sheetHeight,top)/candidate.step[1])+1;
                rows[member].resize(size_t(count));
                int64_t x=sub(box[0],mul(candidate.step[0],quotient));
                // Actual 7c50a4..7c50ad multiplication-before-division; this is
                // not interchangeable with the usual modulo phase expression.
                x=sub(x,mul(x,candidate.width)/candidate.width);if(x<0)x=add(x,candidate.width);
                const int64_t allowance=sub(sheetWidth,box[2]);if(allowance<0){rejected=true;break;}
                int64_t y=sub(0,shift);
                for(auto &row:rows[member]){
                    if(x>=candidate.width)x=sub(x,candidate.width);
                    const uint64_t columns=allowance<x?0:uint64_t(sub(allowance,x)/candidate.width)+1;
                    row={x,y,int64_t(uint32_t(columns))};x=add(x,candidate.step[0]);y=add(y,candidate.step[1]);
                }
            }
            if(rejected)continue;
            for(size_t member=0;member<members;++member)for(const bool removeColumn:{true,false})for(const bool removeRow:{true,false}){
                auto trial=rows;
                if(removeColumn)for(auto &row:trial[member])if(narrow(uint64_t(row.count))>0)row.count=int64_t(uint32_t(row.count)-1U);
                if(removeRow&&!trial[member].empty())trial[member].pop_back();
                auto bounds=boxes[0];if(members==2)for(size_t axis=0;axis<2;++axis){bounds[axis]=std::min(bounds[axis],boxes[1][axis]);bounds[axis+2]=std::max(bounds[axis+2],boxes[1][axis+2]);}
                int32_t count=0;
                for(size_t part=0;part<members;++part)for(const auto &row:trial[part]){
                    const int32_t columns=narrow(uint64_t(row.count));
                    bounds[0]=std::min(bounds[0],row.x);bounds[1]=std::min(bounds[1],row.y);
                    bounds[2]=std::max(bounds[2],add(add(mul(int64_t(columns)-1,candidate.width),boxes[part][2]),row.x));
                    bounds[3]=std::max(bounds[3],add(row.y,boxes[part][3]));count=narrow(uint32_t(count)+uint32_t(columns));
                }
                if(count<1)continue;
                const double density=(double(count)*double(outerArea))/double(mul(sub(bounds[3],bounds[1]),sub(bounds[2],bounds[0])));
                if(!(result.density<density))continue;
                result.density=density;auto &selected=result.geometry;selected.candidate=index;selected.normalization=normalization;selected.count=uint64_t(count);selected.rows=std::move(trial);selected.offsets.resize(members);
                for(size_t part=0;part<members;++part)selected.offsets[part]={add(sub(boxes[part][0],candidate.boxes[part][0]),rectangle[0]),add(sub(boxes[part][1],candidate.boxes[part][1]),rectangle[1])};
            }
        }
    }
    return result;
}
EscherPatternLayout escherPartialPattern(std::span<const EscherPatternRecord> records,
    std::span<const EscherPeriodCandidate> candidates,EscherIntegerBox rectangle,int64_t outerArea,
    std::span<const EscherRecordGeometry> geometries,int32_t restriction){
    const auto result=escherPartialPatternPhase(records.size(),candidates,rectangle,outerArea,restriction);
    if(!result.geometry.candidate)return {};
    return escherLayoutFromRows(records,candidates[*result.geometry.candidate],result.geometry.rows,
        result.geometry.offsets,geometries);
}
}
