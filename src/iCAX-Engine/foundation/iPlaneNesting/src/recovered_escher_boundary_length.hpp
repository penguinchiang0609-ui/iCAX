#pragma once
#include "recovered_escher_boundary.hpp"
#include "recovered_escher_periodic_map.hpp"
#include "recovered_escher_boundary_select.hpp"
namespace study::recovered {
double escherBoundaryPlacementCost(const ConvolutionIntegerPolygon &part,const ConvolutionIntegerPolygon &sheet); //850010
struct EscherBoundarySelectionPlan {double fuzzyCost=0,sampleCost=0,attempts=0;bool fuzzy=false;};
EscherBoundarySelectionPlan escherBoundarySelectionPlan(int32_t interiorCount,int32_t partialCount,
    int32_t vertices,int32_t sequence,const EscherProviderLengthLimits &); //7ac840 cost/dispatch arithmetic
struct EscherBoundaryLengthServices {
    // Return the actual 861f80 polygon for the posed member, before 3dda40 reverses its rings.
    // cacheOnly must not compute a missing entry. The record's source translation is applied by the phase.
    std::function<std::optional<ConvolutionIntegerPolygon>(size_t member,const EscherPatternRecord &posed,
        const ConvolutionIntegerPolygon &sheet,bool cacheOnly)> placementMap;
    std::function<double(size_t member,const ConvolutionIntegerPolygon &sheet)> placementCost;
    EscherFuzzySelectionProvider fuzzy;
    EscherBoundaryOffsetProvider offset;
    ConvolutionHierarchyDiagnostic diagnostic;
    uint32_t selectionSequence=0;
};
EscherBoundaryPhaseResult escherBoundaryLengthPhase(const EscherPreparedSinglePattern &,
    const ConvolutionIntegerPolygon &,bool specialCatalog,uint32_t previousBest,double rotationDegrees,
    EscherProviderLengthLimits &,int32_t candidateIndex,EscherBoundaryLengthServices &); //7ac840
EscherBoundaryPhaseProvider escherBoundaryLengthProvider(EscherBoundaryLengthServices &);
}
