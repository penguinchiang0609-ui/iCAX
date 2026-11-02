#include "FiveAxisIntentProjection.h"
#include "Data/VariantSerializer.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace iCAX::FiveAxisCAM
{
namespace
{
using namespace iCAX::CAM::Intent;
using namespace iCAX::GeometryData;
using Map = IntentMap;
using Array = IntentArray;
using Variant = iCAX::Data::Variant;
void Check(bool valid, const char* message) { if (!valid) throw std::invalid_argument(message); }
const Variant& Required(const Map& value, const std::string& key)
{ const auto found=value.find(key); if(found==value.end())throw std::invalid_argument("FIVEAXIS_INTENT_FIELD_REQUIRED: "+key); return found->second; }
std::string Text(const Map& value,const std::string& key,const std::string& fallback={})
{ const auto found=value.find(key);return found==value.end()?fallback:found->second.To<std::string>(); }
Map Object(const Map& value,const std::string& key)
{ const auto found=value.find(key);return found==value.end()?Map{}:found->second.To<Map>(); }
double Number(const Variant& value)
{
    double result;
    if(value.Is<std::string>()){std::size_t end=0;const auto text=value.To<std::string>();result=std::stod(text,&end);Check(end==text.size(),"FIVEAXIS_INTENT_NUMBER_INVALID");}
    else result=std::visit([](const auto& number)->double
    {
        using T=std::decay_t<decltype(number)>;
        if constexpr(std::is_arithmetic_v<T>&&!std::is_same_v<T,bool>)return static_cast<double>(number);
        else throw std::invalid_argument("FIVEAXIS_INTENT_NUMBER_INVALID");
    },value.m_Value);
    Check(std::isfinite(result),"FIVEAXIS_INTENT_NUMBER_INVALID");return result;
}
IntentId Identifier(const Variant& value)
{ if(value.Is<IntentId>())return value.To<IntentId>();const auto id=IntentId::from_string(value.To<std::string>());Check(id.has_value(),"FIVEAXIS_INTENT_ID_INVALID");return *id; }
ParameterRange Range(const SEditableSegment& segment)
{
    return std::visit([](const auto& curve){
        if constexpr(std::is_same_v<std::decay_t<decltype(curve)>,DirectionalCurveOffset3>)return curve.Source.Range;
        else return curve.Range;
    },segment.Curve);
}
ParameterRange ReadRange(const Map& value)
{
    const auto source=value.contains("range")?Object(value,"range"):value;
    const auto result=ParameterRange{Number(Required(source,"first")),Number(Required(source,"last")),
        source.contains("reversed")&&Required(source,"reversed").To<bool>()};
    Check(result.Last>result.First,"FIVEAXIS_INTENT_SPAN_RANGE_INVALID");return result;
}
Map RangeValue(ParameterRange value)
{return {{"first",value.First},{"last",value.Last},{"reversed",value.Reversed}};}
double First(ParameterRange range){return range.Reversed?range.Last:range.First;}
double Last(ParameterRange range){return range.Reversed?range.First:range.Last;}
bool ContainsCurveData(const Variant&);
bool ContainsCurveData(const Map& value)
{
    for(const auto& [key,item]:value)if(key=="curveData"||ContainsCurveData(item))return true;
    return false;
}
bool ContainsCurveData(const Variant& value)
{
    if(value.Is<Map>())return ContainsCurveData(std::get<Map>(value.m_Value));
    if(value.Is<Array>())for(const auto& item:std::get<Array>(value.m_Value))if(ContainsCurveData(item))return true;
    return false;
}
bool ContainsExactCurveBody(const Variant&);
bool ContainsExactCurveBody(const Map& value)
{
    if(Text(value,"schema")=="cam.intent-curve-data")return true;
    for(const auto& [key,item]:value)if(ContainsExactCurveBody(item))return true;
    return false;
}
bool ContainsExactCurveBody(const Variant& value)
{
    if(value.Is<Map>())return ContainsExactCurveBody(std::get<Map>(value.m_Value));
    if(value.Is<Array>())for(const auto& item:std::get<Array>(value.m_Value))if(ContainsExactCurveBody(item))return true;
    return false;
}
bool ContainsManualPoints(const Variant&);
bool ContainsManualPoints(const Map& value)
{
    for(const auto& [key,item]:value)
    {if(key=="keypoints"&&!std::get<Array>(item.m_Value).empty())return true;if(ContainsManualPoints(item))return true;}
    return false;
}
bool ContainsManualPoints(const Variant& value)
{
    if(value.Is<Map>())return ContainsManualPoints(std::get<Map>(value.m_Value));
    if(value.Is<Array>())for(const auto& item:std::get<Array>(value.m_Value))if(ContainsManualPoints(item))return true;
    return false;
}
bool ContainsContourPosePoints(const Variant&);
bool ContainsContourPosePoints(const Map& value)
{
    for(const auto& [key,item]:value)
    {if(key=="contourPoseData"&&ContainsManualPoints(item))return true;if(ContainsContourPosePoints(item))return true;}
    return false;
}
bool ContainsContourPosePoints(const Variant& value)
{
    if(value.Is<Map>())return ContainsContourPosePoints(std::get<Map>(value.m_Value));
    if(value.Is<Array>())for(const auto& item:std::get<Array>(value.m_Value))if(ContainsContourPosePoints(item))return true;
    return false;
}
Variant WithoutCurveData(const Variant& value)
{
    if(value.Is<Map>()){Map result;for(const auto& [key,item]:std::get<Map>(value.m_Value))if(key!="curveData")result[key]=WithoutCurveData(item);return result;}
    if(value.Is<Array>()){Array result;for(const auto& item:std::get<Array>(value.m_Value))result.push_back(WithoutCurveData(item));return result;}
    return value;
}
bool HasKeypoints(const Map& pose)
{return pose.contains("keypoints")&&!Required(pose,"keypoints").To<Array>().empty();}
void CheckPoseInfluences(const Map& pose)
{
    if(!pose.contains("keypoints"))return;
    for(const auto& value:Required(pose,"keypoints").To<Array>())
    {
        const auto point=value.To<Map>();const auto influence=Object(point,"influence");
        if(!influence.empty())
        {
            const auto interpolation=Text(influence,"interpolation");
            Check(interpolation=="Linear"||interpolation=="Smooth","FIVEAXIS_INTENT_POSE_INTERPOLATION_INVALID");
            (void)Required(influence,"referenceDirection").To<Array>();
            (void)Required(influence,"targets").To<Array>();
        }
    }
}
SFiveAxisIntentMetadata ReadMetadata(const Map& data)
{
    SFiveAxisIntentMetadata result;
    result.SetupID=Identifier(Required(data,"setupId"));result.PlacementID=Identifier(Required(data,"placementId"));
    result.BeforeCommands=Text(data,"beforeCommands");result.AfterCommands=Text(data,"afterCommands");
    result.SourceKey=Text(data,"sourceKey");result.SourceGeometryRef=Object(data,"sourceGeometryRef");
    result.SourceData=Object(data,"sourceData");result.Feature=Object(data,"feature");
    result.Pose=Object(data,"pose");result.Process=Object(data,"process");result.SourceSpans=Object(data,"sourceSpans");
    const auto revision=data.find("inputRevision");
    if(revision!=data.end())result.InputRevision=revision->second.To<unsigned long long>();
    return result;
}
SFiveAxisIntentMetadata BuildMetadata(const Map& input,const SEditableGeometry& geometry,const Array& sourceSpans,bool withControls=false)
{
    auto result=ReadMetadata(input);
    Check(withControls||(!ContainsContourPosePoints(result.Feature)&&!ContainsContourPosePoints(result.SourceData)),"FIVEAXIS_INTENT_CONTOUR_POSE_CONTROLS_REQUIRE_SOURCE_PORT");
    result.Feature=WithoutCurveData(result.Feature).To<Map>();result.Feature.erase("start");
    result.SourceData=WithoutCurveData(result.SourceData).To<Map>();
    if(!sourceSpans.empty())
    {
        Check(result.SourceSpans.empty(),"FIVEAXIS_INTENT_SPAN_SOURCES_CONFLICT");
        Check(sourceSpans.size()==geometry.Segments.size(),"FIVEAXIS_INTENT_SOURCE_SPAN_COUNT_MISMATCH");
        for(std::size_t i=0;i<sourceSpans.size();++i)
        {
            const auto& segment=geometry.Segments[i];Check(!segment.Id.empty(),"FIVEAXIS_INTENT_SEGMENT_ID_REQUIRED");
            Check(segment.ConstructionRole.empty(),"FIVEAXIS_INTENT_CONSTRUCTION_CANNOT_HAVE_CAD_SPAN");
            auto evidence=sourceSpans[i].To<Map>();
            const auto range=evidence.contains("range")||evidence.contains("first")?ReadRange(evidence):Range(segment);
            evidence["range"]=RangeValue(range);evidence["first"]=range.First;evidence["last"]=range.Last;evidence["reversed"]=range.Reversed;
            Check(!ContainsCurveData(evidence),"FIVEAXIS_INTENT_EVIDENCE_GEOMETRY_DUPLICATE");
            Check(result.SourceSpans.emplace(segment.Id,std::move(evidence)).second,"FIVEAXIS_INTENT_SEGMENT_ID_DUPLICATE");
        }
    }
    return result;
}
void ValidateDirection(const Variant& value)
{
    const auto direction=value.To<Array>();Check(direction.size()==3,"FIVEAXIS_INTENT_POSE_DIRECTION_INVALID");
    double norm=0;for(const auto& coordinate:direction){const auto n=Number(coordinate);norm+=n*n;}
    Check(std::isfinite(norm)&&std::abs(norm-1)<=1e-10,"FIVEAXIS_INTENT_POSE_DIRECTION_INVALID");
}
void ValidateTransition(const std::string& transition)
{Check(transition=="ArcLength"||transition=="DirectionChange","FIVEAXIS_INTENT_POSE_TRANSITION_INVALID");}
SPathAnchor SourcePoseAnchor(const Map& old,const SEditableGeometry& geometry,const Map& spans,const SPortRef& port)
{
    const auto parameter=Number(Required(old,"nativeParameter"));const auto spanId=Text(old,"sourceSpanId");
    Check(!spanId.empty(),"FIVEAXIS_INTENT_POSE_ANCHOR_NEEDS_REBIND");std::vector<const SEditableSegment*> matches;
    for(const auto& segment:geometry.Segments)
    {
        const auto evidence=spans.find(segment.Id);if(evidence==spans.end()||!segment.ConstructionRole.empty())continue;
        const auto record=evidence->second.To<Map>();const auto range=Range(segment);
        if(Text(record,"sourceSpanId")==spanId&&parameter>=range.First-1e-12&&parameter<=range.Last+1e-12)matches.push_back(&segment);
    }
    Check(matches.size()==1,"FIVEAXIS_INTENT_POSE_ANCHOR_NEEDS_REBIND");
    // A closed traversal can place the same physical station at both sides of
    // its seam. Preserve one explicit address when a later core Start rotates
    // that seam; the field itself still enforces its closed boundary value.
    return {port,matches.front()->Id,parameter,0,geometry.Closed?"Start":""};
}
Array PoseEntries(const Map& pose,const SEditableGeometry& geometry,const Map& spans,const SPortRef& port,const std::string& collection)
{
    CheckPoseInfluences(pose);Check(!port.OperationId.is_nil()&&!port.PortId.is_nil(),"FIVEAXIS_INTENT_POSE_SOURCE_PORT_REQUIRED");
    Array entries;std::set<std::string> ids,targets;iCAX::Data::uuid_name_generator names(port.OperationId);
    if(pose.contains("keypoints"))for(const auto& value:Required(pose,"keypoints").To<Array>())
    {
        const auto point=value.To<Map>();const auto owner=Text(point,"id");
        Check(!owner.empty()&&ids.insert(owner).second,"FIVEAXIS_INTENT_POSE_POINT_ID_INVALID");
        ValidateDirection(Required(point,"direction"));const auto transition=Text(point,"transition");ValidateTransition(transition);
        const auto anchor=SourcePoseAnchor(Object(point,"anchor"),geometry,spans,port);
        Map control{{"kind",std::string("Control")},{"set",collection},{"id",owner},{"anchor",iCAX::CAM::Intent::ToData(anchor)},
            {"direction",Required(point,"direction")},{"transition",transition},{"boundaryPolicy",std::string("Duplicate")}};
        if(point.contains("influence"))
        {
            const auto influence=Object(point,"influence");ValidateDirection(Required(influence,"referenceDirection"));
            const Map driver{{"id",collection+"/"+owner},{"referenceDirection",Required(influence,"referenceDirection")},
                {"direction",Required(point,"direction")},{"transition",transition},{"interpolation",Required(influence,"interpolation")}};
            control["driver"]=driver;
            for(const auto& value:Required(influence,"targets").To<Array>())
            {
                const auto target=value.To<Map>();const auto targetAnchor=SourcePoseAnchor(Object(target,"anchor"),geometry,spans,port);
                const auto weight=Number(Required(target,"weight"));Check(weight>=0&&weight<=1,"FIVEAXIS_INTENT_POSE_INFLUENCE_WEIGHT_INVALID");
                ValidateDirection(Required(target,"referenceDirection"));
                const auto address=iCAX::Data::VariantSerializer::Serialize(iCAX::CAM::Intent::ToData(targetAnchor));
                Check(targets.insert(address).second,"FIVEAXIS_INTENT_POSE_INFLUENCE_TARGET_OVERLAP");
                Check(targetAnchor.SegmentId!=anchor.SegmentId||std::abs(targetAnchor.Parameter-anchor.Parameter)>1e-12,
                    "FIVEAXIS_INTENT_POSE_INFLUENCE_OWNER_SELECTED");
                const auto id=iCAX::Data::to_string(names("fiveaxis.pose-target/"+collection+"/"+owner+"/"+address));
                entries.emplace_back(Map{{"kind",std::string("InfluenceTarget")},{"set",collection},{"id",id},
                    {"anchor",iCAX::CAM::Intent::ToData(targetAnchor)},{"driver",driver},{"weight",weight},
                    {"referenceDirection",Required(target,"referenceDirection")},{"boundaryPolicy",std::string("Duplicate")}});
            }
        }
        entries.emplace_back(std::move(control));
    }
    return entries;
}
Map PoseReference(const SPathAnchor& anchor,const std::string& collection)
{
    iCAX::Data::uuid_name_generator names(anchor.Port.OperationId);
    return {{"kind",std::string("Reference")},{"set",collection},
        {"id",iCAX::Data::to_string(names("fiveaxis.pose-reference/start/"+collection))},
        {"anchor",iCAX::CAM::Intent::ToData(anchor)},{"boundaryPolicy",std::string("Duplicate")}};
}
Variant ExtractContourControls(const Variant& value,const std::string& location,const SEditableGeometry& geometry,
    const Map& spans,const SPortRef& port,Array& entries,const std::optional<SPathAnchor>& reference)
{
    if(value.Is<Array>())
    {Array result;std::size_t index=0;for(const auto& item:value.To<Array>())result.push_back(ExtractContourControls(item,location+"/"+std::to_string(index++),geometry,spans,port,entries,reference));return result;}
    if(!value.Is<Map>())return value;auto result=value.To<Map>();
    for(auto& [key,item]:result)
    {
        if(key!="contourPoseData"){item=ExtractContourControls(item,location+"/"+key,geometry,spans,port,entries,reference);continue;}
        auto container=item.To<Map>();const bool wrapped=container.contains("value")&&Required(container,"value").Is<Map>();
        auto pose=container.contains("basis")?Object(container,"basis"):wrapped?Object(container,"value"):container;
        const auto collection=location=="feature"?std::string("contour"):"contour/"+location;
        auto controls=PoseEntries(pose,geometry,spans,port,collection);entries.insert(entries.end(),controls.begin(),controls.end());
        if(reference)entries.emplace_back(PoseReference(*reference,collection));
        pose.erase("keypoints");pose.erase("controlsSet");
        item=Map{{"basis",std::move(pose)},{"controlsSet",collection}};
    }
    return result;
}
Map ProjectSpans(const SEditableGeometry& geometry,const Map& catalogue)
{
    Map result;
    for(const auto& segment:geometry.Segments)
    {
        Array evidence;std::set<std::string> unique;const auto current=Range(segment);
        const auto add=[&](const std::string& id,ParameterRange source,ParameterRange output,std::uint64_t occurrence){
            const auto found=catalogue.find(id);if(found==catalogue.end())return;
            auto record=found->second.To<Map>();const auto original=ReadRange(record);
            const auto low=std::max(current.First,output.First),high=std::min(current.Last,output.Last);
            if(high<=low)return;
            const auto toSource=[&](double value){return First(source)+(value-First(output))/(Last(output)-First(output))*(Last(source)-First(source));};
            const auto toOutput=[&](double value){return First(output)+(value-First(source))/(Last(source)-First(source))*(Last(output)-First(output));};
            const auto a=toSource(low),b=toSource(high);
            const auto start=std::max(std::min(a,b),original.First),end=std::min(std::max(a,b),original.Last);
            if(end<=start)return;
            const auto outA=toOutput(start),outB=toOutput(end);
            const bool sourceReversed=toSource(First(current))>toSource(Last(current));
            const ParameterRange clipped{start,end,sourceReversed},out{std::min(outA,outB),std::max(outA,outB),current.Reversed};
            record["sourceSegmentId"]=id;record["range"]=RangeValue(clipped);record["outputRange"]=RangeValue(out);
            record["first"]=start;record["last"]=end;record["reversed"]=sourceReversed;record["occurrence"]=static_cast<unsigned long long>(occurrence);
            if(unique.insert(iCAX::Data::VariantSerializer::Serialize(record)).second)evidence.emplace_back(std::move(record));
        };
        if(segment.ConstructionRole.empty())
        {
            if(catalogue.contains(segment.Id))add(segment.Id,current,current,0);
            for(const auto& mapping:segment.Sources)
            {
                Check(mapping.Precision=="Exact","FIVEAXIS_INTENT_APPROXIMATE_CAD_MAPPING_UNSUPPORTED");
                Check(mapping.SourceRange.Last>mapping.SourceRange.First&&mapping.OutputRange.Last>mapping.OutputRange.First,"FIVEAXIS_INTENT_MAPPING_RANGE_INVALID");
                add(mapping.SourceSegmentId,mapping.SourceRange,mapping.OutputRange,mapping.Occurrence);
            }
        }
        result[segment.Id]=std::move(evidence);
    }
    return result;
}
}

void ValidateFiveAxisIntentMetadata(const SFiveAxisIntentMetadata& metadata)
{
    Check(!metadata.SetupID.is_nil()&&!metadata.PlacementID.is_nil(),"FIVEAXIS_INTENT_OWNER_REQUIRED");
    Check(metadata.InputRevision>0,"FIVEAXIS_INTENT_REVISION_INVALID");
    Check(!ContainsCurveData(metadata.Feature)&&!ContainsCurveData(metadata.SourceData)
        &&!ContainsExactCurveBody(metadata.Feature)&&!ContainsExactCurveBody(metadata.SourceData)
        &&!ContainsExactCurveBody(metadata.Pose)&&!ContainsExactCurveBody(metadata.Process),"FIVEAXIS_INTENT_FEATURE_GEOMETRY_DUPLICATE");
    Check(!metadata.Feature.contains("start"),"FIVEAXIS_INTENT_START_BELONGS_TO_CORE_GEOMETRY");
    Check(!ContainsContourPosePoints(metadata.Feature)&&!ContainsContourPosePoints(metadata.SourceData),"FIVEAXIS_INTENT_CONTOUR_POSE_CONTROLS_REQUIRE_POINT_CHANNEL");
    Check(!HasKeypoints(metadata.Pose),"FIVEAXIS_INTENT_POSE_CONTROLS_REQUIRE_POINT_CHANNEL");
    for(const auto& [id,value]:metadata.SourceSpans)
    {Check(!id.empty(),"FIVEAXIS_INTENT_SEGMENT_ID_REQUIRED");const auto evidence=value.To<Map>();(void)ReadRange(evidence);Check(!ContainsCurveData(evidence),"FIVEAXIS_INTENT_EVIDENCE_GEOMETRY_DUPLICATE");}
}
IntentMap ToFiveAxisIntentChannel(const SFiveAxisIntentMetadata& metadata)
{
    // Most current metadata already stores its start in the core geometry.
    // Only legacy feature starts need a normalized copy of the full record.
    std::optional<SFiveAxisIntentMetadata> normalized;const auto* authoritative=&metadata;
    if(metadata.Feature.contains("start")){normalized=metadata;normalized->Feature.erase("start");authoritative=&*normalized;}
    ValidateFiveAxisIntentMetadata(*authoritative);
    return {{"schema",std::string("fiveaxis.intent/1")},{"schemaVersion",1u},{"attachment",std::string("Path")},
        {"setupId",metadata.SetupID},{"placementId",metadata.PlacementID},
        {"beforeCommands",metadata.BeforeCommands},{"afterCommands",metadata.AfterCommands},{"sourceKey",metadata.SourceKey},
        {"sourceGeometryRef",metadata.SourceGeometryRef},{"sourceData",metadata.SourceData},{"feature",authoritative->Feature},
        {"pose",metadata.Pose},{"process",metadata.Process},{"inputRevision",static_cast<unsigned long long>(metadata.InputRevision)},
        {"sourceSpans",metadata.SourceSpans}};
}
SFiveAxisIntentMetadata FiveAxisIntentMetadataFromChannel(const IntentMap& channel)
{
    Check(Text(channel,"schema")=="fiveaxis.intent/1"&&Text(channel,"attachment")=="Path"
        &&Required(channel,"schemaVersion").To<unsigned int>()==1,"FIVEAXIS_INTENT_CHANNEL_SCHEMA_UNSUPPORTED");
    auto result=ReadMetadata(channel);ValidateFiveAxisIntentMetadata(result);return result;
}
SFiveAxisIntentMetadata ReadFiveAxisIntentMetadata(const SToolpathValue& value)
{
    auto result=FiveAxisIntentMetadataFromChannel(std::get<Map>(Required(value.Channels,"fiveaxis").m_Value));
    if(const auto found=value.Channels.find("pose");found!=value.Channels.end())
    {
        const auto& channel=std::get<Map>(found->second.m_Value);Check(Text(channel,"schema")=="fiveaxis.pose/1"&&Text(channel,"attachment")=="Path","FIVEAXIS_INTENT_POSE_SCHEMA_UNSUPPORTED");
        result.Pose=Object(channel,"policy");Check(!result.Pose.empty()&&!HasKeypoints(result.Pose),"FIVEAXIS_INTENT_POSE_CONTROLS_REQUIRE_POINT_CHANNEL");
    }
    if(const auto found=value.Channels.find("fiveaxis.kerf");found!=value.Channels.end())
    {const auto& channel=std::get<Map>(found->second.m_Value);if(channel.contains("policy"))result.Process["kerf"]=Required(channel,"policy");}
    if(const auto found=value.Channels.find("fiveaxis.trace");found!=value.Channels.end())
    {const auto& channel=std::get<Map>(found->second.m_Value);for(const auto* key:{"gapLengthMm","overcutLengthMm"})if(channel.contains(key))result.Process[key]=Required(channel,key);}
    if(const auto found=value.Channels.find("fiveaxis.leads");found!=value.Channels.end())
    {const auto& channel=std::get<Map>(found->second.m_Value);for(const auto* key:{"leadIn","leadOut"})if(channel.contains(key))result.Process[key]=Required(channel,key);}
    return result;
}
IntentMap BuildFiveAxisIntentChannel(const IntentMap& input,const SEditableGeometry& geometry,const IntentArray& spans)
{
    auto metadata=BuildMetadata(input,geometry,spans);CheckPoseInfluences(metadata.Pose);
    Check(!HasKeypoints(metadata.Pose),"FIVEAXIS_INTENT_POSE_CONTROLS_REQUIRE_SOURCE_PORT");metadata.Pose.erase("keypoints");
    return ToFiveAxisIntentChannel(metadata);
}
IntentMap BuildFiveAxisPoseControls(const IntentMap& pose,const SEditableGeometry& geometry,const IntentMap& spans,const SPortRef& port)
{
    return {{"schema",std::string("fiveaxis.pose-controls/1")},{"schemaVersion",1u},{"attachment",std::string("Points")},
        {"entries",PoseEntries(pose,geometry,spans,port,"active")}};
}
IntentMap BuildFiveAxisIntentChannels(const IntentMap& input,const SEditableGeometry& geometry,const IntentArray& spans,const SPortRef& port)
{
    auto metadata=BuildMetadata(input,geometry,spans,true);
    auto entries=PoseEntries(metadata.Pose,geometry,metadata.SourceSpans,port,"active");
    const auto original=ReadMetadata(input);std::optional<SPathAnchor> reference;
    if(original.Feature.contains("start"))reference=SourcePoseAnchor(Object(original.Feature,"start"),geometry,metadata.SourceSpans,port);
    if(reference)entries.emplace_back(PoseReference(*reference,"active"));
    metadata.Feature=ExtractContourControls(metadata.Feature,"feature",geometry,metadata.SourceSpans,port,entries,reference).To<Map>();
    metadata.SourceData=ExtractContourControls(metadata.SourceData,"sourceData",geometry,metadata.SourceSpans,port,entries,reference).To<Map>();
    metadata.Pose.erase("keypoints");
    return {{"fiveaxis",ToFiveAxisIntentChannel(metadata)},{"poseControls",Map{{"schema",std::string("fiveaxis.pose-controls/1")},
        {"schemaVersion",1u},{"attachment",std::string("Points")},{"entries",std::move(entries)}}}};
}
IntentMap ProjectFiveAxisPoseControls(const SToolpathValue& value)
{
    const auto found=value.Channels.find("poseControls");if(found==value.Channels.end())return {};
    const auto channel=found->second.To<Map>();
    Check(Text(channel,"schema")=="fiveaxis.pose-controls/1"&&Text(channel,"attachment")=="Points"
        &&Required(channel,"schemaVersion").To<unsigned int>()==1,"FIVEAXIS_INTENT_POSE_CHANNEL_SCHEMA_UNSUPPORTED");
    Map result;std::map<std::string,Map> drivers;std::set<std::pair<std::string,std::string>> ids;
    for(const auto& item:Required(channel,"entries").To<Array>())
    {
        auto entry=item.To<Map>();const auto kind=Text(entry,"kind","Control"),collection=Text(entry,"set","active");
        Check((kind=="Control"||kind=="InfluenceTarget"||kind=="Reference")&&!collection.empty(),"FIVEAXIS_INTENT_POSE_ENTRY_INVALID");
        Check(!entry.contains("influence"),"FIVEAXIS_INTENT_POSE_OPAQUE_INFLUENCE_FORBIDDEN");
        Check(!Text(entry,"id").empty()&&ids.emplace(collection,Text(entry,"id")).second,"FIVEAXIS_INTENT_POSE_POINT_ID_INVALID");
        const auto anchor=AnchorFromData(Object(entry,"anchor"));
        Check(!anchor.Port.OperationId.is_nil()&&!anchor.Port.PortId.is_nil(),"FIVEAXIS_INTENT_POSE_SOURCE_PORT_REQUIRED");
        const auto segment=std::find_if(value.Geometry.Segments.begin(),value.Geometry.Segments.end(),[&](const auto& segment){return segment.Id==anchor.SegmentId;});
        Check(segment!=value.Geometry.Segments.end(),"FIVEAXIS_INTENT_POSE_ANCHOR_NEEDS_REBIND");
        const auto range=Range(*segment);Check(std::isfinite(anchor.Parameter)&&anchor.Parameter>=range.First-1e-12&&anchor.Parameter<=range.Last+1e-12,
            "FIVEAXIS_INTENT_POSE_ANCHOR_NEEDS_REBIND");
        Check(Text(entry,"boundaryPolicy")=="Duplicate","FIVEAXIS_INTENT_POSE_BOUNDARY_POLICY_INVALID");
        if(kind=="Control"){ValidateDirection(Required(entry,"direction"));ValidateTransition(Text(entry,"transition"));}
        else if(kind=="InfluenceTarget")
        {
            const auto weight=Number(Required(entry,"weight"));Check(weight>=0&&weight<=1,"FIVEAXIS_INTENT_POSE_INFLUENCE_WEIGHT_INVALID");
            ValidateDirection(Required(entry,"referenceDirection"));Check(entry.contains("driver"),"FIVEAXIS_INTENT_POSE_DRIVER_REQUIRED");
        }
        else Check(!entry.contains("direction")&&!entry.contains("driver")&&!entry.contains("weight"),"FIVEAXIS_INTENT_POSE_REFERENCE_INVALID");
        if(entry.contains("driver"))
        {
            const auto driver=Object(entry,"driver");const auto id=Text(driver,"id");
            Check(!id.empty()&&!driver.contains("anchor"),"FIVEAXIS_INTENT_POSE_DRIVER_INVALID");
            ValidateDirection(Required(driver,"referenceDirection"));ValidateDirection(Required(driver,"direction"));ValidateTransition(Text(driver,"transition"));
            const auto interpolation=Text(driver,"interpolation");Check(interpolation=="Linear"||interpolation=="Smooth","FIVEAXIS_INTENT_POSE_INTERPOLATION_INVALID");
            const auto existing=drivers.find(id);Check(existing==drivers.end()||existing->second==driver,"FIVEAXIS_INTENT_POSE_DRIVER_CONFLICT");drivers[id]=driver;
            if(kind=="Control")Check(Required(entry,"direction")==Required(driver,"direction")&&Text(entry,"transition")==Text(driver,"transition"),
                "FIVEAXIS_INTENT_POSE_DRIVER_CONFLICT");
        }
        entry["kind"]=kind;entry["set"]=collection;
        auto entries=result.contains(collection)?Required(result,collection).To<Array>():Array{};entries.emplace_back(std::move(entry));result[collection]=std::move(entries);
    }
    return result;
}
SFiveAxisIntentProjection ProjectFiveAxisIntentPath(const SResolvedPath& path)
{
    SFiveAxisIntentProjection result;
    result.PathId=path.PathId;result.Port=path.Port;result.Name=path.Name;result.UserNumber=path.UserNumber;
    result.GroupId=path.GroupId;result.Fingerprint=path.Fingerprint;result.Value=path.Value;result.Metadata=ReadFiveAxisIntentMetadata(path.Value);
    result.SourceSpans=ProjectSpans(path.Value.Geometry,result.Metadata.SourceSpans);result.PoseControls=ProjectFiveAxisPoseControls(path.Value);
    result.Metadata.Feature.erase("start");
    if(!path.Value.Geometry.Segments.empty())
    {
        const auto& first=path.Value.Geometry.Segments.front();const auto evidence=Required(result.SourceSpans,first.Id).To<Array>();
        if(evidence.size()==1){const auto record=evidence.front().To<Map>();if(record.contains("sourceSpanId"))
            result.Metadata.Feature["start"]=Map{{"sourceSpanId",Required(record,"sourceSpanId")},{"nativeParameter",First(ReadRange(record))}};}
    }
    return result;
}
std::vector<iCAX::CAM::Intent::SResolvedPath> OrderFiveAxisIntentPaths(std::vector<iCAX::CAM::Intent::SResolvedPath> paths,const Map& constraints)
{
    // The core already expanded stable ports into visible path identities.
    // This is a deterministic presentation of those common constraints,
    // including the contiguity of nested atomic execution blocks.
    std::map<IntentId,std::size_t> positions;for(std::size_t i=0;i<paths.size();++i)positions.emplace(paths[i].PathId,i);
    using Members=std::set<std::size_t>;
    const auto indices=[&](const Variant& value)
    {
        std::vector<std::size_t> result;
        const auto append=[&](const Variant& item){const auto found=positions.find(Identifier(item));if(found!=positions.end())result.push_back(found->second);};
        if(value.Is<Array>())for(const auto& item:value.To<Array>())append(item);else append(value);return result;
    };
    std::vector<Members> blocks(1);for(std::size_t i=0;i<paths.size();++i)blocks[0].insert(i);
    std::set<std::pair<std::size_t,std::size_t>> edges;
    const auto edge=[&](std::size_t before,std::size_t after){if(before!=after)edges.emplace(before,after);};
    if(constraints.contains("blocks"))for(const auto& item:constraints.at("blocks").To<Array>())
    {
        const auto block=item.To<Map>();const auto order=indices(block.at("members"));
        for(std::size_t i=1;i<order.size();++i)edge(order[i-1],order[i]);
        if((!block.contains("atomic")||block.at("atomic").To<bool>())&&order.size()>1)
        {Members members(order.begin(),order.end());if(std::find(blocks.begin(),blocks.end(),members)==blocks.end())blocks.push_back(std::move(members));}
    }
    if(constraints.contains("precedence"))for(const auto& item:constraints.at("precedence").To<Array>())
    {const auto precedence=item.To<Map>();for(auto before:indices(precedence.at("before")))for(auto after:indices(precedence.at("after")))edge(before,after);}
    std::vector<std::size_t> parent(blocks.size(),0);std::vector<std::vector<std::size_t>> children(blocks.size());
    for(std::size_t i=1;i<blocks.size();++i)
    {
        for(std::size_t j=1;j<blocks.size();++j)if(i!=j&&blocks[j].size()>blocks[i].size()
            &&std::includes(blocks[j].begin(),blocks[j].end(),blocks[i].begin(),blocks[i].end())&&blocks[j].size()<blocks[parent[i]].size())parent[i]=j;
        children[parent[i]].push_back(i);
    }
    std::vector<std::size_t> leafParent(paths.size(),0);
    for(std::size_t i=0;i<paths.size();++i)for(std::size_t block=1;block<blocks.size();++block)
        if(blocks[block].contains(i)&&blocks[block].size()<blocks[leafParent[i]].size())leafParent[i]=block;
    const auto unit=[&](std::size_t container,std::size_t leaf)
    {
        if(leafParent[leaf]==container)return blocks.size()+leaf;
        auto block=leafParent[leaf];while(parent[block]!=container){if(block==0)throw std::invalid_argument("CAM_INTENT_ORDER_BLOCK_INVALID");block=parent[block];}return block;
    };
    std::vector<std::size_t> order;
    const auto walk=[&](auto&& self,std::size_t container)->void
    {
        std::set<std::size_t> units(children[container].begin(),children[container].end());
        for(auto leaf:blocks[container])if(leafParent[leaf]==container)units.insert(blocks.size()+leaf);
        std::map<std::size_t,Members> outgoing;std::map<std::size_t,std::size_t> indegree;
        for(auto child:units)indegree[child]=0;
        for(const auto& [before,after]:edges)if(blocks[container].contains(before)&&blocks[container].contains(after))
        {const auto from=unit(container,before),to=unit(container,after);if(from!=to&&outgoing[from].insert(to).second)++indegree[to];}
        const auto rank=[&](std::size_t node){return node<blocks.size()?*blocks[node].begin():node-blocks.size();};
        while(!units.empty())
        {
            auto next=units.end();for(auto it=units.begin();it!=units.end();++it)if(indegree[*it]==0&&(next==units.end()||rank(*it)<rank(*next)))next=it;
            if(next==units.end())throw std::invalid_argument("CAM_INTENT_ORDER_CONSTRAINT_CONFLICT");const auto node=*next;units.erase(next);
            if(node<blocks.size())self(self,node);else order.push_back(node-blocks.size());
            for(auto target:outgoing[node])--indegree[target];
        }
    };
    walk(walk,0);std::vector<iCAX::CAM::Intent::SResolvedPath> result;for(auto index:order)result.push_back(std::move(paths[index]));return result;
}
std::vector<SFiveAxisIntentProjection> ProjectFiveAxisIntentPlan(const CEditableToolpathDocument& document,IntentId plan)
{
    const auto resolved=document.Resolve(plan);Check(resolved.Status==EOutputCompleteness::Complete,"FIVEAXIS_INTENT_PLAN_NOT_CURRENT");
    std::vector<SFiveAxisIntentProjection> result;for(const auto& path:resolved.Paths)result.push_back(ProjectFiveAxisIntentPath(path));return result;
}
}
