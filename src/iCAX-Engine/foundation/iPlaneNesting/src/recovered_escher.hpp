#pragma once
#include "recovered_factory.hpp"
namespace study::recovered {
struct EscherPatternInput {
    double sheetWidth=0,sheetHeight=0,left98=0,rightA0=0,bottomA8=0,topB0=0,leftTrim70=0;
    double gap=0,geometryArea=0;
    int64_t maxPlacedX=0;
    int32_t placedCount13d8=0,trial13bc=0;
    std::optional<int32_t> quantityCap;
};
struct EscherPatternProvider {double scale18=1;int64_t spacingE8=1;};
struct EscherPatternRequest {
    int32_t count=0,discard=0;
    double width=0,height=0;
    bool atLeftEdge=false;
};
class EscherPatternHost {
public:
    virtual ~EscherPatternHost()=default;
    virtual int32_t available()=0;
    virtual std::optional<EscherPatternProvider> provider()=0;
    // 493b00 + 4939a0 are an explicit geometric dependency until their
    // repeating-pattern solver and shape-to-placement conversion are connected.
    virtual std::vector<Placement> build(const EscherPatternRequest &)=0;
    virtual int32_t commit(std::span<const Placement>,int64_t offsetX)=0;
};
int32_t buildEscherPattern(const EscherPatternInput &,Random &,EscherPatternHost &); // 3f3cc0
struct EscherPatternRecord {
    uintptr_t geometry=0;
    uint32_t reflected=0;int32_t angle=0;
    std::array<uint64_t,4> intrinsic{};
    std::array<int64_t,4> bounds{};
    int64_t x=0,y=0;
    bool operator==(const EscherPatternRecord &)const=default;
};
static_assert(sizeof(EscherPatternRecord)==0x60);
struct EscherPatternLayout {
    std::vector<EscherPatternRecord> primary;
    std::array<int64_t,4> basis{};
    std::vector<EscherPatternRecord> secondary;
    std::array<int64_t,4> bounds{};
    bool operator==(const EscherPatternLayout &)const=default;
};
using EscherLatticeProvider=std::function<EscherPatternLayout(const std::array<int64_t,4> &,uint32_t)>;
// 787de0. The lattice geometry remains an explicit provider; length search,
// exact queried rectangles and strict replacement are recovered here.
void improveEscherPatternLength(int64_t &bestLength,std::array<int64_t,4> &rectangle,
    int32_t required,EscherPatternLayout &best,uint32_t limit,const EscherLatticeProvider &);
std::array<int64_t,4> escherLayoutBounds(std::span<const EscherPatternRecord>); // 7860c0
class EscherSolveHost {
public:
    virtual ~EscherSolveHost()=default;
    virtual size_t singleCount()const=0;
    virtual bool singleIsPrimitive(size_t)const=0;
    virtual EscherPatternLayout single(size_t,const std::array<int64_t,4> &,bool,uint64_t,std::array<int64_t,3> &)=0;
    virtual size_t pairCount()const=0;
    virtual void improvePair(size_t,int64_t &,std::array<int64_t,4> &,int32_t,EscherPatternLayout &)=0;
};
EscherPatternLayout solveEscherPatterns(EscherSolveHost &,std::array<int64_t,4> rectangle,
    bool singleMode,bool primitivesOnly,uint64_t required,int64_t &lengthOutput); // 7881a0
}
