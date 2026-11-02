#pragma once
#include "CurveOffset3DService.h"
#include "OffsetCurveParameters.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <stdexcept>

namespace iCAX::GeometryServices::OffsetTraversal
{
using namespace GeometryData;
inline ParameterRange& Range(OffsetCurvePiece3& p){if(auto* l=std::get_if<DirectionalCurveOffset3>(&p.Geometry))return l->Source.Range;return std::get<CurveSegment3>(p.Geometry).Range;}
inline const ParameterRange& Range(const OffsetCurvePiece3& p){if(auto* l=std::get_if<DirectionalCurveOffset3>(&p.Geometry))return l->Source.Range;return std::get<CurveSegment3>(p.Geometry).Range;}
inline double Start(const OffsetCurvePiece3& p){const auto r=Range(p);return r.Reversed?r.Last:r.First;}
inline double End(const OffsetCurvePiece3& p){const auto r=Range(p);return r.Reversed?r.First:r.Last;}
template<class A,class B> double Distance(const A& a,const B& b){return std::hypot(a.X-b.X,a.Y-b.Y,a.Z-b.Z);}
struct Sample {std::size_t PieceIndex=0;CurveOffsetSample3 Value;};
struct Samples {std::vector<Sample> Points;std::vector<double> Lengths;};
struct Location {std::size_t PieceIndex=0;double Parameter=0;};

inline std::vector<double> SmoothParameters(const OffsetCurvePiece3& piece,ParameterRange range)
{
    if(!std::isfinite(range.First)||!std::isfinite(range.Last)||range.First>=range.Last)throw std::invalid_argument("OFFSET_TRAVERSAL_RANGE");
    std::vector<double> values{range.First,range.Last};
    const auto append=[&](const Curve3& curve,int continuity){std::visit([&](const auto& c){OffsetParameters::AppendKnots(c,range,continuity,values,65536);},curve);};
    if(const auto* law=std::get_if<DirectionalCurveOffset3>(&piece.Geometry)){
        append(law->Source.Curve,2);
        if(const auto* field=std::get_if<ParametricVectorField3>(&law->ReferenceDirection))std::visit([&](const auto& c){append(Curve3(c),1);},field->Coefficients);
    }else append(std::get<CurveSegment3>(piece.Geometry).Curve,1);
    std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end()),values.end());return values;
}

// Neutral numerical traversal over retained exact expressions. Samples are only
// display/motion input; trimming changes native domains, never fits a polyline.
class Evaluator
{
    const ICurveOffset3DService& Service;
    CurveOffsetEvaluationOptions Options;
    std::size_t Count=0;
public:
    explicit Evaluator(const ICurveOffset3DService& service,std::function<bool()> cancelled={}):Service(service){Options.IsCancelled=std::move(cancelled);}
    std::vector<CurveOffsetSample3> AtMany(const OffsetCurvePiece3& p,const std::vector<double>& parameters)
    {
        if(parameters.empty())return {};
        const auto range=Range(p);
        if(OffsetParameters::NeedsIndependentQuadrature(p)||SmoothParameters(p,range).size()>2)
        {std::vector<CurveOffsetSample3> result;for(double u:parameters)result.push_back(At(p,u));return result;}
        if(Count>1000000||parameters.size()>1000000-Count)throw std::runtime_error("OFFSET_TRAVERSAL_BUDGET");Count+=parameters.size();
        auto native=parameters;const double roundoff=8*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(range.First),std::abs(range.Last)});
        for(auto& u:native){if(u<range.First&&range.First-u<=roundoff)u=range.First;if(u>range.Last&&u-range.Last<=roundoff)u=range.Last;}
        const auto r=Service.EvaluatePiece(p,native,Options);
        if(r.Status!=ECurveOffsetStatus::SamplesEvaluated)throw std::runtime_error("OFFSET_TRAVERSAL: "+r.Diagnostic);
        return r.Samples;
    }
    CurveOffsetSample3 At(const OffsetCurvePiece3& p,double u)
    {
        if(++Count>1000000)throw std::runtime_error("OFFSET_TRAVERSAL_BUDGET");
        // Subdivision arithmetic can overshoot an exact endpoint by one ULP.
        // Snap only roundoff here; the service still rejects real extrapolation.
        const auto range=Range(p);const double roundoff=8*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(range.First),std::abs(range.Last)});
        if(u<range.First&&range.First-u<=roundoff)u=range.First;
        if(u>range.Last&&u-range.Last<=roundoff)u=range.Last;
        const auto bounds=SmoothParameters(p,range);const OffsetCurvePiece3* evaluated=&p;OffsetCurvePiece3 local;
        if(bounds.size()>2){
            auto index=static_cast<std::size_t>(std::upper_bound(bounds.begin(),bounds.end(),u)-bounds.begin());index=index?index-1:0;index=std::min(index,bounds.size()-2);
            if(range.Reversed&&index&&u==bounds[index])--index;
            local=p;Range(local)={bounds[index],bounds[index+1],range.Reversed};evaluated=&local;
        }
        const auto r=Service.EvaluatePiece(*evaluated,{u},Options);
        if(r.Status!=ECurveOffsetStatus::SamplesEvaluated)throw std::runtime_error("OFFSET_TRAVERSAL: "+r.Diagnostic);
        return r.Samples.front();
    }
    class Piece
    {
        Evaluator& Owner;
        const OffsetCurvePiece3& Curve;
        std::map<double,CurveOffsetSample3> Cache;
        void Prime(const std::vector<double>& parameters)
        {
            std::vector<double> missing;for(double u:parameters)if(!Cache.contains(u)&&std::find(missing.begin(),missing.end(),u)==missing.end())missing.push_back(u);
            if(missing.size()<2)return;const auto samples=Owner.AtMany(Curve,missing);
            for(std::size_t i=0;i<missing.size();++i)Cache.emplace(missing[i],samples.at(i));
        }
        double Speed(double u){const auto d=At(u).OffsetDerivative;return std::hypot(d.X,d.Y,d.Z);}
        double Integrate(double a,double b,double fa,double fm,double fb,double whole,double tolerance,int depth)
        {
            const double m=(a+b)/2,l=(a+m)/2,r=(m+b)/2;Prime({l,r});const double fl=Speed(l),fr=Speed(r);
            const double left=(m-a)*(fa+4*fl+fm)/6,right=(b-m)*(fm+4*fr+fb)/6,error=left+right-whole;
            if(std::abs(error)<=15*tolerance)return left+right+error/15;
            if(depth>=24)throw std::runtime_error("OFFSET_LENGTH_UNRESOLVED");
            return Integrate(a,m,fa,fl,fm,left,tolerance/2,depth+1)+Integrate(m,b,fm,fr,fb,right,tolerance/2,depth+1);
        }
        void Subdivide(double a,double b,double chordTolerance,double step,int depth,std::vector<CurveOffsetSample3>& out)
        {
            Prime({a,b,(a+b)/2});
            const auto x=At(a),y=At(b);const double m=(a+b)/2;const auto center=At(m);
            const auto deviation=[&](const auto& p){const auto& v=x.OffsetPoint;const auto& w=y.OffsetPoint;
                double dx=w.X-v.X,dy=w.Y-v.Y,dz=w.Z-v.Z,n=dx*dx+dy*dy+dz*dz;
                double t=n?std::clamp(((p.X-v.X)*dx+(p.Y-v.Y)*dy+(p.Z-v.Z)*dz)/n,0.,1.):0.;
                return std::hypot(p.X-v.X-t*dx,p.Y-v.Y-t*dy,p.Z-v.Z-t*dz);};
            bool split=Distance(x.OffsetPoint,y.OffsetPoint)>step;
            if(!split){Prime({(3*a+b)/4,(a+3*b)/4});split=std::max({deviation(center.OffsetPoint),deviation(At((3*a+b)/4).OffsetPoint),deviation(At((a+3*b)/4).OffsetPoint)})>chordTolerance;}
            const auto& v=x.OffsetPoint;const auto& w=y.OffsetPoint;
            for(const auto& s:{x,center,y}){const auto d=s.OffsetDerivative;if((d.X*(w.X-v.X)+d.Y*(w.Y-v.Y)+d.Z*(w.Z-v.Z))*(b-a)<0)split=true;}
            if(split){if(depth>=24)throw std::runtime_error("OFFSET_SAMPLING_UNRESOLVED");Subdivide(a,m,chordTolerance,step,depth+1,out);Subdivide(m,b,chordTolerance,step,depth+1,out);}else out.push_back(y);
        }
    public:
        Piece(Evaluator& owner,const OffsetCurvePiece3& curve):Owner(owner),Curve(curve){}
        const CurveOffsetSample3& At(double u){auto it=Cache.find(u);if(it==Cache.end())it=Cache.emplace(u,Owner.At(Curve,u)).first;return it->second;}
        double Length(double a,double b)
        {
            if(a==b)return 0;if(a>b)std::swap(a,b);double length=0;
            const auto domain=Range(Curve);const double roundoff=8*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(domain.First),std::abs(domain.Last)});
            if(a<domain.First-roundoff||b>domain.Last+roundoff)throw std::invalid_argument("OFFSET_TRAVERSAL_OUTSIDE_RANGE");a=std::max(a,domain.First);b=std::min(b,domain.Last);
            const auto bounds=SmoothParameters(Curve,{a,b,false});
            if(bounds.size()>2){for(std::size_t i=1;i<bounds.size();++i){auto smooth=Curve;Range(smooth)={bounds[i-1],bounds[i],false};Piece local(Owner,smooth);length+=local.Length(bounds[i-1],bounds[i]);}return length;}
            if(!OffsetParameters::NeedsIndependentQuadrature(Curve))
            {
                std::vector<double> parameters;parameters.reserve(80);
                for(int i=0;i<16;++i){const double x=a+(b-a)*i/16,y=a+(b-a)*(i+1)/16,m=(x+y)/2;parameters.insert(parameters.end(),{x,m,y,(x+m)/2,(m+y)/2});}
                Prime(parameters);
            }
            for(int i=0;i<16;++i){double x=a+(b-a)*i/16,y=a+(b-a)*(i+1)/16,m=(x+y)/2;Prime({x,m,y});const double fa=Speed(x),fm=Speed(m),fb=Speed(y),whole=(y-x)*(fa+4*fm+fb)/6;
                length+=Integrate(x,y,fa,fm,fb,whole,1e-9+1e-10*std::abs(whole),0);}
            return length;
        }
        std::vector<CurveOffsetSample3> Sample(double tolerance,double step)
        {
            if(!std::isfinite(tolerance)||tolerance<=0||!std::isfinite(step)||step<=0)throw std::invalid_argument("OFFSET_SAMPLING_PARAMETERS");
            const auto range=Range(Curve);const auto bounds=SmoothParameters(Curve,range);
            if(bounds.size()>2){std::vector<CurveOffsetSample3> out;for(std::size_t i=0;i+1<bounds.size();++i){const auto at=range.Reversed?bounds.size()-2-i:i;auto smooth=Curve;Range(smooth)={bounds[at],bounds[at+1],range.Reversed};Piece local(Owner,smooth);auto samples=local.Sample(tolerance,step);out.insert(out.end(),samples.begin(),samples.end());}return out;}
            const auto a=Start(Curve),b=End(Curve);
            if(!OffsetParameters::NeedsIndependentQuadrature(Curve))
            {
                // These are exactly the mandatory first-level stations. Batch
                // their evaluation without adding stations or changing refinement.
                std::vector<double> parameters;parameters.reserve(48);
                for(int i=0;i<16;++i){const double x=a+(b-a)*i/16,y=a+(b-a)*(i+1)/16;parameters.insert(parameters.end(),{x,y,(x+y)/2});}
                Prime(parameters);parameters.clear();
                for(int i=0;i<16;++i){const double x=a+(b-a)*i/16,y=a+(b-a)*(i+1)/16;if(Distance(At(x).OffsetPoint,At(y).OffsetPoint)<=step)parameters.insert(parameters.end(),{(3*x+y)/4,(x+3*y)/4});}
                Prime(parameters);
            }
            std::vector<CurveOffsetSample3> out{At(a)};
            for(int i=0;i<16;++i)Subdivide(a+(b-a)*i/16,a+(b-a)*(i+1)/16,tolerance,step,0,out);return out;
        }
        double Advance(double a,double b,double distance)
        {
            if(distance<=1e-10)return a;double lo=0,hi=1;
            for(int i=0;i<48;++i){double t=(lo+hi)/2,u=a+(b-a)*t;if(Length(a,u)<distance)lo=t;else hi=t;}
            return a+(b-a)*(lo+hi)/2;
        }
    };
    Samples Sample(const CompositeOffsetCurve3& curve,double tolerance=.02,double step=2.)
    {return CollectSamples(curve,tolerance,step,true);}
    // Display and support checks consume only these exact samples. Physical
    // length integration remains available to tracing and execution callers.
    Samples SamplePoints(const CompositeOffsetCurve3& curve,double tolerance=.02,double step=2.)
    {return CollectSamples(curve,tolerance,step,false);}
private:
    Samples CollectSamples(const CompositeOffsetCurve3& curve,double tolerance,double step,bool measureLengths)
    {
        Samples out;
        for(std::size_t i=0;i<curve.Segments.size();++i){Piece p(*this,curve.Segments[i]);auto samples=p.Sample(tolerance,step);
            if(!out.Points.empty()&&Distance(out.Points.back().Value.OffsetPoint,samples.front().OffsetPoint)>1.1e-5)throw std::runtime_error("OFFSET_CURVE_DISCONNECTED");
            for(auto& s:samples)out.Points.push_back({i,std::move(s)});if(measureLengths)out.Lengths.push_back(p.Length(Start(curve.Segments[i]),End(curve.Segments[i])));}
        if(curve.Closed&&!out.Points.empty()&&Distance(out.Points.front().Value.OffsetPoint,out.Points.back().Value.OffsetPoint)>1.1e-5)throw std::runtime_error("OFFSET_CURVE_NOT_CLOSED");return out;
    }
public:
    double Length(const CompositeOffsetCurve3& curve)
    {
        double n=0;for(const auto& s:curve.Segments){Piece p(*this,s);n+=p.Length(Start(s),End(s));}return n;
    }
    CompositeOffsetCurve3 Trace(const CompositeOffsetCurve3& curve,Location start,double distance,bool reverse)
    {
        if(curve.Segments.empty()||start.PieceIndex>=curve.Segments.size()||!std::isfinite(distance)||distance<=0)throw std::invalid_argument("OFFSET_TRACE_PARAMETERS");
        auto pieces=curve.Segments;std::size_t index=start.PieceIndex;
        if(reverse){std::reverse(pieces.begin(),pieces.end());index=pieces.size()-1-index;for(auto& s:pieces)Range(s).Reversed=!Range(s).Reversed;}
        const auto initialRange=Range(pieces[index]);if(start.Parameter<initialRange.First||start.Parameter>initialRange.Last)throw std::invalid_argument("OFFSET_TRACE_START");
        CompositeOffsetCurve3 out;double at=start.Parameter,remaining=distance;
        for(std::size_t visits=0;remaining>1e-8;++visits){
            if(visits>65536)throw std::runtime_error("OFFSET_TRACE_BUDGET");auto s=pieces[index];const auto end=End(s);Piece p(*this,s);double length=p.Length(at,end);
            if(length>1e-10){double to=remaining<length-1e-8?p.Advance(at,end,remaining):end;auto& r=Range(s);r.First=std::min(at,to);r.Last=std::max(at,to);out.Segments.push_back(std::move(s));remaining-=std::min(remaining,length);}
            if(remaining<=1e-8)break;
            if(++index==pieces.size()){if(!curve.Closed)throw std::invalid_argument("OFFSET_TRACE_EXCEEDS_OPEN_CURVE");index=0;}at=Start(pieces[index]);
        }
        return out;
    }
};
}
