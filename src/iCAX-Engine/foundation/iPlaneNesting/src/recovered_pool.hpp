#pragma once
#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace study::recovered {
// Inputs at the original comparator's dependency boundaries. correctedLength
// must be evaluated before a record is published to a pool (4465c0).
struct CandidateMetrics {
    double metric=0;
    int64_t correctedLength=0;
    int32_t integerMetric=0;
    double auxiliary=0; // CandidateBox +10; not a bounding-box area.
    int32_t sourceId=0,trial=0;
};
bool metricBetter(double a,double b); // 446fb0
int candidateTie(const CandidateMetrics &,const CandidateMetrics &); // 446bc0
bool candidateLess(const CandidateMetrics &,const CandidateMetrics &,
                   const std::function<int32_t()> &layoutDifference,bool useSourceId=true); // 4470a0
struct CandidateIntegerTerm {int32_t placement50=0,objectF8=0;};
int32_t candidateInteger(const std::vector<CandidateIntegerTerm> &,double sheet78); // 3cbb10
struct CandidateLengthInput {
    int64_t maximumRight=0;
    double trim70=0,width08=0,height10=0,config10=1;
    bool config3c=false;
    // Explicit unresolved providers 3cc260 and 3cbe50, evaluated in that order.
    std::function<double()> quantity3cc260,quantity3cbe50;
};
int64_t candidateLength(const CandidateLengthInput &); // 3cc380
int64_t rawCandidateLength(int64_t maximumRight,double trim); // 3cc1f0
int64_t referenceCandidateLength(int64_t corrected,double field14e8,double factor); // 447850
// 4478f0. Optional Supervisor evaluator after the first Shaker phase; this
// does not replace the normal local/global candidate-pool comparator.
bool expectedCandidateBetter(const CandidateMetrics &, double firstShake,
    const CandidateMetrics &, double secondShake, double measuredGain);
struct ShakerGainInput {
    double target14b8 = 0, queue20 = 0, placed10 = 0;
    int64_t originalLength10 = 0, maximumRight = 0;
    double trim70 = 0;
};
double measuredShakerGain(const ShakerGainInput &); // 5a5c50..5a5d03
double medianShakerGain(std::span<const double>); // 5a5d65..5a5de9, upper middle
bool useExpectedShakerGain(double); // 5a5e5f, strictly > .0001
struct CorrectedLengthCache {bool valid=false;int64_t value=0;};
void evaluateCorrectedLength(CorrectedLengthCache &,const CandidateLengthInput &,const std::function<void()> &refreshStrip); // 446e60
struct ProcessPlacement;
struct PlacementGroup {std::vector<std::weak_ptr<ProcessPlacement>> members;};
struct ProcessEdge {std::weak_ptr<ProcessPlacement> first,second;double field20=0;};
struct ProcessPlacement {
    uintptr_t group08=0;
    uintptr_t instance40=0;
    bool optional=false;
    double geometry60=0;
    std::vector<ProcessEdge> edges;
    // nullopt represents absent/empty group, otherwise its first member.
    std::optional<std::weak_ptr<ProcessPlacement>> groupFirst;
    std::shared_ptr<PlacementGroup> group58;
};
bool processEdgeActive(const ProcessEdge &); // 4927e0, live-owner snapshot domain
bool processEdgeOwned(const ProcessEdge &,const ProcessPlacement *); // 492930
double candidateAuxiliary(const ProcessPlacement &); // 3c57e0
void linkCommonCut(const std::shared_ptr<ProcessPlacement> &,const std::shared_ptr<ProcessPlacement> &,double); // 3c5a00
using PlacementCopyLookup=std::function<std::optional<std::shared_ptr<ProcessPlacement>>(uintptr_t)>;
// 3c85d0: missing lookup entry skips an edge; a present null placement is an
// assertion notification. Existing destination edges are retained.
double copyPlacementProcesses(const ProcessPlacement &,const std::shared_ptr<ProcessPlacement> &,
    const PlacementCopyLookup &,const std::function<void()> &missingPlacement={});
void copyPlacementRelations(const ProcessPlacement &,const std::shared_ptr<ProcessPlacement> &,
    const PlacementCopyLookup &); // 3c8a90, preserves source group slots and copied group identity
std::array<double,3> candidateProcessQuantities(const std::vector<std::shared_ptr<ProcessPlacement>> &); // 3cbc30
struct StripQuantityCache {bool valid=false;double field88=0,field90=0;};
using StripColumn=std::array<int32_t,2>;
std::vector<StripColumn> stripColumns(std::span<const int32_t>,int32_t initialHeight); // 3acd30
int64_t stripAggregate(std::span<const StripColumn>); // 3aca80
void refreshStripQuantities(StripQuantityCache &,std::span<const int32_t>,int32_t initialHeight,int64_t field80,int64_t pitch230); // arithmetic in 3cda70
double candidateQuantity260(int64_t maximumRight,double trim,double width,double height,bool config3c,StripQuantityCache &,const std::function<void(StripQuantityCache &)> &refresh); // 3cc260
double candidateQuantityE50(const std::array<double,3> &,double informer20,double config18,double config20); // 3cbe50
struct LayoutPlacement {size_t type=0;int64_t x48=0;int32_t y50=0;};
struct LayoutSignature {
    uintptr_t sheetIdentity=0;
    int32_t placementCount=0;
    int64_t maximumRight=0;
    double height10=0,trim70=0;
    size_t typeCount=0;
    std::vector<size_t> typeOrder;
    std::vector<LayoutPlacement> placements;
};
using LayoutPoints=std::vector<std::array<double,2>>;
struct LayoutGroups {LayoutPoints points;std::vector<int32_t> starts,counts;};
LayoutGroups layoutGroups(const LayoutSignature &); // 3d0390
double layoutGroupDistance(LayoutPoints &,LayoutPoints &); // 3d01d0, finite coordinate domain
int32_t layoutDifference(const LayoutSignature &,const LayoutSignature &); // 3d0840
struct RelatedPlacement {
    LayoutPlacement placement;
    double quantity220 = -1, area58 = 0;
    bool optional3c1 = false;
};
// 3d0b70. The default pool notification predicate compares the second
// candidate's ordered 80%-value prefix with first-candidate type buckets.
// Prefix trimming excludes the first member that crosses the threshold.
bool relatedCandidates(const LayoutSignature &first, uintptr_t secondSheet,
    double sheetWidth, double sheetHeight, double secondMetric18,
    std::span<const RelatedPlacement> secondPlacements);

// Original lower-bound search, red-black topology and bounded pool lifecycle.
// A tolerance comparator need not be transitive: replacing this tree with a
// sort, std::set, or a linear scan changes observable comparison paths.
// Caller owns synchronization, matching PoolInsert's unlocked contract.
template<class Value> class CandidatePool {
public:
    using Less=std::function<bool(const Value &,const Value &)>;
    using Related=Less;
    struct Inserted {bool inserted=false,newBest=false;};
    struct Shape {std::optional<Value> value;bool black;};
    explicit CandidatePool(uint32_t limit,Less less):limit_(limit),less_(std::move(less)) {
        if(!limit_ || !less_)throw std::invalid_argument("pool needs a positive limit and comparator");
        resetHead();
    }
    ~CandidatePool(){clear();}
    CandidatePool(const CandidatePool &)=delete;
    CandidatePool &operator=(const CandidatePool &)=delete;
    size_t size()const{return size_;}
    uint32_t limit()const{return limit_;}
    const Value *best()const{return size_?&*head_.left->value:nullptr;}
    const Value *worst()const{return size_?&*head_.right->value:nullptr;}
    std::vector<Value> values()const {
        std::vector<Value> out;out.reserve(size_);
        for(auto n=head_.left;n!=&head_;n=next(n))out.push_back(*n->value);
        return out;
    }
    // Diagnostic preorder includes null leaves, so tests compare full topology.
    std::vector<Shape> shape()const {
        std::vector<Shape> out;
        auto walk=[&](auto &&self,const Node *n)->void {
            out.push_back({n->value,n->black});
            if(n!=&head_){self(self,n->left);self(self,n->right);}
        };walk(walk,head_.parent);return out;
    }
    Inserted insert(const Value &value) { // 3b66d0
        Node *added=nullptr;
        if(size_<limit_ || less_(value,*head_.right->value))added=treeInsert(value);
        if(size_>limit_)erase(head_.right);
        return {added!=nullptr,added && added==head_.left};
    }
    // Unbounded map operations reuse the same 3576d0/36d3a0 tree. They do
    // not apply the candidate pool's ranking limit or eviction policy.
    const Value *findEquivalent(const Value &value)const {
        auto current=head_.parent,bound=const_cast<Node*>(&head_);
        while(current!=&head_){if(!less_(*current->value,value)){bound=current;current=current->left;}else current=current->right;}
        return bound!=&head_&&!less_(value,*bound->value)?&*bound->value:nullptr;
    }
    const Value &findOrInsert(const Value &value){if(auto old=findEquivalent(value))return *old;return *treeInsert(value)->value;}
    template<class Predicate> void eraseWhere(Predicate predicate){
        for(auto current=head_.left;current!=&head_;){auto following=next(current);if(predicate(*current->value))erase(current);current=following;}
    }
    Inserted notify(const Value &value,const Related &related) { // 3b62c0
        std::vector<Node*> removed;
        for(auto n=head_.left;n!=&head_;n=next(n))if(related(*n->value,value)) {
            // Rejection is atomic: do not erase previously collected nodes.
            if(!less_(value,*n->value))return {};
            removed.push_back(n);
        }
        for(auto n:removed)erase(n);
        return insert(value);
    }
    void rebuild(std::optional<uint32_t> limit={}) { // 3b7c80
        if(limit && !*limit)throw std::invalid_argument("zero-capacity original pool has undefined behavior");
        const auto old=values();if(limit)limit_=*limit;clear();
        for(const auto &value:old)insert(value);
    }
    void clear() {
        auto release=[&](auto &&self,Node *n)->void {
            if(n==&head_)return;self(self,n->left);self(self,n->right);delete n;
        };release(release,head_.parent);size_=0;resetHead();
    }
private:
    struct Node {Node *left=nullptr,*parent=nullptr,*right=nullptr;bool black=true;std::optional<Value> value;};
    Node head_;size_t size_=0;uint32_t limit_;Less less_;
    void resetHead(){head_.left=head_.right=head_.parent=&head_;head_.black=true;}
    Node *minimum(Node *n)const{while(n->left!=&head_)n=n->left;return n;}
    Node *maximum(Node *n)const{while(n->right!=&head_)n=n->right;return n;}
    Node *next(Node *n)const {
        if(n->right!=&head_)return minimum(n->right);
        auto p=n->parent;while(p!=&head_ && n==p->right){n=p;p=p->parent;}return p;
    }
    void rotateLeft(Node *n) {
        auto p=n->right;n->right=p->left;if(p->left!=&head_)p->left->parent=n;
        p->parent=n->parent;
        if(n==head_.parent)head_.parent=p;else if(n==n->parent->left)n->parent->left=p;else n->parent->right=p;
        p->left=n;n->parent=p;
    }
    void rotateRight(Node *n) {
        auto p=n->left;n->left=p->right;if(p->right!=&head_)p->right->parent=n;
        p->parent=n->parent;
        if(n==head_.parent)head_.parent=p;else if(n==n->parent->right)n->parent->right=p;else n->parent->left=p;
        p->right=n;n->parent=p;
    }
    Node *treeInsert(const Value &value) { // 3b78d0 -> 3576d0
        auto parent=&head_,bound=&head_,current=head_.parent;bool left=false;
        while(current!=&head_) {
            parent=current;left=!less_(*current->value,value);
            if(left){bound=current;current=current->left;}else current=current->right;
        }
        if(bound!=&head_ && !less_(value,*bound->value))return nullptr;
        auto node=new Node{&head_,parent,&head_,false,value};++size_;
        if(parent==&head_){head_.left=head_.parent=head_.right=node;node->black=true;return node;}
        if(left){parent->left=node;if(parent==head_.left)head_.left=node;}
        else{parent->right=node;if(parent==head_.right)head_.right=node;}
        auto n=node;
        while(!n->parent->black) {
            auto p=n->parent,g=p->parent;
            if(p==g->left) {
                auto u=g->right;
                if(!u->black){p->black=u->black=true;g->black=false;n=g;}
                else{if(n==p->right){n=p;rotateLeft(n);}n->parent->black=true;n->parent->parent->black=false;rotateRight(n->parent->parent);}
            }else{
                auto u=g->left;
                if(!u->black){p->black=u->black=true;g->black=false;n=g;}
                else{if(n==p->left){n=p;rotateRight(n);}n->parent->black=true;n->parent->parent->black=false;rotateLeft(n->parent->parent);}
            }
        }
        head_.parent->black=true;return node;
    }
    void erase(Node *z) { // 36d3a0, including successor-node transplant
        auto y=z,x=&head_,xp=&head_;
        if(z->left==&head_)x=z->right;
        else if(z->right==&head_)x=z->left;
        else{y=minimum(z->right);x=y->right;}
        if(y==z) {
            xp=z->parent;if(x!=&head_)x->parent=xp;
            if(z==head_.parent)head_.parent=x;else if(z==xp->left)xp->left=x;else xp->right=x;
            if(z==head_.left)head_.left=x==&head_?xp:minimum(x);
            if(z==head_.right)head_.right=x==&head_?xp:maximum(x);
        }else{
            z->left->parent=y;y->left=z->left;
            if(y==z->right)xp=y;
            else{xp=y->parent;if(x!=&head_)x->parent=xp;xp->left=x;y->right=z->right;z->right->parent=y;}
            if(z==head_.parent)head_.parent=y;else if(z==z->parent->left)z->parent->left=y;else z->parent->right=y;
            y->parent=z->parent;std::swap(y->black,z->black);
        }
        if(z->black) {
            while(x!=head_.parent && x->black) {
                if(x==xp->left) {
                    auto w=xp->right;
                    if(!w->black){w->black=true;xp->black=false;rotateLeft(xp);w=xp->right;}
                    if(w!=&head_) {
                        if(w->left->black && w->right->black)w->black=false;
                        else{
                            if(w->right->black){w->left->black=true;w->black=false;rotateRight(w);w=xp->right;}
                            w->black=xp->black;xp->black=true;w->right->black=true;rotateLeft(xp);break;
                        }
                    }
                }else{
                    auto w=xp->left;
                    if(!w->black){w->black=true;xp->black=false;rotateRight(xp);w=xp->left;}
                    if(w!=&head_) {
                        if(w->right->black && w->left->black)w->black=false;
                        else{
                            if(w->left->black){w->right->black=true;w->black=false;rotateLeft(w);w=xp->left;}
                            w->black=xp->black;xp->black=true;w->left->black=true;rotateRight(xp);break;
                        }
                    }
                }
                x=xp;xp=xp->parent;
            }
            x->black=true;
        }
        --size_;delete z;
    }
};
}
