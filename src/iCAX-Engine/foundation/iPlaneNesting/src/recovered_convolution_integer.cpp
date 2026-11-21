#include "recovered_convolution_integer.hpp"
#include <algorithm>
#include <bit>
#include <numeric>
#include <map>
#include <stdexcept>
#include <cmath>
namespace study::recovered {
namespace {
int64_t wrapped(uint64_t v){return std::bit_cast<int64_t>(v);}
ConvolutionIntegerPoint difference(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){return {wrapped(uint64_t(a[0])-uint64_t(b[0])),wrapped(uint64_t(a[1])-uint64_t(b[1]))};}
ConvolutionIntegerPoint sum(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){return {wrapped(uint64_t(a[0])+uint64_t(b[0])),wrapped(uint64_t(a[1])+uint64_t(b[1]))};}
int64_t cross(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){return wrapped(uint64_t(a[0])*uint64_t(b[1])-uint64_t(a[1])*uint64_t(b[0]));}
uint64_t magnitude(int64_t a){return a<0?uint64_t(0)-uint64_t(a):uint64_t(a);}
int sign(int64_t a){return a<0?-1:1;}
bool parallelWrapped(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){const auto left=magnitude(a[0])*magnitude(b[1]),right=magnitude(b[0])*magnitude(a[1]);return left==right&&(left==0||sign(a[0])*sign(b[1])==sign(b[0])*sign(a[1]));}
bool inBox(ConvolutionIntegerPoint p,const ConvolutionIntegerSegment &s){for(size_t n=0;n<2;++n)if(p[n]<std::min(s[0][n],s[1][n])||p[n]>std::max(s[0][n],s[1][n]))return false;return true;}
int64_t truncateOriginal(double x){return x>=-0x1p63&&x<0x1p63?int64_t(x):INT64_MIN;}
int64_t floorOriginal(double x){const auto t=truncateOriginal(x);return double(t)<=x||std::isnan(x)?t:wrapped(uint64_t(t)-1);}
ConvolutionIntegerPoint primitive(ConvolutionIntegerPoint p){const auto divisor=wrapped(convolutionIntegerGcd(p));if(divisor==0)throw std::invalid_argument("Integer direction requires a nonzero edge");return {p[0]/divisor,p[1]/divisor};}
}
uint64_t convolutionIntegerGcd(ConvolutionIntegerPoint point){const auto magnitude=[](int64_t v){return v<0?uint64_t(0)-uint64_t(v):uint64_t(v);};return std::gcd(magnitude(point[0]),magnitude(point[1]));}
void convolutionNormalizeIntegerRing(ConvolutionIntegerRing &ring){
    if(ring.empty())return;ConvolutionIntegerRing unique;unique.reserve(ring.size());for(auto p:ring)if(unique.empty()||p!=unique.back())unique.push_back(p);
    while(!unique.empty()&&unique.back()==unique.front())unique.pop_back();ring.clear();if(unique.size()<3)return;
    for(size_t n=0;n<unique.size();++n){const auto prev=(n+unique.size()-1)%unique.size(),next=(n+1)%unique.size();if(cross(difference(unique[n],unique[prev]),difference(unique[next],unique[n]))!=0&&(ring.empty()||ring.back()!=unique[n]))ring.push_back(unique[n]);}
    while(!ring.empty()&&ring.back()==ring.front())ring.pop_back();
}
size_t convolutionRemoveEmptyIntegerRings(ConvolutionIntegerPolygon &polygon){return std::erase_if(polygon,[](const auto &ring){return ring.empty();});}
int convolutionIntegerOrientation(std::span<const ConvolutionIntegerPoint> ring){
    if(ring.empty())throw std::invalid_argument("Integer orientation requires a nonempty ring");const auto first=size_t(std::min_element(ring.begin(),ring.end())-ring.begin());size_t previous=first,next=first;
    do{previous=(previous+ring.size()-1)%ring.size();}while(previous!=first&&ring[previous]==ring[first]);
    do{next=(next+1)%ring.size();}while(next!=first&&ring[next]==ring[first]);
    const auto turn=cross(primitive(difference(ring[first],ring[previous])),primitive(difference(ring[next],ring[first])));return (turn>0)-(turn<0);
}
ConvolutionIntegerRing convolutionIntegerDirections(std::span<const ConvolutionIntegerPoint> ring){
    if(ring.empty())throw std::invalid_argument("Integer directions require a nonempty ring");ConvolutionIntegerRing result;result.reserve(ring.size());for(size_t n=0;n<ring.size();++n)result.push_back(primitive(difference(ring[(n+1)%ring.size()],ring[n])));return result;
}
std::vector<ConvolutionIntegerSegment> convolutionIntegerConvolveEdges(std::span<const ConvolutionIntegerPoint> a,std::span<const ConvolutionIntegerPoint> da,std::span<const ConvolutionIntegerPoint> b,std::span<const ConvolutionIntegerPoint> db){
    if(a.empty()||b.empty()||a.size()!=da.size()||b.size()!=db.size())throw std::invalid_argument("Integer convolution requires matching nonempty point and direction rings");
    if(a.size()>size_t(INT32_MAX)/b.size())throw std::length_error("Integer convolution pair index exceeds original int32 range");
    std::vector<uint8_t> convexA(a.size()),convexB(b.size()),visited(a.size()*b.size());
    for(size_t n=0;n<a.size();++n)convexA[n]=cross(da[(n+a.size()-1)%a.size()],da[n])>0;
    for(size_t n=0;n<b.size();++n)convexB[n]=cross(db[(n+b.size()-1)%b.size()],db[n])>0;
    std::vector<std::array<size_t,2>> stack;for(size_t n=a.size();n--;)stack.push_back({n,0});std::vector<ConvolutionIntegerSegment> result;
    while(!stack.empty()){
        const auto [i,j]=stack.back();stack.pop_back();auto &seen=visited[j*a.size()+i];if(seen)continue;seen=1;
        const auto prevA=da[(i+a.size()-1)%a.size()],prevB=db[(j+b.size()-1)%b.size()];bool advanceB,advanceA;
        if(db[j]==prevA)advanceB=true;else if(db[j]==da[i])advanceB=false;else {const auto first=cross(prevA,db[j]),second=cross(da[i],db[j]);advanceB=convexA[i]?(first>0&&second<=0):(first>0||second<=0);}
        if(da[i]==prevB)advanceA=false;else if(da[i]==db[j])advanceA=true;else {const auto first=cross(prevB,da[i]),second=cross(db[j],da[i]);advanceA=convexB[j]?(first>0&&second<=0):(first>0||second<=0);}
        if(advanceB){const auto next=(j+1)%b.size();stack.push_back({i,next});if(convexA[i])result.push_back({sum(a[i],b[j]),sum(a[i],b[next])});}
        if(advanceA){const auto next=(i+1)%a.size();stack.push_back({next,j});if(convexB[j])result.push_back({sum(a[i],b[j]),sum(a[next],b[j])});}
    }
    return result;
}
std::vector<ConvolutionIntegerSegment> convolutionIntegerRingEdges(std::span<const ConvolutionIntegerPoint> a,std::span<const ConvolutionIntegerPoint> b){return convolutionIntegerConvolveEdges(a,convolutionIntegerDirections(a),b,convolutionIntegerDirections(b));}
void convolutionSortIntegerSegments(std::vector<ConvolutionIntegerSegment> &segments){std::sort(segments.begin(),segments.end());segments.erase(std::unique(segments.begin(),segments.end()),segments.end());}
ConvolutionIntegerGraph convolutionIntegerGraph(size_t vertices,std::span<const std::array<int32_t,2>> edges){
    ConvolutionIntegerGraph graph;graph.offsets.assign(vertices+1,0);std::vector<size_t> sources;for(auto e:edges){if(e[0]<0||e[1]<0||size_t(e[0])>=vertices||size_t(e[1])>=vertices)throw std::invalid_argument("Integer graph edge outside vertex range");sources.push_back(e[0]);graph.targets.push_back(e[1]);++graph.offsets[size_t(e[0])+1];}
    for(size_t n=1;n<graph.offsets.size();++n)graph.offsets[n]+=graph.offsets[n-1];auto cursor=graph.offsets;
    // Original CSR construction partitions in place. A stable bucket scatter
    // changes DFS order and therefore component IDs and representative roots.
    for(size_t n=0;n<sources.size();++n)while(n<graph.offsets[sources[n]]||cursor[sources[n]]<=n){const auto other=cursor[sources[n]]++;if(other!=n){std::swap(sources[n],sources[other]);std::swap(graph.targets[n],graph.targets[other]);}}
    return graph;
}
ConvolutionIntegerComponents convolutionIntegerComponents(const ConvolutionIntegerGraph &graph){
    if(graph.offsets.empty()||graph.offsets.front()!=0||graph.offsets.back()!=graph.targets.size()||!std::is_sorted(graph.offsets.begin(),graph.offsets.end()))throw std::invalid_argument("Invalid integer graph offsets");
    const auto count=graph.offsets.size()-1;for(auto target:graph.targets)if(target>=count)throw std::invalid_argument("Invalid integer graph target");
    ConvolutionIntegerComponents out;out.labels.resize(count);out.discovery.resize(count);out.colors.resize(count);out.roots.resize(count);int32_t discovered=0;std::vector<size_t> active;
    struct Frame {size_t vertex,next;};std::vector<Frame> dfs;
    const auto begin=[&](size_t v){out.colors[v]=1;out.roots[v]=v;out.labels[v]=INT32_MAX;out.discovery[v]=discovered++;active.push_back(v);dfs.push_back({v,graph.offsets[v]});};
    for(size_t start=0;start<count;++start){if(out.colors[start])continue;begin(start);
        while(!dfs.empty()){auto &frame=dfs.back();const auto v=frame.vertex;
            if(frame.next<graph.offsets[v+1]){const auto child=graph.targets[frame.next++];if(out.colors[child]==0)begin(child);continue;}
            out.colors[v]=4;
            for(size_t n=graph.offsets[v];n<graph.offsets[v+1];++n){const auto child=graph.targets[n];if(out.labels[child]==INT32_MAX){const auto candidate=out.roots[child];if(out.discovery[out.roots[v]]>=out.discovery[candidate])out.roots[v]=candidate;}}
            if(out.roots[v]==v){size_t child;do{child=active.back();active.pop_back();out.labels[child]=int32_t(out.count);out.roots[child]=v;}while(child!=v);++out.count;}dfs.pop_back();
        }
    }
    return out;
}
void convolutionFilterIntegerCycles(std::vector<ConvolutionIntegerSegment> &segments){
    if(segments.empty())return;std::map<ConvolutionIntegerPoint,int32_t> ids;std::vector<std::array<int32_t,2>> edges;edges.reserve(segments.size());
    for(auto segment:segments){std::array<int32_t,2> edge;for(size_t n=0;n<2;++n){if(ids.size()>=INT32_MAX)throw std::length_error("Integer graph vertex count exceeds int32");edge[n]=ids.try_emplace(segment[n],int32_t(ids.size())).first->second;}edges.push_back(edge);}
    const auto components=convolutionIntegerComponents(convolutionIntegerGraph(ids.size(),edges));size_t retained=0;for(size_t n=0;n<segments.size();++n)if(components.labels[edges[n][0]]==components.labels[edges[n][1]])segments[retained++]=segments[n];segments.resize(retained);
}
bool convolutionIntegerDirectionLess(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){
    if(a[0]==0)return false;
    if(a[0]<0)a[1]=wrapped(uint64_t(0)-uint64_t(a[1]));a[0]=wrapped(magnitude(a[0]));
    if(b[0]==0)return a[0]!=0;
    if(b[0]<0)b[1]=wrapped(uint64_t(0)-uint64_t(b[1]));b[0]=wrapped(magnitude(b[0]));
    const auto left=magnitude(b[0])*magnitude(a[1]),right=magnitude(b[1])*magnitude(a[0]);
    const auto ls=sign(b[0])*sign(a[1]),rs=sign(b[1])*sign(a[0]);
    return ls!=rs?ls<rs:ls==-1?right<left:left<right;
}
bool convolutionIntegerPointBetween(ConvolutionIntegerPoint p,ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){return a<b?a<p&&p<b:b<p&&p<a;}
int convolutionIntegerPointSide(ConvolutionIntegerPoint p,const ConvolutionIntegerSegment &s){
    if(p==s[0]||p==s[1])return 0;const auto a=difference(s[0],p),b=difference(s[1],p);if(parallelWrapped(a,b))return 0;
    auto result=convolutionIntegerDirectionLess(a,b)?-1:1;if(s[1]<s[0])result=-result;return convolutionIntegerPointBetween(p,s[0],s[1])?result:-result;
}
bool convolutionIntegerSegmentsIntersect(const ConvolutionIntegerSegment &a,const ConvolutionIntegerSegment &b){
    const auto minX=[](const auto&s){return std::min(s[0][0],s[1][0]);};const auto maxX=[](const auto&s){return std::max(s[0][0],s[1][0]);};
    const auto minY=[](const auto&s){return std::min(s[0][1],s[1][1]);};const auto maxY=[](const auto&s){return std::max(s[0][1],s[1][1]);};
    if(minX(a)<maxX(b)&&minX(b)<maxX(a)&&minY(a)<maxY(b)&&minY(b)<maxY(a)){
        for(size_t i=0;i<2;++i)for(size_t j=0;j<2;++j)if(a[i]==b[j]){if(a[1-i]==b[1-j])return false;return parallelWrapped(difference(a[1-i],a[i]),difference(b[1-j],a[i]));}
        auto first=convolutionIntegerPointSide(a[0],b);if(first==0&&convolutionIntegerPointBetween(a[0],b[0],b[1]))return true;
        auto second=convolutionIntegerPointSide(a[1],b);if(second==0&&convolutionIntegerPointBetween(a[1],b[0],b[1]))return true;if(first==second&&first!=0)return false;
        first=convolutionIntegerPointSide(b[0],a);if(first==0&&convolutionIntegerPointBetween(b[0],a[0],a[1]))return true;
        second=convolutionIntegerPointSide(b[1],a);if(second==0&&convolutionIntegerPointBetween(b[1],a[0],a[1]))return true;return first!=second;
    }
    if(a[0][0]==a[1][0]&&b[0][0]==b[1][0]&&a[0][0]==b[0][0])return minY(a)<maxY(b)&&minY(b)<maxY(a)&&(minY(a)!=minY(b)||maxY(a)!=maxY(b));
    if(a[0][1]==a[1][1]&&b[0][1]==b[1][1]&&a[0][1]==b[0][1])return minX(a)<maxX(b)&&minX(b)<maxX(a)&&(minX(a)!=minX(b)||maxX(a)!=maxX(b));return false;
}
bool convolutionIntegerPointInSegmentCell(ConvolutionIntegerPoint p,const ConvolutionIntegerSegment &s){
    if(p==s[0]||p==s[1])return true;if(!inBox(p,s))return false;if(s[0][0]==s[1][0]||s[0][1]==s[1][1])return true;
    if(parallelWrapped(difference(s[0],p),difference(s[1],p))&&convolutionIntegerPointBetween(p,s[0],s[1]))return true;
    const auto diagonal=sum(p,{1,1}),upper=sum(p,{0,1}),right=sum(p,{1,0});
    if(convolutionIntegerSegmentsIntersect({p,diagonal},s)&&convolutionIntegerPointSide(diagonal,s)!=0)return true;
    return convolutionIntegerSegmentsIntersect({upper,right},s)&&convolutionIntegerPointSide(upper,s)!=0&&convolutionIntegerPointSide(right,s)!=0;
}
bool convolutionIntegerApproximateIntersection(ConvolutionIntegerPoint &out,const ConvolutionIntegerSegment &a,const ConvolutionIntegerSegment &b){
    for(size_t n=0;n<2;++n)if(std::min(a[0][n],a[1][n])>std::max(b[0][n],b[1][n])||std::min(b[0][n],b[1][n])>std::max(a[0][n],a[1][n]))return false;
    const auto vertical=[&](const auto &v,const auto &s){if(s[0][0]==s[1][0])return false;auto y=s[0][1];if(y!=s[1][1]){const double dx=double(v[0][0])-double(s[0][0]);const double initial=double(y);y=truncateOriginal(dx==0?initial:((double(s[1][1])-initial)*dx)/(double(s[1][0])-double(s[0][0]))+initial);}y=floorOriginal(double(y));if(y<std::min(v[0][1],v[1][1])||y>std::max(v[0][1],v[1][1]))return false;out={v[0][0],y};return true;};
    if(a[0][0]==a[1][0])return vertical(a,b);if(b[0][0]==b[1][0])return vertical(b,a);
    const auto da=difference(a[1],a[0]),db=difference(b[1],b[0]);const double ax=double(da[0]),ay=double(da[1]),bx=double(db[0]),by=double(db[1]);const double left=bx*ay,right=ax*by;if(left==right)return false;
    const double x=(((double(a[0][0])*ay*bx-double(b[0][0])*by*ax)+double(b[0][1])*ax*bx)-double(a[0][1])*ax*bx)/(left-right);
    const double y=(((double(a[0][1])*ax*by-double(b[0][1])*bx*ay)+double(b[0][0])*ay*by)-double(a[0][0])*ay*by)/(right-left);
    ConvolutionIntegerPoint candidate{floorOriginal(x),floorOriginal(y)};if(a[0][1]==a[1][1])candidate[1]=a[0][1];if(b[0][1]==b[1][1])candidate[1]=b[0][1];if(!inBox(candidate,a)||!inBox(candidate,b))return false;out=candidate;return true;
}
}
