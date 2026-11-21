#include "recovered_escher_period.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int64_t roundInteger(double value){value=value<0?value-.5:value+.5;return !std::isfinite(value)||value>=0x1p63||value< -0x1p63?INT64_MIN:int64_t(value);}
double norm(EscherIntegerPoint point){const double x=double(point[0]),y=double(point[1]);return std::sqrt(y*y+x*x);}
int32_t normalizeAngle(int64_t angle){const int32_t truncated=std::bit_cast<int32_t>(uint32_t(angle));const int32_t remainder=truncated%3600000;return remainder<0?remainder+3600000:remainder;}
}
bool escherPeriodOrthogonal(EscherIntegerPoint u,EscherIntegerPoint v) {
    const double product=norm(v)*norm(u),dot=double(add(mul(v[1],u[1]),mul(v[0],u[0])));
    return std::fabs(dot/product)<5e-8;
}
EscherPeriodCandidate escherNormalizePeriodBasis(EscherLatticeBasis basis) {
    EscherPeriodCandidate output;output.basis=basis;const auto &u=basis[0];const auto &v=basis[1];
    const double length=norm(u);output.width=roundInteger(length);
    require(output.width!=0,PN2S_INVALID,"Escher periodic width is zero");
    if(escherPeriodOrthogonal(u,v))output.step={0,roundInteger(norm(v))};
    else {
        const double cosine=double(sub(0,u[0]))/length,sine=double(u[1])/length;
        int64_t y=roundInteger(double(v[1])*cosine+double(v[0])*sine);
        int64_t x=roundInteger(double(v[0])*cosine-double(v[1])*sine);
        if(y<0){x=sub(0,x);y=sub(0,y);}
        x=sub(x,mul(x/output.width,output.width));if(x<0)x=add(x,output.width);output.step={x,y};
    }
    output.basisSlope=double(u[0])/double(u[1]);output.stepSlope=double(output.step[0])/double(output.step[1]);return output;
}
std::vector<EscherIntegerBox> escherCandidateBoxes(std::span<const EscherSupportTable> tables,EscherIntegerPoint direction) {
    const double length=norm(direction);require(length!=0,PN2S_INVALID,"Escher projected candidate direction is zero");
    const double cosine=double(direction[0])/length,sine=-double(direction[1])/length;
    std::vector<EscherIntegerBox> boxes;boxes.reserve(tables.size());
    for(const auto &table:tables)boxes.push_back(escherBoundsFromSupport(escherSupportExtrema(table,direction),
        escherSupportExtrema(table,{sub(0,direction[1]),direction[0]}),cosine,sine));return boxes;
}
std::vector<std::vector<EscherRepeatRow>> escherSingleRowPoints(std::span<const EscherIntegerBox> boxes,
    std::span<const uint64_t> counts,int64_t width,EscherIntegerPoint step) {
    require(boxes.size()==counts.size(),PN2S_INVALID,"Escher row counts do not match member boxes");
    std::vector<std::vector<EscherRepeatRow>> output(boxes.size());
    for(size_t member=0;member<boxes.size();++member) {
        auto &rows=output[member];rows.resize(size_t(counts[member]));int64_t x=boxes[member][0],y=0;
        for(auto &row:rows){if(x>=width)x=sub(x,width);row.x=x;row.y=y;x=add(step[0],x);y=add(y,step[1]);}
    }
    return output;
}
std::vector<EscherPatternRecord> escherRotateRecords(std::span<const EscherPatternRecord> records,int32_t angle,
    std::span<const EscherRecordGeometry> geometries) {
    require(records.size()==geometries.size(),PN2S_INVALID,"Escher record geometry does not match members");
    std::vector<EscherPatternRecord> output;output.reserve(records.size());
    for(size_t index=0;index<records.size();++index) {
        auto record=records[index];EscherIntegerPoint point{record.x,record.y};
        if(angle==2700000)point={record.y,sub(0,record.x)};
        else if(angle==1800000)point={sub(0,record.x),sub(0,record.y)};
        else if(angle==900000)point={sub(0,record.y),record.x};
        else if(angle!=0)point=escherRotateRecordPoint(point,(double(angle)/10000.)*.017453292519943295);
        const int32_t added=(record.reflected&0xffU)!=0?normalizeAngle(-int64_t(angle)):angle;
        record.angle=normalizeAngle(int64_t(record.angle)+int64_t(added));record.x=point[0];record.y=point[1];
        escherUpdateRecordBounds(record,geometries[index].intrinsic,geometries[index].collision);output.push_back(record);
    }
    return output;
}
std::vector<EscherPatternRecord> escherRepeatRows(std::span<const EscherPatternRecord> records,int64_t width,
    std::span<const std::vector<EscherRepeatRow>> rows,std::span<const EscherIntegerPoint> offsets) {
    require(records.size()==rows.size()&&records.size()==offsets.size(),PN2S_INVALID,"Escher repeated rows do not match members");
    std::vector<EscherPatternRecord> output;
    for(size_t member=0;member<records.size();++member) {
        const auto &source=records[member];const int64_t x=add(source.x,offsets[member][0]),y=add(source.y,offsets[member][1]);
        for(const auto &row:rows[member])for(int32_t index=0;int64_t(index)<row.count;++index) {
            auto record=source;record.x=add(add(mul(index,width),x),row.x);record.y=add(y,row.y);output.push_back(record);
        }
    }
    return output;
}
EscherPatternLayout escherLayoutFromRows(std::span<const EscherPatternRecord> records,const EscherPeriodCandidate &candidate,
    std::span<const std::vector<EscherRepeatRow>> rows,std::span<const EscherIntegerPoint> offsets,
    std::span<const EscherRecordGeometry> geometries) {
    EscherPatternLayout layout;if(candidate.width<1)return layout;
    const auto &u=candidate.basis[0];const double radians=std::atan2(double(u[0])*0.-double(u[1]),double(u[1])*0.+double(u[0]));
    const int32_t rotation=normalizeAngle(roundInteger((radians*57.29577951308232)*10000.));
    layout.secondary=escherRotateRecords(records,rotation,geometries);layout.primary=escherRepeatRows(layout.secondary,candidate.width,rows,offsets);
    layout.basis={candidate.width,0,candidate.step[0],candidate.step[1]};layout.bounds=escherLayoutBounds(layout.primary);return layout;
}
void escherNormalizePairBoxes(uint32_t mode,EscherIntegerPoint horizontal,EscherIntegerPoint step,
    std::vector<EscherIntegerBox> &boxes) {
    require(boxes.size()>=2&&step[1]>0,PN2S_INVALID,"Escher pair normalization requires two boxes and positive Y spacing");
    const auto shift=[&](EscherIntegerPoint amount) {auto &box=boxes[1];box[0]=add(box[0],amount[0]);box[1]=add(box[1],amount[1]);box[2]=add(box[2],amount[0]);box[3]=add(box[3],amount[1]);};
    const EscherIntegerPoint negative{sub(0,step[0]),sub(0,step[1])};
    if((mode&1U)==0) {
        if(boxes[0][1]<boxes[1][1])while(boxes[0][1]<sub(boxes[1][1],step[1]))shift(negative);
        else if(boxes[1][1]<boxes[0][1])do{shift(step);}while(boxes[1][1]<boxes[0][1]);
    } else {
        if(boxes[0][1]<boxes[1][1])do{shift(negative);}while(boxes[0][1]<boxes[1][1]);
        else while(add(boxes[1][1],step[1])<boxes[0][1])shift(step);
    }
    if(mode>1) {
        require(horizontal[0]>0,PN2S_INVALID,"Escher pair normalization has invalid horizontal spacing");
        if(boxes[0][0]<boxes[1][0])do{shift({sub(0,horizontal[0]),sub(0,horizontal[1])});}while(boxes[0][0]<boxes[1][0]);
        else if(boxes[1][0]<boxes[0][0])do{shift(horizontal);}while(boxes[1][0]<boxes[0][0]);
    }
    int64_t x=boxes[0][0],y=boxes[0][1];for(const auto &box:boxes){x=std::min(x,box[0]);y=std::min(y,box[1]);}
    for(auto &box:boxes){box[0]=sub(box[0],x);box[1]=sub(box[1],y);box[2]=sub(box[2],x);box[3]=sub(box[3],y);}
}
EscherPatternLayout escherUnrotatedPattern(std::span<const EscherPatternRecord> records,std::span<const EscherPeriodCandidate> candidates,
    EscherIntegerBox rectangle,uint64_t required,std::array<int64_t,3> &key,std::span<const EscherRecordGeometry> geometries,
    int32_t restrictionDegrees) {
    if(candidates.empty())return {};
    require(records.size()>=2,PN2S_INVALID,"Escher paired periodic enumeration requires at least two members");
    struct Event {uint64_t id=0;bool wide=false;int64_t x=0;bool operator==(const Event &)const=default;};
    std::optional<size_t> selected;std::vector<std::vector<EscherRepeatRow>> selectedRows;std::vector<EscherIntegerPoint> selectedOffsets;
    for(size_t candidateIndex=0;candidateIndex<candidates.size();++candidateIndex) {
        const auto &candidate=candidates[candidateIndex];const auto &u=candidate.basis[0];
        require(candidate.boxes.size()==records.size()&&candidate.width>0&&candidate.step[1]>0,PN2S_INVALID,"Escher periodic candidate has incomplete member geometry");
        if(restrictionDegrees!=0) {
            const int32_t angle=normalizeAngle(int64_t(restrictionDegrees%360)*10000);
            const double increment=(double(angle)/10000.)*.017453292519943295;
            const double bearing=std::atan2(double(u[0])*0.-double(u[1]),double(u[1])*0.+double(u[0]));
            if(!(std::fabs(std::fmod(std::fabs(bearing)+.01,std::fabs(increment))-.01)<.001))continue;
        }
        std::vector<int64_t> widths;for(const auto &box:candidate.boxes)widths.push_back(sub(box[2],box[0]));
        const int64_t minimum=*std::min_element(widths.begin(),widths.end()),maximum=*std::max_element(widths.begin(),widths.end());
        const size_t wideIndex=records.size()==2?size_t(widths[0]<=widths[1]):records.size();
        for(uint32_t normalization=0;normalization<2;++normalization) {
            auto boxes=candidate.boxes;
            if(boxes.size()==2)escherNormalizePairBoxes(normalization,{candidate.width,0},candidate.step,boxes);
            else {const int64_t x=boxes.front()[0],y=boxes.front()[1];boxes.front()={0,0,sub(boxes.front()[2],x),sub(boxes.front()[3],y)};}
            std::vector<uint64_t> rowCounts;uint64_t rowTotal=0;
            for(const auto &box:boxes){const uint64_t count=box[3]<=rectangle[3]?uint64_t(sub(rectangle[3],box[3])/candidate.step[1])+1:0;rowCounts.push_back(count);rowTotal+=count;}
            if(rowCounts[0]==0||rowCounts[1]==0||required<rowTotal)continue;
            int64_t narrowFront=minimum,wideFront=maximum;uint64_t capacity=rowTotal;bool reject=false;
            if(capacity<required)do {
                if(narrowFront==wideFront){narrowFront=add(narrowFront,candidate.width);wideFront=add(wideFront,candidate.width);capacity+=rowTotal;reject=key[0]<=narrowFront;}
                else if(narrowFront<wideFront){capacity+=rowTotal-(wideIndex<rowCounts.size()?rowCounts[wideIndex]:0);narrowFront=add(narrowFront,candidate.width);reject=key[0]<=narrowFront;}
                else {capacity+=wideIndex<rowCounts.size()?rowCounts[wideIndex]:0;wideFront=add(wideFront,candidate.width);reject=key[0]<=wideFront;}
            }while(!reject&&capacity<required);
            if(reject)continue;
            auto rows=escherSingleRowPoints(boxes,rowCounts,candidate.width,candidate.step);
            for(auto &member:rows)for(auto &row:member)while(row.x>=candidate.width)row.x=sub(row.x,candidate.width);
            std::vector<Event> initial;uint64_t id=0;
            for(size_t member=0;member<rows.size();++member)for(const auto &row:rows[member])initial.push_back({id++,member==wideIndex,row.x});
            std::sort(initial.begin(),initial.end(),[](const auto &a,const auto &b){return std::tie(a.x,a.wide,a.id)<std::tie(b.x,b.wide,b.id);});
            auto events=initial;std::vector<uint64_t> bestCounts(size_t(rowTotal),0);int64_t phase=0,bestPhase=0;bool improved=false;
            const auto rotateFirst=[&](){std::rotate(events.begin(),events.begin()+1,events.end());};
            do {
                uint64_t front=0;bool foundNarrow=false,foundWide=false;
                for(auto event=events.rbegin();event!=events.rend();++event) {
                    if(!foundNarrow&&!event->wide){front=std::max(front,uint64_t(add(event->x,minimum)));foundNarrow=true;}
                    if(!foundWide&&event->wide){front=std::max(front,uint64_t(add(event->x,maximum)));foundWide=true;}
                    if(foundNarrow&&foundWide)break;
                }
                uint64_t currentCount=0;std::vector<uint64_t> counts(size_t(rowTotal),0);std::vector<std::pair<int64_t,uint64_t>> endpoints;
                for(const auto &event:events) {
                    uint64_t count=1;int64_t end=add(maximum,event.x);
                    if(!event.wide) {
                        const int64_t begin=add(minimum,event.x);count=front<uint64_t(begin)?0:uint64_t(sub(int64_t(front),begin)/candidate.width)+1;
                        end=add(mul(int64_t(count)-1,candidate.width),begin);
                    }
                    counts[size_t(event.id)]=count;currentCount+=count;endpoints.push_back({end,event.id});
                }
                std::sort(endpoints.begin(),endpoints.end());
                require(!endpoints.empty()&&uint64_t(endpoints.back().first)==front,PN2S_INTERNAL,"Escher periodic sweep front does not match row endpoints");
                currentCount=std::min(currentCount,required);const uint64_t whole=(required-currentCount)/rowTotal;
                for(auto &count:counts)count+=whole;int64_t length;
                if(((required%rowTotal-currentCount)+rowTotal)%rowTotal==0)length=add(mul(int64_t(whole),candidate.width),int64_t(front));
                else {
                    const uint64_t remainder=(required-whole*rowTotal)-currentCount;
                    require(remainder>0&&remainder<=endpoints.size(),PN2S_INTERNAL,"Escher periodic sweep remainder exceeds its rows");
                    for(size_t index=0;index<size_t(remainder);++index)++counts[size_t(endpoints[index].second)];
                    length=add(mul(int64_t(whole+1),candidate.width),endpoints[size_t(remainder-1)].first);
                }
                int64_t area=sub(mul(candidate.basis[1][1],candidate.basis[0][0]),mul(candidate.basis[0][1],candidate.basis[1][0]));if(area<0)area=sub(0,area);
                if(std::tie(length,area,length)<std::tie(key[0],key[2],key[1])) {
                    improved=true;bestCounts=std::move(counts);bestPhase=phase;key={length,length,area};
                }
                int64_t delta=events[1].x;for(uint64_t skipped=0;delta==0;++skipped){if(rowTotal<skipped)break;rotateFirst();delta=events[1].x;}
                phase=sub(phase,delta);for(auto &event:events){event.x=sub(event.x,delta);if(event.x<0)event.x=add(event.x,candidate.width);}rotateFirst();
                if(events==initial)break;
            }while(events.size()!=1);
            if(improved) {
                id=0;for(auto &member:rows)for(auto &row:member){row.x=add(row.x,bestPhase);if(row.x<0)row.x=add(row.x,candidate.width);row.count=int64_t(bestCounts[size_t(id++)]);}
                selected=candidateIndex;selectedRows=std::move(rows);selectedOffsets.resize(records.size());
                for(size_t member=0;member<records.size();++member)selectedOffsets[member]={sub(0,candidate.boxes[member][0]),sub(boxes[member][1],candidate.boxes[member][1])};
            }
        }
    }
    if(!selected)return {};
    for(auto &offset:selectedOffsets){offset[0]=add(offset[0],rectangle[0]);offset[1]=add(offset[1],rectangle[1]);}
    return escherLayoutFromRows(records,candidates[*selected],selectedRows,selectedOffsets,geometries);
}
}
