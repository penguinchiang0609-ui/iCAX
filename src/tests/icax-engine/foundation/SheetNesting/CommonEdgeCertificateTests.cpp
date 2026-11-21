#include "NestingAxisSafety.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace AS=iCAX::SheetNesting::AxisSafety;
namespace
{
constexpr double MinimumSeam=1e-7;
void Require(bool value,const char* message)
{if(!value)throw std::runtime_error(message);}
Shape Rectangle(double width=1,double height=1)
{Shape shape;shape.Outer={{0,0},{width,0},{width,height},{0,height}};return shape;}
void Expect(const AS::Geometry& a,const AS::Geometry& b,double gap,bool common,
    AS::Verdict expected,const char* message)
{
    Require(AS::Pair(a,b,gap,common,MinimumSeam)==expected,message);
    Require(AS::PairExact(a,b,gap,common,MinimumSeam)==expected,message);
}
void ExactNonOrthogonalSeam()
{
    const auto square=Rectangle();constexpr double angle=23.7;
    const auto offset=Detail::Transform({1,0},{0,0,angle});
    const auto first=AS::Make(square,{0,0,angle});
    const auto second=AS::Make(square,{offset.X,offset.Y,angle});
    Require(first.Roundoff>0&&second.Roundoff>0,"nonorthogonal fixture did not retain its conservative roundoff metadata");
    Require(AS::CommonEdge(first,second,MinimumSeam,false),"nonorthogonal seam is not exact in the represented binary64 world contours");
    Require(AS::CommonEdge(first,second,MinimumSeam,true),"filtered seam certificate disagrees with the exact reference");
    const auto firstPadding=first.Roundoff,secondPadding=second.Roundoff;
    Expect(first,second,.2,true,AS::Verdict::Safe,"exact nonorthogonal nominal seam was blocked by artificial pair padding");
    Expect(first,second,.2,false,AS::Verdict::Blocked,"ordinary positive clearance was waived without common-edge permission");
    Expect(first,second,0,false,AS::Verdict::Blocked,"non-common arbitrary-angle roundoff padding was globally removed");
    Require(first.Roundoff==firstPadding&&second.Roundoff==secondPadding,"seam handling changed global geometry roundoff");

    const auto pointOffset=Detail::Transform({1,1},{0,0,angle});
    const auto point=AS::Make(square,{pointOffset.X,pointOffset.Y,angle});
    Require(!AS::CommonEdge(first,point,MinimumSeam,false),"isolated nominal point contact became a straight seam");
    Expect(first,point,.2,true,AS::Verdict::Blocked,"point contact waived positive clearance");

    const auto near=AS::Make(square,{std::nextafter(offset.X,std::numeric_limits<double>::infinity()),offset.Y,angle});
    Require(!AS::CommonEdge(first,near,MinimumSeam,false),"one-ULP separated line received an exact seam certificate");
    Expect(first,near,.2,true,AS::Verdict::Blocked,"near-collinear sub-gap contact was accepted");
}
void ASeamCannotWaiveActualMaterialOverlap()
{
    const auto square=Rectangle();const auto first=AS::Make(square,{});
    const auto same=AS::Make(square,{});
    Require(AS::CommonEdge(first,same,MinimumSeam,false),"same-side overlap control lost its exact shared boundary");
    Expect(first,same,.2,true,AS::Verdict::Blocked,"same-side coincident materials passed on a seam alone");
    const auto inward=AS::Make(square,{std::nextafter(1.,0.),0,0});
    Expect(first,inward,.2,true,AS::Verdict::Blocked,"one-ULP material overlap was rounded away");

    // Both originals explicitly retain a midpoint on the intended seam. Their
    // lower half has identical final endpoints even after a nonorthogonal
    // transform. Moving only the other final endpoint inward by one ULP must
    // still fail; this is a changed represented material, not numerical slack.
    Shape a;a.Outer={{0,0},{1,0},{1,.5},{1,1},{0,1}};
    Shape b;b.Outer={{0,0},{1,0},{1,1},{0,1},{0,.5}};
    constexpr double angle=23.7;const auto offset=Detail::Transform({1,0},{0,0,angle});
    const auto rotatedA=AS::Make(a,{0,0,angle});auto rotatedB=AS::Make(b,{offset.X,offset.Y,angle});
    Require(AS::CommonEdge(rotatedA,rotatedB,MinimumSeam,false),"midpoint seam fixture has no exact certificate");
    Expect(rotatedA,rotatedB,.2,true,AS::Verdict::Safe,"unperturbed midpoint seam is not a valid nominal common boundary");
    const auto corner=Detail::Transform({0,1},{offset.X,offset.Y,angle});
    rotatedB.Rings[0][3]={AS::Number(std::nextafter(corner.X,-std::numeric_limits<double>::infinity())),
        AS::Number(std::nextafter(corner.Y,-std::numeric_limits<double>::infinity()))};
    // After changing a final point do not retain the old convex shortcut.
    // The full original-boundary topology path must reject this overlap.
    rotatedB.Convex=false;
    Require(AS::CommonEdge(rotatedA,rotatedB,MinimumSeam,false),"one-ULP overlap control lost the independent lower seam");
    Expect(rotatedA,rotatedB,.2,true,AS::Verdict::Blocked,"genuine seam elsewhere waived one-ULP final-world material overlap");

    Shape channel;channel.Outer={{0,0},{10,0},{10,10},{7,10},{7,3},{3,3},{3,8},{3,10},{0,10}};
    Shape crossing;crossing.Outer={{3,2},{5,2},{5,8},{3,8},{3,3}};
    const auto concave=AS::Make(channel,{0,0,angle}),overlap=AS::Make(crossing,{0,0,angle});
    Require(AS::CommonEdge(concave,overlap,MinimumSeam,false),"concave overlap control has no exact shared subedge");
    Expect(concave,overlap,.2,true,AS::Verdict::Blocked,"seam on one edge waived material overlap elsewhere in a concave original");
}
void HolesCirclesAndUncertaintyKeepTheirRules()
{
    auto frame=Rectangle(10,10);frame.Holes={{{3,3},{7,3},{7,7},{3,7}}};
    Shape plug;plug.Outer={{3,3},{7,3},{7,7},{3,7}};
    constexpr double angle=23.7;
    const auto host=AS::Make(frame,{0,0,angle}),inside=AS::Make(plug,{0,0,angle});
    Require(host.Roundoff>0&&inside.Roundoff>0&&AS::CommonEdge(host,inside,MinimumSeam,false),
        "transformed hole seam fixture lost its exact final world boundary");
    Expect(host,inside,.2,true,AS::Verdict::Safe,"opposite material sides of an exact hole boundary were rejected");
    Expect(host,inside,.2,false,AS::Verdict::Blocked,"hole contact bypassed ordinary clearance without permission");

    Shape circle;circle.Radius=1;
    const auto ca=AS::Make(circle,{0,0,angle}),cb=AS::Make(circle,{2,0,angle});
    Expect(ca,cb,.2,true,AS::Verdict::Blocked,"circle tangency received a straight common-edge waiver");
    Expect(ca,cb,0,true,AS::Verdict::Blocked,"circle arbitrary-angle roundoff metadata was bypassed");
    const auto circleNextToPolygon=AS::Make(circle,{2,.5,angle});
    const auto polygon=AS::Make(Rectangle(),{});
    Expect(polygon,circleNextToPolygon,.2,true,AS::Verdict::Blocked,"circle/polygon tangency received a seam waiver");

    auto uncertain=host;uncertain.Valid=false;
    Expect(uncertain,inside,.2,true,AS::Verdict::Uncertain,"failed geometry certification was treated as safe common-edge material");
}
}
int main()try
{
    ExactNonOrthogonalSeam();ASeamCannotWaiveActualMaterialOverlap();HolesCirclesAndUncertaintyKeepTheirRules();
    std::cout<<"CommonEdgeCertificateTests: exact nominal seams, strict topology and unchanged non-common padding passed\n";return 0;
}
catch(const std::exception& error)
{std::cerr<<"CommonEdgeCertificateTests: "<<error.what()<<'\n';return 1;}
