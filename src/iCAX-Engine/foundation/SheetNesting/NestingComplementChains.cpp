#include "NestingInternal.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <unordered_set>

namespace iCAX::SheetNesting::Detail
{
    namespace
    {
        struct Complement
        {
            Member Source;
            double Shear=0;
            Rect Envelope;
            std::optional<Rect> Pocket;
            std::string Material;
        };

        Point FromAffine(double u,double v,double shear){return {u+shear*v,v};}
        Shape AffineRectangle(Rect rectangle,double shear)
        {
            Shape shape;
            const double u=rectangle.X,v=rectangle.Y,w=rectangle.Width,h=rectangle.Height;
            shape.Outer={FromAffine(u,v,shear),FromAffine(u+w,v,shear),
                FromAffine(u+w,v+h,shear),FromAffine(u,v+h,shear)};
            return shape;
        }
        double Perimeter(Rect rectangle,double shear)
        {return 2*(rectangle.Width+rectangle.Height*std::hypot(1.0,shear));}

        void MatchSharedSubdivisions(Shape& pocket,const Shape& original,Pose pose)
        {
            std::vector<Point> world;
            double scale=1;
            for(auto point:original.Outer)
            {
                point=Transform(point,pose);world.push_back(point);
                scale=std::max({scale,std::abs(point.X),std::abs(point.Y)});
            }
            const double roundoff=64*std::numeric_limits<double>::epsilon()*scale;
            for(auto& corner:pocket.Outer)
                for(auto point:world)
                    if(std::hypot(corner.X-point.X,corner.Y-point.Y)<=roundoff){corner=point;break;}
            std::vector<Point> boundary;
            for(std::size_t i=0;i<pocket.Outer.size();++i)
            {
                const auto a=pocket.Outer[i],b=pocket.Outer[(i+1)%pocket.Outer.size()];
                const double dx=b.X-a.X,dy=b.Y-a.Y,length=std::hypot(dx,dy);
                std::vector<std::pair<double,Point>> edge{{0,a}};
                if(length>roundoff)
                    for(auto point:world)
                    {
                        const double along=((point.X-a.X)*dx+(point.Y-a.Y)*dy)/(length*length);
                        const long double cross=(static_cast<long double>(point.X)-a.X)*dy-(static_cast<long double>(point.Y)-a.Y)*dx;
                        if(along>0&&along<1&&std::abs(cross)<=static_cast<long double>(roundoff)*length
                            &&std::hypot(point.X-a.X,point.Y-a.Y)>roundoff&&std::hypot(point.X-b.X,point.Y-b.Y)>roundoff)
                            edge.emplace_back(along,point);
                    }
                std::sort(edge.begin(),edge.end(),[](const auto& x,const auto& y){return x.first<y.first;});
                for(const auto& item:edge)
                    if(boundary.empty()||std::hypot(item.second.X-boundary.back().X,item.second.Y-boundary.back().Y)>roundoff)boundary.push_back(item.second);
            }
            pocket.Outer=std::move(boundary);
        }

        std::optional<Complement> Describe(const Problem& problem,const Pattern& singleton,
            const std::function<bool()>& stopped)
        {
            if(singleton.Members.size()!=1)return {};
            const auto member=singleton.Members.front();const auto& part=problem.Parts[member.PartIndex];
            const auto& shape=part.Geometry;const double tolerance=problem.Tolerance;
            if(shape.Radius>0||!shape.Holes.empty()||shape.Outer.size()<4
                ||!AngleAllowed(part,member.Transform.AngleDegrees,tolerance))return {};
            std::vector<Point> points;
            for(auto point:shape.Outer)
            {
                if(stopped())return {};
                point=Transform(point,member.Transform);
                if(points.empty()||std::hypot(point.X-points.back().X,point.Y-points.back().Y)>tolerance)points.push_back(point);
            }
            if(points.size()>1&&std::hypot(points.front().X-points.back().X,points.front().Y-points.back().Y)<=tolerance)points.pop_back();
            // Subdivisions do not introduce additional physical corners.
            bool changed=true;
            while(changed&&points.size()>3)
            {
                changed=false;
                for(std::size_t i=0;i<points.size();++i)
                {
                    if(stopped())return {};
                    const auto a=points[(i+points.size()-1)%points.size()],b=points[i],c=points[(i+1)%points.size()];
                    const double dx=c.X-a.X,dy=c.Y-a.Y,length=std::hypot(dx,dy);
                    if(length<=tolerance)continue;
                    const double along=((b.X-a.X)*dx+(b.Y-a.Y)*dy)/(length*length);
                    const double cross=(b.X-a.X)*dy-(b.Y-a.Y)*dx;
                    if(along>=0&&along<=1&&std::abs(cross)<=tolerance*length)
                    {points.erase(points.begin()+static_cast<std::ptrdiff_t>(i));changed=true;break;}
                }
            }
            if(points.size()!=4&&points.size()!=6)return {};
            double shear=0,longestVertical=0;
            for(std::size_t i=0;i<points.size();++i)
            {
                const auto a=points[i],b=points[(i+1)%points.size()];const double dy=b.Y-a.Y;
                if(std::abs(dy)>std::max(tolerance,longestVertical))
                {longestVertical=std::abs(dy);shear=(b.X-a.X)/dy;}
            }
            if(longestVertical<=tolerance||!std::isfinite(shear))return {};
            const double uTolerance=tolerance*std::hypot(1.0,shear);
            double u0=std::numeric_limits<double>::infinity(),v0=u0,u1=-u0,v1=-u0;
            std::vector<Point> affine;
            for(std::size_t i=0;i<points.size();++i)
            {
                const auto a=points[i],b=points[(i+1)%points.size()];
                const double dx=b.X-a.X,dy=b.Y-a.Y;
                if(std::abs(dy)>tolerance&&std::abs(dx-shear*dy)>2*uTolerance)return {};
                const Point uv{a.X-shear*a.Y,a.Y};
                if(!std::isfinite(uv.X)||!std::isfinite(uv.Y))return {};
                affine.push_back(uv);u0=std::min(u0,uv.X);u1=std::max(u1,uv.X);v0=std::min(v0,uv.Y);v1=std::max(v1,uv.Y);
            }
            Complement output{member,shear,{u0,v0,u1-u0,v1-v0},{},part.Material};
            if(output.Envelope.Width<=4*uTolerance||output.Envelope.Height<=4*tolerance)return {};
            const double area=Area(shape),envelopeArea=output.Envelope.Width*output.Envelope.Height;
            const double allowance=4*tolerance*Perimeter(output.Envelope,shear);
            if(!std::isfinite(envelopeArea)||area<=0||allowance>=envelopeArea*.01)return {};
            if(points.size()==4)
            {
                if(std::abs(area-envelopeArea)<=allowance)return output;
                return {};
            }
            // A genuine six-corner L has one concave corner. Test all interior
            // vertices against the four envelope corners, then verify real material.
            for(const auto c:affine)
            {
                if(c.X<=u0+2*uTolerance||c.X>=u1-2*uTolerance||c.Y<=v0+2*tolerance||c.Y>=v1-2*tolerance)continue;
                for(double cornerU:{u0,u1})for(double cornerV:{v0,v1})
                {
                    if(stopped())return {};
                    Rect pocket{std::min(c.X,cornerU),std::min(c.Y,cornerV),std::abs(c.X-cornerU),std::abs(c.Y-cornerV)};
                    if(std::abs(area+pocket.Width*pocket.Height-envelopeArea)>allowance)continue;
                    auto missing=AffineRectangle(pocket,shear);
                    // Keep the original shared vertices on the virtual pocket;
                    // quantizing a split and an unsplit edge can create a false
                    // sliver. Only floating-point roundoff is canonicalized.
                    MatchSharedSubdivisions(missing,shape,member.Transform);
                    if(ShapesConflict(shape,member.Transform,missing,{},0,tolerance))continue;
                    if(output.Pocket)return {}; // Ambiguous tolerance-scale pockets are not rules.
                    output.Pocket=pocket;
                }
            }
            return output.Pocket?std::optional<Complement>(std::move(output)):std::nullopt;
        }

        std::string PatternKey(const Pattern& pattern,double tolerance)
        {
            std::vector<std::string> members;
            const double quantum=std::max(tolerance,1e-7);
            for(const auto& member:pattern.Members)
            {
                std::ostringstream out;out<<std::setprecision(17)<<member.PartIndex<<':'
                    <<std::round(member.Transform.X/quantum)<<':'<<std::round(member.Transform.Y/quantum)<<':'
                    <<std::round(NormalizeAngle(member.Transform.AngleDegrees)*1e7);
                members.push_back(out.str());
            }
            std::sort(members.begin(),members.end());std::string key;
            for(const auto& member:members){key+=member;key+=';';}return key;
        }
    }

    std::vector<Pattern> BuildComplementChains(const Problem& problem,const Options& options,
        const std::function<bool()>& stop)
    {
        const auto stopped=[&]{return stop&&stop();};
        auto baseOptions=options;baseOptions.EnableCombinations=false;
        auto output=BuildPatterns(problem,baseOptions,stop);
        const auto maxMembers=std::min<std::size_t>(4,options.MaxPatternMembers);
        if(!options.EnableCombinations||problem.PartGap!=0||maxMembers<2||options.MaxPatterns==0||stopped())return output;
        std::vector<Complement> shapes;
        // Ordered dimension ranges avoid both rounding-bucket boundary misses and
        // an all-pairs edge-contact matrix. Equal geometric keys retain all IDs.
        using Slopes=std::map<double,std::vector<std::size_t>>;
        using Heights=std::map<double,Slopes>;
        using Widths=std::map<double,Heights>;
        std::map<std::string,Widths> index;
        for(const auto& singleton:output)
        {
            if(stopped())return output;
            if(auto shape=Describe(problem,singleton,stopped))
            {
                index[shape->Material][shape->Envelope.Width][shape->Envelope.Height][shape->Shear].push_back(shapes.size());
                shapes.push_back(std::move(*shape));
            }
        }
        std::unordered_set<std::string> keys;
        for(const auto& pattern:output)keys.insert(PatternKey(pattern,problem.Tolerance));
        std::size_t additional=0;
        std::vector<std::size_t> consumed(problem.Parts.size());
        const std::size_t perRootBudget=std::clamp(options.MaxPlacementAttempts/std::max<std::size_t>(1,shapes.size()),std::size_t(32),std::size_t(4096));
        for(const auto& root:shapes)
        {
            if(stopped()||additional>=options.MaxPatterns)break;
            if(!root.Pocket)continue;
            const double uTolerance=8*problem.Tolerance*std::hypot(1.0,root.Shear),vTolerance=8*problem.Tolerance;
            const auto material=index.find(root.Material);if(material==index.end())continue;
            Pattern chain;chain.Members.push_back(root.Source);consumed[root.Source.PartIndex]=1;
            std::size_t attempts=0;
            const auto finish=[&]()
            {
                Pattern candidate=chain;
                const double area=std::accumulate(candidate.Members.begin(),candidate.Members.end(),0.0,
                    [&](double sum,const auto& member){return sum+Area(problem.Parts[member.PartIndex].Geometry);});
                if(std::abs(area-root.Envelope.Width*root.Envelope.Height)>4*problem.Tolerance*Perimeter(root.Envelope,root.Shear))return false;
                NormalizePattern(candidate,problem);
                if(candidate.Kind!=ShapeKind::Rectangle&&candidate.Kind!=ShapeKind::Parallelogram)return false;
                if(!keys.insert(PatternKey(candidate,problem.Tolerance)).second)return false;
                output.push_back(std::move(candidate));++additional;return true;
            };
            std::function<bool(Rect)> fill;
            fill=[&](Rect pocket)
            {
                if(stopped()||chain.Members.size()>=maxMembers||attempts>=perRootBudget)return false;
                const double slopeTolerance=uTolerance/std::max(pocket.Height,problem.Tolerance);
                for(auto widths=material->second.lower_bound(pocket.Width-uTolerance);
                    widths!=material->second.end()&&widths->first<=pocket.Width+uTolerance;++widths)
                for(auto heights=widths->second.lower_bound(pocket.Height-vTolerance);
                    heights!=widths->second.end()&&heights->first<=pocket.Height+vTolerance;++heights)
                for(auto slopes=heights->second.lower_bound(root.Shear-slopeTolerance);
                    slopes!=heights->second.end()&&slopes->first<=root.Shear+slopeTolerance;++slopes)
                for(const auto shapeIndex:slopes->second)
                {
                    if(stopped()||attempts++>=perRootBudget)return false;
                    const auto& next=shapes[shapeIndex];const auto partIndex=next.Source.PartIndex;
                    if(consumed[partIndex]>=problem.Parts[partIndex].Quantity)continue;
                    const double du=pocket.X-next.Envelope.X,dv=pocket.Y-next.Envelope.Y;
                    auto member=next.Source;member.Transform.X+=du+root.Shear*dv;member.Transform.Y+=dv;
                    bool valid=true;
                    for(const auto& placed:chain.Members)
                    {
                        if(stopped())return false;
                        if(ShapesConflict(problem.Parts[partIndex].Geometry,member.Transform,
                            problem.Parts[placed.PartIndex].Geometry,placed.Transform,0,problem.Tolerance)){valid=false;break;}
                    }
                    if(!valid)continue;
                    // Matching a dimension is only a search hint. Actual vertices
                    // must stay in this physical affine pocket before publication.
                    for(auto point:problem.Parts[partIndex].Geometry.Outer)
                    {
                        point=Transform(point,member.Transform);const double u=point.X-root.Shear*point.Y;
                        if(u<pocket.X-uTolerance||u>pocket.X+pocket.Width+uTolerance
                            ||point.Y<pocket.Y-vTolerance||point.Y>pocket.Y+pocket.Height+vTolerance){valid=false;break;}
                    }
                    if(!valid)continue;
                    ++consumed[partIndex];chain.Members.push_back(member);
                    const bool complete=next.Pocket?fill({next.Pocket->X+du,next.Pocket->Y+dv,next.Pocket->Width,next.Pocket->Height}):finish();
                    chain.Members.pop_back();--consumed[partIndex];
                    if(complete)return true; // First pass preserves an opportunity for every root family.
                }
                return false;
            };
            fill(*root.Pocket);consumed[root.Source.PartIndex]=0;
        }
        return output;
    }
}
