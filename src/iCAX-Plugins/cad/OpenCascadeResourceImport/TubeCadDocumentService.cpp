#include "pch.h"
#include "TubeCadDocumentService.h"
#include "TubeBoundaryGeometry.h"
#include "TubeCadConstraints.h"
#include "OpenCascadeTubeCSGConverter.h"
#include "OpenCascadeBRepBuilder.h"
#include "OpenCascadeBRepReader.h"
#include "GeometryData/TubeNeutralPersistence.h"
#include "Services/ServicesHelper.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <Standard_Failure.hxx>
#include <set>
#include <functional>
#include <limits>
#include <chrono>
namespace iCAX::CAD
{
namespace
{
namespace Tube=GeometryData::Tube;
using NodeMap=std::map<std::string,const Tube::SSolidNode*>;

std::optional<GeometryData::Circle2> CircularLoop(const Tube::SRegionLoop2& loop)
{
    if(loop.Curves.empty())return std::nullopt;
    const auto* first=std::get_if<GeometryData::Circle2>(&loop.Curves.front().Segment.Curve);
    if(!first)return std::nullopt;
    constexpr double tau=6.28318530717958647692;
    const double tolerance=1e-7*std::max(1.0,first->Radius);
    const auto point=[](const GeometryData::Circle2& circle,double angle) {
        return GeometryData::Point2{circle.Placement.Location.X+circle.Radius*(std::cos(angle)*circle.Placement.XDirection.X
            +std::sin(angle)*circle.Placement.YDirection.X),
            circle.Placement.Location.Y+circle.Radius*(std::cos(angle)*circle.Placement.XDirection.Y
            +std::sin(angle)*circle.Placement.YDirection.Y)};
    };
    double sweep=0;std::optional<GeometryData::Point2> initial,previous;int winding=0;
    for(const auto& curve:loop.Curves) {
        const auto* circle=std::get_if<GeometryData::Circle2>(&curve.Segment.Curve);
        if(!circle||std::abs(circle->Radius-first->Radius)>tolerance
            ||std::hypot(circle->Placement.Location.X-first->Placement.Location.X,
                circle->Placement.Location.Y-first->Placement.Location.Y)>tolerance)return std::nullopt;
        const auto& range=curve.Segment.Range;
        const auto start=point(*circle,range.Reversed?range.Last:range.First);
        const auto end=point(*circle,range.Reversed?range.First:range.Last);
        if(previous&&std::hypot(previous->X-start.X,previous->Y-start.Y)>tolerance)return std::nullopt;
        if(!initial)initial=start;previous=end;
        const double determinant=circle->Placement.XDirection.X*circle->Placement.YDirection.Y
            -circle->Placement.XDirection.Y*circle->Placement.YDirection.X;
        const double angular=(range.Last-range.First)*(range.Reversed?-1:1)*determinant;
        const int direction=angular>0?1:-1;
        if(std::abs(std::abs(determinant)-1)>1e-7||std::abs(angular)<1e-10
            ||(winding&&winding!=direction))return std::nullopt;
        winding=direction;sweep+=angular;
    }
    if(!initial||std::hypot(previous->X-initial->X,previous->Y-initial->Y)>tolerance
        ||std::abs(std::abs(sweep)-tau)>1e-7)return std::nullopt;
    return *first;
}
void SynchronizeSectionDescriptions(Tube::CTubeNeutralGeometry& geometry)
{
    for(auto& node:geometry.SolidNodes)std::visit([&](auto& data) {
        using T=std::decay_t<decltype(data)>;
        if constexpr(std::is_same_v<T,Tube::SExtrudedRegionNode>||std::is_same_v<T,Tube::STaperedRegionNode>) {
            std::erase_if(data.SectionPrimitives,[&](const auto& primitive) {
                return std::none_of(data.Section.Boundaries.begin(),data.Section.Boundaries.end(),[&](const auto& loop){return loop.ID==primitive.LoopID;});
            });
            for(std::size_t index=0;index<data.Section.Boundaries.size();++index) {
                const auto& loop=data.Section.Boundaries[index];
                auto primitive=std::find_if(data.SectionPrimitives.begin(),data.SectionPrimitives.end(),[&](const auto& value){return value.LoopID==loop.ID;});
                if(primitive==data.SectionPrimitives.end()) {
                    Tube::SSectionPrimitive value;value.ID=node.ID+"/section/"+loop.ID;value.LoopID=loop.ID;
                    value.Role=index==0?Tube::ESectionPrimitiveRole::OuterBoundary:Tube::ESectionPrimitiveRole::Cavity;
                    data.SectionPrimitives.push_back(value);primitive=std::prev(data.SectionPrimitives.end());
                }
                const auto set=[](auto& parameter,double value) {
                    if(!parameter)parameter.emplace();parameter->Value=value;
                    parameter->Observability=Tube::EParameterObservability::Observed;
                };
                primitive->Kind=Tube::ESectionPrimitiveKind::CurveLoop;
                if(const auto circle=CircularLoop(loop)) {
                    primitive->Kind=Tube::ESectionPrimitiveKind::Circle;primitive->Center=circle->Placement.Location;
                    primitive->RotationRadians=std::atan2(circle->Placement.XDirection.Y,circle->Placement.XDirection.X);
                    set(primitive->Radius,circle->Radius);primitive->Width.reset();primitive->Height.reset();continue;
                }
                if(loop.Curves.size()==4) {
                    std::array<GeometryData::Point2,4> starts,ends;bool rectangle=true;
                    for(std::size_t i=0;i<4;++i) {
                        const auto* line=std::get_if<GeometryData::Segment2>(&loop.Curves[i].Segment.Curve);
                        if(!line){rectangle=false;break;}
                        starts[i]=line->Start;ends[i]=line->End;
                        if(loop.Curves[i].Segment.Range.Reversed)std::swap(starts[i],ends[i]);
                    }
                    for(std::size_t i=0;rectangle&&i<4;++i) {
                        const auto j=(i+1)%4;
                        const double dx=ends[i].X-starts[i].X,dy=ends[i].Y-starts[i].Y;
                        const double nx=ends[j].X-starts[j].X,ny=ends[j].Y-starts[j].Y;
                        rectangle=std::hypot(ends[i].X-starts[j].X,ends[i].Y-starts[j].Y)<1e-7
                            &&std::hypot(dx,dy)>1e-7&&std::abs(dx*nx+dy*ny)<1e-7*std::hypot(dx,dy)*std::hypot(nx,ny);
                    }
                    if(rectangle) {
                        primitive->Kind=Tube::ESectionPrimitiveKind::Rectangle;
                        primitive->Center={(starts[0].X+starts[2].X)/2,(starts[0].Y+starts[2].Y)/2};
                        primitive->RotationRadians=std::atan2(ends[0].Y-starts[0].Y,ends[0].X-starts[0].X);
                        set(primitive->Width,std::hypot(ends[0].X-starts[0].X,ends[0].Y-starts[0].Y));
                        set(primitive->Height,std::hypot(ends[1].X-starts[1].X,ends[1].Y-starts[1].Y));
                        primitive->Radius.reset();continue;
                    }
                }
                primitive->Width.reset();primitive->Height.reset();primitive->Radius.reset();
            }
        }
    },node.Data);
}
NodeMap Nodes(const Tube::CTubeNeutralGeometry& geometry)
{
    NodeMap result;
    for(const auto& node:geometry.SolidNodes)
        if(node.ID.empty()||!result.emplace(node.ID,&node).second)throw std::invalid_argument("Tube CAD feature IDs must be unique and nonempty");
    if(!result.contains(geometry.BaseNodeID)||!result.contains(geometry.RootNodeID))throw std::invalid_argument("Tube CAD base or root node is missing");
    return result;
}
void ValidateConstruction(const Tube::CTubeCadDocument& document)
{
    if(document.Unit!="mm")throw std::invalid_argument("Tube CAD coordinates must use millimetres");
    const auto nodes=Nodes(document.Construction);
    std::map<std::string,std::vector<std::string>> dependencies;
    const auto support=[&](const Tube::SBoundarySurfaceRef& ref) {
        const auto it=nodes.find(ref.ExtrusionNodeID);
        if(it==nodes.end())throw std::invalid_argument("Tube CAD support node is missing");
        const auto* extrusion=std::get_if<Tube::SExtrudedRegionNode>(&it->second->Data);
        if(!extrusion)throw std::invalid_argument("Tube CAD side atlas requires an extrusion support");
        const auto loop=std::find_if(extrusion->Section.Boundaries.begin(),extrusion->Section.Boundaries.end(),[&](const auto& item){return item.ID==ref.LoopID;});
        if(loop==extrusion->Section.Boundaries.end())throw std::invalid_argument("Tube CAD support loop is missing");
        if(!ref.CurveID.empty()&&std::none_of(loop->Curves.begin(),loop->Curves.end(),[&](const auto& item){return item.ID==ref.CurveID;}))
            throw std::invalid_argument("Tube CAD support curve is missing");
    };
    for(const auto& [id,node]:nodes) {
        auto& refs=dependencies[id];
        if(node->Relations) {
            refs=node->Relations->DependsOnNodeIDs;
            if(node->Relations->Extent&&!node->Relations->Extent->HostNodeID.empty())refs.push_back(node->Relations->Extent->HostNodeID);
            if(node->Relations->Placement) {
                const auto& placement=*node->Relations->Placement;
                if(!placement.HostNodeID.empty())refs.push_back(placement.HostNodeID);
                if(placement.Support)support(*placement.Support);
            }
        }
        std::visit([&](const auto& data) {
            using T=std::decay_t<decltype(data)>;
            if constexpr(std::is_same_v<T,Tube::SBooleanNode>)refs.insert(refs.end(),data.Children.begin(),data.Children.end());
            else if constexpr(std::is_same_v<T,Tube::STransformNode>||std::is_same_v<T,Tube::SConnectedComponentNode>) {
                refs.push_back(data.Child);
                if constexpr(std::is_same_v<T,Tube::SConnectedComponentNode>)
                    if(!std::isfinite(data.Anchor.X)||!std::isfinite(data.Anchor.Y)||!std::isfinite(data.Anchor.Z))
                        throw std::invalid_argument("Tube CAD component anchor must be finite");
            }
            else if constexpr(std::is_same_v<T,Tube::STopologyBevelNode>) {refs.push_back(data.Child);
                for(const auto& selection:data.Selections)if(!selection.WrappedSourceNodeID.empty())refs.push_back(selection.WrappedSourceNodeID);}
            else if constexpr(std::is_same_v<T,Tube::SWrappedVolumeNode>) {support(data.Support);refs.push_back(data.Support.ExtrusionNodeID);}
            else if constexpr(std::is_same_v<T,Tube::SOpeningProfileSweepNode>) {
                refs.push_back(data.SourceCutNodeID);
                if(!data.EvaluatedVolumeNodeID.empty())refs.push_back(data.EvaluatedVolumeNodeID);
                if(data.TargetOpening)refs.push_back(data.TargetOpening->SourceFeatureNodeID);
                if(data.ProfileField.Keys.empty())throw std::invalid_argument("Tube CAD opening profile is empty");
            }
            else if constexpr(std::is_same_v<T,Tube::SCompositeVolumeNode>||std::is_same_v<T,Tube::SResidualBRepNode>) {
                if(!document.Bodies.contains(data.BRepResourceID))throw std::invalid_argument("Tube CAD exact body is missing: "+data.BRepResourceID);
            }
            else if constexpr(std::is_same_v<T,Tube::SAlternativeNode>) {
                std::set<std::string> ids;
                for(const auto& candidate:data.Candidates) {
                    if(candidate.ID.empty()||!ids.insert(candidate.ID).second)throw std::invalid_argument("Tube CAD interpretation IDs are invalid");
                    refs.push_back(candidate.CandidateNodeID);
                }
            }
        },node->Data);
        for(const auto& ref:refs)if(!nodes.contains(ref))throw std::invalid_argument("Tube CAD dependency is missing: "+ref);
    }
    std::set<std::string> visited,active;
    std::function<void(const std::string&)> visit=[&](const auto& id) {
        if(visited.contains(id))return;
        if(!active.insert(id).second)throw std::invalid_argument("Tube CAD feature dependency cycle: "+id);
        for(const auto& dependency:dependencies.at(id))visit(dependency);
        active.erase(id);visited.insert(id);
    };
    for(const auto& [id,node]:nodes)visit(id);
    std::set<std::string> uvIDs;
    for(const auto& feature:document.Construction.UVFeatures) {
        if(feature.ID.empty()||!uvIDs.insert(feature.ID).second)throw std::invalid_argument("Tube CAD surface feature IDs are invalid");
        std::visit([&](const auto& data){support(data.Support);},feature.Data);
    }
}
STubeCadSketchView SketchView(const Tube::CTubeCadDocument& document,
    const NodeMap& nodes, const std::string& id)
{
    const auto found=nodes.find(id);
    if(found==nodes.end())throw std::invalid_argument("Tube CAD sketch node is missing: "+id);
    STubeCadSketchView view;view.NodeID=id;view.Revision=document.Revision;
    std::visit([&](const auto& data) {
        using T=std::decay_t<decltype(data)>;
        if constexpr(std::is_same_v<T,Tube::SExtrudedRegionNode>||std::is_same_v<T,Tube::STaperedRegionNode>) {
            view.Geometry=data.Section;view.ConstructionFrame=data.Frame;
        } else if constexpr(std::is_same_v<T,Tube::SWrappedVolumeNode>) {
            view.Geometry=data.UVRegion;view.SurfaceSupport=data.Support;
        } else if constexpr(std::is_same_v<T,Tube::SOpeningProfileSweepNode>)view.Geometry=data.ProfileField;
        else throw std::invalid_argument("Tube CAD node has no planar construction geometry: "+id);
    },found->second->Data);
    return view;
}
OpenCascade::STubeCSGEvaluationOptions Options(const Tube::CTubeCadDocument& document)
{
    OpenCascade::STubeCSGEvaluationOptions options;
    // These exact operands belong only to optional editing constructions.
    // Core-shape evaluation never consults this map or the authoring graph.
    std::set<std::string> referenced;
    for(const auto& node:document.Construction.SolidNodes)std::visit([&](const auto& data) {
        using T=std::decay_t<decltype(data)>;
        if constexpr(std::is_same_v<T,Tube::SCompositeVolumeNode>||std::is_same_v<T,Tube::SResidualBRepNode>)
            referenced.insert(data.BRepResourceID);
    },node.Data);
    for(const auto& id:referenced) {
        const auto shape=OpenCascade::BuildOpenCascadeShape(document.Bodies.at(id));
        if(!shape.bOK||shape.Shape.IsNull())throw std::invalid_argument("Tube CAD exact body cannot be rebuilt: "+id);
        options.ExternalBRepShapes.emplace(id,shape.Shape);
    }
    return options;
}
gp_Trsf DocumentPlacement(const Tube::CTubeCadDocument& document)
{
    if(document.Unit!="mm")throw std::invalid_argument("Tube CAD coordinates must use millimetres");
    const auto& frame=document.Placement;
    const auto vector=[](const auto& value){return gp_Vec(value.X,value.Y,value.Z);};
    const auto x=vector(frame.XDirection),y=vector(frame.YDirection),z=vector(frame.ZDirection);
    const auto finite=[](const auto& value){return std::isfinite(value.X)&&std::isfinite(value.Y)&&std::isfinite(value.Z);};
    if(!finite(frame.Location)||!finite(frame.XDirection)||!finite(frame.YDirection)||!finite(frame.ZDirection)
        ||std::abs(x.SquareMagnitude()-1)>1e-7||std::abs(y.SquareMagnitude()-1)>1e-7||std::abs(z.SquareMagnitude()-1)>1e-7
        ||std::abs(x.Dot(y))>1e-7||std::abs(y.Dot(z))>1e-7||std::abs(z.Dot(x))>1e-7||x.Crossed(y).Dot(z)<1-1e-7)
        throw std::invalid_argument("Tube CAD placement must be a finite right-handed rigid frame");
    gp_Trsf transform;
    transform.SetValues(frame.XDirection.X,frame.YDirection.X,frame.ZDirection.X,frame.Location.X,
        frame.XDirection.Y,frame.YDirection.Y,frame.ZDirection.Y,frame.Location.Y,
        frame.XDirection.Z,frame.YDirection.Z,frame.ZDirection.Z,frame.Location.Z);
    return transform;
}
STubeCadEvaluation EvaluateCoreNative(const Tube::CTubeCadDocument& document,TopoDS_Shape* nativeShape=nullptr)
{
    const auto placement=DocumentPlacement(document);
    auto shape=OpenCascade::RestoreTubeBoundary(document.Geometry);
    if(placement.Form()!=gp_Identity)shape=BRepBuilderAPI_Transform(shape,placement,true).Shape();
    if(shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())
        throw std::invalid_argument("Tube CAD section and machining boundary do not restore a valid placed body");
    STubeCadEvaluation result;
    result.Shape=OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"","",0.025);
    if(nativeShape)*nativeShape=shape;
    return result;
}
STubeCadSketchView CoreSectionView(const Tube::CTubeCadDocument& document)
{
    DocumentPlacement(document);
    if(document.Geometry.Section.Boundaries.empty())throw std::invalid_argument("Tube CAD section is empty");
    STubeCadSketchView result;result.NodeID="section";result.Revision=document.Revision;result.Geometry=document.Geometry.Section;
    const auto& frame=document.Placement;
    result.ConstructionFrame=GeometryData::Placement3{frame.Location,frame.YDirection,frame.ZDirection,frame.XDirection};
    return result;
}
STubeCadEvaluation EvaluateConstructionNative(const Tube::CTubeCadDocument& document,TopoDS_Shape* nativeShape=nullptr)
{
    ValidateConstruction(document);
    const auto evaluated=OpenCascade::EvaluateTubeCSG(document.Construction,Options(document));
    if(!evaluated.bOK||evaluated.Shape.IsNull()||!BRepCheck_Analyzer(evaluated.Shape).IsValid()) {
        std::string message="Tube CAD construction preview failed";
        for(const auto& diagnostic:evaluated.Diagnostics)message+="; "+diagnostic;
        throw std::invalid_argument(message);
    }
    STubeCadEvaluation result;result.Diagnostics=evaluated.Diagnostics;
    for(const auto& node:document.Construction.SolidNodes)
        if(!evaluated.NodeShapes.contains(node.ID))result.UnavailableConstructionIDs.push_back(node.ID);
    result.Shape=OpenCascade::ConvertOpenCascadeShapeToBRep(evaluated.Shape,"","",0.025);
    for(const auto& [id,shape]:evaluated.NodeShapes)
        result.ConstructionBodies.emplace(id,shape.IsSame(evaluated.Shape)?result.Shape
            :OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"","",0.025));
    if(nativeShape)*nativeShape=evaluated.Shape;
    return result;
}
const Tube::SExtrudedRegionNode& NominalStock(const Tube::CTubeNeutralGeometry& construction)
{
    const auto nodes=Nodes(construction);
    const auto* stock=std::get_if<Tube::SExtrudedRegionNode>(&nodes.at(construction.BaseNodeID)->Data);
    if(!stock)throw std::invalid_argument("Tube CAD boundary representation requires an exact constant-section extrusion stock");
    return *stock;
}
void CompileBoundary(Tube::CTubeCadDocument& document,const TopoDS_Shape& source,std::vector<std::string>& diagnostics)
{
    const auto compiled=OpenCascade::CompileTubeBoundary(source,NominalStock(document.Construction));
    document.Geometry=compiled.Geometry;document.Placement=compiled.LocalToDocument;
    diagnostics.insert(diagnostics.end(),compiled.Diagnostics.begin(),compiled.Diagnostics.end());
}
// The shape core is evaluated independently of authoring construction previews.
class CTubeCadDocumentService final:public ITubeCadDocumentService
{
    AUTO_REGIST_SERVICE(ITubeCadDocumentService,CTubeCadDocumentService)
public:
    void OnLoad() override {}
    void OnUnload() override {}
    STubeCadEditResult CreateGeometry(const Tube::CTubeBoundaryGeometry& geometry,
        const GeometryData::Placement3& placement) const override
    {
        STubeCadEditResult result;result.Document.Geometry=geometry;
        result.Document.Placement=placement;result.Document.Revision=1;
        result.Evaluation=EvaluateCoreNative(result.Document);
        return result;
    }
    GeometryData::Point3 FindInteriorAnchor(const GeometryData::BRepModel& body) const override
    {
        const auto built=OpenCascade::BuildOpenCascadeShape(body);
        if(!built.bOK||built.Shape.IsNull())throw std::invalid_argument("Cannot rebuild component anchor solid");
        TopoDS_Solid solid;std::size_t count=0;
        for(TopExp_Explorer it(built.Shape,TopAbs_SOLID);it.More();it.Next()){solid=TopoDS::Solid(it.Current());++count;}
        if(count!=1)throw std::invalid_argument("Component anchor requires exactly one solid");
        constexpr double tolerance=1e-7;
        Bnd_Box bounds;BRepBndLib::AddOptimal(solid,bounds,false,false);
        double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
        const double scale=std::max({x1-x0,y1-y0,z1-z0});
        BRepClass3d_SolidClassifier classifier(solid);
        // UV candidates only propose points. A strict solid classification is
        // the sole acceptance criterion; neither bounds nor face order select
        // a component or stand in for a material witness.
        for(TopExp_Explorer it(solid,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(face);
            double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
            if(!std::isfinite(u0)||!std::isfinite(u1)||!std::isfinite(v0)||!std::isfinite(v1))continue;
            for(int ui=1;ui<=5;++ui)for(int vi=1;vi<=5;++vi) {
                const double u=u0+(u1-u0)*ui/6,v=v0+(v1-v0)*vi/6;
                if(BRepClass_FaceClassifier(face,gp_Pnt2d(u,v),tolerance).State()!=TopAbs_IN)continue;
                gp_Pnt point;gp_Vec du,dv;surface.D1(u,v,point,du,dv);auto normal=du.Crossed(dv);
                if(normal.SquareMagnitude()<1e-20)continue;normal.Normalize();
                for(double step=scale*.01;step>tolerance*16;step*=.1)for(double sign:{-1.,1.}) {
                    const auto candidate=point.Translated(normal*(sign*step));classifier.Perform(candidate,tolerance);
                    if(classifier.State()==TopAbs_IN)return {candidate.X(),candidate.Y(),candidate.Z()};
                }
            }
        }
        throw std::invalid_argument("Cannot certify a strict interior component anchor");
    }
    STubeCadSketchView ReadSketch(const Tube::CTubeCadDocument& document,const std::string& id) const override
    {
        if(id=="section")return CoreSectionView(document);
        ValidateConstruction(document);
        return SketchView(document,Nodes(document.Construction),id);
    }
    std::vector<STubeCadSketchView> ReadSketches(const Tube::CTubeCadDocument& document,
        const std::vector<std::string>& ids) const override
    {
        std::vector<STubeCadSketchView> views;views.reserve(ids.size());
        for(const auto& id:ids)views.push_back(ReadSketch(document,id));
        return views;
    }
    STubeCadEditResult EditSketch(const Tube::CTubeCadDocument& original,const STubeCadSketchView& sketch) const override
    {
        const auto current=ReadSketch(original,sketch.NodeID);
        if(current.Geometry.index()!=sketch.Geometry.index())throw std::invalid_argument("Tube CAD sketch geometry kind changed");
        auto geometry=original.Construction;
        const auto target=sketch.NodeID=="section"?geometry.BaseNodeID:sketch.NodeID;
        const auto found=std::find_if(geometry.SolidNodes.begin(),geometry.SolidNodes.end(),[&](const auto& node){return node.ID==target;});
        if(found==geometry.SolidNodes.end())throw std::invalid_argument("Tube CAD section edit requires an explicit construction rule; replace the complete boundary geometry for a direct edit");
        std::visit([&](auto& data) {
            using T=std::decay_t<decltype(data)>;
            if constexpr(std::is_same_v<T,Tube::SExtrudedRegionNode>||std::is_same_v<T,Tube::STaperedRegionNode>) {
                data.Section=std::get<Tube::CPlanarRegion2>(sketch.Geometry);
                // A spatial move is a geometric edit of Frame, independent of
                // a planar sketch edit. Do not reinterpret display coordinates.
            } else if constexpr(std::is_same_v<T,Tube::SWrappedVolumeNode>)data.UVRegion=std::get<Tube::CPlanarRegion2>(sketch.Geometry);
            else if constexpr(std::is_same_v<T,Tube::SOpeningProfileSweepNode>)data.ProfileField=std::get<Tube::SOpeningProfileField>(sketch.Geometry);
        },found->Data);
        return Edit(original,sketch.Revision,geometry);
    }
    STubeCadEditResult SetSectionDimension(const Tube::CTubeCadDocument& original,std::uint64_t revision,
        const std::string& id,const std::string& loopID,ETubeSectionDimension dimension,double value) const override
    {
        if(!std::isfinite(value)||value<=1e-7)throw std::invalid_argument("Tube CAD section dimension must be positive");
        auto geometry=original.Construction;SynchronizeSectionDescriptions(geometry);
        const auto target=id=="section"?geometry.BaseNodeID:id;
        const auto found=std::find_if(geometry.SolidNodes.begin(),geometry.SolidNodes.end(),[&](const auto& node){return node.ID==target;});
        if(found==geometry.SolidNodes.end())throw std::invalid_argument("Tube CAD section node is missing");
        std::visit([&](auto& data) {
            using T=std::decay_t<decltype(data)>;
            if constexpr(std::is_same_v<T,Tube::SExtrudedRegionNode>||std::is_same_v<T,Tube::STaperedRegionNode>) {
                auto loop=std::find_if(data.Section.Boundaries.begin(),data.Section.Boundaries.end(),[&](const auto& item){return item.ID==loopID;});
                const auto primitive=std::find_if(data.SectionPrimitives.begin(),data.SectionPrimitives.end(),[&](const auto& item){return item.LoopID==loopID;});
                if(loop==data.Section.Boundaries.end()||primitive==data.SectionPrimitives.end())throw std::invalid_argument("Tube CAD section boundary is missing");
                if(primitive->Kind==Tube::ESectionPrimitiveKind::Circle&&dimension==ETubeSectionDimension::Radius) {
                    for(auto& curve:loop->Curves)std::get<GeometryData::Circle2>(curve.Segment.Curve).Radius=value;
                } else if(primitive->Kind==Tube::ESectionPrimitiveKind::Rectangle
                    &&(dimension==ETubeSectionDimension::Width||dimension==ETubeSectionDimension::Height)) {
                    const double c=std::cos(primitive->RotationRadians),s=std::sin(primitive->RotationRadians);
                    const double sx=dimension==ETubeSectionDimension::Width?value/primitive->Width->Value:1;
                    const double sy=dimension==ETubeSectionDimension::Height?value/primitive->Height->Value:1;
                    const auto scale=[&](GeometryData::Point2& point) {
                        const double dx=point.X-primitive->Center.X,dy=point.Y-primitive->Center.Y;
                        const double x=(c*dx+s*dy)*sx,y=(-s*dx+c*dy)*sy;
                        point={primitive->Center.X+c*x-s*y,primitive->Center.Y+s*x+c*y};
                    };
                    for(auto& curve:loop->Curves) {
                        auto& segment=std::get<GeometryData::Segment2>(curve.Segment.Curve);
                        scale(segment.Start);scale(segment.End);
                    }
                } else throw std::invalid_argument("Tube CAD dimension is not defined by this exact curve loop");
            } else throw std::invalid_argument("Tube CAD node has no editable section dimensions");
        },found->Data);
        return Edit(original,revision,geometry);
    }
    STubeCadEvaluation Evaluate(const Tube::CTubeCadDocument& document) const override
    {
        return EvaluateCoreNative(document);
    }
    STubeCadEvaluation EvaluateConstruction(const Tube::CTubeCadDocument& document) const override
    {
        return EvaluateConstructionNative(document);
    }
    STubeCadEditResult EditGeometry(const Tube::CTubeCadDocument& original,std::uint64_t expected,
        const Tube::CTubeBoundaryGeometry& geometry) const override
    {
        if(original.Revision!=expected||expected==std::numeric_limits<std::uint64_t>::max())
            throw std::invalid_argument("Tube CAD revision conflict");
        STubeCadEditResult result;result.Document=original;
        result.Document.Geometry=geometry;result.Document.Revision=expected+1;
        result.Document.Construction={};result.Document.Bodies.clear();
        result.Evaluation=EvaluateCoreNative(result.Document);
        return result;
    }
    Tube::CPlanarRegion2 ReadPlanarRegion(const GeometryData::BRepModel& faces,
        const GeometryData::Placement3& frame) const override
    {
        const auto built=OpenCascade::BuildOpenCascadeShape(faces);
        if(!built.bOK||built.Shape.IsNull())throw std::invalid_argument("CAD planar faces are invalid");
        return OpenCascade::ExtractPlanarTubeRegion(built.Shape,frame);
    }
    std::vector<GeometryData::CurveSegment2> ReadPlanarCurves(const GeometryData::BRepModel& edges,
        const GeometryData::Placement3& frame) const override {
        const auto built=OpenCascade::BuildOpenCascadeShape(edges);
        if(!built.bOK||built.Shape.IsNull())throw std::invalid_argument("CAD planar boundary is invalid");
        return OpenCascade::ExtractPlanarTubeCurves(built.Shape,frame);
    }
private:
    STubeCadEditResult CreateWithNative(const Tube::CTubeNeutralGeometry& construction,
        const std::map<std::string,GeometryData::BRepModel>& bodies,TopoDS_Shape* nativeShape) const
    {
        STubeCadEditResult result;result.Document.Construction=construction;result.Document.Bodies=bodies;
        result.Document.Revision=1;result.Document.Construction.Version=1;
        SynchronizeSectionDescriptions(result.Document.Construction);
        TopoDS_Shape constructed;
        auto preview=EvaluateConstructionNative(result.Document,&constructed);
        std::vector<std::string> diagnostics;
        CompileBoundary(result.Document,constructed,diagnostics);
        result.Evaluation=EvaluateCoreNative(result.Document,nativeShape);
        result.Evaluation.Diagnostics=std::move(diagnostics);
        result.Evaluation.ConstructionBodies=std::move(preview.ConstructionBodies);
        result.Evaluation.UnavailableConstructionIDs=std::move(preview.UnavailableConstructionIDs);
        return result;
    }
public:
    STubeCadEditResult Create(const Tube::CTubeNeutralGeometry& geometry,
        const std::map<std::string,GeometryData::BRepModel>& bodies) const override
    {
        return CreateWithNative(geometry,bodies,nullptr);
    }
    STubeCadEditResult Import(const GeometryData::BRepModel& source,
        const std::function<void(const std::string&, double)>& stageCompleted) const override
    {
        auto previous=std::chrono::steady_clock::now();
        const auto mark=[&](const char* stage) {
            const auto now=std::chrono::steady_clock::now();
            if(stageCompleted)stageCompleted(stage,std::chrono::duration<double,std::milli>(now-previous).count());
            previous=now;
        };
        const auto built=OpenCascade::BuildOpenCascadeShape(source);
        if(!built.bOK||built.Shape.IsNull())throw std::invalid_argument("Tube CAD source is invalid");
        mark("rebuild");
        OpenCascade::STubeCSGConversionOptions options;
        options.SourceBRepResourceID="source";options.RemovalBRepResourceID="removal";options.AdditionBRepResourceID="addition";
        options.StageCompleted=stageCompleted;
        const auto converted=OpenCascade::ConvertBRepToTubeCSG(built.Shape,options);
        mark("constructionRecognition");
        STubeCadEditResult result;result.Document.Revision=1;
        result.Document.Construction=converted.Geometry;result.Document.Construction.Version=1;
        // The nominal stock identifies the section. Compile the actual source,
        // rather than an optional reconstructed interpretation of that source.
        std::vector<std::string> diagnostics;
        CompileBoundary(result.Document,built.Shape,diagnostics);
        mark("compileBoundary");
        TopoDS_Shape restored;
        result.Evaluation=EvaluateCoreNative(result.Document,&restored);
        const auto replay=OpenCascade::ValidateTubeCSGShapeReplay(built.Shape,restored,1e-6);
        if(!replay.bEquivalent) {
            std::string reason="Tube CAD section and machining boundary change imported geometry";
            for(const auto& diagnostic:replay.Diagnostics)reason+="; "+diagnostic;
            throw std::invalid_argument(reason);
        }
        mark("persistedBoundaryReplay");
        result.Evaluation.Diagnostics=std::move(diagnostics);
        if(converted.ReplayValidation.bEquivalent) {
            try {
                std::set<std::string> referenced;
                for(const auto& node:result.Document.Construction.SolidNodes)std::visit([&](const auto& data) {
                    using T=std::decay_t<decltype(data)>;
                    if constexpr(std::is_same_v<T,Tube::SCompositeVolumeNode>||std::is_same_v<T,Tube::SResidualBRepNode>)
                        referenced.insert(data.BRepResourceID);
                },node.Data);
                for(const auto& id:referenced) {
                    if(id=="source")result.Document.Bodies.emplace(id,source);
                    else if(id=="removal"&&!converted.RemovalShape.IsNull())result.Document.Bodies.emplace(id,
                        OpenCascade::ConvertOpenCascadeShapeToBRep(converted.RemovalShape,"","",0.025));
                    else if(id=="addition"&&!converted.AdditionShape.IsNull())result.Document.Bodies.emplace(id,
                        OpenCascade::ConvertOpenCascadeShapeToBRep(converted.AdditionShape,"","",0.025));
                    else throw std::invalid_argument("Recognized construction refers to an unavailable exact operand");
                }
                SynchronizeSectionDescriptions(result.Document.Construction);
                ValidateConstruction(result.Document);
                for(const auto& [id,shape]:converted.PreviewShapes)
                    if(!shape.IsNull())result.Evaluation.ConstructionBodies.emplace(id,
                        OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"","",0.025));
            } catch(const Standard_Failure& error) {
                result.Document.Construction={};result.Document.Bodies.clear();result.Evaluation.ConstructionBodies.clear();
                result.Evaluation.Diagnostics.push_back(std::string("Optional construction definitions unavailable: ")+error.GetMessageString());
            } catch(const std::exception& error) {
                result.Document.Construction={};result.Document.Bodies.clear();result.Evaluation.ConstructionBodies.clear();
                result.Evaluation.Diagnostics.push_back(std::string("Optional construction definitions unavailable: ")+error.what());
            }
        } else {
            result.Document.Construction={};
            result.Evaluation.Diagnostics.push_back("Optional construction definitions failed independent replay; boundary geometry remains authoritative");
        }
        mark("constructionDefinitions");
        return result;
    }
    STubeCadEditResult Edit(const Tube::CTubeCadDocument& original,std::uint64_t expected,
        const Tube::CTubeNeutralGeometry& geometry) const override
    {
        return EditWithBodies(original,expected,geometry,{});
    }
    STubeCadEditResult EditWithBodies(const Tube::CTubeCadDocument& original,std::uint64_t expected,
        const Tube::CTubeNeutralGeometry& geometry,
        const std::map<std::string,GeometryData::BRepModel>& additional) const override
    {
        if(original.Revision!=expected||expected==std::numeric_limits<std::uint64_t>::max())
            throw std::invalid_argument("Tube CAD revision conflict");
        STubeCadEditResult result;result.Document=original;result.Document.Construction=geometry;
        for(const auto& [id,body]:additional)
            if(!result.Document.Bodies.emplace(id,body).second)
                throw std::invalid_argument("Tube CAD additional body ID already exists: "+id);
        result.Document.Revision=expected+1;result.Document.Construction.Version=result.Document.Revision;
        ValidateConstruction(result.Document);
        ResolveTubeCadConstraints(original.Construction,result.Document.Construction,Options(result.Document));
        SynchronizeSectionDescriptions(result.Document.Construction);
        TopoDS_Shape constructed;
        auto preview=EvaluateConstructionNative(result.Document,&constructed);
        std::vector<std::string> diagnostics;
        CompileBoundary(result.Document,constructed,diagnostics);
        result.Evaluation=EvaluateCoreNative(result.Document);
        result.Evaluation.Diagnostics=std::move(diagnostics);
        result.Evaluation.ConstructionBodies=std::move(preview.ConstructionBodies);
        result.Evaluation.UnavailableConstructionIDs=std::move(preview.UnavailableConstructionIDs);
        return result;
    }
};
}
STubeCadEvaluation EvaluateTubeCadConstruction(const GeometryData::Tube::CTubeCadDocument& document)
{
    return EvaluateConstructionNative(document);
}
}
