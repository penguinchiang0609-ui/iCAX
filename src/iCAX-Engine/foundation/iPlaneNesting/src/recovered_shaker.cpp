#include "recovered_shaker.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace study::recovered {
namespace {
// MINSD/MAXSD select the second operand for unordered or equal inputs.
double minSecond(double a,double b){return a<b?a:b;}
double maxSecond(double a,double b){return a>b?a:b;}
double negate(double x){return std::bit_cast<double>(std::bit_cast<uint64_t>(x)^0x8000000000000000ULL);}
double absolute(double x){return std::bit_cast<double>(std::bit_cast<uint64_t>(x)&0x7fffffffffffffffULL);}
double valueAt(const std::vector<double> &v,int32_t index){
    if(index<0||size_t(index)>=v.size())throw std::out_of_range("Shaker solution index");
    return v[size_t(index)];
}
}
int32_t LinearModel::addColumn(double lower,double upper,double cost){
    if(columns.size()>=INT32_MAX)throw std::length_error("Shaker column count");
    const auto id=static_cast<int32_t>(columns.size());columns.push_back({lower,upper,cost});return id;
}
int32_t LinearModel::addLowerRow(std::vector<LinearTerm> terms,double rhs){
    return addRow(std::move(terms),rhs,std::numeric_limits<double>::infinity());
}
int32_t LinearModel::addRow(std::vector<LinearTerm> terms,double lower,double upper){
    if(rows.size()>=INT32_MAX)throw std::length_error("Shaker row count");
    const auto id=static_cast<int32_t>(rows.size());rows.push_back({std::move(terms),lower,upper});return id;
}
double shakerVariables(ShakerProblem &problem,std::span<ShakerPart> parts,LinearModel &model,
    const std::function<void(ShakerProblem &,LinearModel &)> &additional){
    double objective=0;
    for(auto &p:parts){
        p.columns={-1,-1,-1};const double half=problem.window*0.5;
        for(size_t axis=0;axis<2;++axis)if(p.locked[axis]==0){
            const double upper=minSecond(p.bounds[axis*2+1],p.position[axis]+half);
            const double lower=maxSecond(p.bounds[axis*2],p.position[axis]-half);
            p.columns[axis]=model.addColumn(lower,upper,p.objective[axis]);
            objective=p.position[axis]*p.objective[axis]+objective;
        }
        if(problem.rotation&&p.locked[2]==0){
            const bool same=p.mirror[0]==p.mirror[1];
            const double upper=minSecond(same?p.angleBounds[1]-p.angle:p.angle-p.angleBounds[0],problem.angularWindow);
            const double lower=maxSecond(same?p.angleBounds[0]-p.angle:p.angle-p.angleBounds[1],negate(problem.angularWindow));
            p.columns[2]=model.addColumn(lower,upper,p.objective[2]);
            objective=p.angle*p.objective[2]+objective;
        }
    }
    if(additional)additional(problem,model);else shakerAdditionalVariables(problem,parts,model);
    return objective;
}
std::array<double,4> shakerSplitVector(ShakerPoint point){
    const double width=std::sqrt(point[1]*point[1]+point[0]*point[0])*0.25;
    const double xp=width+point[0],yp=width+point[1],xm=point[0]-width,ym=point[1]-width;
    // CMOVBE also selects the negative endpoint for unordered comparisons.
    return {!(xp<xm)?xm:xp,!(yp<ym)?ym:yp,!(xm<xp)?xm:xp,!(ym<yp)?ym:yp};
}
void shakerAdditionalVariables(ShakerProblem &problem,std::span<const ShakerPart> parts,LinearModel &model){
    const double infinity=std::numeric_limits<double>::infinity();
    if(problem.free5A)for(auto &id:problem.auxiliary114)id=model.addColumn(-infinity,infinity,0);
    // This policy is bounded ABOVE by zero (unlike +cc). Preserve the original
    // argument order; it is not a generic nonnegative relaxation variable.
    if(problem.policy0){problem.slackC8=model.addColumn(-infinity,0,1);return;}
    if(problem.slack49){
        problem.slackCC=model.addColumn(0,infinity,1);
        if(!problem.policy20)return;
        problem.auxiliaryD0=model.addColumn(0,infinity,0.06);problem.hasAuxiliaryD0=true;
        std::vector<int32_t> deviations;
        for(const auto &p:parts)if(p.locked[0]==0||p.locked[1]==0){
            std::array<int32_t,4> d{};for(auto &id:d){id=model.addColumn(0,infinity,0);deviations.push_back(id);}
            // Both axes are present in this policy, even if one axis is locked;
            // its column is an upstream requirement, not silently substituted.
            model.addRow({{p.columns[0],1},{d[0],-1},{d[1],1}},p.reference50[0],p.reference50[0]);
            model.addRow({{p.columns[1],1},{d[2],-1},{d[3],1}},p.reference50[1],p.reference50[1]);
            model.addRow({{problem.auxiliaryD0,-1},{d[0],1},{d[1],1}},-infinity,0);
            model.addRow({{problem.auxiliaryD0,-1},{d[2],1},{d[3],1}},-infinity,0);
        }
        if(!deviations.empty()){
            const double weight=1/double(static_cast<int32_t>(deviations.size()));std::vector<LinearTerm> terms;
            for(auto id:deviations)terms.push_back({id,weight});
            const auto mean=model.addColumn(-infinity,infinity,0.06);terms.push_back({mean,-1});model.addRow(std::move(terms),0,0);
        }
        return;
    }
    if(problem.policy3){problem.auxiliaryD8=model.addColumn(0,infinity,1);return;}
    if(problem.enabled58&&problem.enabled59){
        if(!problem.vector60.valid||!problem.vector78.valid)throw std::invalid_argument("Missing Shaker directional model vectors");
        const auto first=shakerSplitVector(problem.vector60.point),second=shakerSplitVector(problem.vector78.point);
        problem.auxiliaryDC[0]=model.addColumn(first[0],first[2],problem.vector78.point[1]);
        problem.auxiliaryDC[1]=model.addColumn(first[1],first[3],negate(problem.vector78.point[0]));
        problem.auxiliaryDC[2]=model.addColumn(second[0],second[2],negate(problem.vector60.point[1]));
        problem.auxiliaryDC[3]=model.addColumn(second[1],second[3],problem.vector60.point[0]);
        for(auto &id:problem.auxiliaryEC)id=model.addColumn(0,infinity,0);
    }
}
bool shakerTranslationRow(ShakerProblem &problem,LinearModel &model,const ShakerPlane &plane,
    const ShakerAnchor &anchorA,const ShakerAnchor &anchorB,ShakerPart &i,ShakerPart &j,ShakerPoint relative){
    std::vector<LinearTerm> terms;terms.reserve(7);double rhs=negate(plane.c);
    const double a=plane.a,b=plane.b;
    // 726d40 visits j before i and retains zero coefficients for special88.
    for(size_t axis=0;axis<2;++axis){const double coefficient=axis?b:a;
        if(j.special88||(j.locked[axis]==0&&coefficient!=0))terms.push_back({j.columns[axis],coefficient});
        else rhs=rhs-j.position[axis]*coefficient;
    }
    for(size_t axis=0;axis<2;++axis){const double coefficient=axis?b:a;
        if(i.special88||(i.locked[axis]==0&&coefficient!=0))terms.push_back({i.columns[axis],negate(coefficient)});
        else rhs=rhs+i.position[axis]*coefficient;
    }
    double cap=0;
    if(plane.kappa!=0){const double dx=relative[0]-plane.reference[0],dy=relative[1]-plane.reference[1];
        cap=std::sqrt(dy*dy+dx*dx)*plane.kappa;
    }
    if(problem.rotation){
        const double total=negate(b)*relative[0]+a*relative[1];double gammaI,gammaJ;
        if(anchorA.valid){gammaI=a*anchorA.point[1]+negate(b)*anchorA.point[0];gammaJ=total-gammaI;}
        else {gammaJ=a*anchorB.point[1]+negate(b)*anchorB.point[0];gammaI=total-gammaJ;}
        if(i.locked[2]==0)terms.push_back({i.columns[2],gammaI});
        if(j.locked[2]==0)terms.push_back({j.columns[2],gammaJ});
    }
    if(terms.empty())return false; // no slack-only row is emitted
    if(problem.policy0==0){if(problem.slack49)terms.push_back({problem.slackCC,1});}
    else if(problem.policy1==0||(i.category8==0&&j.category8==0))terms.push_back({problem.slackC8,1});
    const double correction=(((i.position[0]-j.position[0])*a-plane.c)+(i.position[1]-j.position[1])*b)*1.0001;
    const double nonnegative=0<=correction?correction:0;
    const double clipped=correction<=cap?nonnegative:cap;
    const double maximum=i.margin80<j.margin80?j.margin80:i.margin80;
    const double margin=(absolute(b)+absolute(a))*maximum+clipped;
    rhs=rhs-margin;
    if(problem.policy3&&(i.special88||j.special88)&&terms.size()==2){
        problem.deferred.push_back({{terms[0],terms[1]},rhs,&i,&j});return true;
    }
    const auto row=model.addLowerRow(std::move(terms),rhs);i.rows.push_back(row);j.rows.push_back(row);return true;
}
int shakerApplySolution(ShakerProblem &problem,std::span<ShakerPart> parts,const ShakerSolution &solution,
    double &objective,const std::function<void(ShakerPart &)> &refresh){
    if(solution.status==3)return 1;
    if(solution.status!=0)return 2;
    objective=solution.objective;size_t activeCount=0;
    std::array<uint64_t,2> last{};
    for(size_t row=0;row<solution.dual.size();++row)if(solution.dual[row]!=0&&problem.policy3){
        const auto it=problem.specialRows.find(static_cast<int32_t>(row));
        if(it!=problem.specialRows.end()){++activeCount;last=it->second;}
    }
    if(activeCount==1){problem.activeSpecialRow=last;problem.hasActiveSpecialRow=true;}
    else problem.hasActiveSpecialRow=false;
    for(auto &p:parts){
        auto position=p.position;double angle=p.angle;
        if(p.locked[0]==0)position[0]=valueAt(solution.primal,p.columns[0]);
        if(p.locked[1]==0)position[1]=valueAt(solution.primal,p.columns[1]);
        const bool rotate=problem.rotation&&p.locked[2]==0;
        if(rotate){
            const double delta=valueAt(solution.primal,p.columns[2]);angle=p.mirror[0]==p.mirror[1]?angle+delta:angle-delta;
            double candidate=angle;if(angle<p.angleBounds[0])candidate=p.angleBounds[0];
            if(p.angleBounds[1]<angle)candidate=p.angleBounds[1];angle=candidate;
        }
        if(p.locked[0]==0||p.locked[1]==0||rotate){
            p.geometryValid30=0;p.position=position;p.angle=angle;if(refresh)refresh(p);
        }
        p.movable188=uint8_t(p.locked[0]==0||p.locked[1]==0||p.locked[2]==0);
        p.dualMagnitude190=0;
        for(auto row:p.rows){const double dual=valueAt(solution.dual,row);if(dual!=0)p.movable188=0;p.dualMagnitude190=absolute(dual)+p.dualMagnitude190;}
    }
    return 0;
}
ShakerPlane shakerLine(ShakerPoint normal,ShakerPoint point){
    const double length=std::sqrt(normal[1]*normal[1]+normal[0]*normal[0]);
    if(1e-9<=length){const double factor=1/length;normal[0]=normal[0]*factor;normal[1]=normal[1]*factor;}
    else normal={1,0};
    return {normal[0],normal[1],negate(normal[1]*point[1]+normal[0]*point[0])};
}
ShakerPoint shakerIntersection(const ShakerPlane &plane,ShakerPoint start,ShakerPoint end){
    const auto evaluate=[&](ShakerPoint q){return (plane.a*q[0]+plane.b*q[1])+plane.c;};
    const double first=evaluate(start),last=evaluate(end);
    if(absolute(first)<0.001)return start;if(absolute(last)<0.001)return end;
    const double dx=end[0]-start[0],dy=end[1]-start[1],denominator=plane.a*dx+plane.b*dy;
    double t=denominator==0?0:negate(first)/denominator;
    if(t<0)t=0;if(t>1)t=1;
    return {start[0]+t*dx,start[1]+t*dy};
}
}
