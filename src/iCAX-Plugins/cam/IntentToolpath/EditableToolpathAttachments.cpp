#include "pch.h"
#include "EditableToolpathInternal.h"
#include <tuple>
#include <bit>

namespace iCAX::CAM::Intent::Editable
{
namespace
{
struct SLocated {std::size_t Segment;double Parameter,Position;};
std::optional<SLocated> OnOutput(const SPathAnchor& anchor,const SOutputCandidate& output,IntentId operation)
{
    std::vector<SLocated> locations;const auto& g=output.Value.Geometry;
    for(std::size_t i=0;i<g.Segments.size();++i)
    {
        const auto& segment=g.Segments[i];const auto r=Range(segment);
        const auto add=[&](double t){if(t>=r.First-1e-12&&t<=r.Last+1e-12)locations.push_back({i,t,i+(t-First(r))/(Last(r)-First(r))});};
        if(anchor.Port==SPortRef{operation,NamedID(Key(operation)+"/port/"+output.RoleKey)}&&anchor.SegmentId==segment.Id)add(anchor.Parameter);
        for(const auto& source:segment.Sources)if(source.Source==anchor.Port&&source.SourceSegmentId==anchor.SegmentId&&source.Occurrence==anchor.Occurrence
            &&anchor.Parameter>=source.SourceRange.First-1e-12&&anchor.Parameter<=source.SourceRange.Last+1e-12)
        {Require(source.Precision=="Exact","TOOLPATH_ATTACHMENT_PRECISION_UNSUPPORTED");const auto f=(anchor.Parameter-First(source.SourceRange))/(Last(source.SourceRange)-First(source.SourceRange));add(First(source.OutputRange)+f*(Last(source.OutputRange)-First(source.OutputRange)));}
    }
    if(locations.empty())return {};
    std::sort(locations.begin(),locations.end(),[](const auto& a,const auto& b){return a.Position<b.Position;});
    locations.erase(std::unique(locations.begin(),locations.end(),[](const auto& a,const auto& b){return std::abs(a.Position-b.Position)<1e-11;}),locations.end());
    if(locations.size()>1&&g.Closed&&anchor.Side=="Start")locations.resize(1);
    if(locations.size()>1&&g.Closed&&anchor.Side=="End")locations.erase(locations.begin(),locations.end()-1);
    Require(locations.size()==1,"TOOLPATH_NEEDS_REBIND_ATTACHMENT");return locations[0];
}
SPathAnchor OutputAnchor(IntentId operation,const SOutputCandidate& output,const SLocated& p)
{return {{operation,NamedID(Key(operation)+"/port/"+output.RoleKey)},output.Value.Geometry.Segments[p.Segment].Id,p.Parameter,0,{}};}
bool Related(const SOutputCandidate& output,const SPortRef& input)
{for(const auto& segment:output.Value.Geometry.Segments)for(const auto& origin:segment.Sources)if(origin.Source==input)return true;return false;}
void AppendMappedChannel(Map& target,const std::string& channel,const Map& envelope,Array entries)
{
    if(const auto found=target.find(channel);found!=target.end())
    {
        auto& combined=std::get<Array>(std::get<Map>(found->second.m_Value).at("entries").m_Value);
        combined.insert(combined.end(),std::make_move_iterator(entries.begin()),std::make_move_iterator(entries.end()));
    }
    else
    {
        Map mapped;for(const auto& [name,value]:envelope)if(name!="entries")mapped.emplace(name,value);
        mapped["entries"]=std::move(entries);target.emplace(channel,std::move(mapped));
    }
}
}
void PropagateChannels(const SOperationEvaluation& in,SOperationCandidate& candidate,const SOperationType& type)
{
    if(!type.PropagateUnwrittenChannels||in.Inputs.empty())return;
    const Map empty;
    // All channel projections read the same immutable inputs. Reuse exact
    // source locations across channels and repeated provenance mappings.
    std::map<std::tuple<const SFrozenPathInput*,std::string,double>,double> locations;
    const auto locate=[&](const SFrozenPathInput& input,const std::string& segment,double parameter)
    {
        const auto key=std::make_tuple(&input,segment,parameter);
        if(const auto found=locations.find(key);found!=locations.end())return found->second;
        const auto position=Locate({input.Binding.Source,segment,parameter},input);locations.emplace(key,position);return position;
    };
    std::optional<SOperationEvaluation> normalizedStorage;
    if(in.Parameters.contains("channelResolution"))normalizedStorage=in;
    if(normalizedStorage)for(const auto& [channel,rule]:MapAt(in.Parameters,"channelResolution"))
    {
        const auto policy=rule.To<Map>();const auto action=Text(policy,"action");Require(action=="UseValue"||action=="PreserveIntervals","TOOLPATH_CHANNEL_RESOLUTION_INVALID");
        for(auto& input:normalizedStorage->Inputs)
        {
            if(action=="UseValue")input.Value.Channels[channel]=At(policy,"value");
            else if(input.Value.Channels.contains(channel))
            {
                const auto previous=input.Value.Channels[channel];
                if(previous.Is<Map>()&&Text(previous.To<Map>(),"attachment","Path")!="Path")continue;
                input.Value.Channels[channel]=Map{{"attachment",std::string("Intervals")},{"schemaVersion",1u},
                    {"entries",Array{Map{{"range",ToData(SPathRange{AnchorAt(input,0),AnchorAt(input,static_cast<double>(input.Value.Geometry.Segments.size())),false})},{"value",previous}}}}};
            }
        }
    }
    const auto& normalized=normalizedStorage?*normalizedStorage:in;
    std::vector<IntentMap> values(candidate.Outputs.size());
    for(const auto& input:normalized.Inputs)for(const auto& [channel,value]:input.Value.Channels)
    {
        if(type.WriteChannels.contains(channel)||type.WriteChannels.contains("*"))continue;
        const auto& envelope=value.Is<Map>()?std::get<Map>(value.m_Value):empty;const auto attachment=Text(envelope,"attachment","Path");
        if(attachment=="Path")
        {
            for(std::size_t i=0;i<candidate.Outputs.size();++i)if(Related(candidate.Outputs[i],input.Binding.Source))
            {const auto old=values[i].find(channel);Require(old==values[i].end()||old->second==value,"TOOLPATH_MERGE_CHANNEL_CONFLICT: "+channel);values[i][channel]=value;}
        }
        else if(attachment=="Points")
        {
            std::vector<Array> entries(candidate.Outputs.size());
            for(const auto& item:std::get<Array>(At(envelope,"entries").m_Value))
            {
                const auto& entry=std::get<Map>(item.m_Value);const auto anchor=AnchorFromData(MapAt(entry,"anchor"));std::vector<std::pair<std::size_t,SLocated>> matches;
                for(std::size_t i=0;i<candidate.Outputs.size();++i)if(Related(candidate.Outputs[i],input.Binding.Source))
                    if(const auto found=OnOutput(anchor,candidate.Outputs[i],in.OperationId))matches.emplace_back(i,*found);
                if(matches.size()>1)
                {
                    const auto policy=Text(entry,"boundaryPolicy",anchor.Side);
                    if(policy=="Before")std::erase_if(matches,[&](const auto& m){return m.second.Position<candidate.Outputs[m.first].Value.Geometry.Segments.size()-1e-11;});
                    else if(policy=="After")std::erase_if(matches,[](const auto& m){return m.second.Position>1e-11;});
                    else Require(policy=="Duplicate","TOOLPATH_ATTACHMENT_BOUNDARY_DECISION_REQUIRED");
                }
                for(const auto& [index,position]:matches){auto mapped=entry;mapped["anchor"]=ToData(OutputAnchor(in.OperationId,candidate.Outputs[index],position));entries[index].emplace_back(std::move(mapped));}
            }
            for(std::size_t i=0;i<entries.size();++i)if(Related(candidate.Outputs[i],input.Binding.Source))
                AppendMappedChannel(values[i],channel,envelope,std::move(entries[i]));
        }
        else if(attachment=="Intervals")
        {
            for(std::size_t i=0;i<candidate.Outputs.size();++i)if(Related(candidate.Outputs[i],input.Binding.Source))
            {
                Array entries;const auto& output=candidate.Outputs[i];
                struct SIntervalMapping{std::size_t Segment;double First,Last,OutputFirst,OutputLast;};
                std::optional<std::vector<SIntervalMapping>> mappings;
                for(const auto& item:std::get<Array>(At(envelope,"entries").m_Value))
                {
                    const auto& entry=std::get<Map>(item.m_Value);const auto range=RangeFromData(MapAt(entry,"range"));const auto windows=RangeWindows(range,input);
                    // Several provenance routes may describe the same physical
                    // projection. Emit that projection once per authored entry;
                    // distinct entries, pieces and parameter domains stay distinct.
                    std::set<std::tuple<std::size_t,std::uint64_t,std::uint64_t>> emitted;
                    // A source span has the same input locations for every
                    // interval. Resolve it once, retaining entry/span order and
                    // the exact interpolation used for each intersection.
                    if(!mappings)
                    {
                        mappings.emplace();
                        for(std::size_t j=0;j<output.Value.Geometry.Segments.size();++j)
                            for(const auto& source:output.Value.Geometry.Segments[j].Sources)if(source.Source==input.Binding.Source)
                                mappings->push_back({j,locate(input,source.SourceSegmentId,First(source.SourceRange)),
                                    locate(input,source.SourceSegmentId,Last(source.SourceRange)),First(source.OutputRange),Last(source.OutputRange)});
                    }
                    for(const auto& mapping:*mappings)
                    {
                        const auto j=mapping.Segment;const auto u=mapping.First,v=mapping.Last;
                        for(const auto& [begin,end]:windows)
                        {
                            const auto lo=std::max(begin,std::min(u,v)),hi=std::min(end,std::max(u,v));if(hi<=lo+1e-12)continue;
                            const auto parameter=[&](double x){return mapping.OutputFirst+(x-u)/(v-u)*(mapping.OutputLast-mapping.OutputFirst);};
                            const auto first=parameter(u<v?lo:hi),last=parameter(u<v?hi:lo);
                            if(!emitted.emplace(j,std::bit_cast<std::uint64_t>(first),std::bit_cast<std::uint64_t>(last)).second)continue;
                            auto mapped=entry;
                            const auto port=SPortRef{in.OperationId,NamedID(Key(in.OperationId)+"/port/"+output.RoleKey)};
                            mapped["range"]=ToData(SPathRange{{port,output.Value.Geometry.Segments[j].Id,first},{port,output.Value.Geometry.Segments[j].Id,last},false});entries.emplace_back(std::move(mapped));
                        }
                    }
                }
                AppendMappedChannel(values[i],channel,envelope,std::move(entries));
            }
        }
        else throw std::invalid_argument("TOOLPATH_ATTACHMENT_SCHEMA_UNSUPPORTED: "+attachment);
    }
    for(std::size_t i=0;i<candidate.Outputs.size();++i)
    {
        for(const auto& [channel,value]:values[i])
        {
            const auto& envelope=value.Is<Map>()?std::get<Map>(value.m_Value):empty;
            if(Text(envelope,"attachment","Path")=="Path")for(const auto& input:normalized.Inputs)
                if(Related(candidate.Outputs[i],input.Binding.Source))Require(input.Value.Channels.contains(channel),"TOOLPATH_MERGE_MISSING_CHANNEL_DECISION: "+channel);
        }
        auto& channels=candidate.Outputs[i].Value.Channels;
        std::erase_if(channels,[&](const auto& item){return !type.WriteChannels.contains("*")&&!type.WriteChannels.contains(item.first);});
        for(auto& [channel,value]:values[i])channels[channel]=std::move(value);
    }
}
void RebindChannelPorts(IntentMap& channels,const SPortRef& from,const SPortRef& to)
{
    const auto rewrite=[&](auto&& self,Variant& value)->void
    {
        if(value.Is<Map>())
        {auto& m=std::get<Map>(value.m_Value);if(m.contains("port")&&m.contains("segmentId")&&m.contains("parameter")&&PortFromData(m.at("port").To<Map>())==from)m["port"]=ToData(to);for(auto& [key,item]:m)self(self,item);}
        else if(value.Is<Array>())for(auto& item:std::get<Array>(value.m_Value))self(self,item);
    };
    for(auto& [key,value]:channels)rewrite(rewrite,value);
}
}
