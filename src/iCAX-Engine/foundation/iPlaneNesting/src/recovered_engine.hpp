#pragma once
#include "recovered_strategy.hpp"
#include "recovered_shaker_iteration.hpp"

namespace study::recovered {
struct SingleEngineBudgetInput {
    double remaining = 0;
    double usePostop9b0 = 1, strategyFraction9c0 = .85;
    double disableConstruction548 = 0;
    bool rectangleModeC8 = false, restrictedC9 = false;
    bool specialTechnology3c = false, auxiliary240 = false;
};
struct SingleEngineBudgets {
    double selectionA0 = 0, refinementA8 = 0, continuationB0 = 0;
    double postB8 = 0, totalC0 = 0;
};
// 5a4e30: budget calculation through 5a50c2. The fractions are .25/.25/.5,
// or .5/0/.5 for rectangle mode, applied after the .98/.85 strategy fraction.
SingleEngineBudgets singleEngineBudgets(const SingleEngineBudgetInput &);
struct PitchSheet {
    double width = 0, left = 0, right = 0;
};
// 55b090: explicit pitch bypasses fallback and precision calculations.
int64_t contextPitch(std::span<const PitchSheet>, double explicitPitch,
                     double maximumPitch, double stripsPerPart9d8, bool specialSheet);
struct SupervisorShakerInput {
    double width = 0, height = 0, left = 0, right = 0, bottom = 0, top = 0;
    double placedArea = 0;
    int32_t placedCount = 0;
    bool specialTechnology3c = false, restrictedC9 = false;
};
bool smallNestingShakeGate(const SupervisorShakerInput &); // 3cd9e0
int32_t supervisorShakerOperation(const SupervisorShakerInput &); // 59f870, type != direction
ShakerIterationOptions supervisorShakerOneOptions(double seconds, double pitch,
    double contextHeight220, double sceneScale, bool rotate); // 650150 -> 70f480
struct MultiPolishBudget {double perLayout=0,reservedLast=0;};
MultiPolishBudget multiPolishBudget(double remaining,uint64_t sheets,bool compactLast); // 4cba60
std::optional<double> multiLastPolishSeconds(double remaining,int32_t placedCount,uint64_t vertices); // 4cb660
struct MultiPolishOrder {
    std::map<int32_t,double> typeQuantities;
    int64_t sheetUserId=0;
    double quantity18=0,width=0,height=0;
    std::vector<int64_t> sortedPartUserIds;
};
bool multiPolishTypeLess(const std::map<int32_t,double> &,const std::map<int32_t,double> &); // 3d3320
bool multiPolishLess(const MultiPolishOrder &,const MultiPolishOrder &); // 4ca670
struct MultiPostBudget {double optimization=0,polishReserve=0;};
MultiPostBudget multiPostBudget(double remaining,double polishGlue930,bool predefinedSheets); // 5bc79f..5bc807
struct MultiPolishStock {
    uintptr_t identity=0;
    double width=0,height=0,margin70=0;
    uint64_t attributesD0=0;
    uint32_t attributesD8=0;
    bool hasContours=false;
};
struct MultiPolishStockQuery {
    double width=0,height=0;
    uint64_t attributesD0=0;
    uint32_t attributesD8=0;
};
// 4cb340: caller-supplied occupied extent is the original informer's fixed X.
std::optional<MultiPolishStockQuery> multiPolishStockQuery(const MultiPolishStock &,
    std::optional<int64_t> occupiedLength,bool predefinedSheets);
// 39f1b0: input order is the pool tree's sheet-user-ID/address order. Only the
// low 32 bits at D0 participate; D4/D8 and shape contours are not lookup gates.
std::optional<size_t> multiPolishLookupStock(std::span<const MultiPolishStock>,
    const MultiPolishStockQuery &);
bool multiPolishAcceptStock(const MultiPolishStock &current,const MultiPolishStock &candidate); // 4cb340
}
