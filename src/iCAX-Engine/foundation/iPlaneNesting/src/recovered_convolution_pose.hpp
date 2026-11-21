#pragma once
#include "recovered_convolution_line.hpp"
#include <mutex>
namespace study::recovered {
struct ConvolutionGeometry {
    int32_t key=0;
    ShakerPoint reference{};
    std::vector<ShakerPoint> points;
};
struct ConvolutionPose {
    std::shared_ptr<const ConvolutionGeometry> geometry;
    double angle=0;
    std::array<uint8_t,2> mirror{};
    int32_t side=0;
};
bool convolutionPoseLess(const ConvolutionPose &,const ConvolutionPose &); // 71f740
ConvolutionTopology convolutionTransform(const ConvolutionPose &,const ConvolutionCurveFactory &); // geometry branch of 721280
// Lookup uses the original lower-bound path and insertion balancing. In
// particular NaN angles are unordered; a standard ordered container would
// impose an unsupported strict-weak-order assumption on these inputs.
class ConvolutionPosePool {
public:
    struct Entry {const ConvolutionPose *pose=nullptr;bool black=true;std::vector<const ConvolutionTopology*> available;};
    ConvolutionPosePool();
    ~ConvolutionPosePool();
    ConvolutionPosePool(const ConvolutionPosePool &)=delete;
    ConvolutionPosePool &operator=(const ConvolutionPosePool &)=delete;
    std::unique_ptr<ConvolutionTopology> acquire(const ConvolutionPose &,const ConvolutionCurveFactory &); // 721280
    void release(const ConvolutionPose &,std::unique_ptr<ConvolutionTopology>); // 7215d0
    void clear(); // pose tree and retained topology in 720cf0
    std::vector<Entry> snapshot()const;
private:
    struct Node;
    struct Search {Node *parent,*bound;bool left;};
    std::unique_ptr<Node> head_;
    std::vector<std::unique_ptr<Node>> nodes_;
    mutable std::recursive_mutex mutex_;
    Search locate(const ConvolutionPose &)const; // 723c20
    Node *insert(const ConvolutionPose &,Search); // 3576d0
    void rotateLeft(Node *),rotateRight(Node *);
};
}
