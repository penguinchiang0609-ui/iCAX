#include "polygon_geometry.hpp"
#include "clipper2/clipper.h"
#include <iostream>
#include <iomanip>

using namespace study;
namespace {
size_t checks=0,cases=0,placedTotal=0,baselineTotal=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
Polygon rectangle(double w,double h,double x=0,double y=0){return{{x,y},{x+w,y},{x+w,y+h},{x,y+h}};}
struct Stock {icax::iplane::geometry::Board board;double width,height;Point shift;};
Stock stockAround(const Polygon &source,double angle,bool flip=false,double padding=.01,
                  std::vector<Polygon> holes={}){
    namespace clip=Clipper2Lib;auto rotated=transform(source,{},Pose{angle,flip});clip::PathD path;
    for(const auto p:rotated)path.push_back({p.x,p.y});
    const auto expanded=clip::InflatePaths(clip::PathsD{path},padding,clip::JoinType::Miter,clip::EndType::Polygon,2.,8);
    check(expanded.size()==1,"Expanded real test stock must have one material exterior");Polygon outer;
    for(const auto p:expanded.front())outer.push_back({p.x,p.y});if(area(outer)<0)std::reverse(outer.begin(),outer.end());
    auto b=bounds(outer);Point shift{-b.x0,-b.y0};for(auto &p:outer){p.x+=shift.x;p.y+=shift.y;}
    for(auto &hole:holes){hole=transform(hole,{},Pose{angle,flip});for(auto &p:hole){p.x+=shift.x;p.y+=shift.y;}if(area(hole)<0)std::reverse(hole.begin(),hole.end());}
    return{{outer,holes,0},b.x1-b.x0,b.y1-b.y0,shift};
}
PartInput part(Polygon outer,std::vector<Pose> poses,int quantity=1,std::vector<Polygon> holes={}){
    PartInput p;p.polygon=std::move(outer);p.poses=std::move(poses);p.quantity=quantity;p.holes=std::move(holes);return p;
}
void independentGeometry(const Input &input,const icax::iplane::geometry::Board &board,const Output &output){
    namespace clip=Clipper2Lib;constexpr long double scale=1e8L;
    const auto region=[&](const Polygon &outer,const std::vector<Polygon> &holes,long double angle,bool flip,Point pivot,Point position){
        const long double radians=angle*3.141592653589793238462643383279502884L/180;
        const long double c=std::cos(radians),s=std::sin(radians);clip::Paths64 result;
        const auto add=[&](const Polygon &ring,bool hole){clip::Path64 path;for(const auto p:ring){
            const long double x=p.x-pivot.x,y=p.y-pivot.y;
            path.emplace_back(std::llround((c*x-s*y+position.x)*scale),
                std::llround(((flip?-1:1)*(s*x+c*y)+position.y)*scale));}
            if(clip::IsPositive(path)==hole)std::reverse(path.begin(),path.end());result.push_back(std::move(path));};
        add(outer,false);for(const auto &hole:holes)add(hole,true);return result;
    };
    const auto toRegion=[&](const Polygon &ring){return region(ring,{},0,false,{},{});};
    const auto segment=[&](clip::Point64 p,clip::Point64 a,clip::Point64 b){
        const long double dx=b.x-a.x,dy=b.y-a.y,x=p.x-a.x,y=p.y-a.y,norm=dx*dx+dy*dy;
        const long double t=norm?std::clamp((x*dx+y*dy)/norm,0.L,1.L):0;
        return double(std::hypot(x-t*dx,y-t*dy)/scale);
    };
    const auto distance=[&](const clip::Paths64 &a,const clip::Paths64 &b){double result=INFINITY;
        for(const auto &one:a)for(const auto &two:b)for(size_t i=0;i<one.size();++i)for(size_t j=0;j<two.size();++j){
            const auto p=one[i],q=one[(i+1)%one.size()],r=two[j],s=two[(j+1)%two.size()];
            result=std::min({result,segment(p,r,s),segment(q,r,s),segment(r,p,q),segment(s,p,q)});
        }return result;
    };
    const auto stock=region(board.outer,board.holes,0,false,{},{});std::vector<clip::Paths64> previous;
    for(const auto &p:output.placements){const auto &source=input.parts[p.group];
        const auto actual=region(source.polygon,source.holes,p.solvedAngle,source.poses[p.poseIndex].flip,source.pivot,{p.exactX,p.exactY});
        check(std::abs(clip::Area(clip::Difference(actual,stock,clip::FillRule::NonZero)))/1e16<=1e-6,"Independent true stock/hole containment at actual continuous angle");
        check(distance(actual,toRegion(board.outer))+1e-6>=board.margin,"Independent actual outer border margin");
        for(size_t h=0;h<board.holes.size();++h){const double margin=board.holeMargins.empty()?board.margin:board.holeMargins[h];
            check(distance(actual,toRegion(board.holes[h]))+1e-6>=margin,"Independent separate stock-hole margin");}
        for(const auto &old:previous){check(std::abs(clip::Area(clip::Intersect(actual,old,clip::FillRule::NonZero)))/1e16<=1e-6,"Independent signed material overlap including moving voids");
            check(distance(actual,old)+1e-6>=input.gap,"Independent positive Euclidean part gap");}
        previous.push_back(actual);
    }
}
icax::iplane::geometry::RunResult run(const char *name,const Stock &stock,std::vector<PartInput> parts,
                             size_t expected,double seconds=2.,double gap=0,bool baseline=false){
    Input input;input.parts=std::move(parts);input.width=stock.width;input.height=stock.height;input.gap=gap;
    Budget budget(seconds);const auto start=std::chrono::steady_clock::now();
    const auto result=icax::iplane::geometry::nest(input,stock.board,budget);
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    check(result.output.placements.size()==expected,"Actual finite/continuous material placement count");
    check(icax::iplane::geometry::validate(input,stock.board,result.output).valid,"Unchanged strict material, hole, gap and margin checks");
    independentGeometry(input,stock.board,result.output);
    for(const auto &p:result.output.placements){const auto &range=input.parts.at(p.group).poses.at(p.poseIndex);
        check(std::isfinite(p.solvedAngle)&&p.solvedAngle>=range.angle&&p.solvedAngle<=range.upper,"Actual angle must belong to its exact source interval");
    }
    check(elapsed<seconds+.3,"Continuous search must share the parent wall-clock budget");
    std::cout<<std::setprecision(17)<<"{\"case\":\""<<name<<"\",\"placed\":"<<result.output.placements.size()
             <<",\"evaluations\":"<<result.continuousEvaluations<<",\"refinements\":"<<result.continuousRefinements
             <<",\"intervalMinimizations\":"<<result.continuousIntervalMinimizations<<",\"subdivisions\":"<<result.continuousSubdivisions
             <<",\"seconds\":"<<elapsed<<",\"angles\":[";
    for(size_t n=0;n<result.output.placements.size();++n){if(n)std::cout<<',';std::cout<<result.output.placements[n].solvedAngle;}std::cout<<"]}\n";
    ++cases;if(baseline)baselineTotal+=expected;else placedTotal+=expected;return result;
}
}
int main(){try{
    const double target=31.234567;
    const auto narrow=stockAround(rectangle(10,2),target,false,.005);
    run("narrow-irregular-finite-baseline",narrow,{part(rectangle(10,2),{{20},{30},{40}})},0,2.,0,true);
    const auto actual=run("narrow-irregular-continuous",narrow,{part(rectangle(10,2),{{20,false,40}})},1);
    check(std::abs(actual.output.placements[0].solvedAngle-target)<.1,"Real boundary-derived nonpreset contact angle");
    run("lower-range-endpoint",narrow,{part(rectangle(10,2),{{target,false,target+5}})},1);
    run("upper-range-endpoint",narrow,{part(rectangle(10,2),{{target-5,false,target}})},1);
    const auto crossing=stockAround(rectangle(10,2),2.345678,false,.005);
    const auto cross=run("unwrapped-cross-zero",crossing,{part(rectangle(10,2),{{350,false,370}})},1);
    check(cross.output.placements[0].solvedAngle>360.,"Cross-zero actual angle retains its unwrapped range identity");

    const Polygon L{{0,0},{8,0},{8,2},{2,2},{2,6},{0,6}};const auto mirrorStock=stockAround(L,target,true,.005);
    run("wrong-mirror-rejected",mirrorStock,{part(L,{{20,false,40}})},0);
    run("mirrored-continuous",mirrorStock,{part(L,{{20,true,40}})},1);
    auto shifted=part(rectangle(10,2),{{20,false,40}});
    for(auto &p:shifted.polygon){p.x+=7.;p.y-=3.;}shifted.pivot={8.,-2.};
    run("shifted-source-pivot",narrow,{shifted},1);

    const double numericAngle=33.123456,rad=numericAngle*pi/180.;
    const double w=8*std::cos(rad)+2*std::sin(rad)+.00001,h=8*std::sin(rad)+2*std::cos(rad)+.00001;
    Stock axis{{rectangle(w,h),{},0},w,h,{}};
    run("noncontact-finite-baseline",axis,{part(rectangle(8,2),{{0},{30},{40},{80}})},0,2.,0,true);
    const auto refined=run("noncontact-interval-minimum",axis,{part(rectangle(8,2),{{0,false,80}})},1);
    check(std::abs(refined.output.placements[0].solvedAngle-numericAngle)<.01,"Continuous minimization must find a nonparallel nonpreset narrow angular window");
    check(refined.continuousIntervalMinimizations>0,"Narrow noncontact feasibility must execute actual interval minimization");

    const Polygon moving=rectangle(12,4);const auto holeStock=stockAround(moving,13.98765,false,.01,{rectangle(1,1,5,1.5)});
    run("actual-stock-hole-in-moving-void",holeStock,{part(moving,{{5,false,25}},1,{rectangle(4,2,4,1)})},1);
    auto slot=transform(rectangle(10.01,2.01),{},Pose{23.4567});const auto sb=bounds(slot);
    for(auto &p:slot){p.x+=5-sb.x0;p.y+=5-sb.y0;}
    Stock frameStock{{rectangle(20,20),{},0},20,20,{}};
    const auto frame=part(rectangle(20,20),{{0}},1,{slot});
    run("placed-hole-finite-baseline",frameStock,{frame,part(rectangle(10,2),{{10},{20},{40}})},1,2.,0,true);
    run("placed-hole-continuous-cache",frameStock,{frame,part(rectangle(10,2),{{10,false,40}})},2);
    Stock spaced{{{{0,0},{20,0},{20,14},{14,14},{14,20},{0,20}},{rectangle(2,2,9,8)},.2},20,20,{}};
    run("continuous-positive-gap-and-all-margins",spaced,{part(rectangle(4,2),{{15,false,45}},3)},3,2.,.3);

    Stock independent{{rectangle(10,10),{rectangle(2,2,4,4)},1,{0}},10,10,{}};
    run("independent-hole-offset",independent,{part(rectangle(8,8),{{0}},1,{rectangle(2,2,3,3)})},1);
    auto uniform=independent;uniform.board.holeMargins.clear();
    run("uniform-hole-margin-control",uniform,{part(rectangle(8,8),{{0}},1,{rectangle(2,2,3,3)})},0);

    Input limited;limited.width=limited.height=100.;limited.parts={part(rectangle(2,1),{{0,false,360}},200)};
    icax::iplane::geometry::Board limitedBoard{rectangle(100,100),{},0};Budget time(.03);
    const auto start=std::chrono::steady_clock::now();const auto kept=icax::iplane::geometry::nest(limited,limitedBoard,time);
    check(!kept.output.placements.empty()&&kept.stopped,"Timeout must retain the real feasible candidates already found");
    check(icax::iplane::geometry::validate(limited,limitedBoard,kept.output).valid,"Timeout checkpoint remains strict-material valid");
    check(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<.4,"Timeout checkpoint retains bounded execution");++cases;
    independentGeometry(limited,limitedBoard,kept.output);
    Budget expired(0.);const auto empty=icax::iplane::geometry::nest(limited,limitedBoard,expired);
    check(empty.stopped&&empty.output.placements.empty(),"Expired budget publishes no fabricated placements");++cases;

    Input validateInput;validateInput.width=validateInput.height=100.;validateInput.parts={part(rectangle(2,1),{{20,false,40}})};
    Output wrong;wrong.placements.push_back({0,0,0,0,0,10,10,19.9999999999});
    check(!icax::iplane::geometry::validate(validateInput,limitedBoard,wrong).valid,"Final validator rejects an out-of-range actual angle");
    wrong.placements[0].solvedAngle=std::numeric_limits<double>::quiet_NaN();
    check(!icax::iplane::geometry::validate(validateInput,limitedBoard,wrong).valid,"Continuous placements require an explicit actual angle");
    for(const Pose invalid:{Pose{40,false,20},Pose{0,false,361}}){auto input=validateInput;input.parts[0].poses={invalid};Budget budget(1.);bool rejected=false;
        try{icax::iplane::geometry::nest(input,limitedBoard,budget);}catch(const Failure &e){rejected=e.code==PN2S_INVALID;}check(rejected,"Invalid angular domain is rejected");}
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"checks\":"<<checks<<",\"placed\":"<<placedTotal<<",\"finiteBaselinePlaced\":"<<baselineTotal<<"}\n";return 0;
}catch(const std::exception &e){std::cerr<<"continuous case "<<cases<<": "<<e.what()<<'\n';return 1;}}
