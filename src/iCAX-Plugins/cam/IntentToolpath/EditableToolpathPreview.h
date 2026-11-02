#pragma once
#include "EditableToolpath.h"

namespace iCAX::CAM::Intent
{
// Pure preview over an explicit immutable input snapshot. Context acquisition
// stays on the owner thread; this function never invokes FreezeContext or reads
// a repository. It applies the same schema, channel and exact coverage rules as
// document preview/generation. No identities, receipts or caches are published.
_INTENT_TOOLPATH_EXP SOperationCandidate EvaluateFrozenToolpathOperation(
    const CToolpathOperationRegistry&,const IToolpathGeometryServices&,const std::string& typeId,SOperationEvaluation);
struct SFrozenToolpathPreviewNode final
{
    std::string TypeId;
    // Input bindings are authoritative. Values and fingerprints are filled
    // from the frozen source/prior-node table during this pure evaluation.
    SOperationEvaluation Evaluation;
};
_INTENT_TOOLPATH_EXP std::map<SPortRef,SFrozenPathInput> EvaluateFrozenToolpathGraph(
    const CToolpathOperationRegistry&,const IToolpathGeometryServices&,
    std::map<SPortRef,SFrozenPathInput> frozenSources,
    const std::vector<SFrozenToolpathPreviewNode>& topologicalNodes,
    const std::shared_ptr<std::atomic_bool>& cancelled = {});
}
