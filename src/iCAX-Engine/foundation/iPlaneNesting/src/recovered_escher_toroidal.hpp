#pragma once
#include "recovered_escher_geometry_producer.hpp"
#include "recovered_shaker_input.hpp"
namespace study::recovered {
using ToroidalBasis=std::array<ShakerPoint,2>;
EscherLatticeBasis toroidalReduceIntegerBasis(EscherLatticeBasis); //81ac50
ToroidalBasis toroidalReduceBasis(ToroidalBasis); //717b90
ToroidalBasis toroidalCanonicalDirections(ToroidalBasis); //718200
double toroidalAreaLineSearch(ToroidalBasis previous,ToroidalBasis proposed); //717940
ShakerGeometryBox toroidalSceneProjection(const ShakerInputScene &,std::span<const size_t>,ToroidalBasis); //7168d0
std::array<int32_t,2> toroidalLayerCounts(const ShakerInputScene &,ToroidalBasis); //71dab0
using ToroidalBoxes=std::map<int32_t,ShakerGeometryBox>;
ToroidalBoxes toroidalMergeBoxes(ToroidalBoxes,const ToroidalBoxes &); //71bfe0
ToroidalBoxes toroidalShiftBoxes(ToroidalBoxes,std::array<int32_t,2>,ToroidalBasis); //71c2c0
std::array<int32_t,2> toroidalWrapShift(const ToroidalBoxes &,ToroidalBoxes,ToroidalBasis,
    const std::function<void()> & = {}); //71c4f0
void toroidalWrapParts(ShakerInputScene &,ToroidalBasis,const std::function<void()> & = {}); //71cb30
struct ToroidalDuplicate {
    size_t original=0,image=0;
    int32_t first=0,second=0;
};
std::vector<ToroidalDuplicate> toroidalDuplicateParts(ShakerInputScene &,size_t originalCount,
    ToroidalBasis,std::array<int32_t,2>,std::span<const size_t> imageShapes,
    const std::function<void()> & = {}); //71d650
void toroidalDuplicateConstraint(const ToroidalDuplicate &,bool rotation,const ShakerProblem &,
    std::span<const ShakerPart>,LinearModel &); //723ec0
EscherEnumerationDescriptor escherToroidalDescriptor(std::span<const EscherPatternRecord>,
    EscherLatticeBasis,int32_t restriction,const std::vector<bool> &orientationChoices,
    int64_t &minimumCellArea); //77e950
struct EscherToroidalTrace {
    size_t descriptors=0,candidates=0,solves=0,improved=0,acceptedSteps=0,failedSolves=0;
};
std::optional<EscherEnumerationDescriptor> escherToroidalShakeModel(
    const EscherEnumerationDescriptor &,const EscherPeriodCandidate &,int64_t &minimumCellArea,
    Budget * = nullptr,EscherToroidalTrace * = nullptr); //7998b0
using EscherToroidalShakeProvider=std::function<std::optional<EscherEnumerationDescriptor>(
    const EscherEnumerationDescriptor &,const EscherPeriodCandidate &,int64_t &)>;
// Actual786a90/79f4a0 entry. Each source descriptor's original candidate
// prefix is fixed before append; subsequent source indexes remain stable.
void escherImproveToroidalCatalog(EscherGeometryCatalog &,int32_t level,
    Budget * = nullptr,EscherToroidalTrace * = nullptr,const EscherToroidalShakeProvider & = {});
}
