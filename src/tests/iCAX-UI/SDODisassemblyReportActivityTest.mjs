// The production SDO client treats genuine reports as activity, not completion.
import assert from "node:assert/strict";
import { MockHostBridge } from "../../iCAX-UI/SDK/Bridge/mockHostBridge.mjs";
import { SDOClient, SDOError, SDOTimeoutError, SDOFrameKind, InvocationStatus } from "../../iCAX-UI/SDK/SDO/sdoClient.mjs";
import { serializeVariantText } from "../../iCAX-UI/SDK/SDO/variantSerializer.mjs";

const bridge = new MockHostBridge({ delayMs: 0 });
// Native owns the work; this transport delivers explicit report/response frames.
bridge.postSDOFrame = async frame => { bridge.postedFrames.push(frame); };
const channelId = await bridge.getApplicationChannelId();
const client = new SDOClient(bridge, { timeoutMs: 70 });
const method = "TubeDesigner.DisassembleSelected";
const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const response = (task, payload, status = InvocationStatus.Ok) => client.receive(channelId, {
  ...bridge.postedFrames.find(frame => frame.callId === task.callId),
  kind: SDOFrameKind.Response, status, payloadText: serializeVariantText(payload),
});
try {
  const reports = [];
  const task = client.invoke(channelId, method, { productEntityIds: ["window"] }, {
    onReport: report => reports.push(report.payload),
  });
  let completed = false;
  const result = task.then(value => { completed = true; return value; });
  for (let index = 1; index <= 6; index++) {
    await delay(30);
    const frame = bridge.emitReport(channelId, task.callId, method, {
      kind: "disassembly", phase: "geometry", completed: index, total: 6,
      message: "正在生成零件几何", elapsedMs: index * 30,
    }, { delayMs: 0 });
    await delay(2);
    assert.equal(completed, false, "a report must not finish the native request");
    assert.equal(client.pending.size, 1);
    assert.equal(reports.length, index);
    assert.equal(client.receive(channelId, { ...frame, callId: task.callId + 999 }), false);
  }
  assert.equal(response(task, { partCount: 6 }), true);
  assert.deepEqual(await result, { partCount: 6 });
  assert.equal(client.pending.size, 0);
  const lateFrame = bridge.emitReport(channelId, task.callId, method, {
    kind: "disassembly", phase: "completed", completed: 6, total: 6,
  }, { delayMs: 0 });
  assert.equal(client.receive(channelId, lateFrame), false);
  await delay(2);
  assert.equal(reports.length, 6, "finished calls must ignore late reports");

  const silent = client.invoke(channelId, method, {});
  await assert.rejects(silent, SDOTimeoutError);
  assert.equal(client.pending.size, 0, "inactivity timeout left a pending request");
  assert.equal(client.receive(channelId, {
    ...bridge.postedFrames.find(frame => frame.callId === silent.callId),
    kind: SDOFrameKind.Report, payloadText: serializeVariantText({ kind: "disassembly", phase: "geometry", completed: 1, total: 6 }),
  }), false, "a late report must not revive a timed-out request");
  assert.equal(response(silent, { partCount: 99 }), false);
  assert.equal(client.pending.size, 0);

  const failed = client.invoke(channelId, method, {});
  const rejection = assert.rejects(failed, error => error instanceof SDOError && /native failure/.test(error.message));
  assert.equal(response(failed, "native failure", InvocationStatus.InvalidInvocation), true);
  await rejection;
  assert.equal(client.pending.size, 0, "native failure left a pending request");

  const brokenBridge = new MockHostBridge({ delayMs: 0 });
  brokenBridge.postSDOFrame = async () => { throw new Error("bridge failure"); };
  const brokenClient = new SDOClient(brokenBridge, { timeoutMs: 70 });
  await assert.rejects(brokenClient.invoke(channelId, method, {}), /bridge failure/);
  assert.equal(brokenClient.pending.size, 0, "transport failure left a pending request");
  brokenClient.dispose();
  console.log("Production SDO disassembly: six reports keep a 70 ms inactivity timeout alive for over 180 ms; reports never resolve requests; final response, inactivity timeout, native and transport errors clear pending calls; late reports ignored.");
} finally {
  client.dispose();
}
