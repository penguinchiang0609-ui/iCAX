#pragma once
#include "recovered_shaker.hpp"
#include <cstddef>
namespace study::recovered {
struct ShakerBoundary {
    ShakerPoint start{},end{};
    ShakerAnchor sourceA{},sourceB{};
    bool sourceAElement=false,sourceBElement=false;
    int32_t previous=-1,next=-1;
    bool direction=true,active=false,processed=false;
    double distance=0;
    int32_t feature=0;
    uintptr_t sourceAIdentity=0,sourceBIdentity=0;
};
struct ShakerCellNode {
    ShakerPlane plane;
    ShakerAnchor sourceA,sourceB;
    ShakerPoint vertex{};
    bool artificial=true;
};
using ShakerCell=std::vector<ShakerCellNode>;
ShakerCell shakerRectangleCell(const std::array<double,4> &box); // xmin,ymin,xmax,ymax, 72ed30
void shakerBoundaryDistance(ShakerBoundary &,ShakerPoint); // 72e3b0
struct ShakerSupport {size_t node=SIZE_MAX;double distance=0;bool vertex=false;};
ShakerSupport shakerClosestCell(const ShakerCell &,ShakerPoint); // 72f180
// The original 7273e0 inline clip retains a cell when all vertices are outside.
// Nodes are kept in original next-pointer order, with the new node as head.
bool shakerClipCell(ShakerCell &,ShakerCellNode);
bool shakerRotationRow(ShakerProblem &,LinearModel &,const ShakerCell &,
    const ShakerBoundary &,ShakerPart &,ShakerPart &,ShakerPoint); // 726900
struct ShakerNear {bool hit=false,start=false,end=false;};
ShakerNear shakerPointNear(ShakerPoint start,ShakerPoint end,ShakerPoint point,double tolerance); // 72e260
double shakerWinding(std::span<const ShakerBoundary>,ShakerPoint,double tolerance); // 72e680
bool shakerInside(std::span<const ShakerBoundary>,ShakerPoint,double tolerance,bool parityGate); // 72ea50
// 72f4e0 preserves the caller's `bothInside` byte on rejected segments and
// on the branch where both endpoints lie outside the window.
bool shakerSegmentWindow(ShakerPoint,ShakerPoint,const std::array<double,4> &,double tolerance,bool &bothInside);
double shakerSegmentDistanceSquared(ShakerPoint,ShakerPoint,ShakerPoint); // 520130
void shakerBoundaryOrder(std::span<const ShakerBoundary>,std::vector<int32_t> &,size_t depth=SIZE_MAX); // 72b090
}
