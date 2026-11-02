#pragma once
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GeomConvert_SurfToAnaSurf.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <GeomConvert.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <optional>
#include <vector>
#include <limits>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <utility>
namespace iCAX::ExtrusionRecognition::section_detail {
    inline bool CylindricalProjectionCoefficients(const TopoDS_Face& face,double& a,double& b)
    {
        const auto source=BRep_Tool::Surface(face);GeomAdaptor_Surface native(source);
        if(native.GetType()==GeomAbs_Cylinder) {
            const auto frame=native.Cylinder().Position();a=frame.XDirection().X();b=frame.YDirection().X();return true;
        }
        if(native.GetType()!=GeomAbs_SurfaceOfRevolution)return false;
        const auto basis=native.BasisCurve();
        if(basis.IsNull()||basis->GetType()!=GeomAbs_Line
            ||!basis->Line().Direction().IsParallel(native.AxeOfRevolution().Direction(),Precision::Angular()))return false;
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        gp_Pnt point;gp_Vec du,dv;source->D1(0,(v0+v1)/2,point,du,dv);a=du.Crossed(dv).X();
        source->D1(1.570796326794896619231321691639751442,(v0+v1)/2,point,du,dv);b=du.Crossed(dv).X();
        return true;
    }

    inline bool SplineProjectionIsInjective(const TopoDS_Face& face)
    {
        const auto source=Handle(Geom_BSplineSurface)::DownCast(BRep_Tool::Surface(face));
        if(source.IsNull()||source->IsURational()||source->IsVRational())return false;
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        auto spline=Handle(Geom_BSplineSurface)::DownCast(source->Copy());
        try{spline->Segment(u0,u1,v0,v1,0.0,0.0);}catch(const Standard_Failure&){return false;}
        std::vector<gp_Vec> du,dv;
        for(int i=1;i<spline->NbUPoles();++i)for(int j=1;j<=spline->NbVPoles();++j)
            du.emplace_back(spline->Pole(i,j),spline->Pole(i+1,j));
        for(int i=1;i<=spline->NbUPoles();++i)for(int j=1;j<spline->NbVPoles();++j)
            dv.emplace_back(spline->Pole(i,j),spline->Pole(i,j+1));
        if(du.empty()||dv.empty())return false;
        const auto sign=[](double lo,double hi){return lo>0?1:hi<0?-1:0;};
        const auto coordinateRange=[](const auto& values,bool y){
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(const auto& d:values){const double value=y?d.Y():d.Z();lo=std::min(lo,value);hi=std::max(hi,value);}
            return std::pair{lo,hi};
        };
        const auto uyRange=coordinateRange(du,true),uzRange=coordinateRange(du,false);
        const auto vyRange=coordinateRange(dv,true),vzRange=coordinateRange(dv,false);
        const auto product=[](const auto& a,const auto& b){
            const std::array<double,4> values{a.first*b.first,a.first*b.second,a.second*b.first,a.second*b.second};
            const auto [lo,hi]=std::minmax_element(values.begin(),values.end());return std::pair{*lo,*hi};
        };
        const auto positive=product(uyRange,vzRange),negative=product(uzRange,vyRange);
        const int determinant=sign(positive.first-negative.second,positive.second-negative.first);if(!determinant)return false;
        const int uy=sign(uyRange.first,uyRange.second),uz=sign(uzRange.first,uzRange.second);
        const int vy=sign(vyRange.first,vyRange.second),vz=sign(vzRange.first,vzRange.second);
        // Non-rational spline derivatives are positive combinations of these
        // control differences (knot/degree factors are positive). Bilinearity
        // bounds every projected Jacobian by the pairwise cross products.
        // After coordinate sign/permutation, positive diagonal derivatives
        // and determinant prove univalence over this actual UV rectangle.
        // This is a control-net proof, without an analytical surface fit or
        // a sampled normal/sign tolerance.
        return (uy&&vz&&determinant==uy*vz)||(uz&&vy&&-determinant==uz*vy);
    }

    inline std::optional<int> ProjectionMultiplicityBound(const TopoDS_Face& face,double tolerance)
    {
        const auto source=BRep_Tool::Surface(face);
        GeomAdaptor_Surface native(source);
        if(native.GetType()==GeomAbs_BSplineSurface&&SplineProjectionIsInjective(face))return 1;
        double cylinderA,cylinderB;
        if(CylindricalProjectionCoefficients(face,cylinderA,cylinderB)) {
            double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
            if(cylinderA==0&&cylinderB==0)return std::nullopt; // projects to a curve
            const double pi=3.141592653589793238462643383279502884;
            const double zero=std::atan2(cylinderB,cylinderA)+pi/2;
            // A zero at a trim endpoint does not fold the patch interior.
            // Locate analytical roots directly; cos(pi/2) roundoff must not
            // turn an exact silhouette endpoint into a sign-change interval.
            for(double u=zero+pi*std::ceil((u0-zero)/pi);u<u1;u+=pi)if(u>u0)return 2;
            return 1;
        }
        auto surface=source;
        if(native.GetType()==GeomAbs_BSplineSurface||native.GetType()==GeomAbs_BezierSurface
            ||native.GetType()==GeomAbs_SurfaceOfRevolution){
            GeomConvert_SurfToAnaSurf conversion(source);
            surface=conversion.ConvertToAnalytical(tolerance);
            if(surface.IsNull()||conversion.Gap()>tolerance)return std::nullopt;
        }
        GeomAdaptor_Surface analytic(surface);
        // A line meets a quadric in at most two points, a torus in at most
        // four. These are geometry bounds, independent of tube dimensions.
        switch(analytic.GetType()){
        case GeomAbs_Plane:return 1;
        case GeomAbs_SurfaceOfExtrusion: {
            const auto extrusion=Handle(Geom_SurfaceOfLinearExtrusion)::DownCast(surface);
            if(extrusion.IsNull())return std::nullopt;
            const auto direction=extrusion->Direction();
            const gp_Vec transverse(0,-direction.Z(),direction.Y());
            if(transverse.SquareMagnitude()==0)return std::nullopt;
            double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
            Handle(Geom_BSplineCurve) basis;
            try {basis=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(extrusion->BasisCurve(),u0,u1));}
            catch(const Standard_Failure&) {return std::nullopt;}
            if(basis.IsNull())return std::nullopt;
            bool increasing=true,decreasing=true;
            const auto coordinate=[&](int i){return transverse.Dot(gp_Vec(gp_Pnt(0,0,0),basis->Pole(i)));};
            double previous=coordinate(1);const double first=previous;
            for(int i=1;i<=basis->NbPoles();++i) {
                if(basis->Weight(i)<=0)return std::nullopt;
                const double value=coordinate(i);
                increasing=increasing&&value>=previous;decreasing=decreasing&&value<=previous;previous=value;
            }
            // Positive rational spline bases are variation diminishing. A
            // monotone transverse control polygon proves that every X fiber
            // meets this trimmed ruled support at most once.
            if(previous!=first&&(increasing||decreasing))return 1;
            return std::nullopt;
        }
        case GeomAbs_Cylinder:
            if(native.GetType()==GeomAbs_Cylinder){
                double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
                const auto axes=native.Cylinder().Position();
                const double a=axes.XDirection().X(),b=axes.YDirection().X();
                const auto value=[&](double u){return a*std::cos(u)+b*std::sin(u);};
                double lo=std::min(value(u0),value(u1)),hi=std::max(value(u0),value(u1));
                const auto stationary=std::atan2(b,a);
                for(double u=stationary+3.141592653589793238462643383279502884*std::ceil((u0-stationary)/3.141592653589793238462643383279502884);u<=u1;u+=3.141592653589793238462643383279502884){
                    lo=std::min(lo,value(u));hi=std::max(hi,value(u));
                }
                // One side of a convex cylinder is a single-valued X graph.
                if(lo>=0||hi<=0)return 1;
            }
            return 2;
        case GeomAbs_Cone:case GeomAbs_Sphere:return 2;
        case GeomAbs_Torus:return 4;
        default:return std::nullopt;
        }
    }

    inline std::vector<TopoDS_Face> SingleValuedMaterialPatches(const TopoDS_Face& face,double tolerance)
    {
        if(const auto count=ProjectionMultiplicityBound(face,tolerance);count&&*count==1)return {face};
        const auto surface=BRep_Tool::Surface(face);
        double a,b;if(!CylindricalProjectionCoefficients(face,a,b))return {};
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        if(std::hypot(a,b)<=std::numeric_limits<double>::epsilon())return {};
        // n.X = a*cos(U)+b*sin(U). Split exactly at its zeros, so each
        // retained trimmed patch is a single-valued graph over the YZ plane.
        const double firstZero=std::atan2(b,a)+0.5*3.141592653589793238462643383279502884;
        std::vector<double> cuts{u0};
        for(double u=firstZero+3.141592653589793238462643383279502884*std::ceil((u0-firstZero)/3.141592653589793238462643383279502884);u<u1;u+=3.141592653589793238462643383279502884)
            if(u>u0+32*std::numeric_limits<double>::epsilon()*std::max(1.0,std::abs(u0)))cuts.push_back(u);
        cuts.push_back(u1);
        std::vector<TopoDS_Face> result;
        for(std::size_t i=1;i<cuts.size();++i) {
            if(cuts[i]-cuts[i-1]<=32*std::numeric_limits<double>::epsilon()*std::max(1.0,std::abs(cuts[i])))continue;
            const auto patch=BRepBuilderAPI_MakeFace(surface,cuts[i-1],cuts[i],v0,v1,Precision::Confusion()).Face();
            TopTools_ListOfShape arguments,tools;arguments.Append(face);tools.Append(patch);
            BRepAlgoAPI_Common trimmed;trimmed.SetArguments(arguments);trimmed.SetTools(tools);
            trimmed.SetNonDestructive(true);trimmed.Build();
            if(!trimmed.IsDone()||!BRepCheck_Analyzer(trimmed.Shape()).IsValid())
                throw std::runtime_error("Cannot split cylindrical material support at its exact silhouette");
            for(TopExp_Explorer it(trimmed.Shape(),TopAbs_FACE);it.More();it.Next())result.push_back(TopoDS::Face(it.Current()));
        }
        return result;
    }

}
