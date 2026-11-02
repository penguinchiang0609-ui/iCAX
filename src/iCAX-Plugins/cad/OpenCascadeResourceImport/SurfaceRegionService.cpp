#include "pch.h"
#include "SurfaceRegionService.h"
#include "OpenCascadeBRepBuilder.h"
#include "Services/ServicesHelper.h"
#include "Task/TaskStatus.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Geom2d_Curve.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <map>
#include <set>
#include <cmath>

namespace iCAX::Geometry { namespace {
class SurfaceRegionService final:public ISurfaceRegionService {
    AUTO_REGIST_SERVICE(ISurfaceRegionService,SurfaceRegionService)
public:
    void OnLoad()override{}void OnUnload()override{}
    std::vector<SmoothSurfaceRegion> SmoothRegions(const GeometryData::BRepModel& model,
        const std::vector<std::uint64_t>& candidates,const SGeometryAnalysisOptions& options)const override {
        const auto check=[&]{if(options.IsCancelled&&options.IsCancelled())throw Tasks::TaskCanceledException();};check();
        if(!std::isfinite(options.AngleToleranceRad)||options.AngleToleranceRad<=0||options.AngleToleranceRad>=.1||!options.MaximumEdgeUses)
            throw std::invalid_argument("GEOMETRY_ANALYSIS_INVALID_OPTIONS");
        const std::set<std::uint64_t> selected(candidates.begin(),candidates.end());
        if(selected.size()!=candidates.size())throw std::invalid_argument("GEOMETRY_PATCH_SELECTION_REQUIRED");
        if(selected.empty())return {};
        const auto built=OpenCascade::BuildOpenCascadeShape(model);
        if(!built.bOK||!BRepCheck_Analyzer(built.Shape).IsValid())throw std::invalid_argument("GEOMETRY_PATCH_INVALID_BREP");
        std::map<std::uint64_t,std::uint64_t> component,region;
        for(const auto& [id,face]:built.Faces)component[id]=id;
        for(auto id:selected){if(!component.contains(id))throw std::invalid_argument("GEOMETRY_PATCH_FACE_MISSING");region[id]=id;}
        const auto root=[](const auto& parent,std::uint64_t id){while(parent.at(id)!=id)id=parent.at(id);return id;};
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> edges;
        std::map<int,std::set<std::uint64_t>> owners;size_t uses=0;
        for(const auto& [id,face]:built.Faces)for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()){
            check();if(++uses>options.MaximumEdgeUses)throw std::runtime_error("GEOMETRY_ANALYSIS_BUDGET_EXHAUSTED");
            owners[edges.Add(e.Current())].insert(id);
        }
        const auto smooth=[&](const TopoDS_Edge& edge,const TopoDS_Face& a,const TopoDS_Face& b){
            if(BRep_Tool::Degenerated(edge))return false;
            double first,last,otherFirst,otherLast;
            const auto ca=BRep_Tool::CurveOnSurface(edge,a,first,last),cb=BRep_Tool::CurveOnSurface(edge,b,otherFirst,otherLast);
            if(ca.IsNull()||cb.IsNull()||std::abs(first-otherFirst)>options.DistanceToleranceMm||std::abs(last-otherLast)>options.DistanceToleranceMm)return false;
            const BRepAdaptor_Surface sa(a),sb(b);
            for(double fraction:{.05,.15,.3,.5,.7,.85,.95}){
                check();const auto t=first+(last-first)*fraction;const auto ua=ca->Value(t),ub=cb->Value(t);gp_Pnt p;gp_Vec au,av,bu,bv;
                sa.D1(ua.X(),ua.Y(),p,au,av);sb.D1(ub.X(),ub.Y(),p,bu,bv);
                const auto na=au.Crossed(av),nb=bu.Crossed(bv);
                if(na.SquareMagnitude()<1e-24||nb.SquareMagnitude()<1e-24||!na.IsParallel(nb,options.AngleToleranceRad))return false;
            }
            return true;
        };
        for(const auto& [edge,faces]:owners){
            check();if(faces.size()!=2)continue;const auto a=*faces.begin(),b=*faces.rbegin();
            component[root(component,b)]=root(component,a);
            if(selected.contains(a)&&selected.contains(b)&&smooth(TopoDS::Edge(edges.FindKey(edge)),built.Faces.at(a),built.Faces.at(b)))
                region[root(region,b)]=root(region,a);
        }
        std::map<std::uint64_t,SmoothSurfaceRegion> grouped;
        for(auto id:selected){check();auto& group=grouped[root(region,id)];group.Component=root(component,id);group.Faces.push_back(id);
            GProp_GProps area;BRepGProp::SurfaceProperties(built.Faces.at(id),area);group.AreaMm2+=area.Mass();}
        std::vector<SmoothSurfaceRegion> result;for(auto& [_,group]:grouped)result.push_back(std::move(group));return result;
    }
};
}}
