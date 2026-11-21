export async function awaitOperationTask({ read, isCurrent = () => true, onState = () => {}, onReadError, intervalMs = 120, retryMs = 1200 }) {
  for (;;) {
    if (!isCurrent()) return null;
    let state;
    try { state = await read(); }
    catch (error) {
      if (!isCurrent()) return null;
      if (!onReadError) throw error;
      onReadError(error);
      await new Promise(resolve => setTimeout(resolve, retryMs));
      continue;
    }
    if (!isCurrent()) return null;
    onState(state);
    if (["Completed", "Failed", "Cancelled", "Stale", "Interrupted", "Error"].includes(String(state.status))) return state;
    await new Promise(resolve => setTimeout(resolve, intervalMs));
  }
}
