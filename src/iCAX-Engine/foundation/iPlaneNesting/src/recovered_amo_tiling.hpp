#pragma once
#include "recovered_general_tiling.hpp"
#include <functional>
namespace study::recovered {
struct AmoTilingBounds {
    bool empty=true;
    std::array<uint8_t,7> reserved{};
    Box box{};
};
static_assert(sizeof(AmoTilingBounds)==40);
struct AmoTilingAxisOccupancy {int32_t count=0;double used=0;};
AmoTilingAxisOccupancy amoTilingAxisOccupancy(double low,double high,double period,double extent); //8b6f30
struct AmoTilingOccupancy {
    int32_t count=0;
    TilingRepetitionCounts repetitions;
    Point used{};
};
AmoTilingOccupancy amoTilingRectangleOccupancy(AmoTilingBounds first,AmoTilingBounds second,
    Point period,Point extent); //8b6ff0
struct AmoTilingCandidate {
    Point translation{};
    double angle=0; // degrees, converted by angle*pi/180 before member geometry
    Point period{};
    double cost=0,secondary=0;
    int32_t repeatX=0,repeatY=0;
    uintptr_t extraX=0,extraY=0;
};
static_assert(sizeof(AmoTilingCandidate)==80);
struct AmoTilingDescriptor {
    Point translation{};
    double angle=0;
    Point period{};
    int32_t repeatX=0,repeatY=0;
    std::array<std::array<int32_t,2>,2> extra{}; // per member X/Y
    bool coincidentAxis=false;
    std::array<uint8_t,7> reserved{};
};
static_assert(sizeof(AmoTilingDescriptor)==72);
AmoTilingDescriptor amoTilingResultDescriptor(const AmoTilingCandidate &,std::array<uintptr_t,2> memberIds); //491e80
struct AmoTilingShape {
    std::vector<std::vector<Point>> rings;
    AmoTilingBounds bounds;
};
AmoTilingBounds amoTilingShapeBounds(const AmoTilingShape &,double angle=0); //8bb730 / b7dc20
struct AmoTilingJob {
    std::array<AmoTilingShape,2> members;
    std::map<double,std::array<AmoTilingBounds,2>> angleBounds;
    // Explicit source object identities, including self-pairs. Null entries
    // select the corresponding owned member; sources must outlive the job.
    std::array<const AmoTilingShape *,2> memberSources{};
};
std::array<const AmoTilingShape *,2> amoTilingMemberSources(const AmoTilingJob &);
std::array<AmoTilingBounds,2> amoTilingMemberBounds(AmoTilingJob &,const AmoTilingCandidate &); //8c00c0
struct AmoTilingCriterion {
    double width=0,height=0;
    int32_t firstDemand=0,secondDemand=0;
    double partialWeight=0;
};
void amoTilingFamilyRepetitions(const AmoTilingCriterion &,std::array<uintptr_t,2> memberIds,
    std::array<AmoTilingBounds,2> transformedMembers,AmoTilingCandidate &); //8b77b0
void amoTilingFamilyRanges(const AmoTilingCriterion &,std::array<uintptr_t,2> memberIds,
    std::array<AmoTilingBounds,2> transformedMembers,AmoTilingCandidate &); //8b73d0
struct AmoTilingRank {
    int64_t cost=0,secondary=0;
    bool axisAlignedTranslation=false;
};
using AmoTilingCandidatePtr=std::shared_ptr<AmoTilingCandidate>;
AmoTilingCandidatePtr amoTilingFamilyMidpoint(const AmoTilingCandidatePtr &,const AmoTilingCandidatePtr &); //8bd420
struct AmoTilingRankCache {
    double tolerance=1;
    std::map<const AmoTilingCandidate *,std::pair<AmoTilingCandidatePtr,AmoTilingRank>> entries;
};
const AmoTilingRank &amoTilingCandidateRank(AmoTilingRankCache &,const AmoTilingCandidatePtr &); //8bf4f0
bool amoTilingCandidateBetter(AmoTilingRankCache &,const AmoTilingCandidatePtr &,const AmoTilingCandidatePtr &); //8bf330
struct AmoTilingSearchState {
    uint32_t calls=0;
    AmoTilingRankCache ranks;
};
struct AmoTilingFamilyCallbacks {
    std::function<AmoTilingCandidatePtr(const AmoTilingCandidatePtr &,const AmoTilingCandidatePtr &)> midpoint;
    std::function<void(const AmoTilingCandidatePtr &)> evaluate;
};
struct AmoTilingSeedCallbacks {
    std::function<double(const AmoTilingCandidatePtr &)> periodY,periodX;
    std::function<void(const AmoTilingCandidatePtr &)> finalize;
};
void amoTilingFamilySeeds(const AmoTilingSeedCallbacks &,const AmoTilingCandidatePtr &); //8b6ec0
AmoTilingCandidatePtr amoTilingBisect(AmoTilingSearchState &,const AmoTilingFamilyCallbacks &,
    const AmoTilingCandidatePtr &,const AmoTilingCandidatePtr &,int32_t budget); //8b6a90
AmoTilingCandidatePtr amoTilingOptimizeFamily(AmoTilingSearchState &,const AmoTilingFamilyCallbacks &,
    std::span<const AmoTilingCandidatePtr>,size_t best); //8b66d0
struct AmoTilingAngleRequest {
    double minimum=0,maximum=0,step=0;
    bool shapeEdges=false;
};
std::vector<double> amoTilingAngles(const AmoTilingAngleRequest &,
    const std::array<std::vector<double>,2> &shapeEdgeAngles={}); //8bd100, edge-angle source is 8ba130
std::vector<double> amoTilingShapeEdgeAngles(const AmoTilingShape &); //8ba130 / 8ba0a0
AmoTilingCandidatePtr amoTilingImproveTranslation(AmoTilingJob &,const AmoTilingCandidatePtr &); //8b5c30 / 8b5940
void amoTilingSortCandidates(std::vector<AmoTilingCandidatePtr> &,AmoTilingRankCache &,size_t depth=SIZE_MAX); //8b82d0
using AmoTilingFamilies=std::vector<std::vector<AmoTilingCandidatePtr>>;
AmoTilingFamilies amoTilingPrepareAngles(const AmoTilingAngleRequest &,
    const std::array<std::vector<double>,2> &shapeEdgeAngles,
    const std::function<AmoTilingFamilies(double)> &prepare); //8bd380
struct AmoTilingCriterionCallbacks {
    std::function<AmoTilingFamilies()> prepare;
    AmoTilingFamilyCallbacks family;
    std::function<void(const AmoTilingCandidatePtr &)> finalize;
};
void amoTilingCriterionSearch(AmoTilingSearchState &,AmoTilingJob &,const AmoTilingCriterionCallbacks &,
    std::vector<AmoTilingCandidatePtr> &output); //8b61d0 / 8b60f0
}
