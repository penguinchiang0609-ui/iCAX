#include "pch.h"
#include "CadBRepHealService.h"
#include "OpenCascadeFaceIntegration.h"
#include "OpenCascadeBRepBuilder.h"
#include "OpenCascadeBRepReader.h"
#include "OpenCascadeCancellation.h"
#include "OpenCascadeTaskExecution.h"
#include <optional>
#include "Services/ServicesHelper.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <BRepLib_CheckCurveOnSurface.hxx>
#include <GeomLib_CheckCurveOnSurface.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Adaptor3d_CurveOnSurface.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepTools_Modification.hxx>
#include <BRepTools_Modifier.hxx>
#include <BRepTools_ReShape.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_SurfToAnaSurf.hxx>
#include <GeomConvert_CurveToAnaCurve.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomProjLib.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_Plane.hxx>
#include <Geom_Circle.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom2d_Curve.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <Precision.hxx>
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <stdexcept>

namespace iCAX::CAD
{
namespace
{
    namespace G = GeometryData;
    std::string Type(const Handle(Geom_Surface)& surface)
    {
        switch (GeomAdaptor_Surface(surface).GetType()) {
        case GeomAbs_Plane:return "Plane";
        case GeomAbs_Cylinder:return "Cylinder";
        case GeomAbs_Cone:return "Cone";
        case GeomAbs_Sphere:return "Sphere";
        case GeomAbs_Torus:return "Torus";
        case GeomAbs_SurfaceOfExtrusion:return "Extrusion";
        case GeomAbs_SurfaceOfRevolution:return "Revolution";
        case GeomAbs_BSplineSurface:return "BSpline";
        case GeomAbs_BezierSurface:return "Bezier";
        case GeomAbs_OffsetSurface:return "Offset";
        default:return "Other";
        }
    }
    double Area(const TopoDS_Shape& shape)
    { GProp_GProps p; BRepGProp::SurfaceProperties(shape,p,1e-9);return p.Mass(); }
    double Perimeter(const TopoDS_Shape& shape)
    { GProp_GProps p;BRepGProp::LinearProperties(shape,p);return p.Mass(); }
    double Volume(const TopoDS_Shape& shape)
    {
        TopTools_IndexedMapOfShape solids;TopExp::MapShapes(shape,TopAbs_SOLID,solids);double volume=0;
        for(int i=1;i<=solids.Extent();++i){GProp_GProps p;BRepGProp::VolumeProperties(solids(i),p,1e-9);volume+=p.Mass();}
        return volume;
    }
    int Count(const TopoDS_Shape& shape,TopAbs_ShapeEnum type)
    { TopTools_IndexedMapOfShape map;TopExp::MapShapes(shape,type,map);return map.Extent(); }
    TopoDS_Compound Frame(const TopoDS_Shape& shape)
    { BRep_Builder b;TopoDS_Compound frame;b.MakeCompound(frame);b.Add(frame,shape);return frame; }

    using OpenCascade::integration_detail::RefinedArea;
    using OpenCascade::integration_detail::RefinedVolume;

    bool CanonicalSupport(Handle(Geom_Surface) surface)
    {
        if(const auto trim=Handle(Geom_RectangularTrimmedSurface)::DownCast(surface))surface=trim->BasisSurface();
        const auto type=GeomAdaptor_Surface(surface).GetType();
        return type==GeomAbs_Plane||type==GeomAbs_Cylinder||type==GeomAbs_Cone
            ||type==GeomAbs_Sphere||type==GeomAbs_Torus;
    }

    // A tensor-product spline is an extrusion when every row is the same
    // rational profile translated linearly in the sweep parameter. Positive
    // weights, partition of unity and linear precision prove this over the
    // entire control net, rather than from a grid of surface samples.
    Handle(Geom_Surface) Extrusion(const Handle(Geom_BSplineSurface)& spline,double tolerance,double& gap)
    {
        for(bool sweepU:{true,false}) {
            const int n=sweepU?spline->NbUPoles():spline->NbVPoles();
            const int m=sweepU?spline->NbVPoles():spline->NbUPoles();
            const int degree=sweepU?spline->UDegree():spline->VDegree();
            if((sweepU?spline->IsUPeriodic():spline->IsVPeriodic())||n<2)continue;
            const auto& knots=sweepU?spline->UKnotSequence():spline->VKnotSequence();
            std::vector<double> parameter(n);
            for(int i=1;i<=n;++i){double value=0;for(int k=1;k<=degree;++k)value+=knots(i+k);
                parameter[i-1]=value/degree;}
            const auto pole=[&](int i,int j){return sweepU?spline->Pole(i,j):spline->Pole(j,i);};
            const auto weight=[&](int i,int j){return sweepU?spline->Weight(i,j):spline->Weight(j,i);};
            const double span=parameter.back()-parameter.front();if(span<=0)continue;
            const gp_Vec velocity=gp_Vec(pole(1,1),pole(n,1))/span;
            if(velocity.Magnitude()<=tolerance)continue;
            double error=0;bool equalWeights=true;
            for(int j=1;j<=m;++j)for(int i=1;i<=n;++i){
                if(weight(i,j)<=0||std::abs(weight(i,j)-weight(1,j))>
                    32*std::numeric_limits<double>::epsilon()*std::max(weight(i,j),weight(1,j)))equalWeights=false;
                error=std::max(error,pole(i,j).Distance(pole(1,j).Translated(velocity*(parameter[i-1]-parameter.front()))));
            }
            if(!equalWeights||error>tolerance)continue;
            double u0,u1,v0,v1;spline->Bounds(u0,u1,v0,v1);
            const auto profile=sweepU?spline->UIso(u0):spline->VIso(v0);
            gap=error;
            return new Geom_SurfaceOfLinearExtrusion(profile,gp_Dir(velocity));
        }
        return {};
    }

    Handle(Geom_Surface) Cone(const Handle(Geom_BSplineSurface)& spline,double tolerance,double& gap)
    {
        double u0,u1,v0,v1;spline->Bounds(u0,u1,v0,v1);
        for(bool sweepU:{true,false}) {
            if(sweepU?spline->IsUPeriodic():spline->IsVPeriodic())continue;
            const auto a=sweepU?spline->UIso(u0):spline->VIso(v0);
            const auto b=sweepU?spline->UIso(u1):spline->VIso(v1);
            double first,last,errorA=0,errorB=0;
            const auto ca=Handle(Geom_Circle)::DownCast(GeomConvert_CurveToAnaCurve::ComputeCircle(
                a,tolerance,a->FirstParameter(),a->LastParameter(),first,last,errorA));
            if(ca.IsNull())continue;
            const auto cb=Handle(Geom_Circle)::DownCast(GeomConvert_CurveToAnaCurve::ComputeCircle(
                b,tolerance,b->FirstParameter(),b->LastParameter(),first,last,errorB));
            if(cb.IsNull())continue;
            const gp_Vec travel(ca->Location(),cb->Location());
            const double height=travel.Magnitude(),r0=ca->Radius(),r1=cb->Radius();
            if(height<=tolerance||r0<=tolerance||std::abs(r1-r0)<=tolerance)continue;
            const gp_Dir axis(travel);
            double error=std::max(errorA,errorB)*std::max(1.,r1/r0)
                +std::max(r0,r1)*gp_Vec(axis).Crossed(gp_Vec(ca->Axis().Direction())).Magnitude()
                +std::max(r0,r1)*gp_Vec(axis).Crossed(gp_Vec(cb->Axis().Direction())).Magnitude();
            const int n=sweepU?spline->NbUPoles():spline->NbVPoles();
            const int m=sweepU?spline->NbVPoles():spline->NbUPoles();
            const int degree=sweepU?spline->UDegree():spline->VDegree();
            const auto& knots=sweepU?spline->UKnotSequence():spline->VKnotSequence();
            std::vector<double> p(n);
            for(int i=1;i<=n;++i){double value=0;for(int k=1;k<=degree;++k)value+=knots(i+k);p[i-1]=value/degree;}
            if(p.back()<=p.front())continue;
            const auto pole=[&](int i,int j){return sweepU?spline->Pole(i,j):spline->Pole(j,i);};
            const auto weight=[&](int i,int j){return sweepU?spline->Weight(i,j):spline->Weight(j,i);};
            bool equalWeights=true;double residual=0;
            // All profile control points must follow the SAME axial affine
            // homothety, with unchanged positive rational weights. This proves
            // a ruled cone across the whole spline, including its interior.
            for(int j=1;j<=m;++j)for(int i=1;i<=n;++i){
                if(weight(i,j)<=0||std::abs(weight(i,j)-weight(1,j))>
                    32*std::numeric_limits<double>::epsilon()*std::max(weight(i,j),weight(1,j)))equalWeights=false;
                const double t=(p[i-1]-p.front())/(p.back()-p.front());
                const auto expected=ca->Location().Translated(travel*t
                    +gp_Vec(ca->Location(),pole(1,j))*((r0+t*(r1-r0))/r0));
                residual=std::max(residual,expected.Distance(pole(i,j)));
            }
            error+=residual;
            if(equalWeights&&std::isfinite(error)&&error<=tolerance){
                gap=error;return new Geom_ConicalSurface(gp_Ax3(ca->Location(),axis,ca->Position().XDirection()),
                    std::atan2(r1-r0,height),r0);
            }
        }
        return {};
    }

    struct Conversion
    {
        struct Trim { TopoDS_Edge Edge; Handle(Geom2d_Curve) PCurve; double Tolerance; };
        TopoDS_Face Face;
        Handle(Geom_Surface) Surface;
        bool ReverseWires=false;
        double Gap=0, Tolerance=0;
        std::vector<Trim> Trims;
    };

    void ProjectTrims(Conversion& c)
    {
        for(TopExp_Explorer it(c.Face,TopAbs_EDGE);it.More();it.Next()) {
            const auto edge=TopoDS::Edge(it.Current());
            if(BRep_Tool::IsClosed(edge,c.Face)||BRep_Tool::Degenerated(edge))
                throw std::runtime_error("Periodic seam or degenerate trim requires a dedicated parameter map");
            double first,last;const auto curve=BRep_Tool::Curve(edge,first,last);
            if(curve.IsNull())throw std::runtime_error("Trim has no 3D curve");
            double tolerance=BRep_Tool::Tolerance(edge);
            const auto pc=GeomProjLib::Curve2d(curve,first,last,c.Surface,tolerance);
            if(pc.IsNull())throw std::runtime_error("Trim projection failed");
            Handle(GeomAdaptor_Curve) curve3d=new GeomAdaptor_Curve(curve,first,last);
            Handle(Geom2dAdaptor_Curve) curve2d=new Geom2dAdaptor_Curve(pc,first,last);
            Handle(GeomAdaptor_Surface) surface=new GeomAdaptor_Surface(c.Surface);
            Handle(Adaptor3d_CurveOnSurface) curveOnSurface=new Adaptor3d_CurveOnSurface(curve2d,surface);
            GeomLib_CheckCurveOnSurface check(curve3d);check.Perform(curveOnSurface);
            if(!check.IsDone())throw std::runtime_error("Projected trim deviation could not be measured");
            const auto point=curve->Value(first);
            const double roundoff=128*std::numeric_limits<double>::epsilon()
                *std::max({1.,std::abs(point.X()),std::abs(point.Y()),std::abs(point.Z())});
            tolerance=std::max(BRep_Tool::Tolerance(edge),check.MaxDistance()+roundoff);
            if(tolerance>std::max(c.Tolerance,BRep_Tool::Tolerance(edge)))
                throw std::runtime_error("Projected trim exceeds the configured geometry budget");
            c.Trims.push_back({edge,pc,tolerance});
        }
    }

    class Modifier final : public BRepTools_Modification
    {
    public:
        std::vector<Conversion> Conversions;
        const Conversion* Find(const TopoDS_Face& face) const
        { for(const auto& c:Conversions)if(c.Face.IsSame(face))return &c;return nullptr; }
        bool NewSurface(const TopoDS_Face& f,Handle(Geom_Surface)& s,TopLoc_Location& l,
            double& tolerance,bool& reverseWires,bool& reverseFace) override
        {
            const auto* c=Find(f);if(!c)return false;
            s=c->Surface;l=TopLoc_Location();tolerance=std::max(BRep_Tool::Tolerance(f),c->Gap);
            reverseWires=c->ReverseWires;reverseFace=c->ReverseWires;return true;
        }
        bool NewCurve(const TopoDS_Edge& e,Handle(Geom_Curve)& c,TopLoc_Location& l,double& tolerance) override
        {
            // Copy all edges so an updated pcurve never changes caller input.
            double first,last;c=BRep_Tool::Curve(e,l,first,last);
            if(!c.IsNull())c=Handle(Geom_Curve)::DownCast(c->Copy());
            tolerance=BRep_Tool::Tolerance(e);
            // BRepTools_Modifier takes edge tolerances from NewCurve; its
            // NewCurve2d callback does not update them. Use the measured
            // projected-curve deviation, never an arbitrary repair tolerance.
            for(const auto& conversion:Conversions)for(const auto& trim:conversion.Trims)
                if(trim.Edge.IsSame(e))tolerance=std::max(tolerance,trim.Tolerance);
            return true;
        }
        bool NewPoint(const TopoDS_Vertex&,gp_Pnt&,double&) override{return false;}
        bool NewParameter(const TopoDS_Vertex&,const TopoDS_Edge&,double&,double&) override{return false;}
        bool NewCurve2d(const TopoDS_Edge& e,const TopoDS_Face& f,const TopoDS_Edge&,
            const TopoDS_Face&,Handle(Geom2d_Curve)& pc,double& tolerance) override
        {
            double first,last;tolerance=BRep_Tool::Tolerance(e);
            if(const auto* c=Find(f)) {
                for(const auto& trim:c->Trims)if(trim.Edge.IsSame(e)){
                    pc=Handle(Geom2d_Curve)::DownCast(trim.PCurve->Copy());tolerance=trim.Tolerance;return true;
                }
                throw std::runtime_error("Projected trim is missing from face conversion");
            } else {
                pc=BRep_Tool::CurveOnSurface(e,f,first,last);
                if(!pc.IsNull())pc=Handle(Geom2d_Curve)::DownCast(pc->Copy());
            }
            return true;
        }
        GeomAbs_Shape Continuity(const TopoDS_Edge& e,const TopoDS_Face& a,const TopoDS_Face& b,
            const TopoDS_Edge&,const TopoDS_Face&,const TopoDS_Face&) override
        {return BRep_Tool::Continuity(e,a,b);}
    };

    Conversion Candidate(const TopoDS_Face& face,const SCadBRepHealOptions& options)
    {
        Conversion c;c.Face=face;c.Tolerance=options.LinearTolerance;
        auto original=BRep_Tool::Surface(face);
        if(const auto trim=Handle(Geom_RectangularTrimmedSurface)::DownCast(original))original=trim->BasisSurface();
        const auto type=GeomAdaptor_Surface(original).GetType();
        if(CanonicalSupport(original))return c;
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        if(type==GeomAbs_BSplineSurface||type==GeomAbs_BezierSurface) {
            const auto spline=GeomConvert::SurfaceToBSplineSurface(original);
            const GeomLib_IsPlanarSurface plane(spline,options.LinearTolerance);
            if(plane.IsPlanar()) {
                double gap=0;for(int i=1;i<=spline->NbUPoles();++i)for(int j=1;j<=spline->NbVPoles();++j)
                    gap=std::max(gap,plane.Plan().Distance(spline->Pole(i,j)));
                if(gap<=options.LinearTolerance){c.Surface=new Geom_Plane(plane.Plan());c.Gap=gap;}
            }
        }
        if(c.Surface.IsNull()) {
            GeomConvert_SurfToAnaSurf converter(original);
            c.Surface=converter.ConvertToAnalytical(options.LinearTolerance,u0,u1,v0,v1);
            c.Gap=converter.Gap();
            if(!c.Surface.IsNull()&&(!std::isfinite(c.Gap)||c.Gap<0||c.Gap>options.LinearTolerance))c.Surface.Nullify();
        }
        if(c.Surface.IsNull()&&(type==GeomAbs_BSplineSurface||type==GeomAbs_BezierSurface)) {
            const auto spline=GeomConvert::SurfaceToBSplineSurface(original);
            c.Surface=Extrusion(spline,options.LinearTolerance,c.Gap);
            if(c.Surface.IsNull())c.Surface=Cone(spline,options.LinearTolerance,c.Gap);
        }
        if(const auto extrusion=Handle(Geom_SurfaceOfLinearExtrusion)::DownCast(c.Surface)) {
            const auto profile=extrusion->BasisCurve();double first,last,circleGap=0;
            const auto circleCurve=GeomConvert_CurveToAnaCurve::ComputeCircle(profile,
                options.LinearTolerance,profile->FirstParameter(),profile->LastParameter(),first,last,circleGap);
            if(!circleCurve.IsNull()) {
                const auto circle=Handle(Geom_Circle)::DownCast(circleCurve);
                if(!circle.IsNull()) {
                    const auto axis=extrusion->Direction();const auto position=circle->Position();
                    const double totalGap=c.Gap+circleGap+circle->Radius()*gp_Vec(axis).Crossed(gp_Vec(position.Direction())).Magnitude();
                    if(std::isfinite(totalGap)&&totalGap<=options.LinearTolerance) {
                        c.Surface=new Geom_CylindricalSurface(gp_Ax3(position.Location(),axis,position.XDirection()),circle->Radius());
                        c.Gap=totalGap;
                    }
                }
            }
        }
        if(c.Surface.IsNull()||Type(c.Surface)==Type(original)) {c.Surface.Nullify();return c;}
        if(Type(c.Surface)=="Plane"&&(type==GeomAbs_BSplineSurface||type==GeomAbs_BezierSurface)) {
            const auto spline=GeomConvert::SurfaceToBSplineSurface(original);
            const auto plane=Handle(Geom_Plane)::DownCast(c.Surface)->Pln();c.Gap=0;
            for(int i=1;i<=spline->NbUPoles();++i)for(int j=1;j<=spline->NbVPoles();++j)
                c.Gap=std::max(c.Gap,plane.Distance(spline->Pole(i,j)));
            if(c.Gap>options.LinearTolerance){c.Surface.Nullify();return c;}
        }
        // Put a recovered cylinder's artificial seam opposite the source
        // patch interior. Independently projected trim edges then use the
        // same angular branch instead of opening a wire at atan2's cut.
        gp_Pnt point;gp_Vec du,dv;original->D1((u0+u1)/2,(v0+v1)/2,point,du,dv);
        if(const auto cylinder=Handle(Geom_CylindricalSurface)::DownCast(c.Surface)) {
            const auto axes=cylinder->Position();const gp_Vec axis(axes.Direction());
            auto radial=gp_Vec(axes.Location(),point);radial-=axis*radial.Dot(axis);
            if(radial.SquareMagnitude()>1e-24)cylinder->SetPosition(gp_Ax3(axes.Location(),axes.Direction(),gp_Dir(-radial)));
        }
        if(const auto cone=Handle(Geom_ConicalSurface)::DownCast(c.Surface)) {
            const auto axes=cone->Position();const gp_Vec axis(axes.Direction());
            auto radial=gp_Vec(axes.Location(),point);radial-=axis*radial.Dot(axis);
            if(radial.SquareMagnitude()>1e-24)cone->SetPosition(gp_Ax3(axes.Location(),axes.Direction(),gp_Dir(-radial)));
        }
        // Compare support normals at the same physical point; UV laws may differ.
        const auto normal=du.Crossed(dv);GeomAPI_ProjectPointOnSurf projection(point,c.Surface);
        if(projection.NbPoints()==0||normal.SquareMagnitude()<1e-24) {c.Surface.Nullify();return c;}
        double u,v;projection.LowerDistanceParameters(u,v);c.Surface->D1(u,v,point,du,dv);
        const auto newNormal=du.Crossed(dv);
        if(newNormal.SquareMagnitude()<1e-24){c.Surface.Nullify();return c;}
        c.ReverseWires=normal.Dot(newNormal)<0;
        return c;
    }

    bool ValidateFace(const TopoDS_Face& before,const TopoDS_Face& after,const SCadBRepHealOptions& options,
        SCadBRepHealFace& record)
    {
        if(Count(before,TopAbs_WIRE)!=Count(after,TopAbs_WIRE)||Count(before,TopAbs_EDGE)!=Count(after,TopAbs_EDGE)) {
            record.Reason="Trim wire or edge count changed";return false;
        }
        record.AreaDifference=std::abs(std::abs(Area(before))-std::abs(Area(after)));
        const double areaAllowance=options.LinearTolerance*Perimeter(before)
            +options.LinearTolerance*options.LinearTolerance;
        if(record.AreaDifference>areaAllowance) {
            record.AreaMethod="Adaptive trim-domain Green integral";
            record.AreaDifference=std::abs(RefinedArea(before)-RefinedArea(after));
        }
        if(record.AreaDifference>areaAllowance){record.Reason="Trimmed area exceeds tolerance budget: source="
            +std::to_string(std::abs(Area(before)))+", result="+std::to_string(std::abs(Area(after)))
            +", allowance="+std::to_string(areaAllowance);return false;}
        double boundaryBudget=options.LinearTolerance;
        for(TopExp_Explorer e(before,TopAbs_EDGE);e.More();e.Next())
            boundaryBudget=std::max(boundaryBudget,BRep_Tool::Tolerance(TopoDS::Edge(e.Current())));
        for(TopExp_Explorer e(after,TopAbs_EDGE);e.More();e.Next()) {
            const auto edge=TopoDS::Edge(e.Current());if(BRep_Tool::Degenerated(edge))continue;
            BRepLib_CheckCurveOnSurface check(edge,after);check.Perform();
            if(!check.IsDone()){record.Reason="Boundary deviation could not be evaluated";return false;}
            record.BoundaryDeviation=std::max(record.BoundaryDeviation,check.MaxDistance());
            if(check.MaxDistance()>boundaryBudget||BRep_Tool::Tolerance(edge)>boundaryBudget) {
                record.Reason="Projected trim exceeds tolerance budget";return false;
            }
        }
        BRepCheck_Analyzer validity(after,true);
        if(!validity.IsValid()) {
            record.Reason="Converted trim is invalid (OCC status)";
            for(const auto type:{TopAbs_EDGE,TopAbs_WIRE,TopAbs_FACE}) {
                TopTools_IndexedMapOfShape shapes;TopExp::MapShapes(after,type,shapes);
                for(int i=1;i<=shapes.Extent();++i) {
                    const auto checked=validity.Result(shapes(i));if(checked.IsNull())continue;
                    for(const auto code:checked->Status())if(code!=BRepCheck_NoError)
                        record.Reason+=" "+std::to_string(static_cast<int>(type))+":"+std::to_string(static_cast<int>(code));
                    for(checked->InitContextIterator();checked->MoreShapeInContext();checked->NextShapeInContext())
                        for(const auto code:checked->StatusOnShape())if(code!=BRepCheck_NoError)
                            record.Reason+=" "+std::to_string(static_cast<int>(type))+"/"+std::to_string(static_cast<int>(checked->ContextualShape().ShapeType()))+":"+std::to_string(static_cast<int>(code));
                }
            }
            return false;
        }
        double u0,u1,v0,v1;BRepTools::UVBounds(before,u0,u1,v0,v1);
        gp_Pnt point;gp_Vec du,dv;BRep_Tool::Surface(before)->D1((u0+u1)/2,(v0+v1)/2,point,du,dv);
        auto normal=du.Crossed(dv);if(before.Orientation()==TopAbs_REVERSED)normal.Reverse();
        GeomAPI_ProjectPointOnSurf projection(point,BRep_Tool::Surface(after));
        if(!projection.NbPoints()){record.Reason="Physical normal could not be compared";return false;}
        double u,v;projection.LowerDistanceParameters(u,v);BRep_Tool::Surface(after)->D1(u,v,point,du,dv);
        auto target=du.Crossed(dv);if(after.Orientation()==TopAbs_REVERSED)target.Reverse();
        if(normal.Dot(target)<=0){record.Reason="Physical face normal changed";return false;}
        return true;
    }
}

SCadBRepHealResult CCadBRepHealService::Heal(const G::BRepModel& geometry,
    const SCadBRepHealOptions& options,const std::function<bool()>& cancelled) const
{
    SCadBRepHealResult result;result.Geometry=geometry;
    const auto rollback=[&](const std::string& reason){
        result.Success=true;result.RejectedChanges=true;result.Geometry=geometry;result.ConvertedFaces=result.MergedFaces=0;
        result.OutputVolume=result.InputVolume;result.VolumeError=0;result.VolumeMethod="Unchanged validated input";
        for(auto& face:result.Faces){face.ResultFaceIds={face.SourceFaceId};face.ResultType=face.SourceType;
            if(face.Status=="converted"||face.Status=="merged"||face.Status=="converted+merged"){
                face.Status="retained";face.Reason=reason;}}
        result.Diagnostics.push_back("Conversion batch not committed; validated original retained: "+reason);
    };
    OpenCascade::COpenCascadeCancellationScope cancellation(cancelled);
    if(!std::isfinite(options.LinearTolerance)||options.LinearTolerance<Precision::Confusion()
        ||!std::isfinite(options.AngularTolerance)||options.AngularTolerance<=0) {
        result.Diagnostics.push_back("Heal requires finite positive tolerances (linear >= kernel confusion)");return result;
    }
    cancellation.ThrowIfCancellationRequested();
    const auto built=OpenCascade::ReconstructOpenCascadeShape(geometry);
    if(!built.bOK||built.Shape.IsNull()){result.Diagnostics=built.Diagnostics;return result;}
    for(const auto& face:geometry.Faces) {
        const auto native=built.Faces.find(face.Id);if(native==built.Faces.end())continue;
        SCadBRepHealFace record;record.SourceFaceId=face.Id;record.SourceType=record.ResultType=Type(BRep_Tool::Surface(native->second));
        record.Status="kept";record.Reason="Already canonical or no validated simpler support";
        record.ResultFaceIds={face.Id};result.Faces.push_back(std::move(record));
    }
    if(!BRepCheck_Analyzer(built.Shape,true,true).IsValid()) {
        result.Diagnostics.push_back("Input BRep is invalid; no blanket healing, sewing or face deletion was applied");return result;
    }
    Handle(Modifier) modifier=new Modifier;
    struct FaceWork {
        TopoDS_Face Original,Private;
        std::vector<std::pair<TopoDS_Edge,TopoDS_Edge>> EdgeMap;
        std::optional<Conversion> Converted;
    };
    std::vector<FaceWork> work(result.Faces.size());
    // OCCT evaluation may populate geometry caches. Each task owns its
    // topology and support curves; publish only validated conversions later.
    for(std::size_t i=0;i<work.size();++i){
        cancellation.ThrowIfCancellationRequested();
        auto& item=work[i];item.Original=built.Faces.at(result.Faces[i].SourceFaceId);
        // These supports cannot produce a conversion. Do not deep-copy their
        // topology and curves merely to rediscover that fact in a worker.
        if(CanonicalSupport(BRep_Tool::Surface(item.Original)))continue;
        BRepBuilderAPI_Copy copy(item.Original,true,false);item.Private=TopoDS::Face(copy.Shape());
        TopTools_IndexedMapOfShape edges;TopExp::MapShapes(item.Original,TopAbs_EDGE,edges);
        for(int e=1;e<=edges.Extent();++e)item.EdgeMap.emplace_back(
            TopoDS::Edge(copy.ModifiedShape(edges(e))),TopoDS::Edge(edges(e)));
    }
    result.NormalizationWorkers=OpenCascade::detail::ForGeometryIndices(work.size(),options.MaximumConcurrency,[&](std::size_t i){
        auto& record=result.Faces[i];auto& item=work[i];const auto face=item.Private;
        if(face.IsNull())return;
        try {
            auto c=Candidate(face,options);if(c.Surface.IsNull())return;
            record.SupportDeviation=c.Gap;
            ProjectTrims(c);
            Handle(Modifier) single=new Modifier;single->Conversions.push_back(c);
            const auto frame=Frame(face);BRepTools_Modifier trial(frame,single);
            if(!trial.IsDone()){record.Reason="Face conversion did not complete";return;}
            const auto converted=TopoDS::Face(TopoDS_Iterator(trial.ModifiedShape(frame)).Value());
            if(!ValidateFace(face,converted,options,record))return;
            // Modifier commits against original topology. Transfer the exact
            // validated pcurves with an explicit private-to-original edge map.
            c.Face=item.Original;
            for(auto& trim:c.Trims){
                const auto match=std::find_if(item.EdgeMap.begin(),item.EdgeMap.end(),[&](const auto& pair){return pair.first.IsSame(trim.Edge);});
                if(match==item.EdgeMap.end())throw std::runtime_error("Normalized trim has no original edge mapping");
                trim.Edge=match->second;
            }
            record.Status="converted";record.Reason="Support and original trim geometry validated";
            record.ResultType=Type(c.Surface);item.Converted=std::move(c);
        } catch(const Standard_Failure& error){record.Reason=error.GetMessageString();}
          catch(const std::exception& error){record.Reason=error.what();}
    });
    for(auto& item:work)if(item.Converted)modifier->Conversions.push_back(std::move(*item.Converted));
    auto shape=built.Shape;std::map<std::uint64_t,TopoDS_Face> current=built.Faces;
    if(!modifier->Conversions.empty()) {
        const auto frame=Frame(shape);BRepTools_Modifier apply(frame,modifier);
        if(!apply.IsDone()||!BRepCheck_Analyzer(apply.ModifiedShape(frame),true,true).IsValid()) {
            rollback("Combined normalization failed topology validation; input retained");return result;
        }
        shape=TopoDS_Iterator(apply.ModifiedShape(frame)).Value();
        for(auto& [id,face]:current)face=TopoDS::Face(apply.ModifiedShape(face));
        result.ConvertedFaces=modifier->Conversions.size();
    }
    if(options.MergeSameDomain) {
        // Never unify a compound: coincident faces in separate nested parts
        // must remain independent. Only explicit, existing solids are owners.
        TopTools_IndexedMapOfShape solids;TopExp::MapShapes(shape,TopAbs_SOLID,solids);
        TopTools_IndexedDataMapOfShapeListOfShape owners;TopExp::MapShapesAndAncestors(shape,TopAbs_FACE,TopAbs_SOLID,owners);
        Handle(BRepTools_ReShape) replacements=new BRepTools_ReShape;
        for(int i=1;i<=solids.Extent();++i) {
            cancellation.ThrowIfCancellationRequested();const auto solid=solids(i);
            bool sharedOwner=false;
            for(TopExp_Explorer f(solid,TopAbs_FACE);f.More();f.Next())
                if(owners.Contains(f.Current())&&owners.FindFromKey(f.Current()).Size()>1){sharedOwner=true;break;}
            if(sharedOwner){result.Diagnostics.push_back("Same-domain merge skipped for a solid sharing face ownership");continue;}
            ShapeUpgrade_UnifySameDomain unify(solid,false,true,false);
            unify.SetSafeInputMode(true);unify.SetLinearTolerance(options.LinearTolerance);
            unify.SetAngularTolerance(options.AngularTolerance);unify.Build();
            const auto merged=unify.Shape();
            if(merged.IsNull()||!BRepCheck_Analyzer(merged,true,true).IsValid()
                ||std::abs(Area(solid)-Area(merged))>options.LinearTolerance*Perimeter(solid)
                ||std::abs(Volume(solid)-Volume(merged))>options.LinearTolerance*std::abs(Area(solid))) {
                result.Diagnostics.push_back("Rejected a same-domain merge that failed solid validation");continue;
            }
            const int removed=Count(solid,TopAbs_FACE)-Count(merged,TopAbs_FACE);
            if(removed<=0)continue;
            auto mapped=current;bool completeHistory=true;
            for(auto& [id,face]:mapped) {
                const auto& modified=unify.History()->Modified(face);
                if(!modified.IsEmpty()) {
                    if(modified.Size()!=1||modified.First().ShapeType()!=TopAbs_FACE){completeHistory=false;break;}
                    face=TopoDS::Face(modified.First());
                } else if(unify.History()->IsRemoved(face)){completeHistory=false;break;}
            }
            if(!completeHistory){result.Diagnostics.push_back("Rejected a same-domain merge without complete face history");continue;}
            current=std::move(mapped);
            result.MergedFaces+=removed;replacements->Replace(solid,merged);
        }
        shape=replacements->Apply(shape);
        if(solids.IsEmpty())result.Diagnostics.push_back("Same-domain merge skipped: no explicit solid ownership");
    }
    if(!result.ConvertedFaces&&!result.MergedFaces) {
        result.Success=true;result.Diagnostics.push_back("Heal validated the input; no geometry change was required");return result;
    }
    if(Count(shape,TopAbs_SOLID)!=Count(built.Shape,TopAbs_SOLID)||!BRepCheck_Analyzer(shape,true,true).IsValid()) {
        rollback("Heal result changed solid topology or became invalid");return result;
    }
    if(Count(shape,TopAbs_SOLID)>0) {
        result.InputVolume=Volume(built.Shape);result.OutputVolume=Volume(shape);
        result.VolumeError=std::abs(result.InputVolume-result.OutputVolume);
        result.VolumeAllowance=options.LinearTolerance*std::abs(Area(built.Shape));
        if(result.VolumeError>result.VolumeAllowance) {
            Bnd_Box bounds;BRepBndLib::AddOptimal(built.Shape,bounds,false,false);
            double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);const gp_Pnt origin((x+X)/2,(y+Y)/2,(z+Z)/2);
            try{result.InputVolume=RefinedVolume(built.Shape,origin);result.OutputVolume=RefinedVolume(shape,origin);}
            catch(const Standard_Failure& error){rollback(error.GetMessageString());return result;}
            catch(const std::runtime_error& error){rollback(error.what());return result;}
            result.VolumeError=std::abs(result.InputVolume-result.OutputVolume);
            result.VolumeMethod="Adaptive trim-domain divergence integral";
        }
        if(result.VolumeError>result.VolumeAllowance) {
            rollback("Heal changed signed material volume beyond its geometry tolerance budget: input="
                +std::to_string(result.InputVolume)+", output="+std::to_string(result.OutputVolume)
                +", allowance="+std::to_string(result.VolumeAllowance));return result;
        }
    }
    TopTools_IndexedMapOfShape outputFaces;std::vector<std::uint64_t> outputIds;
    result.Geometry=OpenCascade::ConvertOpenCascadeShapeToBRepWithDisplay(shape,geometry.Metadata.Name,geometry.Metadata.SourceId,
        [&](const TopoDS_Face& face,std::uint64_t id){
            const int index=outputFaces.Add(face);if(outputIds.size()<static_cast<std::size_t>(index))outputIds.resize(index);
            outputIds[index-1]=id;
            // Exact-only callers intentionally omit display triangulations.
            // Normalizing a support must not create a discarded intermediate mesh.
            if(geometry.Triangulations3.empty())return G::Triangulation3{};
            for(const auto& [sourceId,mapped]:current)if(mapped.IsSame(face)) {
                const auto sourceFace=std::find_if(geometry.Faces.begin(),geometry.Faces.end(),[&](const auto& f){return f.Id==sourceId;});
                if(sourceFace!=geometry.Faces.end())for(const auto& mesh:geometry.Triangulations3)
                    if(mesh.Id==sourceFace->Triangulation3Id&&result.MergedFaces==0){auto display=mesh.Geometry;display.TriangleFaceIds.assign(display.Triangles.size(),id);return display;}
            }
            const auto display=OpenCascade::ConvertOpenCascadeShapeToBRep(face,"","",0.01);
            if(display.Triangulations3.empty())return G::Triangulation3{};
            auto mesh=display.Triangulations3.front().Geometry;mesh.TriangleFaceIds.assign(mesh.Triangles.size(),id);return mesh;
        });
    result.Geometry.Metadata=geometry.Metadata;
    for(auto& record:result.Faces) {
        const int index=outputFaces.FindIndex(current.at(record.SourceFaceId));
        if(index<1){rollback("Original face has no output history mapping");return result;}
        record.ResultFaceIds={outputIds[index-1]};
        record.ResultType=Type(BRep_Tool::Surface(current.at(record.SourceFaceId)));
        if(std::count_if(current.begin(),current.end(),[&](const auto& source){return source.second.IsSame(current.at(record.SourceFaceId));})>1) {
            record.Status=record.Status=="converted"?"converted+merged":"merged";
            record.Reason="Validated same-domain merge within the original solid; all source face IDs retained in history";
        }
        const auto src=std::find_if(geometry.Faces.begin(),geometry.Faces.end(),[&](const auto& f){return f.Id==record.SourceFaceId;});
        const auto dest=std::find_if(result.Geometry.Faces.begin(),result.Geometry.Faces.end(),[&](const auto& f){return f.Id==outputIds[index-1];});
        if(src!=geometry.Faces.end()&&dest!=result.Geometry.Faces.end())dest->Metadata=src->Metadata;
    }
    const auto restored=OpenCascade::ReconstructOpenCascadeShape(result.Geometry);
    result.Success=restored.bOK&&!restored.Shape.IsNull()&&BRepCheck_Analyzer(restored.Shape,true,true).IsValid();
    if(!result.Success){rollback("Neutral output round trip failed; input retained");return result;}
    result.Diagnostics.push_back("Heal normalized "+std::to_string(result.ConvertedFaces)+" faces and merged "+std::to_string(result.MergedFaces)+" same-domain faces within original solids");
    return result;
}

namespace { class CRegisteredCadBRepHealService final : public ICadBRepHealService {
    AUTO_REGIST_SERVICE(iCAX::CAD::ICadBRepHealService,CRegisteredCadBRepHealService)
public:
    void OnLoad() override{} void OnUnload() override{}
    SCadBRepHealResult Heal(const G::BRepModel& geometry,const SCadBRepHealOptions& options,
        const std::function<bool()>& cancel) const override {return CCadBRepHealService{}.Heal(geometry,options,cancel);}
}; }
}
