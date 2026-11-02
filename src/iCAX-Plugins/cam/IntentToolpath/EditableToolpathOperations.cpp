#include "pch.h"
#include "EditableToolpathInternal.h"
#include <shared_mutex>

namespace iCAX::CAM::Intent
{
using namespace Editable;
struct CToolpathOperationRegistry::Impl
{
    mutable std::shared_mutex Mutex;
    std::map<std::string,std::shared_ptr<const SOperationType>> Types;
};
CToolpathOperationRegistry::CToolpathOperationRegistry():m_Impl(std::make_unique<Impl>()){}
CToolpathOperationRegistry::~CToolpathOperationRegistry()=default;
void CToolpathOperationRegistry::Register(SOperationType type)
{
    Require(!type.TypeId.empty()&&type.SchemaVersion>0&&!type.AlgorithmVersion.empty()&&!type.EvaluationRevision.empty()&&type.Evaluate
        &&type.MinimumInputs<=type.MaximumInputs,"TOOLPATH_TYPE_DESCRIPTOR_INVALID");
    for(const auto& role:type.DeclaredOutputRoles)Require(!role.empty(),"TOOLPATH_DECLARED_OUTPUT_ROLE_INVALID");
    for(const auto& [role,index]:type.DeclaredOutputIdentityInputs)Require(type.DeclaredOutputRoles.contains(role)&&index<type.MinimumInputs,"TOOLPATH_DECLARED_OUTPUT_IDENTITY_INVALID");
    std::unique_lock lock(m_Impl->Mutex);
    Require(!m_Impl->Types.contains(type.TypeId),"TOOLPATH_TYPE_ALREADY_REGISTERED");
    const auto key=type.TypeId;m_Impl->Types.emplace(key,std::make_shared<const SOperationType>(std::move(type)));
}
void CToolpathOperationRegistry::Unregister(const std::string& type){std::unique_lock lock(m_Impl->Mutex);m_Impl->Types.erase(type);}
std::shared_ptr<const SOperationType> CToolpathOperationRegistry::Find(const std::string& type)const
{std::shared_lock lock(m_Impl->Mutex);const auto i=m_Impl->Types.find(type);return i==m_Impl->Types.end()?nullptr:i->second;}
std::vector<SOperationType> CToolpathOperationRegistry::Describe()const
{std::shared_lock lock(m_Impl->Mutex);std::vector<SOperationType> result;for(const auto& [id,type]:m_Impl->Types)result.push_back(*type);return result;}

namespace
{
void Cancel(const SOperationEvaluation& in){if(in.Cancelled&&in.Cancelled->load())throw std::runtime_error("TOOLPATH_CANCELLED");}
SToolpathValue Whole(const SFrozenPathInput& input)
{auto value=input.Value;value.Geometry=Slice(input,0,static_cast<double>(input.Value.Geometry.Segments.size()),"whole");value.Geometry.Closed=input.Value.Geometry.Closed;return value;}
SToolpathValue MergeInput(const SFrozenPathInput& input)
{auto value=Whole(input);for(auto& segment:value.Geometry.Segments)segment.Id=Key(input.Binding.Source)+"/"+segment.Id;return value;}
SOutputCandidate One(const SFrozenPathInput& input)
{return {"main",{},std::size_t(0),Whole(input)};}
void ExecutionBlock(SOperationCandidate& candidate,const SOperationEvaluation& in)
{
    std::vector<std::pair<double,std::string>> order;
    for(const auto& output:candidate.Outputs)
    {
        const auto& segment=output.Value.Geometry.Segments.front();bool found=false;
        for(const auto& mapping:segment.Sources)if(mapping.Source==in.Inputs[0].Binding.Source)
        {order.emplace_back(Locate({mapping.Source,mapping.SourceSegmentId,First(mapping.SourceRange)},in.Inputs[0]),output.RoleKey);found=true;break;}
        Require(found,"TOOLPATH_EXECUTION_BLOCK_SOURCE_REQUIRED");
    }
    std::sort(order.begin(),order.end());Array roles;for(const auto& [position,role]:order)roles.emplace_back(role);
    candidate.Constraints["executionBlock"]=Map{{"id",std::string("operation/")+Key(in.OperationId)},{"atomic",true},{"outputRoles",std::move(roles)}};
}
Map Field(const std::string& type,bool required=true){return {{"type",type},{"required",required}};}
void ReverseMaterialSide(SToolpathValue& value)
{
    if(!value.Channels.contains("material"))return;
    Require(value.Channels.at("material").Is<Map>(),"TOOLPATH_UNSUPPORTED_MATERIAL_SIDE_SCHEMA");
    auto material=value.Channels.at("material").To<Map>();
    Require(Text(material,"schema")=="icax.material-side/1"&&Text(material,"attachment")=="Path","TOOLPATH_UNSUPPORTED_MATERIAL_SIDE_SCHEMA");
    const auto side=Text(material,"retainedSide");Require(side=="Left"||side=="Right","TOOLPATH_MATERIAL_SIDE_INVALID");
    const auto normal=ArrayAt(material,"referenceNormal");Require(normal.size()==3,"TOOLPATH_MATERIAL_REFERENCE_NORMAL_INVALID");
    const Vector3 direction{NumericValue(normal[0]),NumericValue(normal[1]),NumericValue(normal[2])};
    Require(std::isfinite(direction.X)&&std::isfinite(direction.Y)&&std::isfinite(direction.Z)&&std::abs(std::hypot(direction.X,direction.Y,direction.Z)-1.)<=1e-10,"TOOLPATH_MATERIAL_REFERENCE_NORMAL_INVALID");
    // referenceNormal is a saved unit direction in Geometry.FrameId, independent
    // of traversal. Left is normal x tangent; Right is its negative. Reversing
    // the tangent and the relative side preserves the same physical material.
    // Unknown plugin schemas must provide their own reversible operation.
    material["retainedSide"]=side=="Left"?std::string("Right"):std::string("Left");value.Channels["material"]=std::move(material);
}
SOperationType Type(const std::string& id,Map fields,
    std::function<SOperationCandidate(const SOperationEvaluation&,const IToolpathGeometryServices&)> eval,
    std::size_t min=1,std::size_t max=1)
{
    SOperationType t;t.TypeId=id;t.ParameterSchema={{"fields",std::move(fields)}};t.Evaluate=std::move(eval);t.MinimumInputs=min;t.MaximumInputs=max;
    t.ReadChannels={"geometry","topology","traversal","provenance","frame"};t.DeclaredOutputRoles={"main"};if(min==1&&max==1)t.DeclaredOutputIdentityInputs={{"main",0}};return t;
}
Map NormalizePartitionIdentity(const std::vector<SFrozenPathInput>& inputs,const Map& parameters)
{
    auto normalized=parameters;const auto& input=inputs.at(0);
    if(Flag(parameters,"retainFirstIdentity")&&!parameters.contains("retainedIdentityAnchor"))normalized["retainedIdentityAnchor"]=ToData(AnchorAt(input,0));
    if(input.Value.Geometry.Closed&&ArrayAt(parameters,"cuts").size()==2&&!parameters.contains("identityTraversal"))
    {
        bool captured=false;for(const auto& segment:input.Value.Geometry.Segments)
        {
            const auto range=Range(segment);for(const auto& source:segment.Sources)if(source.Precision=="Exact")
            {
                const auto parameter=First(source.SourceRange)+(First(range)-First(source.OutputRange))*(Last(source.SourceRange)-First(source.SourceRange))/(Last(source.OutputRange)-First(source.OutputRange));
                normalized["identityTraversal"]=Map{{"anchor",ToData(SPathAnchor{source.Source,source.SourceSegmentId,parameter,source.Occurrence,{}})},{"reversed",source.SourceRange.Reversed}};captured=true;break;
            }
            if(captured)break;
        }
        if(!captured)normalized["identityTraversal"]=Map{{"anchor",ToData(AnchorAt(input,0))},{"reversed",Range(input.Value.Geometry.Segments.front()).Reversed}};
    }
    return normalized;
}
bool PartitionTraversalForward(const SOperationEvaluation& in)
{
    if(!in.Parameters.contains("identityTraversal"))return true;const auto reference=MapAt(in.Parameters,"identityTraversal");const auto anchor=AnchorFromData(MapAt(reference,"anchor"));
    std::set<bool> directions;const auto& input=in.Inputs.at(0);const auto expected=Flag(reference,"reversed");
    for(const auto& segment:input.Value.Geometry.Segments)
    {
        const auto range=Range(segment);if(anchor.Port==input.Binding.Source&&anchor.SegmentId==segment.Id&&anchor.Parameter>=range.First-1e-12&&anchor.Parameter<=range.Last+1e-12)directions.insert(range.Reversed==expected);
        for(const auto& source:segment.Sources)if(source.Source==anchor.Port&&source.SourceSegmentId==anchor.SegmentId&&source.Occurrence==anchor.Occurrence&&anchor.Parameter>=source.SourceRange.First-1e-12&&anchor.Parameter<=source.SourceRange.Last+1e-12)
        {Require(source.Precision=="Exact","TOOLPATH_PARTITION_IDENTITY_REFERENCE_PRECISION");directions.insert(source.SourceRange.Reversed==expected);}
    }
    Require(directions.size()==1,"TOOLPATH_PARTITION_IDENTITY_REFERENCE_NEEDS_REBIND");return *directions.begin();
}
SOperationCandidate Partition(const SOperationEvaluation& in,bool gaps)
{
    const auto& input=in.Inputs.at(0);const auto n=static_cast<double>(input.Value.Geometry.Segments.size());
    struct Boundary{double Position;std::string Key;};std::vector<Boundary> cuts;
    struct Gap{double First,Last;std::string StartKey,EndKey;SPathRange Range;};std::vector<Gap> holes;
    const auto endpointKey=[&](bool end){const auto& segment=end?input.Value.Geometry.Segments.back():input.Value.Geometry.Segments.front();const auto r=Range(segment);return "endpoint/"+segment.Id+((end?!r.Reversed:r.Reversed)?"/nativeLast":"/nativeFirst");};
    const auto items=ArrayAt(in.Parameters,gaps?"gaps":"cuts");Require(!items.empty(),"TOOLPATH_EMPTY_PARTITION");std::set<std::string> ids;
    for(const auto& item:items)
    {
        Cancel(in);const auto m=item.To<Map>();const auto id=Text(m,"id");Require(!id.empty()&&ids.insert(id).second,"TOOLPATH_PARTITION_ID_REQUIRED");
        if(gaps)
        {
            const auto r=NormalizeRange(RangeFromData(MapAt(m,"range")),input);const auto a=Locate(r.Start,input),b=Locate(r.End,input);const auto windows=RangeWindows(r,input);
            Require(r.Start.Port==r.End.Port,"TOOLPATH_GAP_SOURCE_MISMATCH");
            Require(windows.size()==1||(input.Value.Geometry.Closed&&windows.size()==2&&windows.front().first<1e-12&&windows.back().second>n-1e-12),"TOOLPATH_NEEDS_REBIND_GAP_CONNECTIVITY");
            const auto boundary=[&](double position){if(std::abs(position-a)<1e-11)return id+"/start";if(std::abs(position-b)<1e-11)return id+"/end";return std::string("seam");};
            for(const auto& [begin,end]:windows)
            {auto selected=NormalizeRange({AnchorAt(input,begin),AnchorAt(input,end),false},input);holes.push_back({begin,end,boundary(begin),boundary(end),std::move(selected)});}
        }
        else {const auto pos=Locate(AnchorFromData(MapAt(m,"anchor")),input);Require(pos>1e-12&&pos<n-1e-12,"TOOLPATH_SPLIT_AT_ENDPOINT");cuts.push_back({pos,id});}
    }
    SOperationCandidate result;std::optional<double> retainedIdentity;const bool complementaryArcs=!gaps&&input.Value.Geometry.Closed&&cuts.size()==2;
    const bool canonicalForward=!complementaryArcs||PartitionTraversalForward(in);
    if(!gaps&&Flag(in.Parameters,"retainFirstIdentity"))
    {Require(in.Parameters.contains("retainedIdentityAnchor"),"TOOLPATH_RETAINED_IDENTITY_ANCHOR_REQUIRED");retainedIdentity=Locate(AnchorFromData(MapAt(in.Parameters,"retainedIdentityAnchor")),input);}
    const auto add=[&](double a,double b,const std::string& start,const std::string& end)
    {
        if(b-a<1e-12)return;
        std::vector<std::string> boundary{start,end};std::sort(boundary.begin(),boundary.end());
        auto role="component/"+boundary[0]+"/"+boundary[1];
        if(complementaryArcs)
        {const auto arc="closed-arc/"+Hash(Map{{"start",canonicalForward?start:end},{"end",canonicalForward?end:start}});role+="/"+arc;boundary.push_back(arc);std::sort(boundary.begin(),boundary.end());}
        auto value=input.Value;value.Geometry=Slice(input,a,b,role);
        SOutputCandidate output{role,boundary,{},std::move(value)};output.OrganizationInputSlot=input.Binding.Slot;
        if(retainedIdentity&&((*retainedIdentity>=a-1e-12&&*retainedIdentity<=b+1e-12)||(input.Value.Geometry.Closed&&*retainedIdentity+n>=a-1e-12&&*retainedIdentity+n<=b+1e-12)))output.InheritInput=0;
        result.Outputs.push_back(std::move(output));
    };
    if(gaps)
    {
        std::sort(holes.begin(),holes.end(),[](const auto& a,const auto& b){return a.First<b.First;});
        for(std::size_t i=1;i<holes.size();++i)Require(holes[i].First>holes[i-1].Last+1e-12,"TOOLPATH_GAP_OVERLAP_OR_TOUCH");
        for(const auto& hole:holes)result.ExcludedRanges.push_back(hole.Range);
        for(std::size_t i=1;i<holes.size();++i)add(holes[i-1].Last,holes[i].First,holes[i-1].EndKey,holes[i].StartKey);
        if(input.Value.Geometry.Closed)
        {
            if(holes.front().First>1e-12&&holes.back().Last<n-1e-12)
                add(holes.back().Last,holes.front().First+n,holes.back().EndKey,holes.front().StartKey);
            else {add(0,holes.front().First,"source/seam",holes.front().StartKey);add(holes.back().Last,n,holes.back().EndKey,"source/seam");}
        }
        else {add(0,holes.front().First,endpointKey(false),holes.front().StartKey);add(holes.back().Last,n,holes.back().EndKey,endpointKey(true));}
        result.IntentionalEmpty=result.Outputs.empty();
        Array retained;for(const auto& r:result.ExcludedRanges)retained.emplace_back(ToData(r));result.Constraints["retainedIntervals"]=std::move(retained);
    }
    else
    {
        std::sort(cuts.begin(),cuts.end(),[](const auto& a,const auto& b){return a.Position<b.Position;});
        for(std::size_t i=1;i<cuts.size();++i)Require(cuts[i].Position>cuts[i-1].Position+1e-12,"TOOLPATH_SPLIT_DUPLICATE");
        for(std::size_t i=1;i<cuts.size();++i)add(cuts[i-1].Position,cuts[i].Position,cuts[i-1].Key,cuts[i].Key);
        if(input.Value.Geometry.Closed)add(cuts.back().Position,cuts.front().Position+n,cuts.back().Key,cuts.front().Key);
        else {add(0,cuts.front().Position,endpointKey(false),cuts.front().Key);add(cuts.back().Position,n,cuts.back().Key,endpointKey(true));}
    }
    if(gaps&&!result.Outputs.empty())ExecutionBlock(result,in);
    return result;
}
void SameSpace(const SEditableGeometry& a,const SEditableGeometry& b)
{Require(a.Authority==b.Authority&&a.FrameId==b.FrameId&&ExactData(a.Plane)==ExactData(b.Plane)&&ExactData(a.SupportSurface)==ExactData(b.SupportSurface)&&a.Meaning==b.Meaning,"TOOLPATH_MERGE_SPACE_OR_MEANING_CONFLICT");}
template<class T> void Translate(T& value,Vector3 delta)
{
    const auto point=[&](Point3& p){p.X+=delta.X;p.Y+=delta.Y;p.Z+=delta.Z;};
    if constexpr(requires{value.Location;value.XDirection;})point(value.Location);
    else if constexpr(requires{value.Start;value.End;}){point(value.Start);point(value.End);}
    else if constexpr(requires{value.Axis;}){point(value.Axis.Location);if constexpr(requires{value.Basis;})std::visit([&](auto& curve){Translate(curve,delta);},value.Basis);}
    else if constexpr(requires{value.Placement;})Translate(value.Placement,delta);
    else if constexpr(requires{value.Basis;})
    {if constexpr(requires{std::get_if<Segment3>(&value.Basis);})std::visit([&](auto& curve){Translate(curve,delta);},value.Basis);else Translate(value.Basis,delta);}
    else if constexpr(requires{value.Points;})for(auto& p:value.Points)point(p);
    else if constexpr(requires{value.Poles;})for(auto& p:value.Poles)point(p);
    else static_assert(sizeof(T)==0,"Missing exact translation implementation");
}
}

void RegisterStandardToolpathOperations(CToolpathOperationRegistry& registry)
{
    auto source=Type("icax.source",{{"value",Field("object")}},[](const auto& in,const auto&){SOperationCandidate out;out.Outputs.push_back({"main",{}, {},ToolpathFromData(MapAt(in.Parameters,"value"))});return out;},0,0);
    source.ReadChannels={};source.InheritExecutionBlock=false;registry.Register(std::move(source));
    auto reverse=Type("icax.edit.reverse",{},[](const auto& in,const auto&){auto output=One(in.Inputs.at(0));auto& g=output.Value.Geometry;
        std::reverse(g.Segments.begin(),g.Segments.end());for(auto& s:g.Segments){auto r=Range(s);r.Reversed=!r.Reversed;SetRange(s,r);for(auto& p:s.Sources){p.SourceRange.Reversed=!p.SourceRange.Reversed;p.OutputRange.Reversed=!p.OutputRange.Reversed;}}
        ReverseMaterialSide(output.Value);return SOperationCandidate{{std::move(output)}};});
    reverse.ReadChannels.insert("material");reverse.WriteChannels.insert("material");registry.Register(std::move(reverse));
    registry.Register(Type("icax.edit.start",{{"anchor",Field("object")}},[](const auto& in,const auto&){const auto& input=in.Inputs.at(0);Require(input.Value.Geometry.Closed,"TOOLPATH_START_REQUIRES_CLOSED");auto output=One(input);
        const auto p=Locate(AnchorFromData(MapAt(in.Parameters,"anchor")),input),n=static_cast<double>(input.Value.Geometry.Segments.size());
        if(p>1e-12&&p<n-1e-12){output.Value.Geometry=Slice(input,p,n,"start-tail");auto before=Slice(input,0,p,"start-head");auto& s=output.Value.Geometry.Segments;s.insert(s.end(),before.Segments.begin(),before.Segments.end());output.Value.Geometry.Closed=true;}
        return SOperationCandidate{{std::move(output)}};}));
    auto split=Type("icax.edit.split",{{"cuts",Field("array")},{"retainFirstIdentity",Field("boolean",false)},{"retainedIdentityAnchor",Field("object",false)},{"identityTraversal",Field("object",false)}},[](const auto& in,const auto&){return Partition(in,false);});
    split.NormalizeParameters=NormalizePartitionIdentity;
    split.InheritExecutionBlock=false;split.DeclaredOutputRoles.clear();split.DeclaredOutputIdentityInputs.clear();registry.Register(std::move(split));
    auto segment=Type("icax.edit.segment",{{"cuts",Field("array")},{"identityTraversal",Field("object",false)}},[](const auto& in,const auto&){auto candidate=Partition(in,false);for(auto& output:candidate.Outputs)output.OrganizationInputSlot=in.Inputs[0].Binding.Slot;ExecutionBlock(candidate,in);return candidate;});segment.NormalizeParameters=NormalizePartitionIdentity;segment.DeclaredOutputRoles.clear();segment.DeclaredOutputIdentityInputs.clear();registry.Register(std::move(segment));
    auto promote=Type("icax.edit.promote",{},[](const auto& in,const auto&){return SOperationCandidate{{One(in.Inputs[0])}};});promote.InheritExecutionBlock=false;registry.Register(std::move(promote));
    const auto normalizeGaps=[](const std::vector<SFrozenPathInput>& inputs,const Map& parameters)
    {Require(inputs.size()==1,"TOOLPATH_GAP_INPUT_REQUIRED");auto result=parameters;Array gaps;for(const auto& item:ArrayAt(parameters,"gaps")){auto gap=item.To<Map>();gap["range"]=ToData(NormalizeRange(RangeFromData(MapAt(gap,"range")),inputs[0]));gaps.emplace_back(std::move(gap));}result["gaps"]=std::move(gaps);return result;};
    auto microjoint=Type("icax.microjoint",{{"gaps",Field("array")}},[](const auto& in,const auto&){return Partition(in,true);});microjoint.NormalizeParameters=normalizeGaps;microjoint.DeclaredOutputRoles.clear();microjoint.DeclaredOutputIdentityInputs.clear();registry.Register(std::move(microjoint));
    auto removeRanges=Type("icax.edit.removeRanges",{{"gaps",Field("array")}},[](const auto& in,const auto&){auto result=Partition(in,true);result.Constraints["removedIntervals"]=result.Constraints.at("retainedIntervals");result.Constraints.erase("retainedIntervals");return result;});removeRanges.NormalizeParameters=normalizeGaps;removeRanges.DeclaredOutputRoles.clear();removeRanges.DeclaredOutputIdentityInputs.clear();registry.Register(std::move(removeRanges));
    auto merge=Type("icax.edit.merge",{{"channelResolution",Field("object",false)}},[](const auto& in,const auto& geometry){auto value=MergeInput(in.Inputs.at(0));Require(!value.Geometry.Closed,"TOOLPATH_MERGE_CLOSED_INPUT");
        for(std::size_t i=1;i<in.Inputs.size();++i){Cancel(in);auto next=MergeInput(in.Inputs[i]);SameSpace(value.Geometry,next.Geometry);value.Geometry.Tolerance=std::min(value.Geometry.Tolerance,next.Geometry.Tolerance);Require(!next.Geometry.Closed,"TOOLPATH_MERGE_CLOSED_INPUT");
            auto& segments=value.Geometry.Segments;const auto& last=segments.back();const auto& first=next.Geometry.Segments.front();Require(Distance(geometry.Evaluate(value.Geometry,last,Last(Range(last))),geometry.Evaluate(next.Geometry,first,First(Range(first))))<=std::min(value.Geometry.Tolerance,next.Geometry.Tolerance),"TOOLPATH_MERGE_REQUIRES_EXPLICIT_BRIDGE");
            segments.insert(segments.end(),next.Geometry.Segments.begin(),next.Geometry.Segments.end());}
        const auto& a=value.Geometry.Segments.front();const auto& b=value.Geometry.Segments.back();value.Geometry.Closed=Distance(geometry.Evaluate(value.Geometry,a,First(Range(a))),geometry.Evaluate(value.Geometry,b,Last(Range(b))))<=value.Geometry.Tolerance;
        return SOperationCandidate{{SOutputCandidate{"merged",{}, {},std::move(value)}}};},2,100000);merge.InheritExecutionBlock=false;merge.DeclaredOutputRoles={"merged"};registry.Register(std::move(merge));
    auto bridge=Type("icax.edit.bridge",{{"connectors",Field("array")},{"mode",Field("string")},{"channelResolution",Field("object",false)}},[](const auto& in,const auto& geometry)
    {
        const auto connectors=ArrayAt(in.Parameters,"connectors");Require(connectors.size()+1==in.Inputs.size(),"TOOLPATH_BRIDGE_ARITY");
        const auto mode=Text(in.Parameters,"mode");Require(mode=="Cut"||mode=="NoCut","TOOLPATH_BRIDGE_MODE_REQUIRED");auto value=MergeInput(in.Inputs.at(0));Require(!value.Geometry.Closed,"TOOLPATH_BRIDGE_CLOSED_INPUT");
        for(std::size_t i=1;i<in.Inputs.size();++i)
        {
            auto connector=ToolpathFromData(connectors[i-1].To<Map>());auto next=MergeInput(in.Inputs[i]);SameSpace(value.Geometry,connector.Geometry);SameSpace(value.Geometry,next.Geometry);value.Geometry.Tolerance=std::min({value.Geometry.Tolerance,connector.Geometry.Tolerance,next.Geometry.Tolerance});Require(!connector.Geometry.Closed&&!next.Geometry.Closed,"TOOLPATH_BRIDGE_CLOSED_INPUT");geometry.Validate(connector.Geometry);
            auto& segments=value.Geometry.Segments;const auto& a=segments.back();const auto& b=connector.Geometry.Segments.front();const auto& c=connector.Geometry.Segments.back();const auto& d=next.Geometry.Segments.front();
            Require(Distance(geometry.Evaluate(value.Geometry,a,Last(Range(a))),geometry.Evaluate(connector.Geometry,b,First(Range(b))))<=value.Geometry.Tolerance
                &&Distance(geometry.Evaluate(connector.Geometry,c,Last(Range(c))),geometry.Evaluate(next.Geometry,d,First(Range(d))))<=value.Geometry.Tolerance,"TOOLPATH_BRIDGE_ENDPOINT_MISMATCH");
            for(auto& segment:connector.Geometry.Segments){segment.Id=Key(in.OperationId)+"/bridge/"+in.Inputs[i-1].Binding.Slot+"/"+in.Inputs[i].Binding.Slot+"/"+segment.Id;segment.Sources.clear();segment.ConstructionRole=mode=="Cut"?"BridgeCut":"BridgeNoCut";segments.push_back(std::move(segment));}
            segments.insert(segments.end(),next.Geometry.Segments.begin(),next.Geometry.Segments.end());
        }
        return SOperationCandidate{{SOutputCandidate{"bridged",{}, {},std::move(value)}}};
    },2,100000);bridge.InheritExecutionBlock=false;bridge.DeclaredOutputRoles={"bridged"};registry.Register(std::move(bridge));
    registry.Register(Type("icax.edit.translate",{{"delta",Field("array")}},[](const auto& in,const auto&)
    {
        const auto delta=ArrayAt(in.Parameters,"delta");Require(delta.size()==3,"TOOLPATH_TRANSLATION_INVALID");const Vector3 v{NumericValue(delta[0]),NumericValue(delta[1]),NumericValue(delta[2])};Require(std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z),"TOOLPATH_TRANSLATION_INVALID");
        auto output=One(in.Inputs.at(0));auto& g=output.Value.Geometry;
        if(g.Authority==EGeometryAuthority::Spatial3D)for(auto& segment:g.Segments)
        {
            if(auto* offset=std::get_if<DirectionalCurveOffset3>(&segment.Curve))
            {
                std::visit([&](auto& curve){Translate(curve,v);},offset->Source.Curve);
                // A vector field's coefficients are directions, not positions.
                // A surface normal field follows its translated support surface.
                if(auto* field=std::get_if<SurfaceNormalField3>(&offset->ReferenceDirection))std::visit([&](auto& surface){Translate(surface,v);},field->Surface);
            }
            else std::visit([&](auto& curve){Translate(curve,v);},std::get<CurveSegment3>(segment.Curve).Curve);
        }
        else if(g.Authority==EGeometryAuthority::Planar2D)Translate(g.Plane,v);
        if(g.SupportSurface)std::visit([&](auto& surface){Translate(surface,v);},*g.SupportSurface);
        return SOperationCandidate{{std::move(output)}};
    }));
    auto copy=Type("icax.edit.copy",{},[](const auto& in,const auto&){SOperationCandidate out;for(const auto& input:in.Inputs){auto value=Whole(input);out.Outputs.push_back({"copy/"+Key(input.Binding.Source),{}, {},std::move(value)});}return out;},1,100000);copy.InheritExecutionBlock=false;copy.DeclaredOutputRoles.clear();registry.Register(std::move(copy));
    registry.Register(Type("icax.edit.replaceGeometry",{{"value",Field("object")}},[](const auto& in,const auto&){auto output=One(in.Inputs.at(0));const auto replacement=ToolpathFromData(MapAt(in.Parameters,"value"));SameSpace(output.Value.Geometry,replacement.Geometry);output.Value=replacement;
        // Replacement geometry must explicitly carry the exact old-to-new maps.
        return SOperationCandidate{{std::move(output)}};}));
    auto channelEdit=Type("icax.edit.channels",{{"patch",Field("object")}},[](const auto& in,const auto&){auto output=One(in.Inputs.at(0));for(const auto& [key,value]:MapAt(in.Parameters,"patch"))output.Value.Channels[key]=value;return SOperationCandidate{{std::move(output)}};});
    channelEdit.ReadChannels={"*"};channelEdit.WriteChannels={"*"};channelEdit.PropagateUnwrittenChannels=false;registry.Register(std::move(channelEdit));
    registry.Register(Type("icax.lead.linear",{{"atStart",Field("boolean")},{"offset",Field("array")}},[](const auto& in,const auto& geometry){const auto& input=in.Inputs.at(0);auto output=One(input);auto& g=output.Value.Geometry;
        Require(g.Authority!=EGeometryAuthority::SurfaceUV,"TOOLPATH_LEAD_UV_REQUIRES_SURFACE_ALGORITHM");const auto atStart=Flag(in.Parameters,"atStart");const auto offset=ArrayAt(in.Parameters,"offset");Require(offset.size()==3,"TOOLPATH_LEAD_OFFSET_INVALID");
        const double x=NumericValue(offset[0]),y=NumericValue(offset[1]),z=NumericValue(offset[2]);Require(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&std::hypot(x,y,z)>g.Tolerance,"TOOLPATH_LEAD_OFFSET_INVALID");
        const auto& boundary=atStart?g.Segments.front():g.Segments.back();const auto r=Range(boundary);const auto parameter=atStart?First(r):Last(r);SEditableSegment lead;lead.Id=Key(in.OperationId)+"/lead";lead.ConstructionRole="Lead";
        if(g.Authority==EGeometryAuthority::Spatial3D){const auto p=geometry.Evaluate(g,boundary,parameter);const Point3 q{p.X+x,p.Y+y,p.Z+z};lead.Curve=CurveSegment3{Segment3{atStart?q:p,atStart?p:q},{0,1,false}};}
        else {Require(z==0,"TOOLPATH_PLANAR_LEAD_NONPLANAR");const auto& c=std::get<CurveSegment2>(boundary.Curve);SEditableGeometry local=g;local.Plane=Placement3{};const auto p=geometry.Evaluate(local,boundary,parameter);const Point2 a{p.X,p.Y},b{p.X+x,p.Y+y};lead.Curve=CurveSegment2{Segment2{atStart?b:a,atStart?a:b},{0,1,false}};}
        if(atStart)g.Segments.insert(g.Segments.begin(),std::move(lead));else g.Segments.push_back(std::move(lead));g.Closed=false;
        SOperationCandidate result{{std::move(output)}};geometry.ValidateManufacturing("icax.lead.linear",in,result);return result;}));
}
}

namespace iCAX::CAM::Intent::Editable
{
void ValidateParameterSchema(const IntentMap& schema,const IntentMap& parameters)
{
    const auto fields=schema.contains("fields")?MapAt(schema,"fields"):Map{};
    for(const auto& [key,value]:parameters)Require(fields.contains(key)||Flag(schema,"additionalProperties"),"TOOLPATH_UNKNOWN_PARAMETER: "+key);
    for(const auto& [key,description]:fields)
    {
        const auto field=description.To<Map>();const auto i=parameters.find(key);
        if(i==parameters.end()){Require(!Flag(field,"required"),"TOOLPATH_PARAMETER_REQUIRED: "+key);continue;}
        const auto& v=i->second;const auto type=Text(field,"type");
        Require((type=="number"&&IsNumericValue(v))||(type=="integer"&&(v.Is<int>()||v.Is<unsigned int>()||v.Is<long long>()||v.Is<unsigned long long>()))||(type=="boolean"&&v.Is<bool>())||(type=="string"&&v.Is<std::string>())||(type=="object"&&v.Is<Map>())||(type=="array"&&v.Is<Array>()),"TOOLPATH_PARAMETER_TYPE: "+key);
        if(type=="number"){const auto n=NumericValue(v);if(field.contains("minimum"))Require(n>=Number(field,"minimum"),"TOOLPATH_PARAMETER_MINIMUM");if(field.contains("maximum"))Require(n<=Number(field,"maximum"),"TOOLPATH_PARAMETER_MAXIMUM");}
        if(field.contains("choices")){bool found=false;for(const auto& choice:ArrayAt(field,"choices"))found|=choice==v;Require(found,"TOOLPATH_PARAMETER_CHOICE");}
    }
}
}
