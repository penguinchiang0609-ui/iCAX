#include "pch.h"
#include "TubeCadConstraints.h"
#include "OpenCascadeCancellation.h"
#include "GeometryData/TubeNeutralPersistence.h"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Ax3.hxx>
#include <set>
#include <functional>

namespace iCAX::CAD
{
namespace
{
namespace T=GeometryData::Tube;
using NodeMap=std::map<std::string,const T::SSolidNode*>;
gp_Trsf Frame(const GeometryData::Placement3& frame)
{
    gp_Trsf result;
    result.SetValues(frame.XDirection.X,frame.YDirection.X,frame.ZDirection.X,frame.Location.X,
        frame.XDirection.Y,frame.YDirection.Y,frame.ZDirection.Y,frame.Location.Y,
        frame.XDirection.Z,frame.YDirection.Z,frame.ZDirection.Z,frame.Location.Z);
    return result;
}
GeometryData::Placement3 Placement(const gp_Trsf& transform)
{
    GeometryData::Placement3 result;
    result.Location={transform.Value(1,4),transform.Value(2,4),transform.Value(3,4)};
    result.XDirection={transform.Value(1,1),transform.Value(2,1),transform.Value(3,1)};
    result.YDirection={transform.Value(1,2),transform.Value(2,2),transform.Value(3,2)};
    result.ZDirection={transform.Value(1,3),transform.Value(2,3),transform.Value(3,3)};
    return result;
}
std::vector<std::uint8_t> Bytes(const T::SSolidNode& node)
{
    GeometryData::Persistence::Writer writer;writer.Write(node);return std::move(writer.Bytes);
}
TopoDS_Shape Body(const T::CTubeNeutralGeometry& model,const std::string& id,
    const OpenCascade::STubeCSGEvaluationOptions& options)
{
    auto input=model;input.RootNodeID=id;
    auto settings=options;settings.EvaluateAllConstructionBodies=false;
    const auto result=OpenCascade::EvaluateTubeCSG(input,settings);
    if(!result.bOK)throw std::invalid_argument("Cannot resolve tube CAD host: "+id);
    return result.Shape;
}
std::pair<double,double> Range(const TopoDS_Shape& shape,const GeometryData::Placement3& frame)
{
    const auto local=BRepBuilderAPI_Transform(shape,Frame(frame).Inverted(),true).Shape();
    Bnd_Box box;BRepBndLib::AddOptimal(local,box,false,false);
    if(box.IsVoid())throw std::invalid_argument("Tube CAD host has no finite bounds");
    double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);return {z0,z1};
}
std::vector<std::pair<double,double>> MaterialSpans(const T::CTubeNeutralGeometry& geometry,
    const T::SSolidNode& node,const std::string& hostID,const OpenCascade::STubeCSGEvaluationOptions& options)
{
    const auto& extrusion=std::get<T::SExtrudedRegionNode>(node.Data);
    const auto host=Body(geometry,hostID,options);
    const auto range=Range(host,extrusion.Frame);
    auto probe=geometry;
    const auto it=std::find_if(probe.SolidNodes.begin(),probe.SolidNodes.end(),[&](const auto& value){return value.ID==node.ID;});
    auto& extended=std::get<T::SExtrudedRegionNode>(it->Data);
    extended.First=range.first-1;extended.Last=range.second+1;
    const auto tool=Body(probe,node.ID,options);
    BRepAlgoAPI_Common common;NCollection_List<TopoDS_Shape> arguments,tools;
    arguments.Append(host);tools.Append(tool);common.SetArguments(arguments);common.SetTools(tools);
    common.SetNonDestructive(true);common.SetFuzzyValue(options.Tolerance);
    OpenCascade::COpenCascadeCancellationScope::Build(common);
    if(!common.IsDone())throw std::invalid_argument("Cannot resolve tube CAD material spans");
    std::vector<std::pair<double,double>> spans;
    for(TopExp_Explorer solids(common.Shape(),TopAbs_SOLID);solids.More();solids.Next())
        spans.push_back(Range(solids.Current(),extrusion.Frame));
    std::sort(spans.begin(),spans.end());
    // Overlapping axial intervals are one continuous material extent even if
    // their transverse regions happen to be disconnected.
    std::vector<std::pair<double,double>> merged;
    for(const auto& span:spans) {
        if(!merged.empty()&&span.first<=merged.back().second+options.Tolerance)
            merged.back().second=std::max(merged.back().second,span.second);
        else merged.push_back(span);
    }
    return merged;
}
}

void ResolveTubeCadConstraints(const T::CTubeNeutralGeometry& original,T::CTubeNeutralGeometry& edited,
    const OpenCascade::STubeCSGEvaluationOptions& options)
{
    NodeMap oldNodes;std::map<std::string,T::SSolidNode*> nodes;
    for(const auto& node:original.SolidNodes)oldNodes.emplace(node.ID,&node);
    for(auto& node:edited.SolidNodes)nodes.emplace(node.ID,&node);
    std::set<std::string> visited,active,dirty;
    std::function<void(T::SSolidNode&)> resolve=[&](T::SSolidNode& node) {
        if(visited.contains(node.ID))return;
        if(!active.insert(node.ID).second)throw std::invalid_argument("Tube CAD host relationship cycle");
        std::set<std::string> hosts;
        std::visit([&](const auto& data) {
            using D=std::decay_t<decltype(data)>;
            if constexpr(std::is_same_v<D,T::SBooleanNode>)hosts.insert(data.Children.begin(),data.Children.end());
            else if constexpr(std::is_same_v<D,T::STransformNode>||std::is_same_v<D,T::SConnectedComponentNode>)hosts.insert(data.Child);
            else if constexpr(std::is_same_v<D,T::STopologyBevelNode>){hosts.insert(data.Child);for(const auto& selection:data.Selections)if(!selection.WrappedSourceNodeID.empty())hosts.insert(selection.WrappedSourceNodeID);}
            else if constexpr(std::is_same_v<D,T::SWrappedVolumeNode>)hosts.insert(data.Support.ExtrusionNodeID);
            else if constexpr(std::is_same_v<D,T::SOpeningProfileSweepNode>)hosts.insert(data.SourceCutNodeID);
            else if constexpr(std::is_same_v<D,T::SAlternativeNode>)for(const auto& candidate:data.Candidates)hosts.insert(candidate.CandidateNodeID);
        },node.Data);
        if(node.Relations) {
            const auto& relations=*node.Relations;
            hosts.insert(relations.DependsOnNodeIDs.begin(),relations.DependsOnNodeIDs.end());
            if(relations.Placement)hosts.insert(relations.Placement->HostNodeID);
            if(relations.Extent)hosts.insert(relations.Extent->HostNodeID);
        }
        for(const auto& host:hosts)if(!host.empty()) {
            if(!nodes.contains(host))throw std::invalid_argument("Tube CAD host is missing: "+host);
            resolve(*nodes.at(host));
        }
        const auto old=oldNodes.find(node.ID);
        const bool changed=old==oldNodes.end()||Bytes(node)!=Bytes(*old->second)
            ||std::any_of(hosts.begin(),hosts.end(),[&](const auto& id){return dirty.contains(id);});
        if(changed)dirty.insert(node.ID);
        if(node.Relations) {
            auto& relations=*node.Relations;
            if(changed&&relations.Placement&&relations.Placement->Rule==T::EHostPlacementRule::AttachedToHostBoundary
                &&!std::holds_alternative<T::SWrappedVolumeNode>(node.Data))
                throw std::invalid_argument("Tube CAD boundary attachment requires an explicit surface-supported node");
            if(changed&&relations.Placement&&relations.Placement->Rule==T::EHostPlacementRule::RelativeToHostFrame) {
                const auto& placement=*relations.Placement;
                const auto* host=std::get_if<T::SExtrudedRegionNode>(&nodes.at(placement.HostNodeID)->Data);
                if(!host)throw std::invalid_argument("Tube CAD relative placement requires an explicit host frame");
                const auto frame=Placement(Frame(host->Frame)*Frame(placement.LocalFrame));
                std::visit([&](auto& data) {
                    using D=std::decay_t<decltype(data)>;
                    if constexpr(std::is_same_v<D,T::SExtrudedRegionNode>||std::is_same_v<D,T::STaperedRegionNode>)data.Frame=frame;
                    else throw std::invalid_argument("Tube CAD node has no placement frame");
                },node.Data);
            }
            if(changed&&relations.Extent&&relations.Extent->RecomputeAgainstHost) {
                auto& extent=*relations.Extent;
                if(extent.Rule!=T::EFeatureExtentRule::FixedInterval&&extent.Rule!=T::EFeatureExtentRule::Enclosed) {
                    auto* primitive=std::get_if<T::SExtrudedRegionNode>(&node.Data);
                    const auto* oldPrimitive=old==oldNodes.end()?nullptr:std::get_if<T::SExtrudedRegionNode>(&old->second->Data);
                    // Wrapped fields already express depth in the support's
                    // normal coordinates; their support is evaluated afresh.
                    if(!std::holds_alternative<T::SWrappedVolumeNode>(node.Data)) {
                        if(!primitive)
                            throw std::invalid_argument("Tube CAD extent change requires an explicit extrusion interval");
                        const auto after=MaterialSpans(edited,node,extent.HostNodeID,options);
                        if(after.empty())throw std::invalid_argument("Tube CAD feature no longer intersects its host");
                        const double margin=std::max(1e-5,options.Tolerance*10);
                        if(extent.Rule==T::EFeatureExtentRule::ThroughAllHostMaterial) {
                            primitive->First=after.front().first-margin;primitive->Last=after.back().second+margin;
                        } else {
                            // A newly authored exact local cutter supplies its
                            // seed interval. Resolve that interval against the
                            // current host before retaining selected walls.
                            const auto before=oldPrimitive?MaterialSpans(original,*old->second,extent.HostNodeID,options):after;
                            const auto* seed=oldPrimitive?oldPrimitive:primitive;
                            if(before.size()!=after.size())throw std::invalid_argument("Tube CAD selected material boundary changed ambiguously");
                            std::vector<std::size_t> selected;
                            for(std::size_t i=0;i<before.size();++i)
                                if(std::min(before[i].second,seed->Last)-std::max(before[i].first,seed->First)>options.Tolerance)selected.push_back(i);
                            if(selected.empty())throw std::invalid_argument("Tube CAD original material boundary cannot be resolved");
                            if(!oldPrimitive&&selected.size()!=1)
                                throw std::invalid_argument("Tube CAD new selected or blind cutter requires a single observed host wall; use through-all for multiple walls");
                            if(extent.Rule==T::EFeatureExtentRule::ThroughSelectedHostBoundaries) {
                                primitive->First=after[selected.front()].first-margin;
                                primitive->Last=after[selected.back()].second+margin;
                            } else if(extent.Rule==T::EFeatureExtentRule::BlindFromHostBoundary) {
                                if(selected.size()!=1||extent.Direction==T::EExtentDirection::Both)
                                    throw std::invalid_argument("Tube CAD blind feature opening is ambiguous");
                                const auto index=selected.front();
                                if(extent.Direction==T::EExtentDirection::Positive) {
                                    const double depth=extent.Depth?extent.Depth->Value:seed->Last-before[index].first;
                                    if(!std::isfinite(depth)||depth<=options.Tolerance)throw std::invalid_argument("Tube CAD blind depth must be positive");
                                    primitive->First=after[index].first-margin;primitive->Last=after[index].first+depth;
                                    if(primitive->Last>=after[index].second-options.Tolerance)throw std::invalid_argument("Tube CAD blind depth would become through");
                                } else {
                                    const double depth=extent.Depth?extent.Depth->Value:before[index].second-seed->First;
                                    if(!std::isfinite(depth)||depth<=options.Tolerance)throw std::invalid_argument("Tube CAD blind depth must be positive");
                                    primitive->First=after[index].second-depth;primitive->Last=after[index].second+margin;
                                    if(primitive->First<=after[index].first+options.Tolerance)throw std::invalid_argument("Tube CAD blind depth would become through");
                                }
                            }
                        }
                        extent.PrimitiveFirst.Value=primitive->First;extent.PrimitiveLast.Value=primitive->Last;
                    }
                }
            }
        }
        active.erase(node.ID);visited.insert(node.ID);
    };
    for(auto& node:edited.SolidNodes)resolve(node);
}
}
