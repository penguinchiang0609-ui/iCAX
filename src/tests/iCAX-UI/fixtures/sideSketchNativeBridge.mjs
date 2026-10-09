// Test-only transport for production SDOs, BRep models, GPU resources and .ictd files.
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { copyFileSync, mkdirSync, readFileSync, readdirSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const repositoryRoot = fileURLToPath(new URL('../../../../', import.meta.url));

export async function startSideSketchNativeBridge(artifacts) {
  const dllDirectory = process.env.ICAX_SIDE_SKETCH_NATIVE_DLL_DIR || resolve(repositoryRoot, 'src/x64/Debug');
  const bridgePath = process.env.ICAX_SIDE_SKETCH_NATIVE_BRIDGE
    || resolve(repositoryRoot, 'output/tests/side-sketch-native-browser/native/SideSketchAcceptanceBridge.exe');
  const runtime = resolve(artifacts, 'runtime');
  mkdirSync(runtime, { recursive: true });
  for (const name of readdirSync(dllDirectory).filter(name => /\.dll$/i.test(name)))
    copyFileSync(resolve(dllDirectory, name), resolve(runtime, name));
  copyFileSync(bridgePath, resolve(runtime, 'SideSketchAcceptanceBridge.exe'));
  const dllHash = createHash('sha256').update(readFileSync(resolve(runtime, 'TubeDesigner.dll'))).digest('hex');
  const importerHash = createHash('sha256').update(readFileSync(resolve(runtime, 'OpenCascadeResourceImport.dll'))).digest('hex');
  const processHandle = spawn(resolve(runtime, 'SideSketchAcceptanceBridge.exe'), [], {
    cwd: repositoryRoot, windowsHide: true,
    env: { ...process.env, PATH: runtime + ';' + dllDirectory + ';' + process.env.PATH }, stdio: ['pipe', 'pipe', 'pipe'],
  });
  const requests = [], waiting = new Map(); let sequence = 0, stderr = '';
  const failAll = error => {
    for (const pending of waiting.values()) { clearTimeout(pending.timer); pending.reject(error); }
    waiting.clear();
  };
  processHandle.stderr.on('data', bytes => { stderr += bytes; });
  processHandle.on('error', failAll);
  processHandle.on('exit', code => { if (waiting.size) failAll(new Error(`Native bridge exited ${code}: ${stderr}`)); });
  createInterface({ input: processHandle.stdout }).on('line', line => {
    let response;
    try { response = JSON.parse(line); } catch { failAll(new Error('Invalid native response: ' + line)); return; }
    const pending = waiting.get(response.id); if (!pending) return;
    waiting.delete(response.id); clearTimeout(pending.timer);
    if (response.ok) pending.complete(response.result); else pending.reject(new Error(response.error));
  });
  const rpc = message => new Promise((complete, reject) => {
    const id = ++sequence, record = { id, action: message.action, method: message.method, scope: message.scope, started: Date.now() };
    requests.push(record);
    const timer = setTimeout(() => { waiting.delete(id); reject(new Error(`Native timeout: ${message.action}/${message.method}`)); }, 180000);
    waiting.set(id, { timer, reject, complete(result) { record.elapsed = Date.now() - record.started; complete(result); } });
    processHandle.stdin.write(JSON.stringify({ id, ...message }) + '\n');
  });
  const invoke = (method, payload = {}, scope = 'scene') => rpc({ action: 'invoke', scope, method, payload });
  await rpc({ action: 'reset' });
  return { rpc, invoke, dllDirectory, dllHash, importerHash, requests, async close() {
    try { await rpc({ action: 'exit' }); } finally { processHandle.stdin.end(); processHandle.kill(); }
  } };
}

export async function createPerforatedRectangularTube(bridge) {
  const rectangle = (width, height) => {
    const points = [[-width / 2, -height / 2], [width / 2, -height / 2], [width / 2, height / 2], [-width / 2, height / 2]];
    return { kind: 'path', closed: true, segments: points.map((start, index) => ({ kind: 'line', start, end: points[(index + 1) % 4] })) };
  };
  const profile = { schema: 'icax.imported-tube-profile', schemaVersion: 1, kind: 'fixed-section', profileForm: 'fixed',
    name: '真实开孔方管', width: 80, depth: 60, contours: [rectangle(80, 60), rectangle(74, 54)] };
  const sketch = { schema: 'icax.tube-sketch', schemaVersion: 1, kind: 'side', unit: 'mm', length: 400,
    faceHeight: 280, perimeter: 280, coordinateSpace: 'arc-length-axial', trajectoryWidth: 1,
    entities: [{ id: 'circle-hole', kind: 'circle', cx: 250, cy: 125, radius: 12, closed: true },
      { id: 'rectangle-hole', kind: 'rectangle', x: 240, y: 245, width: 20, height: 50, closed: true }] };
  const added = await bridge.invoke('AddNestingSideSketchPart', { profile, sketch, length: 400, quantity: 1, name: '方管圆孔与矩形孔' });
  const scene = await bridge.invoke('List');
  const part = scene.tubeDesigner.nestingGroups.flatMap(group => group.parts).find(part => part.entityId === added.partEntityId);
  if (!part) throw Error('Native scene did not persist the perforated tube');
  const geometry = await bridge.rpc({ action: 'geometry', payload: { partEntityId: part.entityId } });
  return { scene, part, geometry, sketch, profile };
}

export async function createCrossPeriodRoundedTube(bridge) {
  const descriptor = JSON.parse(readFileSync(resolve(repositoryRoot, 'src/apps/tube-designer/templates/profile/rect/profile.json'), 'utf8'));
  const parameters = { ...Object.fromEntries(descriptor.parameters.map(item => [item.key, item.defaultValue])), width: 60, depth: 40, wallThickness: 2, cornerRadius: 3, innerRadius: 1 };
  const payload = { profileRef: { scope: 'system', id: 'rect' }, parameters, length: 1000, quantity: 1, previewResourceKey: 'cross-period-rounded-edges' };
  const blank = await bridge.invoke('PreviewNestingSideSketchPart', payload);
  const period = blank.unfolding.surfaces.find(surface => !surface.inner).uPeriod;
  const sketch = { schema: 'icax.tube-sketch', schemaVersion: 1, kind: 'side', unit: 'mm', coordinateSpace: 'arc-length-axial', length: 1000, faceHeight: period, perimeter: period, trajectoryWidth: 1,
    entities: [{ id: 'corner-hole', kind: 'circle', cx: 54, cy: 200, radius: 20, closed: true },
      { id: 'period-ellipse', kind: 'ellipse', cx: period - 5, cy: 140, radiusX: 25, radiusY: 40, closed: true },
      { id: 'rectangle-hole', kind: 'rectangle', x: period + 10, y: 200, width: 50, height: 25, closed: true }] };
  const added = await bridge.invoke('AddNestingSideSketchPart', { ...payload, sketch, name: '跨周期椭圆圆角方管' });
  const scene = await bridge.invoke('List');
  const part = scene.tubeDesigner.nestingGroups.flatMap(group => group.parts).find(part => part.entityId === added.partEntityId);
  if (!part) throw Error('Native scene did not persist the crossed-period rounded tube');
  const geometry = await bridge.rpc({ action: 'geometry', payload: { partEntityId: part.entityId } });
  return { scene, part, geometry, sketch, profile: blank.profile };
}
