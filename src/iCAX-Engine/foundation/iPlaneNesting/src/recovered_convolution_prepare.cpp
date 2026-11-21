#include "recovered_convolution_filter.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>
namespace study::recovered {
namespace {
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
ConvolutionIntegerPoint dimensions(const ConvolutionIntegerBox &b){return {sub(b[2],b[0]),sub(b[3],b[1])};}
}
ConvolutionIntegerBox ConvolutionPrepareContext::box(const ConvolutionHierarchy &node){
    if(!node.ring||node.ring->empty())throw std::invalid_argument("Prepare bounds requires a nonempty ring");auto [it,fresh]=bounds.try_emplace(node.ring);if(fresh){const auto &ring=*node.ring;it->second={ring[0][0],ring[0][1],ring[0][0],ring[0][1]};for(auto p:ring)for(size_t k=0;k<2;++k){it->second[k]=std::min(it->second[k],p[k]);it->second[k+2]=std::max(it->second[k+2],p[k]);}}return it->second;
}
std::vector<const ConvolutionHierarchy*> convolutionPrepareTerminalExteriors(const ConvolutionHierarchy &node){std::vector<const ConvolutionHierarchy*> out;for(const auto &child:node.children){if(!child)throw std::invalid_argument("Null preparation child");auto items=convolutionPrepareTerminalExteriors(*child);out.insert(out.end(),items.begin(),items.end());}if(out.empty()&&!node.hole)out.push_back(&node);return out;}
ConvolutionIntegerRing convolutionPrepareDimensions(const ConvolutionHierarchy &node,ConvolutionPrepareContext &context){ConvolutionIntegerRing values;for(auto item:convolutionPrepareTerminalExteriors(node))if(!item->hole)values.push_back(dimensions(context.box(*item)));std::sort(values.begin(),values.end());ConvolutionIntegerRing out;for(auto p:values)if(out.empty()||p[1]<out.back()[1])out.push_back(p);return out;}
ConvolutionPreparedNode convolutionPrepareVisit(const ConvolutionHierarchy &node,std::span<const ConvolutionIntegerPoint> frontier,ConvolutionPrepareContext &context){
    if(node.hole&&node.ring){const auto size=dimensions(context.box(node));const auto upper=std::upper_bound(frontier.begin(),frontier.end(),size);if(upper==frontier.begin()||(*(upper-1))[1]>size[1])return {{},true};}
    ConvolutionPreparedNode result{std::make_shared<ConvolutionHierarchy>(),false};result.node->ring=node.ring;result.node->hole=node.hole;
    for(const auto &child:node.children){if(!child)throw std::invalid_argument("Null preparation child");auto prepared=convolutionPrepareVisit(*child,frontier,context);result.changed|=prepared.changed;if(prepared.node)result.node->children.push_back(std::move(prepared.node));}if(!result.node->ring&&result.node->children.empty())result.node.reset();return result;
}
std::array<ConvolutionHierarchy,2> convolutionPrepareMinkowskiOperands(const ConvolutionHierarchy &a,const ConvolutionHierarchy &b){std::array<ConvolutionHierarchy,2> out{a,b};if(a.hole||b.hole)return out;std::array<ConvolutionPrepareContext,2> context;
    for(;;){const auto da=convolutionPrepareDimensions(out[0],context[0]),db=convolutionPrepareDimensions(out[1],context[1]);auto pa=convolutionPrepareVisit(out[0],db,context[0]),pb=convolutionPrepareVisit(out[1],da,context[1]);if(!pa.changed&&!pb.changed)return out;if(!pa.node||!pb.node)throw std::invalid_argument("Preparation removed an external root");out[0]=std::move(*pa.node);out[1]=std::move(*pb.node);}
}
}
