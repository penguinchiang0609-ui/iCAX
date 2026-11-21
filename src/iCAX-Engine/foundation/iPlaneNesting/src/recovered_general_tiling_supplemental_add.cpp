#include "recovered_general_tiling_supplemental_add.hpp"
#include <bit>
namespace study::recovered {
namespace {
int32_t integer32(double value){return value>=-0x1p31&&value<0x1p31?int32_t(value):INT32_MIN;}
int64_t integer64(double value){return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
int32_t sub32(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
int64_t sub64(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int32_t normalized(int32_t angle){const auto value=angle%3600000;return value<0?value+3600000:value;}
bool before(int32_t a,int32_t b){if(b==0)return false;if(a==0)return true;if((a<0)!=(b<0))return a>=0;return a<b;}
int64_t rounded(double value){return integer64(value<0?value-.5:value+.5);}
}
PlacementOrientation generalTilingSupplementalOrientation(PlacementOrientation source,bool sx,bool sy){
    auto result=source;
    const auto consider=[&](PlacementOrientation candidate){if(before(candidate.angle,result.angle))result=candidate;};
    const auto reflected=[&](uint32_t axis){const auto rotation=std::bit_cast<int32_t>(uint32_t(source.angle)*uint32_t(-2)+axis)%36000000;auto candidate=composeOrientation(source,rotation,true);candidate.field4=source.field4;candidate.field8=source.field8;const auto sum=std::bit_cast<int32_t>(uint32_t(source.field4)+uint32_t(source.field8));if(sum>36001000)candidate.field8=sub32(36000500,source.field4);return candidate;};
    if(sx)consider(reflected(72000000U));
    if(sy){consider(reflected(90000000U));if(sx)consider(composeOrientation(source,18000000,false));}
    return result;
}
EscherPatternRecord generalTilingSupplementalRecord(const TilingMember &member,
    const EscherIntegerShapeRecord &owner,double scale,bool sx,bool sy){
    require(std::isfinite(scale)&&scale>0.,PN2S_INVALID,"Supplemental record requires the actual positive integer-job scale");
    const auto pose=generalTilingSupplementalOrientation(member.orientation,sx,sy);
    auto magnitude=pose.angle<0?sub32(0,pose.angle):pose.angle;
    if(magnitude>=36000000)magnitude=sub32(magnitude,36000000);
    const double scaled=(double(magnitude)/100000.)*10000.;const int32_t angle=normalized(integer32(scaled<0?scaled-.5:scaled+.5));
    auto record=escherOwnerPatternRecord(owner,uint32_t(pose.angle<0),angle);
    const auto bounds=escherBoundsForAngle(owner.normal.support(),angle);
    record.x=sub64(rounded(scale*member.box.x0),bounds[0]);record.y=sub64(rounded(scale*member.box.y0),bounds[1]);return record;
}
std::optional<EscherPreparedSource> generalTilingSupplementalSource(const TilingPattern &pattern,
    const EscherIntegerShapeRecord &owner,double scale,bool sx,bool sy){
    if(pattern.members.size()<2)return {};
    auto first=generalTilingSupplementalRecord(pattern.members.front(),owner,scale,sx,sy);
    if(uint8_t(first.reflected))return {};
    const auto translation=EscherIntegerPoint{first.x,first.y};const int32_t rotation=normalized(sub32(0,first.angle));first.x=first.y=0;
    const auto geometry=escherOwnerRecordGeometry(owner);
    first=escherRotateRecords(std::span<const EscherPatternRecord>(&first,1),rotation,std::span<const EscherRecordGeometry>(&geometry,1)).front();
    auto second=generalTilingSupplementalRecord(pattern.members.back(),owner,scale,sx,sy);
    if(uint8_t(second.reflected))return {};
    second.x=sub64(second.x,translation[0]);second.y=sub64(second.y,translation[1]);
    second=escherRotateRecords(std::span<const EscherPatternRecord>(&second,1),rotation,std::span<const EscherRecordGeometry>(&geometry,1)).front();
    auto source=escherSourceDescriptor(std::array<EscherPatternRecord,2>{first,second});source.descriptor.sameWidth=false;
    if(source.descriptor.records.size()!=2||uint8_t(source.descriptor.records.front().reflected)||uint8_t(source.descriptor.records.back().reflected))return {};
    return source;
}
bool generalTilingAppendSupplementalPattern(EscherGeometryCatalog &catalog,const TilingPattern &pattern,
    const EscherIntegerShapeRecord &owner,double scale,bool sx,bool sy){
    auto prepared=generalTilingSupplementalSource(pattern,owner,scale,sx,sy);if(!prepared)return false;auto &source=*prepared;
    const auto nfp=escherPatternSelfNfp(source.descriptor.records);
    if(escherContourArea(nfp)<12)return false;
    catalog.ordinary.push_back(escherPrimitiveDescriptor(std::move(source.descriptor),source.supports,nfp,owner.metadata4,catalog.minimumCellArea,true));return true;
}
}
