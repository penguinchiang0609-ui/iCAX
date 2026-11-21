#include "recovered_convolution_pose.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>
namespace study::recovered {
namespace {
double quiet(double v){return std::bit_cast<double>(std::bit_cast<uint64_t>(v)|0x8000000000000ULL);}
double add(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a+b;}
double sub(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a-b;}
double mul(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a*b;}
double neg(double a){return std::bit_cast<double>(std::bit_cast<uint64_t>(a)^0x8000000000000000ULL);}
ShakerPoint point(const ConvolutionPose &pose,size_t index){
    const auto &g=*pose.geometry;double x=sub(g.points[index][0],g.reference[0]),y=sub(g.points[index][1],g.reference[1]);
    if(pose.angle!=0){const double c=std::cos(pose.angle),s=std::sin(pose.angle);const double py=mul(y,c),px=mul(x,s);const double rotatedY=index==0?add(py,px):add(px,py);x=sub(mul(x,c),mul(y,s));y=rotatedY;}
    if(pose.mirror[0])y=neg(y);if(pose.mirror[1])x=neg(x);return {x,y};
}
}
bool convolutionPoseLess(const ConvolutionPose &a,const ConvolutionPose &b){
    if(a.side!=b.side)return a.side<b.side;
    if(a.geometry->key!=b.geometry->key)return a.geometry->key<b.geometry->key;
    if(a.mirror[0]!=b.mirror[0])return a.mirror[0]<b.mirror[0];
    if(a.mirror[1]!=b.mirror[1])return a.mirror[1]<b.mirror[1];return a.angle<b.angle;
}
ConvolutionTopology convolutionTransform(const ConvolutionPose &pose,const ConvolutionCurveFactory &factory){
    if(!pose.geometry||pose.geometry->points.size()<2)throw std::invalid_argument("Convolution transform needs at least two source points");
    auto result=convolutionTopology(point(pose,0),point(pose,1),factory);
    for(size_t n=2;n<pose.geometry->points.size();++n)convolutionAppend(result,point(pose,n),factory);
    convolutionClose(result,factory);if(pose.mirror[0]!=pose.mirror[1])convolutionReverse(result);return result;
}
struct ConvolutionPosePool::Node {
    Node *left=nullptr,*parent=nullptr,*right=nullptr;
    bool black=true;
    ConvolutionPose pose;
    std::vector<std::unique_ptr<ConvolutionTopology>> available;
};
ConvolutionPosePool::ConvolutionPosePool():head_(std::make_unique<Node>()){head_->left=head_->parent=head_->right=head_.get();}
ConvolutionPosePool::~ConvolutionPosePool()=default;
ConvolutionPosePool::Search ConvolutionPosePool::locate(const ConvolutionPose &pose)const{
    Search result{head_->parent,head_.get(),false};auto current=head_->parent;
    while(current!=head_.get()){result.parent=current;result.left=!convolutionPoseLess(current->pose,pose);if(result.left){result.bound=current;current=current->left;}else current=current->right;}return result;
}
void ConvolutionPosePool::rotateLeft(Node *n){auto p=n->right;n->right=p->left;if(p->left!=head_.get())p->left->parent=n;p->parent=n->parent;if(n==head_->parent)head_->parent=p;else if(n==n->parent->left)n->parent->left=p;else n->parent->right=p;p->left=n;n->parent=p;}
void ConvolutionPosePool::rotateRight(Node *n){auto p=n->left;n->left=p->right;if(p->right!=head_.get())p->right->parent=n;p->parent=n->parent;if(n==head_->parent)head_->parent=p;else if(n==n->parent->right)n->parent->right=p;else n->parent->left=p;p->right=n;n->parent=p;}
ConvolutionPosePool::Node *ConvolutionPosePool::insert(const ConvolutionPose &pose,Search search){
    auto owned=std::make_unique<Node>();auto node=owned.get();node->pose=pose;node->left=node->right=head_.get();node->parent=search.parent;node->black=false;nodes_.push_back(std::move(owned));
    if(search.parent==head_.get()){head_->left=head_->parent=head_->right=node;node->black=true;return node;}
    if(search.left){search.parent->left=node;if(search.parent==head_->left)head_->left=node;}else{search.parent->right=node;if(search.parent==head_->right)head_->right=node;}
    auto n=node;while(!n->parent->black){auto p=n->parent,g=p->parent;
        if(p==g->left){auto u=g->right;if(!u->black){p->black=u->black=true;g->black=false;n=g;}else{if(n==p->right){n=p;rotateLeft(n);}n->parent->black=true;n->parent->parent->black=false;rotateRight(n->parent->parent);}}
        else{auto u=g->left;if(!u->black){p->black=u->black=true;g->black=false;n=g;}else{if(n==p->left){n=p;rotateRight(n);}n->parent->black=true;n->parent->parent->black=false;rotateLeft(n->parent->parent);}}
    }head_->parent->black=true;return node;
}
std::unique_ptr<ConvolutionTopology> ConvolutionPosePool::acquire(const ConvolutionPose &pose,const ConvolutionCurveFactory &factory){
    {std::lock_guard lock(mutex_);auto n=locate(pose).bound;if(n!=head_.get()&&!convolutionPoseLess(pose,n->pose)&&!n->available.empty()){auto result=std::move(n->available.back());n->available.pop_back();return result;}}
    // Construction happens after unlocking the cache, matching 721280.
    return std::make_unique<ConvolutionTopology>(convolutionTransform(pose,factory));
}
void ConvolutionPosePool::release(const ConvolutionPose &pose,std::unique_ptr<ConvolutionTopology> value){
    std::lock_guard lock(mutex_);const auto found=locate(pose);auto n=found.bound;if(n==head_.get()||convolutionPoseLess(pose,n->pose))n=insert(pose,found);n->available.push_back(std::move(value));
}
std::vector<ConvolutionPosePool::Entry> ConvolutionPosePool::snapshot()const{
    std::lock_guard lock(mutex_);std::vector<Entry> result;
    auto walk=[&](auto &&self,const Node *n)->void{Entry entry;entry.black=n->black;if(n!=head_.get()){entry.pose=&n->pose;for(auto &p:n->available)entry.available.push_back(p.get());}result.push_back(std::move(entry));if(n!=head_.get()){self(self,n->left);self(self,n->right);}};walk(walk,head_->parent);return result;
}
void ConvolutionPosePool::clear() {
    std::lock_guard lock(mutex_);
    nodes_.clear();
    head_->left = head_->parent = head_->right = head_.get();
}
}
