#pragma once
#include "recovered_escher_provider.hpp"
#include "recovered_escher_producer.hpp"
#include "recovered_escher_equal_maximum_phase.hpp"
#include "recovered_escher_partial.hpp"
#include "recovered_escher_owner.hpp"
namespace study::recovered {
struct EscherPreparedAreaMetadata {
    // 825040 sums the normal hierarchy's outer rings, as returned by 824f40.
    // It does not subtract nested holes.
    int64_t outerArea=0,materialArea=0;
};
int64_t escherContourArea(const ConvolutionIntegerPolygon &); //347910
int64_t escherHierarchyArea(const ConvolutionHierarchy &); //825040
EscherPreparedAreaMetadata escherOwnerAreaMetadata(const EscherIntegerShapeRecord &);
struct EscherPreparedSinglePattern;
EscherPreparedSinglePattern escherBindPreparedPattern(EscherEnumerationDescriptor);
struct EscherPreparedSinglePattern {
    EscherEnumerationDescriptor descriptor;
    std::vector<EscherRecordGeometry> geometries;
    // Areas come from the actual selected outer contour and material hierarchy.
    // Bounding-box area is not a substitute for either value.
    std::optional<EscherPreparedAreaMetadata> areas;
};
struct EscherPreparedLatticePattern {
    std::vector<EscherPatternRecord> records;
    std::vector<EscherLatticeBasis> candidates;
};
// Descriptor records, orientation choices, restriction and same-width metadata
// are supplied by their owner. The NFP must be the prepared member self-NFP.
EscherPreparedSinglePattern escherPreparePeriodicPattern(EscherEnumerationDescriptor,
    std::vector<EscherRecordGeometry>,std::span<const EscherSupportTable> memberSupports,
    const ConvolutionIntegerPolygon &selfNfp,const EscherCandidateBuildOptions &,int64_t &minimumCellArea);
EscherPatternLayout escherPreparedSingleMaximum(const EscherPreparedSinglePattern &,
    EscherIntegerBox,bool specialCatalog,uint64_t previousBest,uint64_t limit);
class EscherPreparedMaximumHost final:public EscherMaximumHost {
    std::span<const EscherPreparedSinglePattern> singles_;
    std::span<const EscherPreparedLatticePattern> pairs_;
public:
    EscherPreparedMaximumHost(std::span<const EscherPreparedSinglePattern> singles,
        std::span<const EscherPreparedLatticePattern> pairs={}):singles_(singles),pairs_(pairs){}
    size_t singleCount()const override{return singles_.size();}
    bool singleIsPrimitive(size_t index)const override{return singles_[index].descriptor.records.size()==1;}
    EscherPatternLayout singleMaximum(size_t,EscherIntegerBox,bool mode,uint64_t previousBest,uint64_t limit)override;
    size_t pairCount()const override{return pairs_.size();}
    bool pairIsPrimitive(size_t index)const override{return pairs_[index].records.size()==1;}
    EscherPatternLayout pairLayout(size_t,EscherIntegerBox)override;
};
EscherPatternLayout escherPreparedMaximum(std::span<const EscherPreparedSinglePattern>,
    std::span<const EscherPreparedLatticePattern>,EscherIntegerBox,bool singleMode,
    bool primitivesOnly,uint64_t limit);
EscherPatternLayout escherPreparedSinglePartial(const EscherPreparedSinglePattern &,EscherIntegerBox);
EscherPatternLayout escherPreparedPartial(std::span<const EscherPreparedSinglePattern>,
    EscherIntegerBox,bool primitivesOnly,uint64_t limit);
class EscherPreparedSolveHost final:public EscherSolveHost {
    std::span<const EscherPreparedSinglePattern> singles_;
    std::span<const EscherPreparedLatticePattern> pairs_;
    uint32_t pairLimit_;
public:
    EscherPreparedSolveHost(std::span<const EscherPreparedSinglePattern> singles,
        std::span<const EscherPreparedLatticePattern> pairs,uint32_t pairLimit)
        :singles_(singles),pairs_(pairs),pairLimit_(pairLimit){}
    size_t singleCount()const override{return singles_.size();}
    bool singleIsPrimitive(size_t index)const override{return singles_[index].descriptor.records.size()==1;}
    EscherPatternLayout single(size_t,EscherIntegerBox const &,bool mode,uint64_t required,
        std::array<int64_t,3> &key)override;
    size_t pairCount()const override{return pairs_.size();}
    void improvePair(size_t,int64_t &,EscherIntegerBox &,int32_t,EscherPatternLayout &)override;
};
EscherPatternLayout escherPreparedSolve(std::span<const EscherPreparedSinglePattern>,
    std::span<const EscherPreparedLatticePattern>,uint32_t pairLimit,EscherIntegerBox,
    bool singleMode,bool primitivesOnly,uint64_t required,int64_t &lengthOutput);
}
