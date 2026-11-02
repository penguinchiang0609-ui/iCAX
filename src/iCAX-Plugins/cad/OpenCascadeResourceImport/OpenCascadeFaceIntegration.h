#pragma once
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <limits>
#include <optional>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace iCAX::OpenCascade::integration_detail
{
    inline std::vector<double> TrimIntervals(const Handle(Geom2d_Curve)& pc,double first,double last,
        const std::vector<double>& uKnots,const std::vector<double>& vKnots)
    {
        Geom2dAdaptor_Curve adaptor(pc,first,last);const int n=adaptor.NbIntervals(GeomAbs_CN);
        NCollection_Array1<double> native(1,n+1);adaptor.Intervals(native,GeomAbs_CN);
        std::vector<double> result;for(int i=1;i<=n+1;++i)result.push_back(native(i));
        const Handle(Geom2d_Curve) trim=new Geom2d_TrimmedCurve(pc,first,last);
        const auto crossings=[&](const std::vector<double>& knots,bool vertical){for(double knot:knots){
            const Handle(Geom2d_Curve) line=new Geom2d_Line(vertical?gp_Pnt2d(knot,0):gp_Pnt2d(0,knot),
                vertical?gp_Dir2d(0,1):gp_Dir2d(1,0));
            Geom2dAPI_InterCurveCurve intersections(trim,line,1e-12);
            for(int i=1;i<=intersections.NbPoints();++i){const auto t=intersections.Intersector().Point(i).ParamOnFirst();
                if(t>first&&t<last)result.push_back(t);}
        }};
        crossings(uKnots,true);crossings(vKnots,false);std::sort(result.begin(),result.end());
        result.erase(std::unique(result.begin(),result.end(),[](double a,double b){
            return std::abs(a-b)<=32*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(a),std::abs(b)});}),result.end());
        return result;
    }

    inline double Integrate(const std::function<double(double)>& function,double a,double b,int depth=0)
    {
        static constexpr double x16[]{.09501250983763744,.2816035507792589,.4580167776572274,.6178762444026438,
            .755404408355003,.8656312023878318,.9445750230732326,.9894009349916499};
        static constexpr double w16[]{.1894506104550685,.1826034150449236,.1691565193950025,.1495959888165767,
            .1246289712555339,.09515851168249278,.06225352393864789,.02715245941175409};
        static constexpr double x8[]{.1834346424956498,.525532409916329,.7966664774136267,.9602898564975363};
        static constexpr double w8[]{.362683783378362,.3137066458778873,.2223810344533745,.1012285362903763};
        const double middle=(a+b)/2,half=(b-a)/2;double fine=0,coarse=0;
        for(int i=0;i<8;++i)fine+=w16[i]*(function(middle-half*x16[i])+function(middle+half*x16[i]));
        for(int i=0;i<4;++i)coarse+=w8[i]*(function(middle-half*x8[i])+function(middle+half*x8[i]));
        fine*=half;coarse*=half;
        if(!std::isfinite(fine)||!std::isfinite(coarse))throw std::runtime_error("Non-finite surface area integral");
        if(std::abs(fine-coarse)<=1e-11*(1+std::abs(fine)))return fine;
        if(depth>=16)throw std::runtime_error("Surface area quadrature did not converge");
        return Integrate(function,a,middle,depth+1)+Integrate(function,middle,b,depth+1);
    }

    // Independent trim-domain integration for representation changes where
    // OCCT's surface-type-dependent integration orders disagree. Green's
    // theorem reduces integral J du dv to the original oriented trim curves:
    // integral (integral[u0,u] |Su x Sv| ds) dv. Inner wires subtract holes.
    inline double RefinedIntegral(const TopoDS_Face& source,const gp_Pnt* volumeOrigin=nullptr)
    {
        const auto face=TopoDS::Face(source.Oriented(TopAbs_FORWARD));
        const auto surface=BRep_Tool::Surface(face);double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        std::vector<double> knots,vKnots;
        GeomAdaptor_Surface support(surface,u0,u1,v0,v1);
        const int nu=support.NbUIntervals(GeomAbs_CN),nv=support.NbVIntervals(GeomAbs_CN);
        NCollection_Array1<double> uk(1,nu+1),vk(1,nv+1);support.UIntervals(uk,GeomAbs_CN);support.VIntervals(vk,GeomAbs_CN);
        for(int i=1;i<=nu+1;++i)if(uk(i)>u0&&uk(i)<u1)knots.push_back(uk(i));
        for(int i=1;i<=nv+1;++i)if(vk(i)>v0&&vk(i)<v1)vKnots.push_back(vk(i));
        // Planar spline parameterizations can be highly nonuniform. Integrate
        // their physical boundary directly instead of nesting two quadratures.
        // Only a plane within coordinate roundoff qualifies for this identity.
        const auto center=surface->Value((u0+u1)/2,(v0+v1)/2);
        const double scale=std::max({1.,std::abs(center.X()),std::abs(center.Y()),std::abs(center.Z())});
        const GeomLib_IsPlanarSurface planar(surface,256*std::numeric_limits<double>::epsilon()*scale);
        if(planar.IsPlanar()) {
            const gp_Vec normal(planar.Plan().Axis().Direction());double orientedArea=0;
            for(TopExp_Explorer wire(face,TopAbs_WIRE);wire.More();wire.Next())
                for(BRepTools_WireExplorer edge(TopoDS::Wire(wire.Current()),face);edge.More();edge.Next()) {
                    const auto native=edge.Current();double first,last;const auto pc=BRep_Tool::CurveOnSurface(native,face,first,last);
                    if(pc.IsNull())throw std::runtime_error("Area trim has no parameter curve");
                    const auto intervals=TrimIntervals(pc,first,last,knots,vKnots);
                    const auto integrand=[&](double t){gp_Pnt2d uv;gp_Vec2d tangent;pc->D1(t,uv,tangent);
                        gp_Pnt p;gp_Vec du,dv;surface->D1(uv.X(),uv.Y(),p,du,dv);
                        return gp_Vec(center,p).Crossed(du*tangent.X()+dv*tangent.Y()).Dot(normal)/2;};
                    for(std::size_t i=1;i<intervals.size();++i)orientedArea+=(native.Orientation()==TopAbs_REVERSED?-1:1)
                        *Integrate(integrand,intervals[i-1],intervals[i]);
                }
            return volumeOrigin?orientedArea*gp_Vec(*volumeOrigin,center).Dot(normal)/3
                *(source.Orientation()==TopAbs_REVERSED?-1:1):std::abs(orientedArea);
        }
        double area=0;
        for(TopExp_Explorer wire(face,TopAbs_WIRE);wire.More();wire.Next())
            for(BRepTools_WireExplorer edge(TopoDS::Wire(wire.Current()),face);edge.More();edge.Next()) {
                const auto native=edge.Current();double first,last;const auto pc=BRep_Tool::CurveOnSurface(native,face,first,last);
                if(pc.IsNull())throw std::runtime_error("Area trim has no parameter curve");
                const auto integrand=[&](double t){
                    gp_Pnt2d uv;gp_Vec2d derivative;pc->D1(t,uv,derivative);
                    if(std::abs(derivative.Y())<1e-25)return 0.;
                    const auto jacobian=[&](double u){gp_Pnt p;gp_Vec du,dv;surface->D1(u,uv.Y(),p,du,dv);
                        const auto normal=du.Crossed(dv);
                        return volumeOrigin?gp_Vec(*volumeOrigin,p).Dot(normal)/3:normal.Magnitude();};
                    std::vector<double> intervals{u0,uv.X()};const double a=std::min(u0,uv.X()),b=std::max(u0,uv.X());
                    for(double knot:knots)if(knot>a&&knot<b)intervals.push_back(knot);
                    std::sort(intervals.begin(),intervals.end());double value=0;
                    for(std::size_t i=1;i<intervals.size();++i)value+=Integrate(jacobian,intervals[i-1],intervals[i]);
                    return value*(uv.X()<u0?-1:1)*derivative.Y();
                };
                const auto intervals=TrimIntervals(pc,first,last,knots,vKnots);
                for(std::size_t i=1;i<intervals.size();++i)area+=(native.Orientation()==TopAbs_REVERSED?-1:1)
                    *Integrate(integrand,intervals[i-1],intervals[i]);
            }
        return volumeOrigin?area*(source.Orientation()==TopAbs_REVERSED?-1:1):std::abs(area);
    }
    inline double RefinedArea(const TopoDS_Face& face){return RefinedIntegral(face);}
    inline double RefinedVolume(const TopoDS_Shape& shape,const gp_Pnt& origin)
    {
        double volume=0;TopTools_IndexedMapOfShape solids;TopExp::MapShapes(shape,TopAbs_SOLID,solids);
        for(int i=1;i<=solids.Extent();++i)for(TopExp_Explorer face(solids(i),TopAbs_FACE);face.More();face.Next())
            volume+=RefinedIntegral(TopoDS::Face(face.Current()),&origin);
        return volume;
    }

}
