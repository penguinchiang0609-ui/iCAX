#include "recovered_shaker_geometry.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>
namespace study::recovered {
namespace {
double neg(double v){return std::bit_cast<double>(std::bit_cast<uint64_t>(v)^0x8000000000000000ULL);}
double absBits(double v){return std::bit_cast<double>(std::bit_cast<uint64_t>(v)&0x7fffffffffffffffULL);}
double evaluate(const ShakerPlane &p,ShakerPoint q){return (p.b*q[1]+p.a*q[0])+p.c;}
}
ShakerCell shakerRectangleCell(const std::array<double,4> &box){
    const auto [xmin,ymin,xmax,ymax]=box;
    const std::array<ShakerPoint,4> points{{{xmax,ymin},{xmax,ymax},{xmin,ymax},{xmin,ymin}}};
    // Preserve the actual edge differences, including signs of zero and tiny
    // dimensions; exact unit normals are not substituted for normalization.
    const std::array<ShakerPoint,4> normals{{{ymin-ymin,xmax-xmin},{ymin-ymax,xmax-xmax},{ymax-ymax,xmin-xmax},{ymax-ymin,xmin-xmin}}};
    ShakerCell cell;for(size_t n=0;n<4;++n)cell.push_back({shakerLine(normals[n],points[(n+3)%4]),{},{},points[n],true});return cell;
}
void shakerBoundaryDistance(ShakerBoundary &s,ShakerPoint q){
    const auto [ax,ay]=s.start;const auto [bx,by]=s.end;const auto [x,y]=q;
    const double dx=bx-ax,reverseY=ay-by;
    if((by-ay)*(y-ay)+(x-ax)*dx<=0||(x-bx)*(ax-bx)+(y-by)*reverseY<=0){
        const double first=std::sqrt((ax-x)*(ax-x)+(ay-y)*(ay-y));
        const double last=std::sqrt((bx-x)*(bx-x)+(by-y)*(by-y));
        if(last<=first){s.distance=last;s.feature=2;}else{s.distance=first;s.feature=1;}
    }else{
        const double length=std::sqrt(dx*dx+reverseY*reverseY);double a,b;
        if(1e-9<=length){const double scale=1/length;a=scale*reverseY;b=scale*dx;}else{a=1;b=0;}
        s.distance=absBits((y*b+x*a)-(ay*b+ax*a));s.feature=0;
    }
}
ShakerSupport shakerClosestCell(const ShakerCell &cell,ShakerPoint q){
    ShakerSupport result;
    for(size_t n=0;n<cell.size();++n){const auto &node=cell[n];const auto &previous=cell[(n+cell.size()-1)%cell.size()];
        const double h=evaluate(node.plane,q),px=q[0]-h*node.plane.a,py=q[1]-h*node.plane.b;
        const double dx=node.vertex[0]-q[0],dy=node.vertex[1]-q[1];
        const double inside=(previous.vertex[1]-py)*(node.vertex[1]-py)+(previous.vertex[0]-px)*(node.vertex[0]-px);
        const double vertexDistance=std::sqrt(dx*dx+dy*dy);double projected=vertexDistance,distance=vertexDistance;
        if(inside<0){projected=std::sqrt((py-q[1])*(py-q[1])+(px-q[0])*(px-q[0]));distance=vertexDistance<=projected?vertexDistance:projected;}
        if(result.node==SIZE_MAX||distance<result.distance)result={n,distance,0<=inside||vertexDistance<=projected};
    }
    return result;
}
bool shakerClipCell(ShakerCell &cell,ShakerCellNode incoming){
    if(cell.empty())return false;
    size_t enter=SIZE_MAX,leave=SIZE_MAX;bool positive=false;
    const auto crossings=[&](double threshold){
        double before=evaluate(incoming.plane,cell[0].vertex);
        for(size_t k=1;k<=cell.size();++k){const size_t n=k%cell.size();const double after=evaluate(incoming.plane,cell[n].vertex);
            if(before<=threshold){if(threshold<after)enter=n;}
            else{positive=true;if(!(threshold<after))leave=n;}
            before=after;
        }
    };
    crossings(-0.001);if(positive&&leave==SIZE_MAX&&enter==SIZE_MAX)crossings(-0.0);
    if(leave==SIZE_MAX||enter==SIZE_MAX)return false;
    const size_t beforeLeave=(leave+cell.size()-1)%cell.size(),beforeEnter=(enter+cell.size()-1)%cell.size();
    const auto exitPoint=shakerIntersection(incoming.plane,cell[beforeLeave].vertex,cell[leave].vertex);
    incoming.vertex=shakerIntersection(incoming.plane,cell[beforeEnter].vertex,cell[enter].vertex);incoming.artificial=false;
    ShakerCell next;next.push_back(incoming);
    for(size_t n=enter;;n=(n+1)%cell.size()){next.push_back(cell[n]);if(n==leave){next.back().vertex=exitPoint;break;}}
    cell=std::move(next);return true;
}
bool shakerRotationRow(ShakerProblem &problem,LinearModel &model,const ShakerCell &cell,
    const ShakerBoundary &s,ShakerPart &i,ShakerPart &j,ShakerPoint relative){
    if(s.sourceAElement&&s.sourceBElement)return false;
    ShakerPlane plane=shakerLine({s.end[1]-s.start[1],s.start[0]-s.end[0]},s.end);
    if(!cell.empty()){
        double minimum=evaluate(plane,cell[0].vertex);
        for(const auto &node:cell){const double h=evaluate(plane,node.vertex);if(h<=minimum)minimum=h;}
        if(minimum<0&&minimum<=-0.001){const auto a=shakerClosestCell(cell,s.start),b=shakerClosestCell(cell,s.end);plane=cell[a.distance<b.distance?a.node:b.node].plane;}
    }
    return shakerTranslationRow(problem,model,plane,s.sourceA,s.sourceB,i,j,relative);
}
ShakerNear shakerPointNear(ShakerPoint a,ShakerPoint b,ShakerPoint q,double tolerance){
    const double ax=q[0]-a[0],ay=q[1]-a[1],bx=q[0]-b[0],by=q[1]-b[1],square=tolerance*tolerance;
    const double first=ay*ay+ax*ax,last=by*by+bx*bx;ShakerNear result{true,first<square,last<square};
    if(square<=first&&square<=last){const double dx=b[0]-a[0],dy=b[1]-a[1],length=dy*dy+dx*dx,cross=dx*ay-dy*ax;
        if(length<std::bit_cast<double>(0x3c32725dd1d243acULL)||square<=(cross*cross)/length||0<=dy*by+dx*bx||dy*ay+dx*ax<0)result.hit=false;
    }
    return result;
}
double shakerWinding(std::span<const ShakerBoundary> boundary,ShakerPoint q,double tolerance){
    const double pi=std::bit_cast<double>(0x400921fb54442d18ULL),tau=std::bit_cast<double>(0x401921fb54442d18ULL);
    const double absolute=absBits(tolerance),square=absolute*absolute;double sum=0;
    for(const auto &record:boundary){const auto a=record.direction?record.start:record.end,b=record.direction?record.end:record.start;
        const double ax=q[0]-a[0],ay=q[1]-a[1],bx=q[0]-b[0],by=q[1]-b[1];
        const bool nearStart=ay*ay+ax*ax<square;const double last=bx*bx+by*by;
        if(nearStart)continue;
        bool near=last<square;
        if(!near){const double dx=b[0]-a[0],dy=b[1]-a[1],length=dy*dy+dx*dx;
            if(std::bit_cast<double>(0x3c32725dd1d243acULL)<=length){const double cross=dx*ay-dy*ax;
                if((cross*cross)/length<square)near=!(0<=dx*bx+dy*by||dy*ay+dx*ax<0);
            }
        }
        double angle;
        if(square<=last){
            if(near)angle=record.direction?-pi:pi;
            else{const double sy=a[1]-q[1],ey=b[1]-q[1],sx=a[0]-q[0],ex=b[0]-q[0];angle=std::atan2(sx*ey-ex*sy,ex*sx+sy*ey);}
        }else{
            if(record.next<0||size_t(record.next)>=boundary.size())throw std::out_of_range("Missing Shaker boundary adjacency at contact");
            const auto &next=boundary[size_t(record.next)];const auto c=next.direction?next.end:next.start;
            const double dy=a[1]-b[1],cx=c[0]-b[0],cy=c[1]-b[1],dx=a[0]-b[0];
            angle=std::atan2(dx*cy-dy*cx,dy*cy+dx*cx);
            if(record.direction&&next.direction)while(0<=angle)angle=angle-tau;
            else if(!record.direction&&!next.direction)while(angle<=0)angle=angle+tau;
        }
        sum=angle+sum;
    }
    return sum;
}
bool shakerInside(std::span<const ShakerBoundary> boundary,ShakerPoint q,double tolerance,bool parityGate){
    if(parityGate){bool parity=false;int64_t nearCount=0;
        for(const auto &r:boundary){const auto near=shakerPointNear(r.direction?r.start:r.end,r.direction?r.end:r.start,q,absBits(tolerance));
            if(near.hit||near.start||near.end)++nearCount;
            if((q[1]<r.end[1])!=(q[1]<r.start[1])){
                const double dy=r.start[1]-r.end[1],sign=double(int(0<dy)-int(dy<0));
                if((q[0]-r.end[0])*sign*dy<(r.start[0]-r.end[0])*sign*(q[1]-r.end[1]))parity=!parity;
            }
        }
        if(!parity||nearCount==1)return false;
    }
    const double angle=shakerWinding(boundary,q,tolerance),pi=std::bit_cast<double>(0x400921fb54442d18ULL);return pi<angle||angle<-pi;
}
bool shakerSegmentWindow(ShakerPoint a,ShakerPoint b,const std::array<double,4> &box,double tolerance,bool &bothInside){
    const auto [xmin,ymin,xmax,ymax]=box;
    if((tolerance<xmin-a[0]&&tolerance<xmin-b[0])||(tolerance<a[0]-xmax&&tolerance<b[0]-xmax)||
       (tolerance<a[1]-ymax&&tolerance<b[1]-ymax)||(tolerance<ymin-a[1]&&tolerance<ymin-b[1]))return false;
    const bool first=ymin-a[1]<=tolerance&&!(tolerance<a[1]-ymax||tolerance<xmin-a[0]||tolerance<a[0]-xmax);
    const bool last=!(tolerance<ymin-b[1]||tolerance<b[1]-ymax||tolerance<xmin-b[0]||tolerance<b[0]-xmax);
    if(first&&last){bothInside=true;return true;}
    if(first||last){bothInside=false;return true;}
    const auto crossing=[&](double nx,double c,ShakerPoint &out){
        const double h0=(0.*a[1]+nx*a[0])+c,h1=(b[1]*0.+b[0]*nx)+c;
        if(h1*h0<=0){const double t=h0/(h0-h1);out={(b[0]-a[0])*t+a[0],(b[1]-a[1])*t+a[1]};return 0<=t&&t<=1;}return false;
    };
    ShakerPoint left{},right{};
    const bool leftHit=crossing(1,neg(xmin),left),rightHit=crossing(-1,xmax,right);
    const bool reverse=b[0]<=a[0];if(!leftHit)left=reverse?b:a;if(!rightHit)right=reverse?a:b;
    return !((tolerance<left[1]-ymax&&tolerance<right[1]-ymax)||(tolerance<ymin-left[1]&&tolerance<ymin-right[1]));
}
double shakerSegmentDistanceSquared(ShakerPoint a,ShakerPoint b,ShakerPoint q){
    const double ex=q[0]-b[0],ey=q[1]-b[1],dx=b[0]-a[0],dy=b[1]-a[1],sx=q[0]-a[0],sy=q[1]-a[1];
    double result=ey*ey+ex*ex;const double length=dy*dy+dx*dx,first=sy*sy+sx*sx;if(first<=result)result=first;
    if(1e-9<=length){const double t=((q[1]-a[1])*(b[1]-a[1])+(q[0]-a[0])*(b[0]-a[0]))/length;
        if(t<1&&0<t){const double y=q[1]-((b[1]-a[1])*t+a[1]),x=q[0]-((b[0]-a[0])*t+a[0]);result=x*x+y*y;}}
    return result;
}
}
