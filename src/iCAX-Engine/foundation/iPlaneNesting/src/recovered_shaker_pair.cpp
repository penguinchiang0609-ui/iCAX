#include "recovered_shaker_pair.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>
namespace study::recovered {
namespace {
double neg(double v){return std::bit_cast<double>(std::bit_cast<uint64_t>(v)^0x8000000000000000ULL);}
double evaluate(const ShakerPlane &p,ShakerPoint q){return (p.b*q[1]+p.a*q[0])+p.c;}
ShakerPoint normalized(ShakerPoint v){const double length=std::sqrt(v[1]*v[1]+v[0]*v[0]);if(length<1e-9)return {1,0};return {v[0]*(1/length),v[1]*(1/length)};}
const ShakerBoundary *adjacent(const std::vector<ShakerBoundary> &b,int32_t n){
    if(n==-1)return nullptr;if(n<0||size_t(n)>=b.size())throw std::out_of_range("Invalid Shaker convolution adjacency");return &b[size_t(n)];
}
}
ShakerPairResult shakerPairRows(ShakerProblem &problem,LinearModel &model,ShakerPart &i,ShakerPart &j,
    ShakerPairOptions options,const ShakerPairSource &source,double &maximumViolation){
    if(!source.excluded||!source.convolution)throw std::invalid_argument("Shaker pair geometry providers are required");
    ShakerPairResult result;
    for(;;){
        if(source.excluded(problem.rotation))return result;
        const auto mode=options.geometryModeEnabled?(options.hasGeometryMode?options.geometryMode:(problem.rotation?0:1)):0;
        auto boundary=source.convolution(0,mode);
        const ShakerPoint relative{j.position[0]-i.position[0],j.position[1]-i.position[1]};
        const double radius=options.radius;
        const double x0=relative[0]-radius,x1=relative[0]+radius,y0=relative[1]-radius,y1=relative[1]+radius;
        const std::array<double,4> box{x0<=x1?x0:x1,y0<=y1?y0:y1,x1<=x0?x0:x1,y1<=y0?y0:y1};
        std::vector<int32_t> candidates;
        for(size_t n=0;n<boundary.size();++n){auto &b=boundary[n];bool both=false;b.active=b.direction&&shakerSegmentWindow(b.start,b.end,box,0,both);if(b.active)candidates.push_back(int32_t(n));}
        const double scaled=options.sceneSpan*1e-6,tolerance=scaled<=options.toleranceLimit?scaled:options.toleranceLimit;
        result.inside=shakerInside(boundary,relative,tolerance,mode!=0);
        if(result.inside&&options.hasAdmissionTolerance&&candidates.empty()&&shakerInside(boundary,relative,options.admissionTolerance,mode!=0)){result.status=2;return result;}
        for(auto &b:boundary)b.processed=false;
        for(auto n:candidates)shakerBoundaryDistance(boundary[size_t(n)],relative);
        shakerBoundaryOrder(boundary,candidates);auto cell=shakerRectangleCell(box);
        const double epsilon=0.001<=radius*0.25?0.001:radius*0.25;
        bool retry=false;
        for(auto n:candidates){auto &b=boundary[size_t(n)];b.processed=true;
            const auto previous=adjacent(boundary,b.previous),next=adjacent(boundary,b.next);
            const bool fp=previous&&previous->direction,fn=next&&next->direction;
            bool visible=true;
            for(const auto &node:cell){const double hs=evaluate(node.plane,b.start),he=evaluate(node.plane,b.end);
                if((hs<neg(epsilon)&&(he<neg(epsilon)||(he<epsilon&&fn)))||(hs<epsilon&&fp&&he<neg(epsilon))){visible=false;break;}}
            if(!visible||cell.empty())continue;
            ShakerPlane plane=shakerLine({b.end[1]-b.start[1],b.start[0]-b.end[0]},b.end);
            plane.reference={(b.end[0]+b.start[0])*0.5,(b.end[1]+b.start[1])*0.5};
            const double dx=b.start[0]-b.end[0],dy=b.start[1]-b.end[1],length=std::sqrt(dy*dy+dx*dx),rho=result.inside?0:0.001,u=(rho+rho)*1.4142;
            const double ratio=u/(length-u);plane.kappa=length<=u||1<ratio?1:ratio;
            const auto neighbor=b.feature==1?previous:b.feature==2?next:nullptr;
            if(neighbor&&neighbor->active&&0.001<b.distance){
                const auto endpoint=b.feature==1?b.start:b.end;
                const ShakerPoint radial{relative[0]-endpoint[0],relative[1]-endpoint[1]};
                bool replace=false;
                if(options.policy21)replace=!result.inside;
                else if(!neighbor->processed){
                    bool contained=true;for(const auto &node:cell)if(evaluate(node.plane,endpoint)<0){contained=false;break;}
                    if(contained){
                        const auto opposite=b.feature==1?b.end:b.start,neighborEnd=b.feature==1?neighbor->start:neighbor->end;
                        const double ux=opposite[0]-endpoint[0],uy=opposite[1]-endpoint[1],vx=neighborEnd[0]-endpoint[0],vy=neighborEnd[1]-endpoint[1];
                        double z;
                        if(b.feature==1){z=radial[1]*ux-radial[0]*uy;const double other=radial[1]*vx-radial[0]*vy;if(0<=(ux*vy-uy*vx)*z)z=neg(other);}
                        else{z=radial[1]*vx-radial[0]*vy;const double other=ux*radial[1]-uy*radial[0];if(0<=(uy*vx-ux*vy)*z)z=neg(other);}
                        replace=z<0;
                    }
                }
                if(replace)plane=shakerLine(radial,endpoint);
            }
            if((i.special88||j.special88)&&problem.policy3&&!options.policy18&&!problem.rotation){
                const auto &part=i.special88?i:j;const auto reference=options.specialReference90[i.special88?0:1];
                const auto direction=normalized({part.reference50[0]-reference[0],part.reference50[1]-reference[1]}),normal=normalized({plane.a,plane.b});
                const double dot=normal[1]*direction[1]+normal[0]*direction[0];
                if(std::abs(dot)<std::bit_cast<double>(0x3ddb7cdfd9d7bdbbULL))plane.c=plane.c+(-0.001);
            }
            shakerClipCell(cell,{plane,b.sourceA,b.sourceB,{},false});
            if(!result.inside&&!problem.policy3&&evaluate(plane,relative)<neg(tolerance)*1.1){
                // 520130 measures distance to the segment from the current
                // relative point. It does NOT return segment squared length.
                const double distance=std::sqrt(shakerSegmentDistanceSquared(b.start,b.end,relative));
                if(tolerance*144<distance){const double byDistance=(distance*0.707106)*0.95,byWindow=radius*0.75;
                    options.radius=byDistance<=byWindow?byDistance:byWindow;options.overrideWindow=true;retry=true;++result.retries;break;}
            }
        }
        if(retry)continue;
        for(const auto &node:cell){const double violation=neg(evaluate(node.plane,relative));if(maximumViolation<=violation)maximumViolation=violation;}
        if(!problem.rotation){for(const auto &node:cell)if(!node.artificial||options.overrideWindow)shakerTranslationRow(problem,model,node.plane,node.sourceA,node.sourceB,i,j,relative);}
        else{
            auto expanded=source.convolution(problem.angularWindow,0);
            for(auto &b:expanded){bool both=false;b.active=b.direction&&shakerSegmentWindow(b.start,b.end,box,0,both);if(b.active)shakerRotationRow(problem,model,cell,b,i,j,relative);}
        }
        result.cell=std::move(cell);return result;
    }
}
}
