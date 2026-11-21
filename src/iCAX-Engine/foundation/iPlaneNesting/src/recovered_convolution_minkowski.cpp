#include "recovered_convolution_minkowski.hpp"
#include "recovered_convolution_offset.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
namespace study::recovered {
namespace {
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
void append(std::vector<ConvolutionIntegerSegment> &out,const ConvolutionIntegerPolygon &rings){
    for(const auto &ring:rings)for(size_t n=0;n<ring.size();++n)out.push_back({ring[n],ring[(n+1)%ring.size()]});
}
void pairEdges(std::vector<ConvolutionIntegerSegment> &out,const ConvolutionHierarchy &a,const ConvolutionHierarchy &b,ConvolutionMinkowskiOption oa,ConvolutionMinkowskiOption ob,const ConvolutionHierarchyDiagnostic &diagnostic){
    append(out,convolutionFlattenHierarchy(convolutionMinkowskiHierarchy(a,b,oa,ob,diagnostic)).rings);
}
ConvolutionHierarchy ringOnly(const ConvolutionHierarchy &node,const ConvolutionHierarchyDiagnostic &diagnostic){return convolutionHierarchyFromRings({*node.ring},false,diagnostic);}
void visitEdges(std::vector<ConvolutionIntegerSegment> &out,const ConvolutionHierarchy &a,const ConvolutionHierarchy &b,ConvolutionMinkowskiOption oa,ConvolutionMinkowskiOption ob,const ConvolutionHierarchyDiagnostic &diagnostic){
    if(a.hole&&b.hole)return;
    if(a.ring&&a.children.empty()&&b.ring&&b.children.empty()){
        if(oa.offset&&ob.offset)throw std::invalid_argument("Both Minkowski operands specify offset");
        auto edges=oa.offset?convolutionOffsetEdges(*b.ring,oa.arcResolution,convolutionMinkowskiDimensions(*a.ring)[0]/2,diagnostic):ob.offset?convolutionOffsetEdges(*a.ring,ob.arcResolution,convolutionMinkowskiDimensions(*b.ring)[0]/2,diagnostic):convolutionIntegerRingEdges(*a.ring,*b.ring);out.insert(out.end(),edges.begin(),edges.end());return;
    }
    if(a.hole){visitEdges(out,b,a,ob,oa,diagnostic);return;}
    if(!b.hole){
        if(!a.ring){if(oa.offset)throw std::invalid_argument("Offset operand requires a ring");for(const auto &child:a.children)visitEdges(out,*child,b,{},ob,diagnostic);return;}
        if(!b.ring){visitEdges(out,b,a,ob,oa,diagnostic);return;}
        const auto disposition=convolutionHierarchyPairDisposition(*a.ring,*b.ring);
        if(disposition[0]){if(disposition[1])throw std::invalid_argument("Mutually strict Minkowski dimensions");visitEdges(out,b,a,ob,oa,diagnostic);return;}
        const auto ra=ringOnly(a,diagnostic),rb=ringOnly(b,diagnostic);pairEdges(out,rb,ra,ob,oa,diagnostic);
        if(disposition[1])for(const auto &child:a.children){if(oa.offset)throw std::invalid_argument("Offset operand cannot have children");visitEdges(out,*child,b,{},ob,diagnostic);}return;
    }
    if(!a.ring){
        if(oa.offset||ob.offset)throw std::invalid_argument("Grouped exterior/hole convolution cannot specify offset");
        if(!b.ring){for(const auto &child:b.children)visitEdges(out,a,*child,{},{},diagnostic);return;}
        ConvolutionIntegerRing all;for(const auto &child:a.children){if(!child->ring)throw std::invalid_argument("Minkowski group requires immediate rings");all.insert(all.end(),child->ring->begin(),child->ring->end());}
        if(!convolutionMinkowskiHoleFit(*b.ring,all))return;
        for(const auto &child:a.children)pairEdges(out,*child,b,{},{},diagnostic);return;
    }
    if(ob.offset)throw std::invalid_argument("Hole operand cannot specify offset");
    if(!b.ring){for(const auto &child:b.children)pairEdges(out,a,*child,oa,ob,diagnostic);return;}
    if(!convolutionMinkowskiHoleFit(*b.ring,*a.ring))return;
    const auto before=out.size();const auto ra=ringOnly(a,diagnostic),rb=ringOnly(b,diagnostic);pairEdges(out,rb,ra,{},oa,diagnostic);
    if(out.size()!=before)for(const auto &child:b.children)visitEdges(out,a,*child,oa,{},diagnostic);
}
}
ConvolutionIntegerPoint convolutionMinkowskiDimensions(std::span<const ConvolutionIntegerPoint> ring){
    if(ring.empty())throw std::invalid_argument("Minkowski dimensions require a ring");auto lo=ring.front(),hi=lo;
    for(auto p:ring)for(size_t k=0;k<2;++k){lo[k]=std::min(lo[k],p[k]);hi[k]=std::max(hi[k],p[k]);}return {sub(hi[0],lo[0]),sub(hi[1],lo[1])};
}
bool convolutionMinkowskiHoleFit(std::span<const ConvolutionIntegerPoint> hole,std::span<const ConvolutionIntegerPoint> part){const auto h=convolutionMinkowskiDimensions(hole),p=convolutionMinkowskiDimensions(part);return p[0]<sub(h[0],1)&&p[1]<sub(h[1],1);}
ConvolutionHierarchy convolutionMinimalHierarchy(const ConvolutionHierarchy &node){
    if(!node.ring&&node.children.size()==1)return convolutionMinimalHierarchy(*node.children.front());
    ConvolutionHierarchy out;out.ring=node.ring;out.hole=node.hole;for(const auto &child:node.children)out.children.push_back(std::make_shared<ConvolutionHierarchy>(convolutionMinimalHierarchy(*child)));return out;
}
bool convolutionHierarchyIsMinimal(const ConvolutionHierarchy &node){if(!node.ring&&node.children.size()==1)return false;return std::all_of(node.children.begin(),node.children.end(),[](const auto &child){return convolutionHierarchyIsMinimal(*child);});}
std::vector<ConvolutionIntegerSegment> convolutionMinkowskiEdges(const ConvolutionHierarchy &a,const ConvolutionHierarchy &b,const ConvolutionHierarchyDiagnostic &diagnostic){return convolutionMinkowskiEdges(a,b,{},{},diagnostic);}
ConvolutionHierarchy convolutionMinkowskiHierarchy(const ConvolutionHierarchy &a,const ConvolutionHierarchy &b,const ConvolutionHierarchyDiagnostic &diagnostic){return convolutionMinkowskiHierarchy(a,b,{},{},diagnostic);}
std::vector<ConvolutionIntegerSegment> convolutionMinkowskiEdges(const ConvolutionHierarchy &a,const ConvolutionHierarchy &b,ConvolutionMinkowskiOption oa,ConvolutionMinkowskiOption ob,const ConvolutionHierarchyDiagnostic &diagnostic){std::vector<ConvolutionIntegerSegment> out;visitEdges(out,a,b,oa,ob,diagnostic);return out;}
ConvolutionHierarchy convolutionMinkowskiHierarchy(const ConvolutionHierarchy &a,const ConvolutionHierarchy &b,ConvolutionMinkowskiOption oa,ConvolutionMinkowskiOption ob,const ConvolutionHierarchyDiagnostic &diagnostic){
    const auto prepared=convolutionPrepareMinkowskiOperands(a,b);const auto &pa=prepared[0],&pb=prepared[1];
    auto edges=convolutionSplitDirectedIntegerSegments(convolutionMinkowskiEdges(pa,pb,oa,ob,diagnostic));convolutionSortIntegerSegments(edges);convolutionFilterIntegerCycles(edges);
    if(edges.empty()){if(!convolutionHierarchyEmpty(a)&&!a.hole&&!convolutionHierarchyEmpty(b)&&!b.hole&&diagnostic)diagnostic("External non empty polygons have null minko sum ");return {};}
    convolutionSortIntegerSegments(edges);auto rings=convolutionTraceIntegerSegments(edges);for(auto &ring:rings)convolutionNormalizeIntegerRing(ring);convolutionRemoveEmptyIntegerRings(rings);
    ConvolutionIntegerPolygon negative,positive;for(auto &ring:rings)(convolutionIntegerOrientation(ring)==-1?negative:positive).push_back(std::move(ring));
    if(oa.offset)negative=convolutionOffsetFilter(pb,pa,negative);else if(ob.offset)negative=convolutionOffsetFilter(pa,pb,negative);
    auto valid=convolutionFilterNegativeRings(pa,pb,negative,oa.offset||ob.offset,diagnostic);valid.insert(valid.end(),positive.begin(),positive.end());for(auto &ring:valid)convolutionNormalizeIntegerRing(ring);convolutionRemoveEmptyIntegerRings(valid);
    if(valid.empty()){ConvolutionHierarchy out;out.hole=true;return out;}
    auto tree=convolutionHierarchyFromRings(valid,true,diagnostic);const bool hole=pa.hole||pb.hole;if(tree.hole!=hole){ConvolutionHierarchy wrapper;wrapper.hole=hole;wrapper.children.push_back(std::make_shared<ConvolutionHierarchy>(std::move(tree)));tree=std::move(wrapper);}
    auto canonical=convolutionCanonicalizeHierarchy(tree,diagnostic);bool simple=true;for(const auto &child:canonical.children)if((!canonical.ring)!=(child->hole==canonical.hole)||!child->ring||!convolutionHierarchySimple(*child)){simple=false;break;}
    if(!simple&&diagnostic)diagnostic("MultiExternalMinkowskiSum produced invalid polygon. ");auto minimal=convolutionMinimalHierarchy(canonical);
    if(!convolutionHierarchyIsMinimal(minimal)&&diagnostic)diagnostic("MultiExternalMinkowskiSum should produce minimal hierarchical polygon.  ");return minimal;
}
ConvolutionHierarchyFlattened convolutionIntegerMinkowski(const ConvolutionIntegerPolygon &a,const ConvolutionIntegerPolygon &b,const ConvolutionHierarchyDiagnostic &diagnostic){return convolutionIntegerMinkowski(a,b,{},{},diagnostic);}
ConvolutionHierarchyFlattened convolutionIntegerMinkowski(const ConvolutionIntegerPolygon &a,const ConvolutionIntegerPolygon &b,ConvolutionMinkowskiOption oa,ConvolutionMinkowskiOption ob,const ConvolutionHierarchyDiagnostic &diagnostic){if(a.empty()||b.empty())return {};const auto ha=convolutionHierarchyFromRings(a,false,diagnostic),hb=convolutionHierarchyFromRings(b,false,diagnostic);return convolutionFlattenHierarchy(convolutionMinkowskiHierarchy(ha,hb,oa,ob,diagnostic));}
}
