#include "recovered_convolution_index.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
namespace study::recovered {
namespace {
int64_t subtract(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
ConvolutionIntegerBox bounds(const ConvolutionIntegerSegment &edge){return {std::min(edge[0][0],edge[1][0]),std::min(edge[0][1],edge[1][1]),std::max(edge[0][0],edge[1][0]),std::max(edge[0][1],edge[1][1])};}
ConvolutionIntegerBox extend(ConvolutionIntegerBox a,const ConvolutionIntegerBox &b){for(size_t axis=0;axis<2;++axis){a[axis]=std::min(a[axis],b[axis]);a[axis+2]=std::max(a[axis+2],b[axis+2]);}return a;}
double area(const ConvolutionIntegerBox &b){return double(subtract(b[2],b[0]))*double(subtract(b[3],b[1]));}
struct Seeds {size_t first,second;double separation;};
Seeds seeds(std::span<const ConvolutionIndexEntry> entries,size_t axis){
    size_t smallestHigh=0;int64_t low=entries[0].box[axis],high=entries[0].box[axis+2];
    for(size_t n=1;n<entries.size();++n){const auto &box=entries[n].box;if(box[axis+2]<entries[smallestHigh].box[axis+2])smallestHigh=n;low=std::min(low,box[axis]);high=std::max(high,box[axis+2]);}
    size_t largestLow=smallestHigh==0?1:0;
    for(size_t n=largestLow;n<entries.size();++n)if(n!=smallestHigh&&entries[largestLow].box[axis]<entries[n].box[axis])largestLow=n;
    double separation=double(subtract(entries[largestLow].box[axis],entries[smallestHigh].box[axis+2]));const auto width=subtract(high,low);if(width>0)separation/=double(width);
    return {largestLow,smallestHigh,separation};
}
struct NodeSplit {std::unique_ptr<ConvolutionIndexNode> sibling;std::array<ConvolutionIntegerBox,2> boxes;};
NodeSplit insert(ConvolutionIndexNode &node,ConvolutionIndexEntry item){
    if(node.leaf)node.entries.push_back(item);
    else {
        size_t selected=0;double bestGrowth=std::numeric_limits<double>::max(),bestArea=bestGrowth;
        for(size_t n=0;n<node.entries.size();++n){const auto expanded=area(extend(node.entries[n].box,item.box)),growth=expanded-area(node.entries[n].box);if(growth<bestGrowth||(growth==bestGrowth&&expanded<bestArea)){selected=n;bestGrowth=growth;bestArea=expanded;}}
        node.entries[selected].box=extend(node.entries[selected].box,item.box);auto result=insert(*node.children[selected],item);
        if(result.sibling){node.entries[selected].box=result.boxes[0];node.entries.push_back({result.boxes[1]});node.children.push_back(std::move(result.sibling));}
    }
    if(node.entries.size()<=8)return {};
    auto numbered=node.entries;for(size_t n=0;n<numbered.size();++n)numbered[n].index=n;const auto split=convolutionIndexSplit(numbered);
    auto sibling=std::make_unique<ConvolutionIndexNode>();sibling->leaf=node.leaf;auto oldEntries=std::move(node.entries);auto oldChildren=std::move(node.children);
    for(size_t side=0;side<2;++side){auto &target=side?*sibling:node;for(const auto &entry:split.groups[side]){target.entries.push_back(oldEntries[entry.index]);if(!node.leaf)target.children.push_back(std::move(oldChildren[entry.index]));}}
    return {std::move(sibling),split.boxes};
}
bool rayBox(const ConvolutionIntegerSegment &ray,const ConvolutionIntegerBox &box){
    const auto p=ray[0];if(box[0]<=p[0]&&p[0]<=box[2]&&box[1]<=p[1]&&p[1]<=box[3])return true;
    const auto sign=[](int64_t value){return (value>0)-(value<0);};
    const bool same=sign(subtract(ray[1][0],p[0]))==sign(subtract(ray[1][1],p[1]));
    const ConvolutionIntegerSegment diagonal=same?ConvolutionIntegerSegment{{{box[2],box[1]},{box[0],box[3]}}}:ConvolutionIntegerSegment{{{box[0],box[1]},{box[2],box[3]}}};
    return convolutionHierarchyRayIntersects(ray,diagonal);
}
void query(const ConvolutionIndexNode &node,const ConvolutionIntegerSegment &ray,std::vector<ConvolutionIntegerSegment> &out){
    for(size_t n=0;n<node.entries.size();++n){const auto &entry=node.entries[n];if(node.leaf){if(convolutionHierarchyRayIntersects(ray,entry.edge))out.push_back(entry.edge);}else if(rayBox(ray,entry.box))query(*node.children[n],ray,out);}
}
}
ConvolutionIndexSplit convolutionIndexSplit(std::span<const ConvolutionIndexEntry> entries){
    if(entries.size()!=9)throw std::invalid_argument("Integer edge index split requires nine entries");
    const auto x=seeds(entries,0),y=seeds(entries,1),chosen=y.separation<=x.separation?x:y;ConvolutionIndexSplit result;
    result.groups[0].push_back(entries[chosen.first]);result.groups[1].push_back(entries[chosen.second]);result.boxes={entries[chosen.first].box,entries[chosen.second].box};
    std::array<double,2> areas{area(result.boxes[0]),area(result.boxes[1])};size_t remaining=7;
    for(size_t n=0;n<entries.size();++n){if(n==chosen.first||n==chosen.second)continue;size_t side;
        const std::array<ConvolutionIntegerBox,2> expanded{extend(result.boxes[0],entries[n].box),extend(result.boxes[1],entries[n].box)};
        const std::array<double,2> newAreas{area(expanded[0]),area(expanded[1])};
        if(result.groups[0].size()+remaining<=2)side=0;else if(result.groups[1].size()+remaining<=2)side=1;
        else{const double a=newAreas[0]-areas[0],b=newAreas[1]-areas[1];side=(a<b||(a==b&&(areas[0]<areas[1]||(areas[0]==areas[1]&&result.groups[0].size()<=result.groups[1].size()))))?0:1;}
        result.groups[side].push_back(entries[n]);result.boxes[side]=expanded[side];areas[side]=newAreas[side];--remaining;
    }
    return result;
}
void ConvolutionEdgeIndex::insert(ConvolutionIntegerSegment edge){
    if(!root)root=std::make_unique<ConvolutionIndexNode>();auto split=recovered::insert(*root,{bounds(edge),edge,count++});if(!split.sibling)return;
    auto parent=std::make_unique<ConvolutionIndexNode>();parent->leaf=false;parent->entries={{split.boxes[0]},{split.boxes[1]}};parent->children.push_back(std::move(root));parent->children.push_back(std::move(split.sibling));root=std::move(parent);++height;
}
void ConvolutionEdgeIndex::build(const ConvolutionIntegerPolygon &rings){
    for(const auto &ring:rings)if(ring.empty())throw std::invalid_argument("Integer edge index requires nonempty rings");root.reset();count=height=0;
    for(const auto &ring:rings){for(size_t n=1;n<ring.size();++n)insert({ring[n-1],ring[n]});insert({ring.back(),ring.front()});}
}
std::vector<ConvolutionIntegerSegment> ConvolutionEdgeIndex::query(const ConvolutionIntegerSegment &ray) const {std::vector<ConvolutionIntegerSegment> result;if(root)recovered::query(*root,ray,result);return result;}
}
