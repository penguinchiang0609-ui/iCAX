#pragma once
#include "recovered_convolution_pose.hpp"
#include "recovered_pool.hpp"
#include <shared_mutex>
#include <unordered_map>
namespace study::recovered {
struct ConvolutionKey {std::array<ConvolutionPose,2> poses;double expansion=0;};
bool convolutionKeyLess(const ConvolutionKey &,const ConvolutionKey &); // 71f7f0
uint64_t convolutionReferenceHash(double); // 536b50
bool convolutionGeometryEqual(std::span<const ShakerPoint>,std::span<const ShakerPoint>); // 360dc0
uint64_t convolutionGeometryHash(const ConvolutionGeometry &); // hash path in 720e60
class ConvolutionGeometryCatalog {
public:
    using Geometry=std::shared_ptr<const ConvolutionGeometry>;
    struct Snapshot {
        std::unordered_map<const ConvolutionGeometry*,uint64_t> hashes;
        std::unordered_map<const ConvolutionGeometry*,Geometry> aliases;
        std::unordered_map<uint64_t,Geometry> representatives;
    };
    Geometry canonicalize(Geometry); // 720e60
    Snapshot snapshot()const;
private:
    mutable std::mutex mutex_;
    Snapshot state_;
    std::unordered_map<const ConvolutionGeometry*,Geometry> retained_;
};
struct ConvolutionSymmetry {int32_t type=1;bool flag=false,mirror=false;int32_t angle=0;int64_t geometryKey=0;int32_t reflectionAngle=0;};
ConvolutionSymmetry convolutionPoseSymmetry(const ConvolutionPose &); // 721e40
struct ConvolutionBoundaryData {
    std::vector<ShakerBoundary> records;
    std::atomic<int64_t> lastAccess=0;
};
struct ConvolutionCacheAccounting {
    int32_t bytes=0;
    int64_t peak=0;
    mutable std::shared_mutex mutex;
};
class ConvolutionCache {
public:
    struct Entry {ConvolutionKey key;std::shared_ptr<ConvolutionBoundaryData> data;};
    using Value=std::shared_ptr<Entry>;
    using Shape=CandidatePool<Value>::Shape;
    ConvolutionCache(ConvolutionCacheAccounting &,std::function<int64_t()> clock100ns,int32_t initialBytes=0);
    std::shared_ptr<ConvolutionBoundaryData> get(const ConvolutionKey &); // 7205c0
    std::shared_ptr<ConvolutionBoundaryData> put(const ConvolutionKey &,std::shared_ptr<ConvolutionBoundaryData>); // 71ff00
    std::vector<Shape> snapshot()const;
    int32_t bytes()const;
    void clear(); // 7206b0, including global byte accounting
private:
    ConvolutionCacheAccounting &accounting_;
    std::function<int64_t()> clock_;
    mutable std::shared_mutex mutex_;
    CandidatePool<Value> entries_;
    int32_t bytes_;
    void touch(const std::shared_ptr<ConvolutionBoundaryData> &);
};
enum class ConvolutionBackend {SourceEdges=0,SymmetricPolygons=1,IntegerPolygons=2};
class ConvolutionSymmetryCache;
// Sourced and integer-polygon branches of 722010. boundary() accepts canonical
// geometry identities; get() performs 720e60 identity canonicalization and
// 7217e0 output copying. Mode 1 uses the order-one descriptors from 721e40.
class ConvolutionStraightRuntime {
public:
    ConvolutionStraightRuntime(ConvolutionCacheAccounting &,std::function<int64_t()> clock100ns,std::function<int64_t()> clockNanoseconds,bool symmetryEnabled=true);
    ~ConvolutionStraightRuntime();
    std::shared_ptr<ConvolutionBoundaryData> boundary(const ConvolutionKey &,double &elapsedSeconds,ConvolutionBackend=ConvolutionBackend::SourceEdges);
    std::vector<ShakerBoundary> get(ConvolutionKey &,double &elapsedSeconds,ConvolutionBackend=ConvolutionBackend::SourceEdges); // 7217e0
    void clear(); // 720cf0, retains canonical geometry and symmetry entries
private:
    ConvolutionGeometryCatalog geometries_;
    ConvolutionCurves curves_;
    ConvolutionPosePool poses_;
    ConvolutionCache cache_;
    ConvolutionCache polygonCache_;
    std::unique_ptr<ConvolutionSymmetryCache> symmetryCache_;
    bool symmetryEnabled_;
    std::function<int64_t()> clock_;
};
}
