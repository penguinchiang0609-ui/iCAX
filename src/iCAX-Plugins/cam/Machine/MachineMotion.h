#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

// Neutral tree kinematics. No repository, resource or product state is owned here.
// Lengths are mm, angles radians, matrices row-major acting on column vectors.
namespace iCAX::CAM::Motion {
using Vector=std::array<double,3>;
using Matrix=std::array<double,16>;
inline Matrix Identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
inline Matrix Multiply(const Matrix& a,const Matrix& b){Matrix r{};for(int i=0;i<4;++i)for(int j=0;j<4;++j)for(int k=0;k<4;++k)r[i*4+j]+=a[i*4+k]*b[k*4+j];return r;}
inline Matrix Inverse(const Matrix& a){auto r=Identity();for(int i=0;i<3;++i){for(int j=0;j<3;++j)r[i*4+j]=a[j*4+i];for(int j=0;j<3;++j)r[i*4+3]-=a[j*4+i]*a[j*4+3];}return r;}
inline Vector Transform(const Matrix& a,const Vector& p,bool direction=false){Vector r{};for(int i=0;i<3;++i){r[i]=direction?0:a[i*4+3];for(int j=0;j<3;++j)r[i]+=a[i*4+j]*p[j];}return r;}
inline double Length(const Vector& a){return std::hypot(a[0],a[1],a[2]);}
inline Vector Unit(Vector a){const auto n=Length(a);if(!std::isfinite(n)||n<1e-12)throw std::invalid_argument("Invalid motion axis");for(auto& x:a)x/=n;return a;}
struct Node {
    std::string ID,Type="fixed";
    int Parent=-1;
    Matrix Origin=Identity();
    Vector Axis{1,0,0};
    double Lower=0,Upper=0,Position=0,Velocity=0;
};
inline Matrix Joint(const Node& node,double q){
    auto r=Identity();if(node.Type=="fixed")return r;
    const auto a=Unit(node.Axis);
    if(node.Type=="prismatic"){for(int i=0;i<3;++i)r[i*4+3]=a[i]*q;return r;}
    if(node.Type!="revolute"&&node.Type!="continuous")throw std::invalid_argument("Unsupported joint type: "+node.Type);
    const auto c=std::cos(q),s=std::sin(q),v=1-c;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)r[i*4+j]=a[i]*a[j]*v+(i==j?c:0);
    r[1]-=a[2]*s;r[2]+=a[1]*s;r[4]+=a[2]*s;r[6]-=a[0]*s;r[8]-=a[1]*s;r[9]+=a[0]*s;return r;
}
inline std::vector<Matrix> Forward(const std::vector<Node>& nodes,const std::vector<double>& q){
    if(nodes.size()!=q.size())throw std::invalid_argument("Joint vector size mismatch");
    std::vector<Matrix> result;result.reserve(nodes.size());
    for(size_t i=0;i<nodes.size();++i){const auto& n=nodes[i];if(n.Parent>=static_cast<int>(i)||n.Parent < -1)throw std::invalid_argument("Machine tree is not ordered");
        auto m=Multiply(n.Origin,Joint(n,q[i]));result.push_back(n.Parent<0?m:Multiply(result[n.Parent],m));}return result;
}
struct Target {int Tool=-1,Support=-1;Vector TCP{},Beam{0,0,-1},Position{},Direction{0,0,-1};};
struct Solution {bool Reached=false;std::vector<double> Positions;double PositionError=0,DirectionError=0;};
inline std::array<double,6> Pose(const std::vector<Node>& nodes,const std::vector<double>& q,const Target& t){
    const auto w=Forward(nodes,q);const auto m=Multiply(Inverse(w.at(t.Support)),w.at(t.Tool));
    const auto p=Transform(m,t.TCP),d=Unit(Transform(m,t.Beam,true));return {p[0],p[1],p[2],100*d[0],100*d[1],100*d[2]};
}
inline Solution Solve(const std::vector<Node>& nodes,const Target& target,const std::vector<double>& seed){
    auto q=seed;const auto d=Unit(target.Direction);const std::array<double,6> goal{target.Position[0],target.Position[1],target.Position[2],100*d[0],100*d[1],100*d[2]};
    std::vector<size_t> axes;for(size_t i=0;i<nodes.size();++i)if(nodes[i].Type!="fixed")axes.push_back(i);
    Solution result;double best=1e300;auto bestQ=q;
    // Damped least squares, bounded by physical joint limits, with continuity
    // from the preceding sample. Alternate starts handle antiparallel axes.
    for(int attempt=0;attempt<5;++attempt){
        q=attempt?bestQ:seed;
        if(attempt)for(auto i:axes)if(nodes[i].Type!="prismatic")q[i]=std::clamp(q[i]+(attempt%2?1:-1)*(.35+.25*attempt),nodes[i].Lower,nodes[i].Upper);
        for(int iteration=0;iteration<160;++iteration){
            const auto value=Pose(nodes,q,target);std::array<double,6> e{};double cost=0;
            for(int k=0;k<6;++k){e[k]=goal[k]-value[k];cost+=e[k]*e[k];}
            if(cost<best){best=cost;bestQ=q;}
            const auto pe=std::hypot(e[0],e[1],e[2]),de=std::hypot(e[3],e[4],e[5])/100;
            if(pe<=.01&&de<=1e-5)return {true,q,pe,de};
            std::vector<std::array<double,6>> jac;
            for(auto i:axes){auto trial=q;const auto h=nodes[i].Type=="prismatic"?.001:1e-5;trial[i]+=h;const auto v=Pose(nodes,trial,target);std::array<double,6> col{};for(int k=0;k<6;++k)col[k]=(v[k]-value[k])/h;jac.push_back(col);}
            double a[6][7]{};for(int r=0;r<6;++r){for(int c=0;c<6;++c){for(const auto& j:jac)a[r][c]+=j[r]*j[c];if(r==c)a[r][c]+=.01;}a[r][6]=e[r];}
            for(int c=0;c<6;++c){int pivot=c;for(int r=c+1;r<6;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;
                for(int k=c;k<7;++k)std::swap(a[c][k],a[pivot][k]);const auto divisor=a[c][c];for(int k=c;k<7;++k)a[c][k]/=divisor;
                for(int r=0;r<6;++r)if(r!=c){const auto f=a[r][c];for(int k=c;k<7;++k)a[r][k]-=f*a[c][k];}}
            double movement=0;for(size_t j=0;j<axes.size();++j){const auto i=axes[j];double step=0;for(int k=0;k<6;++k)step+=jac[j][k]*a[k][6];
                const auto limit=nodes[i].Type=="prismatic"?50.:.15;const auto next=std::clamp(q[i]+std::clamp(step,-limit,limit),nodes[i].Lower,nodes[i].Upper);movement+=std::abs(next-q[i]);q[i]=next;}
            if(movement<1e-10)break;
        }
    }
    const auto v=Pose(nodes,bestQ,target);return {false,bestQ,std::hypot(goal[0]-v[0],goal[1]-v[1],goal[2]-v[2]),std::hypot(goal[3]-v[3],goal[4]-v[4],goal[5]-v[5])/100};
}
}
