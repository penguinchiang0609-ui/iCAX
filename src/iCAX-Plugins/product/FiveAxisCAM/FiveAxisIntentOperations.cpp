#include "FiveAxisIntentOperations.h"
#include "FiveAxisIntentProjection.h"
#include "FiveAxisIntentPoseField.h"
#include "FiveAxisIntentBranchProcess.h"
#include "FiveAxisIntentCorrection.h"
#include "../../cam/IntentToolpath/EditableToolpathPreview.h"
#include "../../cam/IntentToolpath/EditableToolpathProjection.h"
#include <GeometryServices/OffsetCurveTraversal.h>
#include <GeometryData/BRepPersistence.h>
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
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
namespace Walk=iCAX::GeometryServices::OffsetTraversal;
using Map=IntentMap;using Array=IntentArray;using Variant=iCAX::Data::Variant;
void Check(bool valid,const std::string& error){if(!valid)throw std::invalid_argument(error);}
std::string Text(const Map& map,const char* key,const std::string& fallback={})
{const auto found=map.find(key);return found==map.end()?fallback:found->second.To<std::string>();}
double Numeric(const Variant& value)
{const auto number=std::visit([](const auto& stored)->double{using T=std::decay_t<decltype(stored)>;if constexpr(std::is_same_v<T,std::string>){std::size_t end=0;const auto number=std::stod(stored,&end);Check(end==stored.size(),"FIVEAXIS_INTENT_NUMBER_INVALID");return number;}else if constexpr(std::is_arithmetic_v<T>&&!std::is_same_v<T,bool>)return static_cast<double>(stored);else throw std::invalid_argument("FIVEAXIS_INTENT_NUMBER_INVALID");},value.m_Value);Check(std::isfinite(number),"FIVEAXIS_INTENT_NUMBER_NONFINITE");return number;}
double Number(const Map& map,const char* key,double fallback=0)
{const auto found=map.find(key);return found==map.end()?fallback:Numeric(found->second);}
Map Object(const Map& map,const char* key){const auto found=map.find(key);return found==map.end()?Map{}:found->second.To<Map>();}
bool Flag(const Map& map,const char* key,bool fallback=false){const auto found=map.find(key);return found==map.end()?fallback:found->second.To<bool>();}
void Fields(const Map& map,std::initializer_list<const char*> allowed)
{for(const auto& [key,_]:map)Check(std::any_of(allowed.begin(),allowed.end(),[&](const auto* field){return key==field;}),"FIVEAXIS_INTENT_PARAMETER_UNKNOWN: "+key);}
void Cancel(const SOperationEvaluation& input){if(input.Cancelled&&input.Cancelled->load())throw std::runtime_error("TOOLPATH_CANCELLED");}
template<class T> const T& Record(const std::vector<T>& records,std::uint64_t id)
{for(const auto& item:records)if(item.Id==id)return item;throw std::invalid_argument("CAM_GENERATE_SOURCE_MISMATCH");}
int Sign(ETopologyOrientation orientation)
{if(orientation==ETopologyOrientation::Forward)return 1;if(orientation==ETopologyOrientation::Reversed)return -1;throw std::invalid_argument("CAM_GENERATE_SOURCE_ORIENTATION");}
ParameterRange Range(const SEditableSegment& segment)
{return std::visit([](const auto& curve){if constexpr(requires{curve.Source.Range;})return curve.Source.Range;else return curve.Range;},segment.Curve);}
void SetRange(SEditableSegment& segment,ParameterRange range)
{std::visit([&](auto& curve){if constexpr(requires{curve.Source.Range;})curve.Source.Range=range;else curve.Range=range;},segment.Curve);}
double Start(ParameterRange r){return r.Reversed?r.Last:r.First;}
double End(ParameterRange r){return r.Reversed?r.First:r.Last;}
Direction3 Unit(Direction3 v)
{const auto n=std::hypot(v.X,v.Y,v.Z);Check(std::isfinite(n)&&n>1e-12,"CAM_POSE_DIRECTION_INVALID");return {v.X/n,v.Y/n,v.Z/n};}
Direction3 Cross(Direction3 a,Direction3 b){return {a.Y*b.Z-a.Z*b.Y,a.Z*b.X-a.X*b.Z,a.X*b.Y-a.Y*b.X};}
double Dot(Direction3 a,Direction3 b){return a.X*b.X+a.Y*b.Y+a.Z*b.Z;}
Direction3 Times(Direction3 v,double factor){return {v.X*factor,v.Y*factor,v.Z*factor};}
Array Point(Direction3 v){return {v.X,v.Y,v.Z};}
Direction3 Direction(const Variant& value)
{const auto a=value.To<Array>();Check(a.size()==3,"CAM_POSE_DIRECTION_INVALID");Direction3 v{Numeric(a[0]),Numeric(a[1]),Numeric(a[2])};const auto n=std::hypot(v.X,v.Y,v.Z);Check(std::isfinite(n)&&std::abs(n-1.)<=1e-10,"CAM_POSE_DIRECTION_INVALID");return v;}
std::string Digest(const std::vector<std::uint8_t>& bytes)
{
    Check(bytes.size()<=std::numeric_limits<ULONG>::max(),"FIVEAXIS_INTENT_CONTEXT_TOO_LARGE");unsigned char digest[32]{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),digest,sizeof(digest))<0)throw std::runtime_error("FIVEAXIS_INTENT_CONTEXT_HASH_FAILED");
    constexpr char digits[]="0123456789abcdef";std::string output;for(auto value:digest){output+=digits[value>>4];output+=digits[value&15];}return output;
}
const CFiveAxisIntentOperationContext& Context(const SOperationEvaluation& input)
{const auto* context=dynamic_cast<const CFiveAxisIntentOperationContext*>(input.RuntimeContext.get());Check(context!=nullptr,"FIVEAXIS_INTENT_FROZEN_CONTEXT_REQUIRED");Check(input.Inputs.size()==1&&context->Frame()==input.Inputs[0].Value.Geometry.FrameId,"FIVEAXIS_INTENT_CONTEXT_FRAME_MISMATCH");return *context;}
struct Bound final{const SFiveAxisIntentCadSpan* Binding=nullptr;double Parameter=0;};
Bound Resolve(const CFiveAxisIntentOperationContext& context,const SEditableSegment& segment,double parameter)
{
    std::vector<Bound> matches;
    const auto add=[&](const std::string& id,double native){const auto found=context.SourceBindings().find(id);if(found==context.SourceBindings().end())return;
        if(std::none_of(matches.begin(),matches.end(),[&](const auto& old){return old.Binding==&found->second&&std::abs(old.Parameter-native)<1e-11;}))matches.push_back({&found->second,native});};
    add(segment.Id,parameter);
    for(const auto& source:segment.Sources)if(parameter>=source.OutputRange.First-1e-12&&parameter<=source.OutputRange.Last+1e-12&&context.SourceBindings().contains(source.SourceSegmentId)) {
        Check(source.Precision=="Exact","FIVEAXIS_INTENT_CAD_MAPPING_PRECISION_UNSUPPORTED");const auto fraction=(parameter-Start(source.OutputRange))/(End(source.OutputRange)-Start(source.OutputRange));
        add(source.SourceSegmentId,Start(source.SourceRange)+fraction*(End(source.SourceRange)-Start(source.SourceRange)));
    }
    Check(matches.size()==1,"FIVEAXIS_INTENT_CAD_BINDING_AMBIGUOUS_OR_MISSING");return matches.front();
}
BoundaryLeadInput Query(Bound bound)
{BoundaryLeadInput q;q.FaceId=bound.Binding->FaceId;q.WireId=bound.Binding->WireId;q.EdgeId=bound.Binding->EdgeId;q.CoedgeIndex=bound.Binding->CoedgeIndex;q.NativeParameter=bound.Parameter;return q;}
Map Channel(const SToolpathValue& value,const char* id)
{const auto found=value.Channels.find(id);Check(found!=value.Channels.end(),std::string("FIVEAXIS_INTENT_CHANNEL_REQUIRED: ")+id);return found->second.To<Map>();}
Map Pose(const SToolpathValue& value)
{const auto found=value.Channels.find("pose");if(found==value.Channels.end())return ReadFiveAxisIntentMetadata(value).Pose;const auto channel=found->second.To<Map>();Check(Text(channel,"schema")=="fiveaxis.pose/1","FIVEAXIS_INTENT_POSE_SCHEMA_UNSUPPORTED");return Object(channel,"policy");}
bool Fixed(const Map& pose){return Text(pose,"mode")=="FixedDirection"||(Text(pose,"mode")=="Continuous"&&Text(pose,"initialization")=="FixedDirection");}
void CheckPoseControls(const SToolpathValue& value)
{const auto found=value.Channels.find("poseControls");if(found!=value.Channels.end()){const auto channel=found->second.To<Map>();const auto entries=channel.find("entries");if(entries!=channel.end())for(const auto& item:entries->second.To<Array>()){const auto point=item.To<Map>();if(Text(point,"set","active")=="active")Check(Text(point,"kind","Control")=="Reference","FIVEAXIS_INTENT_AUTHORED_POSE_KERF_UNSUPPORTED");}}const auto field=value.Channels.find("poseField");if(field!=value.Channels.end())Check(!Flag(field->second.To<Map>(),"authored",true),"FIVEAXIS_INTENT_AUTHORED_POSE_KERF_UNSUPPORTED");}
void ValidatePose(const Map& pose)
{
    Fields(pose,{"schema","schemaVersion","mode","frame","direction","initialization","transitionMetric","leadAngleRad","sideAngleRad","leadDeviationLimitRad","sideDeviationLimitRad","angularToleranceRad","surfaceNormalPreferenceWeight","axisReversed"});
    if(pose.contains("schema"))Check(Text(pose,"schema")=="cam.pose-policy","FIVEAXIS_INTENT_POSE_SCHEMA_UNSUPPORTED");if(pose.contains("schemaVersion"))Check(Numeric(pose.at("schemaVersion"))==1,"FIVEAXIS_INTENT_POSE_SCHEMA_UNSUPPORTED");
    const auto mode=Text(pose,"mode");Check(mode=="FixedDirection"||mode=="Continuous","CAM_INITIAL_FIXED_ROTARY_NOT_IMPLEMENTED");
    if(pose.contains("frame"))Check(Text(pose,"frame")=="Workpiece","CAM_POSE_FRAME_INVALID");
    if(Fixed(pose)){(void)Direction(pose.at("direction"));return;}
    Check(Text(pose,"initialization")=="SurfaceNormal"&&Number(pose,"leadAngleRad")==0&&Number(pose,"sideAngleRad")==0,"CAM_GENERATE_POSE_UNSUPPORTED");
}
Direction3 BaseAxis(const Map& pose,const CFiveAxisIntentOperationContext& context,const BoundaryFrame& frame)
{ValidatePose(pose);if(Fixed(pose))return Direction(pose.at("direction"));return Unit(Times(frame.Normal,context.OutwardSign()*(Flag(pose,"axisReversed")?-1:1)));}
std::string BoundaryRole(const SToolpathValue& value)
{
    auto feature=ReadFiveAxisIntentMetadata(value).Feature;auto role=Text(feature,"boundaryRole");
    for(std::size_t depth=0;role=="PatchBoundary"&&feature.contains("splitFromData");++depth){Check(depth<256,"CAM_CUT_SOURCE_MAP_MISMATCH");feature=Object(feature,"splitFromData");if(feature.contains("revision")&&feature.contains("value"))feature=Object(feature,"value");role=Text(feature,"boundaryRole");}
    Check(role=="Opening"||role=="OuterTrim","FIVEAXIS_INTENT_BOUNDARY_ROLE_UNSUPPORTED");return role;
}
bool Flip(const SToolpathValue& value)
{
    const auto side=Text(ReadFiveAxisIntentMetadata(value).Feature,"retainedSide"),role=BoundaryRole(value);
    Check(side=="Interior"||side=="Exterior","CAM_GENERATE_RETAINED_SIDE_REQUIRED");
    return (role=="Opening"&&side=="Interior")||(role=="OuterTrim"&&side=="Exterior");
}
OffsetCurvePiece3 Piece(const SEditableSegment& segment,std::size_t index)
{
    Check(segment.Parameterization=="CadNative","FIVEAXIS_INTENT_NATIVE_GEOMETRY_REQUIRED");OffsetCurvePiece3 output;output.SourceSegment=index;
    std::visit([&](const auto& exact){if constexpr(std::is_same_v<std::decay_t<decltype(exact)>,CurveSegment2>)throw std::invalid_argument("FIVEAXIS_INTENT_SPATIAL_GEOMETRY_REQUIRED");else output.Geometry=exact;},segment.Curve);return output;
}
CompositeOffsetCurve3 Composite(const SToolpathValue& value)
{CompositeOffsetCurve3 output;output.Closed=value.Geometry.Closed;for(std::size_t i=0;i<value.Geometry.Segments.size();++i)output.Segments.push_back(Piece(value.Geometry.Segments[i],i));return output;}
std::string GeometryFingerprint(const SFrozenPathInput& input){return ToolpathFingerprint(input.Value,{"geometry","topology","traversal","frame"});}
SEditableSegment Clip(const SFrozenPathInput& input,std::size_t index,ParameterRange range,const std::string& id,
    const std::string& geometryFingerprint,std::uint64_t occurrence=0)
{
    auto segment=input.Value.Geometry.Segments.at(index);segment.Id=id;SetRange(segment,range);
    std::vector<SSpanMapping> mappings;for(auto mapping:segment.Sources) {
        const auto lo=std::max(range.First,mapping.OutputRange.First),hi=std::min(range.Last,mapping.OutputRange.Last);if(hi<=lo+1e-12)continue;
        const ParameterRange overlap{lo,hi,range.Reversed};
        const auto map=[&](double parameter){const auto f=(parameter-Start(mapping.OutputRange))/(End(mapping.OutputRange)-Start(mapping.OutputRange));return Start(mapping.SourceRange)+f*(End(mapping.SourceRange)-Start(mapping.SourceRange));};
        const auto a=map(Start(overlap)),b=map(End(overlap));mapping.SourceRange={std::min(a,b),std::max(a,b),a>b};mapping.OutputRange=overlap;if(occurrence)mapping.Occurrence=occurrence;mappings.push_back(std::move(mapping));
    }
    segment.Sources=std::move(mappings);
    segment.Sources.push_back({input.Binding.Source,input.Value.Geometry.Segments[index].Id,range,range,"Exact",0,occurrence,geometryFingerprint});return segment;
}
SToolpathValue Whole(const SFrozenPathInput& input)
{auto value=input.Value;const auto fingerprint=GeometryFingerprint(input);for(std::size_t i=0;i<value.Geometry.Segments.size();++i)value.Geometry.Segments[i]=Clip(input,i,Range(value.Geometry.Segments[i]),value.Geometry.Segments[i].Id,fingerprint);return value;}
SOperationCandidate One(SToolpathValue value)
{SOutputCandidate output;output.RoleKey="main";output.InheritInput=0;output.Value=std::move(value);return {{std::move(output)}};}
std::vector<SPathRange> MissingRanges(const SFrozenPathInput& input,const SToolpathValue& output)
{
    std::vector<SPathRange> missing;
    for(const auto& original:input.Value.Geometry.Segments) {
        const auto domain=Range(original);std::vector<std::pair<double,double>> intervals;
        for(const auto& segment:output.Geometry.Segments)for(const auto& mapping:segment.Sources)if(mapping.Source==input.Binding.Source&&mapping.SourceSegmentId==original.Id)intervals.emplace_back(mapping.SourceRange.First,mapping.SourceRange.Last);
        std::sort(intervals.begin(),intervals.end());double cursor=domain.First;
        const auto add=[&](double a,double b){if(b-a>1e-11){SPathRange range{{input.Binding.Source,original.Id,a},{input.Binding.Source,original.Id,b},false};range.ExactSpans.push_back({input.Binding.Source,original.Id,{a,b,false},0});missing.push_back(std::move(range));}};
        for(auto [a,b]:intervals){if(a>cursor)add(cursor,a);cursor=std::max(cursor,b);}if(cursor<domain.Last)add(cursor,domain.Last);
    }
    return missing;
}
void ValidateKerf(const Map& policy)
{
    Fields(policy,{"owner","widthMm","miterLimit","measurementSection","cornerMode"});Check(Number(policy,"widthMm")>=0&&Number(policy,"miterLimit",4)>=1,"CAM_KERF_PARAMETERS_INVALID");
    const auto owner=Text(policy,"owner");Check(owner=="Cam"||owner=="Executor","CAM_KERF_OWNER_INVALID");const auto section=Text(policy,"measurementSection");Check(section=="Surface"||section=="BeamNormalSection"||section=="ThicknessSection","CAM_MEASUREMENT_SECTION_INVALID");
    if(Number(policy,"widthMm")>0){Check(owner=="Cam","CAM_GENERATE_EXECUTOR_COMPENSATION_UNSUPPORTED");Check(section!="ThicknessSection","CAM_GENERATE_THICKNESS_COMPENSATION_UNSUPPORTED");}
    const auto corner=Text(policy,"cornerMode");Check(corner=="Miter"||corner=="Bevel"||corner=="Round","CAM_KERF_CORNER_INVALID");
}
SOperationCandidate Kerf(const SOperationEvaluation& input,const IToolpathGeometryServices& geometry)
{
    const auto& context=Context(input);const auto& path=input.Inputs.at(0);const auto policy=Object(input.Parameters,"policy");ValidateKerf(policy);const auto width=Number(policy,"widthMm");
    if(width==0){auto value=Whole(path);value.Channels["fiveaxis.kerf"]=Map{{"schema",std::string("fiveaxis.kerf/1")},{"attachment",std::string("Path")},{"policy",policy},{"trimmedJoints",0u},{"addedConnectors",0u}};return One(std::move(value));}CheckPoseControls(path.Value);Check(path.Value.Geometry.Meaning=="NominalBoundary","FIVEAXIS_INTENT_COMPENSATION_ALREADY_APPLIED");const auto pose=Pose(path.Value);ValidatePose(pose);const auto flip=Flip(path.Value);std::vector<DirectionalCurveOffset3> laws;
    const bool geometryEdited=Flag(ReadFiveAxisIntentMetadata(path.Value).Feature,"geometryEdited");
    for(const auto& segment:path.Value.Geometry.Segments) {
        Cancel(input);Check(segment.Parameterization=="CadNative"&&std::holds_alternative<CurveSegment3>(segment.Curve),"FIVEAXIS_INTENT_KERF_SOURCE_UNSUPPORTED");const auto source=std::get<CurveSegment3>(segment.Curve);const auto u=(source.Range.First+source.Range.Last)*.5;
        const auto index=static_cast<std::size_t>(&segment-path.Value.Geometry.Segments.data());
        if(segment.ConstructionRole=="MergeConnector")
        {Check(!laws.empty(),"CAM_MERGE_CONNECTOR_SUPPORT_REQUIRED");CFiveAxisIntentPoseField field(context,path.Value);const auto a=field.NormalAt(index,source.Range.First),b=field.NormalAt(index,source.Range.Last);Check(Dot(a,b)>1-1e-10,"CAM_MERGE_CONNECTOR_NORMAL_VARIATION");DirectionalCurveOffset3 law;law.Source=source;law.ReferenceDirection=Fixed(pose)?Direction(pose.at("direction")):a;law.SignedDistance=laws.back().SignedDistance*(laws.back().Source.Range.Reversed?-1:1);laws.push_back(std::move(law));continue;}
        const auto query=Query(Resolve(context,segment,u));auto frame=CorrectFiveAxisFrame(path.Value,index,context.Boundary().Frame(context.Model(),query));const auto nominal=geometry.Evaluate(path.Value.Geometry,segment,u);
        if(geometryEdited){const auto sample=context.Offset().EvaluatePiece(Piece(segment,index),{u});Check(sample.Samples.size()==1,"CAM_PATH_FRAME_UNAVAILABLE");frame.Point=nominal;const auto d=sample.Samples.front().SourceDerivative;frame.Tangent=Unit({d.X,d.Y,d.Z});}
        else Check(Walk::Distance(nominal,frame.Point)<=path.Value.Geometry.Tolerance,"CAM_GENERATE_SOURCE_MISMATCH");
        const auto axis=Fixed(pose)?Direction(pose.at("direction")):BaseAxis(pose,context,frame);
        const auto& face=Record(context.Model().Faces,query.FaceId);const auto& wire=Record(context.Model().Wires,query.WireId);const auto occurrence=std::find(face.WireIds.begin(),face.WireIds.end(),wire.Id);Check(occurrence!=face.WireIds.end()&&query.CoedgeIndex<wire.Coedges.size(),"CAM_GENERATE_SOURCE_MISMATCH");
        const auto& coedge=wire.Coedges.at(query.CoedgeIndex);const auto faceSign=Sign(face.Orientation);const auto wireSign=face.WireOrientations.empty()?1:Sign(face.WireOrientations.at(occurrence-face.WireIds.begin()));
        const auto scrap=Times(Cross(frame.Tangent,Times(frame.Normal,faceSign)),Sign(coedge.Orientation)*wireSign*(flip?-1:1));const auto side=Dot(Unit(Cross(axis,frame.Tangent)),scrap);Check(std::abs(side)>=1e-8,"CAM_GENERATE_KERF_SIDE_AMBIGUOUS");
        DirectionalCurveOffset3 law;law.Source=source;law.SignedDistance=(side>0?1:-1)*width/2;
        if(Fixed(pose)){Check(Text(policy,"measurementSection")!="Surface"||std::abs(Dot(axis,frame.Normal))>=1-1e-9,"CAM_GENERATE_SURFACE_KERF_REQUIRES_NORMAL_AXIS");law.ReferenceDirection=axis;}
        else {const auto& surface=Record(context.Model().Surfaces3,face.Surface3Id).Geometry;if(std::holds_alternative<PlaneSurface3>(surface))law.ReferenceDirection=axis;
            else law.ReferenceDirection=SurfaceNormalField3{surface,Record(context.Model().Curves2,coedge.Curve2Id).Geometry,double(faceSign*context.OutwardSign()*(Flag(pose,"axisReversed")?-1:1))};}
        laws.push_back(std::move(law));
    }
    // Retain the established backend topology-construction tolerance. It is
    // distinct from the authority geometry's stricter endpoint-connectivity
    // tolerance, which the common geometry validator still enforces.
    CompositeOffsetOptions3 options;options.MiterLimit=Number(policy,"miterLimit",4);options.LoopRetention=EOffsetLoopRetention3::Start;
    options.Join=Text(policy,"cornerMode")=="Round"?EOffsetJoin3::Round:Text(policy,"cornerMode")=="Bevel"?EOffsetJoin3::Bevel:EOffsetJoin3::Miter;options.IsCancelled=[cancelled=input.Cancelled]{return cancelled&&cancelled->load();};
    const auto built=context.Offset().BuildComposite(laws,path.Value.Geometry.Closed,options);Cancel(input);Check(built.Status==ECompositeOffsetStatus3::Built,"CAM_GENERATE_COMPENSATION_FAILED: "+built.Diagnostic);
    // Preserve the old generation pipeline's bounded support/material checks.
    // These samples are validation evidence; the stored result remains the law.
    Walk::Evaluator verification(context.Offset(),options.IsCancelled);const auto checked=verification.SamplePoints(built.Curve,.02,2.);
    std::vector<BoundaryLeadInput> supportQueries;std::vector<std::size_t> supportSamples;
    for(std::size_t i=0;i<checked.Points.size();++i){Cancel(input);const auto& sample=checked.Points[i];const auto& piece=built.Curve.Segments.at(sample.PieceIndex);if(!piece.SourceSegment)continue;
        const auto sourceIndex=*piece.SourceSegment;if(path.Value.Geometry.Segments.at(sourceIndex).ConstructionRole=="MergeConnector")continue;
        supportQueries.push_back(Query(Resolve(context,path.Value.Geometry.Segments.at(sourceIndex),sample.Value.SourceParameter)));supportSamples.push_back(i);}
    // Bound each preparation batch so cancellation remains responsive even for
    // a long spatial contour; no geometry is cached across operations or models.
    constexpr std::size_t supportBatchSize=128;
    for(std::size_t begin=0;begin<supportQueries.size();begin+=supportBatchSize){Cancel(input);const auto end=std::min(begin+supportBatchSize,supportQueries.size());
        const std::vector<BoundaryLeadInput> queries(supportQueries.begin()+begin,supportQueries.begin()+end);const auto frames=context.Boundary().Frames(context.Model(),queries);
        Check(frames.size()==queries.size(),"CAM_GENERATE_SOURCE_MISMATCH");
        for(std::size_t at=begin;at<end;++at){Cancel(input);const auto& sample=checked.Points[supportSamples[at]];const auto sourceIndex=*built.Curve.Segments.at(sample.PieceIndex).SourceSegment;const auto& query=supportQueries[at];
        auto frame=CorrectFiveAxisFrame(path.Value,sourceIndex,frames[at-begin]);
        if(geometryEdited){frame.Point=sample.Value.SourcePoint;const auto d=sample.Value.SourceDerivative;frame.Tangent=Unit({d.X,d.Y,d.Z});}
        else Check(Walk::Distance(sample.Value.SourcePoint,frame.Point)<=path.Value.Geometry.Tolerance,"CAM_GENERATE_SOURCE_MISMATCH");
        const auto axis=Fixed(pose)?Direction(pose.at("direction")):BaseAxis(pose,context,frame);
        Check(Text(policy,"measurementSection")!="Surface"||std::abs(Dot(axis,frame.Normal))>=1-1e-9,"CAM_GENERATE_SURFACE_KERF_REQUIRES_NORMAL_AXIS");
        if(Fixed(pose)){const auto& face=Record(context.Model().Faces,query.FaceId);const auto& wire=Record(context.Model().Wires,query.WireId);const auto occurrence=std::find(face.WireIds.begin(),face.WireIds.end(),wire.Id);
            Check(occurrence!=face.WireIds.end()&&query.CoedgeIndex<wire.Coedges.size(),"CAM_GENERATE_SOURCE_MISMATCH");const auto wireSign=face.WireOrientations.empty()?1:Sign(face.WireOrientations.at(occurrence-face.WireIds.begin()));
            const auto scrap=Times(Cross(frame.Tangent,Times(frame.Normal,Sign(face.Orientation))),Sign(wire.Coedges[query.CoedgeIndex].Orientation)*wireSign*(flip?-1:1));
            Check(Dot(Cross(axis,frame.Tangent),scrap)*laws.at(sourceIndex).SignedDistance>1e-10,"CAM_GENERATE_KERF_SIDE_CHANGED");}}}
    std::vector<std::string> ids;std::map<std::size_t,std::size_t> counts,occurrences;for(const auto& piece:built.Curve.Segments)if(piece.SourceSegment)++counts[*piece.SourceSegment];
    for(std::size_t i=0;i<built.Curve.Segments.size();++i) {
        const auto& piece=built.Curve.Segments[i];if(piece.SourceSegment){auto id=path.Value.Geometry.Segments.at(*piece.SourceSegment).Id+"/kerf";if(counts[*piece.SourceSegment]>1)id+="/part/"+std::to_string(occurrences[*piece.SourceSegment]++);ids.push_back(std::move(id));}
        else {std::size_t before=i,after=i;for(std::size_t n=0;n<built.Curve.Segments.size();++n){before=before?before-1:built.Curve.Segments.size()-1;if(built.Curve.Segments[before].SourceSegment)break;}
            for(std::size_t n=0;n<built.Curve.Segments.size();++n){after=(after+1)%built.Curve.Segments.size();if(built.Curve.Segments[after].SourceSegment)break;}
            Check(built.Curve.Segments[before].SourceSegment&&built.Curve.Segments[after].SourceSegment,"FIVEAXIS_INTENT_OFFSET_CONNECTOR_ORIGIN_MISSING");const auto left=path.Value.Geometry.Segments.at(*built.Curve.Segments[before].SourceSegment).Id,right=path.Value.Geometry.Segments.at(*built.Curve.Segments[after].SourceSegment).Id;
            const auto count=(after+built.Curve.Segments.size()-before-1)%built.Curve.Segments.size(),part=(i+built.Curve.Segments.size()-before-1)%built.Curve.Segments.size();ids.push_back("kerf/connector/"+left+"/"+right+"/"+Text(policy,"cornerMode")+"/"+std::to_string(count)+"/"+std::to_string(part));}
    }
    SResolvedPath resolved;resolved.Port=path.Binding.Source;resolved.PathId=path.PathId;resolved.Value=path.Value;auto value=MakeFiveAxisOffsetIntentValue(built.Curve,resolved,ids);value.Channels=path.Value.Channels;
    // Native-u remains shared, so clipped ancestor CAD mappings remain exact.
    const auto fingerprint=GeometryFingerprint(path);
    for(std::size_t i=0;i<built.Curve.Segments.size();++i)if(built.Curve.Segments[i].SourceSegment){const auto index=*built.Curve.Segments[i].SourceSegment;auto inherited=Clip(path,index,Range(value.Geometry.Segments[i]),ids[i],fingerprint);value.Geometry.Segments[i].Sources=std::move(inherited.Sources);}
    value.Channels["fiveaxis.kerf"]=Map{{"schema",std::string("fiveaxis.kerf/1")},{"attachment",std::string("Path")},{"policy",policy},{"trimmedJoints",static_cast<unsigned long long>(built.TrimmedJoints)},{"addedConnectors",static_cast<unsigned long long>(built.AddedConnectors)}};
    value.Channels["poseField"]=BuildFiveAxisIntentPoseField(context,value,DeclaredToolpathPort(input.OperationId,"main"));
    auto result=One(std::move(value));result.ExcludedRanges=MissingRanges(path,result.Outputs[0].Value);Array removed;
    for(const auto& loop:built.RemovedLoops){Persistence::Writer writer;writer.Write(loop);std::string bytes;constexpr char hex[]="0123456789abcdef";for(auto byte:writer.Bytes){bytes+=hex[byte>>4];bytes+=hex[byte&15];}removed.emplace_back(bytes);}
    result.Constraints["removedOffsetLoops"]=removed;result.Constraints["offsetTopologyToleranceMm"]=options.Tolerance;return result;
}
struct Located final{Walk::Location Location;bool Adjusted=false;};
Located LocateStart(const SFrozenPathInput& input,const Map& parameters)
{
    if(!parameters.contains("start"))return {{0,Start(Range(input.Value.Geometry.Segments.front()))},false};const auto anchor=AnchorFromData(Object(parameters,"start"));std::vector<Located> exact,clipped;double nearest=std::numeric_limits<double>::infinity();
    for(std::size_t i=0;i<input.Value.Geometry.Segments.size();++i){const auto& segment=input.Value.Geometry.Segments[i];const auto range=Range(segment);
        if(anchor.Port==input.Binding.Source&&anchor.SegmentId==segment.Id&&anchor.Parameter>=range.First&&anchor.Parameter<=range.Last)exact.push_back({{i,anchor.Parameter},false});
        for(const auto& mapping:segment.Sources)if(mapping.Source==anchor.Port&&mapping.SourceSegmentId==anchor.SegmentId&&mapping.Occurrence==anchor.Occurrence){Check(mapping.Precision=="Exact","FIVEAXIS_INTENT_START_MAPPING_UNSUPPORTED");const auto u=std::clamp(anchor.Parameter,mapping.SourceRange.First,mapping.SourceRange.Last);
            const auto f=(u-Start(mapping.SourceRange))/(End(mapping.SourceRange)-Start(mapping.SourceRange)),parameter=Start(mapping.OutputRange)+f*(End(mapping.OutputRange)-Start(mapping.OutputRange));
            if(u==anchor.Parameter)exact.push_back({{i,parameter},false});else if(Flag(parameters,"allowTrimmedStart")){const auto distance=std::abs(u-anchor.Parameter);if(distance<nearest-1e-12){nearest=distance;clipped.clear();}if(std::abs(distance-nearest)<=1e-12)clipped.push_back({{i,parameter},true});}}
    }
    auto& candidates=exact.empty()?clipped:exact;std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.Location.PieceIndex!=b.Location.PieceIndex?a.Location.PieceIndex<b.Location.PieceIndex:a.Location.Parameter<b.Location.Parameter;});
    candidates.erase(std::unique(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.Location.PieceIndex==b.Location.PieceIndex&&std::abs(a.Location.Parameter-b.Location.Parameter)<1e-12;}),candidates.end());Check(candidates.size()==1,"TOOLPATH_NEEDS_REBIND_ANCHOR");return candidates.front();
}
void ValidateTrace(const Map& parameters)
{const auto gap=Number(parameters,"gapLengthMm"),overcut=Number(parameters,"overcutLengthMm");Check(gap>=0&&overcut>=0&&gap<=100000&&overcut<=100000&&!(gap>0&&overcut>0),"CAM_LEAD_CLOSURE_CONFLICT");if(parameters.contains("start"))(void)AnchorFromData(Object(parameters,"start"));}
SOperationCandidate Trace(const SOperationEvaluation& input,const IToolpathGeometryServices&)
{
    const auto& context=Context(input);const auto& path=input.Inputs.at(0);ValidateTrace(input.Parameters);const auto gap=Number(input.Parameters,"gapLengthMm"),overcut=Number(input.Parameters,"overcutLengthMm");
    if(!path.Value.Geometry.Closed){Check(gap==0&&overcut==0&&!input.Parameters.contains("start"),"CAM_LEAD_CLOSURE_REQUIRES_CLOSED_CURVE");auto value=Whole(path);value.Channels["fiveaxis.trace"]=Map{{"schema",std::string("fiveaxis.trace/1")},{"attachment",std::string("Path")},{"gapLengthMm",gap},{"overcutLengthMm",overcut},{"startAdjusted",false}};return One(std::move(value));}
    const auto exact=Composite(path.Value);Walk::Evaluator evaluator(context.Offset(),[cancelled=input.Cancelled]{return cancelled&&cancelled->load();});const auto perimeter=evaluator.Length(exact);Check(gap<perimeter-1e-6,"CAM_LEAD_GAP_EXCEEDS_CONTOUR");const auto start=LocateStart(path,input.Parameters);
    const auto traced=evaluator.Trace(exact,start.Location,perimeter-gap+overcut,false);Cancel(input);auto value=path.Value;value.Geometry.Segments.clear();value.Geometry.Closed=gap==0&&overcut==0;
    std::map<std::size_t,std::vector<std::vector<std::pair<double,double>>>> visits;bool repeated=false;const auto fingerprint=GeometryFingerprint(path);
    for(const auto& piece:traced.Segments){Check(piece.SourceSegment.has_value(),"FIVEAXIS_INTENT_TRACE_SOURCE_MISSING");const auto index=*piece.SourceSegment;const auto range=Walk::Range(piece);auto& occurrences=visits[index];std::size_t occurrence=0;
        for(;occurrence<occurrences.size();++occurrence)if(std::none_of(occurrences[occurrence].begin(),occurrences[occurrence].end(),[&](const auto& previous){return std::min(range.Last,previous.second)>std::max(range.First,previous.first)+1e-11;}))break;
        if(occurrence==occurrences.size())occurrences.emplace_back();occurrences[occurrence].emplace_back(range.First,range.Last);repeated|=occurrence>0;const auto original=Range(path.Value.Geometry.Segments[index]);
        const auto id=path.Value.Geometry.Segments[index].Id+"/trace/"+(range.First==original.First?"source-first":"selected-first")+"/"+(range.Last==original.Last?"source-last":"selected-last")+"/occurrence/"+std::to_string(occurrence);value.Geometry.Segments.push_back(Clip(path,index,range,id,fingerprint,occurrence));}
    value.Channels["fiveaxis.trace"]=Map{{"schema",std::string("fiveaxis.trace/1")},{"attachment",std::string("Path")},{"gapLengthMm",gap},{"overcutLengthMm",overcut},{"perimeterMm",perimeter},{"startAdjusted",start.Adjusted}};
    auto result=One(std::move(value));result.AllowRepeatedCoverage=repeated;result.ExcludedRanges=MissingRanges(path,result.Outputs[0].Value);Array retained;for(const auto& range:result.ExcludedRanges)retained.emplace_back(ToData(range));result.Constraints["retainedIntervals"]=retained;return result;
}
void ValidateLead(const Map& policy)
{
    Fields(policy,{"kind","placement","lengthMm","angleRad","radiusMm"});const auto kind=Text(policy,"kind");const auto length=Number(policy,"lengthMm"),angle=Number(policy,"angleRad");Check((kind=="None"&&length==0)||((kind=="Line"||kind=="Arc")&&length>0&&length<=100000),"CAM_LEAD_INVALID");
    const auto placement=Text(policy,"placement");Check(placement=="ScrapSide"||placement=="OutsideStock","CAM_LEAD_PLACEMENT_INVALID");Check(std::abs(angle)<=std::numbers::pi,"CAM_LEAD_ANGLE_INVALID");
    if(kind=="None"){Check(!policy.contains("radiusMm"),"CAM_LEAD_RADIUS_NOT_APPLICABLE");return;}
    Check(placement=="ScrapSide","CAM_LEAD_OUTSIDE_NOT_IMPLEMENTED");Check(std::abs(angle)<std::numbers::pi/2,"LEAD_PARAMETERS_INVALID");if(kind=="Arc")Check(Number(policy,"radiusMm")>0&&Number(policy,"radiusMm")<=100000&&length/Number(policy,"radiusMm")<=std::numbers::pi&&angle==0,"LEAD_ARC_PARAMETERS_INVALID");else Check(!policy.contains("radiusMm"),"CAM_LEAD_RADIUS_NOT_APPLICABLE");
}
std::optional<CompositeCurve3> CurrentHoleBoundary(const SToolpathValue& value)
{
    const auto hole=Object(ReadFiveAxisIntentMetadata(value).Feature,"standardHole");if(!hole.contains("center")||!hole.contains("xDirection"))return {};
    const auto a=hole.at("center").To<Array>();const Point3 center{Numeric(a[0]),Numeric(a[1]),Numeric(a[2])};
    const auto x=Direction(hole.at("xDirection")),z=Direction(hole.contains("normal")?hole.at("normal"):hole.at("direction")),y=Cross(z,x);
    const auto point=[&](double u,double v){return Point3{center.X+u*x.X+v*y.X,center.Y+u*x.Y+v*y.Y,center.Z+u*x.Z+v*y.Z};};
    CompositeCurve3 result;result.Closed=true;const auto line=[&](Point3 from,Point3 to){const auto length=Walk::Distance(from,to);result.Segments.push_back({Line3{{from,Unit({to.X-from.X,to.Y-from.Y,to.Z-from.Z})}},{0,length,false}});};
    const auto kind=Text(hole,"kind");Placement3 placement;placement.Location=center;placement.XDirection=x;placement.YDirection=y;placement.ZDirection=z;
    if(kind=="Circle")result.Segments.push_back({Circle3{placement,Number(hole,"diameterMm")/2},{0,2*std::numbers::pi,false}});
    else if(kind=="Ellipse")result.Segments.push_back({Ellipse3{placement,Number(hole,"majorDiameterMm")/2,Number(hole,"minorDiameterMm")/2},{0,2*std::numbers::pi,false}});
    else if(kind=="Rectangle"){const auto w=Number(hole,"widthMm")/2,l=Number(hole,"lengthMm")/2;line(point(-w,-l),point(w,-l));line(point(w,-l),point(w,l));line(point(w,l),point(-w,l));line(point(-w,l),point(-w,-l));}
    else if(kind=="Slot"){const auto r=Number(hole,"widthMm")/2,h=(Number(hole,"lengthMm")-2*r)/2;line(point(-h,-r),point(h,-r));placement.Location=point(h,0);result.Segments.push_back({Circle3{placement,r},{-std::numbers::pi/2,std::numbers::pi/2,false}});line(point(h,r),point(-h,r));placement.Location=point(-h,0);result.Segments.push_back({Circle3{placement,r},{std::numbers::pi/2,3*std::numbers::pi/2,false}});}
    else return {};return result;
}
SOperationCandidate Leads(const SOperationEvaluation& input,const IToolpathGeometryServices&)
{
    const auto& context=Context(input);const auto& path=input.Inputs.at(0);const auto leadIn=Object(input.Parameters,"leadIn"),leadOut=Object(input.Parameters,"leadOut");ValidateLead(leadIn);ValidateLead(leadOut);
    if(Text(leadIn,"kind")=="None"&&Text(leadOut,"kind")=="None"){auto value=Whole(path);value.Channels["fiveaxis.leads"]=Map{{"schema",std::string("fiveaxis.leads/1")},{"attachment",std::string("Path")},{"leadIn",leadIn},{"leadOut",leadOut}};return One(std::move(value));}
    Check(Text(ReadFiveAxisIntentMetadata(path.Value).Feature,"machiningKind")=="Cut","CAM_LEADS_REQUIRE_CUTTING_PATH");
    auto value=Whole(path);std::vector<SEditableSegment> entry,exit;auto pose=Channel(path.Value,"pose");auto constructed=Object(pose,"constructedDirections"),normals=Object(pose,"constructedNormals");const bool flip=Flip(path.Value);
    for(bool entering:{true,false}){Cancel(input);const auto& policy=entering?leadIn:leadOut;const auto kind=Text(policy,"kind");if(kind=="None")continue;
        const auto index=entering?0:path.Value.Geometry.Segments.size()-1;const auto& segment=path.Value.Geometry.Segments[index];const auto native=Range(segment);const auto parameter=entering?Start(native):End(native);const auto evaluated=context.Offset().EvaluatePiece(Piece(segment,index),{parameter});Check(evaluated.Status==ECurveOffsetStatus::SamplesEvaluated&&evaluated.Samples.size()==1,"FIVEAXIS_INTENT_LEAD_ENDPOINT_UNAVAILABLE");
        const auto endpoint=evaluated.Samples.front();auto tangent=Unit({endpoint.OffsetDerivative.X,endpoint.OffsetDerivative.Y,endpoint.OffsetDerivative.Z});if(native.Reversed)tangent=Times(tangent,-1);
        std::optional<Bound> bound;std::size_t supportIndex=index;double supportParameter=parameter;
        for(std::size_t step=0;step<path.Value.Geometry.Segments.size();++step){const auto at=entering?std::min(index+step,path.Value.Geometry.Segments.size()-1):(index>=step?index-step:0);const auto& support=path.Value.Geometry.Segments[at];const auto range=Range(support);const auto u=entering?Start(range):End(range);
            try{bound=Resolve(context,support,u);supportIndex=at;supportParameter=u;break;}catch(const std::invalid_argument& error){if(std::string(error.what())!="FIVEAXIS_INTENT_CAD_BINDING_AMBIGUOUS_OR_MISSING")throw;}}
        Check(bound.has_value(),"CAM_GENERATE_LEAD_SOURCE_MISSING");auto query=Query(*bound);auto frame=CorrectFiveAxisFrame(path.Value,supportIndex,context.Boundary().Frame(context.Model(),query));
        if(Flag(ReadFiveAxisIntentMetadata(path.Value).Feature,"geometryEdited")){const auto d=endpoint.SourceDerivative;frame.Tangent=Unit({d.X,d.Y,d.Z});}
        const auto axis=EvaluateFiveAxisIntentDirection(context,path.Value,supportIndex,supportParameter);
        const auto& face=Record(context.Model().Faces,query.FaceId);const auto& wire=Record(context.Model().Wires,query.WireId);const auto position=std::find(face.WireIds.begin(),face.WireIds.end(),wire.Id);Check(position!=face.WireIds.end()&&query.CoedgeIndex<wire.Coedges.size(),"CAM_GENERATE_SOURCE_MISMATCH");const auto wireSign=face.WireOrientations.empty()?1:Sign(face.WireOrientations.at(position-face.WireIds.begin()));
        auto scrap=Times(Cross(frame.Tangent,Times(frame.Normal,Sign(face.Orientation))),Sign(wire.Coedges[query.CoedgeIndex].Orientation)*wireSign*(flip?-1:1));
        if(path.Value.Geometry.Meaning=="ToolCenter"){auto across=Unit(Cross(axis,tangent));Check(std::abs(Dot(across,scrap))>1e-10,"CAM_GENERATE_KERF_SIDE_AMBIGUOUS");scrap=Times(across,Dot(across,scrap)>0?1:-1);}
        else {const auto projection=Dot(scrap,tangent);scrap=Unit({scrap.X-projection*tangent.X,scrap.Y-projection*tangent.Y,scrap.Z-projection*tangent.Z});}
        query.Entry=entering;query.Arc=kind=="Arc";query.Length=Number(policy,"lengthMm");query.Angle=Number(policy,"angleRad");query.Radius=Number(policy,"radiusMm");query.FlipSide=flip;query.RequireInsideWire=Text(ReadFiveAxisIntentMetadata(path.Value).Feature,"retainedSide")=="Exterior";query.RequireOutsideWire=!query.RequireInsideWire;query.RegionBoundary=CurrentHoleBoundary(path.Value);if(query.RegionBoundary)query.RegionPoint=endpoint.SourcePoint;query.Anchor=BoundaryLeadAnchor{endpoint.OffsetPoint,tangent,scrap};const auto built=context.Boundary().Build(context.Model(),query);auto& segments=entering?entry:exit;
        for(std::size_t i=0;i<built.Curve.Segments.size();++i){SEditableSegment lead;lead.Id=segment.Id+(entering?"/lead-in/":"/lead-out/")+kind+"/"+std::to_string(i);lead.Curve=built.Curve.Segments[i];lead.Parameterization="CadNative";lead.ConstructionRole=entering?"LeadIn":"LeadOut";constructed[lead.Id]=Point(axis);normals[lead.Id]=Point(Times(frame.Normal,context.OutwardSign()));segments.push_back(std::move(lead));}}
    value.Geometry.Segments.insert(value.Geometry.Segments.begin(),entry.begin(),entry.end());value.Geometry.Segments.insert(value.Geometry.Segments.end(),exit.begin(),exit.end());if(!entry.empty()||!exit.empty())value.Geometry.Closed=false;
    pose["constructedDirections"]=constructed;pose["constructedNormals"]=normals;value.Channels["pose"]=pose;value.Channels["fiveaxis.leads"]=Map{{"schema",std::string("fiveaxis.leads/1")},{"attachment",std::string("Path")},{"leadIn",leadIn},{"leadOut",leadOut}};return One(std::move(value));
}
Map Field(const char* type,bool required=true){return {{"type",std::string(type)},{"required",required}};}
Map ContourPolicy(const SToolpathValue& value)
{
    const auto saved=Object(ReadFiveAxisIntentMetadata(value).Feature,"contourPoseData");Check(saved.contains("basis")&&!Text(saved,"controlsSet").empty(),"CAM_CONTOUR_POSE_INVALID");const auto policy=Object(saved,"basis");Check(Text(policy,"mode")=="Continuous","CAM_CONTOUR_POSE_INVALID");return policy;
}
void CopyControlSet(SToolpathValue& value,const std::string& from,const std::string& to)
{
    if(!value.Channels.contains("poseControls"))return;auto channel=value.Channels.at("poseControls").To<Map>();const auto original=channel.at("entries").To<Array>();Array entries;for(const auto& item:original)if(Text(item.To<Map>(),"set","active")!=to)entries.push_back(item);for(const auto& item:original){auto entry=item.To<Map>();if(Text(entry,"set","active")!=from)continue;entry["set"]=to;entries.emplace_back(std::move(entry));}channel["entries"]=std::move(entries);value.Channels["poseControls"]=std::move(channel);
}
void ActivateContour(const CFiveAxisIntentOperationContext& context,SToolpathValue& value,const SPortRef& port)
{
    Check(value.Channels.contains("contourPoseField"),"FIVEAXIS_INTENT_CONTOUR_FIELD_REQUIRED");auto field=value.Channels.at("contourPoseField").To<Map>();Check(value.Geometry.Meaning!="ToolCenter"||!Flag(field,"authored"),"CAM_POSE_MANUAL_KERF_UNSUPPORTED");const auto sourceSet=Text(field,"set","contour");const auto policy=field.contains("basisPolicy")?Object(field,"basisPolicy"):ContourPolicy(value);field["set"]=std::string("active");value.Channels["poseField"]=field;auto pose=value.Channels.contains("pose")?value.Channels.at("pose").To<Map>():Map{{"schema",std::string("fiveaxis.pose/1")},{"attachment",std::string("Path")}};pose["policy"]=policy;value.Channels["pose"]=pose;CopyControlSet(value,sourceSet,"active");
    // An un-authored normal/fixed basis has no interpolation history to keep.
    // Recompile it on the current exact pieces so offset connectors added after
    // the saved source contour also receive their precise CAD support basis.
    if(!Flag(field,"authored"))value.Channels["poseField"]=BuildFiveAxisIntentPoseField(context,value,port);
}
void RefreshConstructedPose(const CFiveAxisIntentOperationContext& context,SToolpathValue& value)
{
    std::vector<std::size_t> cutting;for(std::size_t i=0;i<value.Geometry.Segments.size();++i){const auto& role=value.Geometry.Segments[i].ConstructionRole;if(role!="LeadIn"&&role!="LeadOut"&&role!="Lead")cutting.push_back(i);}auto pose=Channel(value,"pose");const auto oldNormals=Object(pose,"constructedNormals");Map directions,normals;CFiveAxisIntentPoseField field(context,value);const auto policy=Object(pose,"policy");
    for(const auto& segment:value.Geometry.Segments){const auto& role=segment.ConstructionRole;if(role!="LeadIn"&&role!="LeadOut"&&role!="Lead")continue;Direction3 axis,normal;if(!cutting.empty()){const auto index=role=="LeadOut"?cutting.back():cutting.front();const auto range=Range(value.Geometry.Segments[index]);const auto parameter=role=="LeadOut"?End(range):Start(range);axis=field.At(index,parameter);normal=field.NormalAt(index,parameter);}else {std::vector<Direction3> support;const auto add=[&](const std::string& id){const auto found=oldNormals.find(id);if(found!=oldNormals.end())support.push_back(Direction(found->second));};add(segment.Id);for(const auto& source:segment.Sources)add(source.SourceSegmentId);Check(!support.empty(),"FIVEAXIS_INTENT_CONSTRUCTED_NORMAL_REQUIRED");normal=support.front();axis=Fixed(policy)?Direction(policy.at("direction")):Times(normal,Flag(policy,"axisReversed")?-1:1);}directions[segment.Id]=Point(axis);normals[segment.Id]=Point(normal);}pose["constructedDirections"]=directions;pose["constructedNormals"]=normals;value.Channels["pose"]=pose;
}
SOperationCandidate PoseOperation(const SOperationEvaluation& input,const IToolpathGeometryServices&)
{
    const auto& context=Context(input);const auto& source=input.Inputs.at(0);const auto policy=Object(input.Parameters,"policy");ValidatePose(policy);auto value=Whole(source);auto pose=value.Channels.contains("pose")?value.Channels.at("pose").To<Map>():Map{{"schema",std::string("fiveaxis.pose/1")},{"attachment",std::string("Path")}};pose["policy"]=policy;value.Channels["pose"]=pose;if(Flag(input.Parameters,"clearControls")&&value.Channels.contains("poseControls")){auto controls=value.Channels.at("poseControls").To<Map>();auto entries=controls.at("entries").To<Array>();std::erase_if(entries,[](const auto& item){return Text(item.To<Map>(),"set","active")=="active";});controls["entries"]=std::move(entries);value.Channels["poseControls"]=std::move(controls);}value.Channels["poseField"]=BuildFiveAxisIntentPoseField(context,value,source.Binding.Source);
    if(!value.Channels.contains("contourPoseField")&&ReadFiveAxisIntentMetadata(value).Feature.contains("contourPoseData")){auto field=BuildFiveAxisIntentPoseField(context,value,source.Binding.Source,"contour");field["basisPolicy"]=ContourPolicy(value);value.Channels["contourPoseField"]=std::move(field);}RefreshConstructedPose(context,value);return One(std::move(value));
}
SOperationCandidate CutTypeOperation(const SOperationEvaluation& input,const IToolpathGeometryServices&)
{
    const auto& context=Context(input);const auto& source=input.Inputs.at(0);auto value=Whole(source);auto metadata=ReadFiveAxisIntentMetadata(value);const auto type=Text(input.Parameters,"cutType");Check(type=="Contour"||type=="StandardHole","CAM_CUT_TYPE_INVALID");
    if(input.Parameters.contains("standardHole")){
        Check(type=="StandardHole","CAM_CUT_TYPE_INVALID");const auto hole=Object(input.Parameters,"standardHole");const auto kind=Text(hole,"kind");
        Check(Flag(hole,"manual")&&(kind=="Circle"||kind=="Ellipse"),"CAM_MANUAL_HOLE_KIND_INVALID");
        (void)Direction(hole.at("direction"));(void)Direction(hole.at("xDirection"));
        Check(std::abs(Dot(Direction(hole.at("direction")),Direction(hole.at("xDirection"))))<1e-8,"CAM_MANUAL_HOLE_FRAME_INVALID");
        const auto major=Number(hole,kind=="Circle"?"diameterMm":"majorDiameterMm"),minor=kind=="Circle"?major:Number(hole,"minorDiameterMm");
        Check(major>1e-6&&major<=1e6&&minor>1e-6&&minor<=major,"CAM_HOLE_DIMENSIONS_INVALID");
        metadata.Feature["standardHole"]=hole;metadata.Feature["geometryKind"]=kind;
        metadata.Feature["boundaryRole"]=std::string("Opening");metadata.Feature["retainedSide"]=std::string("Exterior");
    }
    if(type=="Contour")ActivateContour(context,value,source.Binding.Source);
    else {Check(metadata.Feature.contains("standardHole")&&(Flag(Object(metadata.Feature,"standardHole"),"manual")||!metadata.Feature.contains("splitFromData")),"CAM_CUT_NOT_ORIGINAL_STANDARD_HOLE");const auto policy=Object(input.Parameters,"policy");ValidatePose(policy);Check(Text(policy,"mode")=="FixedDirection","CAM_STANDARD_HOLE_POSE_INVALID");const auto previous=Pose(value);if(Text(previous,"mode")!="FixedDirection"){auto field=value.Channels.contains("poseField")?value.Channels.at("poseField").To<Map>():BuildFiveAxisIntentPoseField(context,value,source.Binding.Source);field["set"]=std::string("contour");field["basisPolicy"]=previous;value.Channels["contourPoseField"]=field;CopyControlSet(value,"active","contour");metadata.Feature["contourPoseData"]=Map{{"basis",previous},{"controlsSet",std::string("contour")}};}auto pose=Channel(value,"pose");pose["policy"]=policy;value.Channels["pose"]=pose;auto controls=value.Channels.contains("poseControls")?value.Channels.at("poseControls").To<Map>():Map{};if(!controls.empty()){auto entries=controls.at("entries").To<Array>();std::erase_if(entries,[](const auto& entry){return Text(entry.To<Map>(),"set","active")=="active";});controls["entries"]=entries;value.Channels["poseControls"]=controls;}value.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);value.Channels["poseField"]=BuildFiveAxisIntentPoseField(context,value,source.Binding.Source);}
    RefreshConstructedPose(context,value);return One(std::move(value));
}
void EraseControlGroups(Array& entries,const std::set<std::string>& groups)
{std::erase_if(entries,[&](const auto& item){const auto point=item.To<Map>();return groups.contains(Text(point,"id"))||point.contains("driver")&&groups.contains(Text(Object(point,"driver"),"id"));});}
SOperationCandidate PoseRemoveOperation(const SOperationEvaluation&,const IToolpathGeometryServices&);
SOperationCandidate PoseDeltaOperation(const SOperationEvaluation& input,const IToolpathGeometryServices& geometry)
{
    // A pending authoring layer can replace or remove several controls before
    // any interpolation is requested. Materialize that layer only in generation.
    if(input.Parameters.contains("removedIds")){
        auto parameters=input.Parameters;const auto ids=parameters.at("removedIds").To<Array>();parameters.erase("removedIds");
        if(!ids.empty()){
            auto removal=input;removal.Parameters={{"ids",ids}};auto removed=PoseRemoveOperation(removal,geometry);
            if(parameters.at("controls").To<Array>().empty())return removed;
            auto update=input;update.Parameters=std::move(parameters);update.Inputs.at(0).Value=std::move(removed.Outputs.at(0).Value);
            update.Inputs.at(0).Fingerprint=ToolpathFingerprint(update.Inputs.at(0).Value);
            return PoseDeltaOperation(update,geometry);
        }
    }
    Check(input.Inputs.at(0).Value.Geometry.Meaning!="ToolCenter","CAM_POSE_MANUAL_KERF_UNSUPPORTED");
    const auto& context=Context(input);const auto& source=input.Inputs.at(0);const auto controls=input.Parameters.at("controls").To<Array>();auto value=Whole(source);value.Channels["poseField"]=BuildFiveAxisIntentPoseDelta(context,source.Value,source.Binding.Source,controls,"active");auto channel=value.Channels.contains("poseControls")?value.Channels.at("poseControls").To<Map>():Map{{"schema",std::string("fiveaxis.pose-controls/1")},{"schemaVersion",1u},{"attachment",std::string("Points")},{"entries",Array{}}};auto entries=channel.at("entries").To<Array>();std::set<std::string> replacing;for(const auto& item:controls){const auto point=item.To<Map>();if(Text(point,"kind","Control")=="Control")replacing.insert(Text(point,"id"));}EraseControlGroups(entries,replacing);for(const auto& item:controls){Check(!Text(item.To<Map>(),"id").empty(),"FIVEAXIS_INTENT_POSE_POINT_ID_INVALID");entries.push_back(item);}channel["entries"]=entries;value.Channels["poseControls"]=channel;RefreshConstructedPose(context,value);return One(std::move(value));
}
SOperationCandidate PoseRemoveOperation(const SOperationEvaluation& input,const IToolpathGeometryServices&)
{
    Check(input.Inputs.at(0).Value.Geometry.Meaning!="ToolCenter","CAM_POSE_MANUAL_KERF_UNSUPPORTED");
    const auto& context=Context(input);const auto& source=input.Inputs.at(0);std::set<std::string> ids;for(const auto& item:input.Parameters.at("ids").To<Array>()){const auto id=item.To<std::string>();Check(!id.empty()&&ids.insert(id).second,"FIVEAXIS_INTENT_POSE_POINT_ID_INVALID");}Check(!ids.empty(),"FIVEAXIS_INTENT_POSE_EDITS_REQUIRED");auto value=Whole(source);value.Channels["poseField"]=RemoveFiveAxisIntentPoseControls(context,source.Value,source.Binding.Source,ids);if(value.Channels.contains("poseControls")){auto channel=value.Channels.at("poseControls").To<Map>();auto entries=channel.at("entries").To<Array>();EraseControlGroups(entries,ids);channel["entries"]=entries;value.Channels["poseControls"]=channel;}RefreshConstructedPose(context,value);return One(std::move(value));
}
SOperationCandidate PosePolicyOperation(const SOperationEvaluation& input,const IToolpathGeometryServices&)
{const auto& context=Context(input);const auto& source=input.Inputs.at(0);const auto stored=Object(input.Parameters,"policy");ValidatePose(stored);auto policy=Pose(source.Value);for(const auto* key:{"transitionMetric","leadDeviationLimitRad","sideDeviationLimitRad","angularToleranceRad","surfaceNormalPreferenceWeight"})if(stored.contains(key))policy[key]=stored.at(key);auto value=Whole(source);value.Channels["poseField"]=UpdateFiveAxisIntentPoseFieldPolicy(context,source.Value,source.Binding.Source,policy);auto pose=Channel(value,"pose");pose["policy"]=policy;value.Channels["pose"]=pose;return One(std::move(value));}
SOperationCandidate PoseReverseOperation(const SOperationEvaluation& input,const IToolpathGeometryServices&)
{
    const auto& context=Context(input);const auto& source=input.Inputs.at(0);auto value=Whole(source);value.Channels["poseField"]=ReverseFiveAxisIntentPoseField(context,source.Value,source.Binding.Source);auto pose=value.Channels.contains("pose")?value.Channels.at("pose").To<Map>():Map{{"schema",std::string("fiveaxis.pose/1")},{"attachment",std::string("Path")},{"policy",Pose(value)}};auto policy=Object(pose,"policy");if(Fixed(policy))policy["direction"]=Point(Times(Direction(policy.at("direction")),-1));else policy["axisReversed"]=!Flag(policy,"axisReversed");pose["policy"]=policy;auto constructed=Object(pose,"constructedDirections");for(auto& [_,direction]:constructed)direction=Point(Times(Direction(direction),-1));pose["constructedDirections"]=constructed;value.Channels["pose"]=pose;if(value.Channels.contains("poseControls")){auto channel=value.Channels.at("poseControls").To<Map>();auto entries=channel.at("entries").To<Array>();for(auto& item:entries){auto point=item.To<Map>();for(const auto* key:{"direction","referenceDirection"})if(point.contains(key))point[key]=Point(Times(Direction(point.at(key)),-1));if(point.contains("driver")){auto driver=Object(point,"driver");for(const auto* key:{"direction","referenceDirection"})driver[key]=Point(Times(Direction(driver.at(key)),-1));point["driver"]=driver;}item=point;}channel["entries"]=entries;value.Channels["poseControls"]=channel;}return One(std::move(value));
}
SOperationCandidate SplitOperation(const SOperationEvaluation& input,const IToolpathGeometryServices& geometry,const CToolpathOperationRegistry& generic)
{
    auto prepared=input;for(auto& path:prepared.Inputs){if(Text(Pose(path.Value),"mode")=="FixedDirection")ActivateContour(Context(input),path.Value,path.Binding.Source);path.Value.Geometry.Closed=false;path.Fingerprint=ToolpathFingerprint(path.Value);}auto result=EvaluateFrozenToolpathOperation(generic,geometry,"icax.edit.split",prepared);
    for(auto& output:result.Outputs){for(auto& segment:output.Value.Geometry.Segments)for(auto& source:segment.Sources)for(const auto& original:input.Inputs)if(source.Source==original.Binding.Source)source.SourceGeometryFingerprint=ToolpathFingerprint(original.Value,{"geometry","topology","traversal","frame"});auto metadata=ReadFiveAxisIntentMetadata(output.Value);const Map none{{"kind",std::string("None")},{"placement",std::string("ScrapSide")},{"lengthMm",0.},{"angleRad",0.}};auto leads=output.Value.Channels.contains("fiveaxis.leads")?output.Value.Channels.at("fiveaxis.leads").To<Map>():Map{{"schema",std::string("fiveaxis.leads/1")},{"attachment",std::string("Path")},{"leadIn",none},{"leadOut",none}};for(const auto& [role,key]:std::vector<std::pair<std::string,std::string>>{{"LeadIn","leadIn"},{"LeadOut","leadOut"}})if(std::none_of(output.Value.Geometry.Segments.begin(),output.Value.Geometry.Segments.end(),[&](const auto& segment){return segment.ConstructionRole==role;}))leads[key]=none;output.Value.Channels["fiveaxis.leads"]=leads;output.Value.Channels["fiveaxis.trace"]=Map{{"schema",std::string("fiveaxis.trace/1")},{"attachment",std::string("Path")},{"gapLengthMm",0.},{"overcutLengthMm",0.},{"startAdjusted",false}};metadata.Process["gapLengthMm"]=0.;metadata.Process["overcutLengthMm"]=0.;metadata.Process["leadIn"]=leads.at("leadIn");metadata.Process["leadOut"]=leads.at("leadOut");if(Text(metadata.Feature,"boundaryRole")!="PatchBoundary")metadata.Feature["splitFromData"]=metadata.Feature;metadata.Feature["geometryKind"]=std::string("GeneralContour");metadata.Feature["boundaryRole"]=std::string("PatchBoundary");metadata.Feature.erase("standardHole");metadata.Feature.erase("contourPoseData");output.Value.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);}return result;
}
#include "FiveAxisIntentCorrection.inc"
SOperationCandidate MergeOperation(const SOperationEvaluation& input,const IToolpathGeometryServices& geometry)
{
    const auto count=static_cast<std::size_t>(Number(input.Parameters,"pathCount"));const auto nominalIndices=input.Parameters.at("nominalIndices").To<Array>();Check(count>=2&&nominalIndices.size()==count&&input.Inputs.size()>count,"CAM_MERGE_REQUIRES_TWO_PATHS");const auto tolerance=Number(input.Parameters,"toleranceMm");Check(tolerance>=0&&tolerance<=1000,"CAM_MERGE_TOLERANCE_INVALID");
    const auto* context=dynamic_cast<const CFiveAxisIntentOperationContext*>(input.RuntimeContext.get());Check(context,"FIVEAXIS_INTENT_CONTEXT_MISSING");
    auto metadata=ReadFiveAxisIntentMetadata(input.Inputs.front().Value);
    std::vector<SToolpathValue> pieces;Map sourceSpans,corrections;Array controls;std::set<std::string> controlIds;
    for(std::size_t pathIndex=0;pathIndex<count;++pathIndex){const auto& path=input.Inputs[pathIndex];auto nextMetadata=ReadFiveAxisIntentMetadata(path.Value);Check(nextMetadata.SetupID==metadata.SetupID&&nextMetadata.PlacementID==metadata.PlacementID,"CAM_MERGE_SAME_SETUP_REQUIRED");auto common=nextMetadata.Process,previous=metadata.Process;for(const auto* key:{"leadIn","leadOut","gapLengthMm","overcutLengthMm"}){common.erase(key);previous.erase(key);}Check(common==previous&&nextMetadata.Pose==metadata.Pose&&nextMetadata.BeforeCommands==metadata.BeforeCommands&&nextMetadata.AfterCommands==metadata.AfterCommands,"CAM_MERGE_PROCESS_CONFLICT");
        const auto nominalIndex=static_cast<std::size_t>(Numeric(nominalIndices[pathIndex]));Check(nominalIndex>=count&&nominalIndex<input.Inputs.size(),"CAM_MERGE_NOMINAL_INPUT_INVALID");auto projected=ProjectToolpathSelection(path,input.Inputs[nominalIndex],input.OperationId,{"LeadIn","LeadOut","Lead","OffsetConnector"},geometry);auto value=std::move(projected.Value);value.Channels.erase("fiveaxis.correction");value.Channels.erase("fiveaxis.geometryEdits");
        if(path.Value.Channels.contains("fiveaxis.geometryEdits"))for(const auto& item:path.Value.Channels.at("fiveaxis.geometryEdits").To<Map>().at("entries").To<Array>()){const auto edit=item.To<Map>();SOperationEvaluation stage=input;stage.Parameters=edit.at("parameters").To<Map>();stage.Inputs={SFrozenPathInput{{"stage",DeclaredToolpathPort(input.OperationId,"main"),true,{}},path.PathId,value,ToolpathFingerprint(value)}};auto candidate=Text(edit,"type")=="fiveaxis.correction"?CorrectionOperation(stage,geometry):HoleDimensionsOperation(stage,geometry);value=std::move(candidate.Outputs.front().Value);}
        Check(!value.Geometry.Segments.empty(),"CAM_MERGE_EMPTY_PATH");const auto& first=value.Geometry.Segments.front();const auto& last=value.Geometry.Segments.back();const auto a=geometry.Evaluate(value.Geometry,first,Start(Range(first))),b=geometry.Evaluate(value.Geometry,last,End(Range(last)));Check(std::hypot(a.X-b.X,a.Y-b.Y,a.Z-b.Z)>value.Geometry.Tolerance,"CAM_MERGE_CLOSED_PATH");
        for(auto& s:value.Geometry.Segments){const auto matrix=SegmentCorrection(value,s);s.Id=ToolpathPortKey(path.Binding.Source)+"/"+s.Id;corrections[s.Id]=CorrectionData(matrix);}for(const auto& [key,span]:nextMetadata.SourceSpans)sourceSpans[key]=span;
        if(value.Channels.contains("poseControls"))for(const auto& item:value.Channels.at("poseControls").To<Map>().at("entries").To<Array>()){const auto control=item.To<Map>();const auto id=Text(control,"id");if(controlIds.insert(id).second)controls.push_back(item);}
        pieces.push_back(std::move(value));}
    const auto point=[&](const SToolpathValue& value,bool end){const auto& segment=end?value.Geometry.Segments.back():value.Geometry.Segments.front();return geometry.Evaluate(value.Geometry,segment,end?End(Range(segment)):Start(Range(segment)));};
    const auto distance=[](Point3 a,Point3 b){return std::hypot(a.X-b.X,a.Y-b.Y,a.Z-b.Z);};
    const auto reverse=[](SToolpathValue& value){std::reverse(value.Geometry.Segments.begin(),value.Geometry.Segments.end());for(auto& segment:value.Geometry.Segments){auto range=Range(segment);range.Reversed=!range.Reversed;SetRange(segment,range);for(auto& source:segment.Sources){source.SourceRange.Reversed=!source.SourceRange.Reversed;source.OutputRange.Reversed=!source.OutputRange.Reversed;}}};
    std::size_t first=0;bool firstReverse=false;bool foundEnd=false;
    for(std::size_t i=0;i<pieces.size()&&!foundEnd;++i)for(bool end:{false,true}){bool linked=false;for(std::size_t j=0;j<pieces.size();++j)if(i!=j)for(bool other:{false,true})linked|=distance(point(pieces[i],end),point(pieces[j],other))<=tolerance+pieces[i].Geometry.Tolerance;if(!linked){first=i;firstReverse=end;foundEnd=true;break;}}
    auto value=pieces[first];if(firstReverse)reverse(value);std::set<std::size_t> used{first};
    while(used.size()<pieces.size()){Cancel(input);std::size_t chosen=pieces.size();bool backwards=false;double best=std::numeric_limits<double>::infinity();const auto tail=point(value,true);
        for(std::size_t i=0;i<pieces.size();++i)if(!used.contains(i))for(bool end:{false,true}){const auto d=distance(tail,point(pieces[i],end));if(d<best){best=d;chosen=i;backwards=end;}}
        Check(chosen<pieces.size()&&best<=tolerance+value.Geometry.Tolerance,"CAM_MERGE_OUTSIDE_TOLERANCE");auto next=pieces[chosen];if(backwards)reverse(next);const auto head=point(next,false);
        if(best>value.Geometry.Tolerance){SEditableSegment connector;connector.Id="merge/"+std::to_string(used.size());BSpline3 line;line.Degree=1;line.Poles={tail,head};line.Knots={0,1};line.Multiplicities={2,2};connector.Curve=CurveSegment3{line,{0,1,false}};connector.Parameterization="CadNative";connector.ConstructionRole="MergeConnector";value.Geometry.Segments.push_back(std::move(connector));}
        value.Geometry.Segments.insert(value.Geometry.Segments.end(),next.Geometry.Segments.begin(),next.Geometry.Segments.end());used.insert(chosen);}
    const auto closure=distance(point(value,false),point(value,true));value.Geometry.Closed=closure<=value.Geometry.Tolerance;
    const Map none{{"kind",std::string("None")},{"placement",std::string("ScrapSide")},{"lengthMm",0.},{"angleRad",0.}};metadata.SourceSpans=std::move(sourceSpans);metadata.Feature["geometryKind"]=std::string("GeneralContour");metadata.Feature["boundaryRole"]=std::string("PatchBoundary");metadata.Feature.erase("standardHole");metadata.Feature.erase("contourPoseData");if(!metadata.Feature.contains("splitFromData"))metadata.Feature["splitFromData"]=ReadFiveAxisIntentMetadata(input.Inputs.front().Value).Feature;
    metadata.Feature["geometryEdited"]=true;metadata.Process["leadIn"]=none;metadata.Process["leadOut"]=none;metadata.Process["gapLengthMm"]=0.;metadata.Process["overcutLengthMm"]=0.;value.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);
    value.Channels["fiveaxis.leads"]=Map{{"schema",std::string("fiveaxis.leads/1")},{"attachment",std::string("Path")},{"leadIn",none},{"leadOut",none}};
    value.Channels["fiveaxis.trace"]=Map{{"schema",std::string("fiveaxis.trace/1")},{"attachment",std::string("Path")},{"gapLengthMm",0.},{"overcutLengthMm",0.},{"startAdjusted",false}};
    value.Channels.erase("fiveaxis.correction");value.Channels.erase("fiveaxis.geometryEdits");value.Channels["fiveaxis.segmentCorrections"]=Map{{"schema",std::string("fiveaxis.segment-corrections/1")},{"attachment",std::string("Path")},{"matrices",corrections}};
    value.Channels.erase("poseField");value.Channels.erase("contourPoseField");if(value.Channels.contains("poseControls")){auto channel=value.Channels.at("poseControls").To<Map>();channel["entries"]=std::move(controls);value.Channels["poseControls"]=channel;}
    std::vector<CorrectionMatrix> transforms;for(const auto& segment:value.Geometry.Segments)transforms.push_back(SegmentCorrection(value,segment));const auto outputPort=DeclaredToolpathPort(input.OperationId,"main");SFrozenPathInput stage{{"input/0",outputPort,true,{}},{},value,ToolpathFingerprint(value)};value=FinalizeProjectedToolpath(stage,input.OperationId,{input.OperationId},geometry);Map mappedTransforms;Check(value.Geometry.Segments.size()==transforms.size(),"CAM_MERGE_CORRECTION_MAPPING_INVALID");for(std::size_t i=0;i<transforms.size();++i)mappedTransforms[value.Geometry.Segments[i].Id]=CorrectionData(transforms[i]);value.Channels["fiveaxis.segmentCorrections"]=Map{{"schema",std::string("fiveaxis.segment-corrections/1")},{"attachment",std::string("Path")},{"matrices",mappedTransforms}};value.Channels["poseField"]=BuildFiveAxisIntentPoseField(*context,value,outputPort);SOperationCandidate result;result.Outputs.push_back({"main",{}, {},std::move(value)});for(const auto& path:input.Inputs)if(path.Binding.Replace){auto excluded=MissingRanges(path,result.Outputs.front().Value);result.ExcludedRanges.insert(result.ExcludedRanges.end(),excluded.begin(),excluded.end());}return result;
}
SOperationType Type(const std::string& id,Map fields,FiveAxisIntentContextResolver resolver,std::function<SOperationCandidate(const SOperationEvaluation&,const IToolpathGeometryServices&)> evaluate,std::set<std::string> writes)
{
    SOperationType type;type.TypeId=id;type.ParameterSchema={{"fields",std::move(fields)}};type.WriteChannels=std::move(writes);type.ReadChannels={"geometry","topology","traversal","provenance","frame","pose","poseControls","poseField","contourPoseField","fiveaxis"};type.Evaluate=std::move(evaluate);type.DeclaredOutputRoles={"main"};type.DeclaredOutputIdentityInputs={{"main",0}};
    for(const auto* channel:{"fiveaxis.correction","fiveaxis.segmentCorrections","fiveaxis.geometryEdits"})type.ReadChannels.insert(channel);
    type.FreezeContext=[resolver=std::move(resolver)](const Map& persisted)->std::shared_ptr<const IToolpathOperationContext>{const auto context=resolver(persisted);Check(context!=nullptr,"FIVEAXIS_INTENT_CONTEXT_MISSING");return context;};return type;
}
}

CFrozenFiveAxisIntentModel::CFrozenFiveAxisIntentModel(BRepModel model):m_Model(std::move(model))
{Persistence::Writer writer;writer.Write(m_Model);m_Fingerprint=Digest(writer.Bytes);}
const BRepModel& CFrozenFiveAxisIntentModel::Model() const{return m_Model;}
const std::string& CFrozenFiveAxisIntentModel::Fingerprint() const{return m_Fingerprint;}
CFiveAxisIntentOperationContext::CFiveAxisIntentOperationContext(BRepModel model,Bindings bindings,std::string geometryVersion,std::string serviceVersion,std::string frame,int outwardSign,std::shared_ptr<const IBoundaryLeadService> boundary,std::shared_ptr<const ICurveOffset3DService> offset,std::shared_ptr<const iCAX::Geometry::ICurveEditingService> editing)
    :CFiveAxisIntentOperationContext(std::make_shared<const CFrozenFiveAxisIntentModel>(std::move(model)),std::move(bindings),std::move(geometryVersion),std::move(serviceVersion),std::move(frame),outwardSign,std::move(boundary),std::move(offset),std::move(editing)){}
CFiveAxisIntentOperationContext::CFiveAxisIntentOperationContext(std::shared_ptr<const CFrozenFiveAxisIntentModel> model,Bindings bindings,std::string geometryVersion,std::string serviceVersion,std::string frame,int outwardSign,std::shared_ptr<const IBoundaryLeadService> boundary,std::shared_ptr<const ICurveOffset3DService> offset,std::shared_ptr<const iCAX::Geometry::ICurveEditingService> editing)
    :m_Model(std::move(model)),m_Bindings(std::move(bindings)),m_GeometryVersion(std::move(geometryVersion)),m_ServiceVersion(std::move(serviceVersion)),m_Frame(std::move(frame)),m_OutwardSign(outwardSign),m_Boundary(std::move(boundary)),m_Offset(std::move(offset)),m_Editing(std::move(editing))
{
    Check(m_Model&&m_Boundary&&m_Offset&&!m_GeometryVersion.empty()&&!m_ServiceVersion.empty()&&!m_Frame.empty()&&(outwardSign==1||outwardSign==-1),"FIVEAXIS_INTENT_CONTEXT_INVALID");Persistence::Writer writer;writer.Write(m_Model->Fingerprint());writer.Write(m_GeometryVersion);writer.Write(m_ServiceVersion);writer.Write(m_Frame);writer.Write(m_OutwardSign);writer.Write(static_cast<bool>(m_Editing));
    for(const auto& [id,binding]:m_Bindings){Check(!id.empty(),"FIVEAXIS_INTENT_CONTEXT_SEGMENT_ID_REQUIRED");const auto& face=Record(Model().Faces,binding.FaceId);const auto& wire=Record(Model().Wires,binding.WireId);const auto& edge=Record(Model().Edges,binding.EdgeId);
        Check(std::find(face.WireIds.begin(),face.WireIds.end(),wire.Id)!=face.WireIds.end()&&binding.CoedgeIndex<wire.Coedges.size()&&wire.Coedges[binding.CoedgeIndex].EdgeId==edge.Id,"FIVEAXIS_INTENT_CONTEXT_BINDING_INVALID");writer.Write(id);writer.Write(binding.FaceId);writer.Write(binding.WireId);writer.Write(binding.EdgeId);writer.Write(static_cast<std::uint64_t>(binding.CoedgeIndex));}
    m_Fingerprint=Digest(writer.Bytes);
}
std::string CFiveAxisIntentOperationContext::Fingerprint() const{return m_Fingerprint;}
const BRepModel& CFiveAxisIntentOperationContext::Model() const{return m_Model->Model();}
std::shared_ptr<const CFrozenFiveAxisIntentModel> CFiveAxisIntentOperationContext::FrozenModel() const{return m_Model;}
const CFiveAxisIntentOperationContext::Bindings& CFiveAxisIntentOperationContext::SourceBindings() const{return m_Bindings;}
const std::string& CFiveAxisIntentOperationContext::Frame() const{return m_Frame;}
int CFiveAxisIntentOperationContext::OutwardSign() const{return m_OutwardSign;}
const IBoundaryLeadService& CFiveAxisIntentOperationContext::Boundary() const{return *m_Boundary;}
const ICurveOffset3DService& CFiveAxisIntentOperationContext::Offset() const{return *m_Offset;}
std::shared_ptr<const iCAX::Geometry::ICurveEditingService> CFiveAxisIntentOperationContext::Editing() const{return m_Editing;}

CFiveAxisIntentOperationContext::Bindings BuildFiveAxisIntentCadBindings(const IntentMap& spans)
{
    const auto integer=[](const Variant& value)->std::uint64_t {
        if(value.Is<std::string>()){const auto text=value.To<std::string>();Check(!text.empty()&&text.find_first_not_of("0123456789")==std::string::npos,"FIVEAXIS_INTENT_CAD_BINDING_ID_INVALID");std::size_t count=0;const auto number=std::stoull(text,&count);Check(count==text.size(),"FIVEAXIS_INTENT_CAD_BINDING_ID_INVALID");return number;}
        if(value.Is<unsigned long long>())return value.To<unsigned long long>();
        if(value.Is<unsigned int>())return value.To<unsigned int>();
        if(value.Is<long long>()){const auto number=value.To<long long>();Check(number>=0,"FIVEAXIS_INTENT_CAD_BINDING_ID_INVALID");return static_cast<std::uint64_t>(number);}
        if(value.Is<int>()){const auto number=value.To<int>();Check(number>=0,"FIVEAXIS_INTENT_CAD_BINDING_ID_INVALID");return static_cast<std::uint64_t>(number);}
        throw std::invalid_argument("FIVEAXIS_INTENT_CAD_BINDING_ID_INVALID");
    };
    CFiveAxisIntentOperationContext::Bindings bindings;
    for(const auto& [id,value]:spans){Check(!id.empty(),"FIVEAXIS_INTENT_CONTEXT_SEGMENT_ID_REQUIRED");const auto span=value.To<IntentMap>();
        SFiveAxisIntentCadSpan binding;binding.FaceId=integer(span.at("faceId"));binding.WireId=integer(span.at("wireId"));binding.EdgeId=integer(span.at("edgeId"));const auto index=integer(span.at("coedgeIndex"));
        Check(binding.FaceId&&binding.WireId&&binding.EdgeId&&index<=std::numeric_limits<std::size_t>::max(),"FIVEAXIS_INTENT_CAD_BINDING_ID_INVALID");binding.CoedgeIndex=static_cast<std::size_t>(index);bindings.emplace(id,binding);}
    return bindings;
}

Direction3 EvaluateFiveAxisIntentDirection(const CFiveAxisIntentOperationContext& context,const SToolpathValue& value,std::size_t index,double parameter)
{
    Check(context.Frame()==value.Geometry.FrameId,"FIVEAXIS_INTENT_CONTEXT_FRAME_MISMATCH");const auto domain=Range(value.Geometry.Segments.at(index));Check(std::isfinite(parameter)&&parameter>=domain.First&&parameter<=domain.Last,"FIVEAXIS_INTENT_PARAMETER_OUTSIDE_RANGE");
    ValidatePose(Pose(value));return EvaluateFiveAxisIntentPoseField(context,value,index,parameter);
}
void RegisterFiveAxisIntentOperations(CToolpathOperationRegistry& registry,FiveAxisIntentContextResolver resolver)
{
    Check(static_cast<bool>(resolver),"FIVEAXIS_INTENT_CONTEXT_RESOLVER_REQUIRED");
    auto correction=Type("fiveaxis.correction",{{"matrix",Field("array")}},resolver,CorrectionOperation,{"*"});correction.EvaluationRevision="2";correction.ReadChannels={"*"};correction.PropagateUnwrittenChannels=false;correction.ValidateParameters=[](const Map& p){(void)ReadCorrection(p);};registry.Register(std::move(correction));
    auto dimensions=Type("fiveaxis.holeDimensions",{{"dimensions",Field("object")},{"descriptor",Field("object",false)}},resolver,HoleDimensionsOperation,{"*"});dimensions.EvaluationRevision="2";dimensions.MaximumInputs=2;dimensions.ReadChannels={"*"};dimensions.PropagateUnwrittenChannels=false;registry.Register(std::move(dimensions));
    auto merge=Type("fiveaxis.merge",{{"toleranceMm",Field("number")},{"pathCount",Field("number")},{"nominalIndices",Field("array")}},resolver,MergeOperation,{"*"});merge.EvaluationRevision="2";merge.MinimumInputs=3;merge.MaximumInputs=512;merge.DeclaredOutputIdentityInputs.clear();merge.PropagateUnwrittenChannels=false;merge.InheritExecutionBlock=false;merge.ReadChannels={"*"};registry.Register(std::move(merge));
    auto pose=Type("fiveaxis.pose",{{"policy",Field("object")},{"clearControls",Field("boolean",false)}},resolver,PoseOperation,{"pose","poseField","poseControls","contourPoseField"});
    pose.ValidateParameters=[](const Map& parameters){ValidatePose(Object(parameters,"policy"));};registry.Register(std::move(pose));
    auto delta=Type("fiveaxis.pose.delta",{{"controls",Field("array")},{"removedIds",Field("array",false)}},resolver,PoseDeltaOperation,{"poseField","poseControls","pose"});
    delta.EvaluationRevision="2";
    registry.Register(std::move(delta));
    auto policyEdit=Type("fiveaxis.pose.policy",{{"policy",Field("object")}},resolver,PosePolicyOperation,{"pose","poseField"});policyEdit.ValidateParameters=[](const Map& parameters){ValidatePose(Object(parameters,"policy"));};registry.Register(std::move(policyEdit));
    auto remove=Type("fiveaxis.pose.remove",{{"ids",Field("array")}},resolver,PoseRemoveOperation,{"poseField","poseControls","pose"});remove.EvaluationRevision="2";registry.Register(std::move(remove));
    auto reversed=Type("fiveaxis.pose.reverse",{},resolver,PoseReverseOperation,{"poseField","pose","poseControls"});
    registry.Register(std::move(reversed));
    auto cutType=Type("fiveaxis.cutType",{{"cutType",Field("string")},{"policy",Field("object",false)},{"standardHole",Field("object",false)}},resolver,CutTypeOperation,{"pose","poseField","poseControls","contourPoseField","fiveaxis"});cutType.EvaluationRevision="3";registry.Register(std::move(cutType));
    const auto standardSplit=registry.Find("icax.edit.split");Check(standardSplit!=nullptr,"FIVEAXIS_INTENT_STANDARD_SPLIT_REQUIRED");auto splitRegistry=std::make_shared<CToolpathOperationRegistry>();splitRegistry->Register(*standardSplit);auto split=Type("fiveaxis.split",{},resolver,[splitRegistry](const SOperationEvaluation& input,const IToolpathGeometryServices& geometry){return SplitOperation(input,geometry,*splitRegistry);},{"*"});split.EvaluationRevision="2";split.ParameterSchema=standardSplit->ParameterSchema;split.NormalizeParameters=standardSplit->NormalizeParameters;split.InheritExecutionBlock=standardSplit->InheritExecutionBlock;split.DeclaredOutputRoles.clear();split.DeclaredOutputIdentityInputs.clear();split.ReadChannels={"*"};split.PropagateUnwrittenChannels=false;registry.Register(std::move(split));
    auto kerf=Type("fiveaxis.kerf",{{"policy",Field("object")}},resolver,Kerf,{"fiveaxis.kerf","poseField"});kerf.EvaluationRevision="3";kerf.ValidateParameters=[](const Map& parameters){ValidateKerf(Object(parameters,"policy"));};registry.Register(std::move(kerf));
    auto trace=Type("fiveaxis.trace",{{"gapLengthMm",Field("number")},{"overcutLengthMm",Field("number")},{"start",Field("object",false)},{"startLocked",Field("boolean",false)},{"allowTrimmedStart",Field("boolean",false)}},resolver,Trace,{"fiveaxis.trace"});trace.ValidateParameters=ValidateTrace;registry.Register(std::move(trace));
    auto leads=Type("fiveaxis.leads",{{"leadIn",Field("object")},{"leadOut",Field("object")}},resolver,Leads,{"fiveaxis.leads","pose"});leads.EvaluationRevision="4";leads.ValidateParameters=[](const Map& parameters){ValidateLead(Object(parameters,"leadIn"));ValidateLead(Object(parameters,"leadOut"));};registry.Register(std::move(leads));
    RegisterFiveAxisIntentBranchProcess(registry,resolver);
}
Direction3 CorrectFiveAxisDirection(const SToolpathValue& value,Direction3 direction,std::size_t segment)
{CorrectPoint(direction,SegmentCorrection(value,value.Geometry.Segments.at(segment)),true);return direction;}
iCAX::GeometryServices::BoundaryFrame CorrectFiveAxisFrame(const SToolpathValue& value,std::size_t segment,iCAX::GeometryServices::BoundaryFrame frame)
{const auto matrix=SegmentCorrection(value,value.Geometry.Segments.at(segment));CorrectPoint(frame.Point,matrix);CorrectPoint(frame.Tangent,matrix,true);CorrectPoint(frame.Normal,matrix,true);return frame;}
std::array<double,16> FiveAxisSegmentCorrection(const SToolpathValue& value,std::size_t segment)
{return SegmentCorrection(value,value.Geometry.Segments.at(segment));}
}
