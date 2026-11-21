#pragma once
#include "ordinary_nomap.hpp"
namespace study {
//Existing host PartInput distinguishes holes semantically. Transform all rings
//with the same pivot/pose and give inner rings their original negative winding
//before the recovered clean Part preparation. No protection geometry is made.
std::vector<Polygon> ordinaryPreparedContours(const PartInput &,Pose);
//Finite fixed poses, zero spacing/protection, clean prepared outer+holes, and
//the existing1e6-X/1000-Y exact host grid. Continuous/off-grid poses stay false.
bool ordinaryPreparedDomain(const Input &);
struct OrdinaryPairGeometryTrace {size_t considered=0,convolved=0,holePairsSkipped=0,smallHolesSkipped=0;};
//348170 over actual signed outer/hole paths. The second source is reflected
//through the origin by the ordinary operand producer; caller supplies true
//for that ordinary forbidden-translation domain, false for direct comparison.
std::vector<Polygon> ordinaryPairContours(std::span<const Polygon> first,
    std::span<const Polygon> second,bool oppositeSecond,Budget &,
    OrdinaryPairGeometryTrace * =nullptr);
//3a7d40→3a7e00 mode2: aligned floating raster then closed intersections of
//neighboring physical strips. Row0 remains empty; offsets are not rebased.
Raster ordinaryPairRaster(std::span<const Polygon>,int64_t pitch,bool duplicatePitch=false);
struct OrdinaryPairMapTrace {size_t builds=0,hits=0;OrdinaryPairGeometryTrace geometry;};
//Single ordinary layer. Prepared sourceA uses module slot3(0), sourceB slot2(0).
//Both normal and whole-pitch-duplicate rasters are built together, as34aae0.
class OrdinaryPairMapProvider {
public:
    OrdinaryPairMapProvider(std::span<const OrdinaryNoMapModule>,int64_t pitch,Budget &);
    ~OrdinaryPairMapProvider();
    const Raster &pair(size_t occupied,size_t candidate,bool duplicatePitch=false);
    const OrdinaryPairMapTrace &trace()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
struct OrdinarySelectedModules {
    std::vector<uint32_t> words;
    uint64_t bitCount=0;
    bool all=false;
};
//398d70 consumes actual54f270 selected module IDs;550110 is the complete
//module catalog size. Geometry/holes are not eligibility substitutes here.
OrdinarySelectedModules ordinarySelectedModules(std::span<const int64_t>,uint64_t bitCount,int32_t catalogSize);
}
