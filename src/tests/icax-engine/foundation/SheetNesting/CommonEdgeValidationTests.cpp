#include "SheetNesting.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace iCAX::SheetNesting;

namespace
{
    void Require(bool value, const char* message)
    { if (!value) throw std::runtime_error(message); }

    Shape Rectangle(double width, double height)
    { Shape shape; shape.Outer = {{0,0},{width,0},{width,height},{0,height}}; return shape; }

    void MirroredGeometry()
    {
        Require(!Pose{}.Mirrored&&!Part{}.AllowMirror&&
            Options{}.OrientationProposals==OrientationProposalPolicy::AllowedReflections,
            "mirror support or proposal policy changed existing input defaults");
        const auto close=[](Point a,Point b)
        {return std::abs(a.X-b.X)<1e-10&&std::abs(a.Y-b.Y)<1e-10;};
        Require(close(Detail::Transform({3,7},{10,20,90,true}),{17,23}),
            "local reflection was applied after rotation");
        for(const bool parentMirror:{false,true})for(const bool childMirror:{false,true})
        for(const double parentAngle:{0.,90.,37.})for(const double childAngle:{-23.,180.})
        {
            const Pose parent{11,-4,parentAngle,parentMirror},child{-2,8,childAngle,childMirror};
            const Point point{3.25,-7.5};const auto combined=Detail::ComposePose(parent,child);
            Require(combined.Mirrored==(parentMirror!=childMirror)&&
                close(Detail::Transform(point,combined),Detail::Transform(Detail::Transform(point,child),parent)),
                "pose composition lost reflection parity or reversed the child's angle incorrectly");
            const auto world=Detail::Transform(point,parent);
            Require(close(Detail::InverseTransform(world,parent),point)&&
                close(Detail::Transform(world,Detail::InversePose(parent)),point),
                "mirrored pose inverse failed its original-coordinate round trip");
        }

        Part asymmetric;asymmetric.ID="asymmetric";asymmetric.AllowMirror=true;
        asymmetric.Geometry.Outer={{0,0},{8,3},{1,5}};
        const auto directions=Detail::CandidateOrientations(asymmetric,1e-7);
        Require(!directions.empty()&&directions.size()%2==0,"mirror direction enumeration lost an original direction");
        bool reflectedEdgeAlignment=false;
        const auto reflectedEdgeAngle=std::atan2(3.,8.)*180/Detail::Pi;
        for(std::size_t i=0;i<directions.size();i+=2)
        {
            Require(!directions[i].Mirrored&&directions[i+1].Mirrored&&directions[i].X==0&&directions[i].Y==0&&
                directions[i+1].X==0&&directions[i+1].Y==0&&
                directions[i+1].AngleDegrees==Detail::NormalizeAngle(-directions[i].AngleDegrees),
                "ordinary/mirrored directions were not interleaved using reflected contour edge angles");
            reflectedEdgeAlignment|=std::abs(directions[i+1].AngleDegrees-reflectedEdgeAngle)<1e-7;
        }
        Require(reflectedEdgeAlignment,"free mirrored directions omitted the reflected slanted edge alignment");
        const auto originalOnly=Detail::CandidateOrientations(asymmetric,1e-7,OrientationProposalPolicy::OriginalOnly);
        Require(originalOnly.size()*2==directions.size()&&asymmetric.AllowMirror,
            "original-only proposal policy changed source permission or omitted an original direction");
        for(std::size_t i=0;i<originalOnly.size();++i)
            Require(!originalOnly[i].Mirrored&&originalOnly[i].X==0&&originalOnly[i].Y==0&&
                originalOnly[i].AngleDegrees==directions[2*i].AngleDegrees,
                "original-only proposal policy changed the original direction order");
        Require(Detail::PoseAllowed(asymmetric,directions[1],1e-7),
            "a proposal restriction became a source mirror-permission restriction");
        asymmetric.FreeRotation=false;asymmetric.AllowedAnglesDegrees={17,200};
        const auto restricted=Detail::CandidateOrientations(asymmetric,1e-7);
        Require(restricted.size()==4&&restricted[0].AngleDegrees==17&&restricted[1].AngleDegrees==17&&
            restricted[2].AngleDegrees==200&&restricted[3].AngleDegrees==200,
            "reflection negated an explicitly restricted final rotation");
        const auto restrictedOriginal=Detail::CandidateOrientations(asymmetric,1e-7,OrientationProposalPolicy::OriginalOnly);
        Require(restrictedOriginal.size()==2&&!restrictedOriginal[0].Mirrored&&!restrictedOriginal[1].Mirrored&&
            restrictedOriginal[0].AngleDegrees==17&&restrictedOriginal[1].AngleDegrees==200&&asymmetric.AllowMirror,
            "original-only proposals changed the original finite-angle domain or mirror permission");
        Require(Detail::PoseAllowed(asymmetric,{0,0,17,true},1e-7)&&
            !Detail::PoseAllowed(asymmetric,{0,0,88,true},1e-7),"mirrored pose bypassed allowed final angles");
        asymmetric.AllowMirror=false;
        Require(!Detail::PoseAllowed(asymmetric,{0,0,17,true},1e-7)&&
            Detail::CandidateOrientations(asymmetric,1e-7).size()==2,
            "a non-mirrorable original exposed a mirrored pose");

        auto frame=Rectangle(10,10);frame.Holes={{{2,1},{5,1},{5,4},{2,4}}};frame.CommonEdgeRings={true,false};
        const auto reflected=Detail::ReflectedShape(frame);
        Require(frame.Holes[0][0].Y==1&&reflected.Holes[0][0].Y==-1&&reflected.Outer[2].Y==-10&&
            reflected.CommonEdgeRings==frame.CommonEdgeRings&&Detail::Area(reflected)==Detail::Area(frame),
            "private reflected preparation mutated the source, material area, holes or ring permissions");
        const Pose mirror{1,11,0,true};const auto plug=Rectangle(1,1);const Pose inserted{3.5,7.5,0};
        Require(!Detail::ShapesConflict(frame,mirror,plug,inserted,.2,1e-7,true)&&
            Detail::ShapesConflict(frame,{1,1,0},plug,inserted,.2,1e-7,true),
            "reflected hole material was evaluated at the original unreflected hole");
        Require(AxisSafety::Containment(AxisSafety::Make(frame,mirror),12,12,1)==AxisSafety::Verdict::Safe&&
            AxisSafety::Containment(AxisSafety::MakeExact(frame,mirror),12,12,1)==AxisSafety::Verdict::Safe&&
            AxisSafety::Pair(AxisSafety::Make(frame,mirror),AxisSafety::Make(plug,inserted),.2,true,1e-7)==AxisSafety::Verdict::Safe,
            "exact quarter-turn material safety ignored a reflected exterior or hole");
        const Pose seam{3,7,0};
        Require(!Detail::ShapesHaveCommonEdge(frame,mirror,plug,seam,1e-7)&&
            Detail::ShapesConflict(frame,mirror,plug,seam,.2,1e-7,true),
            "reflection turned an ineligible hole ring into a common seam");
        auto eligible=frame;eligible.CommonEdgeRings[1]=true;
        Require(Detail::ShapesHaveCommonEdge(eligible,mirror,plug,seam,1e-7)&&
            !Detail::ShapesConflict(eligible,mirror,plug,seam,.2,1e-7,true)&&
            AxisSafety::Pair(AxisSafety::Make(eligible,mirror),AxisSafety::Make(plug,seam),.2,true,1e-7)==AxisSafety::Verdict::Safe,
            "reflection lost an eligible exact hole seam");
        for(const double angle:{0.,90.,180.,270.,37.})for(const bool mirrored:{false,true})
        {
            const Pose pose{20,30,angle,mirrored};
            for(const auto geometry:{AxisSafety::Make(frame,pose),AxisSafety::MakeExact(frame,pose),AxisSafety::MakeBoundary(frame,pose)})
            {
                Require(geometry.Valid&&geometry.CommonEdgeRings==frame.CommonEdgeRings,"mirrored safety preparation lost metadata");
                for(std::size_t r=0;r<geometry.Rings.size();++r)
                {
                    const auto& source=r?frame.Holes[r-1]:frame.Outer;
                    for(std::size_t i=0;i<source.size();++i)
                    {
                        const auto expected=Detail::Transform(source[i],pose);
                        const auto x=AxisSafety::CoordinateBounds(geometry.Rings[r][i].X),y=AxisSafety::CoordinateBounds(geometry.Rings[r][i].Y);
                        Require(x.Valid()&&y.Valid()&&x.Low<=expected.X&&x.High>=expected.X&&
                            y.Low<=expected.Y&&y.High>=expected.Y,"nominal and exact mirrored world material disagree");
                    }
                }
            }
        }
        Shape circle;circle.Radius=1;circle.Center={2,3};const Pose circlePose{5,8,90,true};
        const auto circleBox=Detail::Bounds(circle,circlePose);
        const auto circleGeometry=AxisSafety::Make(circle,circlePose);
        Require(circleBox.X==7&&circleBox.Y==9&&circleBox.Width==2&&circleBox.Height==2&&
            AxisSafety::CoordinateBounds(circleGeometry.Center.X).Low==8&&
            AxisSafety::CoordinateBounds(circleGeometry.Center.Y).Low==10&&Detail::ReflectedShape(circle).Center.Y==-3,
            "mirrored analytic circle lost its source center");

        Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.AllowCommonEdges=true;
        Part host;host.ID="host";host.Geometry=frame;host.AllowMirror=true;host.FreeRotation=false;host.AllowedAnglesDegrees={0};
        Part insert;insert.ID="insert";insert.Geometry=plug;problem.Parts={host,insert};problem.Stocks={{"stock",12,12,1}};
        Result result;result.State=Status::Complete;result.Layouts={{"stock",0,12,12,
            {{"host",0,mirror,0,false},{"insert",0,inserted,0,false}},0}};
        Options proposalOptions;proposalOptions.OrientationProposals=OrientationProposalPolicy::OriginalOnly;
        const auto hostProposals=Detail::CandidateOrientations(problem.Parts[0],problem.Tolerance,proposalOptions.OrientationProposals);
        Require(hostProposals.size()==1&&!hostProposals[0].Mirrored&&problem.Parts[0].AllowMirror&&
            Detail::PoseAllowed(problem.Parts[0],mirror,problem.Tolerance),
            "constructor proposal policy rewrote original mirror eligibility");
        const auto checked=Validate(problem,result);
        Require(checked.Valid&&checked.Complete&&checked.PlacedCount==2,"final validation rejected actual reflected material and original identities");
        problem.Parts[0].AllowMirror=false;
        Require(!Validate(problem,result).Valid,"final validation accepted a mirrored placement without original permission");
        problem.Parts[0].AllowMirror=true;result.Layouts[0].Placements[0].Transform={1,1,0};
        Require(!Validate(problem,result).Valid,"final validation checked a reflected certificate after the output pose changed");
    }

    bool ValidPair(const Shape& first, Pose firstPose, const Shape& second, Pose secondPose,
        bool common = true, double tolerance = 1e-7)
    {
        Problem problem;
        problem.PartGap = .2;
        problem.Tolerance = tolerance;
        problem.AllowCommonEdges = common;
        problem.Stocks.push_back({"stock",1000,1000,1,1,"metal"});
        Part a; a.ID = "first"; a.Geometry = first; a.Material = "metal";
        Part b; b.ID = "second"; b.Geometry = second; b.Material = "metal";
        problem.Parts = {a,b};
        // Shift the entire pair into the stock, preserving all pair geometry.
        firstPose.X += 100; firstPose.Y += 100;
        secondPose.X += 100; secondPose.Y += 100;
        Result result; result.State = Status::Complete;
        result.Layouts.push_back({"stock",0,1000,1000,
            {{"first",0,firstPose,0,false},{"second",0,secondPose,0,false}},0});
        const auto checked = Validate(problem,result);
        Require(checked.PlacedCount == 2 && checked.Complete,"test fixture lost original inventory");
        return checked.Valid;
    }

    void RingEligibility()
    {
        auto frame = Rectangle(10,10);
        frame.Holes.push_back({{3,3},{7,3},{7,7},{3,7}});
        const auto originalOuter = frame.Outer;
        const auto originalHoles = frame.Holes;
        const double originalArea = Detail::Area(frame);
        frame.CommonEdgeRings = {true,false};
        const auto insert = Rectangle(2,2);

        // A chord of an imported curved hole remains a material boundary,
        // but it cannot waive the ordinary positive clearance.
        Require(!ValidPair(frame,{},insert,{3,3,0}),"an ineligible hole chord waived the positive gap");
        Require(!Detail::ShapesHaveCommonEdge(frame,{},insert,{3,3,0},1e-7),
            "the common-edge query exposed an ineligible hole chord");
        Require(!Detail::ShapesHaveCommonEdge(insert,{3,3,0},frame,{},1e-7),
            "ring eligibility depended on the order of the originals");
        Require(!ValidPair(frame,{},insert,{3.1,3.1,0}),"an ineligible hole accepted a sub-gap insertion");
        Require(ValidPair(frame,{},insert,{3.2,3.2,0}),"an ineligible hole rejected an ordinary .2-gap insertion");
        Require(ValidPair(frame,{},Rectangle(2,2),{10,0,0}),"disabling hole seams also disabled the eligible exterior seam");
        Require(Detail::ShapesHaveCommonEdge(frame,{},Rectangle(2,2),{10,0,0},1e-7),
            "the eligible exterior disappeared from the common-edge query");

        auto facetedHole = Rectangle(10,10);
        facetedHole.Holes = {{{3,4},{4,3},{6,3},{7,4},{7,6},{6,7},{4,7},{3,6}}};
        Require(ValidPair(facetedHole,{},insert,{4,3,0}),"legacy exact polygon-hole seam control is not valid");
        facetedHole.CommonEdgeRings = {true,false};
        Require(!ValidPair(facetedHole,{},insert,{4,3,0}),"a faceted curved-hole chord became an authorized straight seam");
        Require(ValidPair(facetedHole,{},Rectangle(1.6,1.6),{4.2,3.2,0}),
            "faceted curved-hole metadata changed ordinary positive-clearance containment");

        auto disabledInsert = insert;disabledInsert.CommonEdgeRings = {false};
        auto exactFrame = frame;exactFrame.CommonEdgeRings = {true,true};
        Require(ValidPair(exactFrame,{},insert,{3,3,0}),"an eligible exact polygon hole lost historical common-edge behavior");
        Require(!ValidPair(exactFrame,{},disabledInsert,{3,3,0}),"only the stationary ring's eligibility was checked");
        Require(!ValidPair(disabledInsert,{3,3,0},exactFrame,{}),"only the moving ring's eligibility was checked");
        Require(!ValidPair(Rectangle(2,2),{},disabledInsert,{2,0,0}),"an ineligible outer ring became a seam");

        Require(frame.Outer.size() == originalOuter.size() && frame.Holes.size() == originalHoles.size() &&
            Detail::Area(frame) == originalArea,"ring eligibility changed the original material");
        for (std::size_t i = 0; i < originalOuter.size(); ++i)
            Require(frame.Outer[i].X == originalOuter[i].X && frame.Outer[i].Y == originalOuter[i].Y,
                "ring eligibility changed an exterior vertex");
        for (std::size_t r = 0; r < originalHoles.size(); ++r)
        {
            Require(frame.Holes[r].size() == originalHoles[r].size(),"ring eligibility changed a hole vertex count");
            for (std::size_t i = 0; i < originalHoles[r].size(); ++i)
                Require(frame.Holes[r][i].X == originalHoles[r][i].X && frame.Holes[r][i].Y == originalHoles[r][i].Y,
                    "ring eligibility changed a hole vertex");
        }

        std::string error;
        Require(Detail::ShapeValid(frame,1e-7,error),"valid per-ring common-edge metadata was rejected");
        auto malformed = frame;malformed.CommonEdgeRings = {true};
        Require(!Detail::ShapeValid(malformed,1e-7,error),"missing hole eligibility was silently accepted");
        malformed.CommonEdgeRings = {true,false,true};
        Require(!Detail::ShapeValid(malformed,1e-7,error),"extra ring eligibility was silently accepted");
        Shape circle;circle.Radius = 2;circle.CommonEdgeRings = {false};
        Require(!Detail::ShapeValid(circle,1e-7,error),"an analytic circle accepted polygon-ring metadata");
        circle.CommonEdgeRings.clear();
        Require(Detail::ShapeValid(circle,1e-7,error),"ordinary analytic circles became invalid");
    }
}

int main() try
{
    Require(!Problem{}.AllowCommonEdges,"common edges changed existing problem defaults");
    const auto square = Rectangle(10,10);
    Require(ValidPair(square,{},square,{10,0,0}),"full common edge rejected");
    Require(ValidPair(square,{},square,{10,5,0}),"partial common edge rejected");
    Require(!ValidPair(square,{},square,{10,0,0},false),"ordinary positive gap bypassed without permission");
    Require(!ValidPair(square,{},square,{10,10,0}),"point contact misclassified as common edge");
    Require(!ValidPair(square,{},square,{10.1,0,0}),"ordinary sub-gap pair accepted");
    Require(ValidPair(square,{},square,{10.2,0,0}),"ordinary exact required gap rejected");
    Require(!ValidPair(square,{},square,{9.9,0,0}),"common boundary waived material overlap");
    Require(!ValidPair(square,{},square,{}),"coincident boundaries waived complete overlap");

    // Geometric tolerance must not turn a near parallel edge into an actual seam.
    Require(!ValidPair(square,{},square,{10.00001,0,0},true,.01),"near-collinear offset misclassified as common edge");
    Shape tilted = square; tilted.Outer[0].X += .00001; tilted.Outer[3].X -= .00001;
    Require(!ValidPair(square,{},tilted,{10.00002,0,0},true,.01),"near-parallel edge misclassified as common edge");
    Require(!ValidPair(square,{},square,{10,9.995,0},true,.01),"sub-tolerance shared length accepted");

    Shape triangle; triangle.Outer = {{0,0},{10,0},{0,10}};
    Require(ValidPair(triangle,{},triangle,{10,10,180}),"oblique common edge rejected");
    Require(!ValidPair(triangle,{},triangle,{10,10,180},false),"oblique shared edge bypassed ordinary gap");
    const double angle = 23.7;
    const auto offset = Detail::Transform({10,0},{0,0,angle});
    Require(ValidPair(square,{0,0,angle},square,{offset.X,offset.Y,angle}),"rotated coincident boundary roundoff rejected");

    Shape frame = square;
    frame.Holes.push_back({{3,3},{7,3},{7,7},{3,7}});
    const auto insert = Rectangle(2,2);
    Require(ValidPair(frame,{},insert,{3,3,0}),"hole boundary common edge rejected");
    Require(!ValidPair(frame,{},insert,{3,3,0},false),"hole boundary bypassed ordinary positive gap");
    Require(!ValidPair(frame,{},insert,{2.9,3,0}),"material overlap at hole boundary accepted");
    Shape channel;
    channel.Outer = {{0,0},{10,0},{10,10},{7,10},{7,3},{3,3},{3,10},{0,10}};
    Require(!ValidPair(channel,{},Rectangle(2,6),{3,2,0}),
        "a genuine seam elsewhere waived concave material overlap");

    Shape circle; circle.Radius = 2;
    Require(!ValidPair(circle,{},circle,{4,0,0}),"circle tangency misclassified as straight common edge");
    Require(ValidPair(circle,{},circle,{4.2,0,0}),"ordinary circle gap rejected");
    Require(!ValidPair(square,{},circle,{12,5,0}),"circle/polygon tangency waived positive gap");
    Require(!ValidPair(frame,{},circle,{5,5,0}),"circle tangency to a hole waived positive gap");
    Require(ValidPair(frame,{},circle,{5,5,0},true) ==
        ValidPair(frame,{},circle,{5,5,0},false),"circle behavior changed with common-edge setting");

    Require(Detail::ShapesConflict(square,{},square,{10,0,0},.2,1e-7),"internal default conflict behavior changed");
    Require(!Detail::ShapesConflict(square,{},square,{10,0,0},.2,1e-7,true),"shared geometry service missed common-edge permission");
    RingEligibility();
    MirroredGeometry();
    std::cout << "CommonEdgeValidationTests passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
