#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include "SheetNestingNfp.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace iCAX::SheetNesting;

namespace
{
    void Require(bool value,const char* message)
    { if (!value) throw std::runtime_error(message); }

    bool SamePoint(Point a,Point b)
    { return std::bit_cast<std::uint64_t>(a.X)==std::bit_cast<std::uint64_t>(b.X)&&
        std::bit_cast<std::uint64_t>(a.Y)==std::bit_cast<std::uint64_t>(b.Y); }

    bool SameRing(const std::vector<Point>& a,const std::vector<Point>& b)
    { return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),SamePoint); }

    std::vector<Point> Repeated(const std::vector<Point>& ring)
    {
        std::vector<Point> result;
        for (std::size_t i=0;i<ring.size();++i)
            for (std::size_t copy=0;copy<2+i%2;++copy) result.push_back(ring[i]);
        if (!ring.empty()) for (int copy=0;copy<3;++copy) result.push_back(ring.front());
        return result;
    }

    Shape Raw(Shape shape)
    {
        shape.Outer=Repeated(shape.Outer);
        for (auto& hole:shape.Holes) hole=Repeated(hole);
        return shape;
    }

    Shape Box(double width=12,double height=8)
    { Shape shape;shape.Outer={{0,0},{width,0},{width,height},{0,height}};return shape; }

    std::string Signature(const Problem& problem)
    {
        std::ostringstream out;
        const auto number=[&](double value) { out<<std::bit_cast<std::uint64_t>(value)<<','; };
        const auto ring=[&](const std::vector<Point>& points)
        {
            out<<points.size()<<'[';
            for (const auto point:points) { number(point.X);number(point.Y); }
            out<<']';
        };
        out<<problem.Parts.size()<<'|';
        for (const auto& part:problem.Parts)
        {
            out<<part.ID.size()<<':'<<part.ID<<part.Material.size()<<':'<<part.Material<<'|'
                <<part.Quantity<<'|'<<part.FreeRotation<<'|'<<part.AllowedAnglesDegrees.size()<<'|';
            for (const double angle:part.AllowedAnglesDegrees) number(angle);
            number(part.Geometry.Radius);number(part.Geometry.Center.X);number(part.Geometry.Center.Y);
            ring(part.Geometry.Outer);out<<part.Geometry.Holes.size()<<'|';
            for (const auto& hole:part.Geometry.Holes) ring(hole);
        }
        out<<problem.Stocks.size()<<'|';
        for (const auto& stock:problem.Stocks)
        {
            out<<stock.ID.size()<<':'<<stock.ID<<stock.Material.size()<<':'<<stock.Material<<'|'<<stock.Quantity<<'|';
            number(stock.Width);number(stock.Height);number(stock.Cost);
        }
        number(problem.PartGap);number(problem.EdgeMargin);number(problem.Tolerance);out<<problem.AllowCommonEdges;
        return out.str();
    }

    void SameNominalGeometry(const Shape& clean,const Shape& raw,Pose pose)
    {
        const auto first=AxisSafety::Make(clean,pose),second=AxisSafety::Make(raw,pose);
        Require(first.Valid&&second.Valid&&first.Convex==second.Convex&&first.Roundoff==second.Roundoff&&
            first.Rings.size()==second.Rings.size(),"raw ring changed nominal geometry metadata");
        for (std::size_t ring=0;ring<first.Rings.size();++ring)
        {
            Require(first.Rings[ring].size()==second.Rings[ring].size(),"nominal material retained duplicate zero edges");
            for (std::size_t point=0;point<first.Rings[ring].size();++point)
                Require(first.Rings[ring][point].X.Terms==second.Rings[ring][point].X.Terms&&
                    first.Rings[ring][point].Y.Terms==second.Rings[ring][point].Y.Terms,
                    "nominal material changed an original coordinate");
        }
    }

    void ExactRepresentation()
    {
        const auto clean=Box().Outer,raw=Repeated(clean),snapshot=raw;
        auto material=Detail::MaterialRing(raw);
        Require(SameRing(material,clean),"exact consecutive or closing duplicates were retained");
        material.front().X=100;
        Require(SameRing(raw,snapshot),"material copy mutated the original contour");
        Require(Detail::MaterialRing({}).empty(),"empty representation acquired a material point");
        Require(Detail::MaterialRing({{3,4},{3,4},{3,4}}).size()==1,"fully collapsed representation acquired an edge");
        auto rotated=clean;std::rotate(rotated.begin(),rotated.begin()+1,rotated.end());
        Require(SameRing(Detail::MaterialRing(Repeated(rotated)),rotated),"material view changed the original start point");
        std::reverse(rotated.begin(),rotated.end());
        Require(SameRing(Detail::MaterialRing(Repeated(rotated)),rotated),"material view changed winding or point order");
        std::cout<<"PASS exact material representation and immutable source\n";
    }

    void NativeAndNominalGeometry()
    {
        auto clean=Box(20,16);clean.Holes={{{4,4},{8,4},{8,8},{4,8}}};
        const auto raw=Raw(clean);std::string error;
        Require(Detail::ShapeValid(clean,1e-7,error)&&Detail::ShapeValid(raw,1e-7,error),
            "lossless duplicate representation was rejected");
        Require(Detail::Area(clean)==Detail::Area(raw),"duplicate representation changed material area");
        const auto other=Box(2,2),rawOther=Raw(other);
        bool collision=false,separation=false;
        for (const double angle:{0.,90.,180.,270.,37.})
        {
            const Pose pose{22,18,angle};
            const auto a=Detail::Bounds(clean,pose),b=Detail::Bounds(raw,pose);
            Require(a.X==b.X&&a.Y==b.Y&&a.Width==b.Width&&a.Height==b.Height,"duplicate representation changed bounds");
            Require(Detail::InsideSheet(clean,pose,60,50,1,1e-7)==Detail::InsideSheet(raw,pose,60,50,1,1e-7),
                "duplicate representation changed stock containment");
            SameNominalGeometry(clean,raw,pose);
        }
        for (const Point location:std::array<Point,5>{{{1,1},{5,5},{3,4},{20,2},{22,2}}})
            for (const double gap:{0.,.25}) for (const bool common:{false,true})
            {
                const Pose pose{location.X,location.Y,0};
                const bool expected=Detail::ShapesConflict(clean,{},other,pose,gap,1e-7,common);
                Require(expected==Detail::ShapesConflict(raw,{},rawOther,pose,gap,1e-7,common),
                    "duplicate representation changed native material collision or gap");
                Require(Detail::ShapesHaveCommonEdge(clean,{},other,pose,1e-7)==
                    Detail::ShapesHaveCommonEdge(raw,{},rawOther,pose,1e-7),"duplicate representation changed shared boundaries");
                collision=collision||expected;separation=separation||!expected;
            }
        Require(collision&&separation,"material comparison did not exercise occupied material and an empty hole");
        std::cout<<"PASS outer and hole area, containment, collision, gap and nominal poses\n";
    }

    void DistinctAndInvalidContours()
    {
        const double near=1e-10;
        const std::vector<Point> subnormalClosing{{0,0},{10,0},{10,10},{0,10},{0,std::nextafter(0.,1.)}};
        Require(SameRing(Detail::MaterialRing(subnormalClosing),subnormalClosing),
            "distinct subnormal closing coordinate was coalesced");
        Shape shortEdge;shortEdge.Outer={{0,0},{near,0},{10,0},{10,10},{0,10}};
        Require(SameRing(Detail::MaterialRing(Repeated(shortEdge.Outer)),shortEdge.Outer),"distinct nearby vertices were coalesced");
        std::string error;
        Require(Detail::ShapeValid(shortEdge,1e-7,error)&&Detail::ShapeValid(Raw(shortEdge),1e-7,error),
            "a nonzero short edge was rejected by the spacing tolerance");
        Shape nearClosed;nearClosed.Outer={{0,0},{10,0},{10,10},{0,10},{0,near}};
        Require(SameRing(Detail::MaterialRing(nearClosed.Outer),nearClosed.Outer),"distinct nearby closing point was coalesced");
        Require(Detail::ShapeValid(nearClosed,1e-7,error)&&Detail::ShapeValid(Raw(nearClosed),1e-7,error),
            "a distinct nearby closing point was rejected");
        Shape nearCollinear;nearCollinear.Outer={{0,0},{5,near},{10,0},{10,10},{0,10}};
        Require(Detail::MaterialRing(nearCollinear.Outer).size()==5&&Detail::ShapeValid(nearCollinear,1e-7,error),
            "distinct near-collinear point was removed or reclassified as invalid");
        Require(Detail::Area(nearCollinear)!=100&&Detail::Area(Raw(nearCollinear))==Detail::Area(nearCollinear),
            "near-collinear original material was approximated");
        const std::array<std::vector<Point>,3> invalid{{
            {{0,0},{10,10},{0,10},{10,0}},
            {{0,0},{8,0},{8,8},{0,0},{0,8}},
            {{0,0},{8,0},{4,0},{8,8},{0,8}}}};
        for (const auto& ring:invalid)
        {
            Require(SameRing(Detail::MaterialRing(Repeated(ring)),ring),"self-contact or reversing vertices were removed");
            Shape shape;shape.Outer=ring;
            Require(!Detail::ShapeValid(shape,1e-7,error)&&!Detail::ShapeValid(Raw(shape),1e-7,error),
                "genuine self-crossing, nonadjacent contact or adjacent reversal was accepted");
        }
        Shape uncertain;uncertain.Outer=subnormalClosing;
        Require(!Detail::ShapeValid(uncertain,1e-7,error)&&error.find("arithmetic")!=std::string::npos,
            "uncertified subnormal topology was accepted or its arithmetic failure was hidden");
        std::cout<<"PASS distinct near points, exact topology and explicit arithmetic uncertainty\n";
    }

    void TinyStepTopology()
    {
        const std::array<std::vector<Point>,2> rings{{
            {{3,0},{27.1992,0},{27.1992,-1.77636e-14},{29.9793,-1.77636e-14},{30,10},{3,10}},
            {{-1.46549e-13,78.0579},{-1.46994e-13,77.254},{0,77.254},{0,75.1972},
                {10,75.1972},{10,80},{-1.46549e-13,80}}}};
        for (const auto& ring:rings)
        {
            Shape shape;shape.Outer=ring;
            const auto snapshot=shape.Outer;
            std::string error;
            Require(Detail::ShapeValid(shape,1e-7,error)&&Detail::ShapeValid(Raw(shape),1e-7,error),
                "a nonzero tiny step was classified as a self-contact");
            auto touching=shape;
            touching.Outer.insert(touching.Outer.begin()+4,shape.Outer[1]);
            Require(!Detail::ShapeValid(touching,1e-7,error)&&!Detail::ShapeValid(Raw(touching),1e-7,error),
                "an exact nonadjacent contact beside a tiny step was accepted");
            auto reversing=shape;
            reversing.Outer.insert(reversing.Outer.begin()+2,shape.Outer.front());
            Require(!Detail::ShapeValid(reversing,1e-7,error),"an adjacent reversal beside a tiny step was accepted");
            NfpCache cache;
            const auto moving=Box(2,2);
            const auto first=cache.Get(shape,0,moving,0);
            const auto warm=cache.Get(Raw(shape),0,moving,0);
            Require(first.State==NfpStatus::Ready&&warm.State==NfpStatus::Ready&&warm.CacheHit&&first.Region==warm.Region,
                "tiny-step original material disagreed between NFP cold and warm queries");
            auto snapped=shape;
            for (auto& point:snapped.Outer)
            {
                if (std::abs(point.X)<1e-12) point.X=0;
                if (std::abs(point.Y)<1e-12) point.Y=0;
            }
            const auto distinct=cache.Get(snapped,0,moving,0);
            Require(distinct.State==NfpStatus::Ready&&!distinct.CacheHit&&distinct.Region!=first.Region,
                "a nearby but distinct contour reused the original tiny-step NFP entry");
            Require(SameRing(shape.Outer,snapshot),"topology or NFP processing modified a tiny-step source contour");
        }
        Shape crossing;
        crossing.Outer={{0,0},{30,0},{27.1992,-1.77636e-14},{27.1992,1.77636e-14},{30,10},{0,10}};
        std::string error;
        Require(!Detail::ShapeValid(crossing,1e-7,error)&&!Detail::ShapeValid(Raw(crossing),1e-7,error),
            "a true crossing on a tiny step was accepted");
        std::cout<<"PASS tiny-step original topology, invalid contacts and distinct cold/warm NFP keys\n";
    }

    void CacheAndOriginalOrders()
    {
        const auto clean=Box(),raw=Raw(clean),moving=Box(3,2),rawMoving=Raw(moving);
        for (const bool rawFirst:{false,true})
        {
            NfpCache cache;
            const auto first=cache.Get(rawFirst?raw:clean,90,rawFirst?rawMoving:moving,0);
            const auto second=cache.Get(rawFirst?clean:raw,90,rawFirst?moving:rawMoving,0);
            Require(first.State==NfpStatus::Ready&&second.State==NfpStatus::Ready&&second.CacheHit&&
                first.Region==second.Region,"cold and warm NFP paths disagree on exact duplicate representation");
            auto invalid=clean;invalid.Outer.insert(invalid.Outer.begin()+2,invalid.Outer.front());
            Require(!cache.Prepare(invalid),"nonadjacent contact hit a valid normalized cache entry");
        }
        Problem original;original.Tolerance=1e-7;original.Stocks={{"sheet",40,30,1,1,"m"}};
        Part rectangle;rectangle.ID="rectangle";rectangle.Material="m";rectangle.Quantity=3;
        rectangle.FreeRotation=false;rectangle.AllowedAnglesDegrees={90};rectangle.Geometry=clean;
        Part triangle;triangle.ID="triangle";triangle.Material="m";triangle.Quantity=2;
        triangle.FreeRotation=false;triangle.AllowedAnglesDegrees={0,180};triangle.Geometry.Outer={{0,0},{6,0},{0,4}};
        original.Parts={rectangle,triangle};auto repeated=original;
        for (auto& part:repeated.Parts) part.Geometry=Raw(part.Geometry);
        const auto originalSignature=Signature(original),repeatedSignature=Signature(repeated);
        Require(Detail::CandidateAngles(original.Parts[0],1e-7)==Detail::CandidateAngles(repeated.Parts[0],1e-7),
            "original finite angle domain changed");
        auto free=original.Parts[0],rawFree=repeated.Parts[0];free.FreeRotation=rawFree.FreeRotation=true;
        Require(Detail::CandidateAngles(free,1e-7)==Detail::CandidateAngles(rawFree,1e-7),
            "exact duplicates changed free-rotation candidate angles");
        Options options;options.TimeLimitSeconds=3;options.MaxPlacementAttempts=20000;
        const auto registry=DefaultStrategies();
        const auto first=RunJob({"clean","rules.contact-large",original,options},registry);
        const auto second=RunJob({"raw","rules.contact-large",repeated,options},registry);
        Require(first.Checked.Valid&&second.Checked.Valid&&first.Checked.Complete&&second.Checked.Complete&&
            first.Checked.PlacedCount==5&&second.Checked.PlacedCount==5,"normal job changed original demand completeness");
        Require(first.Checked.MaterialArea==second.Checked.MaterialArea,"normal job changed original material area");
        for (const auto& layout:second.Output.Layouts) for (const auto& placement:layout.Placements)
        {
            const auto part=std::find_if(repeated.Parts.begin(),repeated.Parts.end(),[&](const Part& item){return item.ID==placement.PartID;});
            Require(part!=repeated.Parts.end()&&placement.Instance<part->Quantity&&
                Detail::AngleAllowed(*part,placement.Transform.AngleDegrees,repeated.Tolerance),
                "normal job changed an original identity, quantity or angle domain");
        }
        Require(Signature(original)==originalSignature&&Signature(repeated)==repeatedSignature,
            "validation, NFP preparation or a normal job mutated the source problem");
        std::cout<<"PASS NFP cold/warm cache, normal entry, original angles, quantities and immutable problem\n";
    }
}

int main()try
{
    ExactRepresentation();NativeAndNominalGeometry();DistinctAndInvalidContours();TinyStepTopology();CacheAndOriginalOrders();
    std::cout<<"Contour representation tests passed\n";
    return 0;
}
catch (const std::exception& error)
{ std::cerr<<error.what()<<'\n';return 1; }
