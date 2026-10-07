// Test-only independent stdio bridges; scene operations remain exclusive to worker 0.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

export const rootWorkerIndex = (key, count) => {
  let hash = 2166136261;
  for (const character of key) hash = Math.imul(hash ^ character.charCodeAt(0), 16777619) >>> 0;
  return hash % count;
};

function bridge({ bridgePath, runtimeRoot, binaryRoot, index, onEvent, timeoutMs }) {
  const child = spawn(bridgePath, [], { cwd: runtimeRoot, windowsHide: true,
    env: { ...process.env, PATH: `${binaryRoot};${process.env.PATH}` }, stdio: ['pipe', 'pipe', 'pipe'] });
  const pending = new Map();
  let nextId = 0, stderr = '';
  const fail = error => {
    for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); }
    pending.clear();
  };
  child.stderr.on('data', value => { stderr += value; });
  child.on('error', fail);
  child.on('exit', code => { if (pending.size) fail(new Error(`Worker ${index} exited ${code}: ${stderr}`)); });
  createInterface({ input: child.stdout }).on('line', line => {
    let response;
    try { response = JSON.parse(line); } catch { fail(new Error(line)); return; }
    const request = pending.get(response.id);
    if (!request) return;
    pending.delete(response.id); clearTimeout(request.timer);
    onEvent?.({ workerIndex: index, nativeStage: request.method, phase: 'finished',
      seconds: (Date.now() - request.startedAt) / 1000, ok: response.ok,
      ...request.workload, inspectionCache: response.result?.inspectionCache });
    response.ok ? request.resolve(response.result) : request.reject(new Error(`${request.method}: ${response.error}`));
  });
  return {
    pid: child.pid,
    invoke(method, payload) {
      return new Promise((resolveRequest, reject) => {
        const id = ++nextId;
        const workload = method === 'InspectNeutralModel' ? { requestedRoots: payload.geometryKeys.length,
          uniqueKeys: [...new Set(payload.geometryKeys)], points: payload.points?.length || 0,
          manufacturingCoordinates: payload.manufacturingCoordinates === true } : {};
        const timer = setTimeout(() => { pending.delete(id);
          reject(new Error(`Timeout ${method} worker ${index}: ${JSON.stringify(workload)} ${stderr}`)); }, timeoutMs);
        pending.set(id, { resolve: resolveRequest, reject, timer, method, workload, startedAt: Date.now() });
        onEvent?.({ workerIndex: index, nativeStage: method, phase: 'started',
          at: new Date().toISOString(), ...workload });
        child.stdin.write(JSON.stringify({ id, method, payload }) + '\n');
      });
    },
    close() {
      return new Promise(resolveClosed => {
        if (child.exitCode !== null || child.signalCode !== null || !child.pid) return resolveClosed();
        child.once('exit', resolveClosed); child.stdin.end(); child.kill();
      });
    },
  };
}

export function createCachedNeutralInspectionPool({ bridgePath, runtimeRoot, binaryRoot = runtimeRoot,
  workerCount = 1, onEvent, timeoutMs = 600000 }) {
  assert.ok(Number.isInteger(workerCount) && workerCount >= 1 && workerCount <= 4);
  const binarySha256 = createHash('sha256').update(readFileSync(bridgePath)).digest('hex');
  const routerPath = fileURLToPath(import.meta.url);
  const routerSha256 = createHash('sha256').update(readFileSync(routerPath)).digest('hex');
  const workers = Array.from({ length: workerCount }, (_, index) =>
    bridge({ bridgePath, runtimeRoot, binaryRoot, index, onEvent, timeoutMs }));
  const evidence = [];
  let routeSequence = 0;
  return {
    router: { path: routerPath, sha256: routerSha256 },
    workerMetadata: workers.map((worker, workerIndex) => ({ workerIndex, pid: worker.pid,
      bridgePath, binarySha256, sceneOperationsAllowed: workerIndex === 0 })),
    routingEvidence: evidence,
    verifySourceUnchanged() {
      assert.equal(createHash('sha256').update(readFileSync(routerPath)).digest('hex'), routerSha256);
      assert.equal(createHash('sha256').update(readFileSync(bridgePath)).digest('hex'), binarySha256);
      return true;
    },
    async invoke(method, payload = {}) {
      if (method !== 'InspectNeutralModel') return workers[0].invoke(method, payload);
      const requested = payload.geometryKeys;
      assert.ok(Array.isArray(requested) && requested.length >= 1 && requested.length <= 100);
      const groups = Array.from({ length: workerCount }, () => []);
      for (const key of requested) groups[rootWorkerIndex(key, workerCount)].push(key);
      const results = await Promise.all(groups.map(async (keys, workerIndex) => {
        const replies = [];
        // Five roots cap only controls per-API work; every root and point is preserved.
        const chunkSize = workerCount === 1 ? 100 : 5;
        for (let first = 0; first < keys.length; first += chunkSize) {
          const geometryKeys = keys.slice(first, first + chunkSize), included = new Set(geometryKeys);
          const request = { ...payload, geometryKeys };
          if (payload.points) request.points = payload.points.filter(point => included.has(point.geometryKey));
          const reply = await workers[workerIndex].invoke(method, request);
          assert.deepEqual(reply.geometryChecks.map(check => check.geometryKey), geometryKeys);
          for (const key of geometryKeys) assert.equal(rootWorkerIndex(key, workerCount), workerIndex);
          replies.push(reply);
        }
        return { workerIndex, keys, replies };
      }));
      const byKey = new Map();
      for (const result of results) for (const reply of result.replies) for (const check of reply.geometryChecks) {
        const queue = byKey.get(check.geometryKey) || []; queue.push(check); byKey.set(check.geometryKey, queue);
      }
      const geometryChecks = requested.map(key => {
        const queue = byKey.get(key); assert.ok(queue?.length, `Missing routed root ${key}`); return queue.shift();
      });
      assert.ok([...byKey.values()].every(queue => queue.length === 0), 'No extra routed roots');
      assert.deepEqual(geometryChecks.map(check => check.geometryKey), requested);
      for (const check of geometryChecks) if (payload.points)
        assert.equal(check.pointChecks.length, payload.points.filter(point => point.geometryKey === check.geometryKey).length);
      const replies = results.flatMap(result => result.replies), cacheRows = replies.map(reply => reply.inspectionCache);
      assert.ok(cacheRows.length > 0 && cacheRows.every(row => row?.keyKind === 'complete-standard-json-document-and-root'
        && row.unnormalizedBRepCached && row.privateShapeCopyBeforeInspection));
      const latestRows = results.filter(result => result.replies.length).map(result => result.replies.at(-1).inspectionCache);
      const roundTripRows = replies.flatMap(reply => reply.roundTripChecks);
      const roundTripChecks = requested.flatMap(key => roundTripRows.filter(row => row.geometryKey === key));
      const routing = { sequence: ++routeSequence, requestedKeys: [...requested], returnedKeys: geometryChecks.map(row => row.geometryKey),
        uniqueKeys: [...new Set(requested)], everyRootReturnedOncePerRequest: true,
        points: payload.points?.length || 0, checkedPoints: geometryChecks.reduce((sum, row) => sum + row.pointChecks.length, 0),
        manufacturingCoordinates: payload.manufacturingCoordinates === true,
        workers: results.filter(result => result.keys.length).map(result => ({ workerIndex: result.workerIndex,
          keys: [...result.keys], calls: result.replies.length,
          hits: result.replies.reduce((sum, reply) => sum + reply.inspectionCache.hits, 0),
          misses: result.replies.reduce((sum, reply) => sum + reply.inspectionCache.misses, 0) })) };
      evidence.push(routing);
      return { geometryChecks, roundTripChecks, routing, inspectionCache: {
        keyKind: 'complete-standard-json-document-and-root',
        hits: cacheRows.reduce((sum, row) => sum + row.hits, 0), misses: cacheRows.reduce((sum, row) => sum + row.misses, 0),
        models: latestRows.reduce((sum, row) => sum + row.models, 0), roots: latestRows.reduce((sum, row) => sum + row.roots, 0),
        unnormalizedBRepCached: true, privateShapeCopyBeforeInspection: true } };
    },
    async close() { await Promise.all(workers.map(worker => worker.close())); },
  };
}
