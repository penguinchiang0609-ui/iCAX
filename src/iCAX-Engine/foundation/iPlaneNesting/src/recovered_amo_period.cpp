#include "recovered_amo_axis.hpp"
#include <bit>
namespace study::recovered {
namespace {
int32_t truncate32(double value){return value>=-0x1p31&&value<0x1p31?int32_t(value):INT32_MIN;}
double maximum(double first,double second){return first<=second?second:first;}
double period(AmoTilingMapRuntime &runtime,AmoTilingJob &job,const AmoTilingCandidate &candidate,bool horizontal){
    const auto map=[&](size_t first,size_t second){return amoTilingBuildContourMap(runtime.maps,runtime.sources[first],runtime.sources[second],candidate.angle,runtime.geometry);};
    const auto first=map(0,0),second=map(1,1);const size_t axis=horizontal?0:1;
    const auto query=[&](AmoTilingContourMap &value,double coordinate){return horizontal?amoTilingMapAxisX(value,coordinate):amoTilingMapAxisY(value,coordinate);};
    const auto cached=[&](AmoTilingContourMap &value){auto &entry=value.cachedPeriods[axis];if(!entry.valid){entry.value=query(value,0.);entry.valid=true;}return entry.value;};
    const double secondSelf=cached(*second),firstSelf=cached(*first);const double base=maximum(secondSelf,firstSelf);
    const double transverse=horizontal?candidate.translation.y:candidate.translation.x,parallel=horizontal?candidate.translation.x:candidate.translation.y;
    const auto forward=map(0,1);double result=maximum(query(*forward,transverse)-parallel,base);const auto backward=map(1,0);result=maximum(query(*backward,-transverse)+parallel,result);
    const double other=horizontal?candidate.period.y:candidate.period.x;
    if(other>0.){
        const auto members=amoTilingMemberBounds(job,candidate);bool empty=true;Box envelope{};for(const auto &member:members)if(!member.empty){if(empty){envelope=member.box;empty=false;}else{envelope.x0=std::min(envelope.x0,member.box.x0);envelope.y0=std::min(envelope.y0,member.box.y0);envelope.x1=std::max(envelope.x1,member.box.x1);envelope.y1=std::max(envelope.y1,member.box.y1);}}
        const double span=horizontal?std::fabs(envelope.y1-envelope.y0):std::fabs(envelope.x1-envelope.x0),limit=span/other;const int32_t begin=truncate32(-limit);
        require(begin!=INT32_MIN&&limit<double(INT32_MAX-1),PN2S_INVALID,"Amo transverse period scan exceeds its original signed index range");
        for(int32_t index=begin;double(index)<=limit;index+=index==-1?2:1){const double offset=double(index)*other;result=maximum(query(*first,offset),result);result=maximum(query(*second,offset),result);result=maximum(query(*forward,offset+transverse)-parallel,result);result=maximum(query(*backward,offset-transverse)+parallel,result);}
    }
    return result;
}
}
double amoTilingPeriodY(AmoTilingMapRuntime &runtime,AmoTilingJob &job,const AmoTilingCandidate &candidate){return period(runtime,job,candidate,false);}
double amoTilingPeriodX(AmoTilingMapRuntime &runtime,AmoTilingJob &job,const AmoTilingCandidate &candidate){return period(runtime,job,candidate,true);}
}
