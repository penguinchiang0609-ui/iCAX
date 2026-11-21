#pragma once
#include "NestingInternal.h"
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace iCAX::SheetNesting::AxisSafety
{
    struct ArithmeticUncertainty {};
    inline bool ArithmeticSupported()
    {
        if (std::fegetround()!=FE_TONEAREST) return false;
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
        if (_mm_getcsr()&((1u<<15)|(1u<<6))) return false;
#endif
        return true;
    }
    // Error-free sums/products retain the exact binary64 source and pose values.
    // The geometry guard therefore does not inherit ClipperD's decimal grid.
    struct Number
    {
        std::vector<double> Terms;
        bool Valid = true;
        Number() = default;
        Number(double value) { if (value != 0) Terms.push_back(value); Valid = std::isfinite(value); }
        void Add(double value)
        {
            std::vector<double> next;
            double sum = value;
            for (const double term : Terms)
            {
                const double combined = sum + term;
                const double virtualTerm = combined - sum;
                const double error = (sum - (combined - virtualTerm)) + (term - virtualTerm);
                if (error != 0) next.push_back(error);
                sum = combined;
            }
            if (sum != 0) next.push_back(sum);
            if (!std::isfinite(sum)) Valid = false;
            Terms = std::move(next);
        }
        int Sign() const
        {
            if (!Valid) throw ArithmeticUncertainty{};
            return Terms.empty() ? 0 : (Terms.back() > 0 ? 1 : -1);
        }
    };
    inline Number operator+(Number a, const Number& b)
    {
        a.Valid = a.Valid && b.Valid;
        for (const double term : b.Terms) a.Add(term);
        return a;
    }
    inline Number operator-(Number a, const Number& b)
    {
        a.Valid = a.Valid && b.Valid;
        for (const double term : b.Terms) a.Add(-term);
        return a;
    }
    inline Number operator*(const Number& a, const Number& b)
    {
        Number result;
        result.Valid = a.Valid && b.Valid;
        for (const double first : a.Terms) for (const double second : b.Terms)
        {
            int firstExponent=0,secondExponent=0;
            std::frexp(first,&firstExponent);std::frexp(second,&secondExponent);
            // The lowest possible product bit must survive gradual underflow,
            // including an FMA residual below a still-normal rounded product.
            if (firstExponent+secondExponent<std::numeric_limits<double>::min_exponent+
                std::numeric_limits<double>::digits)
                result.Valid=false;
            const double product = first * second;
            // Underflow cannot be used as an exact separation certificate.
            if (!std::isfinite(product) || (first != 0 && second != 0 &&
                std::abs(product) < std::numeric_limits<double>::min())) result.Valid = false;
            const double error = std::fma(first, second, -product);
            if (error != 0) result.Add(error);
            if (product != 0) result.Add(product);
        }
        return result;
    }
    struct Interval
    {
        double Low=1,High=-1;
        bool Valid() const { return std::isfinite(Low)&&std::isfinite(High)&&Low<=High; }
    };
    inline double SumBound(double a,double b,bool upper)
    {
        const double sum=a+b;
        const double virtualB=sum-a;
        const double error=(a-(sum-virtualB))+(b-virtualB);
        if ((upper&&error>0)||(!upper&&error<0))
            return std::nextafter(sum,upper?std::numeric_limits<double>::infinity():
                -std::numeric_limits<double>::infinity());
        return sum;
    }
    inline Interval operator+(Interval a,Interval b)
    {
        if (!a.Valid()||!b.Valid()) return {};
        return {SumBound(a.Low,b.Low,false),SumBound(a.High,b.High,true)};
    }
    inline Interval operator-(Interval a,Interval b)
    {
        return a+Interval{-b.High,-b.Low};
    }
    inline Interval ProductBounds(double a,double b)
    {
        if (a==0||b==0) return {0,0};
        // This conservative range also protects the FMA residual. Products
        // have exponents within +/-800 and exact low bits no lower than -904.
        if (std::abs(a)<0x1p-400||std::abs(b)<0x1p-400||
            std::abs(a)>0x1p400||std::abs(b)>0x1p400) return {};
        const double product=a*b,error=std::fma(a,b,-product);
        return {error<0?std::nextafter(product,-std::numeric_limits<double>::infinity()):product,
            error>0?std::nextafter(product,std::numeric_limits<double>::infinity()):product};
    }
    inline Interval operator*(Interval a,Interval b)
    {
        if (!a.Valid()||!b.Valid()) return {};
        if (a.Low==a.High&&b.Low==b.High) return ProductBounds(a.Low,b.Low);
        const Interval products[]{ProductBounds(a.Low,b.Low),ProductBounds(a.Low,b.High),
            ProductBounds(a.High,b.Low),ProductBounds(a.High,b.High)};
        Interval result=products[0];
        for (const auto product:products)
        {
            if (!product.Valid()) return {};
            result.Low=std::min(result.Low,product.Low);result.High=std::max(result.High,product.High);
        }
        return result;
    }
    inline Interval CoordinateBounds(const Number& number)
    {
        if (!number.Valid) return {};
        Interval result{0,0};
        for (const double term:number.Terms)
        {
            // Degree-two exact fallback operations on these binary64 terms
            // cannot overflow or lose a subnormal product residual. Outside
            // this range the original Number path remains authoritative.
            const double magnitude=std::abs(term);
            if (magnitude!=0&&(magnitude<0x1p-129||magnitude>=0x1p128)) return {};
            if (number.Terms.size()==1) return {term,term};
            result=result+Interval{term,term};
        }
        return result;
    }
    struct Vector { Number X,Y; };
    struct PointBounds { Interval X,Y; };
    inline PointBounds BoundsOf(const Vector& point)
    {
        return {CoordinateBounds(point.X),CoordinateBounds(point.Y)};
    }
    inline Vector operator-(const Vector& a, const Vector& b) { return {a.X-b.X,a.Y-b.Y}; }
    inline Number Dot(const Vector& a,const Vector& b) { return a.X*b.X+a.Y*b.Y; }
    inline Number Cross(const Vector& a,const Vector& b) { return a.X*b.Y-a.Y*b.X; }
    inline Number Orientation(const Vector& a,const Vector& b,const Vector& c) { return Cross(b-a,c-a); }
    inline bool AtLeast(const Number& a,const Number& b)
    { const auto difference=a-b; return difference.Sign()>=0; }
    inline std::optional<int> IntervalSign(Interval value)
    {
        if (!value.Valid()) return {};
        if (value.Low>0) return 1;
        if (value.High<0) return -1;
        if (value.Low==0&&value.High==0) return 0;
        return {};
    }
    inline Interval OrientationBounds(const Vector& a,const Vector& b,const Vector& c)
    {
        const auto first=BoundsOf(a),second=BoundsOf(b),point=BoundsOf(c);
        return (second.X-first.X)*(point.Y-first.Y)-(second.Y-first.Y)*(point.X-first.X);
    }
    inline int OrientationSign(const Vector& a,const Vector& b,const Vector& c,bool filtered)
    {
        if (filtered) if (const auto sign=IntervalSign(OrientationBounds(a,b,c))) return *sign;
        return Orientation(a,b,c).Sign();
    }
    inline int DifferenceSign(const Vector& a,const Vector& b,bool xAxis,bool filtered)
    {
        if (filtered)
        {
            const auto first=BoundsOf(a),second=BoundsOf(b);
            if (const auto sign=IntervalSign(xAxis?first.X-second.X:first.Y-second.Y)) return *sign;
        }
        return ((xAxis?a.X:a.Y)-(xAxis?b.X:b.Y)).Sign();
    }
    inline int ProjectionSign(const Vector& a,const Vector& b,const Vector& c,const Vector& d,bool filtered)
    {
        if (filtered)
        {
            const auto first=BoundsOf(a),second=BoundsOf(b),start=BoundsOf(c),end=BoundsOf(d);
            const auto projection=(first.X-second.X)*(start.X-end.X)+(first.Y-second.Y)*(start.Y-end.Y);
            if (const auto sign=IntervalSign(projection)) return *sign;
        }
        return Dot(a-b,c-d).Sign();
    }
    inline bool Between(const Vector& point,const Vector& a,const Vector& b,bool filtered=false)
    { return ProjectionSign(point,a,b,a,filtered)>=0&&ProjectionSign(point,b,a,b,filtered)>=0; }

    // Build an exact world-coordinate hull used only for separation certificates.
    // Collision topology continues to use every original outer and hole vertex.
    inline std::vector<Vector> SeparationHull(const std::vector<Vector>& ring,bool filtered=true)
    {
        if (ring.size()<3) return {};
        std::vector<std::size_t> indices;
        indices.reserve(ring.size());
        for (std::size_t i=0;i<ring.size();++i)
        {
            const auto bounds=BoundsOf(ring[i]);
            if (!bounds.X.Valid()||!bounds.Y.Valid()||ring[i].X.Terms.size()>4||ring[i].Y.Terms.size()>4)
                throw ArithmeticUncertainty{};
            indices.push_back(i);
        }
        std::sort(indices.begin(),indices.end(),[&](std::size_t a,std::size_t b)
        {
            const int x=DifferenceSign(ring[a],ring[b],true,filtered);
            return x<0||(!x&&DifferenceSign(ring[a],ring[b],false,filtered)<0);
        });
        std::vector<std::size_t> hull;
        hull.reserve(ring.size()*2);
        const auto append=[&](std::size_t index,std::size_t minimum)
        {
            while (hull.size()>minimum&&OrientationSign(ring[hull[hull.size()-2]],ring[hull.back()],ring[index],filtered)<=0)
                hull.pop_back();
            hull.push_back(index);
        };
        for (const auto index:indices) append(index,1);
        const auto lower=hull.size();
        for (auto i=indices.size()-1;i>0;--i) append(indices[i-1],lower);
        if (hull.size()>1) hull.pop_back();
        std::vector<Vector> result;
        result.reserve(hull.size());
        for (const auto index:hull) result.push_back(ring[index]);
        return result;
    }
    struct Geometry
    {
        std::vector<std::vector<Vector>> Rings;
        std::vector<bool> CommonEdgeRings; // Empty permits every original polygon ring.
        std::vector<Vector> Hull; // Exact enclosing hull of the nominal world exterior.
        PointBounds Envelope; // Conservative bounds of every represented material vertex.
        Vector Center;
        double Radius = 0;
        double Roundoff = 0;
        bool Convex = false;
        bool Valid = true;
    };
    inline PointBounds MaterialEnvelope(const Geometry& geometry)
    {
        // Limit the certificate to the same supported expansion range used by
        // the edge filters. Unsupported coordinates retain the complete exact
        // path, including its Uncertain verdicts.
        if (geometry.Radius>0||geometry.Rings.empty()) return {};
        PointBounds result;bool first=true;
        for (const auto& ring:geometry.Rings) for (const auto& point:ring)
        {
            if (point.X.Terms.size()>4||point.Y.Terms.size()>4) return {};
            const auto bounds=BoundsOf(point);
            if (!bounds.X.Valid()||!bounds.Y.Valid()) return {};
            if (first) {result=bounds;first=false;}
            else
            {
                result.X.Low=std::min(result.X.Low,bounds.X.Low);result.X.High=std::max(result.X.High,bounds.X.High);
                result.Y.Low=std::min(result.Y.Low,bounds.Y.Low);result.Y.High=std::max(result.Y.High,bounds.Y.High);
            }
        }
        return result;
    }
    inline Geometry MakeWithPredicates(const Shape& shape,Pose pose,bool filtered,bool boundaryOnly=false)
    {
        Geometry result;
        if (!ArithmeticSupported()) { result.Valid=false; return result; }
        result.CommonEdgeRings=shape.CommonEdgeRings;
        const auto ringCount=shape.Radius>0?std::size_t{}:shape.Holes.size()+1;
        if (!result.CommonEdgeRings.empty()&&result.CommonEdgeRings.size()!=ringCount)
        { result.Valid=false; return result; }
        const double angle=Detail::NormalizeAngle(pose.AngleDegrees);
        const bool quarter=angle==0||angle==90||angle==180||angle==270;
        double scale=std::max({1.,std::abs(pose.X),std::abs(pose.Y),shape.Radius});
        const auto transform=[&](Point point)
        {
            scale=std::max(scale,std::abs(point.X)+std::abs(point.Y));
            if (!quarter)
            {
                const auto world=Detail::Transform(point,pose);
                return Vector{Number(world.X),Number(world.Y)};
            }
            if (pose.Mirrored) point.Y=-point.Y;
            if (angle==90) point={-point.Y,point.X};
            else if (angle==180) point={-point.X,-point.Y};
            else if (angle==270) point={point.Y,-point.X};
            return Vector{Number(point.X)+Number(pose.X),Number(point.Y)+Number(pose.Y)};
        };
        result.Radius=shape.Radius;
        if (shape.Radius>0) result.Center=transform(shape.Center);
        else
        {
            const auto append=[&](const std::vector<Point>& source)
            {
                auto& ring=result.Rings.emplace_back();
                for (const auto point:Detail::MaterialRing(source)) ring.push_back(transform(point));
            };
            append(shape.Outer);
            for (const auto& hole:shape.Holes) append(hole);
            if (!boundaryOnly&&result.Rings.size()==1)
            {
                const auto& ring=result.Rings.front();
                int winding=0;
                result.Convex=true;
                for (std::size_t i=0;i<ring.size();++i)
                {
                    int sign=0;
                    try{sign=OrientationSign(ring[i],ring[(i+1)%ring.size()],ring[(i+2)%ring.size()],filtered);}
                    catch(const ArithmeticUncertainty&){result.Valid=false;continue;}
                    if (sign&&winding&&sign!=winding) result.Convex=false;
                    if (sign) winding=sign;
                }
            }
        }
        if (!boundaryOnly&&!result.Convex&&!result.Rings.empty()&&result.Rings.front().size()>16)
        {
            try { result.Hull=SeparationHull(result.Rings.front(),filtered); }
            catch (const ArithmeticUncertainty&) { result.Hull.clear(); }
        }
        // Arbitrary-angle trig and world-coordinate rounding are conservatively
        // enclosed. Exact quarter turns above need no artificial material gap.
        if (!quarter) result.Roundoff=128*std::numeric_limits<double>::epsilon()*scale;
        result.Envelope=MaterialEnvelope(result);
        return result;
    }
    inline Geometry Make(const Shape& shape,Pose pose)
    {return MakeWithPredicates(shape,pose,true);}
    // A necessary-condition screen needs the unchanged represented material
    // rings and seam permissions, but no convexity or separation hull. Keep
    // its quarter-turn arithmetic and arbitrary-angle world rounding in the
    // same preparation path as the complete material guard.
    inline Geometry MakeBoundary(const Shape& shape,Pose pose)
    {return MakeWithPredicates(shape,pose,true,true);}
    // Differential preparation reference: identical represented coordinates,
    // with unfiltered expansion predicates for convexity and hull selection.
    inline Geometry MakeExact(const Shape& shape,Pose pose)
    {return MakeWithPredicates(shape,pose,false);}
    // Quarter turns use error-free source-coordinate plus pose-offset sums.
    // Arbitrary-angle poses retain Make and its pose-dependent roundoff bound.
    inline Geometry TranslateQuarterTurn(const Geometry& source,Point offset)
    {
        Geometry result=source;
        if (source.Roundoff!=0||(!source.CommonEdgeRings.empty()&&source.CommonEdgeRings.size()!=source.Rings.size()))
        { result.Valid=false;return result; }
        const Number dx(offset.X),dy(offset.Y);
        const auto translate=[&](Vector& point)
        {
            point.X=point.X+dx;point.Y=point.Y+dy;
            result.Valid=result.Valid&&point.X.Valid&&point.Y.Valid;
        };
        for (auto& ring:result.Rings) for (auto& point:ring) translate(point);
        for (auto& point:result.Hull) translate(point);
        if (result.Radius>0) translate(result.Center);
        result.Envelope=MaterialEnvelope(result);
        return result;
    }
    enum class Verdict { Safe, Blocked, Uncertain };
    inline Number Clearance(const Geometry& a,const Geometry& b,double nominal)
    { return Number(nominal)+Number(a.Roundoff)+Number(b.Roundoff); }
    inline bool EnvelopesSeparated(const Geometry& a,const Geometry& b,const Number& required)
    {
        if (!ArithmeticSupported()||!a.Valid||!b.Valid||required.Terms.size()>4||
            !a.Envelope.X.Valid()||!a.Envelope.Y.Valid()||!b.Envelope.X.Valid()||!b.Envelope.Y.Valid()) return false;
        const auto radius=CoordinateBounds(required);
        // Strict lower axis separation proves the full ordinary clearance.
        // Near/touching pairs retain every original material predicate.
        return radius.Valid()&&radius.Low>=0&&
            (SumBound(b.Envelope.X.Low,-a.Envelope.X.High,false)>radius.High||
             SumBound(a.Envelope.X.Low,-b.Envelope.X.High,false)>radius.High||
             SumBound(b.Envelope.Y.Low,-a.Envelope.Y.High,false)>radius.High||
             SumBound(a.Envelope.Y.Low,-b.Envelope.Y.High,false)>radius.High);
    }
    inline double ProposalClearance(const Geometry& a,const Geometry& b,double nominal)
    {
        double result=nominal;
        for (const double correction:{a.Roundoff,b.Roundoff})
            if (correction>0) result=std::nextafter(result+correction,std::numeric_limits<double>::infinity());
        return result;
    }
    inline double ProposalMargin(const Geometry& geometry,double nominal)
    {
        return geometry.Roundoff>0?std::nextafter(nominal+geometry.Roundoff,
            std::numeric_limits<double>::infinity()):nominal;
    }
    inline bool SegmentDistanceClearBounds(const Vector& p,const Vector& q,
        const Vector& u,const Vector& v,const Number& radius,bool strict=false)
    {
        // Preserve legacy expansion uncertainty as well as the distance sign.
        // Input low bits are at least 2^-181, hence degree-four low bits at
        // least 2^-724. The bounded term count also excludes overflow.
        const Number* inputs[]{&p.X,&p.Y,&q.X,&q.Y,&u.X,&u.Y,&v.X,&v.Y,&radius};
        Interval values[9];
        for (std::size_t i=0;i<9;++i)
        {
            if (inputs[i]->Terms.size()>4) return false;
            values[i]=CoordinateBounds(*inputs[i]);
            if (!values[i].Valid()||std::abs(values[i].Low)>0x1p130||
                std::abs(values[i].High)>0x1p130) return false;
        }
        if (values[8].Low<0) return false;
        const auto px=values[2]-values[0],py=values[3]-values[1];
        const auto ux=values[6]-values[4],uy=values[7]-values[5];
        // PointSegmentClear rejects degenerate edges before its distance test.
        if (!(px.Low>0||px.High<0||py.Low>0||py.High<0)||
            !(ux.Low>0||ux.High<0||uy.Low>0||uy.High<0)) return false;
        for (std::size_t axis=0;axis<2;++axis)
        {
            const double firstLow=std::min(values[axis].Low,values[2+axis].Low);
            const double firstHigh=std::max(values[axis].High,values[2+axis].High);
            const double secondLow=std::min(values[4+axis].Low,values[6+axis].Low);
            const double secondHigh=std::max(values[4+axis].High,values[6+axis].High);
            const double forward=SumBound(secondLow,-firstHigh,false);
            const double backward=SumBound(firstLow,-secondHigh,false);
            if (strict?(forward>values[8].High||backward>values[8].High):
                (forward>=values[8].High||backward>=values[8].High)) return true;
        }
        return false;
    }
    inline bool PointSegmentClear(const Vector& point,const Vector& a,const Vector& b,const Number& radius)
    {
        const auto direction=b-a,offset=point-a;
        const auto length2=Dot(direction,direction),projection=Dot(offset,direction);
        const auto required2=radius*radius;
        if (!projection.Valid||!length2.Valid||!required2.Valid||length2.Sign()<=0) throw ArithmeticUncertainty{};
        if (projection.Sign()<=0) return AtLeast(Dot(offset,offset),required2);
        if (AtLeast(projection,length2)) return AtLeast(Dot(point-b,point-b),required2);
        const auto cross=Cross(direction,offset);
        return AtLeast(cross*cross,required2*length2);
    }
    inline std::optional<bool> SeparateAxisBounds(PointBounds start,PointBounds end,
        const std::vector<PointBounds>& a,const std::vector<PointBounds>& b)
    {
        struct Range { Interval Minimum,Maximum; };
        const auto dx=end.X-start.X,dy=end.Y-start.Y;
        const auto project=[&](const std::vector<PointBounds>& ring)->std::optional<Range>
        {
            Range range;
            bool first=true;
            for (const auto& point:ring)
            {
                const auto value=dx*(point.Y-start.Y)-dy*(point.X-start.X);
                if (!value.Valid()) return {};
                if (first) { range={value,value};first=false; }
                else
                {
                    range.Minimum.Low=std::min(range.Minimum.Low,value.Low);
                    range.Minimum.High=std::min(range.Minimum.High,value.High);
                    range.Maximum.Low=std::max(range.Maximum.Low,value.Low);
                    range.Maximum.High=std::max(range.Maximum.High,value.High);
                }
            }
            if (first) return {};
            return range;
        };
        const auto first=project(a),second=project(b);
        if (!first||!second) return {};
        if (first->Maximum.High<=second->Minimum.Low||second->Maximum.High<=first->Minimum.Low) return true;
        if (first->Maximum.Low>second->Minimum.High&&second->Maximum.Low>first->Minimum.High) return false;
        return {};
    }
    inline bool SeparateOnEdges(const std::vector<Vector>& edges,const std::vector<Vector>& a,
        const std::vector<Vector>& b,bool filtered=false)
    {
        // A pair query owns these interval copies. Their coordinates have the
        // same enclosures as BoundsOf in every projection, without repeatedly
        // summing the same expansion for every edge of a detailed contour.
        std::vector<PointBounds> firstBounds,secondBounds;
        if (filtered)
        {
            firstBounds.reserve(a.size());secondBounds.reserve(b.size());
            for (const auto& point:a) firstBounds.push_back(BoundsOf(point));
            for (const auto& point:b) secondBounds.push_back(BoundsOf(point));
        }
        for (std::size_t i=0;i<edges.size();++i)
        {
            if (filtered) if (const auto separation=SeparateAxisBounds(BoundsOf(edges[i]),
                BoundsOf(edges[(i+1)%edges.size()]),firstBounds,secondBounds))
            {
                if (*separation) return true;
                continue;
            }
            const auto direction=edges[(i+1)%edges.size()]-edges[i];
            const Vector axis{Number{}-direction.Y,direction.X};
            Number al=Dot(a.front()-edges[i],axis),ah=al;
            Number bl=Dot(b.front()-edges[i],axis),bh=bl;
            bool valid=al.Valid&&bl.Valid;
            const auto extend=[&](const std::vector<Vector>& ring,Number& low,Number& high)
            {
                for (const auto& point:ring)
                {
                    const auto value=Dot(point-edges[i],axis);
                    valid=valid&&value.Valid;
                    if (AtLeast(low,value)) low=value;
                    if (AtLeast(value,high)) high=value;
                }
            };
            extend(a,al,ah);extend(b,bl,bh);
            if (valid&&(AtLeast(bl,ah)||AtLeast(al,bh))) return true;
        }
        return false;
    }
    enum class Location { Outside, Boundary, Inside, Uncertain };
    inline Location InRing(const Vector& point,const std::vector<Vector>& ring,bool filtered=false)
    {
        int winding=0;
        const auto pointBounds=filtered?BoundsOf(point):PointBounds{};
        for (std::size_t i=0;i<ring.size();++i)
        {
            const auto& a=ring[i];const auto& b=ring[(i+1)%ring.size()];
            if (filtered&&point.X.Terms.size()<=4&&point.Y.Terms.size()<=4&&
                a.X.Terms.size()<=4&&a.Y.Terms.size()<=4&&b.X.Terms.size()<=4&&b.Y.Terms.size()<=4)
            {
                const auto first=BoundsOf(a),second=BoundsOf(b);
                // Strict vertical separation excludes both a ray crossing and
                // boundary contact. Supported coordinate bounds also ensure
                // the skipped degree-two exact arithmetic cannot underflow.
                if (pointBounds.X.Valid()&&pointBounds.Y.Valid()&&first.X.Valid()&&first.Y.Valid()&&
                    second.X.Valid()&&second.Y.Valid()&&
                    (std::min(first.Y.Low,second.Y.Low)>pointBounds.Y.High||
                     std::max(first.Y.High,second.Y.High)<pointBounds.Y.Low)) continue;
            }
            const int side=OrientationSign(a,b,point,filtered);
            const int ay=DifferenceSign(a,point,false,filtered),by=DifferenceSign(b,point,false,filtered);
            if (!side&&Between(point,a,b,filtered)) return Location::Boundary;
            if (ay<=0&&by>0&&side>0) ++winding;
            else if (ay>0&&by<=0&&side<0) --winding;
        }
        return winding?Location::Inside:Location::Outside;
    }
    inline Location InMaterial(const Vector& point,const Geometry& polygon,bool filtered=false)
    {
        const auto outer=InRing(point,polygon.Rings.front(),filtered);
        if (outer!=Location::Inside) return outer;
        for (std::size_t i=1;i<polygon.Rings.size();++i)
        {
            const auto hole=InRing(point,polygon.Rings[i],filtered);
            if (hole==Location::Inside) return Location::Outside;
            if (hole!=Location::Outside) return hole;
        }
        return Location::Inside;
    }
    inline int MaterialSide(const std::vector<Vector>& ring,bool hole)
    {
        Number area;
        for (std::size_t i=1;i+1<ring.size();++i)
            area=area+Cross(ring[i]-ring.front(),ring[i+1]-ring.front());
        const int sign=area.Sign();
        if (!sign) throw ArithmeticUncertainty{};
        return hole?-sign:sign;
    }
    inline Verdict BoundaryCertificate(const Geometry& first,const Geometry& second,bool filtered=false)
    {
        for (std::size_t r=0;r<first.Rings.size();++r)
        {
            const auto& ring=first.Rings[r];
            const int side=MaterialSide(ring,r!=0);
            for (std::size_t i=0;i<ring.size();++i)
            {
                const auto& start=ring[i];const auto& end=ring[(i+1)%ring.size()];
                const auto direction=end-start;
                std::vector<Vector> cuts{start,end};
                for (const auto& other:second.Rings) for (const auto& point:other)
                    if (!OrientationSign(start,end,point,filtered)&&Between(point,start,end,filtered)) cuts.push_back(point);
                std::sort(cuts.begin(),cuts.end(),[&](const Vector& a,const Vector& b)
                { return ProjectionSign(a,b,end,start,filtered)<0; });
                for (std::size_t cut=1;cut<cuts.size();++cut)
                {
                    if (!ProjectionSign(cuts[cut],cuts[cut-1],end,start,filtered)) continue;
                    const Vector middle{(cuts[cut].X+cuts[cut-1].X)*Number(.5),
                        (cuts[cut].Y+cuts[cut-1].Y)*Number(.5)};
                    const auto position=InMaterial(middle,second,filtered);
                    if (position==Location::Inside) return Verdict::Blocked;
                    if (position==Location::Uncertain) return Verdict::Uncertain;
                    if (position!=Location::Boundary) continue;
                    bool matched=false;
                    for (std::size_t s=0;s<second.Rings.size();++s)
                    {
                        const auto& other=second.Rings[s];
                        for (std::size_t j=0;j<other.size();++j)
                        {
                            const auto& a=other[j];const auto& b=other[(j+1)%other.size()];
                            if (OrientationSign(a,b,middle,filtered)||!Between(middle,a,b,filtered)) continue;
                            const int alignment=Dot(direction,b-a).Sign();
                            if (!alignment) return Verdict::Uncertain;
                            if (side==MaterialSide(other,s!=0)*alignment) return Verdict::Blocked;
                            matched=true;
                        }
                    }
                    if (!matched) return Verdict::Uncertain;
                }
            }
        }
        return Verdict::Safe;
    }
    struct EdgeBounds
    {
        Interval X,Y;
        bool Valid=false;
    };
    inline std::vector<std::vector<EdgeBounds>> BoundsForEdges(const Geometry& geometry)
    {
        std::vector<std::vector<EdgeBounds>> result;
        result.reserve(geometry.Rings.size());
        for (const auto& ring:geometry.Rings)
        {
            std::vector<PointBounds> points;
            points.reserve(ring.size());
            for (const auto& point:ring)
            {
                auto bounds=BoundsOf(point);
                if (point.X.Terms.size()>4||point.Y.Terms.size()>4||
                    std::abs(bounds.X.Low)>0x1p130||std::abs(bounds.X.High)>0x1p130||
                    std::abs(bounds.Y.Low)>0x1p130||std::abs(bounds.Y.High)>0x1p130) bounds={};
                points.push_back(bounds);
            }
            auto& boxes=result.emplace_back();
            boxes.reserve(ring.size());
            for (std::size_t i=0;i<ring.size();++i)
            {
                const auto& first=points[i];const auto& second=points[(i+1)%ring.size()];
                if (!first.X.Valid()||!first.Y.Valid()||!second.X.Valid()||!second.Y.Valid())
                { boxes.push_back({});continue; }
                const auto dx=second.X-first.X,dy=second.Y-first.Y;
                const bool nonzero=dx.Low>0||dx.High<0||dy.Low>0||dy.High<0;
                boxes.push_back({{std::min(first.X.Low,second.X.Low),std::max(first.X.High,second.X.High)},
                    {std::min(first.Y.Low,second.Y.Low),std::max(first.Y.High,second.Y.High)},nonzero});
            }
        }
        return result;
    }
    inline bool BoundsClear(const EdgeBounds& a,const EdgeBounds& b,double radius)
    {
        return a.Valid&&b.Valid&&(SumBound(b.X.Low,-a.X.High,false)>radius||
            SumBound(a.X.Low,-b.X.High,false)>radius||SumBound(b.Y.Low,-a.Y.High,false)>radius||
            SumBound(a.Y.Low,-b.Y.High,false)>radius);
    }
    class EdgeIndex
    {
        struct Node
        {
            EdgeBounds Bounds;
            std::size_t Begin=0,End=0,Left=0,Right=0;
        };
        std::vector<std::size_t> Order;
        std::vector<Node> Nodes;
        const std::vector<EdgeBounds>* Edges;
        std::size_t Build(std::size_t begin,std::size_t end)
        {
            const auto index=Nodes.size();
            EdgeBounds bounds=(*Edges)[Order[begin]];
            for (std::size_t i=begin+1;i<end;++i)
            {
                const auto& edge=(*Edges)[Order[i]];
                bounds.Valid=bounds.Valid&&edge.Valid;
                bounds.X.Low=std::min(bounds.X.Low,edge.X.Low);bounds.X.High=std::max(bounds.X.High,edge.X.High);
                bounds.Y.Low=std::min(bounds.Y.Low,edge.Y.Low);bounds.Y.High=std::max(bounds.Y.High,edge.Y.High);
            }
            Nodes.push_back({bounds,begin,end,0,0});
            if (end-begin>8)
            {
                const bool xAxis=bounds.X.High-bounds.X.Low>=bounds.Y.High-bounds.Y.Low;
                const auto middle=begin+(end-begin)/2;
                std::nth_element(Order.begin()+begin,Order.begin()+middle,Order.begin()+end,[&](std::size_t a,std::size_t b)
                {
                    const auto& first=(*Edges)[a];const auto& second=(*Edges)[b];
                    return xAxis?first.X.Low+first.X.High<second.X.Low+second.X.High:
                        first.Y.Low+first.Y.High<second.Y.Low+second.Y.High;
                });
                const auto left=Build(begin,middle),right=Build(middle,end);
                Nodes[index].Left=left;Nodes[index].Right=right;
            }
            return index;
        }
        void Query(std::size_t index,const EdgeBounds& box,double radius,std::vector<std::size_t>& found) const
        {
            const auto& node=Nodes[index];
            if (BoundsClear(box,node.Bounds,radius)) return;
            if (node.Left)
            { Query(node.Left,box,radius,found);Query(node.Right,box,radius,found);return; }
            for (std::size_t i=node.Begin;i<node.End;++i)
                if (!BoundsClear(box,(*Edges)[Order[i]],radius)) found.push_back(Order[i]);
        }
    public:
        explicit EdgeIndex(const std::vector<EdgeBounds>& edges):Edges(&edges)
        {
            Order.reserve(edges.size());Nodes.reserve(edges.size()*2);
            for (std::size_t i=0;i<edges.size();++i) Order.push_back(i);
            if (edges.size()>16) Build(0,edges.size());
        }
        bool Ready() const { return !Nodes.empty(); }
        std::vector<std::size_t> Query(const EdgeBounds& box,double radius) const
        {
            if (Nodes.empty()) return Order;
            std::vector<std::size_t> found;
            Query(0,box,radius,found);
            // Exact predicates retain their original edge order, including
            // which unsupported arithmetic would be encountered first.
            std::sort(found.begin(),found.end());
            return found;
        }
    };
    inline bool CommonEdge(const Geometry& a,const Geometry& b,double minimumLength,bool filtered=false)
    {
        if (!a.Valid||!b.Valid||(!a.CommonEdgeRings.empty()&&a.CommonEdgeRings.size()!=a.Rings.size())||
            (!b.CommonEdgeRings.empty()&&b.CommonEdgeRings.size()!=b.Rings.size())) return false;
        filtered=filtered&&ArithmeticSupported();
        const bool cacheBounds=filtered&&!a.Rings.empty()&&!b.Rings.empty()&&
            a.Rings.front().size()+b.Rings.front().size()>64;
        const auto aBoxes=cacheBounds?BoundsForEdges(a):std::vector<std::vector<EdgeBounds>>{};
        const auto bBoxes=cacheBounds?BoundsForEdges(b):std::vector<std::vector<EdgeBounds>>{};
        std::vector<EdgeIndex> edgeIndices;
        if (cacheBounds)
        {
            edgeIndices.reserve(bBoxes.size());
            for (const auto& boxes:bBoxes) edgeIndices.emplace_back(boxes);
        }
        for (std::size_t r=0;r<a.Rings.size();++r)
        {
            if (!a.CommonEdgeRings.empty()&&!a.CommonEdgeRings[r]) continue;
            const auto& ar=a.Rings[r];
            for (std::size_t i=0;i<ar.size();++i) for (std::size_t s=0;s<b.Rings.size();++s)
            {
                if (!b.CommonEdgeRings.empty()&&!b.CommonEdgeRings[s]) continue;
                const auto& br=b.Rings[s];
                // Only overlapping conservative edge boxes can share a
                // positive-length segment. Unsupported bounds keep the full
                // original edge order and exact predicates below.
                const bool indexed=cacheBounds&&aBoxes[r][i].Valid&&edgeIndices[s].Ready();
                const auto candidates=indexed?edgeIndices[s].Query(aBoxes[r][i],0):std::vector<std::size_t>{};
                const auto candidateCount=indexed?candidates.size():br.size();
                for (std::size_t cursor=0;cursor<candidateCount;++cursor)
                {
                    const auto j=indexed?candidates[cursor]:cursor;
                    if (cacheBounds&&BoundsClear(aBoxes[r][i],bBoxes[s][j],0)) continue;
                    const auto& p=ar[i];const auto& q=ar[(i+1)%ar.size()];
                    const auto& u=br[j];const auto& v=br[(j+1)%br.size()];
                    const int first=OrientationSign(p,q,u,filtered),second=OrientationSign(p,q,v,filtered);
                    if (first||second) continue;
                    const auto direction=q-p;
                    const auto length2=Dot(direction,direction);
                    const auto start=Dot(u-p,direction),end=Dot(v-p,direction);
                    const auto low=AtLeast(start,end)?end:start,high=AtLeast(start,end)?start:end;
                    if (AtLeast(high,Number{})&&AtLeast(length2,low))
                    {
                        const auto clippedLow=AtLeast(low,Number{})?low:Number{};
                        const auto clippedHigh=AtLeast(length2,high)?high:length2;
                        const auto overlap=clippedHigh-clippedLow;
                        const auto lengthTest=overlap*overlap-Number(minimumLength)*Number(minimumLength)*length2;
                        if (overlap.Sign()>0&&lengthTest.Sign()>0) return true;
                    }
                }
            }
        }
        return false;
    }
    inline std::optional<bool> SeparatedHulls(const std::vector<Vector>& a,const std::vector<Vector>& b)
    {
        if (a.size()<3||b.size()<3) return {};
        try
        {
            // Both exact world hulls are counterclockwise. Their edge normals
            // rotate in that order, so each opposite support advances at most
            // one full circuit. An unsupported comparison retains legacy SAT.
            const auto sweep=[&](const std::vector<Vector>& edges,const std::vector<Vector>& points)->std::optional<bool>
            {
                std::size_t support=0,advances=0;
                Vector normal{edges.front().Y-edges[1].Y,edges[1].X-edges.front().X};
                const Vector zero;
                for (std::size_t i=1;i<points.size();++i)
                    if (ProjectionSign(points[i],points[support],normal,zero,true)>0) support=i;
                for (std::size_t edge=0;edge<edges.size();++edge)
                {
                    const auto& start=edges[edge];const auto& end=edges[(edge+1)%edges.size()];
                    normal={start.Y-end.Y,end.X-start.X};
                    while (ProjectionSign(points[(support+1)%points.size()],points[support],normal,zero,true)>=0)
                    {
                        if (++advances>points.size()) return {};
                        support=(support+1)%points.size();
                    }
                    if (OrientationSign(start,end,points[support],true)<=0) return true;
                }
                return false;
            };
            const auto first=sweep(a,b);
            if (!first||*first) return first;
            return sweep(b,a);
        }
        catch (const ArithmeticUncertainty&) { return {}; }
    }
    inline Verdict PairChecked(const Geometry& a,const Geometry& b,double gap,bool allowCommonEdges,
        double commonEdgeMinimumLength,bool filtered=false)
    {
        if (!a.Valid||!b.Valid) return Verdict::Uncertain;
        filtered=filtered&&ArithmeticSupported();
        Number required=Clearance(a,b,gap);
        if (filtered&&EnvelopesSeparated(a,b,required)) return Verdict::Safe;
        if (a.Radius>0&&b.Radius>0)
        {
            const auto radius=required+Number(a.Radius)+Number(b.Radius);
            return AtLeast(Dot(a.Center-b.Center,a.Center-b.Center),radius*radius)?Verdict::Safe:Verdict::Blocked;
        }
        if (a.Radius>0||b.Radius>0)
        {
            const auto& circle=a.Radius>0?a:b;
            const auto& polygon=a.Radius>0?b:a;
            const auto location=InMaterial(circle.Center,polygon,filtered);
            if (location==Location::Uncertain) return Verdict::Uncertain;
            if (location!=Location::Outside) return Verdict::Blocked;
            const auto radius=required+Number(circle.Radius);
            for (const auto& ring:polygon.Rings) for (std::size_t i=0;i<ring.size();++i)
                if (!PointSegmentClear(circle.Center,ring[i],ring[(i+1)%ring.size()],radius)) return Verdict::Blocked;
            return Verdict::Safe;
        }
        const auto& aBoundary=filtered&&!a.Hull.empty()?a.Hull:a.Rings.front();
        const auto& bBoundary=filtered&&!b.Hull.empty()?b.Hull:b.Rings.front();
        const auto hullSeparation=filtered&&!a.Hull.empty()&&!b.Hull.empty()?SeparatedHulls(a.Hull,b.Hull):std::optional<bool>{};
        const bool separated=hullSeparation?*hullSeparation:
            (SeparateOnEdges(aBoundary,aBoundary,bBoundary,filtered)||
             SeparateOnEdges(bBoundary,aBoundary,bBoundary,filtered));
        if (!separated&&a.Convex&&b.Convex) return Verdict::Blocked;
        if (allowCommonEdges&&CommonEdge(a,b,commonEdgeMinimumLength,filtered))
        {
            // This exact certificate concerns the represented final world
            // contours in Rings. For an arbitrary angle these are binary64
            // transformed vertices, not an interval proof about unrepresented
            // ideal trigonometric coordinates. A certified seam waives the
            // artificial padding for this polygon pair only. The complete
            // nominal material/topology checks below still have to pass;
            // finding a seam is never itself a non-overlap certificate.
            required=Number(0);
        }
        if (separated&&!required.Sign()) return Verdict::Safe;
        bool touch=false;
        const bool cacheBounds=filtered&&a.Rings.front().size()+b.Rings.front().size()>64;
        const auto aBoxes=cacheBounds?BoundsForEdges(a):std::vector<std::vector<EdgeBounds>>{};
        const auto bBoxes=cacheBounds?BoundsForEdges(b):std::vector<std::vector<EdgeBounds>>{};
        const auto radius=cacheBounds?CoordinateBounds(required):Interval{};
        const bool radiusSupported=cacheBounds&&required.Terms.size()<=4&&radius.Valid()&&radius.Low>=0&&radius.High<=0x1p130;
        std::vector<EdgeIndex> edgeIndices;
        if (radiusSupported)
        {
            edgeIndices.reserve(bBoxes.size());
            for (const auto& boxes:bBoxes) edgeIndices.emplace_back(boxes);
        }
        for (std::size_t r=0;r<a.Rings.size();++r)
        {
            const auto& ar=a.Rings[r];
            for (std::size_t i=0;i<ar.size();++i) for (std::size_t s=0;s<b.Rings.size();++s)
            {
                const auto& br=b.Rings[s];
                const bool indexed=radiusSupported&&aBoxes[r][i].Valid&&edgeIndices[s].Ready();
                const auto candidates=indexed?edgeIndices[s].Query(aBoxes[r][i],radius.High):std::vector<std::size_t>{};
                const auto candidateCount=indexed?candidates.size():br.size();
                for (std::size_t cursor=0;cursor<candidateCount;++cursor)
                {
                    const auto j=indexed?candidates[cursor]:cursor;
                    const auto& p=ar[i];const auto& q=ar[(i+1)%ar.size()];
                    const auto& u=br[j];const auto& v=br[(j+1)%br.size()];
                    if (radiusSupported&&BoundsClear(aBoxes[r][i],bBoxes[s][j],radius.High)) continue;
                    if (filtered&&SegmentDistanceClearBounds(p,q,u,v,required,true)) continue;
                    const int pu=OrientationSign(p,q,u,filtered),pv=OrientationSign(p,q,v,filtered);
                    const int up=OrientationSign(u,v,p,filtered),uq=OrientationSign(u,v,q,filtered);
                    if (pu*pv<0&&up*uq<0) return Verdict::Blocked;
                    const bool contact=(!pu&&Between(u,p,q,filtered))||(!pv&&Between(v,p,q,filtered))||
                        (!up&&Between(p,u,v,filtered))||(!uq&&Between(q,u,v,filtered));
                    touch=touch||contact;
                    if (required.Sign()>0)
                    {
                        if (contact) return Verdict::Blocked;
                        if (!PointSegmentClear(p,u,v,required)||!PointSegmentClear(q,u,v,required)||
                            !PointSegmentClear(u,p,q,required)||!PointSegmentClear(v,p,q,required)) return Verdict::Blocked;
                    }
                }
            }
        }
        if (separated) return Verdict::Safe;
        const auto supportedCoordinates=[](const Geometry& geometry)
        {
            for (const auto& ring:geometry.Rings) for (const auto& point:ring)
            {
                const auto bounds=BoundsOf(point);
                if (point.X.Terms.size()>4||point.Y.Terms.size()>4||!bounds.X.Valid()||!bounds.Y.Valid()) return false;
            }
            return true;
        };
        if (filtered&&supportedCoordinates(a)&&supportedCoordinates(b))
        {
            // Without boundary contact, each outer ring stays in one region
            // of the other material. With contact, split open edges below
            // certify containment and coincident material sides directly.
            if (!touch) for (const auto* first:{&a,&b})
            {
                const auto& second=first==&a?b:a;
                const auto position=InMaterial(first->Rings.front().front(),second,true);
                if (position==Location::Inside) return Verdict::Blocked;
                if (position==Location::Uncertain) return Verdict::Uncertain;
            }
        }
        else for (const auto* first:{&a,&b})
        {
            const auto& second=first==&a?b:a;
            for (const auto& ring:first->Rings) for (const auto& point:ring)
            {
                const auto position=InMaterial(point,second,filtered);
                if (position==Location::Inside) return Verdict::Blocked;
                if (position==Location::Uncertain) return Verdict::Uncertain;
            }
        }
        if (!touch) return Verdict::Safe;
        // Split each open edge at every exact boundary contact. Its interior
        // must remain outside the other material. Coincident intervals must
        // have opposite material sides, including inner-hole boundaries.
        const auto first=BoundaryCertificate(a,b,filtered);
        return first==Verdict::Safe?BoundaryCertificate(b,a,filtered):first;
    }
    inline Verdict Pair(const Geometry& a,const Geometry& b,double gap,bool allowCommonEdges,
        double commonEdgeMinimumLength)
    {
        try { return PairChecked(a,b,gap,allowCommonEdges,commonEdgeMinimumLength,true); }
        catch (const ArithmeticUncertainty&) { return Verdict::Uncertain; }
    }
    // Exact reference entry for differential tests and isolated measurements.
    // It retains the original expansion/topology path without interval filters.
    inline Verdict PairExact(const Geometry& a,const Geometry& b,double gap,bool allowCommonEdges,
        double commonEdgeMinimumLength)
    {
        try { return PairChecked(a,b,gap,allowCommonEdges,commonEdgeMinimumLength,false); }
        catch (const ArithmeticUncertainty&) { return Verdict::Uncertain; }
    }
    inline bool InsideChecked(const Geometry& geometry,double width,double height,double margin)
    {
        if (!geometry.Valid) throw ArithmeticUncertainty{};
        const Number low=Number(margin)+Number(geometry.Roundoff);
        const Number right=Number(width)-Number(margin)-Number(geometry.Roundoff);
        const Number top=Number(height)-Number(margin)-Number(geometry.Roundoff);
        const auto point=[&](const Vector& p,const Number& radius)
        {
            return AtLeast(p.X-radius,low)&&AtLeast(p.Y-radius,low)&&
                AtLeast(right,p.X+radius)&&AtLeast(top,p.Y+radius);
        };
        if (geometry.Radius>0) return point(geometry.Center,Number(geometry.Radius));
        for (const auto& ring:geometry.Rings) for (const auto& p:ring)
            if (!point(p,Number{})) return false;
        return geometry.Valid;
    }
    inline bool Inside(const Geometry& geometry,double width,double height,double margin)
    {
        try { return InsideChecked(geometry,width,height,margin); }
        catch (const ArithmeticUncertainty&) { return false; }
    }
    inline Verdict Containment(const Geometry& geometry,double width,double height,double margin)
    {
        try { return InsideChecked(geometry,width,height,margin)?Verdict::Safe:Verdict::Blocked; }
        catch (const ArithmeticUncertainty&) { return Verdict::Uncertain; }
    }
    inline bool Compatible(const Geometry& source,const Geometry& candidate)
    {
        if (!source.Valid||!candidate.Valid||source.Radius!=candidate.Radius||
            source.Roundoff!=candidate.Roundoff||source.Rings.size()!=candidate.Rings.size()||
            source.CommonEdgeRings!=candidate.CommonEdgeRings) return false;
        for (std::size_t ring=0;ring<source.Rings.size();++ring)
            if (source.Rings[ring].size()!=candidate.Rings[ring].size()) return false;
        return true;
    }
    inline bool Equal(const Number& first,const Number& second)
    { return (first-second).Sign()==0; }
    inline bool SameAxisCoordinatesChecked(const Geometry& source,const Geometry& candidate,bool xAxis)
    {
        if (!Compatible(source,candidate)) return false;
        const auto same=[&](const Vector& first,const Vector& second)
        { return Equal(xAxis?first.X:first.Y,xAxis?second.X:second.Y); };
        if (source.Radius>0) return same(source.Center,candidate.Center);
        for (std::size_t ring=0;ring<source.Rings.size();++ring)
            for (std::size_t point=0;point<source.Rings[ring].size();++point)
                if (!same(source.Rings[ring][point],candidate.Rings[ring][point])) return false;
        return true;
    }
    inline bool InsideAxisChecked(const Geometry& geometry,double extent,double margin,bool xAxis)
    {
        if (!geometry.Valid) throw ArithmeticUncertainty{};
        const Number low=Number(margin)+Number(geometry.Roundoff);
        const Number high=Number(extent)-Number(margin)-Number(geometry.Roundoff);
        const auto inside=[&](const Vector& point,const Number& radius)
        {
            const auto& coordinate=xAxis?point.X:point.Y;
            return AtLeast(coordinate-radius,low)&&AtLeast(high,coordinate+radius);
        };
        if (geometry.Radius>0) return inside(geometry.Center,Number(geometry.Radius));
        for (const auto& ring:geometry.Rings) for (const auto& point:ring)
            if (!inside(point,Number{})) return false;
        return true;
    }
    // The caller has validated source with the original Problem. An unchanged
    // orthogonal coordinate inherits only that source constraint. Every changed
    // coordinate still needs the full nominal margin and numerical enclosure.
    inline Verdict ContainmentWithSource(const Geometry& source,const Geometry& candidate,
        double width,double height,double margin,bool changedXAxis,bool& inherited)
    {
        inherited=false;
        const auto nominal=Containment(candidate,width,height,margin);
        if (nominal!=Verdict::Blocked) return nominal;
        try
        {
            if (!SameAxisCoordinatesChecked(source,candidate,!changedXAxis)||
                !InsideAxisChecked(candidate,changedXAxis?width:height,margin,changedXAxis)) return Verdict::Blocked;
            inherited=true;
            return Verdict::Safe;
        }
        catch (const ArithmeticUncertainty&) { return Verdict::Uncertain; }
    }
    inline Vector ReferencePoint(const Geometry& geometry)
    {
        if (geometry.Radius>0) return geometry.Center;
        if (geometry.Rings.empty()||geometry.Rings.front().empty()) throw ArithmeticUncertainty{};
        return geometry.Rings.front().front();
    }
    inline bool TranslationMatchesChecked(const Geometry& source,const Geometry& candidate,const Vector& delta)
    {
        if (!Compatible(source,candidate)) return false;
        const auto same=[&](const Vector& first,const Vector& second)
        {
            const auto actual=second-first;
            return Equal(actual.X,delta.X)&&Equal(actual.Y,delta.Y);
        };
        if (source.Radius>0) return same(source.Center,candidate.Center);
        for (std::size_t ring=0;ring<source.Rings.size();++ring)
            for (std::size_t point=0;point<source.Rings[ring].size();++point)
                if (!same(source.Rings[ring][point],candidate.Rings[ring][point])) return false;
        return true;
    }
    // A group tag or common requested distance does not prove rigid movement.
    // Every actual material point must have the same exact Number displacement.
    inline Verdict PairWithSource(const Geometry& sourceFirst,const Geometry& candidateFirst,
        const Geometry& sourceSecond,const Geometry& candidateSecond,double gap,bool allowCommonEdges,
        double commonEdgeMinimumLength,bool& inherited)
    {
        inherited=false;
        const auto nominal=Pair(candidateFirst,candidateSecond,gap,allowCommonEdges,commonEdgeMinimumLength);
        if (nominal!=Verdict::Blocked) return nominal;
        try
        {
            const auto delta=ReferencePoint(candidateFirst)-ReferencePoint(sourceFirst);
            if (!TranslationMatchesChecked(sourceFirst,candidateFirst,delta)||
                !TranslationMatchesChecked(sourceSecond,candidateSecond,delta)) return Verdict::Blocked;
            inherited=true;
            return Verdict::Safe;
        }
        catch (const ArithmeticUncertainty&) { return Verdict::Uncertain; }
    }
}
