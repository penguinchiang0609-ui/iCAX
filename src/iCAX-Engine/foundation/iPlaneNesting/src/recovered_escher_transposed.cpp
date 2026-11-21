#include "recovered_escher_transposed.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int32_t narrow(uint64_t value){return std::bit_cast<int32_t>(uint32_t(value));}
bool keyLess(const std::array<int64_t,3> &a,const std::array<int64_t,3> &b){return std::tie(a[0],a[2],a[1])<std::tie(b[0],b[2],b[1]);}
std::vector<EscherRecordGeometry> layoutGeometries(std::span<const EscherPatternRecord> placed,
    std::span<const EscherPatternRecord> members,std::span<const EscherRecordGeometry> geometries) {
    std::vector<EscherRecordGeometry> result;result.reserve(placed.size());
    for(const auto &record:placed) {
        const auto member=std::find_if(members.begin(),members.end(),[&](const auto &value){return value.geometry==record.geometry;});
        require(member!=members.end(),PN2S_INVALID,"Escher enumerated record has no source geometry");result.push_back(geometries[size_t(member-members.begin())]);
    }
    return result;
}
}
std::vector<EscherRepeatRow> escherTransposedRows(std::span<const EscherIntegerBox> boxes,int64_t width,
    EscherIntegerPoint step,int64_t allowance,uint64_t required,int64_t columns) {
    require((boxes.size()==1||boxes.size()==2)&&width>0&&required>0&&columns>1,PN2S_INVALID,"Escher transposed row seed is invalid");
    std::vector<EscherIntegerPoint> points;for(const auto &box:boxes){int64_t x=box[0]%width;if(x<0)x=add(x,width);points.push_back({x,0});}
    std::vector<EscherRepeatRow> rows;uint64_t total=0;size_t member=0;
    do {
        auto &point=points[member];if(point[0]>=width)point[0]=sub(point[0],width);
        int64_t count=allowance<point[0]?sub(columns,1):columns;total+=uint64_t(count);if(total>=required)count=add(sub(count,int64_t(total)),int64_t(required));
        rows.push_back({point[0],point[1],count});point[0]=add(point[0],step[0]);point[1]=add(point[1],step[1]);if(boxes.size()==2)member=member==0;
    } while(total<required);
    return rows;
}
EscherPatternLayout escherTransposedSinglePattern(std::span<const EscherPatternRecord> records,std::span<const EscherPeriodCandidate> candidates,
    EscherIntegerBox rectangle,uint64_t required,std::array<int64_t,3> &key,std::span<const EscherRecordGeometry> geometries,
    int32_t restrictionDegrees,bool specialCatalog) {
    require((records.size()==1||records.size()==2)&&records.size()==geometries.size()&&required>0,PN2S_INVALID,"Escher transposed enumeration requires one or two members");
    if(specialCatalog)return {};
    const int64_t width=sub(rectangle[2],rectangle[0]);int64_t bestLength=key[0];std::optional<size_t> selected;
    std::vector<std::vector<EscherRepeatRow>> selectedRows;std::vector<EscherIntegerPoint> selectedOffsets;
    for(size_t candidateIndex=0;candidateIndex<candidates.size();++candidateIndex) {
        const auto &candidate=candidates[candidateIndex];if(candidate.step[0]==0)continue;
        require(candidate.width>0&&candidate.boxes.size()==records.size()&&candidate.offsets.size()==records.size(),PN2S_INVALID,"Escher transposed candidate has incomplete geometry");
        if(restrictionDegrees!=0) {
            const int32_t angle=(restrictionDegrees%360)*10000;const double radians=double(angle)/10000.*.017453292519943295;
            const auto &u=candidate.basis[0];const double bearing=std::atan2(-double(u[0])-double(u[1])*0.,double(u[0])*0.-double(u[1]));
            if(!(std::fabs(std::fmod(std::fabs(bearing)+.01,std::fabs(radians))-.01)<.001))continue;
        }
        const auto &first=candidate.boxes[0];const int64_t memberWidth=sub(first[2],first[0]);if(width<memberWidth)continue;
        const uint64_t columns=uint64_t(sub(width,memberWidth)/candidate.width)+1;if(columns<=1)continue;
        const uint64_t preliminaryRows=required/columns+uint64_t(required%columns!=0);
        const int64_t lastRow=records.size()==2?int64_t((preliminaryRows+1)/2)-1:int64_t(preliminaryRows)-1;
        if(add(sub(mul(lastRow,candidate.step[1]),first[1]),first[3])>bestLength)continue;
        auto boxes=candidate.boxes;
        for(size_t member=0;member<boxes.size();++member)for(size_t coordinate=0;coordinate<4;++coordinate)
            boxes[member][coordinate]=add(boxes[member][coordinate],candidate.offsets[member][coordinate%2]);
        const bool swapped=boxes[0][0]!=0||boxes[0][1]!=0;if(swapped){require(boxes.size()==2,PN2S_INVALID,"Escher transposed one-member candidate must start at zero");std::swap(boxes[0],boxes[1]);}
        const int64_t allowance=add(sub(sub(boxes[0][0],boxes[0][2]),mul(int64_t(columns-1),candidate.width)),width);
        auto rows=escherTransposedRows(boxes,candidate.width,candidate.step,allowance,required,int64_t(columns));
        int32_t lastCount=narrow(uint64_t(rows.back().count)),gain=0;int64_t shift=0;
        if(rows.size()>1)for(;;) {
            std::vector<int64_t> xs;for(size_t index=0;index+1<rows.size();++index)xs.push_back(rows[index].x);
            gain=0;shift=0;
            if(!xs.empty()) {
                std::sort(xs.begin(),xs.end());const size_t initial=size_t(std::upper_bound(xs.begin(),xs.end(),allowance)-xs.begin());const auto original=xs;
                for(size_t index=0;index<initial;++index)xs.push_back(add(original[index],candidate.width));
                require(initial>0,PN2S_INVALID,"Escher transposed row phase has no initial anchor");
                size_t left=0,right=initial-1;int32_t maximum=narrow(initial);int64_t negativeShift=0;
                while(right!=xs.size()) {
                    if(allowance<sub(xs[right],xs[left]))++left;
                    else {
                        const int32_t count=narrow(right-left+1);++right;if(maximum<count){maximum=count;negativeShift=sub(0,xs[left]);}
                    }
                }
                gain=narrow(uint32_t(maximum)-uint32_t(narrow(initial)));if(negativeShift!=0)shift=add(negativeShift,candidate.width);
            }
            if(gain<lastCount)break;
            rows.pop_back();uint64_t total=0;
            for(size_t index=0;index<rows.size();++index) {
                auto &row=rows[index];row.x=add(row.x,shift);if(row.x>=candidate.width)row.x=sub(row.x,candidate.width);
                row.count=int64_t(row.x<=allowance?columns:columns-1);total+=uint64_t(row.count);
                if(total>=required){row.count=add(sub(row.count,int64_t(total)),int64_t(required));rows.resize(index+1);break;}
            }
            lastCount=narrow(uint64_t(rows.back().count));
        }
        for(auto &row:rows){row.x=add(row.x,shift);if(row.x>=candidate.width)row.x=sub(row.x,candidate.width);row.count=int64_t(row.x<=allowance?columns:columns-1);}
        rows.back().count=narrow(uint32_t(lastCount)-uint32_t(gain));
        const size_t firstRows=records.size()==2?(rows.size()+1)/2:rows.size(),secondRows=records.size()==2?rows.size()/2:0;
        int64_t length=add(mul(int64_t(firstRows-1),candidate.step[1]),boxes[0][3]);
        if(secondRows)length=std::max(length,add(mul(int64_t(secondRows-1),candidate.step[1]),boxes[1][3]));
        if(length>=bestLength)continue;
        std::vector<std::vector<EscherRepeatRow>> grouped(records.size());for(size_t index=0;index<rows.size();++index)grouped[records.size()==2?index%2:0].push_back(rows[index]);
        int64_t area=sub(mul(candidate.basis[1][1],candidate.basis[0][0]),mul(candidate.basis[0][1],candidate.basis[1][0]));if(area<0)area=sub(0,area);
        const std::array<int64_t,3> trialKey{length,length,area};if(!keyLess(trialKey,key))continue;
        if(swapped){std::swap(grouped[0],grouped[1]);std::swap(boxes[0],boxes[1]);}
        auto offsets=candidate.offsets;for(size_t member=0;member<offsets.size();++member)offsets[member][0]=sub(offsets[member][0],boxes[member][0]);
        key=trialKey;bestLength=length;selected=candidateIndex;selectedRows=std::move(grouped);selectedOffsets=std::move(offsets);
    }
    if(!selected)return {};
    for(auto &offset:selectedOffsets){offset[0]=add(offset[0],rectangle[0]);offset[1]=add(offset[1],rectangle[1]);}
    return escherLayoutFromRows(records,candidates[*selected],selectedRows,selectedOffsets,geometries);
}
EscherPatternLayout escherEnumerateSinglePattern(const EscherEnumerationDescriptor &descriptor,EscherIntegerBox rectangle,
    bool specialCatalog,uint64_t required,std::array<int64_t,3> &key,std::span<const EscherRecordGeometry> geometries) {
    require(descriptor.records.size()==geometries.size(),PN2S_INVALID,"Escher orientation enumeration geometry count mismatch");
    EscherPatternLayout output;const auto &candidates=specialCatalog?descriptor.specialCandidates:descriptor.normalCandidates;
    for(const bool transposed:descriptor.orientationChoices) {
        const auto previous=key;EscherPatternLayout trial;
        if(!transposed)trial=descriptor.sameWidth?escherSingleWidthPattern(descriptor.records,candidates,rectangle,required,key,geometries,descriptor.restrictionDegrees):
            escherUnrotatedPattern(descriptor.records,candidates,rectangle,required,key,geometries,descriptor.restrictionDegrees);
        else if(descriptor.sameWidth)trial=escherTransposedSinglePattern(descriptor.records,candidates,escherTransposeRectangle(rectangle),required,key,geometries,descriptor.restrictionDegrees,specialCatalog);
        else continue;
        if(!keyLess(key,previous)||key[0]>rectangle[2])continue;
        output=std::move(trial);
        if(transposed) {
            const auto primaryGeometry=layoutGeometries(output.primary,descriptor.records,geometries),secondaryGeometry=layoutGeometries(output.secondary,descriptor.records,geometries);
            output.primary=escherRotateRecords(output.primary,2700000,primaryGeometry);output.secondary=escherRotateRecords(output.secondary,2700000,secondaryGeometry);
            output.basis={output.basis[1],sub(0,output.basis[0]),output.basis[3],sub(0,output.basis[2])};output.bounds=escherLayoutBounds(output.primary);
        }
    }
    if(descriptor.restrictionDegrees!=0) {
        const auto secondaryGeometry=layoutGeometries(output.secondary,descriptor.records,geometries);
        bool accepted=escherLayoutRotationAccept(output.secondary,descriptor.restrictionDegrees,secondaryGeometry);
        if(accepted){const auto primaryGeometry=layoutGeometries(output.primary,descriptor.records,geometries);accepted=escherLayoutRotationAccept(output.primary,descriptor.restrictionDegrees,primaryGeometry);}
        if(!accepted){output.primary.clear();output.secondary.clear();output.basis={};}
    }
    return output;
}
}
