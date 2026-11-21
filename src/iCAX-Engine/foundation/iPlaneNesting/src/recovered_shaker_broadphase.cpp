#include "recovered_shaker_broadphase.hpp"
#include <stdexcept>
#include <cmath>
namespace study::recovered {
namespace {
ShakerBox extend(ShakerBox box,const ShakerBox &other){
    for(size_t point=0;point<2;++point)for(size_t axis=0;axis<2;++axis){const double value=other[2*point+axis];if(value<box[axis])box[axis]=value;if(box[2+axis]<value)box[2+axis]=value;}
    return box;
}
double area(const ShakerBox &b){return (b[2]-b[0])*(b[3]-b[1]);}
struct Seeds {size_t first,second;double separation;};
Seeds seeds(std::span<const ShakerBroadEntry> entries,size_t axis){
    size_t smallestHigh=0;double minimum=entries[0].box[axis],maximum=entries[0].box[axis+2];
    for(size_t n=1;n<entries.size();++n){const auto &b=entries[n].box;if(b[axis+2]<entries[smallestHigh].box[axis+2])smallestHigh=n;
        // Conditional selection matches original MINSD/MAXSD when unordered.
        minimum=minimum<=b[axis]?minimum:b[axis];maximum=b[axis+2]<=maximum?maximum:b[axis+2];}
    size_t largestLow=smallestHigh==0?1:0;
    for(size_t n=0;n<entries.size();++n)if(n!=smallestHigh&&entries[largestLow].box[axis]<entries[n].box[axis])largestLow=n;
    double separation=entries[largestLow].box[axis]-entries[smallestHigh].box[axis+2];
    const double width=maximum-minimum;if(width>std::numeric_limits<double>::epsilon())separation=separation/width;
    return {largestLow,smallestHigh,separation};
}
ShakerBox nodeBox(const ShakerBroadNode &node){auto box=node.entries.front().box;for(size_t n=1;n<node.entries.size();++n)box=extend(box,node.entries[n].box);return box;}
struct NodeSplit {std::unique_ptr<ShakerBroadNode> sibling;ShakerBox first{},second{};};
NodeSplit insertNode(ShakerBroadNode &node,ShakerBroadEntry item){
    if(node.leaf)node.entries.push_back(item);
    else{
        size_t selected=0;double bestGrowth=std::numeric_limits<double>::max(),bestArea=bestGrowth;
        for(size_t n=0;n<node.entries.size();++n){const auto box=extend(node.entries[n].box,item.box);const double expandedArea=area(box),growth=expandedArea-area(node.entries[n].box);
            if(growth<bestGrowth||(growth==bestGrowth&&expandedArea<bestArea)){selected=n;bestGrowth=growth;bestArea=expandedArea;}}
        node.entries[selected].box=extend(node.entries[selected].box,item.box);
        auto split=insertNode(*node.children[selected],item);
        if(split.sibling){node.entries[selected].box=split.first;node.entries.push_back({split.second,0});node.children.push_back(std::move(split.sibling));}
    }
    if(node.entries.size()<=16)return {};
    auto numbered=node.entries;for(size_t n=0;n<numbered.size();++n)numbered[n].index=n;
    const auto split=shakerBroadSplit(numbered);auto sibling=std::make_unique<ShakerBroadNode>();sibling->leaf=node.leaf;
    auto oldEntries=std::move(node.entries);auto oldChildren=std::move(node.children);
    for(size_t side=0;side<2;++side){auto &target=side?*sibling:node;for(const auto &entry:split.groups[side]){target.entries.push_back(oldEntries[entry.index]);if(!target.leaf)target.children.push_back(std::move(oldChildren[entry.index]));}}
    return {std::move(sibling),split.boxes[0],split.boxes[1]};
}
bool intersects(const ShakerBox &a,const ShakerBox &b){return a[0]<=b[2]&&b[0]<=a[2]&&a[1]<=b[3]&&b[1]<=a[3];}
void queryNode(const ShakerBroadNode &node,const ShakerBox &box,std::vector<size_t> &out){
    for(size_t n=0;n<node.entries.size();++n)if(intersects(box,node.entries[n].box)){if(node.leaf)out.push_back(node.entries[n].index);else queryNode(*node.children[n],box,out);}
}
}
ShakerBroadSplit shakerBroadSplit(std::span<const ShakerBroadEntry> entries){
    if(entries.size()!=17)throw std::invalid_argument("Shaker R-tree split requires 17 entries");
    const auto x=seeds(entries,0),y=seeds(entries,1);const auto chosen=y.separation<=x.separation?x:y;
    ShakerBroadSplit result;result.groups[0].push_back(entries[chosen.first]);result.groups[1].push_back(entries[chosen.second]);result.boxes={entries[chosen.first].box,entries[chosen.second].box};
    std::array<double,2> areas{area(result.boxes[0]),area(result.boxes[1])};size_t remaining=15;
    for(size_t n=0;n<entries.size();++n){if(n==chosen.first||n==chosen.second)continue;
        size_t side;std::array<ShakerBox,2> boxes{extend(result.boxes[0],entries[n].box),extend(result.boxes[1],entries[n].box)};
        const std::array<double,2> newAreas{area(boxes[0]),area(boxes[1])};
        if(result.groups[0].size()+remaining<5)side=0;
        else if(result.groups[1].size()+remaining<5)side=1;
        else{const double a=newAreas[0]-areas[0],b=newAreas[1]-areas[1];
            side=(a<b||(a==b&&(areas[0]<areas[1]||(areas[0]==areas[1]&&result.groups[0].size()<=result.groups[1].size()))))?0:1;}
        result.groups[side].push_back(entries[n]);result.boxes[side]=boxes[side];areas[side]=newAreas[side];--remaining;
    }
    return result;
}
void ShakerBroadTree::insert(ShakerBroadEntry item){
    if(!root)root=std::make_unique<ShakerBroadNode>();auto split=insertNode(*root,item);if(!split.sibling)return;
    auto parent=std::make_unique<ShakerBroadNode>();parent->leaf=false;parent->entries={{split.first,0},{split.second,0}};parent->children.push_back(std::move(root));parent->children.push_back(std::move(split.sibling));root=std::move(parent);++height;
}
std::vector<size_t> ShakerBroadTree::query(const ShakerBox &box) const {std::vector<size_t> result;if(root)queryNode(*root,box,result);return result;}
std::vector<std::array<size_t,2>> shakerBroadphase(std::span<const ShakerBroadItem> items,double radius){
    ShakerBroadTree tree;std::vector<std::array<size_t,2>> pairs;
    for(size_t n=0;n<items.size();++n){const auto &item=items[n];ShakerBox box{};
        for(size_t axis=0;axis<2;++axis){box[axis]=item.first[axis]<=item.second[axis]?item.first[axis]:item.second[axis];box[axis+2]=item.second[axis]<=item.first[axis]?item.first[axis]:item.second[axis];}
        const ShakerBox expanded{box[0]-radius,box[1]-radius,box[2]+radius,box[3]+radius};
        for(auto previous:tree.query(expanded))pairs.push_back({n,previous});tree.insert({box,n});}
    return pairs;
}
}
