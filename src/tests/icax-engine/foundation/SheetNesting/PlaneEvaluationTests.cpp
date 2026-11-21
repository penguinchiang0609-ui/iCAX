#include "SheetNestingPlaneEvaluation.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message)
{if(!value)throw std::runtime_error(message);}
bool Near(double value,double expected,double tolerance=1e-12)
{return std::abs(value-expected)<=tolerance*std::max(1.,std::abs(expected));}

void SourceWorkedExample()
{
    // doc/Plane/43, section 6: the unselected (8,7) pitch must not affect K.
    const std::vector<PlaneArrayPitchTemplate> templates{
        {10,6,{{8,7},{10,6}}},{12,5,{{12,5}}}};
    const auto single=ComputePlaneArrayAkward(PlaneArrayContainerKind::Single,templates);
    Require(single.Valid&&single.HasEligibleTemplate&&single.SelectedTemplate==0&&single.K==17672,
        "source worked example did not select its container weight");
    const auto score=EvaluatePlaneArray(12,30,24,40,30,single.K);
    Require(score.Valid&&Near(score.E,.014776548935583802)&&score.P&&Near(*score.P,261.13117278963693),
        "source worked E/P values or parentheses differ");
    const auto pair=ComputePlaneArrayAkward(PlaneArrayContainerKind::SameSpecificationPair,
        {{12,5,{{12,5}}}});
    Require(pair.Valid&&pair.K==20910.125&&pair.SelectedTemplate==0,
        "separate single/pair containers shared their weights");
    const auto unweighted=EvaluatePlaneArray(12,30,24,40,30);
    Require(unweighted.Valid&&!unweighted.P&&Near(unweighted.E,score.E),
        "an absent container weight was invented");
    // A filled square has no residual term: N=4, X=Y=W=H=2.
    const auto full=EvaluatePlaneArray(4,2,2,2,2,7);
    Require(full.Valid&&full.E==1&&full.P&&*full.P==7,
        "exact boundary filling changed the original count score");
    // A partial pair extension has its actual count, not 2*ceil(N/2).
    const auto odd=EvaluatePlaneArray(5,2,2,2,2);
    Require(odd.Valid&&odd.E==1.25,"partial pair extension rounded the original-piece count");
}

void LastPitchAndSequentialTolerance()
{
    const auto first=ComputePlaneArrayAkward(PlaneArrayContainerKind::Single,{{1,1,{{0,0},{1,1}}}});
    const auto reversed=ComputePlaneArrayAkward(PlaneArrayContainerKind::Single,{{1,1,{{1,1},{0,0}}}});
    Require(first.Valid&&first.K==4.5&&reversed.Valid&&reversed.K==2.25,
        "weight did not read the final stored pitch only");
    // With L=B=1 and pitch=(t,0), K=2.25+.375*t^2.
    // Adjacent values differ by less than 1e-4, but the endpoints differ by
    // more. The original sequential update is deliberately order dependent.
    const double low=2.25,mid=2.250075,high=2.25015;
    const double tMid=std::sqrt(.0002),tHigh=.02;
    const auto descending=ComputePlaneArrayAkward(PlaneArrayContainerKind::Single,
        {{1,1,{{tHigh,0}}},{1,1,{{tMid,0}}},{1,1,{{0,0}}}});
    const auto changedOrder=ComputePlaneArrayAkward(PlaneArrayContainerKind::Single,
        {{1,1,{{tMid,0}}},{1,1,{{0,0}}},{1,1,{{tHigh,0}}}});
    Require(descending.Valid&&descending.SelectedTemplate==2&&Near(descending.K,low),
        "sequential tolerance did not accept an endpoint improvement");
    Require(changedOrder.Valid&&changedOrder.SelectedTemplate==0&&Near(changedOrder.K,mid)&&high>changedOrder.K,
        "sequential tolerance was replaced by an exact or order-independent minimum");
    const auto skipped=ComputePlaneArrayAkward(PlaneArrayContainerKind::Single,
        {{std::numeric_limits<double>::quiet_NaN(),0,{}},{1,1,{{0,0}}}});
    Require(skipped.Valid&&skipped.SelectedTemplate==1&&skipped.K==2.25,
        "a template without a pitch contributed a weight");
    for(const auto kind:{PlaneArrayContainerKind::Single,PlaneArrayContainerKind::SameSpecificationPair})
    {
        const auto empty=ComputePlaneArrayAkward(kind,{});
        const auto noPitch=ComputePlaneArrayAkward(kind,{{1,1,{}}});
        Require(empty.Valid&&!empty.HasEligibleTemplate&&empty.K==std::numeric_limits<double>::max()&&
            noPitch.Valid&&!noPitch.HasEligibleTemplate&&noPitch.K==std::numeric_limits<double>::max(),
            "empty source weight was confused with the default uninitialized zero cache");
    }
}

void DomainAndScoreBoundaries()
{
    const auto nan=std::numeric_limits<double>::quiet_NaN();
    const auto inf=std::numeric_limits<double>::infinity();
    Require(!EvaluatePlaneArray(0,1,1,2,2).Valid&&!EvaluatePlaneArray(1,0,1,2,2).Valid&&
        !EvaluatePlaneArray(1,1,1,0,2).Valid&&!EvaluatePlaneArray(1,1,1,2,-1).Valid,
        "empty or zero-area source score was accepted");
    Require(!EvaluatePlaneArray(1,2.00000001,1,2,2).Valid&&!EvaluatePlaneArray(1,1,3,2,2).Valid&&
        !EvaluatePlaneArray(1,nan,1,2,2).Valid&&!EvaluatePlaneArray(1,1,1,inf,2).Valid&&
        !EvaluatePlaneArray(1,1,1,2,2,nan).Valid,
        "nonfinite or out-of-region scoring operands were accepted");
    Require(!ComputePlaneArrayAkward(PlaneArrayContainerKind::Single,{{nan,1,{{1,1}}}}).Valid&&
        !ComputePlaneArrayAkward(PlaneArrayContainerKind::Single,{{1,1,{{inf,1}}}}).Valid&&
        !ComputePlaneArrayAkward(static_cast<PlaneArrayContainerKind>(7),{}).Valid,
        "unsupported or nonfinite container input was accepted");
    Require(!ComputePlaneArrayAkward(PlaneArrayContainerKind::Single,{{1e100,1e100,{{1,1}}}}).Valid&&
        !EvaluatePlaneArray(100,1,1,1,1,std::numeric_limits<double>::max()).Valid,
        "source scoring arithmetic overflow was accepted");
    // Source section 7: a smaller occupied width near the region boundary can
    // have a lower E. This heuristic must not masquerade as material density.
    const auto touching=EvaluatePlaneArray(1,100,100,100,100);
    const auto smaller=EvaluatePlaneArray(1,99.99,100,100,100);
    Require(touching.Valid&&smaller.Valid&&smaller.E<touching.E,
        "source residual exponent was silently changed to an envelope-density score");
}

Part SpaceRectangle(const char* id,double width,double height)
{
    Part part;part.ID=id;part.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};return part;
}
Layout SpaceBoard()
{
    Layout layout;layout.StockID="sheet";layout.Width=10;layout.Height=1000000;return layout;
}
void LocalSpaceDomainAndFragments()
{
    Problem problem;problem.Tolerance=0;problem.Parts={SpaceRectangle("bar",2,10)};
    const auto board=SpaceBoard();PlanePlacementSpaceEvaluator evaluator(problem,board,{0,0,10,10});
    const auto empty=evaluator.Evaluate({});
    Require(evaluator.Ready()&&empty.Valid&&empty.FreeComponents==1&&empty.FreeArea==100&&
        empty.LargestFreeArea==100&&empty.LargestFreeRegularArea==100&&empty.FragmentFreeArea==0&&
        empty.FreePerimeter==40&&empty.Attachment==0&&!empty.CircleProxyUsed,
        "local-space scoring used the million-unit stock instead of the explicit consumed domain");
    const auto edge=evaluator.Evaluate({{"bar",0,{0,0,0}}});
    const auto middle=evaluator.Evaluate({{"bar",0,{4,0,0}}});
    Require(edge.Valid&&middle.Valid&&edge.FreeArea==80&&middle.FreeArea==80&&
        edge.LargestFreeArea==80&&edge.LargestFreeRegularArea==80&&edge.FragmentFreeArea==0&&edge.FreeComponents==1&&
        middle.LargestFreeArea==40&&middle.LargestFreeRegularArea==40&&middle.FragmentFreeArea==40&&middle.FreeComponents==2&&
        edge.FreePerimeter==36&&middle.FreePerimeter==56,
        "equal material was not distinguished by the actual large remaining component and fragments");
    const auto clipped=evaluator.Evaluate({{"bar",0,{-1,0,0}}});
    Require(clipped.Valid&&clipped.FreeArea==90&&clipped.LargestFreeRegularArea==90,
        "local-space material was not clipped to the explicit evaluation domain");
    // Evaluate is read-only: a previous candidate must never become fixed.
    const auto again=evaluator.Evaluate({});
    Require(again.Valid&&again.FreeArea==empty.FreeArea&&again.FreePerimeter==empty.FreePerimeter,
        "candidate evaluation mutated the fixed-layout cache");
}

void LocalSpaceHolesConcavityAndAttachment()
{
    Problem problem;problem.Tolerance=0;
    auto frame=SpaceRectangle("frame",10,10);frame.Geometry.Holes={{{2,2},{8,2},{8,8},{2,8}}};
    problem.Parts={frame,SpaceRectangle("square",2,2)};
    auto board=SpaceBoard();board.Placements={{"frame",0,{}}};
    PlanePlacementSpaceEvaluator evaluator(problem,board,{0,0,10,10});
    const auto hole=evaluator.Evaluate({});
    Require(hole.Valid&&hole.FreeArea==36&&hole.LargestFreeArea==36&&hole.LargestFreeRegularArea==36&&
        hole.FreeComponents==1&&hole.FreePerimeter==24,
        "an original hole was filled as solid material by local-space scoring");
    const auto center=evaluator.Evaluate({{"square",0,{4,4,0}}});
    const auto attached=evaluator.Evaluate({{"square",0,{2,4,0}}});
    Require(center.Valid&&attached.Valid&&center.FreeArea==32&&attached.FreeArea==32&&
        Near(center.LargestFreeRegularArea,32.*32/36)&&Near(attached.LargestFreeRegularArea,32.*32/36)&&
        center.FreePerimeter==32&&attached.FreePerimeter==28&&center.Attachment==0&&attached.Attachment==2,
        "hole boundaries were omitted from free perimeter or actual material attachment");

    auto nestedFrame=SpaceRectangle("nested",8,8);nestedFrame.Geometry.Holes={{{2,2},{6,2},{6,6},{2,6}}};
    Problem nested;nested.Tolerance=0;nested.Parts={nestedFrame};
    auto nestedBoard=SpaceBoard();nestedBoard.Placements={{"nested",0,{2,2,0}}};
    const auto islands=PlanePlacementSpaceEvaluator(nested,nestedBoard,{0,0,12,12}).Evaluate({});
    Require(islands.Valid&&islands.FreeComponents==2&&islands.FreeArea==96&&islands.LargestFreeArea==80&&
        islands.FragmentFreeArea==16&&Near(islands.LargestFreeRegularArea,80.*80/144)&&islands.FreePerimeter==96,
        "a free island inside a material hole was merged into the outer connected component");

    Part concave;concave.ID="L";concave.Geometry.Outer={{0,0},{4,0},{4,1},{1,1},{1,4},{0,4}};
    Problem recess;recess.Tolerance=0;recess.Parts={concave};
    const auto contour=PlanePlacementSpaceEvaluator(recess,SpaceBoard(),{0,0,10,10}).Evaluate({{"L",0,{}}});
    Require(contour.Valid&&contour.FreeArea==93&&contour.LargestFreeArea==93,
        "the actual concave material was replaced by its AABB in local-space scoring");
}

void LocalSpaceContactBufferAndCandidateUnion()
{
    Problem problem;problem.Tolerance=0;problem.Parts={SpaceRectangle("part",2,4)};
    auto board=SpaceBoard();board.Placements={{"part",0,{0,0,0}}};
    PlanePlacementSpaceEvaluator evaluator(problem,board,{0,0,10,10});
    const auto touching=evaluator.Evaluate({{"part",1,{2,0,0}}});
    const auto detached=evaluator.Evaluate({{"part",1,{4,0,0}}});
    Require(touching.Valid&&detached.Valid&&touching.FreeArea==84&&detached.FreeArea==84&&
        touching.Attachment==4&&detached.Attachment==0,
        "material attachment was not measured against the fixed buffered contour");
    const auto ownContact=PlanePlacementSpaceEvaluator(problem,SpaceBoard(),{0,0,10,10}).Evaluate(
        {{"part",0,{0,0,0}},{"part",1,{2,0,0}}});
    const auto overlap=PlanePlacementSpaceEvaluator(problem,SpaceBoard(),{0,0,10,10}).Evaluate(
        {{"part",0,{0,0,0}},{"part",1,{1,0,0}}});
    Require(ownContact.Valid&&ownContact.Attachment==0&&ownContact.FreeArea==84&&overlap.Valid&&overlap.FreeArea==88,
        "candidate members used XOR material or counted their own contact as attachment to the fixed layout");

    Problem buffered;buffered.PartGap=.2;buffered.Tolerance=1e-7;buffered.Parts={SpaceRectangle("square",2,2)};
    auto bufferedBoard=SpaceBoard();bufferedBoard.Placements={{"square",0,{2,2,0}}};
    PlanePlacementSpaceEvaluator withGap(buffered,bufferedBoard,{0,0,10,10});
    const auto atGap=withGap.Evaluate({{"square",1,{4.2,2,0}}});
    const auto separate=withGap.Evaluate({{"square",1,{5,2,0}}});
    std::cout<<std::setprecision(17)<<"local-space round buffer="<<(buffered.PartGap/2+4*buffered.Tolerance)
        <<" at-gap(valid="<<atGap.Valid<<", attachment="<<atGap.Attachment<<", free="<<atGap.FreeArea
        <<", diagnostic="<<atGap.Diagnostic<<") separated(valid="<<separate.Valid
        <<", attachment="<<separate.Attachment<<", free="<<separate.FreeArea
        <<", diagnostic="<<separate.Diagnostic<<")\n";
    Require(atGap.Valid&&separate.Valid&&atGap.Attachment>0&&separate.Attachment==0&&
        atGap.FreeArea<92&&separate.FreeArea<92,
        "half-gap plus four-tolerance buffering was omitted from remaining space or attachment");
}

void LocalSpaceTransformsCircleAndFixedSnapshot()
{
    Problem problem;problem.Tolerance=0;
    Part triangle;triangle.ID="triangle";triangle.Geometry.Outer={{0,0},{2,0},{0,1}};
    Part unused;unused.ID="unplaced-poison";unused.Quantity=std::numeric_limits<std::size_t>::max();
    unused.Geometry.Outer={{std::numeric_limits<double>::quiet_NaN(),0}};
    problem.Parts={triangle,unused};
    const auto mirrored=PlanePlacementSpaceEvaluator(problem,SpaceBoard(),{0,0,10,10}).Evaluate(
        {{"triangle",0,{5,5,90,true}}});
    Require(mirrored.Valid&&mirrored.FreeArea==99&&Near(mirrored.FreePerimeter,43+std::sqrt(5.))&&
        !mirrored.CircleProxyUsed,
        "local-space scoring changed the stored mirror/rotation or inspected an unplaced original");
    PlanePlacementSpaceEvaluator edge(problem,SpaceBoard(),{0,0,10,10});
    const auto reflectedEdge=edge.Evaluate({{"triangle",0,{0,0,90,true}}});
    const auto ordinaryEdge=edge.Evaluate({{"triangle",0,{0,0,90,false}}});
    const auto translatedAway=edge.Evaluate({{"triangle",0,{12,12,0}}});
    Require(reflectedEdge.Valid&&ordinaryEdge.Valid&&translatedAway.Valid&&
        reflectedEdge.FreeArea==99&&ordinaryEdge.FreeArea==100&&translatedAway.FreeArea==100,
        "stored translation, angle or reflection was ignored before domain clipping");
    auto board=SpaceBoard();board.Placements={{"triangle",0,{5,5,90,true}}};
    PlanePlacementSpaceEvaluator snapshot(problem,board,{0,0,10,10});
    board.Placements.clear();
    const auto cached=snapshot.Evaluate({});
    Require(cached.Valid&&cached.FreeArea==mirrored.FreeArea&&Near(cached.FreePerimeter,mirrored.FreePerimeter),
        "fixed actual material was read again from a subsequently changed layout");

    Part disk;disk.ID="disk";disk.Geometry.Radius=1;disk.Geometry.Center={0,0};
    Problem circles;circles.Tolerance=0;circles.Parts={disk};
    const auto circle=PlanePlacementSpaceEvaluator(circles,SpaceBoard(),{0,0,10,10}).Evaluate({{"disk",0,{5,5,0}}});
    const double pi=std::acos(-1.);
    Require(circle.Valid&&circle.CircleProxyUsed&&Near(circle.FreeArea,100-32*std::sin(2*pi/64),1e-8)&&
        Near(circle.FreePerimeter,40+128*std::sin(pi/64),1e-8),
        "the circle's explicitly marked 64-edge scoring proxy is inconsistent");
    const auto turned=PlanePlacementSpaceEvaluator(circles,SpaceBoard(),{0,0,10,10}).Evaluate({{"disk",0,{5,5,37,true}}});
    Require(turned.Valid&&turned.FreeArea==circle.FreeArea&&turned.FreePerimeter==circle.FreePerimeter,
        "a disk's irrelevant orientation changed its local-space proxy");
}

void LocalSpaceFailureIsNotCertification()
{
    Problem problem;problem.Tolerance=0;problem.Parts={SpaceRectangle("part",2,2)};
    const auto nan=std::numeric_limits<double>::quiet_NaN();
    for(const auto domain:std::vector<Rect>{{0,0,0,10},{0,0,10,-1},{nan,0,10,10},{0,0,1e100,10}})
    {
        const PlanePlacementSpaceEvaluator invalid(problem,SpaceBoard(),domain);
        const auto result=invalid.Evaluate({});
        Require(!invalid.Ready()&&!result.Valid&&!result.Diagnostic.empty(),
            "an invalid or unrepresentable local-space domain produced a usable score");
    }
    PlanePlacementSpaceEvaluator evaluator(problem,SpaceBoard(),{0,0,10,10});
    const auto unknown=evaluator.Evaluate({{"unknown",0,{}}});
    const auto nonfinite=evaluator.Evaluate({{"part",0,{nan,0,0}}});
    Require(!unknown.Valid&&!unknown.Diagnostic.empty()&&!nonfinite.Valid&&!nonfinite.Diagnostic.empty()&&
        evaluator.Evaluate({}).Valid,
        "a malformed candidate escaped the scoring failure result or poisoned the fixed cache");
    // This helper is deliberately not a legality service. The hard caller gate
    // must reject overlap; the proxy simply measures the union, without XOR.
    const auto notCertified=evaluator.Evaluate({{"part",0,{}},{"part",1,{}}});
    Require(notCertified.Valid&&notCertified.FreeArea==96,
        "the local-space heuristic unexpectedly became an inventory or collision validator");
}
}
int main()try
{
    SourceWorkedExample();LastPitchAndSequentialTolerance();DomainAndScoreBoundaries();
    LocalSpaceDomainAndFragments();LocalSpaceHolesConcavityAndAttachment();LocalSpaceContactBufferAndCandidateUnion();
    LocalSpaceTransformsCircleAndFixedSnapshot();LocalSpaceFailureIsNotCertification();
    std::cout<<"PASS source array evaluation and fixed-state local material space, holes, fragments, contact, proxies and failures\n";return 0;
}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
