#pragma once
#include "EditableToolpath.h"

namespace iCAX::CAM::Intent
{
// An empty policy disables automatic initial labels. Otherwise numberScope is
// an arbitrary caller-owned namespace and firstNumber is an exact positive
// integer. The optional name is literal; empty/omitted uses "刀路 <number>".
_INTENT_TOOLPATH_EXP void ValidateToolpathOutputLabelPolicy(const IntentMap& policy);
_INTENT_TOOLPATH_EXP void ValidateToolpathLabelScope(const std::string& scope);

// Owner-thread, read-only publication preflight. Returns complete pathIdentity
// Data only for identities absent from the repository; inherited and dormant
// identities retain their original facts. Stable PathId order determines the
// first assignment, independently of display/candidate ordering. No counter or
// entity is written, so a rejected/rolled-back publication cannot consume numbers.
_INTENT_TOOLPATH_EXP std::map<IntentId,IntentMap> AllocateInitialToolpathLabels(
    const iCAX::Database::IRepository& repository,IntentId documentId,IntentId originOperationId,
    const IntentMap& policy,const std::vector<IntentId>& outputPathIds);
}
