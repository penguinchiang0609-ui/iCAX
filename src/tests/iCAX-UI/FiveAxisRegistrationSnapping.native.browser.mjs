import assert from 'node:assert/strict';
import { resolve } from 'node:path';

export async function verifyRegistrationSnapping({ page, command, invoke, screenPoint, cancel, output }) {
  const before = await invoke('CamScene.ReadState'), evidence = [];
  const camera = await page.evaluate(() => fixture.view.viewport.getCameraState());
  await page.evaluate(() => { fixture.view.viewport.setStandardView('top'); window.snapCanvas = document.querySelector('canvas'); });
  const waitHover = async mode => {
    try { await page.waitForFunction(mode => fixture.view.fiveRegistrationInteraction?.hover?.query.mode === mode, mode, { timeout: 5000 }); }
    catch (error) {
      const diagnostic = await page.evaluate(async () => {
        const state = fixture.view.fiveRegistrationInteraction;
        try { return { result: await state.candidate?.promise, error: state.error }; }
        catch (error) { return { error: error.message, request: state.candidate?.request }; }
      });
      throw Error(`${error.message}\n${JSON.stringify(diagnostic)}`);
    }
  };
  const hover = () => page.evaluate(() => structuredClone(fixture.view.fiveRegistrationInteraction.hover));
  const move = p => page.mouse.move(p.x, p.y);
  const outside = async (p, distance) => {
    const edge = await screenPoint(p), center = await screenPoint([20, 30, 20]);
    const dx = edge.x - center.x, dy = edge.y - center.y, length = Math.hypot(dx, dy);
    return { x: edge.x + dx / length * distance, y: edge.y + dy / length * distance };
  };
  const rawHit = p => page.evaluate(async p => {
    const THREE = await import('/src/iCAX-UI/SDK/ThirdParty/three/three.module.js');
    const vp = fixture.view.viewport, rect = vp.renderer.domElement.getBoundingClientRect(), ray = new THREE.Raycaster();
    ray.setFromCamera(new THREE.Vector2((p.x - rect.x) / rect.width * 2 - 1, 1 - (p.y - rect.y) / rect.height * 2), vp.camera);
    return ray.intersectObjects([...vp.sceneObjects.values()], false).length;
  }, p);
  for (const zoom of [1, 1.65]) {
    await page.evaluate(zoom => fixture.view.viewport.fitView(zoom), zoom);
    for (const [method, mode, anchor, prefix] of [
      ['point', 'endpoint', [40, 0, 20], 'origin'],
      ['line', 'line', [40, 30, 20], 'axisX'],
      ['face', 'plane', [40, 30, 20], 'plane'],
    ]) {
      await command(`five.rough.${method}`);
      await page.getByRole('button', { name: '捕捉点', exact: true }).click();
      const far = await outside(anchor, 24), near = await outside(anchor, 8);
      assert.equal(await rawHit(near), 0, 'pointer is outside the mesh, not an ordinary face hit');
      await move(far);
      await page.waitForFunction(() => !fixture.view.fiveRegistrationInteraction?.hover);
      const start = Date.now(); await move(near); await waitHover(mode);
      const elapsed = Date.now() - start, candidate = await hover();
      assert.match(await page.locator('.five-reference-prompt').innerText(), /已吸附/);
      if (mode === 'endpoint') assert.ok(Math.hypot(...candidate.point.map((v, i) => v - anchor[i])) < 1e-6);
      if (zoom === 1) await page.screenshot({ path: resolve(output, `snap-${method}-outside.png`) });
      await page.mouse.click(near.x, near.y);
      await page.waitForFunction(prefix => {
        const draft = fixture.view.fiveAxisRegistrationPick?.draft ?? Object.values(fixture.view.fiveAxisUI.drafts).find(d => d.referenceResults?.[prefix]);
        return !fixture.view.pending && draft?.referenceResults?.[prefix];
      }, prefix);
      const picked = await page.evaluate(async prefix => (await import('/src/apps/five-axis-cam/webpage/workspaceModel.mjs')).currentDraft(fixture.view).referenceResults[prefix], prefix);
      // Ray unprojection after a camera update can differ at floating-point
      // roundoff; the CAD identity and geometry must remain the same.
      const sameFeature=(a,b,path='reference')=>{
        if(typeof a==='number'&&typeof b==='number'){assert.ok(Math.abs(a-b)<=1e-9*Math.max(1,Math.abs(a),Math.abs(b)),`${mode} ${path}: ${a} != ${b}`);return;}
        if(a&&b&&typeof a==='object'&&typeof b==='object'){assert.deepEqual(Object.keys(a).sort(),Object.keys(b).sort());for(const key of Object.keys(a))sameFeature(a[key],b[key],`${path}.${key}`);return;}
        assert.deepEqual(a,b);
      };
      // Probe distance and nearPoint describe the pointer query; compare the
      // exact snapped identity and geometry, not transient query diagnostics.
      for(const key of ['point','direction','faceId','edgeId','lineEnds','triangleRanges','geometryRef'])sameFeature(picked[key],candidate[key],key);
      assert.equal(picked.query.mode,candidate.query.mode);
      evidence.push({ zoom, method, mode, outsidePixels: 8, hoverMilliseconds: elapsed, point: picked.point });
      await cancel();
      assert.deepEqual(await invoke('CamScene.ReadState'), before, 'snap and Cancel do not modify the project');
    }
  }
  // A late native snap cannot replace a newer hover, resurrect Cancel, or pick
  // a stale endpoint after moving into blank space.
  await command('five.rough.point');
  await page.evaluate(() => { fixture.holdMethod = 'CamPlacement.ResolveReference'; fixture.hold = true; });
  await move(await outside([40, 0, 20], 8));
  await page.waitForFunction(() => fixture.held);
  const far = await outside([40, 0, 20], 24); await move(far);
  await page.evaluate(() => fixture.release());
  await page.waitForFunction(() => !fixture.held && !fixture.view.fiveRegistrationInteraction.busy);
  assert.equal(await hover(), null);
  await page.mouse.click(far.x, far.y);
  assert.equal(await page.evaluate(() => fixture.view.fiveAxisRegistrationPick.prefix), 'origin');
  await cancel();
  assert.equal(await page.evaluate(() => snapCanvas === document.querySelector('canvas')), true);
  assert.deepEqual(await invoke('CamScene.ReadState'), before);
  await page.evaluate(camera => fixture.view.viewport.setCameraState(camera), camera);
  return evidence;
}
