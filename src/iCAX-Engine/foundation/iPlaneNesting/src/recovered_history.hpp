#pragma once
#include "recovered_construction.hpp"
namespace study::recovered {
struct HistoryStatistics {
    std::vector<std::vector<double>> samples;
    std::vector<double> estimates,weights;
    std::vector<int32_t> attempts;
    double best=0;
    int32_t updates=0,elite=0;
};
// 3a0f60: descending insertion/32-item merge runs, then reversal. Equal finite
// estimates end up in reverse insertion order; unordered scores follow the
// original comparisons and merge sentinel moves. Allocation failure propagates.
std::vector<int32_t> historyOrder(std::span<const double>);
void refreshHistoryWeights(HistoryStatistics &,bool exploitOnly); // 3a05a0
void updateHistory(HistoryStatistics &,size_t index,double length); // 3a0a50
struct HistoryDistribution {std::vector<double> normalized,cumulative;};
HistoryDistribution historyDistribution(std::span<const double>); // 3a2040
struct HistoryChoice {int32_t index=0,previousAttempts=0;};
HistoryChoice chooseHistory(HistoryStatistics &,Random &,bool exploitOnly); // 3a08c0

// The history controller owns the structuring element snapshots. Concrete
// geometry carried by derived elements is consumed by the geometry provider.
struct StructuringElement {
    virtual ~StructuringElement()=default;
    double value10=0;
};
struct HistoryControl {
    HistoryStatistics statistics;
    Random random{0};
    std::vector<std::shared_ptr<const StructuringElement>> elements;
    std::shared_ptr<const StructuringElement> fixedElement;
    bool dynamic29dc=false;
    // Offsets use the supervisor base (wrapper+8). Its +1c8 is a phase flag;
    // the assigned-trial counter is base+1c0 (wrapper+1c8).
    int32_t phase1c8=0,targetManager29d8=0;
    double budget1b8=0;
};
struct PreparedHistory {
    bool enabledDynamic=false;
    std::optional<HistoryChoice> choice; // static path leaves caller's index alone
};
// 454510. Callers synchronize shared state and supply the supervisor clock and
// target manager's locked count. The 5*N threshold uses the history element
// count at supervisor+15f4 (history+84), not the worker count.
PreparedHistory prepareHistory(HistoryControl &,TrialNesting &,double elapsed,
    std::optional<int32_t> targetManagerAttempts);
struct HistoryCompletionInput {
    double target14b8=0,queue20=0,placedArea=0;
    int64_t maximumRight=0;
    double trim70=0,config758=0,elapsed=0;
    int32_t managerId=0,index=0;
};
struct HistoryLogEntry {double length=0,elementValue=0;int32_t managerId=0;double elapsed=0;};
struct CompletedHistory {bool updated=false;std::optional<HistoryLogEntry> log;};
// 454770. HistoryAdd(3a0c20) is diagnostic output only: returning a log entry
// must never append another element or change the selection distribution.
CompletedHistory completeHistory(HistoryControl &,const TrialNesting &,const HistoryCompletionInput &);
}
