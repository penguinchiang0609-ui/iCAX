#include "recovered_convolution_filter.hpp"
#include <algorithm>
#include <stdexcept>
namespace study::recovered {
namespace {
void expand(ConvolutionFilterBox &a,const ConvolutionFilterBox &b){for(size_t k=0;k<2;++k){if(b[k]<a[k])a[k]=b[k];if(a[k+2]<b[k+2])a[k+2]=b[k+2];}}
struct BulkEntry {std::array<double,2> center;ConvolutionFilterChildEntry entry;};
class ChildBulkBuilder {
    std::vector<BulkEntry> entries;
    void median(size_t a,size_t b,size_t c,size_t axis){const auto less=[&](size_t i,size_t j){return entries[i].center[axis]<entries[j].center[axis];};if(less(b,a))std::swap(entries[b],entries[a]);if(less(c,b)){std::swap(entries[c],entries[b]);if(less(b,a))std::swap(entries[b],entries[a]);}}
    std::pair<size_t,size_t> partition(size_t first,size_t last,size_t axis){
        const auto less=[&](size_t a,size_t b){return entries[a].center[axis]<entries[b].center[axis];};const auto mid=first+(last-first)/2;
        if(last-first>41){const auto step=(last-first)/8;median(first,first+step,first+2*step,axis);median(mid-step,mid,mid+step,axis);median(last-1-2*step,last-1-step,last-1,axis);median(first+step,mid,last-1-step,axis);}else median(first,mid,last-1,axis);
        size_t pfirst=mid,plast=mid+1;while(first<pfirst&&!less(pfirst-1,pfirst)&&!less(pfirst,pfirst-1))--pfirst;while(plast<last&&!less(plast,pfirst)&&!less(pfirst,plast))++plast;
        size_t right=plast,left=pfirst;
        for(;;){for(;right<last;++right){if(less(pfirst,right))continue;if(less(right,pfirst))break;if(plast!=right)std::swap(entries[plast],entries[right]);++plast;}
            for(;first<left;--left){if(less(left-1,pfirst))continue;if(less(pfirst,left-1))break;--pfirst;if(pfirst!=left-1)std::swap(entries[pfirst],entries[left-1]);}
            if(left==first&&right==last)return {pfirst,plast};
            if(left==first){if(plast!=right)std::swap(entries[pfirst],entries[plast]);++plast;std::swap(entries[pfirst++],entries[right++]);}
            else if(right==last){--left;--pfirst;if(left!=pfirst)std::swap(entries[left],entries[pfirst]);std::swap(entries[pfirst],entries[--plast]);}
            else std::swap(entries[right++],entries[--left]);}
    }
    void nth(size_t first,size_t middle,size_t last,size_t axis){if(middle==last)return;while(last-first>32){const auto [a,b]=partition(first,last,axis);if(middle<b){if(a<=middle)return;last=a;}else first=b;}
        for(size_t n=first+1;n<last;++n){auto item=entries[n];size_t j=n;while(j>first&&item.center[axis]<entries[j-1].center[axis]){entries[j]=entries[j-1];--j;}entries[j]=item;}}
    std::pair<std::unique_ptr<ConvolutionFilterChildNode>,ConvolutionFilterBox> node(size_t first,size_t last,ConvolutionFilterBox bounds,size_t capacity,size_t minimum){
        auto result=std::make_unique<ConvolutionFilterChildNode>();ConvolutionFilterBox actual{};
        if(capacity<2){result->leaf=true;for(size_t n=first;n<last;++n){const auto b=entries[n].entry.box;if(n==first)actual=b;else expand(actual,b);result->entries.push_back(entries[n].entry);}}
        else {result->leaf=false;const auto append=[&](auto &&self,size_t begin,size_t end,ConvolutionFilterBox region)->void {
            const auto count=end-begin;if(count<=capacity){auto [child,b]=node(begin,end,region,capacity/16,minimum/16);if(result->entries.empty())actual=b;else expand(actual,b);result->entries.push_back({b,0});result->children.push_back(std::move(child));return;}
            size_t groups=count/capacity,split=(groups/2)*capacity;
            if(count%capacity){if(count%capacity<minimum){const auto remaining=count-minimum;groups=remaining/capacity;split=remaining%capacity;if(split){if(groups)split=((groups+2)/2)*capacity;}else split=((groups+1)/2)*capacity;}else split=((groups+1)/2)*capacity;}
            const size_t axis=region[2]-region[0]<region[3]-region[1];nth(begin,begin+split,end,axis);const auto cut=(region[axis+2]-region[axis])*0.5+region[axis];auto left=region,right=region;left[axis+2]=cut;right[axis]=cut;self(self,begin,begin+split,left);self(self,begin+split,end,right);
        };append(append,first,last,bounds);}
        return {std::move(result),actual};
    }
public:
    void build(ConvolutionFilterChildIndex &out,std::span<const ConvolutionFilterChildEntry> input){out.root.reset();out.count=input.size();out.height=0;if(input.empty())return;ConvolutionFilterBox bounds=input.front().box;
        for(const auto &s:input){entries.push_back({{(s.box[0]+s.box[2])*0.5,(s.box[1]+s.box[3])*0.5},s});expand(bounds,s.box);}
        size_t capacity=1,next=16;while(next<input.size()){capacity=next;next*=16;++out.height;}out.root=node(0,entries.size(),bounds,capacity,(capacity/16)*4).first;
    }
};
}
void ConvolutionFilterChildIndex::build(std::span<const ConvolutionFilterChildEntry> entries){ChildBulkBuilder().build(*this,entries);}
std::vector<size_t> ConvolutionFilterChildIndex::query(const ConvolutionFilterBox &q,bool contained)const{
    std::vector<size_t> out;const auto contains=[](const auto &a,const auto &b){return a[0]<=b[0]&&a[1]<=b[1]&&b[2]<=a[2]&&b[3]<=a[3];};
    const auto visit=[&](auto &&self,const ConvolutionFilterChildNode &node)->void {for(size_t n=0;n<node.entries.size();++n){const auto &b=node.entries[n].box;if(node.leaf){const bool match=contained?contains(q,b)&&b[0]<b[2]&&b[1]<b[3]:contains(b,q)&&q[0]<q[2]&&q[1]<q[3];if(match)out.push_back(node.entries[n].index);}else{const bool match=contained?q[0]<=b[2]&&b[0]<=q[2]&&q[1]<=b[3]&&b[1]<=q[3]:contains(b,q);if(match)self(self,*node.children[n]);}}};if(root)visit(visit,*root);return out;
}
}
