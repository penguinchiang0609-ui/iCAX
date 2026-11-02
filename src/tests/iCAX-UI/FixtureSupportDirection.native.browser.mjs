import assert from 'node:assert/strict';
import {verifyFixtureAutomaticFaces} from './FixtureAutomaticFaces.native.browser.mjs';
export async function verifyFixtureSupportDirection(args){
 const {page,calls}=args;
 await verifyFixtureAutomaticFaces({...args,beforeRecognition:async()=>{
  const menu=page.locator('.five-support-direction-menu');await menu.locator('summary').click();
  const field=k=>page.locator(`[data-five-fixture-field="design:${k}"]`),start=calls.length;
  await field('supportZ').fill('0');await page.locator('[data-five-command="five.fixture.design.auto"]').click();
  assert.match(await page.evaluate(()=>fixture.view.error),/零向量/);
  await menu.locator('summary').click();await field('supportX').fill('3');await field('supportY').fill('4');
  const direction=await page.evaluate(async()=>{const m=await import('/src/apps/five-axis-cam/webpage/fixtureDesign.mjs');return m.supportDirection(m.designDraft(fixture.view));});
  assert.deepEqual(direction,[.6,.8,0]);assert.equal(calls.slice(start).filter(c=>/CamFixture\.(FindSupportFaces|Generate|SetRecipe)/.test(c.method||'')).length,0);
  await field('supportX').fill('0');await field('supportY').fill('0');await field('supportZ').fill('2');
  await field('supportZ').press('Escape');assert.equal(await menu.getAttribute('open'),null);
 }});
}
