#include "CompositeCurveOffset3D.h"
#include "OffsetCurveParameters.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <stdexcept>

namespace iCAX::GeometryServices
{
namespace
{
    using namespace GeometryData;
    using Status=ECompositeOffsetStatus3;
    struct Failure { Status Code; std::string Message; };
    struct V {
        double x=0,y=0,z=0;
        V operator+(V b) const{return {x+b.x,y+b.y,z+b.z};}
        V operator-(V b) const{return {x-b.x,y-b.y,z-b.z};}
        V operator*(double s) const{return {x*s,y*s,z*s};}
        double Dot(V b) const{return x*b.x+y*b.y+z*b.z;}
        V Cross(V b) const{return {y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x};}
        double Norm() const{return std::hypot(x,y,z);}
        V Unit() const {const auto n=Norm();if(n<1e-14)throw Failure{Status::EvaluationFailed,"Undefined tangent or connector plane"};return *this*(1/n);}
    };
    template<class T> V Vec(const T& p){return {p.X,p.Y,p.Z};}
    Point3 Point(V p){return {p.x,p.y,p.z};}
    Direction3 Dir(V p){return {p.x,p.y,p.z};}
    ParameterRange& Range(OffsetCurvePiece3& p){if(auto* l=std::get_if<DirectionalCurveOffset3>(&p.Geometry))return l->Source.Range;return std::get<CurveSegment3>(p.Geometry).Range;}
    const ParameterRange& Range(const OffsetCurvePiece3& p){if(const auto* l=std::get_if<DirectionalCurveOffset3>(&p.Geometry))return l->Source.Range;return std::get<CurveSegment3>(p.Geometry).Range;}
    double Start(const OffsetCurvePiece3& p){const auto r=Range(p);return r.Reversed?r.Last:r.First;}
    double End(const OffsetCurvePiece3& p){const auto r=Range(p);return r.Reversed?r.First:r.Last;}
    double Fraction(const OffsetCurvePiece3& p,double u){return (u-Start(p))/(End(p)-Start(p));}
    bool Trim(OffsetCurvePiece3& p,double a,double b){auto& r=Range(p);if(std::abs(a-b)<1e-13*std::max(1.,r.Last-r.First))return false;r.First=std::min(a,b);r.Last=std::max(a,b);return true;}
    struct Node {double u;V p,t;};
    struct Chord {std::size_t piece,span;Node a,b;V low,high;double firstS=0,lastS=0;};
    struct Hit {std::size_t a,b;double u,v;};
    struct Closest {double a=0,b=0,distance=0;};
    Closest Lines(V p,V q,V r,V s)
    {
        const V u=q-p,v=s-r,w=p-r;const double a=u.Dot(u),b=u.Dot(v),c=v.Dot(v),d=u.Dot(w),e=v.Dot(w);
        double x=0,y=0,den=a*c-b*b;
        if(a<1e-28&&c<1e-28)return {0,0,w.Norm()};
        if(a<1e-28)y=std::clamp(e/c,0.,1.);
        else if(c<1e-28)x=std::clamp(-d/a,0.,1.);
        else {x=den>1e-14*a*c?std::clamp((b*e-c*d)/den,0.,1.):0;y=(b*x+e)/c;
            if(y<0){y=0;x=std::clamp(-d/a,0.,1.);}else if(y>1){y=1;x=std::clamp((b-d)/a,0.,1.);}}
        return {x,y,(w+u*x-v*y).Norm()};
    }
    class Builder
    {
        const ICurveOffset3DService& Service;
        const CompositeOffsetOptions3& Options;
        std::size_t Evaluations=0,Pairs=0;
        CompositeOffsetResult3 Result;
        std::unique_ptr<IPlanarOffsetEvaluator3> Planar;
        using Cache=std::map<double,Node>;
        Node SampleNode(double u,const CurveOffsetSample3& s) const{return {u,Vec(s.OffsetPoint),Vec(s.OffsetDerivative)};}
        void Check(){if(Options.IsCancelled&&Options.IsCancelled())throw Failure{Status::Cancelled,"Composite offset cancelled"};}
        CurveOffsetEvaluation3 Evaluate(const OffsetCurvePiece3& piece,const std::vector<double>& parameters,bool spatial=false)
        {
            Check();if(parameters.size()>Options.MaxSamples-Evaluations)throw Failure{Status::BudgetExceeded,"Composite offset evaluation budget exceeded"};
            Evaluations+=parameters.size();
            CurveOffsetEvaluationOptions options;options.IsCancelled=Options.IsCancelled;
            options.IntegrationTolerance=std::min(options.IntegrationTolerance,Options.Tolerance*.01);
            const bool prepared=!spatial&&Planar&&piece.SourceSegment&&std::holds_alternative<DirectionalCurveOffset3>(piece.Geometry);
            auto r=prepared?Planar->Evaluate(piece,parameters,options):Service.EvaluatePiece(piece,parameters,options);
            if(r.Status!=ECurveOffsetStatus::SamplesEvaluated)throw Failure{r.Status==ECurveOffsetStatus::Cancelled?Status::Cancelled:r.Status==ECurveOffsetStatus::BudgetExceeded?Status::BudgetExceeded:Status::EvaluationFailed,r.Diagnostic};
            if(Planar&&!prepared&&!spatial)for(auto& sample:r.Samples){
                sample.SourcePoint=Planar->ToLocal(sample.SourcePoint);sample.OffsetPoint=Planar->ToLocal(sample.OffsetPoint);
                sample.SourceDerivative=Planar->ToLocal(sample.SourceDerivative);sample.OffsetDerivative=Planar->ToLocal(sample.OffsetDerivative);}
            return r;
        }
        Node Eval(const OffsetCurvePiece3& piece,double u)
        {
            const auto r=Evaluate(piece,{u});
            return SampleNode(u,r.Samples.front());
        }
        Node Spatial(const OffsetCurvePiece3& piece,double u)
        {const auto r=Evaluate(piece,{u},true);return SampleNode(u,r.Samples.front());}
        Node Cached(const OffsetCurvePiece3& piece,double u,Cache& cache){Check();auto it=cache.find(u);if(it!=cache.end())return it->second;return cache.emplace(u,Eval(piece,u)).first->second;}
        void Prime(const OffsetCurvePiece3& piece,const std::vector<double>& parameters,Cache& cache)
        {
            // Reuse one exact curve construction for this subdivision's missing
            // samples. Keep per-call quadrature budgets for clothoid integration.
            if(OffsetParameters::NeedsIndependentQuadrature(piece))return;
            std::vector<double> missing;for(double u:parameters)if(!cache.contains(u)&&std::find(missing.begin(),missing.end(),u)==missing.end())missing.push_back(u);
            if(missing.size()<2)return;
            for(std::size_t begin=0;begin<missing.size();begin+=512)
            {
                const auto end=std::min(begin+512,missing.size());const auto evaluated=Evaluate(piece,std::vector<double>(missing.begin()+begin,missing.begin()+end));
                for(std::size_t i=begin;i<end;++i)cache.emplace(missing[i],SampleNode(missing[i],evaluated.Samples.at(i-begin)));
            }
        }
        void Subdivide(const OffsetCurvePiece3& p,const Node& a,const Node& b,int depth,Cache& cache,std::vector<Node>& nodes)
        {
            Prime(p,{(a.u+b.u)*.5,(a.u*3+b.u)*.25,(a.u+b.u*3)*.25},cache);
            const auto m=Cached(p,(a.u+b.u)*.5,cache),l=Cached(p,(a.u*3+b.u)*.25,cache),r=Cached(p,(a.u+b.u*3)*.25,cache);
            const auto deviation=[&](const Node& n){return Lines(a.p,b.p,n.p,n.p).distance;};
            const auto chord=b.p-a.p;const double length=chord.Norm();
            bool split=std::max({deviation(l),deviation(m),deviation(r)})>Options.Tolerance*.2;
            // Derivative checks catch folds even if interpolation nodes happen
            // to lie on the chord. Input knot intervals are split separately.
            if(length>Options.Tolerance)for(const auto& n:{a,l,m,r,b})
                if(n.t.Unit().Dot(chord.Unit())*(b.u>a.u?1:-1)<.95)split=true;
            if(split){if(depth>=24)throw Failure{Status::IntersectionUnresolved,"Offset requires finer subdivision or has a cusp"};Subdivide(p,a,m,depth+1,cache,nodes);Subdivide(p,m,b,depth+1,cache,nodes);}
            else nodes.push_back(b);
        }
        std::vector<Node> Sample(const OffsetCurvePiece3& piece,Cache* retained=nullptr)
        {
            const auto range=Range(piece);std::vector<double> seeds{range.First,range.Last};
            std::size_t minimum=16;
            const auto add=[&](const auto& c){using T=std::decay_t<decltype(c)>;
                const auto before=seeds.size();
                OffsetParameters::AppendKnots(c,range,-1,seeds,Options.MaxSamples);
                if constexpr(std::is_same_v<T,Bezier3>)minimum=std::max(minimum,c.Poles.size()*2);
                if constexpr(std::is_same_v<T,BSpline3>||std::is_same_v<T,NURBS3>)
                {
                    // A trimmed span depends on at most degree+1 poles. Using
                    // every pole of the full periodic curve for every tiny
                    // retained span multiplies mandatory work by the cut count.
                    // This is an upper bound on all active bases in the range;
                    // the same adaptive distance/derivative checks still follow.
                    const auto active=std::min(c.Poles.size(),(static_cast<std::size_t>(c.Degree)+1)*(seeds.size()-before+1));
                    minimum=std::max(minimum,active*2);
                }
                if constexpr(std::is_same_v<T,Clothoid3>) {
                    const double turns=std::ceil((range.Last-range.First)*std::max(std::abs(c.StartCurvature),std::abs(c.EndCurvature))/(std::numbers::pi*.25));
                    if(!std::isfinite(turns)||turns>Options.MaxSamples)throw Failure{Status::BudgetExceeded,"Clothoid sampling budget exceeded"};
                    minimum=std::max(minimum,static_cast<std::size_t>(std::max(0.,turns)));
                }};
            if(const auto* l=std::get_if<DirectionalCurveOffset3>(&piece.Geometry)){
                std::visit(add,l->Source.Curve);if(const auto* f=std::get_if<ParametricVectorField3>(&l->ReferenceDirection))std::visit(add,f->Coefficients);
            }else std::visit(add,std::get<CurveSegment3>(piece.Geometry).Curve);
            if(minimum>Options.MaxSamples)throw Failure{Status::BudgetExceeded,"Curve complexity exceeds sample budget"};
            for(std::size_t i=1;i<minimum;++i)seeds.push_back(range.First+(range.Last-range.First)*i/minimum);
            std::sort(seeds.begin(),seeds.end());seeds.erase(std::unique(seeds.begin(),seeds.end()),seeds.end());if(range.Reversed)std::reverse(seeds.begin(),seeds.end());
            Cache owned;auto& cache=retained?*retained:owned;
            if(!OffsetParameters::NeedsIndependentQuadrature(piece))
            {
                std::vector<double> parameters=seeds;
                for(std::size_t i=1;i<seeds.size();++i){const double a=seeds[i-1],b=seeds[i];parameters.insert(parameters.end(),{(a+b)*.5,(a*3+b)*.25,(a+b*3)*.25});}
                Prime(piece,parameters,cache);
            }
            std::vector<Node> nodes{Cached(piece,seeds.front(),cache)};
            for(std::size_t i=1;i<seeds.size();++i)Subdivide(piece,nodes.back(),Cached(piece,seeds[i],cache),0,cache,nodes);
            return nodes;
        }
        std::optional<Hit> Refine(const std::vector<OffsetCurvePiece3>& pieces,const Chord& a,const Chord& b,const Closest& seed,std::vector<Cache>& caches)
        {
            double u=a.a.u+(a.b.u-a.a.u)*seed.a,v=b.a.u+(b.b.u-b.a.u)*seed.b;
            const double ua=std::min(a.a.u,a.b.u),ub=std::max(a.a.u,a.b.u),va=std::min(b.a.u,b.b.u),vb=std::max(b.a.u,b.b.u);
            const auto at=[&](std::size_t index,double parameter){return Cached(pieces[index],parameter,caches[index]);};
            for(int n=0;n<30;++n){const auto x=at(a.piece,u),y=at(b.piece,v);const V w=x.p-y.p;
                if(w.Norm()<Options.Tolerance*.05)return Hit{a.piece,b.piece,u,v};
                const double aa=x.t.Dot(x.t),bb=x.t.Dot(y.t),cc=y.t.Dot(y.t),d=x.t.Dot(w),e=y.t.Dot(w),den=aa*cc-bb*bb;
                double du=0,dv=0;
                if(den>1e-14*aa*cc){du=(bb*e-cc*d)/den;dv=(aa*e-bb*d)/den;}
                else {du=-d/aa;dv=e/cc;}
                bool moved=false;
                for(double step=1;step>=1./128;step*=.5){const double un=std::clamp(u+du*step,ua,ub),vn=std::clamp(v+dv*step,va,vb);
                    if((at(a.piece,un).p-at(b.piece,vn).p).Norm()<w.Norm()){u=un;v=vn;moved=true;break;}}
                if(!moved)break;
            }
            if((at(a.piece,u).p-at(b.piece,v).p).Norm()<=Options.Tolerance)return Hit{a.piece,b.piece,u,v};
            return {};
        }
        std::vector<Hit> Intersections(const std::vector<OffsetCurvePiece3>& pieces,bool closed,bool joint=false)
        {
            std::vector<Chord> chords;double totalLength=0;
            // Sampling, intersection refinement and adjacency checks share
            // immutable pieces within this call. Retain their exact evaluations.
            std::vector<Cache> caches(pieces.size());
            for(std::size_t i=0;i<pieces.size();++i){auto nodes=Sample(pieces[i],&caches[i]);
                for(std::size_t n=1;n<nodes.size();++n){const auto a=nodes[n-1],b=nodes[n];const double pad=Options.Tolerance*1.5;
                    const double startLength=totalLength;totalLength+=(b.p-a.p).Norm();
                    chords.push_back({i,n-1,a,b,{std::min(a.p.x,b.p.x)-pad,std::min(a.p.y,b.p.y)-pad,std::min(a.p.z,b.p.z)-pad},
                        {std::max(a.p.x,b.p.x)+pad,std::max(a.p.y,b.p.y)+pad,std::max(a.p.z,b.p.z)+pad},startLength,totalLength});}}
            std::sort(chords.begin(),chords.end(),[](auto& a,auto& b){return a.low.x<b.low.x;});std::vector<Hit> hits;
            for(std::size_t i=0;i<chords.size();++i)for(std::size_t j=i+1;j<chords.size()&&chords[j].low.x<=chords[i].high.x;++j){
                Check();if(++Pairs>Options.MaxIntersectionPairs)throw Failure{Status::BudgetExceeded,"Intersection candidate budget exceeded"};
                const auto& a=chords[i];const auto& b=chords[j];
                if(a.high.y<b.low.y||b.high.y<a.low.y||a.high.z<b.low.z||b.high.z<a.low.z)continue;
                if(joint&&a.piece==b.piece)continue;
                if(a.piece==b.piece&&(a.span==b.span||a.span+1==b.span||b.span+1==a.span))continue;
                // A local neighbourhood of a continuous curve is necessarily
                // within distance tolerance of itself. It is not a loop.
                // Arc separation, rather than a fixed sample-index exclusion,
                // also handles very short corner connectors and closed seams.
                if(!joint){const double gap=std::max({0.,a.firstS-b.lastS,b.firstS-a.lastS});
                    const double wrap=totalLength-std::max(a.lastS,b.lastS)+std::min(a.firstS,b.firstS);
                    if(gap<=Options.Tolerance*2||(closed&&wrap<=Options.Tolerance*2))continue;}
                const auto seed=Lines(a.a.p,a.b.p,b.a.p,b.b.p);if(seed.distance>Options.Tolerance*1.5)continue;
                auto hit=Refine(pieces,a,b,seed,caches);if(!hit)continue;
                if(hit->a>hit->b||(hit->a==hit->b&&Fraction(pieces[hit->a],hit->u)>Fraction(pieces[hit->b],hit->v))){std::swap(hit->a,hit->b);std::swap(hit->u,hit->v);}
                const auto near=[&](std::size_t index,double u,bool end){return (Cached(pieces[index],u,caches[index]).p-Cached(pieces[index],end?End(pieces[index]):Start(pieces[index]),caches[index]).p).Norm()<=Options.Tolerance*2;};
                if(!joint){
                    if(hit->a+1==hit->b&&near(hit->a,hit->u,true)&&near(hit->b,hit->v,false))continue;
                    if(closed&&hit->a==0&&hit->b==pieces.size()-1&&near(hit->a,hit->u,false)&&near(hit->b,hit->v,true))continue;
                }
                if(hits.size()>4096)throw Failure{Status::IntersectionUnresolved,"Overlapping curves or too many intersections"};
                bool duplicate=false;for(const auto& h:hits)if(h.a==hit->a&&h.b==hit->b&&std::abs(Fraction(pieces[h.a],h.u)-Fraction(pieces[h.a],hit->u))<1e-6&&std::abs(Fraction(pieces[h.b],h.v)-Fraction(pieces[h.b],hit->v))<1e-6){duplicate=true;break;}
                if(!duplicate)hits.push_back(*hit);
            }
            std::sort(hits.begin(),hits.end(),[&](const auto& a,const auto& b){const auto x=a.a+Fraction(pieces[a.a],a.u),y=b.a+Fraction(pieces[b.a],b.u);return x!=y?x<y:a.b+Fraction(pieces[a.b],a.v)<b.b+Fraction(pieces[b.b],b.v);});
            return hits;
        }
        void Line(std::vector<OffsetCurvePiece3>& joins,V a,V b)
        {if((a-b).Norm()>Options.Tolerance){joins.push_back({CurveSegment3{Segment3{Point(a),Point(b)},{0,1,false}}, {}});++Result.AddedConnectors;}}
        std::vector<OffsetCurvePiece3> Join(const OffsetCurvePiece3& a,const OffsetCurvePiece3& b,V center)
        {
            // Preserve the original three-dimensional connector arithmetic and
            // endpoint bits. Only topology search and trimming use the plane.
            std::vector<OffsetCurvePiece3> joins;const auto x=Spatial(a,End(a)),y=Spatial(b,Start(b));if((x.p-y.p).Norm()<=Options.Tolerance)return joins;
            if(Options.Join==EOffsetJoin3::Round){const auto p=x.p-center,q=y.p-center;const double r=p.Norm();
                if(std::abs(r-q.Norm())>Options.Tolerance||r<=Options.Tolerance)throw Failure{Status::IntersectionUnresolved,"Round joint requires equal offset radii"};
                const auto z=p.Cross(q).Unit(),xx=p.Unit(),yy=z.Cross(xx);const double angle=std::atan2(q.Dot(yy),q.Dot(xx));
                if(angle<=0||angle>=std::numbers::pi-1e-10)throw Failure{Status::IntersectionUnresolved,"Ambiguous round joint"};
                Circle3 c;c.Radius=r;c.Placement.Location=Point(center);c.Placement.XDirection=Dir(xx);c.Placement.YDirection=Dir(yy);c.Placement.ZDirection=Dir(z);
                joins.push_back({CurveSegment3{c,{0,angle,false}}, {}});++Result.AddedConnectors;return joins;
            }
            if(Options.Join==EOffsetJoin3::Miter){const V u=x.t.Unit()*(Range(a).Reversed?-1:1),v=y.t.Unit()*(Range(b).Reversed?-1:1),w=y.p-x.p;
                const double dot=u.Dot(v),den=1-dot*dot;
                if(den>1e-12){const double s=(w.Dot(u)-dot*w.Dot(v))/den,t=(dot*w.Dot(u)-w.Dot(v))/den;const auto p=x.p+u*s,q=y.p+v*t;
                    const double limit=Options.MiterLimit*std::max((x.p-center).Norm(),(y.p-center).Norm());
                    if(s>=-Options.Tolerance&&t<=Options.Tolerance&&(p-q).Norm()<=Options.Tolerance&&(p-center).Norm()<=limit){Line(joins,x.p,p);Line(joins,p,y.p);return joins;}}
            }
            // A bounded miter clips to a bevel when its rays are skew or the
            // intersection exceeds the miter limit. No source extrapolation.
            Line(joins,x.p,y.p);return joins;
        }
        std::vector<OffsetCurvePiece3> Slice(const std::vector<OffsetCurvePiece3>& pieces,std::size_t a,double u,std::size_t b,double v)
        {
            std::vector<OffsetCurvePiece3> out;for(std::size_t i=a;i<=b;++i){auto p=pieces[i];if(Trim(p,i==a?u:Start(p),i==b?v:End(p)))out.push_back(std::move(p));}return out;
        }
        double Length(const std::vector<OffsetCurvePiece3>& pieces){double n=0;for(const auto& p:pieces){const auto nodes=Sample(p);for(std::size_t i=1;i<nodes.size();++i)n+=(nodes[i].p-nodes[i-1].p).Norm();}return n;}
        void VerifyConnected(const CompositeOffsetCurve3& curve)
        {
            if(curve.Segments.empty())throw Failure{Status::Collapsed,"Offset contour collapsed"};
            for(std::size_t i=0;i<curve.Segments.size();++i){if(i+1==curve.Segments.size()&&!curve.Closed)break;const auto& a=curve.Segments[i];const auto& b=curve.Segments[(i+1)%curve.Segments.size()];
                if((Eval(a,End(a)).p-Eval(b,Start(b)).p).Norm()>Options.Tolerance*1.01)throw Failure{Status::IntersectionUnresolved,"Trimmed contour is disconnected"};}
        }
    public:
        Builder(const ICurveOffset3DService& service,const CompositeOffsetOptions3& options):Service(service),Options(options){}
        CompositeOffsetResult3 Run(const std::vector<DirectionalCurveOffset3>& source,bool closed,bool allowPlanar=true)
        {
            try {
                Check();if(source.empty()||source.size()>4096||!std::isfinite(Options.Tolerance)||Options.Tolerance<=0||!std::isfinite(Options.MiterLimit)||Options.MiterLimit<1||!Options.MaxSamples||!Options.MaxIntersectionPairs)
                    throw Failure{Status::InvalidInput,"Invalid composite offset input or limits"};
                if(Options.Join!=EOffsetJoin3::Miter&&Options.Join!=EOffsetJoin3::Bevel&&Options.Join!=EOffsetJoin3::Round)throw Failure{Status::InvalidInput,"Invalid join mode"};
                if(Options.LoopRetention!=EOffsetLoopRetention3::Longest&&Options.LoopRetention!=EOffsetLoopRetention3::Start)throw Failure{Status::InvalidInput,"Invalid loop retention policy"};
                if(allowPlanar)Planar=PreparePlanarOffset3D(source,Options);
                std::vector<OffsetCurvePiece3> pieces;std::vector<V> centers,spatialCenters;
                for(std::size_t i=0;i<source.size();++i){const auto& r=source[i].Source.Range;std::vector<double> splits{r.First,r.Last};
                    Check();if(!std::isfinite(r.First)||!std::isfinite(r.Last)||r.First>=r.Last)throw Failure{Status::InvalidInput,"Invalid source range"};
                    const auto knots=[&](const auto& c,int continuity){OffsetParameters::AppendKnots(c,r,continuity,splits,Options.MaxSamples);};
                    std::visit([&](const auto& c){knots(c,2);},source[i].Source.Curve);
                    if(const auto* f=std::get_if<ParametricVectorField3>(&source[i].ReferenceDirection))std::visit([&](const auto& c){knots(c,1);},f->Coefficients);
                    std::sort(splits.begin(),splits.end());splits.erase(std::unique(splits.begin(),splits.end()),splits.end());if(r.Reversed)std::reverse(splits.begin(),splits.end());
                    if(splits.size()<2)throw Failure{Status::InvalidInput,"Invalid source range"};
                    for(std::size_t k=1;k<splits.size();++k){if(pieces.size()>=Options.MaxSamples)throw Failure{Status::BudgetExceeded,"Continuity subdivision budget exceeded"};auto law=source[i];law.Source.Range={std::min(splits[k-1],splits[k]),std::max(splits[k-1],splits[k]),r.Reversed};pieces.push_back({std::move(law),i});}
                }
                for(std::size_t i=0;i<pieces.size();++i){const auto& law=std::get<DirectionalCurveOffset3>(pieces[i].Geometry);
                    const auto ends=Evaluate(pieces[i],{Start(pieces[i]),End(pieces[i])});
                    centers.push_back(Vec(ends.Samples.back().SourcePoint));
                    if(Planar){const auto endpoint=Evaluate(pieces[i],{End(pieces[i])},true);spatialCenters.push_back(Vec(endpoint.Samples.front().SourcePoint));}
                    else spatialCenters.push_back(centers.back());
                    if(const auto* circle=std::get_if<Circle3>(&law.Source.Curve))if(const auto* axis=std::get_if<Direction3>(&law.ReferenceDirection))
                        if(std::abs(Vec(*axis).Unit().Dot(Vec(circle->Placement.ZDirection)))>1-1e-12&&Vec(ends.Samples.front().SourceDerivative).Dot(Vec(ends.Samples.front().OffsetDerivative))<=0)
                            throw Failure{Status::Collapsed,"Circular offset reaches or crosses its curvature center"};
                    if(i){const auto prior=Evaluate(pieces[i-1],{End(pieces[i-1])});if((Vec(prior.Samples.front().SourcePoint)-Vec(ends.Samples.front().SourcePoint)).Norm()>Options.Tolerance)throw Failure{Status::DisconnectedSource,"Source segments are not connected"};}
                }
                if(closed){const auto first=Evaluate(pieces.front(),{Start(pieces.front())});if((centers.back()-Vec(first.Samples.front().SourcePoint)).Norm()>Options.Tolerance)throw Failure{Status::DisconnectedSource,"Source seam is not closed"};}
                const auto joints=closed?pieces.size():pieces.size()-1;
                // Trim inner corners at refined intersections of the offset
                // expressions, not intersections of their display chords.
                for(std::size_t i=0;i<joints;++i){const auto j=(i+1)%pieces.size();if(i==j)continue;
                    if((Eval(pieces[i],End(pieces[i])).p-Eval(pieces[j],Start(pieces[j])).p).Norm()<=Options.Tolerance)continue;
                    const std::vector<OffsetCurvePiece3> pair{pieces[i],pieces[j]};const auto hits=Intersections(pair,false,true);
                    if(!hits.empty()){const auto h=*std::min_element(hits.begin(),hits.end(),[&](const auto& a,const auto& b){return 1-Fraction(pair[0],a.u)+Fraction(pair[1],a.v)<1-Fraction(pair[0],b.u)+Fraction(pair[1],b.v);});
                        if(!Trim(pieces[i],Start(pieces[i]),h.u)||!Trim(pieces[j],h.v,End(pieces[j])))throw Failure{Status::Collapsed,"Corner trimming consumed a segment"};++Result.TrimmedJoints;}
                }
                Result.Curve.Closed=closed;
                for(std::size_t i=0;i<pieces.size();++i){Result.Curve.Segments.push_back(pieces[i]);if(i<joints){auto joins=Join(pieces[i],pieces[(i+1)%pieces.size()],spatialCenters[i]);Result.Curve.Segments.insert(Result.Curve.Segments.end(),joins.begin(),joins.end());}}
                VerifyConnected(Result.Curve);
                for(;;){auto& p=Result.Curve.Segments;const auto hits=Intersections(p,closed);if(hits.empty())break;
                    if(Result.RemovedLoops.size()>=Options.MaxRemovedLoops)throw Failure{Status::BudgetExceeded,"Loop removal budget exceeded"};const auto h=hits.front();
                    auto loop=Slice(p,h.a,h.u,h.b,h.v),prefix=Slice(p,0,Start(p.front()),h.a,h.u),suffix=Slice(p,h.b,h.v,p.size()-1,End(p.back()));
                    std::vector<OffsetCurvePiece3> kept;
                    if(closed){kept=suffix;kept.insert(kept.end(),prefix.begin(),prefix.end());if(Options.LoopRetention==EOffsetLoopRetention3::Longest&&Length(loop)>Length(kept))std::swap(loop,kept);}
                    else {kept=prefix;kept.insert(kept.end(),suffix.begin(),suffix.end());}
                    if(kept.empty()||loop.empty()||Length(kept)<=Options.Tolerance||Length(loop)<=Options.Tolerance)throw Failure{Status::IntersectionUnresolved,"Overlapping or degenerate self-intersection at pieces "+std::to_string(h.a)+"/"+std::to_string(h.b)+" parameters "+std::to_string(h.u)+"/"+std::to_string(h.v)};
                    Result.RemovedLoops.push_back({std::move(loop),true});p=std::move(kept);VerifyConnected(Result.Curve);
                }
                Check();Result.Status=Status::Built;Result.Tolerance=Options.Tolerance;
                if(Planar)Result.Diagnostic="OCCT/PlanarTarget2D";
                return Result;
            }catch(const PlanarOffsetPreparationCancelled&){Result={};Result.Status=Status::Cancelled;Result.Diagnostic="Planar preparation cancelled";}
            catch(const Failure& e){Result={};Result.Status=e.Code;Result.Diagnostic=e.Message;}
            catch(const std::length_error& e){Result={};Result.Status=Status::BudgetExceeded;Result.Diagnostic=e.what();}
            catch(const std::invalid_argument& e){Result={};Result.Status=Status::InvalidInput;Result.Diagnostic=e.what();}
            catch(const std::exception& e){Result={};Result.Status=Status::EvaluationFailed;Result.Diagnostic=e.what();}
            if(Planar&&Result.Status!=Status::Cancelled&&Result.Status!=Status::BudgetExceeded)
            {
                // Near the plane-proof roundoff guard, projecting an extremely
                // short source can trip a derivative threshold. Confirm such a
                // failure with the spatial algorithm. Retain this call's used
                // evaluation/pair budgets and permit only this single retry.
                Planar.reset();Result={};return Run(source,closed,false);
            }
            return Result;
        }
    };
}
GeometryData::CompositeOffsetResult3 BuildCompositeOffset3D(const ICurveOffset3DService& service,
    const std::vector<GeometryData::DirectionalCurveOffset3>& segments,bool closed,const CompositeOffsetOptions3& options)
{return Builder(service,options).Run(segments,closed);}
}
