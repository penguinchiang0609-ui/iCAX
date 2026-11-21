#include "recovered_convolution_index.hpp"
#include <algorithm>
#include <bit>
#include <map>
#include <stdexcept>
#include <string>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t negate(int64_t a){return std::bit_cast<int64_t>(0-uint64_t(a));}
void checkInput(const ConvolutionIntegerPolygon &rings,const ConvolutionHierarchyDiagnostic &diagnostic){
    if(rings.empty())throw std::invalid_argument("Hierarchy requires nonempty rings");auto low=rings.front().empty()?ConvolutionIntegerPoint{}:rings.front().front(),high=low;
    for(const auto &ring:rings){if(ring.size()<2)throw std::invalid_argument("Hierarchy requires at least two distinct ring points");for(auto p:ring)for(size_t axis=0;axis<2;++axis){low[axis]=std::min(low[axis],p[axis]);high[axis]=std::max(high[axis],p[axis]);}}
    if(diagnostic&&(high[0]>0x80000000LL||high[1]>0x80000000LL||low[0]<-0x80000000LL||low[1]<-0x80000000LL)){
        const auto message="Badly scaled polygon may overflow. The Bounding box is ["+std::to_string(low[0])+","+std::to_string(high[0])+"] X ["+std::to_string(low[1])+","+std::to_string(high[1])+"]";diagnostic(message.c_str());
    }
}
}
ConvolutionHierarchyGeometry convolutionHierarchyRelations(const ConvolutionIntegerPolygon &rings,const ConvolutionHierarchyDiagnostic &diagnostic){
    checkInput(rings,diagnostic);ConvolutionHierarchyGeometry out;out.relations.resize(rings.size(),{0,false});out.holes.resize(rings.size());
    std::map<ConvolutionIntegerSegment,int32_t> owners;std::vector<ConvolutionIntegerPoint> highest(rings.size());
    for(size_t n=0;n<rings.size();++n){const auto &ring=rings[n];out.holes[n]=convolutionIntegerOrientation(ring)==-1;int64_t maximum=INT64_MIN;
        for(size_t k=0;k<ring.size();++k){owners[{ring[k],ring[(k+1)%ring.size()]}]=int32_t(n);if(maximum<ring[k][1]){maximum=ring[k][1];highest[n]=ring[k];}}}
    if(rings.size()==1){out.relations[0]={-1,true};return out;}
    ConvolutionEdgeIndex index;index.build(rings);
    for(size_t n=0;n<rings.size();++n){const auto origin=highest[n];const ConvolutionIntegerSegment ray{origin,{origin[0],add(origin[1],1)}};std::vector<ConvolutionHierarchyRayHit> hits;
        for(const auto &edge:index.query(ray)){ConvolutionHierarchyRayHit hit;hit.segment=edge;convolutionClassifyHierarchyRay({0,1},{0,-1},{negate(origin[0]),negate(origin[1])},hit,true);if(hit.distanceSquared>=0)hits.push_back(hit);}
        convolutionSortHierarchyRayHits(hits);std::reverse(hits.begin(),hits.end());std::vector<ConvolutionHierarchyRayHit> previous;bool assigned=false;
        for(const auto &hit:hits){const auto owner=owners[hit.segment];if(owner==int32_t(n))continue;
            if(assigned){if(previous.empty()){if(diagnostic)diagnostic("No previous intersection but rerlative already stet");break;}
                if(hit.distanceSquared<=previous.back().distanceSquared){if(previous.back().distanceSquared<=hit.distanceSquared){assigned=false;out.relations[n]={0,false};previous.clear();continue;}if(diagnostic)diagnostic("Badly ordered intersecting segments");}break;
            }
            if(!hit.crossing)continue;
            if(hit.distanceSquared==0){const auto contact=convolutionHierarchyRingContacts(rings[owner],rings[n]);if(!contact[0]&&!contact[1])continue;previous.push_back(hit);out.relations[n]={owner,contact[0]};assigned=true;break;}
            if(hit.crossing==2||hit.crossing==-2){out.relations[n]={owner,(hit.crossing==2)!=(out.holes[owner]!=0)};assigned=true;previous.push_back(hit);continue;}
            if(previous.empty()){previous.push_back(hit);continue;}
            const auto &last=previous.back();bool side;
            if(last.point!=hit.point&&owners[last.segment]==owner&&(last.crossing==1||last.crossing==-1)&&(hit.crossing==1||hit.crossing==-1))side=last.crossing==1;
            else side=convolutionHierarchyEndpointOrder(last,hit,diagnostic);
            out.relations[n]={owner,side!=(out.holes[owner]!=0)};assigned=true;previous.push_back(hit);break;
        }
        if(previous.empty())out.relations[n]={-1,true};
    }
    return out;
}
ConvolutionHierarchy convolutionHierarchyFromRings(const ConvolutionIntegerPolygon &rings,bool allowNonSimple,const ConvolutionHierarchyDiagnostic &diagnostic){
    const auto geometry=convolutionHierarchyRelations(rings,diagnostic);return convolutionBuildHierarchy(rings,geometry.relations,geometry.holes,allowNonSimple,diagnostic);
}
}
