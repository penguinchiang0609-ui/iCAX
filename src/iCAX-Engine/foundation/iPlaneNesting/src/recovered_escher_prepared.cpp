#include "recovered_escher_prepared.hpp"
#include "recovered_convolution_minkowski.hpp"
#include "recovered_convolution_offset.hpp"
#include <bit>
#include <set>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int32_t narrow(uint32_t value){return std::bit_cast<int32_t>(value);}
int32_t normalized(int32_t value){const int32_t remainder=value%3600000;return remainder<0?remainder+3600000:remainder;}
void negate(ConvolutionIntegerPolygon &rings){for(auto &ring:rings)for(auto &point:ring)point={sub(0,point[0]),sub(0,point[1])};}
int64_t area(const ConvolutionIntegerPolygon &rings){int64_t result=0;for(const auto &ring:rings)result=add(result,convolutionIntegerArea(ring));return result;}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int turn(const ConvolutionIntegerRing &ring,size_t index){const size_t count=ring.size();const auto a=ring[(index+count-1)%count],b=ring[index],c=ring[(index+1)%count];const int64_t cross=sub(mul(sub(c[1],b[1]),sub(b[0],a[0])),mul(sub(b[1],a[1]),sub(c[0],b[0])));return (cross>0)-(cross<0);}
void rotateRing(ConvolutionIntegerRing &ring,int32_t angle){std::vector<int> before;if(ring.size()>=3){before.reserve(ring.size());for(size_t index=0;index<ring.size();++index)before.push_back(turn(ring,index));}for(auto &point:ring)point=escherRotateRecordPoint(point,(double(angle)/10000.)*.017453292519943295);std::vector<size_t> remove;for(size_t index=0;index<before.size();++index){const int after=turn(ring,index);if(after!=before[index]&&after!=0&&before[index]!=0)remove.push_back(index);}for(auto index=remove.rbegin();index!=remove.rend();++index)ring.erase(ring.begin()+*index);}
}
EscherOrientationGrid escherOrientationGrid(std::span<const SdkOrientation> source,bool initiallyValid){
    std::set<std::tuple<double,double,bool>> ranges;bool mirrored=false;for(const auto &range:source){ranges.emplace(range.minimum,range.maximum,range.reflected!=0);mirrored=mirrored||range.reflected!=0;}
    const auto covered=[&](double angle,bool flip){return std::any_of(ranges.begin(),ranges.end(),[&](const auto &range){return std::get<2>(range)==flip&&!(angle<std::get<0>(range)-.001)&&!(std::get<1>(range)+.001<angle);});};
    EscherOrientationGrid result;result.valid=initiallyValid;std::vector<double> candidates{2.123};for(const int32_t divisions:{360,180,120,90,36,12,8,4,2})if(size_t(divisions)<=ranges.size())candidates.push_back(360./double(divisions));candidates.push_back(359.);
    for(const double step:candidates){double angle=0.;while(covered(angle,false)){angle+=step;if(359.<=angle){result.valid=true;result.quantizedStep=step==2.123?0:(int32_t(step)%360)*10000;goto selected;}}}
selected:
    if(mirrored){const double step=result.quantizedStep/10000==0?2.123:double(result.quantizedStep/10000);for(double angle=0.;angle<359.;angle+=step)if(!covered(angle,true)){mirrored=false;break;}}
    result.mirrored=mirrored;return result;
}
EscherNfpMember escherNfpMember(const EscherPatternRecord &record){
    require(record.geometry!=0,PN2S_INVALID,"Escher prepared record has no integer shape owner");const auto &shape=*reinterpret_cast<const EscherIntegerShapeRecord *>(record.geometry);const auto &owner=shape.alternate?*shape.alternate:shape.normal;
    return {&owner.normalContours(),reinterpret_cast<uintptr_t>(&owner),uint8_t(record.reflected&255),record.angle,{record.x,record.y}};
}
void escherAppendSourceRecord(EscherPreparedSource &source,const EscherPatternRecord &record,const ConvolutionIntegerPolygon &normalOuterContours){
    source.descriptor.records.push_back(record);auto rings=normalOuterContours;escherTransformNfp(rings,record.angle,uint8_t(record.reflected&255));std::vector<EscherIntegerPoint> points;
    for(auto &ring:rings)for(auto point:ring){point[0]=add(point[0],record.x);point[1]=add(point[1],record.y);points.push_back(point);}source.supports.push_back(escherBuildSupportTable(std::move(points)));source.descriptor.sameWidth=source.descriptor.records.size()==1;
}
EscherPreparedSource escherSourceDescriptor(std::span<const EscherPatternRecord> records){EscherPreparedSource source;for(const auto &record:records){require(record.geometry!=0,PN2S_INVALID,"Escher source record has no integer owner");const auto &owner=*reinterpret_cast<const EscherIntegerShapeRecord *>(record.geometry);escherAppendSourceRecord(source,record,owner.normal.outerContours());}return source;}
EscherNfpCanonicalPair escherCanonicalNfpPair(const EscherNfpMember &first,const EscherNfpMember &second){
    require(first.contours&&second.contours,PN2S_INVALID,"Escher NFP requires normalized integer contours");
    EscherNfpCanonicalPair pair{first,second};pair.relativeReflection=first.reflected^second.reflected;
    const int32_t direct=normalized(pair.relativeReflection?narrow(uint32_t(first.angle)+uint32_t(second.angle)):narrow(uint32_t(first.angle)-uint32_t(second.angle)));
    const int32_t reverse=pair.relativeReflection?direct:normalized(narrow(uint32_t(second.angle)-uint32_t(first.angle)));
    const auto a=first.ownerIdentity?first.ownerIdentity:uintptr_t(first.contours),b=second.ownerIdentity?second.ownerIdentity:uintptr_t(second.contours);
    pair.swapped=a>b||(a==b&&direct>reverse);if(pair.swapped)std::swap(pair.first,pair.second);pair.relativeAngle=pair.swapped?reverse:direct;return pair;
}
void escherTransformNfp(ConvolutionIntegerPolygon &rings,int32_t angle,uint8_t reflected){
    if(angle)for(auto &ring:rings){if(angle==2700000)for(auto &point:ring)point={point[1],sub(0,point[0])};else if(angle==1800000)for(auto &point:ring)point={sub(0,point[0]),sub(0,point[1])};else if(angle==900000)for(auto &point:ring)point={sub(0,point[1]),point[0]};else rotateRing(ring,angle);}
    if(reflected)for(auto &ring:rings){for(auto &point:ring)point[1]=sub(0,point[1]);std::reverse(ring.begin(),ring.end());}
}
ConvolutionIntegerPolygon escherPairNfp(const EscherNfpMember &first,const EscherNfpMember &second){
    const auto pair=escherCanonicalNfpPair(first,second);if(pair.first.contours->empty()||pair.second.contours->empty())return {};
    auto negative=*pair.first.contours;escherTransformNfp(negative,pair.relativeAngle,pair.relativeReflection);negate(negative);for(auto &ring:negative)convolutionNormalizeIntegerRing(ring);convolutionRemoveEmptyIntegerRings(negative);
    auto result=convolutionIntegerMinkowski(*pair.second.contours,negative).rings;escherTransformNfp(result,pair.second.angle,pair.second.reflected);if(pair.swapped)negate(result);return result;
}
ConvolutionIntegerPolygon escherPatternSelfNfp(std::span<const EscherNfpMember> members){
    require(!members.empty(),PN2S_INVALID,"Escher self-NFP requires a nonempty prepared pattern");if(members.size()==1)return escherPairNfp(members[0],members[0]);
    const bool aligned=members[1].reflected==0&&(members[1].angle/10000)%180==0;ConvolutionIntegerMultiPolygon groups;
    for(const auto &first:members)for(const auto &second:members){const EscherIntegerPoint translation{sub(second.translation[0],first.translation[0]),sub(second.translation[1],first.translation[1])};if(aligned&&translation==EscherIntegerPoint{}&&first.translation!=EscherIntegerPoint{})continue;
        auto nfp=escherPairNfp(first,second);if(area(nfp)<add(area(*first.contours),area(*second.contours)))return {};
        if(translation!=EscherIntegerPoint{})for(auto &ring:nfp)for(auto &point:ring){point[0]=add(point[0],translation[0]);point[1]=add(point[1],translation[1]);}groups.push_back(std::move(nfp));
    }
    if(groups.empty())return {};convolutionMergeIntegerOffsets(groups);ConvolutionIntegerPolygon output;for(auto &group:groups)output.insert(output.end(),std::make_move_iterator(group.begin()),std::make_move_iterator(group.end()));return output;
}
ConvolutionIntegerPolygon escherPatternSelfNfp(std::span<const EscherPatternRecord> records){std::vector<EscherNfpMember> members;members.reserve(records.size());for(const auto &record:records)members.push_back(escherNfpMember(record));return escherPatternSelfNfp(members);}
EscherEnumerationDescriptor escherPrimitiveDescriptor(EscherEnumerationDescriptor source,std::span<const EscherSupportTable> supports,const ConvolutionIntegerPolygon &selfNfp,int32_t restriction,int64_t &minimum,bool adaptive){
    require(!source.records.empty()&&source.records.size()==supports.size(),PN2S_INVALID,"Escher primitive descriptor records/supports differ");
    const auto options=escherDefaultCandidateBuildOptions(restriction,adaptive);auto candidates=escherBuildPeriodicCandidates(supports,selfNfp,options,minimum);source.normalCandidates=std::move(candidates.ordinary);source.specialCandidates=std::move(candidates.special);source.restrictionDegrees=restriction/10000;source.orientationChoices={false,true};return source;
}
}
