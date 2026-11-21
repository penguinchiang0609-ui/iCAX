#pragma once
#include "SheetNesting.h"

namespace iCAX::SheetNesting
{
    // Independent construction from the original input, with no incumbent.
    // Each bounded legal-angle plan branches on convex SAT separation axes
    // and solves all translations together using the shared feasibility kernel.
    // Scope is one stock instance, zero gap and at most 12 demanded instances
    // of simple convex polygons with fixed angle domains and at most 64 vertices.
    // Up to 256 angle plans, 4096 nodes per plan and 4096 open nodes are searched.
    // Work counts search nodes and simplex pivots under the caller's attempt cap.
    // Actual-world containment, exact material separation and original Validate
    // guard every retained result. Unsupported scope retains all original demand.
    // Registration is opt-in. All mutable construction state belongs to its job.
    StrategyFactory MakeConvexConstructionFactory();
}
