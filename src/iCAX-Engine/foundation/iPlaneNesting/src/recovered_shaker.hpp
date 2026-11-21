#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <vector>

namespace study::recovered {
using ShakerPoint = std::array<double,2>;
struct ShakerPlane {
    double a=0,b=0,c=0,kappa=0;
    ShakerPoint reference{};
};
struct ShakerAnchor {ShakerPoint point{};bool valid=false;};
struct LinearColumn {double lower,upper,cost;};
struct LinearTerm {int32_t column;double coefficient;};
struct LinearRow {
    std::vector<LinearTerm> terms;
    double lower=-std::numeric_limits<double>::infinity();
    double upper=std::numeric_limits<double>::infinity();
};
struct LinearModel {
    std::vector<LinearColumn> columns;
    std::vector<LinearRow> rows;
    bool minimize=true;
    int32_t addColumn(double lower,double upper,double cost);
    int32_t addLowerRow(std::vector<LinearTerm> terms,double rhs);
    int32_t addRow(std::vector<LinearTerm> terms,double lower,double upper);
};
struct ShakerPart {
    ShakerPoint position{};
    ShakerPoint reference50{};
    double angle=0,margin80=0;
    std::array<double,4> bounds{}; // xmin,xmax,ymin,ymax, +110..+128
    std::array<double,2> angleBounds{};
    std::array<uint8_t,3> locked{};
    std::array<uint8_t,2> mirror{};
    std::array<double,3> objective{};
    std::array<int32_t,3> columns{-1,-1,-1};
    uint8_t category8=0,special88=0;
    std::vector<int32_t> rows;
    uint8_t geometryValid30=0,movable188=0;
    double dualMagnitude190=0;
};
struct DeferredShakerRow {
    std::array<LinearTerm,2> terms;
    double lower;
    ShakerPart *first,*second;
};
struct ShakerProblem {
    double window=0,angularWindow=0;
    bool rotation=false;
    uint8_t policy0=0,policy1=0,policy3=0,slack49=0;
    int32_t slackC8=-1,slackCC=-1;
    std::vector<DeferredShakerRow> deferred;
    // 72acd0 returns the deferred record at node+40; 726480 reads +8/+16:
    // these are coefficient bit patterns, not the source part identities.
    std::map<int32_t,std::array<uint64_t,2>> specialRows;
    std::array<uint64_t,2> activeSpecialRow{};
    bool hasActiveSpecialRow=false;
    uint8_t policy20=0,enabled58=0,enabled59=0,free5A=0;
    ShakerAnchor vector60{},vector78{};
    int32_t auxiliaryD0=-1,auxiliaryD8=-1;
    bool hasAuxiliaryD0=false;
    std::array<int32_t,4> auxiliaryDC{-1,-1,-1,-1};
    std::array<int32_t,4> auxiliaryEC{-1,-1,-1,-1};
    std::array<int32_t,4> auxiliary114{-1,-1,-1,-1};
};
// 72a150 and its 729210 tail call. An explicit provider can replace the latter
// for isolated differential tests; ordinary execution uses the recovered model.
double shakerVariables(ShakerProblem &,std::span<ShakerPart>,LinearModel &,
    const std::function<void(ShakerProblem &,LinearModel &)> &additional={});
// 729210 optional slack/deviation/directional model and 729110 bounds.
void shakerAdditionalVariables(ShakerProblem &,std::span<const ShakerPart>,LinearModel &);
std::array<double,4> shakerSplitVector(ShakerPoint);
// 726d40: exact sparse insertion order, fixed-axis substitution, optional
// rotation/slack, deferred special rows, and per-part row ownership.
bool shakerTranslationRow(ShakerProblem &,LinearModel &,const ShakerPlane &,
    const ShakerAnchor &,const ShakerAnchor &,ShakerPart &,ShakerPart &,ShakerPoint relative);
struct ShakerSolution {
    // Interface status consumed by 726480: 0=solution, 3=stop, other=failure.
    int32_t status=2;
    double objective=0;
    std::vector<double> primal,dual;
};
// 726480 candidate writeback. Geometry refresh is a provider; accepting a
// solved LP is not the later 71a8e0 nonlinear geometry validation.
int shakerApplySolution(ShakerProblem &,std::span<ShakerPart>,const ShakerSolution &,
    double &objective,const std::function<void(ShakerPart &)> &refresh);
// Real open-source HiGHS backend. Solver-version/trajectory equality with the
// binary is not asserted. This adapter is separate from recovered arithmetic.
ShakerSolution solveShakerHighs(const LinearModel &,double seconds=std::numeric_limits<double>::infinity(),double feasibilityTolerance=1e-7,int32_t iterationLimit=INT32_MAX);
ShakerPlane shakerLine(ShakerPoint normal,ShakerPoint point); // 532730
ShakerPoint shakerIntersection(const ShakerPlane &,ShakerPoint,ShakerPoint); // 72f3c0
}
