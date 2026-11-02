#pragma once
#include "EditableToolpath.h"

namespace iCAX::CAM::Intent::Editable
{
// Validate typed source-identity declarations before preparing any edits.
// This does not claim existing entities: the document's overlay verifies
// ownership, collisions, and the source-to-path relationship.
void ValidateHostSourceIdentities(const std::vector<IntentMap>& commands,
    const SEditableToolpathHostEdit& host);

// Receipts/retainedReceipts contain new commands only. StageDomain must queue
// the complete preflighted field write set and durable command receipts into
// this transaction, without committing or opening another repository scope.
void CommitToolpathHostEdit(iCAX::Database::IRepository& repository, IntentId document,
    const SEditableToolpathHostEdit& host, const std::vector<IntentMap>& receipts,
    const std::vector<IntentId>& retainedReceipts,
    const std::function<void(iCAX::Database::ITransaction&)>& stageDomain);
}
