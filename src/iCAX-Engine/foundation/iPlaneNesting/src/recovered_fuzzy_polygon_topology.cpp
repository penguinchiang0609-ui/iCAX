#include "recovered_fuzzy_polygon_topology.hpp"
#include <bit>
#include <numeric>
namespace study::recovered {
namespace {
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t neg(int64_t v){return std::bit_cast<int64_t>(uint64_t(0)-uint64_t(v));}
int64_t cross(EscherIntegerPoint a,EscherIntegerPoint b){return std::bit_cast<int64_t>(uint64_t(a[0])*uint64_t(b[1])-uint64_t(a[1])*uint64_t(b[0]));}
int sign(int64_t value){return (value>0)-(value<0);}
EscherIntegerPoint primitive(EscherIntegerPoint value){
 const auto abs=[](int64_t v){return v<0?uint64_t(0)-uint64_t(v):uint64_t(v);};
 const auto divisor=std::bit_cast<int64_t>(std::gcd(abs(value[0]),abs(value[1])));
 require(divisor!=0,PN2S_INVALID,"Fuzzy continuation requires a nonzero direction");
 return {value[0]/divisor,value[1]/divisor};
}
EscherIntegerPoint direction(EscherIntegerPoint a,EscherIntegerPoint b){return primitive({sub(a[0],b[0]),sub(a[1],b[1])});}
}
EscherIntegerPoint fuzzyTopologyContinuation(const ConvolutionIntegerSegment &edge,
    std::span<const EscherIntegerPoint> destinations){
 require(!destinations.empty(),PN2S_INVALID,"Fuzzy continuation requires candidates");
 const auto before=direction(edge[0],edge[1]);auto point=destinations[0];
 for(size_t n=1;point==edge[0]&&n<destinations.size();++n)point=destinations[n];
 auto best=direction(point,edge[1]);
 for(const auto candidate:destinations){
  if(candidate==edge[0])continue;
  const auto next=direction(candidate,edge[1]);
  bool collinearGate=next==before||next==best||before==best;
  bool skip=false;
  if(!collinearGate){
   const auto sweep=sign(cross(before,best)),first=sign(cross(before,next)),second=sign(cross(best,next));
   if(sweep==1){if(first!=1)collinearGate=true;else if(second==1)skip=true;}
   else if(first!=1&&second==1)collinearGate=true;
  }
  if(collinearGate&&(sign(cross(before,next))!=0||sign(cross(best,next))!=-1))skip=true;
  if(!skip){point=candidate;best=next;}
 }
 return point;
}
std::vector<int32_t> fuzzyTopologyPathOrder(std::span<const int32_t> components,
    std::span<const EscherIntegerPoint> summits){
 require(components.size()==summits.size(),PN2S_INVALID,"Fuzzy path components and summits differ");
 std::vector<int32_t> result(components.size());std::iota(result.begin(),result.end(),0);
 std::sort(result.begin(),result.end(),[&](int32_t a,int32_t b){return std::tuple{components[a],neg(summits[a][1]),a}<std::tuple{components[b],neg(summits[b][1]),b};});return result;
}
FuzzyTopologySummits fuzzyTopologySummits(const FuzzyTopology &topology){
 FuzzyTopologySummits result;
 for(size_t region=0;region<topology.regions.size();++region){
  EscherIntegerPoint best{INT64_MIN,INT64_MIN};FuzzyTopologySummitLocation location{int32_t(region),-1,-1};
  const auto &geometry=topology.regions[region].geometry;
  for(size_t rangeIndex=0;rangeIndex<geometry.size();++rangeIndex){const auto &range=geometry[rangeIndex];
   require(range.segments&&range.begin>=0&&range.begin<range.end&&size_t(range.end)<=range.segments->size(),PN2S_INVALID,"Fuzzy summit geometry range is invalid");
   const auto found=topology.segmentIndex.find(range.segments->at(size_t(range.begin)));
   require(found!=topology.segmentIndex.end()&&found->second.path>=0&&size_t(found->second.path)<topology.paths.size(),PN2S_INVALID,"Fuzzy summit segment has no source path");
   const auto &source=*topology.paths.at(size_t(found->second.path));const auto begin=found->second.position,end=begin+(range.end-range.begin);
   require(begin>=0&&begin<end&&size_t(end)<=source.size(),PN2S_INVALID,"Fuzzy summit source range is invalid");
   // 82ea10 builds the original minimum tree over double(wrapped -y).
   // Equal rounded values retain the earliest original path position.
   int32_t selected=begin;double score=double(neg(source[size_t(begin)][0][1]));
   for(int32_t n=begin+1;n<end;++n){const double candidate=double(neg(source[size_t(n)][0][1]));if(candidate<score){selected=n;score=candidate;}}
   const auto point=source[size_t(selected)][0];
   if(best[1]<point[1]){best=point;location.range=int32_t(rangeIndex);location.offset=selected-begin;}
  }
  result.points.push_back(best);result.locations.push_back(location);
 }
 return result;
}
}
