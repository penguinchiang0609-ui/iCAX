#pragma once
#include "SheetNesting.h"

namespace iCAX::SheetNesting::Detail
{
    struct SheetTemplateReuseReport
    {
        std::size_t Copies=0,Parts=0,Attempts=0;
        bool Cancelled=false,Truncated=false;
        std::string Diagnostic;
    };

    // The prototype is an already committed board of this immutable problem.
    // nextStockInstance and used describe the contiguous identities committed
    // in output, including that first board. Only additional physical boards
    // are emitted. A task-level Partial prototype is expected for large jobs.
    // The remaining Options budget includes preparation, one source geometry
    // validation and atomic copy expansion; each attempted member costs one
    // attempt. A stopped draft never consumes inventory or emits half a board.
    // Remaining/State are reconciled after the committed prefix is verified.
    SheetTemplateReuseReport AppendSheetTemplateCopies(const Problem& problem,
        const Layout& prototype,const Stock& stock,std::size_t& nextStockInstance,
        std::vector<std::size_t>& used,std::size_t& nextGroup,Result& output,
        const Options& remainingBudget,double minimumUtilization=0);
}
