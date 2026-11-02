#include "pch.h"
#include "EditableToolpathProjection.h"
#include "EditableToolpathInternal.h"

namespace iCAX::CAM::Intent
{
using namespace Editable;
namespace
{
double MapParameter(double parameter,ParameterRange from,ParameterRange to)
{return First(to)+(parameter-First(from))/(Last(from)-First(from))*(Last(to)-First(to));}
ParameterRange Between(double first,double last){return {std::min(first,last),std::max(first,last),first>last};}
void AddUnique(std::vector<SSpanMapping>& mappings,SSpanMapping mapping)
{
    const auto same=[&](const SSpanMapping& other){return other.Source==mapping.Source&&other.SourceSegmentId==mapping.SourceSegmentId
        &&other.SourceRange.First==mapping.SourceRange.First&&other.SourceRange.Last==mapping.SourceRange.Last&&other.SourceRange.Reversed==mapping.SourceRange.Reversed
        &&other.OutputRange.First==mapping.OutputRange.First&&other.OutputRange.Last==mapping.OutputRange.Last&&other.OutputRange.Reversed==mapping.OutputRange.Reversed
        &&other.Occurrence==mapping.Occurrence&&other.SourceGeometryFingerprint==mapping.SourceGeometryFingerprint;};
    if(std::none_of(mappings.begin(),mappings.end(),same))mappings.push_back(std::move(mapping));
}
SPathRange NativeSelection(const SFrozenPathInput& input,const std::string& segment,double first,double last)
{
    SPathRange range{{input.Binding.Source,segment,first},{input.Binding.Source,segment,last},false};
    range.TraversalDirection=Locate(range.Start,input)<=Locate(range.End,input)?1:-1;return NormalizeRange(std::move(range),input);
}
void CheckPointConservation(const SToolpathValue& before,const SToolpathValue& after)
{
    for(const auto& [name,value]:before.Channels)if(value.Is<Map>()&&Text(value.To<Map>(),"attachment")=="Points")
    {
        const auto previous=ArrayAt(value.To<Map>(),"entries");const auto found=after.Channels.find(name);
        Require(found!=after.Channels.end()&&found->second.Is<Map>()&&ArrayAt(found->second.To<Map>(),"entries").size()==previous.size(),
            "TOOLPATH_SELECTION_PROJECTION_NEEDS_REBIND_POINT_LOSS");
    }
}
SToolpathValue Transfer(const SFrozenPathInput& from,SToolpathValue output,IntentId operation,const IToolpathGeometryServices& geometry)
{
    SOperationEvaluation evaluation{operation,{},{},{},{}};evaluation.Inputs.push_back(from);SOperationCandidate candidate;candidate.Outputs.push_back({"main",{},std::size_t(0),std::move(output)});
    SOperationType type;PropagateChannels(evaluation,candidate,type);geometry.Validate(candidate.Outputs.front().Value.Geometry);return std::move(candidate.Outputs.front().Value);
}
}

SProjectedToolpathSelection ProjectToolpathSelection(const SFrozenPathInput& selection,const SFrozenPathInput& upstream,
    IntentId operation,const std::set<std::string>& removable,const IToolpathGeometryServices& geometry)
{
    Require(!operation.is_nil()&&!selection.Value.Geometry.Segments.empty()&&!upstream.Value.Geometry.Segments.empty(),"TOOLPATH_SELECTION_PROJECTION_INPUT_REQUIRED");
    geometry.Validate(selection.Value.Geometry);geometry.Validate(upstream.Value.Geometry);
    const auto selectedGeometry=MapAt(ToData(selection.Value),"geometry"),upstreamGeometry=MapAt(ToData(upstream.Value),"geometry");
    for(const auto* key:{"authority","frame","plane","support"})Require(At(selectedGeometry,key)==At(upstreamGeometry,key),"TOOLPATH_SELECTION_PROJECTION_FRAME_MISMATCH");
    const auto selectionFingerprint=ToolpathFingerprint(selection.Value,{"geometry","topology","traversal","frame"});
    const auto upstreamFingerprint=ToolpathFingerprint(upstream.Value,{"geometry","topology","traversal","frame"});
    SProjectedToolpathSelection result;result.Value=upstream.Value;result.Value.Geometry.Segments.clear();result.Value.Geometry.Closed=selection.Value.Geometry.Closed;result.Value.Channels.clear();
    struct Piece{double First,Last;std::size_t Upstream;SSpanMapping Mapping;};
    for(std::size_t index=0;index<selection.Value.Geometry.Segments.size();++index)
    {
        const auto& selected=selection.Value.Geometry.Segments[index];const auto current=Range(selected);std::vector<Piece> pieces;
        for(std::size_t sourceIndex=0;sourceIndex<upstream.Value.Geometry.Segments.size();++sourceIndex)
        {
            const auto& source=upstream.Value.Geometry.Segments[sourceIndex];
            const auto add=[&](const SSpanMapping& mapping)
            {
                Require(mapping.Precision=="Exact"&&mapping.ErrorBound==0,"TOOLPATH_SELECTION_PROJECTION_NEEDS_REBIND_PRECISION");
                Require(mapping.SourceGeometryFingerprint.empty()||mapping.SourceGeometryFingerprint==upstreamFingerprint,"TOOLPATH_SELECTION_PROJECTION_NEEDS_REBIND_SOURCE_CHANGED");
                const auto first=std::max(current.First,mapping.OutputRange.First),last=std::min(current.Last,mapping.OutputRange.Last);if(last<=first+1e-12)return;
                Piece piece{first,last,sourceIndex,mapping};
                const auto duplicate=std::find_if(pieces.begin(),pieces.end(),[&](const auto& old){return old.First==first&&old.Last==last&&old.Upstream==sourceIndex
                    &&MapParameter(first,old.Mapping.OutputRange,old.Mapping.SourceRange)==MapParameter(first,mapping.OutputRange,mapping.SourceRange)
                    &&MapParameter(last,old.Mapping.OutputRange,old.Mapping.SourceRange)==MapParameter(last,mapping.OutputRange,mapping.SourceRange);});
                if(duplicate==pieces.end())pieces.push_back(std::move(piece));
            };
            if(selection.Binding.Source==upstream.Binding.Source&&selected.Id==source.Id)add({upstream.Binding.Source,source.Id,current,current,"Exact",0,0,upstreamFingerprint});
            for(const auto& mapping:selected.Sources)if(mapping.Source==upstream.Binding.Source&&mapping.SourceSegmentId==source.Id)add(mapping);
        }
        std::sort(pieces.begin(),pieces.end(),[](const auto& a,const auto& b){return a.First<b.First;});double cursor=current.First;
        if(pieces.empty())
        {
            Require(!selected.ConstructionRole.empty()&&removable.contains(selected.ConstructionRole),"TOOLPATH_SELECTION_PROJECTION_NEEDS_REBIND_CONSTRUCTION");
            result.ExcludedRanges.push_back(NativeSelection(selection,selected.Id,First(current),Last(current)));continue;
        }
        for(const auto& piece:pieces){Require(std::abs(piece.First-cursor)<1e-10,"TOOLPATH_SELECTION_PROJECTION_NEEDS_REBIND_AMBIGUOUS_OR_GAP");cursor=piece.Last;}
        Require(std::abs(cursor-current.Last)<1e-10,"TOOLPATH_SELECTION_PROJECTION_NEEDS_REBIND_SPAN_LOSS");if(current.Reversed)std::reverse(pieces.begin(),pieces.end());
        for(std::size_t part=0;part<pieces.size();++part)
        {
            const auto& piece=pieces[part];const auto selectedRange=ParameterRange{piece.First,piece.Last,current.Reversed};
            const auto a=MapParameter(First(selectedRange),piece.Mapping.OutputRange,piece.Mapping.SourceRange),b=MapParameter(Last(selectedRange),piece.Mapping.OutputRange,piece.Mapping.SourceRange);
            const auto& native=upstream.Value.Geometry.Segments[piece.Upstream];const auto nativeRange=Range(native);
            Require(std::min(a,b)>=nativeRange.First-1e-12&&std::max(a,b)<=nativeRange.Last+1e-12,"TOOLPATH_SELECTION_PROJECTION_NEEDS_REBIND_SOURCE_RANGE");
            const auto position=[&](double parameter){return piece.Upstream+(parameter-First(nativeRange))/(Last(nativeRange)-First(nativeRange));};
            auto clipped=Slice(upstream,std::min(position(a),position(b)),std::max(position(a),position(b)),"projection");Require(clipped.Segments.size()==1,"TOOLPATH_SELECTION_PROJECTION_SEGMENT_INVALID");
            auto output=std::move(clipped.Segments.front());const auto beforeRange=Range(output);const auto range=Between(a,b);SetRange(output,range);
            output.Id="projection/"+selected.Id+"/"+std::to_string(part);output.ConstructionRole=native.ConstructionRole;
            for(auto& mapping:output.Sources)if(beforeRange.Reversed!=range.Reversed){mapping.SourceRange.Reversed=!mapping.SourceRange.Reversed;mapping.OutputRange.Reversed=!mapping.OutputRange.Reversed;}
            for(const auto& origin:selected.Sources)
            {
                const auto low=std::max(piece.First,origin.OutputRange.First),high=std::min(piece.Last,origin.OutputRange.Last);if(high<=low+1e-12)continue;
                auto mapping=origin;mapping.SourceRange=Between(MapParameter(current.Reversed?high:low,origin.OutputRange,origin.SourceRange),MapParameter(current.Reversed?low:high,origin.OutputRange,origin.SourceRange));
                mapping.OutputRange=Between(MapParameter(current.Reversed?high:low,piece.Mapping.OutputRange,piece.Mapping.SourceRange),MapParameter(current.Reversed?low:high,piece.Mapping.OutputRange,piece.Mapping.SourceRange));
                AddUnique(output.Sources,std::move(mapping));
            }
            AddUnique(output.Sources,{selection.Binding.Source,selected.Id,selectedRange,range,"Exact",0,0,selectionFingerprint});
            result.Value.Geometry.Segments.push_back(std::move(output));
        }
    }
    Require(!result.Value.Geometry.Segments.empty(),"TOOLPATH_SELECTION_PROJECTION_EMPTY");
    try{result.Value=Transfer(selection,std::move(result.Value),operation,geometry);}
    catch(const std::invalid_argument& error){throw std::invalid_argument(std::string("TOOLPATH_SELECTION_PROJECTION_NEEDS_REBIND: ")+error.what());}
    CheckPointConservation(selection.Value,result.Value);return result;
}

std::vector<SPathRange> ProjectToolpathRangesToInput(const SFrozenPathInput& carrier,const std::vector<SPathRange>& ranges,const SFrozenPathInput& target)
{
    std::vector<SPathRange> result;const auto fingerprint=ToolpathFingerprint(target.Value,{"geometry","topology","traversal","frame"});
    for(const auto& range:ranges)for(const auto& window:RangeWindows(range,carrier))
        for(std::size_t i=0;i<carrier.Value.Geometry.Segments.size();++i)
        {
            const auto& segment=carrier.Value.Geometry.Segments[i];const auto domain=Range(segment);const auto first=std::max(window.first,double(i)),last=std::min(window.second,double(i+1));if(last<=first+1e-12)continue;
            const auto a=First(domain)+(first-i)*(Last(domain)-First(domain)),b=First(domain)+(last-i)*(Last(domain)-First(domain));
            for(const auto& mapping:segment.Sources)if(mapping.Source==target.Binding.Source)
            {
                Require(mapping.Precision=="Exact"&&mapping.ErrorBound==0&&(mapping.SourceGeometryFingerprint.empty()||mapping.SourceGeometryFingerprint==fingerprint),"TOOLPATH_SELECTION_PROJECTION_NEEDS_REBIND_EXCLUSION");
                const auto low=std::max(std::min(a,b),mapping.OutputRange.First),high=std::min(std::max(a,b),mapping.OutputRange.Last);if(high<=low+1e-12)continue;
                const auto mapped=NativeSelection(target,mapping.SourceSegmentId,MapParameter(a>b?high:low,mapping.OutputRange,mapping.SourceRange),MapParameter(a>b?low:high,mapping.OutputRange,mapping.SourceRange));
                if(std::none_of(result.begin(),result.end(),[&](const auto& old){return ToData(old)==ToData(mapped);}))result.push_back(mapped);
            }
        }
    return result;
}

SToolpathValue FinalizeProjectedToolpath(const SFrozenPathInput& finalStage,IntentId operation,const std::set<IntentId>& privateOperations,const IToolpathGeometryServices& geometry)
{
    auto value=finalStage.Value;const auto closed=value.Geometry.Closed;
    value.Geometry=Slice(finalStage,0,double(value.Geometry.Segments.size()),"final-projection");value.Geometry.Closed=closed;
    value=Transfer(finalStage,std::move(value),operation,geometry);
    for(auto& segment:value.Geometry.Segments)std::erase_if(segment.Sources,[&](const auto& source){return privateOperations.contains(source.Source.OperationId);});
    const auto check=[&](auto&& self,const Variant& value)->void
    {
        if(value.Is<Map>()){const auto& object=std::get<Map>(value.m_Value);if(object.contains("port")&&object.contains("segmentId")&&object.contains("parameter")){const auto port=PortFromData(MapAt(object,"port"));Require(port.OperationId==operation||!privateOperations.contains(port.OperationId),"TOOLPATH_SELECTION_PROJECTION_PRIVATE_ANCHOR");}for(const auto& [key,item]:object)self(self,item);}
        else if(value.Is<Array>())for(const auto& item:std::get<Array>(value.m_Value))self(self,item);
    };
    for(const auto& [name,channel]:value.Channels)check(check,channel);geometry.Validate(value.Geometry);return value;
}
}
