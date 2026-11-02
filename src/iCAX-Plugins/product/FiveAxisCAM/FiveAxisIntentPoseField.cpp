#include "FiveAxisIntentPoseField.h"
#include "FiveAxisIntentProjection.h"
#include "FiveAxisIntentCorrection.h"
#include <OpenCascadeResourceImport/CurveEditingService.h>
#include <GeometryData/BRepPersistence.h>
#include <Data/VariantSerializer.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace iCAX::FiveAxisCAM
{
namespace
{
using namespace iCAX::CAM::Intent;
using namespace iCAX::GeometryData;
using namespace iCAX::GeometryServices;
using Map=IntentMap;using Array=IntentArray;using Variant=iCAX::Data::Variant;
using V=std::array<double,3>;using Q=std::array<double,4>;
void Check(bool value,const std::string& message){if(!value)throw std::invalid_argument(message);}
const Variant& Required(const Map& map,const char* key){const auto found=map.find(key);Check(found!=map.end(),std::string("FIVEAXIS_POSE_FIELD_REQUIRED: ")+key);return found->second;}
Map Object(const Map& map,const char* key){const auto found=map.find(key);return found==map.end()?Map{}:found->second.To<Map>();}
const Map& MapView(const Variant& value){return std::get<Map>(value.m_Value);}
const Array& ArrayView(const Variant& value){return std::get<Array>(value.m_Value);}
const Map& ObjectView(const Map& map,const char* key){static const Map empty;const auto found=map.find(key);return found==map.end()?empty:MapView(found->second);}
std::string Text(const Map& map,const char* key,const std::string& fallback={}){const auto found=map.find(key);return found==map.end()?fallback:found->second.To<std::string>();}
double Numeric(const Variant& value)
{const auto result=std::visit([](const auto& stored)->double{using T=std::decay_t<decltype(stored)>;if constexpr(std::is_same_v<T,std::string>){std::size_t used=0;const auto number=std::stod(stored,&used);Check(used==stored.size(),"CAM_POSE_NUMBER_INVALID");return number;}else if constexpr(std::is_arithmetic_v<T>&&!std::is_same_v<T,bool>)return static_cast<double>(stored);else throw std::invalid_argument("CAM_POSE_NUMBER_INVALID");},value.m_Value);Check(std::isfinite(result),"CAM_POSE_NUMBER_INVALID");return result;}
double Number(const Map& map,const char* key,double fallback=0){const auto found=map.find(key);return found==map.end()?fallback:Numeric(found->second);}
bool Flag(const Map& map,const char* key){const auto found=map.find(key);return found!=map.end()&&found->second.To<bool>();}
ParameterRange Range(const SEditableSegment& segment){return std::visit([](const auto& curve){if constexpr(requires{curve.Source.Range;})return curve.Source.Range;else return curve.Range;},segment.Curve);}
double Start(ParameterRange range){return range.Reversed?range.Last:range.First;}
double End(ParameterRange range){return range.Reversed?range.First:range.Last;}
Map RangeData(ParameterRange range){return {{"first",range.First},{"last",range.Last},{"reversed",range.Reversed}};}
ParameterRange ReadRange(const Map& map){ParameterRange range{Number(map,"first"),Number(map,"last"),Flag(map,"reversed")};Check(range.Last>range.First,"CAM_POSE_KEYPOINT_OUTSIDE_SOURCE");return range;}
double Dot(const V& a,const V& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
V Cross(const V& a,const V& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
V Unit(V value){const auto length=std::hypot(value[0],value[1],value[2]);Check(std::isfinite(length)&&length>=1e-12,"CAM_POSE_DIRECTION_INVALID");for(auto& coordinate:value)coordinate/=length;return value;}
V Vector(Direction3 value){return {value.X,value.Y,value.Z};}
Direction3 Direction(V value){return {value[0],value[1],value[2]};}
V ReadDirection(const Variant& value){const auto& data=ArrayView(value);Check(data.size()==3,"CAM_POSE_DIRECTION_INVALID");return Unit({Numeric(data[0]),Numeric(data[1]),Numeric(data[2])});}
Array Data(const Q& value){return {value[0],value[1],value[2],value[3]};}
Q ReadRotation(const Variant& value){const auto& data=ArrayView(value);Check(data.size()==4,"CAM_POSE_ROTATION_INVALID");Q result{};double norm=0;for(int i=0;i<4;++i){result[i]=Numeric(data[i]);norm+=result[i]*result[i];}Check(std::abs(norm-1)<1e-9,"CAM_POSE_ROTATION_INVALID");return result;}
Q Rotation(const V& from,const V& to)
{
    const auto dot=std::clamp(Dot(from,to),-1.,1.);if(dot<-.999999){const auto cross=Cross(from,to);const auto length=std::hypot(cross[0],cross[1],cross[2]);
        if(length>1e-12){const auto axis=Unit(cross);const auto half=.5*std::atan2(length,dot),s=std::sin(half);return {axis[0]*s,axis[1]*s,axis[2]*s,std::cos(half)};}
        int index=0;for(int i=1;i<3;++i)if(std::abs(from[i])<std::abs(from[index]))index=i;V basis{0,0,0};basis[index]=1;auto axis=Unit(Cross(from,basis));
        for(auto coordinate:axis)if(std::abs(coordinate)>1e-12){if(coordinate<0)for(auto& n:axis)n=-n;break;}return {axis[0],axis[1],axis[2],0};}
    const auto cross=Cross(from,to);Q result{cross[0],cross[1],cross[2],1+dot};double norm=0;for(auto coordinate:result)norm+=coordinate*coordinate;norm=std::sqrt(norm);for(auto& coordinate:result)coordinate/=norm;return result;
}
Q Blend(Q a,Q b,double fraction)
{
    double dot=0;for(int i=0;i<4;++i)dot+=a[i]*b[i];if(dot<0){for(auto& coordinate:b)coordinate=-coordinate;dot=-dot;}double first=1-fraction,last=fraction;
    if(dot<.999999){const auto angle=std::acos(std::clamp(dot,-1.,1.)),sine=std::sin(angle);first=std::sin((1-fraction)*angle)/sine;last=std::sin(fraction*angle)/sine;}
    Q result{};double norm=0;for(int i=0;i<4;++i){result[i]=a[i]*first+b[i]*last;norm+=result[i]*result[i];}norm=std::sqrt(norm);for(auto& coordinate:result)coordinate/=norm;return result;
}
V Rotate(const Q& rotation,const V& vector){const V axis{rotation[0],rotation[1],rotation[2]};const auto a=Cross(axis,vector),b=Cross(axis,a);V result;for(int i=0;i<3;++i)result[i]=vector[i]+2*(rotation[3]*a[i]+b[i]);return Unit(result);}
std::string Key(const std::string& bytes){iCAX::Data::uuid_name_generator generator(*IntentId::from_string("521a410c-035b-5b8f-a09e-08b8d608a7e2"));return iCAX::Data::to_string(generator(bytes));}
std::string PolicyKey(const Map& policy){return Key(iCAX::Data::VariantSerializer::Serialize(policy));}
std::string CurveKey(const Curve3& curve){Persistence::Writer writer;writer.Write(curve);return Key(std::string(writer.Bytes.begin(),writer.Bytes.end()));}
std::optional<V> Constructed(const SToolpathValue& value,const SEditableSegment& segment,const char* channel="constructedDirections")
{
    if(segment.ConstructionRole!="LeadIn"&&segment.ConstructionRole!="LeadOut"&&segment.ConstructionRole!="Lead")return {};
    const auto pose=value.Channels.find("pose");Check(pose!=value.Channels.end(),"FIVEAXIS_INTENT_POSE_CHANNEL_REQUIRED");const auto& directions=ObjectView(MapView(pose->second),channel);std::vector<V> matches;
    const auto add=[&](const std::string& id){const auto found=directions.find(id);if(found!=directions.end())matches.push_back(ReadDirection(found->second));};add(segment.Id);for(const auto& source:segment.Sources)add(source.SourceSegmentId);
    Check(!matches.empty(),"FIVEAXIS_INTENT_CONSTRUCTED_POSE_REQUIRED");for(const auto& match:matches)Check(Dot(match,matches[0])>=1-1e-10,"FIVEAXIS_INTENT_CONSTRUCTED_POSE_AMBIGUOUS");return matches[0];
}
template<class T> const T& Record(const std::vector<T>& values,std::uint64_t id){for(const auto& value:values)if(value.Id==id)return value;throw std::invalid_argument("CAM_CUT_SOURCE_MAP_MISMATCH");}
Map Policy(const SToolpathValue& value,const std::string& set)
{
    if(set=="active"){const auto found=value.Channels.find("pose");if(found!=value.Channels.end())return Object(found->second.To<Map>(),"policy");return ReadFiveAxisIntentMetadata(value).Pose;}
    const auto metadata=ReadFiveAxisIntentMetadata(value);const auto find=[&](auto&& self,const Variant& node)->Map{if(node.Is<Map>()){const auto map=node.To<Map>();if(Text(map,"controlsSet")==set)return map;for(const auto& [_,item]:map)if(item.Is<Map>()||item.Is<Array>()){auto result=self(self,item);if(!result.empty())return result;}}else if(node.Is<Array>())for(const auto& item:node.To<Array>()){auto result=self(self,item);if(!result.empty())return result;}return {};};auto policy=find(find,metadata.Feature);if(policy.empty())policy=find(find,metadata.SourceData);Check(!policy.empty()&&policy.contains("basis"),"FIVEAXIS_INTENT_CONTOUR_POSE_REQUIRED");return Object(policy,"basis");
}
bool Fixed(const Map& policy){return Text(policy,"mode")=="FixedDirection"||(Text(policy,"mode")=="Continuous"&&Text(policy,"initialization")=="FixedDirection");}
V Base(const Map& policy,const CFiveAxisIntentOperationContext& context,const BoundaryFrame& frame)
{
    if(Fixed(policy))return ReadDirection(Required(policy,"direction"));
    Check(Text(policy,"initialization")=="SurfaceNormal"&&Number(policy,"leadAngleRad")==0&&Number(policy,"sideAngleRad")==0,"CAM_GENERATE_POSE_UNSUPPORTED");
    const auto sign=context.OutwardSign()*(Flag(policy,"axisReversed")?-1:1);return Unit({frame.Normal.X*sign,frame.Normal.Y*sign,frame.Normal.Z*sign});
}
struct Bound{std::string Key;SFiveAxisIntentCadSpan Binding;double Parameter=0;};
bool Same(const SFiveAxisIntentCadSpan& a,const SFiveAxisIntentCadSpan& b){return a.FaceId==b.FaceId&&a.WireId==b.WireId&&a.EdgeId==b.EdgeId&&a.CoedgeIndex==b.CoedgeIndex;}
std::optional<Bound> Resolve(const CFiveAxisIntentOperationContext& context,const SEditableSegment& segment,double parameter)
{
    std::vector<Bound> matches;const auto add=[&](const std::string& key,double source){const auto found=context.SourceBindings().find(key);if(found==context.SourceBindings().end())return;
        if(std::none_of(matches.begin(),matches.end(),[&](const auto& old){return Same(old.Binding,found->second)&&std::abs(old.Parameter-source)<1e-11;}))matches.push_back({key,found->second,source});};
    if(segment.ConstructionRole.empty())add(segment.Id,parameter);
    for(const auto& source:segment.Sources)if(parameter>=source.OutputRange.First-1e-12&&parameter<=source.OutputRange.Last+1e-12&&context.SourceBindings().contains(source.SourceSegmentId))
    {Check(source.Precision=="Exact","FIVEAXIS_INTENT_CAD_MAPPING_PRECISION_UNSUPPORTED");const auto fraction=(parameter-Start(source.OutputRange))/(End(source.OutputRange)-Start(source.OutputRange));add(source.SourceSegmentId,Start(source.SourceRange)+fraction*(End(source.SourceRange)-Start(source.SourceRange)));}
    Check(matches.size()<=1,"FIVEAXIS_INTENT_CAD_BINDING_AMBIGUOUS_OR_MISSING");if(matches.empty())return {};return matches.front();
}
BoundaryFrame Frame(const CFiveAxisIntentOperationContext& context,const std::string& key,double parameter)
{
    const auto found=context.SourceBindings().find(key);Check(found!=context.SourceBindings().end(),"FIVEAXIS_INTENT_POSE_SUPPORT_MISSING");const auto& binding=found->second;BoundaryLeadInput query;
    query.FaceId=binding.FaceId;query.WireId=binding.WireId;query.EdgeId=binding.EdgeId;query.CoedgeIndex=binding.CoedgeIndex;query.NativeParameter=parameter;return context.Boundary().Frame(context.Model(),query);
}
Map BoundData(const Bound& value){return {{"key",value.Key},{"parameter",value.Parameter}};}
Map Basis(const CFiveAxisIntentOperationContext& context,const SToolpathValue& value,std::size_t index,const Map& policy)
{
    const auto& segment=value.Geometry.Segments[index];const auto range=Range(segment);Map result{{"range",RangeData(range)}};Array correction;for(const auto item:FiveAxisSegmentCorrection(value,index))correction.emplace_back(item);result["correction"]=correction;
    const auto a=Resolve(context,segment,range.First),b=Resolve(context,segment,range.Last);
    if(a&&b){Check(Same(a->Binding,b->Binding),"FIVEAXIS_INTENT_POSE_SUPPORT_CHANGED_WITHIN_SEGMENT");result["mode"]=std::string("Boundary");result["key"]=a->Key;result["first"]=a->Parameter;result["last"]=b->Parameter;return result;}
    if(!a&&!b&&segment.ConstructionRole.empty()&&Fixed(policy)){result["mode"]=std::string("Fixed");return result;}Check(!a&&!b&&!segment.ConstructionRole.empty(),"FIVEAXIS_INTENT_POSE_SUPPORT_MISSING");Map before,after;
    for(std::size_t step=1;step<=value.Geometry.Segments.size();++step)
    {
        if(before.empty()&&(index>=step||value.Geometry.Closed)){const auto at=(index+value.Geometry.Segments.size()-step)%value.Geometry.Segments.size();if(const auto bound=Resolve(context,value.Geometry.Segments[at],End(Range(value.Geometry.Segments[at]))))before=BoundData(*bound);}
        if(after.empty()&&(index+step<value.Geometry.Segments.size()||value.Geometry.Closed)){const auto at=(index+step)%value.Geometry.Segments.size();if(const auto bound=Resolve(context,value.Geometry.Segments[at],Start(Range(value.Geometry.Segments[at]))))after=BoundData(*bound);}
        if(!before.empty()&&!after.empty())break;
    }
    if(before.empty()&&after.empty()&&Fixed(policy)){result["mode"]=std::string("Fixed");return result;}Check(!before.empty()||!after.empty(),"FIVEAXIS_INTENT_POSE_SUPPORT_MISSING");result["mode"]=std::string("Neighbors");result["before"]=before;result["after"]=after;return result;
}
BoundaryFrame UncorrectedBasisFrame(const CFiveAxisIntentOperationContext& context,const Map& basis,double parameter)
{
    if(Text(basis,"mode")=="Fixed")return {};
    const auto range=ReadRange(ObjectView(basis,"range"));const auto fraction=(parameter-range.First)/(range.Last-range.First);
    if(Text(basis,"mode")=="Boundary")return Frame(context,Text(basis,"key"),Number(basis,"first")+fraction*(Number(basis,"last")-Number(basis,"first")));
    const auto& before=ObjectView(basis,"before");const auto& after=ObjectView(basis,"after");const auto& first=before.empty()?after:before;const auto& last=after.empty()?before:after;
    const auto a=Frame(context,Text(first,"key"),Number(first,"parameter")),b=Frame(context,Text(last,"key"),Number(last,"parameter"));
    const auto progress=range.Reversed?1-fraction:fraction;const auto normal=Unit({a.Normal.X+(b.Normal.X-a.Normal.X)*progress,a.Normal.Y+(b.Normal.Y-a.Normal.Y)*progress,a.Normal.Z+(b.Normal.Z-a.Normal.Z)*progress});auto result=a;result.Normal=Direction(normal);return result;
}
BoundaryFrame BasisFrame(const CFiveAxisIntentOperationContext& context,const Map& basis,double parameter)
{
    auto frame=UncorrectedBasisFrame(context,basis,parameter);if(!basis.contains("correction"))return frame;const auto matrix=basis.at("correction").To<Array>();Check(matrix.size()==16,"CAM_PATH_TRANSFORM_INVALID");
    const auto point=[&](auto& p,bool vector){const auto x=p.X,y=p.Y,z=p.Z;const auto scale=vector?std::hypot(Numeric(matrix[0]),Numeric(matrix[4]),Numeric(matrix[8])):1.;p.X=(Numeric(matrix[0])*x+Numeric(matrix[1])*y+Numeric(matrix[2])*z+(vector?0:Numeric(matrix[3])))/scale;p.Y=(Numeric(matrix[4])*x+Numeric(matrix[5])*y+Numeric(matrix[6])*z+(vector?0:Numeric(matrix[7])))/scale;p.Z=(Numeric(matrix[8])*x+Numeric(matrix[9])*y+Numeric(matrix[10])*z+(vector?0:Numeric(matrix[11])))/scale;};point(frame.Point,false);point(frame.Tangent,true);point(frame.Normal,true);return frame;
}
struct Location{std::size_t Segment=0;double Parameter=0;};
std::vector<Location> Locations(const SToolpathValue& value,const SPathAnchor& anchor)
{
    std::vector<Location> result;for(std::size_t i=0;i<value.Geometry.Segments.size();++i){const auto& segment=value.Geometry.Segments[i];const auto range=Range(segment);
        const auto add=[&](double parameter){if(parameter<range.First-1e-12||parameter>range.Last+1e-12)return;if(std::none_of(result.begin(),result.end(),[&](const auto& old){return old.Segment==i&&std::abs(old.Parameter-parameter)<1e-11;}))result.push_back({i,parameter});};
        if(segment.Id==anchor.SegmentId)add(anchor.Parameter);else for(const auto& source:segment.Sources)if(source.Source==anchor.Port&&source.SourceSegmentId==anchor.SegmentId&&source.Occurrence==anchor.Occurrence&&anchor.Parameter>=source.SourceRange.First-1e-12&&anchor.Parameter<=source.SourceRange.Last+1e-12)
        {Check(source.Precision=="Exact","FIVEAXIS_INTENT_POSE_MAPPING_PRECISION_UNSUPPORTED");const auto fraction=(anchor.Parameter-Start(source.SourceRange))/(End(source.SourceRange)-Start(source.SourceRange));add(Start(source.OutputRange)+fraction*(End(source.OutputRange)-Start(source.OutputRange)));}}
    return result;
}
double OriginalParameter(const SEditableSegment& segment,double parameter,const Map& descriptor)
{
    if(!descriptor.contains("origin")){Check(segment.Id==Text(descriptor,"segmentId"),"FIVEAXIS_INTENT_POSE_FIELD_NEEDS_REBIND");return parameter;}
    const auto origin=AnchorFromData(ObjectView(descriptor,"origin"));std::vector<double> values;
    const auto add=[&](double value){if(std::none_of(values.begin(),values.end(),[&](double old){return std::abs(old-value)<1e-11;}))values.push_back(value);};
    if(segment.Id==origin.SegmentId)add(parameter);
    for(const auto& source:segment.Sources)if(source.Source==origin.Port&&source.SourceSegmentId==origin.SegmentId&&parameter>=source.OutputRange.First-1e-12&&parameter<=source.OutputRange.Last+1e-12)
    {Check(source.Precision=="Exact","FIVEAXIS_INTENT_POSE_MAPPING_PRECISION_UNSUPPORTED");const auto fraction=(parameter-Start(source.OutputRange))/(End(source.OutputRange)-Start(source.OutputRange));add(Start(source.SourceRange)+fraction*(End(source.SourceRange)-Start(source.SourceRange)));}
    Check(values.size()==1,"FIVEAXIS_INTENT_POSE_FIELD_NEEDS_REBIND");return values.front();
}
Map MeasureReference(const CFiveAxisIntentOperationContext& context,const SToolpathValue& value,std::size_t index)
{
    const auto& segment=value.Geometry.Segments[index];const auto& curve=std::get<CurveSegment3>(segment.Curve);const auto a=Resolve(context,segment,curve.Range.First),b=Resolve(context,segment,curve.Range.Last);
    if(a&&b&&Same(a->Binding,b->Binding)){const auto m=FiveAxisSegmentCorrection(value,index);return {{"mode",std::string("CAD")},{"key",a->Key},{"first",a->Parameter},{"last",b->Parameter},{"scale",std::hypot(m[0],m[4],m[8])}};}
    return {{"mode",std::string("Local")},{"curveFingerprint",CurveKey(curve.Curve)}};
}
std::pair<double,double> Coordinates(const CFiveAxisIntentOperationContext& context,const SEditableSegment& current,double parameter,const Map& descriptor)
{
    const auto range=ReadRange(ObjectView(descriptor,"domain"));const auto first=Start(range),last=End(range);Check(parameter>=range.First-1e-12&&parameter<=range.Last+1e-12,"CAM_POSE_KEYPOINT_OUTSIDE_SOURCE");
    std::pair<double,double> result{Number(descriptor,"arcOffset"),Number(descriptor,"turnOffset")};
    if(std::abs(parameter-first)<1e-12*(1+range.Last-range.First))return result;
    if(std::abs(parameter-last)<1e-12*(1+range.Last-range.First))return {result.first+Number(descriptor,"length"),result.second+Number(descriptor,"turn")};
    const auto editing=context.Editing();Check(editing!=nullptr,"FIVEAXIS_INTENT_POSE_MEASURE_SERVICE_REQUIRED");const auto& reference=ObjectView(descriptor,"measure");CurveSegment3 partial;
    if(Text(reference,"mode")=="CAD")
    {
        const auto binding=context.SourceBindings().find(Text(reference,"key"));Check(binding!=context.SourceBindings().end(),"FIVEAXIS_INTENT_POSE_SUPPORT_MISSING");const auto& edge=Record(context.Model().Edges,binding->second.EdgeId);partial.Curve=Record(context.Model().Curves3,edge.Curve3Id).Geometry;
        const auto source=[&](double u){return Number(reference,"first")+(u-range.First)/(range.Last-range.First)*(Number(reference,"last")-Number(reference,"first"));};const auto a=source(first),b=source(parameter);partial.Range={std::min(a,b),std::max(a,b),a>b};
    }
    else
    {
        const Curve3* curve=nullptr;if(const auto* native=std::get_if<CurveSegment3>(&current.Curve))curve=&native->Curve;else if(const auto* offset=std::get_if<DirectionalCurveOffset3>(&current.Curve))curve=&offset->Source.Curve;
        Check(curve&&CurveKey(*curve)==Text(reference,"curveFingerprint"),"FIVEAXIS_INTENT_POSE_MEASURE_SOURCE_CHANGED");partial={*curve,{std::min(first,parameter),std::max(first,parameter),range.Reversed}};
    }
    const auto measured=editing->Measure(partial);result.first+=measured.LengthMm*(Text(reference,"mode")=="CAD"?Number(reference,"scale",1):1);result.second+=measured.TurnRad;return result;
}
struct Knot{double Arc=0,Turn=0;Q Rotation{0,0,0,1};std::string Metric;bool Smooth=false;std::string Author;Map Fallback;};
Map KnotData(const Knot& value){Map result{{"arc",value.Arc},{"turn",value.Turn},{"rotation",Data(value.Rotation)},{"metric",value.Metric},{"smooth",value.Smooth},{"author",value.Author}};if(!value.Fallback.empty())result["fallback"]=value.Fallback;return result;}
V EvaluateDescriptor(const CFiveAxisIntentOperationContext& context,const Map& policy,const SEditableSegment& segment,double parameter,const Map& descriptor,unsigned depth=0)
{
    Check(depth<64,"FIVEAXIS_INTENT_POSE_LAYER_LIMIT");
    if(descriptor.contains("polarity")){const auto sign=Number(descriptor,"polarity");Check(sign==1||sign==-1,"FIVEAXIS_INTENT_POSE_POLARITY_INVALID");auto result=EvaluateDescriptor(context,ObjectView(descriptor,"basePolicy"),segment,parameter,ObjectView(descriptor,"baseExpression"),depth+1);for(auto& coordinate:result)coordinate*=sign;return result;}
    const auto original=OriginalParameter(segment,parameter,descriptor);
    if(Flag(descriptor,"baseOnly"))return Base(policy,context,BasisFrame(context,ObjectView(descriptor,"basis"),original));
    const auto coordinate=Coordinates(context,segment,original,descriptor);const auto& a=ObjectView(descriptor,"firstKnot");const auto& b=ObjectView(descriptor,"lastKnot");
    double fraction=Number(b,"arc")-Number(a,"arc")>1e-14?(coordinate.first-Number(a,"arc"))/(Number(b,"arc")-Number(a,"arc")):0;
    if(Text(descriptor,"metric")=="DirectionChange"&&Number(b,"turn")-Number(a,"turn")>1e-10)fraction=(coordinate.second-Number(a,"turn"))/(Number(b,"turn")-Number(a,"turn"));
    fraction=std::clamp(fraction,0.,1.);if(Flag(descriptor,"smooth"))fraction=fraction*fraction*(3-2*fraction);
    const auto base=descriptor.contains("baseExpression")?EvaluateDescriptor(context,ObjectView(descriptor,"basePolicy"),segment,parameter,ObjectView(descriptor,"baseExpression"),depth+1):Base(policy,context,BasisFrame(context,ObjectView(descriptor,"basis"),original));
    return Rotate(Blend(ReadRotation(Required(a,"rotation")),ReadRotation(Required(b,"rotation")),fraction),base);
}
}

struct CFiveAxisIntentPoseField::Impl
{
    const CFiveAxisIntentOperationContext& Context;const SToolpathValue Value;std::string Set;Map Pose,Channel;
    std::vector<Map> Bases;
    struct ExpressionSpan{double First,Last;const Map* Descriptor;};
    std::vector<std::vector<ExpressionSpan>> ExpressionIndex;
    std::vector<Map> DefaultExpressions;
    bool HasEntries=false;
    Impl(const Impl&)=delete;Impl& operator=(const Impl&)=delete;
    Impl(const CFiveAxisIntentOperationContext& context,const SToolpathValue& value,std::string set,bool rebuild=false,bool delta=false,const Array* overrideControls=nullptr):Context(context),Value(value),Set(std::move(set))
    {
        const auto field=value.Channels.find("poseField");if(Set.empty())Set=field==value.Channels.end()?"active":Text(MapView(field->second),"set","active");Pose=Policy(value,Set);
        Check(Text(Pose,"mode")=="Continuous"||Text(Pose,"mode")=="FixedDirection","CAM_POSE_MODE_INVALID");
        if(!rebuild&&field!=value.Channels.end())
        {Channel=field->second.To<Map>();Check(Text(Channel,"schema")=="fiveaxis.pose-field/1"&&Text(Channel,"attachment")=="Intervals"&&Text(Channel,"set","active")==Set,"FIVEAXIS_INTENT_POSE_FIELD_SCHEMA_UNSUPPORTED");for(const auto& item:ArrayView(Required(Channel,"entries")))Check(MapView(item).contains("range"),"FIVEAXIS_INTENT_POSE_FIELD_REQUIRES_COMMON_RANGES");Check(Text(Channel,"policyFingerprint")==PolicyKey(Pose)&&Text(Channel,"contextFingerprint")==context.Fingerprint(),"FIVEAXIS_INTENT_POSE_FIELD_STALE");}
        else {std::unique_ptr<const Impl> previous;if(delta)previous=std::make_unique<Impl>(context,value,Set);Build(previous.get(),overrideControls);}
        const bool compiled=!ArrayView(Required(Channel,"entries")).empty();for(std::size_t index=0;index<Value.Geometry.Segments.size();++index)Bases.push_back(compiled||Constructed(Value,Value.Geometry.Segments[index])?Map{}:Basis(Context,Value,index,Pose));
        IndexExpressions();
    }
    void Build(const Impl* previous=nullptr,const Array* overrideControls=nullptr)
    {
        Array controls;const auto controlChannel=Value.Channels.find("poseControls");if(controlChannel!=Value.Channels.end())
        {const auto data=controlChannel->second.To<Map>();Check(Text(data,"schema")=="fiveaxis.pose-controls/1"&&Text(data,"attachment")=="Points","FIVEAXIS_INTENT_POSE_CONTROLS_SCHEMA_UNSUPPORTED");for(const auto& item:Required(data,"entries").To<Array>()){const auto point=item.To<Map>();if(Text(point,"set","active")==Set)controls.push_back(item);}}
        if(overrideControls)controls=*overrideControls;
        Channel={{"schema",std::string("fiveaxis.pose-field/1")},{"schemaVersion",1u},{"attachment",std::string("Intervals")},{"set",Set},{"policyFingerprint",PolicyKey(Pose)},{"contextFingerprint",Context.Fingerprint()},{"authored",true},{"entries",Array{}}};
        if(previous)Channel["layer"]=std::string("RotationDelta");
        if(std::none_of(controls.begin(),controls.end(),[](const auto& item){return Text(item.To<Map>(),"kind","Control")!="Reference";})){if(previous)Channel=previous->Channel;else {Channel["authored"]=false;Array entries;for(std::size_t i=0;i<Value.Geometry.Segments.size();++i){const auto& segment=Value.Geometry.Segments[i];if(Constructed(Value,segment))continue;const auto range=Range(segment);entries.emplace_back(Map{{"segmentId",segment.Id},{"first",Start(range)},{"last",End(range)},{"value",Map{{"baseOnly",true},{"domain",RangeData(range)},{"basis",Basis(Context,Value,i,Pose)},{"segmentId",segment.Id}}}});}Channel["entries"]=std::move(entries);}return;}
        // Explicit keypoint corrections also apply to a fixed-direction base.
        // The feature type and its nominal geometry remain unchanged.
        const auto editing=Context.Editing();Check(editing!=nullptr,"FIVEAXIS_INTENT_POSE_MEASURE_SERVICE_REQUIRED");
        CompositeCurve3 geometry;geometry.Closed=Value.Geometry.Closed;std::vector<iCAX::Geometry::SCurveMeasure> measures;std::vector<double> arcs,turns;std::vector<Map> bases,measureRefs;double arc=0,turn=0;
        for(std::size_t index=0;index<Value.Geometry.Segments.size();++index)
        {const auto& segment=Value.Geometry.Segments[index];Check(segment.Parameterization=="CadNative"&&std::holds_alternative<CurveSegment3>(segment.Curve),"FIVEAXIS_INTENT_POSE_FIELD_SOURCE_UNSUPPORTED");const auto curve=std::get<CurveSegment3>(segment.Curve);const auto measured=editing->Measure(curve);
         if(index)turn+=std::acos(std::clamp(Dot(measures.back().LastTangent,measured.FirstTangent),-1.,1.));arcs.push_back(arc);turns.push_back(turn);arc+=measured.LengthMm;turn+=measured.TurnRad;measures.push_back(measured);geometry.Segments.push_back(curve);bases.push_back(Basis(Context,Value,index,Pose));measureRefs.push_back(MeasureReference(Context,Value,index));}
        const auto coordinate=[&](Location at)
        {const auto& segment=geometry.Segments.at(at.Segment);const auto r=segment.Range;Check(at.Parameter>=r.First-1e-12&&at.Parameter<=r.Last+1e-12,"CAM_POSE_KEYPOINT_OUTSIDE_SOURCE");std::pair<double,double> result{arcs[at.Segment],turns[at.Segment]};const auto first=Start(r),last=End(r);
         if(std::abs(at.Parameter-first)<1e-12*(1+r.Last-r.First))return result;if(std::abs(at.Parameter-last)<1e-12*(1+r.Last-r.First))return std::pair<double,double>{result.first+measures[at.Segment].LengthMm,result.second+measures[at.Segment].TurnRad};auto partial=segment;partial.Range.First=std::min(first,at.Parameter);partial.Range.Last=std::max(first,at.Parameter);const auto measured=editing->Measure(partial);return std::pair<double,double>{result.first+measured.LengthMm,result.second+measured.TurnRad};};
        const auto locate=[&](const Map& point)
        {const auto anchor=AnchorFromData(Object(point,"anchor"));auto matches=Locations(Value,anchor);Check(!matches.empty(),"CAM_POSE_KEYPOINT_OUTSIDE_SOURCE");if(Text(point,"kind","Control")!="Reference"){const auto& role=Value.Geometry.Segments[matches.front().Segment].ConstructionRole;Check(role!="LeadIn"&&role!="LeadOut"&&role!="Lead","FIVEAXIS_INTENT_LEAD_POSE_IS_ENDPOINT_DERIVED");}if(matches.size()>1){const auto first=coordinate(matches.front());for(const auto& match:matches){const auto next=coordinate(match);Check(std::abs(next.first-first.first)<1e-8||(Value.Geometry.Closed&&std::abs(std::abs(next.first-first.first)-arc)<1e-8),"FIVEAXIS_INTENT_POSE_ANCHOR_AMBIGUOUS");}}return matches.front();};
        std::vector<Location> stations;CompositeCurve3 sourceGeometry;sourceGeometry.Closed=Value.Geometry.Closed;std::vector<std::size_t> sourceIndices;double sourceLength=0;
        for(std::size_t i=0;i<geometry.Segments.size();++i)if(Value.Geometry.Segments[i].ConstructionRole!="CadJunction"){sourceGeometry.Segments.push_back(geometry.Segments[i]);sourceIndices.push_back(i);sourceLength+=measures[i].LengthMm;}
        if(previous&&Flag(previous->Channel,"authored")){for(const auto& item:Required(previous->Channel,"entries").To<Array>()){const auto entry=item.To<Map>();if(entry.contains("range")){const auto selected=RangeFromData(Object(entry,"range"));for(const auto& anchor:{selected.Start,selected.End})for(const auto& at:Locations(Value,anchor))stations.push_back(at);}else {const auto id=Text(entry,"segmentId");for(std::size_t i=0;i<Value.Geometry.Segments.size();++i)if(Value.Geometry.Segments[i].Id==id){stations.push_back({i,Number(entry,"first")});stations.push_back({i,Number(entry,"last")});}}}}
        else {const auto samples=editing->Sample(sourceGeometry,std::max(.01,sourceLength/3000.),std::max(.05,sourceLength/36.));double since=std::numeric_limits<double>::infinity();std::array<double,3> samplePrevious{};bool first=true;
        for(std::size_t i=0;i<samples.Points.size();++i){const auto& point=samples.Points[i];if(!first){double distance=0;for(int j=0;j<3;++j)distance+=std::pow(point.Point[j]-samplePrevious[j],2);since+=std::sqrt(distance);}samplePrevious=point.Point;first=false;const bool begin=i==0||samples.Points[i-1].SegmentIndex!=point.SegmentIndex,end=i+1==samples.Points.size()||samples.Points[i+1].SegmentIndex!=point.SegmentIndex;if(begin||end||since>=sourceLength/36.){stations.push_back({sourceIndices.at(point.SegmentIndex),point.NativeParameter});since=0;}}}
        Check(stations.size()<=4096,"CAM_POSE_DISPLAY_SAMPLE_BUDGET");const auto metric=Text(Pose,"transitionMetric","ArcLength");Check(metric=="ArcLength"||metric=="DirectionChange","CAM_POSE_TRANSITION_INVALID");std::vector<Knot> knots{{0,0,{0,0,0,1},metric},{arc,turn,{0,0,0,1},metric}};
        const auto baseAt=[&](Location at){return previous?previous->At(at.Segment,at.Parameter):Base(Pose,Context,BasisFrame(Context,bases[at.Segment],at.Parameter));};
        const auto put=[&](Location at,const Q& rotation,const std::string& transition,bool smooth,const std::optional<V>& desired=std::nullopt,const std::string& author=std::string{})
        {const auto position=coordinate(at);Knot knot{position.first,position.second,rotation,transition,smooth,author};const auto found=std::find_if(knots.begin(),knots.end(),[&](const auto& value){return std::abs(value.Arc-knot.Arc)<1e-8;});if(found==knots.end())knots.push_back(knot);else {if(!author.empty())knot.Fallback=found->Author==author?found->Fallback:KnotData(*found);*found=knot;}
         if(desired&&Value.Geometry.Closed&&(position.first<1e-8||arc-position.first<1e-8)){const auto& firstRange=geometry.Segments.front().Range;const auto& lastRange=geometry.Segments.back().Range;for(std::size_t endpoint=0;endpoint<2;++endpoint){const auto previousKnot=knots[endpoint];const auto location=endpoint==0?Location{0,Start(firstRange)}:Location{geometry.Segments.size()-1,End(lastRange)};Knot closed{endpoint==0?0:arc,endpoint==0?0:turn,Rotation(baseAt(location),*desired),transition,smooth,author};if(!author.empty())closed.Fallback=previousKnot.Author==author?previousKnot.Fallback:KnotData(previousKnot);knots[endpoint]=std::move(closed);}}stations.push_back(at);};
        for(const auto& at:stations){const auto position=coordinate(at);if(std::none_of(knots.begin(),knots.end(),[&](const auto& knot){return std::abs(knot.Arc-position.first)<1e-8;}))knots.push_back({position.first,position.second,{0,0,0,1},metric});}
        for(const auto& item:controls){const auto point=item.To<Map>();if(Text(point,"kind","Control")=="Reference")put(locate(point),{0,0,0,1},Text(point,"transition",metric),false);}
        std::set<double> used;
        for(const auto& item:controls)
        {const auto point=item.To<Map>();if(Text(point,"kind","Control")!="Control")continue;const auto at=locate(point);const auto position=coordinate(at);for(auto old:used)Check(std::abs(old-position.first)>=1e-8,"CAM_POSE_DUPLICATE_KEYPOINT");used.insert(position.first);const auto driver=Object(point,"driver");const auto desired=ReadDirection(Required(point,"direction"));put(at,Rotation(baseAt(at),desired),Text(point,"transition",metric),Text(driver,"interpolation")=="Smooth",desired,Text(point,"id"));}
        std::set<double> affected;
        for(const auto& item:controls)
        {const auto target=item.To<Map>();if(Text(target,"kind","Control")!="InfluenceTarget")continue;const auto at=locate(target);const auto position=coordinate(at);const auto same=[&](double old){return std::abs(old-position.first)<1e-8||(Value.Geometry.Closed&&std::abs(std::abs(old-position.first)-arc)<1e-8);};for(auto old:affected)Check(!same(old),"CAM_POSE_INFLUENCE_OVERLAP");affected.insert(position.first);
         const auto driver=Object(target,"driver");Check(!driver.empty(),"FIVEAXIS_INTENT_POSE_DRIVER_REQUIRED");for(const auto& other:controls){const auto point=other.To<Map>();if(Text(point,"kind","Control")!="Control")continue;if(Object(point,"driver").empty()&&Text(point,"id")!=Text(driver,"id"))continue;Check(!same(coordinate(locate(point)).first),"CAM_POSE_INFLUENCE_OWNER_SELECTED");}
         const auto weight=Number(target,"weight");Check(weight>=0&&weight<=1,"CAM_POSE_INFLUENCE_WEIGHT_INVALID");const auto delta=Rotation(ReadDirection(Required(driver,"referenceDirection")),ReadDirection(Required(driver,"direction")));const auto desired=Rotate(Blend({0,0,0,1},delta,weight),ReadDirection(Required(target,"referenceDirection")));put(at,Rotation(baseAt(at),desired),Text(driver,"transition",metric),Text(driver,"interpolation")=="Smooth",desired,Text(driver,"id"));}
        std::sort(knots.begin(),knots.end(),[](const auto& a,const auto& b){return a.Arc<b.Arc;});Array table;for(const auto& knot:knots)table.emplace_back(KnotData(knot));auto layers=previous?Object(previous->Channel,"layers"):Map{};const auto layerKey=Key(iCAX::Data::VariantSerializer::Serialize(Map{{"knots",table},{"controls",controls},{"baseFingerprint",previous?PolicyKey(previous->Channel):std::string{}}}));layers[layerKey]=Map{{"knots",table}};Channel["layers"]=layers;Array entries;
        for(std::size_t i=0;i<geometry.Segments.size();++i)
        {
            const auto range=geometry.Segments[i].Range;std::vector<double> boundaries{range.First,range.Last};for(const auto& station:stations)if(station.Segment==i&&station.Parameter>range.First+1e-12&&station.Parameter<range.Last-1e-12)boundaries.push_back(station.Parameter);std::sort(boundaries.begin(),boundaries.end());boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end());if(range.Reversed)std::reverse(boundaries.begin(),boundaries.end());
            if(previous){for(const auto& item:Required(previous->Channel,"entries").To<Array>()){const auto entry=item.To<Map>();if(entry.contains("range")){const auto selected=RangeFromData(Object(entry,"range"));for(const auto& anchor:{selected.Start,selected.End})for(const auto& at:Locations(Value,anchor))if(at.Segment==i&&at.Parameter>range.First+1e-12&&at.Parameter<range.Last-1e-12)boundaries.push_back(at.Parameter);}else if(Text(entry,"segmentId")==Value.Geometry.Segments[i].Id){boundaries.push_back(Number(entry,"first"));boundaries.push_back(Number(entry,"last"));}}std::sort(boundaries.begin(),boundaries.end());boundaries.erase(std::unique(boundaries.begin(),boundaries.end(),[](double a,double b){return std::abs(a-b)<1e-12;}),boundaries.end());if(range.Reversed)std::reverse(boundaries.begin(),boundaries.end());}
            for(std::size_t j=1;j<boundaries.size();++j){const auto middle=coordinate({i,(boundaries[j-1]+boundaries[j])/2});auto right=std::upper_bound(knots.begin(),knots.end(),middle.first,[](double position,const auto& knot){return position<knot.Arc;});const auto& a=right==knots.begin()?knots.front():*(right-1);const auto& b=right==knots.end()?knots.back():*right;
                Map descriptor{{"domain",RangeData(range)},{"arcOffset",arcs[i]},{"turnOffset",turns[i]},{"length",measures[i].LengthMm},{"turn",measures[i].TurnRad},{"basis",bases[i]},{"measure",measureRefs[i]},{"firstKnot",KnotData(a)},{"lastKnot",KnotData(b)},{"metric",a.Metric},{"smooth",a.Smooth||b.Smooth},{"segmentId",Value.Geometry.Segments[i].Id},{"layerKey",layerKey},{"intervalArcMiddle",middle.first}};
                if(previous){descriptor["baseExpression"]=previous->Expression(i,(boundaries[j-1]+boundaries[j])/2);descriptor["basePolicy"]=previous->Pose;}
                entries.emplace_back(Map{{"segmentId",Value.Geometry.Segments[i].Id},{"first",boundaries[j-1]},{"last",boundaries[j]},{"value",std::move(descriptor)}});Check(entries.size()<=100000,"CAM_POSE_MEASURE_BUDGET");}
        }
        Channel["entries"]=std::move(entries);
    }
    void IndexExpressions()
    {
        // Channel is owned by this non-copyable evaluator. Build it completely
        // before retaining references, then leave it immutable during reads.
        ExpressionIndex.resize(Value.Geometry.Segments.size());DefaultExpressions.reserve(Value.Geometry.Segments.size());
        for(std::size_t i=0;i<Value.Geometry.Segments.size();++i)DefaultExpressions.push_back({{"baseOnly",true},{"domain",RangeData(Range(Value.Geometry.Segments[i]))},{"basis",Bases[i]},{"segmentId",Value.Geometry.Segments[i].Id}});
        const auto& entries=ArrayView(Required(Channel,"entries"));HasEntries=!entries.empty();
        const auto add=[&](std::size_t index,double first,double last,const Map& descriptor){ExpressionIndex[index].push_back({std::min(first,last),std::max(first,last),&descriptor});};
        for(const auto& item:entries)
        {
            const auto& entry=MapView(item);const auto& descriptor=ObjectView(entry,"value");
            if(entry.contains("range"))
            {
                const auto selected=RangeFromData(ObjectView(entry,"range"));const auto first=Locations(Value,selected.Start),last=Locations(Value,selected.End);
                for(const auto& a:first)for(const auto& b:last)if(a.Segment==b.Segment)add(a.Segment,a.Parameter,b.Parameter,descriptor);
            }
            else for(std::size_t i=0;i<Value.Geometry.Segments.size();++i)if(Text(entry,"segmentId")==Value.Geometry.Segments[i].Id)add(i,Number(entry,"first"),Number(entry,"last"),descriptor);
        }
    }
    std::vector<const Map*> Expressions(std::size_t index,double parameter)const
    {
        std::vector<const Map*> result;
        for(const auto& span:ExpressionIndex.at(index))if(parameter>=span.First-1e-12&&parameter<=span.Last+1e-12&&std::find(result.begin(),result.end(),span.Descriptor)==result.end())result.push_back(span.Descriptor);
        return result;
    }
    const Map& Expression(std::size_t index,double parameter)const
    {
        for(const auto& span:ExpressionIndex.at(index))if(parameter>=span.First-1e-12&&parameter<=span.Last+1e-12)return *span.Descriptor;
        Check(!HasEntries,"FIVEAXIS_INTENT_POSE_FIELD_NEEDS_REBIND");return DefaultExpressions.at(index);
    }
    V ReferenceAt(std::size_t index,double parameter,bool normal)const
    {
        Check(index<Value.Geometry.Segments.size()&&std::isfinite(parameter),"CAM_POSE_KEYPOINT_OUTSIDE_SOURCE");const auto& segment=Value.Geometry.Segments[index];const auto range=Range(segment);Check(parameter>=range.First-1e-12&&parameter<=range.Last+1e-12,"CAM_POSE_KEYPOINT_OUTSIDE_SOURCE");if(segment.ConstructionRole=="LeadIn"||segment.ConstructionRole=="LeadOut"||segment.ConstructionRole=="Lead")return *Constructed(Value,segment,normal?"constructedNormals":"constructedDirections");if(!normal&&Fixed(Pose))return ReadDirection(Required(Pose,"direction"));const auto* descriptor=&Expression(index,parameter);for(unsigned depth=0;descriptor->contains("baseExpression");++depth){Check(depth<64,"FIVEAXIS_INTENT_POSE_LAYER_LIMIT");descriptor=&ObjectView(*descriptor,"baseExpression");}const auto original=OriginalParameter(segment,parameter,*descriptor);const auto& basis=ObjectView(*descriptor,"basis");if(normal){if(Text(basis,"mode")=="Fixed"){const auto bound=Resolve(Context,segment,parameter);Check(bound.has_value(),"FIVEAXIS_INTENT_POSE_SUPPORT_MISSING");const auto frame=Frame(Context,bound->Key,bound->Parameter);return Unit({frame.Normal.X*Context.OutwardSign(),frame.Normal.Y*Context.OutwardSign(),frame.Normal.Z*Context.OutwardSign()});}const auto frame=BasisFrame(Context,basis,original);return Unit({frame.Normal.X*Context.OutwardSign(),frame.Normal.Y*Context.OutwardSign(),frame.Normal.Z*Context.OutwardSign()});}return Base(Pose,Context,BasisFrame(Context,basis,original));
    }
    V At(std::size_t index,double parameter)const
    {
        Check(index<Value.Geometry.Segments.size()&&std::isfinite(parameter),"CAM_POSE_KEYPOINT_OUTSIDE_SOURCE");const auto& segment=Value.Geometry.Segments[index];const auto domain=Range(segment);Check(parameter>=domain.First-1e-12&&parameter<=domain.Last+1e-12,"CAM_POSE_KEYPOINT_OUTSIDE_SOURCE");if(const auto constructed=Constructed(Value,segment))return *constructed;std::vector<V> values;
        for(const auto* original:Expressions(index,parameter))
        {
            values.push_back(EvaluateDescriptor(Context,Pose,segment,parameter,*original));
        }
        if(values.empty()){Check(!HasEntries,"FIVEAXIS_INTENT_POSE_FIELD_NEEDS_REBIND");return Base(Pose,Context,BasisFrame(Context,Bases[index],parameter));}
        for(const auto& value:values)Check(Dot(value,values[0])>=1-1e-10,"FIVEAXIS_INTENT_POSE_FIELD_BOUNDARY_AMBIGUOUS");return values.front();
    }
};

CFiveAxisIntentPoseField::CFiveAxisIntentPoseField(const CFiveAxisIntentOperationContext& context,const SToolpathValue& value,const std::string& set):m_Impl(std::make_unique<Impl>(context,value,set)){}
CFiveAxisIntentPoseField::~CFiveAxisIntentPoseField()=default;
Direction3 CFiveAxisIntentPoseField::At(std::size_t segment,double parameter)const{return Direction(m_Impl->At(segment,parameter));}
Direction3 CFiveAxisIntentPoseField::AtWithoutControls(std::size_t segment,double parameter,const std::set<std::string>& ids)const
{
 if(ids.empty())return At(segment,parameter);const auto& field=*m_Impl;const auto layers=Object(field.Channel,"layers");
 const auto restore=[&](auto&& self,Map knot,unsigned depth)->std::optional<Map>{Check(depth<64,"FIVEAXIS_INTENT_POSE_LAYER_LIMIT");if(knot.contains("fallback")){const auto fallback=self(self,Object(knot,"fallback"),depth+1);if(fallback)knot["fallback"]=*fallback;else knot.erase("fallback");}if(!ids.contains(Text(knot,"author")))return knot;if(knot.contains("fallback"))return Object(knot,"fallback");return {};};
 const auto expression=[&](auto&& self,Map descriptor,unsigned depth)->Map{
  Check(depth<64,"FIVEAXIS_INTENT_POSE_LAYER_LIMIT");if(descriptor.contains("baseExpression"))descriptor["baseExpression"]=self(self,Object(descriptor,"baseExpression"),depth+1);
  const auto key=Text(descriptor,"layerKey");if(!key.empty()){
   const auto found=layers.find(key);Check(found!=layers.end(),"FIVEAXIS_INTENT_POSE_LAYER_TABLE_MISSING");Array knots;
   for(const auto& item:Required(found->second.To<Map>(),"knots").To<Array>())if(const auto knot=restore(restore,item.To<Map>(),0))knots.emplace_back(*knot);
   Check(knots.size()>=2,"FIVEAXIS_INTENT_POSE_LAYER_ENDPOINTS_MISSING");const auto position=Number(descriptor,"intervalArcMiddle");
   const auto right=std::upper_bound(knots.begin(),knots.end(),position,[](double x,const auto& item){return x<Number(item.To<Map>(),"arc");});
   const auto a=(right==knots.begin()?knots.front():*(right-1)).To<Map>(),b=(right==knots.end()?knots.back():*right).To<Map>();
   descriptor["firstKnot"]=a;descriptor["lastKnot"]=b;descriptor["metric"]=Text(a,"metric");descriptor["smooth"]=Flag(a,"smooth")||Flag(b,"smooth");
  }return descriptor;
 };
 return Direction(EvaluateDescriptor(field.Context,field.Pose,field.Value.Geometry.Segments.at(segment),parameter,expression(expression,field.Expression(segment,parameter),0)));
}
Direction3 CFiveAxisIntentPoseField::BaseAt(std::size_t segment,double parameter)const{return Direction(m_Impl->ReferenceAt(segment,parameter,false));}
Direction3 CFiveAxisIntentPoseField::NormalAt(std::size_t segment,double parameter)const{return Direction(m_Impl->ReferenceAt(segment,parameter,true));}
void TransformFiveAxisIntentPoseFields(const SToolpathValue& before,SToolpathValue& after,const std::array<double,16>& matrix)
{
    const auto scale=std::hypot(matrix[0],matrix[4],matrix[8]);
    const auto rotate=[&](const Array& a){Check(a.size()==3||a.size()==4,"CAM_POSE_DIRECTION_INVALID");const auto x=Numeric(a[0]),y=Numeric(a[1]),z=Numeric(a[2]);Array result{(matrix[0]*x+matrix[1]*y+matrix[2]*z)/scale,(matrix[4]*x+matrix[5]*y+matrix[6]*z)/scale,(matrix[8]*x+matrix[9]*y+matrix[10]*z)/scale};if(a.size()==4)result.push_back(a[3]);return result;};
    std::map<std::string,std::string> curves;for(std::size_t i=0;i<before.Geometry.Segments.size();++i){const auto curve=[](const SEditableSegment& s)->const Curve3&{if(const auto* offset=std::get_if<DirectionalCurveOffset3>(&s.Curve))return offset->Source.Curve;return std::get<CurveSegment3>(s.Curve).Curve;};curves[CurveKey(curve(before.Geometry.Segments[i]))]=CurveKey(curve(after.Geometry.Segments[i]));}
    const auto transform=[&](auto&& self,Variant& item)->void{
        if(item.Is<Array>()){for(auto& child:std::get<Array>(item.m_Value))self(self,child);return;}if(!item.Is<Map>())return;auto& map=std::get<Map>(item.m_Value);
        for(auto& [key,child]:map){
            if(key=="positionOffset"&&child.Is<Array>()){auto offset=rotate(child.To<Array>());for(auto& component:offset)component=Numeric(component)*scale;child=offset;continue;}
            if((key=="direction"||key=="referenceDirection"||key=="rotation")&&child.Is<Array>()){child=rotate(child.To<Array>());continue;}
            if(key=="constructedDirections"||key=="constructedNormals"){auto values=child.To<Map>();for(auto& [_,direction]:values)direction=rotate(direction.To<Array>());child=std::move(values);continue;}
            if((key=="arc"||key=="arcOffset"||key=="length")&&!child.Is<Map>()&&!child.Is<Array>()){child=Numeric(child)*scale;continue;}
            if(key=="basis"&&child.Is<Map>()){auto basis=child.To<Map>();std::array<double,16> previous{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};if(basis.contains("correction")){const auto data=basis.at("correction").To<Array>();for(int i=0;i<16;++i)previous[i]=Numeric(data[i]);}Array combined;for(int r=0;r<4;++r)for(int c=0;c<4;++c){double value=0;for(int k=0;k<4;++k)value+=matrix[r*4+k]*previous[k*4+c];combined.emplace_back(value);}basis["correction"]=combined;child=std::move(basis);continue;}
            if(key=="measure"&&child.Is<Map>()){auto measure=child.To<Map>();if(Text(measure,"mode")=="CAD")measure["scale"]=Number(measure,"scale",1)*scale;else if(const auto found=curves.find(Text(measure,"curveFingerprint"));found!=curves.end())measure["curveFingerprint"]=found->second;child=std::move(measure);continue;}
            self(self,child);
        }
    };
    for(const auto* name:{"pose","poseControls","poseField","contourPoseField"})if(after.Channels.contains(name))transform(transform,after.Channels[name]);
    auto metadata=ReadFiveAxisIntentMetadata(after);Variant storedPose=metadata.Pose;transform(transform,storedPose);metadata.Pose=storedPose.To<Map>();
    const auto savedContours=[&](auto&& self,Variant& item)->void{if(item.Is<Array>()){for(auto& child:std::get<Array>(item.m_Value))self(self,child);}else if(item.Is<Map>())for(auto& [key,child]:std::get<Map>(item.m_Value)){if(key=="contourPoseData")transform(transform,child);else if(child.Is<Map>()||child.Is<Array>())self(self,child);}};
    Variant feature=metadata.Feature;savedContours(savedContours,feature);metadata.Feature=feature.To<Map>();after.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);
    if(after.Channels.contains("poseField")){auto field=after.Channels.at("poseField").To<Map>();field["policyFingerprint"]=PolicyKey(Policy(after,"active"));after.Channels["poseField"]=field;}
    if(after.Channels.contains("contourPoseField")){auto field=after.Channels.at("contourPoseField").To<Map>();if(field.contains("basisPolicy"))field["policyFingerprint"]=PolicyKey(Object(field,"basisPolicy"));after.Channels["contourPoseField"]=field;}
}
namespace
{
Map AnchorField(Map channel,const SPortRef& port)
{
    const auto origin=[&](auto&& self,Map descriptor)->Map{if(!descriptor.contains("origin"))descriptor["origin"]=ToData(SPathAnchor{port,Text(descriptor,"segmentId"),Start(ReadRange(Object(descriptor,"domain")))});if(descriptor.contains("baseExpression"))descriptor["baseExpression"]=self(self,Object(descriptor,"baseExpression"));return descriptor;};Array entries;
    for(const auto& item:Required(channel,"entries").To<Array>()){auto entry=item.To<Map>();if(entry.contains("range")){entries.emplace_back(std::move(entry));continue;}const auto id=Text(entry,"segmentId");const auto first=Number(entry,"first"),last=Number(entry,"last");entry["value"]=origin(origin,Object(entry,"value"));entry.erase("segmentId");entry.erase("first");entry.erase("last");entry["range"]=ToData(SPathRange{{port,id,first},{port,id,last},false});entries.emplace_back(std::move(entry));}channel["entries"]=std::move(entries);return channel;
}
}
IntentMap BuildFiveAxisIntentPoseField(const CFiveAxisIntentOperationContext& context,const SToolpathValue& value,const SPortRef& port,const std::string& set)
{
    Check(!port.OperationId.is_nil()&&!port.PortId.is_nil(),"FIVEAXIS_INTENT_POSE_SOURCE_PORT_REQUIRED");CFiveAxisIntentPoseField::Impl field(context,value,set,true);return AnchorField(field.Channel,port);
}
IntentMap BuildFiveAxisIntentPoseDelta(const CFiveAxisIntentOperationContext& context,const SToolpathValue& value,const SPortRef& port,const Array& controls,const std::string& set)
{
    Check(!port.OperationId.is_nil()&&!port.PortId.is_nil(),"FIVEAXIS_INTENT_POSE_SOURCE_PORT_REQUIRED");Check(!controls.empty(),"FIVEAXIS_INTENT_POSE_EDITS_REQUIRED");auto baseline=value;std::set<std::string> replacing;for(const auto& item:controls){const auto point=item.To<Map>();if(Text(point,"kind","Control")=="Control")replacing.insert(Text(point,"id"));else if(Text(point,"kind")=="InfluenceTarget")replacing.insert(Text(Object(point,"driver"),"id"));}if(value.Channels.contains("poseField")&&!replacing.empty())baseline.Channels["poseField"]=RemoveFiveAxisIntentPoseControls(context,value,port,replacing);CFiveAxisIntentPoseField::Impl field(context,baseline,set,true,true,&controls);return AnchorField(field.Channel,port);
}
IntentMap RemoveFiveAxisIntentPoseControls(const CFiveAxisIntentOperationContext& context,const SToolpathValue& value,const SPortRef& port,const std::set<std::string>& ids)
{
    (void)CFiveAxisIntentPoseField(context,value);auto channel=value.Channels.contains("poseField")?value.Channels.at("poseField").To<Map>():BuildFiveAxisIntentPoseField(context,value,port);auto layers=Object(channel,"layers");Check(!Flag(channel,"authored")||!layers.empty(),"FIVEAXIS_INTENT_POSE_EDITABLE_LAYER_TABLE_REQUIRED");const auto restore=[&](auto&& self,Map knot,unsigned depth)->std::optional<Map>{Check(depth<64,"FIVEAXIS_INTENT_POSE_LAYER_LIMIT");if(knot.contains("fallback")){const auto fallback=self(self,Object(knot,"fallback"),depth+1);if(fallback)knot["fallback"]=*fallback;else knot.erase("fallback");}if(!ids.contains(Text(knot,"author")))return knot;if(knot.contains("fallback"))return Object(knot,"fallback");return {};};bool authored=false;
    for(auto& [_,item]:layers){auto layer=item.To<Map>();Array knots;for(const auto& entry:Required(layer,"knots").To<Array>())if(const auto knot=restore(restore,entry.To<Map>(),0)){authored|=!Text(*knot,"author").empty();knots.emplace_back(*knot);}Check(knots.size()>=2,"FIVEAXIS_INTENT_POSE_LAYER_ENDPOINTS_MISSING");layer["knots"]=std::move(knots);item=std::move(layer);}
    const auto recompile=[&](auto&& self,Map descriptor,unsigned depth)->Map{Check(depth<64,"FIVEAXIS_INTENT_POSE_LAYER_LIMIT");if(descriptor.contains("baseExpression"))descriptor["baseExpression"]=self(self,Object(descriptor,"baseExpression"),depth+1);const auto key=Text(descriptor,"layerKey");if(!key.empty()){const auto found=layers.find(key);Check(found!=layers.end(),"FIVEAXIS_INTENT_POSE_LAYER_TABLE_MISSING");const auto knots=Required(found->second.To<Map>(),"knots").To<Array>();const auto position=Number(descriptor,"intervalArcMiddle");const auto right=std::upper_bound(knots.begin(),knots.end(),position,[](double x,const auto& item){return x<Number(item.To<Map>(),"arc");});const auto a=(right==knots.begin()?knots.front():*(right-1)).To<Map>(),b=(right==knots.end()?knots.back():*right).To<Map>();descriptor["firstKnot"]=a;descriptor["lastKnot"]=b;descriptor["metric"]=Text(a,"metric");descriptor["smooth"]=Flag(a,"smooth")||Flag(b,"smooth");}return descriptor;};Array entries;for(const auto& item:Required(channel,"entries").To<Array>()){auto entry=item.To<Map>();entry["value"]=recompile(recompile,Object(entry,"value"),0);entries.emplace_back(std::move(entry));}channel["entries"]=std::move(entries);channel["layers"]=std::move(layers);channel["authored"]=authored;return channel;
}
IntentMap ReverseFiveAxisIntentPoseField(const CFiveAxisIntentOperationContext& context,const SToolpathValue& value,const SPortRef& port)
{
    auto channel=value.Channels.contains("poseField")?value.Channels.at("poseField").To<Map>():BuildFiveAxisIntentPoseField(context,value,port);const auto policy=Policy(value,"active");auto reversed=policy;if(Fixed(reversed)){const auto direction=ReadDirection(Required(reversed,"direction"));reversed["direction"]=Array{-direction[0],-direction[1],-direction[2]};}else reversed["axisReversed"]=!Flag(reversed,"axisReversed");channel["policyFingerprint"]=PolicyKey(reversed);channel["layer"]=std::string("Polarity");Array entries;
    for(const auto& item:Required(channel,"entries").To<Array>()){auto entry=item.To<Map>();auto expression=Object(entry,"value");Map wrapped{{"polarity",-1.},{"baseExpression",expression},{"basePolicy",policy},{"domain",Required(expression,"domain")},{"segmentId",Required(expression,"segmentId")}};if(expression.contains("origin"))wrapped["origin"]=expression.at("origin");entry["value"]=std::move(wrapped);entries.emplace_back(std::move(entry));}channel["entries"]=std::move(entries);return channel;
}
IntentMap UpdateFiveAxisIntentPoseFieldPolicy(const CFiveAxisIntentOperationContext& context,const SToolpathValue& value,const SPortRef& port,const Map& policy)
{
    auto oldBasis=Policy(value,"active"),newBasis=policy;for(const auto* key:{"transitionMetric","leadDeviationLimitRad","sideDeviationLimitRad","angularToleranceRad","surfaceNormalPreferenceWeight"}){oldBasis.erase(key);newBasis.erase(key);}Check(oldBasis==newBasis,"FIVEAXIS_INTENT_POSE_BASIS_CHANGE_REQUIRES_ABSOLUTE_FIELD");(void)CFiveAxisIntentPoseField(context,value);auto channel=value.Channels.contains("poseField")?value.Channels.at("poseField").To<Map>():BuildFiveAxisIntentPoseField(context,value,port);const auto metric=Text(policy,"transitionMetric","ArcLength");Check(metric=="ArcLength"||metric=="DirectionChange","CAM_POSE_TRANSITION_INVALID");const auto knot=[&](auto&& self,Map data,unsigned depth)->Map{Check(depth<64,"FIVEAXIS_INTENT_POSE_LAYER_LIMIT");if(Text(data,"author").empty())data["metric"]=metric;if(data.contains("fallback"))data["fallback"]=self(self,Object(data,"fallback"),depth+1);return data;};auto layers=Object(channel,"layers");for(auto& [_,item]:layers){auto layer=item.To<Map>();Array knots;for(const auto& entry:Required(layer,"knots").To<Array>())knots.emplace_back(knot(knot,entry.To<Map>(),0));layer["knots"]=knots;item=layer;}channel["layers"]=layers;
    const auto expression=[&](auto&& self,Map data,unsigned depth)->Map{Check(depth<64,"FIVEAXIS_INTENT_POSE_LAYER_LIMIT");if(data.contains("baseExpression"))data["baseExpression"]=self(self,Object(data,"baseExpression"),depth+1);if(data.contains("firstKnot")){const auto first=knot(knot,Object(data,"firstKnot"),0);data["firstKnot"]=first;data["lastKnot"]=knot(knot,Object(data,"lastKnot"),0);data["metric"]=Text(first,"metric");}return data;};Array entries;for(const auto& item:Required(channel,"entries").To<Array>()){auto entry=item.To<Map>();entry["value"]=expression(expression,Object(entry,"value"),0);entries.emplace_back(std::move(entry));}channel["entries"]=entries;channel["policyFingerprint"]=PolicyKey(policy);return channel;
}
Direction3 EvaluateFiveAxisIntentPoseField(const CFiveAxisIntentOperationContext& context,const SToolpathValue& value,std::size_t segment,double parameter,const std::string& set)
{return CFiveAxisIntentPoseField(context,value,set).At(segment,parameter);}
}
