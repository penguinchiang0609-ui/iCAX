// Bulk cut publication and queued view reads share the native Scene channel.
// Give these operations enough time to finish before reconciling their receipt.
export const cutCreationTimeoutMs = 120000;
// Snapshot capture and result publication run on the Scene channel even though
// geometric calculation runs asynchronously. Large jobs must not time out at
// these boundaries; polling stays cancellable between responses.
export const machiningTaskTimeoutMs = 120000;
// A new resource closure requires writing the full project archive. Keep its
// acknowledgement alive beyond the ordinary 15-second UI request budget.
export const projectPersistenceTimeoutMs = 120000;
