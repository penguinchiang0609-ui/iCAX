#pragma once
#include "MachineMotion.h"
#include <functional>

namespace iCAX::CAM::Motion {
// Upper bound on the travel of every point of a body during joint-linear
// interpolation. Rotations use arc length, including all downstream offsets.
inline double SweepBound(const std::vector<Node>& nodes,int body,double radius,
    const std::vector<double>& a,const std::vector<double>& b,int stopAncestor=-1){
    double bound=0;
    for(int i=body;i>=0&&i!=stopAncestor;i=nodes[i].Parent){const auto& n=nodes[i];
        if(n.Type=="prismatic"){bound+=std::abs(b[i]-a[i]);radius+=std::max(std::abs(a[i]),std::abs(b[i]));}
        else if(n.Type!="fixed")bound+=std::abs(b[i]-a[i])*radius;
        radius+=Length({n.Origin[3],n.Origin[7],n.Origin[11]});
    }return bound;
}
inline double RelativeSweepBound(const std::vector<Node>& nodes,int first,double firstRadius,int second,double secondRadius,
    const std::vector<double>& a,const std::vector<double>& b){
    int common=-1;for(int i=first;i>=0&&common<0;i=nodes[i].Parent)for(int j=second;j>=0;j=nodes[j].Parent)if(i==j){common=i;break;}
    // A shared ancestor applies the same rigid motion to both bodies. It
    // cannot change their separation, regardless of its rotation/translation.
    return SweepBound(nodes,first,firstRadius,a,b,common)+SweepBound(nodes,second,secondRadius,a,b,common);
}
struct SweepResult {bool Clear=false;double Fraction=0;std::size_t Queries=0;};
// The callback supplies a conservative separation lower bound at actual FK
// poses. Endpoints alone never certify the interior of a motion interval.
inline SweepResult CertifySweep(const std::vector<double>& a,const std::vector<double>& b,
    double travelBound,double clearance,const std::function<double(const std::vector<double>&)>& distance){
    SweepResult result;
    const auto at=[&](double t){auto q=a;for(size_t i=0;i<q.size();++i)q[i]+=(b[i]-a[i])*t;++result.Queries;return distance(q);};
    if(at(0)<=clearance)return result;
    if(travelBound==0){result.Clear=true;return result;}
    std::function<bool(double,double,int)> visit=[&](double l,double r,int depth){
        const auto t=(l+r)*.5,d=at(t);if(d>clearance+travelBound*(r-l)*.5)return true;
        // Search in motion order: never jump to an already penetrating endpoint.
        // At the resolution boundary stop conservatively before unproved travel.
        if(depth>=22||travelBound*(r-l)<.001||result.Queries>=4096){result.Fraction=l;return false;}
        return visit(l,t,depth+1)&&visit(t,r,depth+1);
    };result.Clear=visit(0,1,0);return result;
}
}
